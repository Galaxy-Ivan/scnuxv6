// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(blockno) (blockno % NBUCKET)

extern uint ticks;

struct {
  struct spinlock lock;
  // struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache[NBUCKET];

struct buf buf[NBUF]; // 确保这行代码存在

struct spinlock bcache_lock;

extern char end[]; // in kernel.ld
void
binit(void)
{
  struct buf *b;

  initlock(&bcache_lock, "bcache");

  char buffer[11] = "bcache_000";
  for (int i = 0; i < NBUCKET; ++i) {
    int x = i;
    for (int j = 0; j < 3; ++j) {
      buffer[10 - j] = x % 10;
      x /= 10;
    }
    initlock(&bcache[i].lock, buffer);
    // 初始化链表头
    bcache[i].head.prev = &bcache[i].head;
    bcache[i].head.next = &bcache[i].head;
  }

for (b = buf; b < buf + NBUF; b++) {
    struct buf *head = &bcache[0].head;
    
    b->next = head->next;
    b->prev = head;
    initsleeplock(&b->lock, "buffer");
    
    head->next->prev = b;
    head->next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
/*
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
*/

// helped with Gemeni
static struct buf*
bget (uint dev, uint blockno) {
  struct buf *b;
  int id = HASH(blockno);

  acquire(&bcache[id].lock);

  for(b = bcache[id].head.next; b != &bcache[id].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache[id].lock);

  acquire(&bcache_lock);

  acquire(&bcache[id].lock);
  for (b = bcache[id].head.next; b != &bcache[id].head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[id].lock);
      release(&bcache_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // LRU victim
  struct buf *victim = 0;
  int victim_id = -1;
  uint min_tick = 0xffffffff;

  for (int i = 0; i < NBUCKET; ++i) {
    int holds_lock = (i == id); 
    
    if(!holds_lock) acquire(&bcache[i].lock);

    struct buf *p;
    for (p = bcache[i].head.next; p != &bcache[i].head; p = p->next) {
      if (p->refcnt == 0 && p->timestamp < min_tick) {
        min_tick = p->timestamp;
        victim = p;
        victim_id = i;
      }
    }

    if(!holds_lock) release(&bcache[i].lock);
  }

  if (victim) {
    if (victim_id != id) {
      acquire(&bcache[victim_id].lock);
      // 从旧桶移除
      victim->prev->next = victim->next;
      victim->next->prev = victim->prev;
      release(&bcache[victim_id].lock);
      // 插入到新桶
      victim->next = bcache[id].head.next;
      victim->prev = &bcache[id].head;
      bcache[id].head.next->prev = victim;
      bcache[id].head.next = victim;
    }
    
    // 重置 buffer
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    
    release(&bcache[id].lock);
    release(&bcache_lock);
    acquiresleep(&victim->lock);
    return victim;
  }
  // 没有空闲 buffer，抛异常
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = HASH(b->blockno);

  acquire(&bcache[id].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks; // 更新时间戳，用于LRU
  }
  release(&bcache[id].lock);
}

void
bpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache[id].lock);
  b->refcnt++;
  release(&bcache[id].lock);
}

void
bunpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache[id].lock);
  b->refcnt--;
  release(&bcache[id].lock);
}



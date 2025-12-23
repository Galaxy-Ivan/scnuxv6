// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h" // ref need

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct {
  struct spinlock lock; // 添加一个锁，避免多 CPU 产生的错误
  int count[PHYSTOP / PGSIZE];
} ref;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  for (int i = 0; i < PHYSTOP / PGSIZE; ++i)
    ref.count[i] = 1; // 初始化为 1，允许后面 kfree 初始化时，自动将其减一丢进可用内存链表
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  // 先行判断，确保 pa 合法并对齐
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&ref.lock);
  int refid = (uint64)pa / PGSIZE; // ensured (pa % PGSIZE == 0)
  if (ref.count[refid] <= 0) 
    panic("kfree");
  if (--ref.count[refid] != 0) {
    release(&ref.lock);
    return;
  }
  release(&ref.lock);
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  acquire(&ref.lock);
  ref.count[(uint64)r / PGSIZE] = 1; // new ref page assigned
  release(&ref.lock);
  return (void*)r;
}

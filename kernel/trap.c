#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  // 正如笔者所说，xv6 对异常的处理相当‘无趣’，只要内核发生了 re，那就直接报 panic
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter. 保存用户程序的断点
  p->trapframe->epc = r_sepc();
  
  // scause == 8，是主动抛出的异常，目的是执行系统调用
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4; // 完事了之后去执行下一条指令

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on(); // 在保存了寄存器之后，才允许中断

    syscall(); // 然后交给系统调用去处理
  }
  else if(r_scause() == 13 || r_scause() == 15) { // scause() == 13 || 15, 是页面错误
    // printf("scause=%d stval=%p\n", r_scause(), r_stval());
    // uint64 upsz = PGROUNDUP(r_stval());
    uint64 downsz = PGROUNDDOWN(r_stval());
    if (r_stval() >= p->sz) { // 如果确实是越界了
      // printf("dbg1 %p %p\n", newsz, myproc()->sz);
      // printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      // printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
      goto end;
    } // else if (r_stval() < PGROUNDDOWN(p->trapframe->sp)) {
    //   p->killed = 1;
    //   goto end;
    // }
    // duel with [remap]
    pte_t *pte = walk(p->pagetable, downsz, 0);
    if (pte != 0 && (*pte & PTE_V)) {
      // printf("dbgA\n");
      // 如果已经有有效映射了，说明这不是缺页问题，而是权限问题（如向只读页写入）
      p->killed = 1;
      goto end;
    }

    // 哪里亮了点哪里，我不需要关心 oldsz 是多少，我只需要为当前位置申请内存
    char *mem = kalloc();
    if (mem == 0) { // 没申请到
      // printf("dbg2\n");
      // printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      // printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
      goto end;
    }
    if (mappages(p->pagetable, downsz, PGSIZE, (uint64)mem,
                PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
      kfree(mem);
      printf("dbg3\n");
      // printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      // printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
      goto end;
    }
    memset(mem, 0, PGSIZE); // 申请成功，将这一段内存清空，避免读取到之前程序留下的敏感信息
end:;
  }
  else if((which_dev = devintr()) != 0){ // 顺便获取 which_dev
    // ok 如果是从外面进来的，设备中断，CPU 中断啥的，那就是别人已经处理好的了，那就不管它
  } else { // 不然就是 RE 了
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt. 时钟中断
  if(which_dev == 2)
    yield();

  usertrapret(); // 将保存的寄存器恢复，退出中断处理
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}


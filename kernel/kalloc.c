// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PG2REFIDX(_pa) ((((uint64)_pa) - KERNBASE) / PGSIZE)
#define MX_REFIDX PG2REFIDX(PHYSTOP)
#define PG_REFCNT(_pa) kmem_pgref.cntarray[PG2REFIDX((_pa))]

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int cntarray[MX_REFIDX];  //保存<free memory>中每一个物理内存页的引用计数
} kmem_pgref;


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kmem_pgref.lock, "kmem_pgref");
  freerange(end, (void*)PHYSTOP);  //释放<free memory>
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

  // 我认为这里有些许不足：应当判断一下pa是否为空闲页，释放一个本就空闲的页是不被允许的
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem_pgref.lock);
  if(--PG_REFCNT(pa) <= 0)  //若引用计数已减为0
  {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    // 将该页重新插入空闲页链表（头插）
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&kmem_pgref.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void*
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    // Fill with junk.
    memset((char*)r, 5, PGSIZE);

    // Init page reference count.
    acquire(&kmem_pgref.lock);
    PG_REFCNT((void*)r) = 1;
    release(&kmem_pgref.lock);
  }

  return (void*)r;
}

// 累积物理内存页pa的引用计数
void
pgref_accum(void *pa)
{
  acquire(&kmem_pgref.lock);
  PG_REFCNT(pa)++;
  release(&kmem_pgref.lock);
}

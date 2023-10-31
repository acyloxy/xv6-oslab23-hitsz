// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end, uint32 cpuid);

void kfree0(void *pa, uint32 cpuid);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock locks[CPUS];
  struct spinlock borrow_mutex;
  struct run *freelists[CPUS];
} kmem;

void
kinit()
{
  uint64 base = PGROUNDUP((uint64)end);
  uint64 range = PGROUNDDOWN((PHYSTOP - (uint64)end) / CPUS);
  for (int i = 0; i < CPUS; i++) {
    initlock(&kmem.locks[i], "kmem");
    if (i == CPUS - 1) {
      freerange((void *)(base + range * i), (void *)PHYSTOP, i);
    } else {
      freerange((void *)(base + range * i), (void *)(base + range * (i + 1)), i);
    }
  }
}

void
freerange(void *pa_start, void *pa_end, uint32 cpuid)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree0(p, cpuid);
}

void
kfree0(void *pa, uint32 cpuid)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.locks[cpuid]);
  r->next = kmem.freelists[cpuid];
  kmem.freelists[cpuid] = r;
  release(&kmem.locks[cpuid]);
}

void
kfree(void *pa)
{
  kfree0(pa, cpuid());
}

void *
kalloc(void)
{
  struct run *r;
  uint64 cid = cpuid();

  acquire(&kmem.locks[cid]);
  r = kmem.freelists[cid];
  if(r)
    kmem.freelists[cid] = r->next;
  else {
    // borrow from other harts
    release(&kmem.locks[cid]);
    acquire(&kmem.borrow_mutex);
    acquire(&kmem.locks[cid]);
    for (int i = 0; i < CPUS; i++) {
      if (i == cid) {
        continue;
      }
      acquire(&kmem.locks[i]);
      r = kmem.freelists[i];
      if (r) {
        kmem.freelists[i] = r->next;
        release(&kmem.locks[i]);
        break;
      }
      release(&kmem.locks[i]);
    }
    release(&kmem.borrow_mutex);
  }
  release(&kmem.locks[cid]);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

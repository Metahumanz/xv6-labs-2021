// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");

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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r = 0;

  push_off();
  int id = cpuid();

  // 先从当前CPU自己的freelist取。
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // 当前CPU没有空闲页，从其他CPU偷。
  if(r == 0){
    for(int i = 0; i < NCPU; i++){
      if(i == id)
        continue;

      acquire(&kmem[i].lock);

      if(kmem[i].freelist){
        struct run *head = kmem[i].freelist;
        struct run *slow = head;
        struct run *fast = head;
        struct run *prev = 0;

        // 找到链表中点，大约偷一半。
        while(fast && fast->next){
          prev = slow;
          slow = slow->next;
          fast = fast->next->next;
        }

        if(prev == 0){
          // 对方只有一个空闲页。
          kmem[i].freelist = 0;
        } else {
          // 前半部分偷走，后半部分留给对方。
          prev->next = 0;
          kmem[i].freelist = slow;
        }

        release(&kmem[i].lock);

        // 偷来的一批中，第一张直接返回，
        // 剩下的保存到当前CPU自己的freelist。
        r = head;
        struct run *rest = r->next;

        if(rest){
          acquire(&kmem[id].lock);
          prev->next = kmem[id].freelist;
          kmem[id].freelist = rest;
          release(&kmem[id].lock);
        }

        break;
      }

      release(&kmem[i].lock);
    }
  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE);

  return (void*)r;
}

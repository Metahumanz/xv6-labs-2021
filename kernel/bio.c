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

struct {
  // 只用于串行化cache miss和buffer淘汰。
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct {
    struct spinlock lock;
    struct buf head;
  } bucket[NBUCKET];
} bcache;

static int
bhash(uint blockno)
{
  return blockno % NBUCKET;
}

static void
bremove(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
}

static void
binsert(int i, struct buf *b)
{
  b->next = bcache.bucket[i].head.next;
  b->prev = &bcache.bucket[i].head;
  bcache.bucket[i].head.next->prev = b;
  bcache.bucket[i].head.next = b;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];

    initsleeplock(&b->lock, "buffer");

    b->dev = (uint)-1;
    b->blockno = 0;
    b->valid = 0;
    b->refcnt = 0;
    b->timestamp = 0;

    // 初始化时把空buffer均匀分到各个bucket。
    binsert(i % NBUCKET, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *victim;
  uint oldest;
  int victim_bucket;
  int h = bhash(blockno);

  // Fast path：只访问目标bucket。
  acquire(&bcache.bucket[h].lock);

  for(b = bcache.bucket[h].head.next;
      b != &bcache.bucket[h].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[h].lock);

      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[h].lock);

  // Cache miss。
  // 只允许一个CPU进行buffer淘汰和身份变化。
  acquire(&bcache.lock);

  // 刚才释放目标bucket后，
  // 可能已经有另一个miss把这个block放进来了。
  acquire(&bcache.bucket[h].lock);

  for(b = bcache.bucket[h].head.next;
      b != &bcache.bucket[h].head;
      b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;

      release(&bcache.bucket[h].lock);
      release(&bcache.lock);

      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[h].lock);

retry:
  victim = 0;
  oldest = 0;
  victim_bucket = -1;

  // 一个bucket一个bucket地检查，
  // 不再同时持有所有bucket锁。
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.bucket[i].lock);

    for(b = bcache.bucket[i].head.next;
        b != &bcache.bucket[i].head;
        b = b->next){

      if(b->refcnt == 0){
        if(victim == 0 || b->timestamp < oldest){
          victim = b;
          oldest = b->timestamp;
          victim_bucket = i;
        }
      }
    }

    release(&bcache.bucket[i].lock);
  }

  if(victim == 0){
    release(&bcache.lock);
    panic("bget: no buffers");
  }

  /*
   * 扫描结束以后victim可能被普通cache hit抢走，
   * 所以必须重新拿victim所在bucket的锁确认。
   *
   * 由于我们一直持有bcache.lock，
   * 不会有另一个cache miss把它移到别的bucket。
   */
  acquire(&bcache.bucket[victim_bucket].lock);

  if(victim->refcnt != 0){
    release(&bcache.bucket[victim_bucket].lock);
    goto retry;
  }

  // 从原bucket摘下来。
  bremove(victim);

  release(&bcache.bucket[victim_bucket].lock);

  /*
   * 插入目标bucket。
   * 此时仍然持有bcache.lock，
   * 因而不会有另一个miss同时创建相同的block。
   */
  acquire(&bcache.bucket[h].lock);

  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;

  binsert(h, victim);

  release(&bcache.bucket[h].lock);
  release(&bcache.lock);

  acquiresleep(&victim->lock);
  return victim;
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
  uint now;
  int h;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);

  h = bhash(b->blockno);

  acquire(&bcache.bucket[h].lock);

  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = now;

  release(&bcache.bucket[h].lock);
}

void
bpin(struct buf *b)
{
  int h = bhash(b->blockno);

  acquire(&bcache.bucket[h].lock);
  b->refcnt++;
  release(&bcache.bucket[h].lock);
}

void
bunpin(struct buf *b)
{
  uint now;
  int h = bhash(b->blockno);

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);

  acquire(&bcache.bucket[h].lock);

  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = now;

  release(&bcache.bucket[h].lock);
}



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

struct {
  struct spinlock locks[NBUFBUCKET];
  struct spinlock borrow_mutex;
  struct buf buf[NBUF];
  struct buf heads[NBUFBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b, *e;
  uint m = NBUF / NBUFBUCKET;
  uint n = NBUF % NBUFBUCKET;
  uint buf_begin, buf_end = 0;

  for (int i = 0; i < NBUFBUCKET; i++) {
    initlock(&bcache.locks[i], "bcache");
    struct buf *head = &bcache.heads[i];
    buf_begin = buf_end;
    buf_end = buf_begin + m + (i < n ? 1 : 0);

    // Create linked list of buffers
    head->prev = head;
    head->next = head;
    for(b = bcache.buf + buf_begin, e = bcache.buf + buf_end; b < e ; b++){
      b->next = head->next;
      b->prev = head;
      initsleeplock(&b->lock, "buffer");
      head->next->prev = b;
      head->next = b;
    }
  }
  initlock(&bcache.borrow_mutex, "bcache");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bucket_idx = blockno % NBUFBUCKET;
  struct spinlock *lock = &bcache.locks[bucket_idx];
  struct buf *head = &bcache.heads[bucket_idx];

  acquire(lock);

  // Is the block already cached?
  for(b = head->next; b != head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = head->prev; b != head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // We need borrowing from other buckets.
  acquire(&bcache.borrow_mutex);
  for (int i = 0; i < NBUFBUCKET; i++) {
    if (i == bucket_idx) {
      continue;
    }
    struct spinlock *peer_lock = &bcache.locks[i];
    struct buf *peer_head = &bcache.heads[i];
    if (peer_lock->locked) {
      continue;
    }
    acquire(peer_lock);
    for (b = peer_head->prev; b != peer_head; b = b->prev) {
      if (b->refcnt == 0) {
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = head->next;
        b->prev = head;
        head->next->prev = b;
        head->next = b;
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(peer_lock);
        release(&bcache.borrow_mutex);
        release(lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(peer_lock);
  }
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

  uint bucket_idx = b->blockno % NBUFBUCKET;
  struct spinlock *lock = &bcache.locks[bucket_idx];
  struct buf *head = &bcache.heads[bucket_idx];

  acquire(lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = head->next;
    b->prev = head;
    head->next->prev = b;
    head->next = b;
  }

  release(lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno % NBUFBUCKET]);
  b->refcnt++;
  release(&bcache.locks[b->blockno % NBUFBUCKET]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.locks[b->blockno % NBUFBUCKET]);
  b->refcnt--;
  release(&bcache.locks[b->blockno % NBUFBUCKET]);
}



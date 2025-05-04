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
  struct spinlock lock; //自旋锁
  struct buf buf[NBUF]; //固定大小 固定NBUF数目的缓冲区

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  /*缓冲区缓存的头部节点，用于维护一个双向链表。
  这个链表通过 buf 结构体中的 prev 和 next 指针连接所有的缓冲区。
  链表中的缓冲区按照最近使用的时间顺序排序，
  head.next 指向最近最常使用的缓冲区，而 head.prev 指向最近最少使用的缓冲区。*/
  struct buf head;  //
} bcache;

/*初始化磁盘缓冲区缓存：
·初始化自旋锁
·初始化缓冲区的双向链表*/
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers.
  //将 bcache.head的前后指针都指向自身，形成一个初始的空链表。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  //遍历缓冲区数组的每个缓冲区
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    //将遍历的缓冲区插入到链表头部 然后更新相关节点的前后指针
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  /*注意初始化后 所有缓冲区组织到双向链表中
  但是缓冲区都处于未使用状态（valid/disk等标志位为0*
  使用时根据磁盘块好查找对应的缓冲区 若未被使用 则从链表中移除使用
  若已经使用 等待期释放
  需要淘汰缓冲区 则选择链表尾部（最近使用最少的 )进行替换*/
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
/*用于获取一个指定设备和块号的磁盘块缓冲区
在缓冲区中查找现有的缓冲区 
如果没有找到则分配一个新的缓冲区 并返回锁定的缓冲区*/
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);//获取自旋锁

  // Is the block already cached?
  //遍历缓冲区缓存链表
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    //设备号和块号匹配上
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;//引用计数+1
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;//获取睡眠锁并返回该缓冲区b
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  //如果没有匹配的 则需要分配一个新的缓冲区 分配就是从尾部开始遍历
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    //从尾部开始 找到引用计数为0的 
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;//数据有效位为0 表示数据无效 需要重新从磁盘读取
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);//获取睡眠锁并返回该缓冲区b
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  //调用bget()为给定扇区获取缓冲区
  b = bget(dev, blockno);
  //如果valid为0 说明缓冲区的数据无效（比如已经过期或者没数据 总之就是要重新从磁盘去读取）
  if(!b->valid) {
    //调用该读写函数 从磁盘读取数据到缓冲区b
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  //检查锁状态 确保持有该缓冲区的睡眠锁后才进行磁盘写入
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

  releasesleep(&b->lock);//释放时不再读写 所以释放睡眠锁？

  acquire(&bcache.lock);
  b->refcnt--;
  //引用计数为0时执行释放操作
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    //下边就是把这个释放的缓冲区插到链表头部去 
    //刚释放 说明是最近使用的 所以插到头部
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}



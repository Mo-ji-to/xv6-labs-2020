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
} kmem[NCPU];  //每个CPU分配独立的freelist 多个CPU并发分配物理内存 不会相互竞争

char* kmem_lock_names[] = {
  "kmem_cpu_0",
  "keme_cpu_1",
  "kmem_cpu_2",
  "keme_cpu_3",
  "kmem_cpu_4",
  "keme_cpu_5",
  "kmem_cpu_6",
  "keme_cpu_7",
};

void
kinit()
{
  //为所有锁初始化以“kmem”开头的名称，该函数只会被一个CPU调用
  for(int i = 0;i < NCPU;++i){
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
  //freerange调用kfree将所有空闲内存挂在该CPU的空闲列表上
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
// 翻译：释放物理内存页面，该页面通常应由kalloc()函数返回。
// 如果在初始化分配器时，这是一个例外。（见上面的kinit函数）
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
  //获取cpu编号 获取cpuid前必须关闭中断 否则获取途中一旦中断发生 cpu切换到另一个进程或CPU
  //那么返回的值将会不是当前的cpu
  int cpu = cpuid();  

  //释放锁 其实就是把空闲物理页从头插入 freelist
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();//重新打开中断
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

/*kalloc分配内存时 可能会出现当前CPU没有freelist的情况 也就是该CPU空闲内存不足
需要从其他CPU偷内存页 涉及到共享数据的修改
所以分配时需要加锁 偷页时也需要加锁*/
void *
kalloc(void)
{
  struct run *r;

  push_off();   //关闭中断

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);

  if(!kmem[cpu].freelist){  // 如果当前CPU已经没有freelist了 去其他cpu偷内存页
    int steal_left = 64;    //指定偷64个内存页
    for(int i = 0;i < NCPU;++i){
      if(i == cpu)
        continue;   //跳过当前CPU

      acquire(&kmem[i].lock);
      if(!kmem[i].freelist){    //如果在想要偷页的cpu也没有freelist了 则释放锁跳过
        release(&kmem[i].lock);
        continue;
      }
      
      struct run* rr = kmem[i].freelist;//rr 一开始指向kmem[i].freelist的第一个节点
      while(rr && steal_left){//循环将kmem[i]的freelist的第一个节点 移动到当前cpu kmem[cpu]的freelist头部
        kmem[i].freelist = rr->next; //将kmem[i]的自由链表的头指针移到rr的下一个节点 相当于移除了头节点
        rr->next = kmem[cpu].freelist; //将rr指向的节点的next指针指向kmem[cpu]的自由链表的当前头节点 确保链表的连接关系
        kmem[cpu].freelist = rr;//将rr指向的节点（原先kmem[i]的头结点）设置为 kmem[cpu] 的自由链表的新头节点
        rr = kmem[i].freelist;//更新rr为kmem[i].freelist的新的头节点 
        steal_left--;
      }

      release(&kmem[i].lock);

      if(steal_left == 0) //偷到指定页数后退出循环
        break;
    }
  }

  //如果足够 或者是 偷完后足够
  //从freelist上取出一个物理页r去分配
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  pop_off();    //打开中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

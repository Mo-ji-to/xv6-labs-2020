// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#ifdef LAB_LOCK
#define NLOCK 500

static struct spinlock *locks[NLOCK];
struct spinlock lock_locks;

void
freelock(struct spinlock *lk)
{
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if(locks[i] == lk) {
      locks[i] = 0;
      break;
    }
  }
  release(&lock_locks);
}

static void
findslot(struct spinlock *lk) {
  acquire(&lock_locks);
  int i;
  for (i = 0; i < NLOCK; i++) {
    if(locks[i] == 0) {
      locks[i] = lk;
      release(&lock_locks);
      return;
    }
  }
  panic("findslot");
}
#endif

// 对自旋锁的初始化
// 给锁起了个名字，并且将锁中的标志位置为空
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
#ifdef LAB_LOCK
  lk->nts = 0;
  lk->n = 0;
  findslot(lk);
#endif  
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// 译：获取锁
// 在原地循环直到锁被获取
void
acquire(struct spinlock *lk)
{
  // 调用push_off关闭中断，增加迭代深度
  push_off(); // disable interrupts to avoid deadlock.

  // 如果已经持有了要获取的锁，则内核陷入panic
  // Xv6不允许可重入锁(re-entrant lock)
  if(holding(lk))
    panic("acquire");

#ifdef LAB_LOCK
    __sync_fetch_and_add(&(lk->n), 1);
#endif      

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)

  /*原子命令  amoswap.w.aq 原子交换字大小的变量值
  也就是说 __sync_lock_test_and_set不断将1和lk->locked交换 返回值是&lk->locked交换前的值
  如果交换前就是0 那么说明锁没人用 交换后lk->locked为1 成功获得锁
  交换前&lk->locked为1 锁有人用 和1交换后没变化 继续循环*/
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0) {
#ifdef LAB_LOCK
    __sync_fetch_and_add(&(lk->nts), 1);
#else
   ;
#endif
  }

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  // 译：告知C编译器和处理器，不要让load/store命令随意翻越此处!!!
  // 这是为了保证对临界区内存的访问严格发生在锁被获取之后
  // 在RISC-V架构上，这会生成一个fence指令
  // fence指令将会保证位于这条指令前后的访存指令，都互相不越界!!!

  /*因为CPU和编译器在运行时 有时为了提升指令执行速度，会对指令执行顺序重新做排序
  这种规则叫访存模型，因为这个原因有时即是正确安排了锁次序也会出现错误
  所以需要确保内存的访问必须在获取锁之后*/
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  // 译：将当前获取锁的PCU信息记录在锁结构体中，方便调试
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  // 如果没有持有锁，那么没必要释放
  // 这是一个错误，内核会陷入panic
  if(!holding(lk))
    panic("release");

  // 首先将锁中的CPU信息清除
  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  //原因同acquire中的解释  需要一个fence指令 确保对临界区的加载严格发生在释放锁之前
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
   // 译：释放锁，等同于lk->locked = 0
  // 这里的代码没有使用C语言里的赋值运算符，因为C标准会用
  // 多个store指令来实现一个赋值操作
  // 在RISC-V中，sync_lock_release则会变成一个原子交换操作amoswap.w
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
/*译:
push_off 和 pop_off 就像 intr_off() 和 intr_on() 一样，
只不过它们是配对使用的：需要两次 pop_off() 才能抵消两次 push_off() 的操作。
而且，如果最初中断是关闭的，那么执行 push_off 和 pop_off 后，中断仍然保持关闭状态。
psuh_off对应intr_off  push_on对应intr_on*/

/*这么做的目的是因为，如果一个代码路径上多次获取和释放了不同的自旋锁!!!形成了锁链!!!
那么开关中断动作也随着这加锁、解锁形成了多次嵌套（xv6在申请锁之前必须关中断）
因此我们用noff和intena这两个变量记录这些嵌套操作的深度和进入锁链前CPU的中断开关情况
一旦进入锁链 中断必须关闭； 逐层从锁链退出后 必须恢复到进入锁链前的中断状态*/

void
push_off(void)
{
  // 获取当前CPU的中断开关状态
  int old = intr_get();

  // 无论如何，都关闭中断
  // 事实上在初次获取到锁之后，中断就已经是关闭状态了
  intr_off();

  // 如果是刚刚进入锁链
  // 就将进入锁链之前的中断状态，也就是上面的old
  // 保存在intena中
  if(mycpu()->noff == 0)
    mycpu()->intena = old;

  // 嵌套深度+1
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  // 获取当前的CPU
  struct cpu *c = mycpu();

  // 错误情况检测与判断：
  // 1.如果中断没关，这是不符合常理的，处于锁链之中的CPU中断一定是关掉的
  // 2.中断嵌套深度<1(也就是0)，说明已经退出锁链，没有理由再调用pop_off
  // 两种情况之一发生，内核陷入panic
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");

  //嵌套深度-1
  c->noff -= 1;

  // 如果当前已经完全退出锁链，且未进入锁链时中断打开
  // 则恢复CPU在进入锁链之前的开中断状态
  if(c->noff == 0 && c->intena)
    intr_on();
}

#ifdef LAB_LOCK
int
snprint_lock(char *buf, int sz, struct spinlock *lk)
{
  int n = 0;
  if(lk->n > 0) {
    n = snprintf(buf, sz, "lock: %s: #fetch-and-add %d #acquire() %d\n",
                 lk->name, lk->nts, lk->n);
  }
  return n;
}

int
statslock(char *buf, int sz) {
  int n;
  int tot = 0;

  acquire(&lock_locks);
  n = snprintf(buf, sz, "--- lock kmem/bcache stats\n");
  for(int i = 0; i < NLOCK; i++) {
    if(locks[i] == 0)
      break;
    if(strncmp(locks[i]->name, "bcache", strlen("bcache")) == 0 ||
       strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0) {
      tot += locks[i]->nts;
      n += snprint_lock(buf +n, sz-n, locks[i]);
    }
  }
  
  n += snprintf(buf+n, sz-n, "--- top 5 contended locks:\n");
  int last = 100000000;
  // stupid way to compute top 5 contended locks
  for(int t = 0; t < 5; t++) {
    int top = 0;
    for(int i = 0; i < NLOCK; i++) {
      if(locks[i] == 0)
        break;
      if(locks[i]->nts > locks[top]->nts && locks[i]->nts < last) {
        top = i;
      }
    }
    n += snprint_lock(buf+n, sz-n, locks[top]);
    last = locks[top]->nts;
  }
  n += snprintf(buf+n, sz-n, "tot= %d\n", tot);
  release(&lock_locks);  
  return n;
}
#endif

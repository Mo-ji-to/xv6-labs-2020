#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Possible states of a thread: */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4


//定义存储用户线程上下文的结构体
struct tcontext {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};


struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct     tcontext context;  /*用户进程上下文*/

};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;
extern void thread_switch(uint64, uint64);
              
void 
thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule().  it needs a stack so that the first thread_switch() can
  // save thread 0's state.  thread_schedule() won't run the main thread ever
  // again, because its state is set to RUNNING, and thread_schedule() selects
  // a RUNNABLE thread.
  //翻译：
  // main() 是线程 0，它将首次调用 thread_schedule()。为了使第一次调用 thread_switch() 能够保存线程 0 的状态，它需要一个栈。
  // thread_schedule() 不会再次运行主线程，因为主线程的状态被设置为 RUNNING，而 thread_schedule() 会选择一个状态为 RUNNABLE 的线程。
  //也就是说 设置.c文件的main函数为第一个线程，之后不在调度，而是执行线程abc
  //因为它不会将状态标记为RUNNABLE
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* Find another runnable thread. 
  从当前进程t的下一个位置开始遍历所有线程
  直到找到下一个可运行线程
  next_thread用来存储可运行线程
  */
  next_thread = 0;
  t = current_thread + 1;
  //遍历线程数组 找到可运行线程
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  //如果一个可运行线程都没有了  则退出uthread程序
  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }
  //只有新线程与当前线程不同时才切换
  if (current_thread != next_thread) {         /* switch threads?  */
    //接下来就是线程切换 将新进程标记为running
    //更新当前线程为新线程next_thread
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
     //调用切换函数   完成用户态线程的切换
     thread_switch((uint64)&t->context, (uint64)&current_thread->context);
  } else
    next_thread = 0;
}

//创建一个线程的函数
void 
thread_create(void (*func)())
{
  struct thread *t;

  //找到空闲线程
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  //标记该空闲线程为可调度状态
  t->state = RUNNABLE;
  // YOUR CODE HERE
  //为线程分配资源 包括返回地址和栈
  t->context.ra = (uint64)func;                   // 设定函数返回地址
  t->context.sp = (uint64)t->stack + STACK_SIZE;  // 设定栈指针
}

void 
thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void 
thread_a(void)
{
  int i;
  //将a_started设置为1，表示线程thread_a已经启动
  printf("thread_a started\n");
  a_started = 1;

  //线程thread_a会不断调用thread_yield()，释放CPU，直到b_started和c_started都为1
  while(b_started == 0 || c_started == 0)
    thread_yield();
  
  //循环记录线程a执行次数  执行一次后主动让出cpu
  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);
  //执行100次后 将该线程设置为空闲 表示线程已经结束了 可以被其他线程复用
  current_thread->state = FREE;
  //调用schedule主动让出CPU
  thread_schedule();
}

void 
thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while(a_started == 0 || c_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void 
thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while(a_started == 0 || b_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int 
main(int argc, char *argv[]) 
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  thread_schedule();
  exit(0);
}

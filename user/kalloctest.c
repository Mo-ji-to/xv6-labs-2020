#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NCHILD 2
#define N 100000
#define SZ 4096

void test1(void);
void test2(void);
char buf[SZ];

int
main(int argc, char *argv[])
{
  test1();
  test2();
  exit(0);
}


/*
从buf缓冲区中提取一个整数值
并选择是否打印缓冲区的内容
*/
int ntas(int print)
{
  int n;
  char *c;

  //检查缓冲区buf是否有统计数据
  if (statistics(buf, SZ) <= 0) {
    fprintf(2, "ntas: no stats\n");
  }
  //从buf中查找第一个等于号=的位置
  c = strchr(buf, '=');
  //从等于号后面第二个字符开始，将字符串转换为整数
  n = atoi(c+2);
  //如果print不等于0 则打印缓冲区内容
  if(print)
    printf("%s", buf);
  //返回提取的整数值
  return n;
}

/*
用来验证sbrk内存分配和释放的准确性
创建多个子进程 并对每个子进程进行一定次数的内存分配和释放操作 检查统计结果*/
void test1(void)
{
  void *a, *a1;
  int n, m;
  printf("start test1\n"); 
  //获取初始的资源统计值m(如内存页数量) 不打印
  m = ntas(0);
  //创建NCHILD个子进程
  for(int i = 0; i < NCHILD; i++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(-1);
    }
    //每个子进程循环执行内存分配和释放的操作N次
    if(pid == 0){
      for(i = 0; i < N; i++) {
        /*分配4KB内存 返回的是分配前的起始地址a*/
        a = sbrk(4096);
        //写入数据1 确保内存被实际分配（避免因为lazy allocation引起的延迟分配）
        *(int *)(a+4) = 1;
        //释放刚刚分配的4KB内存 返回的是释放前的起始地址a1
        a1 = sbrk(-4096);
        //a1和a地址差了一个物理页 
        if (a1 != a + 4096) {
          printf("wrong sbrk\n");
          exit(-1);
        }
      }
      exit(-1);
    }
  }
  //父进程等待子进程结束
  for(int i = 0; i < NCHILD; i++){
    wait(0);
  }
  printf("test1 results:\n");
  //获取最终的资源统计值n
  n = ntas(1);
  //检查资源统计值差是否小于10
  if(n-m < 10) 
    printf("test1 OK\n");
  else
    printf("test1 FAIL\n");
}

//
// countfree() from usertests.c
//
/*
计算当前系统中可用的内存页面数量。
使用sbrk函数不断尝试分配内存，直到分配失败为止。
在分配过程中，修改分配到的内存以确保其被实际分配。
最后释放分配的内存并返回分配的内存页面数量。
*/
int
countfree()
{
  uint64 sz0 = (uint64)sbrk(0);
  int n = 0;

  while(1){
    uint64 a = (uint64) sbrk(4096);
    if(a == 0xffffffffffffffff){
      break;
    }
    // modify the memory to make sure it's really allocated.
    *(char *)(a + 4096 - 1) = 1;
    n += 1;
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}

void test2() {
  int free0 = countfree();
  int free1;
  int n = (PHYSTOP-KERNBASE)/PGSIZE;
  printf("start test2\n");  
  printf("total free number of pages: %d (out of %d)\n", free0, n);
  if(n - free0 > 1000) {
    printf("test2 FAILED: cannot allocate enough memory");
    exit(-1);
  }
  for (int i = 0; i < 50; i++) {
    free1 = countfree();
    if(i % 10 == 9)
      printf(".");
    if(free1 != free0) {
      printf("test2 FAIL: losing pages\n");
      exit(-1);
    }
  }
  printf("\ntest2 OK\n");  
}



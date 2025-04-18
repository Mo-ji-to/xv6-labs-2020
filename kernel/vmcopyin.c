#include "param.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

//
// This file contains copyin_new() and copyinstr_new(), the
// replacements for copyin and coyinstr in vm.c.
//

static struct stats {
  int ncopyin;
  int ncopyinstr;
} stats;

int
statscopyin(char *buf, int sz) {
  int n;
  n = snprintf(buf, sz, "copyin: %d\n", stats.ncopyin);
  n += snprintf(buf+n, sz, "copyinstr: %d\n", stats.ncopyinstr);
  return n;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  struct proc *p = myproc();

  if (srcva >= p->sz || srcva+len >= p->sz || srcva+len < srcva)
    return -1;
  memmove((void *) dst, (void *)srcva, len);
  stats.ncopyin++;   // XXX lock
  return 0;
}

// Copy a null-terminated string from user to kernel.拷贝字符串 从user到kernel
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
/*
@params：
用户进程页表pagetable_t pagetable, 
指向内核空间中用于存放复制字符串的目的地址char *dst, 
用户空间中字符串的起始虚拟地址uint64 srcva, 
最多复制的字节数uint64 max
*/
int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  struct proc *p = myproc();//获取当前进程控制块指针
  //将用户空间字符串的起始虚拟地址（srcva）强制转换为字符指针类型，方便后续按字节访问内存
  char *s = (char *) srcva;//这一步就是那个解引用的过程？
  
  stats.ncopyinstr++;   // 用于记录 copyinstr_new 函数的调用次数   多线程环境下需要加锁确保一致性XXX lock
  //循环 逐字节复制字符串 条件是复制的字节数不大于max 且 起始虚拟地址加上偏移量（i）的大小小于进程大小p->sz
  for(int i = 0; i < max && srcva + i < p->sz; i++){
    dst[i] = s[i];//用户空间地址s+i处的字符复制到 内核空间dst[i]中
    if(s[i] == '\0')
      return 0;
  }
  return -1;
}

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

#include "spinlock.h"
#include "proc.h"


/*最核心的函数是walk和mappages，前者为虚拟地址找到PTE，后者为新映射装载PTE。
名称以kvm开头的函数操作内核页表；以uvm开头的函数操作用户页表；其他函数用于二者。
copyout和copyin复制数据到用户虚拟地址或从用户虚拟地址复制数据，
这些虚拟地址作为系统调用参数提供*/

/*
 * the kernel's page table.
 在riscv.h中有如下定义 
 typedef uint64 *pagetable_t; // 512 PTEs
 pagetable_t其实就是一个指向根页表页的 指针
 一个pagetable_t既可以是内核页表 也可以是进程页表
 核心函数是walk和mappages
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 创建并初始化   全局 内核页表！！ 
 并完成内核的虚拟内存映射 确保内核能够正确访问硬件设备、内核代码和数据段等。
kvmmap调用mappages()
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);//分配一个页面大小的内存给kernel_pagetable，即初始化内核页表

  // uart registers
  //UART0的虚拟地址映射到UART0的物理地址，大小为一个页面（PGSIZE），权限为可读可写（PTE_R | PTE_W）。这是为了访问串口设备的寄存器。
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
/*安装内核页表 将根页表页的物理地址写入寄存器satp；之后CPU将使用内核页表转换地址
由于内核使用标识映射，下一条指令的当前虚拟地址将映射到正确的物理内存地址。*/
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}
//进程内核页表假造到SATP寄存器
//将进程的内核页表加载到SATP寄存器
void
proc_inithart(pagetable_t kpt){
  w_satp(MAKE_SATP(kpt));
  sfence_vma(); //清除快表缓存 刷新TLB缓存 确保地址转换表的更改生效
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.

/*在多级页表中查找虚拟地址对应的页表项PTE 如果需要 还会分配新的页表页*/
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)//页表根指针pagetable 虚拟地址va 返回值alloc 如果为1 代表需要分配新的页表页
{
  if(va >= MAXVA)//虚拟地址是否在范围内 超出调用panic 触发崩溃 错误信息"walk"
    panic("walk");

      /*多级页表查找 一次从3级页表中获取9个比特位。它使用上一级的9位虚拟地址来查找下一级页表或最终页面的PTE*/
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];//计算当前页表项PTE的索引 一个64位数的指针？
    if(*pte & PTE_V) {//判断PTE是否存在
      pagetable = (pagetable_t)PTE2PA(*pte);//存在则更新pagetable为下一级页表的基地址
    } else {
      /*kalloc分配一个页面大小的内存并memset初始化为0
      分配完成后 更新pagetable为新的页表基地址*/
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)//如果alloc为0 或者 分配失败 返回0
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];//成功进行多级页表查找后 则返回第一级页表项中的页表项指针 该页表项PTE记录了映射到物理地址的44位物理页框（块号？）
}

// Look up a virtual address, return the physical address,
/*即 查找 虚拟地址 对应的 物理地址*/
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.

/*对 xv6的内核页表进行映射 */
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  //将va虚拟地址映射到pa物理地址 映射大小sz  perm为映射的访问权限
  //mappages为上边提到的映射分配页表项
  /*将范围虚拟地址到同等范围物理地址的映射装载到一个页表中。它以页面大小为间隔，为范围内的每个虚拟地址单独执行此操作。
   */
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Just follow the kvmmap on vm.c
//对 进程的！！！ 内核页表进行映射
void
uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("uvmmap");
}

// Create a kernel page table for the process
//用于在allocproc中初始化进程的内核页表 uvmmap用于对进程的内核页表进行映射
pagetable_t
proc_kpt_init(){
  pagetable_t kernelpt = uvmcreate();//在这里边执行kalloc 其实就是为进程页表分配一个页面大小的内存
  if (kernelpt == 0) return 0;
  uvmmap(kernelpt, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  uvmmap(kernelpt, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
  uvmmap(kernelpt, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  uvmmap(kernelpt, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
  uvmmap(kernelpt, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  uvmmap(kernelpt, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return kernelpt;
}


// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.

//内核虚拟地址映射到物理地址  这个函数在内核运行需要访问物理地址时调用
//而kvmmap主要是系统启动时调用 完成内核页表初始化 建立映射关系
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  //原先是遍历的共享内核页表
  //pte = walk(kernel_pagetable, va, 0);

  //修改成遍历进程的内核页表
  pte = walk(myproc()->kernelpt,va,0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
/*参数：页表根指针 虚拟地址的起始地址 映射大小 物理地址起始地址 权限*/
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  //使用无限循环处理每个页 直到映射完成
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)//如果查找或分配失败 返回-1
      return -1;
    if(*pte & PTE_V)//如果该页表项已经存在 代表改虚拟地址已经被映射 返回崩溃错误
      panic("remap");
      //否则 进行虚拟地址到物理地址的映射 并且将该pte页表项设置为有效PTE_V
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    //映射完成后 更新虚拟地址和物理地址 处理下一个页
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
//用于递归释放页表占用的内存 前提是假设：
// 所有叶节点映射（即实际的物理页面映射）已经被移除，因此它只处理页表本身的释放。
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  //遍历整个页表
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    //pte & PTE_V 检查PTE是否有效
    //pte & (PTE_R|PTE_W|PTE_X) 检查PTE是否未设置读、写、执行权限，未设置则说明这是一个指向下一级页表的PTE，而不是最后一层,即叶节点
    //因为最后一层要指向实际的物理地址 PTE_R|PTE_W|PTE_X 三者其中至少一个被设置为1
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);//提取物理地址部分
      freewalk((pagetable_t)child);//只要页表有效且不是最后一层就递归调用 遍历整个页表
      pagetable[i] = 0;//清零 确保释放后不会留下悬空指针
    } else if(pte & PTE_V){//如果是叶节点且 R/W/X有被设置 和假设额宝墩 说明有叶节点未移除 触发panic
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

//lab3 第一个实验 print_pgtbl的实现

/**
 * 递归遍历页表并打印相关信息
 * @param pagetable 所要打印的页表
 * @param level 页表的层级
 */
 void
 _vmprint(pagetable_t pagetable, int level){
   // there are 2^9 = 512 PTEs in a page table.
   for(int i = 0; i < 512; i++){
     pte_t pte = pagetable[i];
     // PTE_V is a flag for whether the page table is valid
     if(pte & PTE_V){
      //有效 则打印 先打印层级关系信息 ..代表第一层 .. .. ..最后一层
       for (int j = 0; j < level; j++){
         if (j) printf(" ");
         printf("..");
       }
       uint64 child = PTE2PA(pte);//提取物理地址部分
       printf("%d: pte %p pa %p\n", i, pte, child);
       //说明不是最后一层 这是一个指向下一级页表的PTE 递归打印 层级level+1
       if((pte & (PTE_R|PTE_W|PTE_X)) == 0){
         // this PTE points to a lower-level page table.
         _vmprint((pagetable_t)child, level + 1);
       }
     }
   }
 }
 
 /**
  * 打印页表的入口函数 
  * @brief vmprint 打印页表
  * @param pagetable 所要打印的页表
  */
 void
 vmprint(pagetable_t pagetable){
   printf("page table %p\n", pagetable);
   _vmprint(pagetable, 1);
 }
 

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

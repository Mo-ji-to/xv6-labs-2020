// Mutual exclusion lock.
//译：互斥锁
struct spinlock {
   // 锁是否已被持有的标志位
  uint locked;       // Is the lock held?

  // For debugging:
  // 译：用于debug的一些附加信息
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};


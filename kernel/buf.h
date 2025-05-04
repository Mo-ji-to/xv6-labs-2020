struct buf {
  int valid;   // 缓冲区是否已经有了包含块的副本？为1表示已读取，该数据有效；为0表示需要重新从磁盘读取
  int disk;    // 缓冲区内容是否已经提交给磁盘？这可能会更改缓冲区
  // 此时缓冲区中的数据可能正在被写入磁盘，其他操作需要等待；值为 0 时表示缓冲区可以被其他操作使用。
  uint dev;     //磁盘设备号 用来区分不同的磁盘设备
  uint blockno; //磁盘块编号 定位磁盘上具体的数据块
  struct sleeplock lock;  //睡眠所 同步对缓冲区的访问 确保多线程环境下对缓冲区的操作互斥
  uint refcnt;  //引用计数 管理缓冲区生命周期 为0时可释放或重新引用
  struct buf *prev; 
  struct buf *next;// LRU cache list  双向链表 将缓冲区组织成LRU缓存列表
  uchar data[BSIZE];  //实际存储磁盘块数据的缓冲区字节大小
};


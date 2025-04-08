#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }//参数解析 检查用户输入参数  确保trace命令至少三个参数 分别是 程序名 掩码 命令   trace 32 grep hello README

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }//设置跟踪掩码 atoi 字符串形式转换成整数
  
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }// 从 argv[2] 开始，将用户输入的命令及其参数复制到 nargv中 MAXARG是参数最大数量
  exec(nargv[0], nargv);//nargv[0]对应要执行的命令  就是上边的grep
  exit(0);
}

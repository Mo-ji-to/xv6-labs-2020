#include "kernel/types.h"

#include "kernel/fs.h"
#include "kernel/stat.h"
#include "user/user.h"
//查找目录树中具有特定名称的文件 find参考的不是代码随想录的  虽然不知道有什么区别
//递归查找 查找路径位path的目录下是否有目标文件filename
void find(char *path, const char *filename)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  //打开目录
  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
 //获取目录状态信息
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot fstat %s\n", path);
    close(fd);
    return;
  }

  //参数错误，find的第一个参数必须是目录
  if (st.type != T_DIR) {
    fprintf(2, "usage: find <DIRECTORY> <filename>\n");
    return;
  }

  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
    fprintf(2, "find: path too long\n");
    return;
  }
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/'; //p指向最后一个'/'之后
  while (read(fd, &de, sizeof de) == sizeof de) {
    if (de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ); //添加路径名称
    p[DIRSIZ] = 0;               //字符串结束标志
    if (stat(buf, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }
    //不要在“.”和“..”目录中递归
    if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
      find(buf, filename);
    } else if (strcmp(filename, p) == 0)
      printf("%s\n", buf);
  }

  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "usage: find <directory> <filename>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}

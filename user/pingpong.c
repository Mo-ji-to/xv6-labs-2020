#include "kernel/types.h"
#include "user/user.h"

#define RD 0 //pipe的read端
#define WR 1 //pipe的write端


/*实验描述：
1、父进程在管道向子进程发送一个字节 子进程读取并打印出来
2、子进程在管道中写入字节发送回给父进程 子进程退出
3、父进程在管道读取子进程返回的字节 然后父进程退出
最后所有的描述符都会被释放 节省资源*/

/*使用两个管道进行父子进程通信，需要注意的是如果管道的写端没有close，
那么管道中数据为空时对管道的读取将会阻塞。
因此对于不需要的管道描述符，要尽可能早的关闭。*/
int main(int argc, char const *argv[]) {
    char buf = 'P'; //用于传送的字节

    int fd_c2p[2]; //子进程->父进程
    int fd_p2c[2]; //父进程->子进程
    pipe(fd_c2p);
    pipe(fd_p2c);

    int pid = fork();
    int exit_status = 0;

    if (pid < 0) {
        fprintf(2, "fork() error!\n");
        close(fd_c2p[RD]);
        close(fd_c2p[WR]);
        close(fd_p2c[RD]);
        close(fd_p2c[WR]);
        exit(1);
    } else if (pid == 0) { //子进程
        /*子进程读取父进程写入的字节 
        显式关闭父->子的写端（已经写完，避免子进程读操作堵塞）
        关闭子->父的读 因为此时父进程不需要读取*/
        close(fd_p2c[WR]);
        close(fd_c2p[RD]);

        if (read(fd_p2c[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child read() error!\n");
            exit_status = 1; //标记出错
        } else {
            fprintf(1, "%d: received ping\n", getpid());
        }

        if (write(fd_c2p[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "child write() error!\n");
            exit_status = 1;
        }
        /*子进程向父进程写数据 写完后关闭子->父的写端
        关闭父->子的读端 子进程不需要再读取了
        然后子进程退出*/
        close(fd_p2c[RD]);
        close(fd_c2p[WR]);

        exit(exit_status);
    } else { //父进程
        /*父进程向子进程写入字节
        关闭父->子的读端 
        关闭子->父的写 */
        close(fd_p2c[RD]);
        close(fd_c2p[WR]);

        if (write(fd_p2c[WR], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent write() error!\n");
            exit_status = 1;
        }

        if (read(fd_c2p[RD], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "parent read() error!\n");
            exit_status = 1; //标记出错
        } else {
            fprintf(1, "%d: received pong\n", getpid());
        }
        /*父进程读取子进程返回的字节
        关闭父->子的写端 不需要再写入了
        关闭子到父的读端 读取完毕了已经
        退出父进程*/
        close(fd_p2c[WR]);
        close(fd_c2p[RD]);

        exit(exit_status);
    }
}

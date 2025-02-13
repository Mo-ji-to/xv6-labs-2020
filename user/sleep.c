// user /sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//lab1_work1
int main(int argc,char **argv){
    if(argc < 2){
        printf("Usage: sleep <ticks>\n");
    }
    sleep(atoi(argv[1]));
    exit(0);
}
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  int p2c[2];  // parent -> child
  int c2p[2];  // child -> parent
  char byte = 'x';

  // 当前程序不需要命令行参数。
  (void)argc;
  (void)argv;

  // 创建两个方向的管道。
  if(pipe(p2c) < 0 || pipe(c2p) < 0){
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");

    close(p2c[0]);
    close(p2c[1]);
    close(c2p[0]);
    close(c2p[1]);

    exit(1);
  }

  if(pid == 0){
    // 子进程只需要：
    // 从 p2c 读取，从 c2p 写入。
    close(p2c[1]);
    close(c2p[0]);

    if(read(p2c[0], &byte, 1) != 1){
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }

    printf("%d: received ping\n", getpid());

    if(write(c2p[1], &byte, 1) != 1){
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }

    close(p2c[0]);
    close(c2p[1]);

    exit(0);
  }

  // 父进程只需要：
  // 向 p2c 写入，从 c2p 读取。
  close(p2c[0]);
  close(c2p[1]);

  if(write(p2c[1], &byte, 1) != 1){
    fprintf(2, "pingpong: parent write failed\n");
    exit(1);
  }

  // 父进程不会再向子进程发送数据。
  close(p2c[1]);

  if(read(c2p[0], &byte, 1) != 1){
    fprintf(2, "pingpong: parent read failed\n");
    exit(1);
  }

  printf("%d: received pong\n", getpid());

  close(c2p[0]);

  // 回收已经退出的子进程。
  wait(0);

  exit(0);
}
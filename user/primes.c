#include "kernel/types.h"
#include "user/user.h"

// 从 read_fd 接收上一层传来的数字。
// 读取第一个数字作为当前素数，
// 再把不能被该素数整除的数字传给下一层。
void sieve(int read_fd)
{
  int prime;

  // 第一个数字就是当前阶段找到的素数。
  int nread = read(read_fd, &prime, sizeof(prime));

  // 如果没有数字可读，说明整个筛选链结束。
  if(nread == 0){
    close(read_fd);
    exit(0);
  }

  if(nread != sizeof(prime)){
    fprintf(2, "primes: read prime failed\n");
    close(read_fd);
    exit(1);
  }

  printf("prime %d\n", prime);

  // 创建通往下一筛选阶段的管道。
  int next_pipe[2];

  if(pipe(next_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    close(read_fd);
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");

    close(read_fd);
    close(next_pipe[0]);
    close(next_pipe[1]);

    exit(1);
  }

  if(pid == 0){
    // 子进程是下一筛选阶段。
    // 它只负责从 next_pipe 中读取。
    close(next_pipe[1]);

    // 子进程不再使用上一阶段的管道。
    close(read_fd);

    sieve(next_pipe[0]);

    // sieve 本身通常已经 exit，
    // 这里作为保险。
    exit(0);
  }

  // 当前进程负责过滤。
  // 它只向 next_pipe 写入。
  close(next_pipe[0]);

  int number;

  while(1){
    int result = read(read_fd, &number, sizeof(number));

    if(result == 0){
      // 上一阶段已经关闭写端，数据全部读取完毕。
      break;
    }

    if(result != sizeof(number)){
      fprintf(2, "primes: read number failed\n");

      close(read_fd);
      close(next_pipe[1]);

      wait(0);
      exit(1);
    }

    // 只把不能被当前素数整除的数传给下一阶段。
    if(number % prime != 0){
      if(write(next_pipe[1], &number, sizeof(number))
          != sizeof(number)){
        fprintf(2, "primes: write failed\n");

        close(read_fd);
        close(next_pipe[1]);

        wait(0);
        exit(1);
      }
    }
  }

  close(read_fd);

  // 非常重要：
  // 关闭写端，使下一层的 read 最终能够返回 0。
  close(next_pipe[1]);

  // 等待下一层筛选进程结束。
  wait(0);

  exit(0);
}

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  int first_pipe[2];

  if(pipe(first_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");

    close(first_pipe[0]);
    close(first_pipe[1]);

    exit(1);
  }

  if(pid == 0){
    // 第一个筛选进程只读取主进程生成的数字。
    close(first_pipe[1]);

    sieve(first_pipe[0]);

    exit(0);
  }

  // 主进程只负责生成 2～35。
  close(first_pipe[0]);

  for(int number = 2; number <= 35; number++){
    if(write(first_pipe[1], &number, sizeof(number))
        != sizeof(number)){
      fprintf(2, "primes: initial write failed\n");

      close(first_pipe[1]);
      wait(0);
      exit(1);
    }
  }

  // 告诉第一个筛选进程：
  // 不会再有新数字了。
  close(first_pipe[1]);

  wait(0);

  exit(0);
}
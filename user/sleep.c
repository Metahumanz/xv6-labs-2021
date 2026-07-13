#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  // argv[0] 是程序名 sleep，argv[1] 是 tick 数量。
  if(argc != 2){
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  // 命令行参数都是字符串，需要先转换为整数。
  int ticks = atoi(argv[1]);

  // 调用 xv6 已有的 sleep 系统调用。
  sleep(ticks);

  exit(0);
}
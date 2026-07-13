#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define LINE_SIZE 512

// 判断字符是否是本实验需要处理的空白字符。
static int is_blank(char c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

// 将一行文本解析成参数，并执行一次命令。
static void run_command(char *line, char **base_args, int base_count)
{
  char *args[MAXARG];
  int arg_count = 0;
  char *p = line;

  // 先复制 xargs 命令行中已有的固定参数。
  //
  // 例如：
  // xargs echo bye
  //
  // base_args 中保存：
  // echo
  // bye
  for(int i = 0; i < base_count; i++){
    args[arg_count++] = base_args[i];
  }

  // 再解析标准输入中的这一行。
  while(*p != 0){
    // 跳过单词之间的空格、Tab 等。
    while(is_blank(*p))
      p++;

    // 到达字符串末尾。
    if(*p == 0)
      break;

    // 必须给最后的空指针保留一个位置。
    if(arg_count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }

    // 当前单词的开始位置。
    args[arg_count++] = p;

    // 向后寻找当前单词的结束位置。
    while(*p != 0 && !is_blank(*p))
      p++;

    // 如果遇到了空白字符，就用 '\0' 将单词截断。
    if(*p != 0){
      *p = 0;
      p++;
    }
  }

  // 空白行中没有附加参数，本实现直接跳过。
  if(arg_count == base_count)
    return;

  // exec 的参数数组必须以空指针结束。
  args[arg_count] = 0;

  int pid = fork();

  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    exec(args[0], args);

    // exec 成功后不会返回。
    // 能运行到这里，说明 exec 失败。
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  }

  // 一行执行结束后，才开始处理下一行。
  wait(0);
}

int main(int argc, char *argv[])
{
  char line[LINE_SIZE];
  int length = 0;
  char c;

  if(argc < 2){
    fprintf(2, "usage: xargs command [initial-arguments]\n");
    exit(1);
  }

  // argv[1] 开始才是 xargs 要执行的命令和固定参数。
  char **base_args = &argv[1];
  int base_count = argc - 1;

  // 逐字符读取标准输入。
  while(read(0, &c, 1) == 1){
    if(c == '\n'){
      // 将当前一行变成合法 C 字符串。
      line[length] = 0;

      run_command(line, base_args, base_count);

      // 准备接收下一行。
      length = 0;
      continue;
    }

    if(length >= LINE_SIZE - 1){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }

    line[length++] = c;
  }

  // 如果最后一行没有换行符，也要处理。
  if(length > 0){
    line[length] = 0;
    run_command(line, base_args, base_count);
  }

  exit(0);
}
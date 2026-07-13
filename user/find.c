#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 返回路径中最后一个部分。
// 例如：
//   "./a/b" -> "b"
//   "hello" -> "hello"
static char *last_component(char *path)
{
  char *p;

  p = path + strlen(path);

  // 从字符串结尾向前寻找最后一个 '/'。
  while(p > path && *(p - 1) != '/')
    p--;

  return p;
}

// 从 path 开始递归查找名称为 target 的文件。
static void find(char *path, char *target)
{
  char buf[512];
  char *p;
  char *name;
  int fd;
  struct dirent de;
  struct stat st;

  // 以只读方式打开当前路径。
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // 获取当前路径的类型和其他属性。
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
  case T_DEVICE:
    // 当前路径是普通文件或设备文件。
    // 取出路径最后一段，与目标名称比较。
    name = last_component(path);

    if(strcmp(name, target) == 0)
      printf("%s\n", path);

    break;

  case T_DIR:
    // 构造的最大路径包括：
    // 原路径 + '/' + 最长目录项名称 + '\0'。
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      break;
    }

    // 先把当前路径复制进缓冲区。
    strcpy(buf, path);

    // p 指向当前路径末尾。
    p = buf + strlen(buf);

    // 如果路径末尾还没有 '/'，就补上一个。
    if(p > buf && *(p - 1) != '/')
      *p++ = '/';

    // 依次读取目录中的每一个目录项。
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      // 跳过无效目录项。
      if(de.inum == 0)
        continue;

      // 把当前目录项名称复制到路径末尾。
      memmove(p, de.name, DIRSIZ);

      // de.name 不保证以 '\0' 结尾，
      // 所以主动添加字符串结束符。
      p[DIRSIZ] = 0;

      // 不能递归进入 "." 和 ".."。
      if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;

      // buf 现在是完整子路径，例如：
      // "." + "/" + "a" = "./a"
      //
      // 递归检查这个子路径。
      find(buf, target);
    }

    break;
  }

  close(fd);
}

int main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path filename\n");
    exit(1);
  }

  find(argv[1], argv[2]);

  exit(0);
}
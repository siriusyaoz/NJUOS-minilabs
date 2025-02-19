#include <assert.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_MATCHES 3 // 整个匹配 + 2 个捕获组
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define BILLION (1000 * 1000 * 1000)

typedef struct {
  char syscall[64];
  double time_seconds;
} SyscallInfo;
typedef struct {
  SyscallInfo *entries; // 动态数组指针
  size_t count;         // 当前元素数量
  size_t capacity;      // 总容量
  double total_time;
} SyscallArray;

extern char **environ;

char** build_new_argv(int argc, char* argv[]);
uint minus(struct timespec a, struct timespec b);
int syscall_array_init(SyscallArray *arr, size_t initial_capacity);
int syscall_array_add(SyscallArray *arr, const SyscallInfo *info);
void syscall_array_free(SyscallArray *arr);
int parse_strace_line(const char *line, SyscallInfo *info);
static int comp_sys_info(const void *va, const void *vb);
void syscall_array_reset(SyscallArray *arr);

int main(int argc, char *argv[]) {
  for (int i = 0; i < argc; i++) {
    assert(argv[i]);
    printf("argv[%d] = %s\n", i, argv[i]);
  }
  assert(!argv[argc]);
  char *filename = "/usr/bin/strace";
  char **newargv = build_new_argv(argc, argv);

  int fd[2];
  pipe(fd);
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  uint interval_ns = 10 * 1000000;

  pid_t pid = fork();
  if (pid == 0) {
    for (int i = 0; i < argc+1; i++) {
      assert(newargv[i]);
      printf("newargv[%d] = %s\n", i, newargv[i]);
    }
    fflush(stdout);

    dup2(fd[1], 2);
    close(fd[1]);
    close(fd[0]);

    execve(filename, newargv, environ);
    perror("execve");
  } else {
    close(fd[1]);
    struct timespec now;
    FILE *fp = fdopen(fd[0], "r"); // fd 是文件描述符
    if (!fp) {
      perror("fdopen");
      return -1;
    }
    char line[1024];

    SyscallArray arr;
    SyscallInfo info;
    if (syscall_array_init(&arr, 16) == -1) {
      perror("syscall init failed");
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
      clock_gettime(CLOCK_MONOTONIC, &now);
      printf("(parent process) Line: %s", line);
      printf("%ld.%09ld seconds\n",now.tv_sec,now.tv_nsec);
      // process statstics here
      parse_strace_line(line, &info);
      syscall_array_add(&arr, &info);
      //超过100ms
      if (minus(now, start) >= interval_ns) {
        // print the data in this interval
        qsort(arr.entries, arr.count, sizeof(SyscallInfo), comp_sys_info);
        printf("time passed :%.2lf\n",(now.tv_sec+ now.tv_nsec/(double)BILLION));
        for (int i = 0; i < MIN(5, arr.count); i++) {
          int percent=  arr.entries[i].time_seconds *100/arr.total_time;
          printf("%s (%d%%)\n", arr.entries[i].syscall, percent);
        }
        printf("======================\n");
        start = now;
        syscall_array_reset(&arr);
      }
    }
    fclose(fp);
    syscall_array_free(&arr);
  }
  return 0;
}
char** build_new_argv(int argc, char* argv[]) {
  // 计算新参数数组的长度
  int new_argc = argc + 1;  // 去掉 argv[0]，添加 "strace" 和 "-T"

  // 分配内存
  char** new_argv = malloc((new_argc + 1) * sizeof(char*));  // +1 用于 NULL 结尾
  if (!new_argv) {
      perror("malloc failed");
      return NULL;
  }

  // 构建新参数数组
  new_argv[0] = "strace";  // 第一个参数
  new_argv[1] = "-T";      // 第二个参数

  // 复制剩余参数
  for (int i = 1; i < argc; i++) {
      new_argv[i + 1] = argv[i];
  }

  // 最后一个元素设置为 NULL
  new_argv[new_argc] = NULL;

  return new_argv;
}

uint minus(struct timespec a, struct timespec b) {
  return (a.tv_sec - b.tv_sec) * BILLION + a.tv_nsec - b.tv_nsec;
}

int parse_strace_line(const char *line, SyscallInfo *info) {
  regex_t regex;
  regmatch_t matches[MAX_MATCHES];
  int ret;
  const char *pattern = "^([a-zA-Z0-9_]+)$.*$\\s*=\\s*.*<([0-9.]+)>$";

  // 编译正则表达式
  if (regcomp(&regex, pattern, REG_EXTENDED)) {
    fprintf(stderr, "Could not compile regex\n");
    return -1;
  }

  // 执行匹配
  if ((ret = regexec(&regex, line, MAX_MATCHES, matches, 0))) {
    regfree(&regex);
    return -1; // 没有匹配
  }

  // 提取系统调用名称
  int syscall_len = matches[1].rm_eo - matches[1].rm_so;
  strncpy(info->syscall, line + matches[1].rm_so, syscall_len);
  info->syscall[syscall_len] = '\0';

  // 提取时间字符串并转换
  char *time_str =
      strndup(line + matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
  info->time_seconds = atof(time_str);
  free(time_str);

  regfree(&regex);
  return 0;
}

// 初始化动态数组
int syscall_array_init(SyscallArray *arr, size_t initial_capacity) {
  arr->entries = malloc(initial_capacity * sizeof(SyscallInfo));
  if (!arr->entries)
    return -1;

  arr->count = 0;
  arr->capacity = initial_capacity;
  arr->total_time = 0.0;
  return 0;
}

// 扩容函数（内部使用）
static int syscall_array_grow(SyscallArray *arr) {
  const size_t new_capacity = arr->capacity * 2; // 容量翻倍
  SyscallInfo *new_entries =
      realloc(arr->entries, new_capacity * sizeof(SyscallInfo));

  if (!new_entries) {
    // 保持原有数据不变，返回错误
    return -1;
  }

  arr->entries = new_entries;
  arr->capacity = new_capacity;
  return 0;
}

// 添加元素（示例用法）
int syscall_array_add(SyscallArray *arr, const SyscallInfo *info) {
  if (arr->count >= arr->capacity) {
    if (syscall_array_grow(arr) != 0) {
      return -1; // 扩容失败
    }
  }
  for (int i = 0; i < arr->count; i++) {
    if (strcmp(info->syscall, arr->entries[i].syscall) == 0) {
      arr->entries[i].time_seconds += info->time_seconds;
      arr->total_time += info->time_seconds;
      return 0;
    }
  }
  // 拷贝数据（可根据需要改为深拷贝）
  memcpy(&arr->entries[arr->count], info, sizeof(SyscallInfo));
  arr->count++;
  arr->total_time += info->time_seconds;
  return 0;
}
void syscall_array_reset(SyscallArray *arr) {
  arr->count = 0;
  arr->total_time = 0.0;
}
// 释放资源
void syscall_array_free(SyscallArray *arr) {
  free(arr->entries);
  arr->entries = NULL;
  arr->count = 0;
  arr->capacity = 0;
  arr->total_time = 0.0;
}
static int comp_sys_info(const void *va, const void *vb) {
  SyscallInfo a = *(SyscallInfo *)va;
  SyscallInfo b = *(SyscallInfo *)vb;
  double time = a.time_seconds < b.time_seconds;
  if (time < 0) {
    return -1;
  } else if (time > 0) {
    return 1;
  } else {
    return 0;
  }
}
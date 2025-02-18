#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void complie_shared_lib(char *line, int compile);
int eval(char *line);
void create_tmp_file();
void remove_last_line(const char *filename);

static char path[] = "crepl_functionsXXXXXX";
static char so_path[] = "./crepl_functions.so";
int main(int argc, char *argv[]) {
  static char line[1024];
  char c[] = "int";
  char *expressions[100];
  int index = 0;
  create_tmp_file();
  while (1) {
    printf("crepl> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }
    line[strcspn(line, "\n")] = '\0';

    // To be implemented.
    // printf("Got %zu chars.\n", strlen(line));
    if (strncmp(line, "quit", 4) == 0) {
      break;
    }

    if (strncmp(line, c, 3) == 0) {
      complie_shared_lib(line, 0);
      printf("Added function: %s\n", line);
      fflush(stdout);
    } else {
      char new_line[1100];
      snprintf(new_line, sizeof(new_line),
               "int __expr_wrapper_%d() {return %s;}", index, line);
      printf("%s\n", new_line);
      complie_shared_lib(new_line, 1);
      snprintf(new_line, sizeof(new_line), "__expr_wrapper_%d", index);
      printf("result is %d\n", eval(new_line));
      fflush(stdout);
      index++;
    }
  }
  unlink(path);
}

void create_tmp_file() {
  int fd = mkstemp(path);
  if (fd == -1) {
    perror("mkstemp");
    exit(EXIT_FAILURE);
  }
  close(fd);
}

void complie_shared_lib(char *line, int compile) {
  FILE *fp = fopen(path, "a");
  if (!fp) {
    perror("fopen");
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  fprintf(fp, "%s\n", line); // 将函数定义写入临时文件
  fclose(fp);

  if (compile) {
    // snprintf(so_path, sizeof(so_path), "%s.so", path);
    pid_t pid = fork();
    if (pid == 0) {
      execlp("gcc", "gcc", "-shared", "-x", "c", "-fPIC", "-o",
             "crepl_functions.so", path, NULL);
      perror("execlp");
      exit(EXIT_FAILURE);
    } else {
      // Parent process: Wait for the child to finish
      int status;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Compilation failed\n");
        remove_last_line(path);
        return;
      }
    }
  }
}

int eval(char *func) {
  // Step 3: Load the shared library and get the function pointer
  void *handle;
  int (*foo)(void); // 假设foo是一个无参数且返回int的函数
  char *error;

  // 打开共享库
  handle = dlopen(so_path, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "%s\n", dlerror());
    return -1;
  }

  // 清除现有的错误
  dlerror();
  printf("Looking for symbol:%s\n", func); // 确认符号名称
  // 获取foo函数的地址
  *(void **)(&foo) = dlsym(handle, func);
  if ((error = dlerror()) != NULL) {
    fprintf(stderr, "%s\n", error);
    dlclose(handle);
    return -1;
  }

  // 调用函数
  int result = foo();

  // 关闭共享库
  dlclose(handle);
  unlink(so_path);
  return result;
}

void remove_last_line(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
      perror("fopen");
      return;
  }

  // 读取所有行到内存中（除了最后一行）
  char **lines = malloc(sizeof(char *) * 1024);  // 假设最多1024行
  int line_count = 0;
  char buffer[1024];
  
  while (fgets(buffer, sizeof(buffer), fp)) {
      // 去掉每行末尾的换行符
      buffer[strcspn(buffer, "\n")] = 0;
      lines[line_count] = strdup(buffer);
      line_count++;
  }
  fclose(fp);

  // 重写文件，不写入最后一行
  fp = fopen(filename, "w");
  if (!fp) {
      perror("fopen");
      return;
  }
  
  for (int i = 0; i < line_count - 1; i++) {
      fprintf(fp, "%s\n", lines[i]);
      free(lines[i]);  // 释放内存
  }
  free(lines[line_count - 1]);  // 释放最后一行的内存
  free(lines);  // 释放数组内存
  fclose(fp);
}
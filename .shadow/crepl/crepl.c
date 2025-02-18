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

static char path[] = "crepl_functionsXXXXXX";
static char so_path[25];
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

    // To be implemented.
    //printf("Got %zu chars.\n", strlen(line));
    if(strncmp(line,"quit",4)==0){
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
    pid_t pid = fork();
    if (pid == 0) {
      snprintf(so_path, sizeof(so_path), "%s.so", path);
      execlp("gcc", "gcc", "-shared", "-x", "c", "-fPIC", "-o", so_path, path,
             NULL);
      perror("execlp");
      exit(EXIT_FAILURE);
    } else {
      // Parent process: Wait for the child to finish
      int status;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Compilation failed\n");
        unlink(path);
        return;
      }else{
        printf("Compilation success\n");
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
    return 1;
  }

  // 清除现有的错误
  dlerror();
  printf("Looking for symbol:%s\n", func); // 确认符号名称
  // 获取foo函数的地址
  *(void **)(&foo) = dlsym(handle, func);
  if ((error = dlerror()) != NULL) {
    fprintf(stderr, "%s\n", error);
    dlclose(handle);
    unlink(so_path);
    return 1;
  }

  // 调用函数
  int result = foo();

  // 关闭共享库
  dlclose(handle);
  unlink(so_path);
  return result;
}
#include "co.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef LOCAL_MACHINE
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#define STACK_SIZE 0x100000
#define NUMCOROUTINES 128

static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg);


enum co_status {
  CO_NEW = 1,  // 新创建，还未执行过
  CO_RUNNING,  // 已经执行过
  CO_WAITING,  // 在 co_wait 上等待
  CO_DEAD,     // 已经结束，但还未释放资源
};

struct co {
  const char *name;
  void (*func)(void *);  // co_start 指定的入口地址和参数
  void *arg;

  enum co_status status;          // 协程的状态
  struct co *waiter;              // 是否有其他协程在等待当前协程
  jmp_buf context;                // 寄存器现场
  uint8_t stack[STACK_SIZE + 1];  // 协程的堆栈
  int position;                   // 在coarray中的position
};

static struct co *current;
static struct co *coarray[NUMCOROUTINES];

static struct co main_co = {.name = "main",
                            .func = NULL,
                            .arg = NULL,

                            .status = CO_RUNNING,
                            .waiter = NULL,
                            .stack = {0},
                            .position = 0,};

//不能用这两个！！
// If the function that called setjmp has exited (whether by return or by a
// different longjmp higher up the stack), the behavior is undefined. In other
// words, only long jumps up the call stack are allowed. cppreference longjmp

// 保存当前寄存器状态
// int save_context(struct co *coroutine) { return setjmp(coroutine->context); }

// 恢复寄存器状态
// void restore_context(struct co *coroutine) { longjmp(coroutine->context, 1);
//   debug("\nreturn from restore!\n");
// }

//我认为写的最好的部分！！
//在wrapper里处理协程运行完毕之后的工作，设置进程为dead，返回到waiter或main
void coroutine_wrapper(void *arg) {
  struct co *coro = (struct co *)arg;
  void (*func)(void *) = coro->func;
  void *argf = coro->arg;
  func(argf);
  coro->status = CO_DEAD;
  coarray[coro->position] = NULL;
  //可以这样做吗？调用的coro在这里返回到main_coro?
  if (coro->waiter) {
    current = coro->waiter;
    debug("co %s status is set to Dead, back to waiter %s \n", coro->name,
          current->name);
  } else {
    current = &main_co;
  }
  longjmp(current->context, 1);
}

struct co *co_start(const char *name, void (*func)(void *), void *arg) {
  struct co *coro = (struct co *)malloc(sizeof(struct co));
  coro->name = name;
  coro->func = func;
  coro->arg = arg;

  coro->status = CO_NEW;
  coro->waiter=NULL;        //必需的，不然会跳到非法地址
  //coro->stack= {0};
  debug("\nnew_coro->status is %d,name is %s,costart!\n", coro->status,
        coro->name);
  for (int i = 0; i < NUMCOROUTINES; i++) {
    if (!coarray[i]) {
      coarray[i] = coro;  // 给新分配的coroutine分配位置
      coro->position = i;
      break;
    }
  }
  return coro;
}

void co_wait(struct co *co) {
  debug("current %s is co waiting co %s \n",current->name, co->name);
  //若co 已经执行完毕，直接返回
  if (co->status == CO_DEAD) {
    debug("co %s status is Dead\n", co->name);
    free(co);
    return;
  }
  co->waiter = current;
  current->status = CO_WAITING;

  while (co->status != CO_DEAD) {
    co_yield ();
  }
  debug("co %s status is Dead\n", co->name);
  free(co);
}

int find_random_coro();

void co_yield () {
  int val = setjmp(current->context);

  if (val == 0) {
    int next_coro_index = find_random_coro();
    if (next_coro_index == -1) {
      debug("no nonzero coro!coro %s should continue!\n",current->name);
      return;
    }
    struct co *new_coro = coarray[next_coro_index];
    // 增加详细调试
    // debug("Attempting to yield: index=%d, coro=%p, status=%d, name=%s\n",
    //       next_coro_index, (void *)new_coro, new_coro ? new_coro->status : -1,
    //       new_coro ? new_coro->name : "NULL");
    if (new_coro->status == CO_RUNNING || new_coro->status == CO_WAITING) {
      current = new_coro;
      longjmp(current->context, 1);
      debug("\nreturn from restore!\n");
    } else if (new_coro->status == CO_NEW) {
      current = new_coro;
      current->status = CO_RUNNING;
      stack_switch_call(&current->stack[STACK_SIZE], coroutine_wrapper,
                        (uintptr_t)new_coro);
      debug("\n\nreturn to coyield!\n\n");
    } else {
      debug("new_coro->status is %d,name is %s,should not be here!\n",
            new_coro->status, new_coro->name);
    }
  }
}

__attribute__((constructor)) void init_main_co() {
  current = &main_co;
  coarray[0] = &main_co;
  srand(time(NULL));
}

//糟糕的调度算法
int find_random_coro() {
  int nonzero_indices[128];
  int count = 0;

  // 收集非零索引
  for (int i = 0; i < NUMCOROUTINES; i++) {
    // 随意切换
    // && coarray[i] != current  && coarray[i] != current->waiter
    if (coarray[i] ) {
      nonzero_indices[count++] = i;
      //debug("%d", i);
    }
  }

  // 如果没有非零元素
  if (count == 0) {
    return -1;
  }

  // 随机选择一个非零索引
  int index = rand() % count;
  debug("index is %d,count is %d\n",index,count);
  return nonzero_indices[index];
}

//这段汇编代码实现来自https://github.com/SiyuanYue/NJUOSLab-M2-libco/blob/master/libco/co.c
static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
  asm volatile(
#if __x86_64__
      "movq %%rsp,-0x10(%0); leaq -0x20(%0), %%rsp; movq %2, %%rdi ; call *%1; "
      "movq -0x10(%0) ,%%rsp;"
      :
      : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
      : "memory"
#else
      "movl %%esp, -0x8(%0); leal -0xC(%0), %%esp; movl %2, -0xC(%0); call "
      "*%1;movl -0x8(%0), %%esp"
      :
      : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
      : "memory"
#endif
  );
}
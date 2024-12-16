#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "co-test.h"

int g_count = 0;

static void add_count() { g_count++; }

static int get_count() { return g_count; }
static void work_loop(void *arg) {
  const char *s = (const char *)arg;
  for (int i = 0; i < 100; ++i) {
    printf("%s%d  ", s, get_count());
    add_count();
    co_yield ();
  }
}

static void work(void *arg) { work_loop(arg); }

static void test_1() {
  struct co *thd1 = co_start("thread-1", work, "X");
  struct co *thd2 = co_start("thread-2", work, "Y");
  struct co *thd3 = co_start("thread-2", work, "Z");

  co_wait(thd1);
  co_wait(thd2);
  co_wait(thd3);

  //    printf("\n");
}

// -----------------------------------------------

static int g_running = 1;

static void do_produce(Queue *queue) {
  assert(!q_is_full(queue));
  Item *item = (Item *)malloc(sizeof(Item));
  if (!item) {
    fprintf(stderr, "New item failure\n");
    return;
  }
  item->data = (char *)malloc(10);
  if (!item->data) {
    fprintf(stderr, "New data failure\n");
    free(item);
    return;
  }
  memset(item->data, 0, 10);
  sprintf(item->data, "libco-%d", g_count++);
  q_push(queue, item);
}

static void producer(void *arg) {
  Queue *queue = (Queue *)arg;
  for (int i = 0; i < 100;) {
    if (!q_is_full(queue)) {
      // co_yield();
      do_produce(queue);
      i += 1;
    }
    co_yield ();
  }
}

static void do_consume(Queue *queue) {
  assert(!q_is_empty(queue));

  Item *item = q_pop(queue);
  if (item) {
    printf("%s  ", (char *)item->data);
    free(item->data);
    free(item);
  }
}

static void consumer(void *arg) {
  Queue *queue = (Queue *)arg;
  while (g_running) {
    if (!q_is_empty(queue)) {
      do_consume(queue);
    }
    co_yield ();
  }
}

static void test_2() {
  Queue *queue = q_new();

  struct co *thd1 = co_start("producer-1", producer, queue);
  struct co *thd2 = co_start("producer-2", producer, queue);
  struct co *thd3 = co_start("consumer-1", consumer, queue);
  struct co *thd4 = co_start("consumer-2", consumer, queue);

  co_wait(thd1);
  co_wait(thd2);

  g_running = 0;

  co_wait(thd3);
  co_wait(thd4);

  while (!q_is_empty(queue)) {
    do_consume(queue);
  }

  q_free(queue);
}

// Test 3: Simultaneous coroutines with different behaviors
static void work_with_conditional_yield(void *arg) {
  int *count = (int *)arg;
  for (int i = 0; i < 50; ++i) {
    // Conditional yield based on counter value
    if (*count % 2 == 0) {
      co_yield ();
    }
    (*count)++;
  }
}

static void test_3() {
  int shared_counter = 0;
  struct co *thd1 =
      co_start("conditional-1", work_with_conditional_yield, &shared_counter);
  struct co *thd2 =
      co_start("conditional-2", work_with_conditional_yield, &shared_counter);

  co_wait(thd1);
  co_wait(thd2);
}

// Test 4: Multiple coroutines with different yield frequencies
static void work_variable_yield(void *arg) {
  int id = *(int *)arg;
  for (int i = 0; i < 30; ++i) {
    printf("Thread %d iteration %d  \n", id, i);

    // Variable yield based on thread ID
    if (id == 1 && i % 3 == 0) co_yield ();
    if (id == 2 && i % 2 == 0) co_yield ();
    if (id == 3) co_yield ();
  }
}

static void test_4() {
  int id1 = 1, id2 = 2, id3 = 3;
  struct co *thd1 = co_start("variable-yield-1", work_variable_yield, &id1);
  struct co *thd2 = co_start("variable-yield-2", work_variable_yield, &id2);
  struct co *thd3 = co_start("variable-yield-3", work_variable_yield, &id3);

  co_wait(thd1);
  co_wait(thd2);
  co_wait(thd3);
}

// Test 5: Stress test with many coroutines
static void stress_work(void *arg) {
  int thread_num = *(int *)arg;
  for (int i = 0; i < 20; ++i) {
    printf("Stress Thread %d iter %d  \n", thread_num, i);
    if (i % 2 == 0) co_yield ();
  }
}

static void test_5() {
#define STRESS_THREADS 10
  struct co *threads[STRESS_THREADS];
  int thread_ids[STRESS_THREADS];

  for (int i = 0; i < STRESS_THREADS; i++) {
    thread_ids[i] = i;
    threads[i] = co_start("stress-thread", stress_work, &thread_ids[i]);
  }

  for (int i = 0; i < STRESS_THREADS; i++) {
    co_wait(threads[i]);
  }
}

// Test 6: Coroutine with nested function calls and yields
static void inner_work(void *arg) {
  int depth = *(int *)arg;
  printf("Inner work at depth %d  \n", depth);

  if (depth > 0) {
    co_yield ();
  }
}

static void nested_work(void *arg) {
  int depth = *(int *)arg;
  char name[30];
  for (int i = 0; i < 10; ++i) {
    printf("\n\nNested thread %d iter %d  \n", depth, i);

    // Simulate nested coroutine-like behavior
    if (i % 3 == 0) {
      sprintf(name, "inner_thread_iter%ddepth%d", i, depth);
      struct co *inner = co_start(name, inner_work, &depth);
      co_wait(inner);
    }
    co_yield ();
  }
}

static void test_6() {
  int depth1 = 1, depth2 = 2;
  struct co *thd1 = co_start("nested-1", nested_work, &depth1);
  struct co *thd2 = co_start("nested-2", nested_work, &depth2);

  co_wait(thd1);
  co_wait(thd2);
}

int main() {
  setbuf(stdout, NULL);

  printf("Test #1. Expect: (X|Y){0, 1, 2, ..., 199}\n");
  test_1();

  printf("\n\nTest #2. Expect: (libco-){200, 201, 202, ..., 399}\n");
  test_2();

  printf("\n\n");
  printf("\n\nTest #3\n");
  test_3();
  printf("\n\nTest #4\n");
  test_4();
  printf("\n\nTest #5\n");
  test_5();
  printf("\n\nTest #6\n");
  test_6();
  printf("\n\n");

  return 0;
}

#include <setjmp.h>
#include <stdio.h>
// If the function that called setjmp has exited (whether by return or by a
// different longjmp higher up the stack), the behavior is undefined. In other
// words, only long jumps up the call stack are allowed. cppreference longjmp
int sjmp(jmp_buf buf) { return setjmp(buf); }
void ljmp(jmp_buf buf, int n) { longjmp(buf, n); }
void example() {
  int n = 0;
  jmp_buf buf;
  sjmp(buf);
  printf("Hello %d\n", n);
  ljmp(buf, n++);
};

int main() {
  example();
  // int n = 0;
  // jmp_buf buf;

  // setjmp(buf);
  // printf("Hello %d\n", n);
  // longjmp(buf, n++);
}

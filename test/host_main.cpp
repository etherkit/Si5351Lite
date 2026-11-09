#include <stdio.h>

extern int run_checks();

int main() {
  int fails = run_checks();
  if (fails != 0) {
    printf("HOST FAIL %d\n", fails);
    return 1;
  }
  printf("HOST PASS\n");
  return 0;
}

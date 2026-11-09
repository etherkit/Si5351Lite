#include <stdint.h>

extern int run_checks();
extern "C" int g_fails;
extern "C" int g_checks;
extern "C" {
volatile uint8_t g_hold = 0;
__attribute__((noinline, used)) void test_done(void) { g_hold = 1; }
}

int main() {
  g_checks = 0x1111;
  g_fails = 0x2222;
  g_fails = run_checks();
  test_done();
  for (;;) {
  }
}

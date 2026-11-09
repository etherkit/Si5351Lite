#include "Si5351Lite.h"

#include <string.h>

#ifndef SI5351LITE_AVR
#include <stdio.h>
#endif

extern "C" {
int g_fails = 0;
int g_checks = 0;
}

static void expect_at(bool ok, const char *what, int line) {
  g_checks++;
  if (ok) {
    return;
  }
  g_fails++;
#ifndef SI5351LITE_AVR
  if (g_fails <= 24) {
    printf("FAIL line %d: %s\n", line, what);
  }
#else
  (void)what;
  (void)line;
#endif
}

#define EXPECT(cond) expect_at(!!(cond), #cond, __LINE__)

static TwoWire g_bus;
static Si5351Lite g_clock(g_bus, 0x60);

struct Snap {
  uint16_t tx;
  uint16_t ends;
  uint8_t begins;
};

static Snap snap() {
  Snap s;
  s.tx = g_bus.tx_calls;
  s.ends = g_bus.end_calls;
  s.begins = g_bus.begin_calls;
  return s;
}

static bool quiet(Snap s) {
  return g_bus.tx_calls == s.tx && g_bus.end_calls == s.ends &&
         g_bus.begin_calls == s.begins;
}

static bool bytes_eq(const uint8_t *got, const uint8_t *exp, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (got[i] != exp[i]) {
      return false;
    }
  }
  return true;
}

static bool region_is(uint8_t reg, const uint8_t *exp, uint8_t n) {
  return bytes_eq(&g_bus.regs[reg], exp, n);
}

static const uint8_t kPll25[8] = {0x00, 0x01, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kPll27[8] = {0x42, 0x40, 0x00, 0x0C, 0xD0, 0xF9, 0x0B, 0x00};
static const uint8_t kMs10[8] = {0x00, 0x01, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kMs14[8] = {0x42, 0x40, 0x00, 0x1A, 0x6B, 0xFC, 0xA1, 0x40};
static const uint8_t kMs100[8] = {0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kMs500k[8] = {0x00, 0x01, 0x03, 0x1E, 0x00, 0x00, 0x00, 0x00};
static const uint8_t kMs32[8] = {0x00, 0x01, 0x00, 0x0A, 0x80, 0x00, 0x00, 0x00};

static bool tuning_regs_only() {
  for (uint8_t i = 0; i < g_bus.log_n; i++) {
    if (g_bus.log[i].n == 0) {
      return false;
    }
    uint8_t reg = g_bus.log[i].buf[0];
    bool ok = (reg == 3) || (reg >= 16 && reg <= 21) || reg == 42 || reg == 50 ||
              reg == 58 || reg == 66 || reg == 74 || reg == 82;
    if (!ok) {
      return false;
    }
  }
  return g_bus.log_overflow == 0;
}

static void test_muldiv_literals() {
  EXPECT(Si5351Lite::test_muldiv_q(65535UL, 65535UL, 1UL) == 4294836225UL);
  EXPECT(Si5351Lite::test_muldiv_r(65535UL, 65535UL, 1UL) == 0);
  EXPECT(Si5351Lite::test_muldiv_q(2000000000UL, 3UL, 2UL) == 3000000000UL);
  EXPECT(Si5351Lite::test_muldiv_r(2000000000UL, 3UL, 2UL) == 0);
  EXPECT(Si5351Lite::test_muldiv_q(0xFFFFFFFFUL, 2UL, 4UL) == 2147483647UL);
  EXPECT(Si5351Lite::test_muldiv_r(0xFFFFFFFFUL, 2UL, 4UL) == 2UL);
  /* 108e6 * 5e5 / 1e9 = 54000 exactly (13.5 kHz at quarter-hertz). */
  EXPECT(Si5351Lite::test_muldiv_q(108000000UL, 500000UL, 1000000000UL) == 54000UL);
  EXPECT(Si5351Lite::test_muldiv_r(108000000UL, 500000UL, 1000000000UL) == 0UL);
  EXPECT(Si5351Lite::test_muldiv_r(100UL, 3UL, 7UL) == 6UL);
  EXPECT(Si5351Lite::test_muldiv_q(100UL, 3UL, 7UL) == 42UL);
  EXPECT(Si5351Lite::test_muldiv_q(0, 5UL, 9UL) == 0);
  EXPECT(Si5351Lite::test_muldiv_q(5UL, 0, 9UL) == 0);
  EXPECT(Si5351Lite::test_muldiv_q(1UL, 1UL, 0) == 0);
}

static void test_plan_literals() {
  Si5351Lite::TestPlan p;
  EXPECT(Si5351Lite::test_plan(25000000UL, 0, p));
  EXPECT(p.a == 32 && p.b == 0 && p.c == 1);
  EXPECT(p.ref_q == 100000000UL);
  EXPECT(p.pll_q == 3200000000UL);

  EXPECT(Si5351Lite::test_plan(27000000UL, 0, p));
  EXPECT(p.a == 29 && p.b == 629630UL && p.c == 1000000UL);
  EXPECT(p.pll_q == 3200000040UL);

  EXPECT(Si5351Lite::test_plan(25000000UL, 1, p));
  EXPECT(p.ref_q == 100000000UL && p.b == 0 && p.pll_q == 3200000000UL);
  EXPECT(Si5351Lite::test_plan(25000000UL, 9, p));
  EXPECT(p.ref_q == 100000000UL);
  EXPECT(Si5351Lite::test_plan(25000000UL, 10, p));
  EXPECT(p.ref_q == 100000001UL && p.a == 32 && p.b == 0);
  EXPECT(p.pll_q == 3200000032UL);
  EXPECT(Si5351Lite::test_plan(25000000UL, -10, p));
  EXPECT(p.ref_q == 99999999UL && p.a == 32 && p.b == 1);
  EXPECT((p.pll_q >> 2) == 800000016UL);
  EXPECT(Si5351Lite::test_plan(27000000UL, 10, p));
  EXPECT((p.pll_q >> 2) == 800000017UL);

  EXPECT(!Si5351Lite::test_plan(0, 0, p));
  EXPECT(!Si5351Lite::test_plan(1000000UL, 0, p));
  EXPECT(!Si5351Lite::test_plan(60000000UL, 0, p));
}

static bool begin25() {
  g_bus.reset();
  return g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0);
}

static void test_begin_image() {
  EXPECT(begin25());
  EXPECT(g_clock.pll_hz() == 800000000UL);
  EXPECT(g_clock.test_begun() == 1);
  EXPECT(g_clock.test_ref_q() == 100000000UL);
  EXPECT(region_is(26, kPll25, 8));
  EXPECT(region_is(34, kPll25, 8));
  EXPECT(g_bus.regs[183] == 0x92);
  EXPECT(g_bus.regs[15] == 0x00);
  EXPECT(g_bus.regs[149] == 0x15);
  EXPECT(g_bus.regs[162] == 0 && g_bus.regs[163] == 0 && g_bus.regs[164] == 0);
  EXPECT(g_bus.regs[177] == 0xA0);
  EXPECT(g_bus.regs[3] == 0xFF);
  for (uint8_t i = 0; i < 6; i++) {
    EXPECT(g_bus.regs[16 + i] == 0x0C);
    EXPECT(g_clock.test_ms_set(i) == 0);
  }
  EXPECT(g_bus.regs[22] == 0x80);
  EXPECT(g_bus.regs[23] == 0x80);
  EXPECT(g_bus.log_overflow == 0);
  EXPECT(g_bus.begin_calls == 1);
  EXPECT(g_bus.log_n > 0 && g_bus.log[0].addr == 0x60 && g_bus.log[0].n == 0);
}

static void test_loads_and_xtals() {
  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_6PF, 25000000UL, 0));
  EXPECT(g_bus.regs[183] == 0x52);
  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_10PF, 27000000UL, 0));
  EXPECT(g_bus.regs[183] == 0xD2);
  EXPECT(g_clock.pll_hz() == 800000010UL);
  EXPECT(region_is(26, kPll27, 8));
  EXPECT(region_is(34, kPll27, 8));

  uint32_t keep = g_clock.pll_hz();
  Snap s = snap();
  uint8_t xtal = g_bus.regs[183];
  EXPECT(!g_clock.begin(static_cast<Si5351Lite::Load>(0), 25000000UL, 0));
  EXPECT(!g_clock.begin(static_cast<Si5351Lite::Load>(0x12), 25000000UL, 0));
  EXPECT(g_clock.pll_hz() == keep);
  EXPECT(g_bus.regs[183] == xtal);
  EXPECT(quiet(s));
}

static void test_validation_silent() {
  g_bus.reset();
  Si5351Lite raw(g_bus, 0x60);
  Snap s = snap();
  EXPECT(!raw.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT(!raw.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_8MA));
  EXPECT(!raw.output_enable(Si5351Lite::Clk::CLK0, false));
  EXPECT(raw.pll_hz() == 0);
  EXPECT(quiet(s));

  EXPECT(!raw.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 500001L));
  EXPECT(!raw.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, -500001L));
  EXPECT(!raw.begin(Si5351Lite::Load::LOAD_8PF, 0, 0));
  EXPECT(!raw.begin(static_cast<Si5351Lite::Load>(0), 25000000UL, 0));
  EXPECT(quiet(s));

  Si5351Lite bad(g_bus, 0x62);
  EXPECT(!bad.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(bad.pll_hz() == 0);
  EXPECT(quiet(s));

  Si5351Lite alt(g_bus, 0x61);
  EXPECT(alt.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 10));
  EXPECT(alt.pll_hz() == 800000008UL);
  EXPECT(g_bus.log[0].addr == 0x61);

  EXPECT(begin25());
  Snap live = snap();
  EXPECT(!g_clock.set_freq(499999UL, Si5351Lite::Clk::CLK0));
  EXPECT(!g_clock.set_freq(100000001UL, Si5351Lite::Clk::CLK0));
  EXPECT(!g_clock.set_freq(10000000UL, static_cast<Si5351Lite::Clk>(6)));
  EXPECT(!g_clock.set_freq(10000000UL, static_cast<Si5351Lite::Clk>(255)));
  EXPECT(!g_clock.set_drive(Si5351Lite::Clk::CLK1, static_cast<Si5351Lite::Drive>(4)));
  EXPECT(!g_clock.set_drive(static_cast<Si5351Lite::Clk>(7),
                           Si5351Lite::Drive::DRIVE_2MA));
  EXPECT(!g_clock.output_enable(static_cast<Si5351Lite::Clk>(9), true));
  EXPECT(!g_clock.output_enable(Si5351Lite::Clk::CLK0, true));
  EXPECT(quiet(live));
  EXPECT(g_clock.test_ms_set(0) == 0);
}

static void test_probe_bounds() {
  g_bus.reset();
  g_bus.fail_on_end = 1;
  EXPECT(!g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(g_clock.pll_hz() == 0);
  EXPECT(g_clock.test_begun() == 0);
  EXPECT(g_bus.regs[3] == 0x00);
  EXPECT(g_bus.regs[26] == 0x11);

  g_bus.reset();
  g_bus.sys_init_ones = 100;
  EXPECT(!g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(g_bus.read_calls == 32);
  EXPECT(g_clock.pll_hz() == 0);
  Snap s = snap();
  EXPECT(!g_clock.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT(quiet(s));

  g_bus.reset();
  g_bus.sys_init_ones = 2;
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(g_bus.read_calls == 4);
  EXPECT(g_clock.pll_hz() == 800000000UL);

  g_bus.reset();
  g_bus.fail_read_n = 2;
  EXPECT(!g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(g_clock.pll_hz() == 0);
  EXPECT(g_bus.regs[3] == 0xFF);
  EXPECT(g_bus.regs[26] == 0x11);
  g_bus.fail_read_n = 0;
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(g_clock.pll_hz() == 800000000UL);
  EXPECT(region_is(26, kPll25, 8));
}

static void test_partial_and_retry() {
  EXPECT(begin25());
  EXPECT(g_clock.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT(region_is(42, kMs10, 8));
  EXPECT(g_bus.regs[16] == 0x4C);
  EXPECT(g_bus.regs[3] == 0xFE);
  EXPECT(g_clock.test_ms_set(0) == 1);

  g_bus.arm_bulk_fail = 1;
  g_bus.bulk_keep = 3;
  EXPECT(!g_clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0));
  EXPECT(g_clock.test_ms_set(0) == 0);
  EXPECT(g_bus.regs[42] == 0x42);
  EXPECT(g_bus.regs[43] == 0x40);
  EXPECT(g_bus.regs[44] == 0x00);
  EXPECT(g_bus.regs[45] == 0x26);
  EXPECT(g_bus.regs[3] == 0xFE);
  Snap s = snap();
  EXPECT(!g_clock.output_enable(Si5351Lite::Clk::CLK0, true));
  EXPECT(quiet(s));

  EXPECT(g_clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0));
  EXPECT(region_is(42, kMs14, 8));
  EXPECT(g_bus.regs[16] == 0x0C);
  EXPECT(g_clock.test_ms_set(0) == 1);
  EXPECT((g_bus.regs[3] & 0x01) == 0);
}

static void test_tuning_independence() {
  EXPECT(begin25());
  uint8_t pll[16];
  memcpy(pll, &g_bus.regs[26], 16);
  uint8_t reset = g_bus.regs[177];
  g_bus.clear_log();

  const Si5351Lite::Clk clks[6] = {
      Si5351Lite::Clk::CLK0, Si5351Lite::Clk::CLK1, Si5351Lite::Clk::CLK2,
      Si5351Lite::Clk::CLK3, Si5351Lite::Clk::CLK4, Si5351Lite::Clk::CLK5};
  const uint32_t hz[6] = {14074000UL, 7074000UL, 10106000UL,
                          3560000UL,  21074000UL, 28074000UL};
  for (uint8_t i = 0; i < 6; i++) {
    EXPECT(g_clock.set_freq(hz[i], clks[i]));
    EXPECT(g_clock.test_ms_set(i) == 1);
  }
  EXPECT(memcmp(pll, &g_bus.regs[26], 16) == 0);
  EXPECT(g_bus.regs[177] == reset);
  EXPECT(tuning_regs_only());
  EXPECT(g_bus.regs[3] == 0xC0);

  uint8_t clk0[8];
  memcpy(clk0, &g_bus.regs[42], 8);
  uint8_t ctrl0 = g_bus.regs[16];
  EXPECT(g_clock.set_freq(1800000UL, Si5351Lite::Clk::CLK3));
  EXPECT(bytes_eq(clk0, &g_bus.regs[42], 8));
  EXPECT(g_bus.regs[16] == ctrl0);
  EXPECT(g_bus.regs[66] != clk0[0] || g_bus.regs[67] != clk0[1] ||
         g_bus.regs[68] != clk0[2] || memcmp(&g_bus.regs[66], clk0, 8) != 0);

  EXPECT(g_clock.output_enable(Si5351Lite::Clk::CLK1, false));
  EXPECT(g_bus.regs[3] == 0xC2);
  EXPECT(bytes_eq(clk0, &g_bus.regs[42], 8));
  EXPECT(g_clock.test_ms_set(1) == 1);
  EXPECT(g_clock.output_enable(Si5351Lite::Clk::CLK1, true));
  EXPECT(g_bus.regs[3] == 0xC0);

  EXPECT(g_clock.output_enable(Si5351Lite::Clk::CLK5, false));
  EXPECT(g_clock.set_freq(28074000UL, Si5351Lite::Clk::CLK5));
  EXPECT((g_bus.regs[3] & 0x20) == 0);
  EXPECT((g_bus.regs[3] & 0x02) == 0);
}

static void test_drive_and_integer() {
  EXPECT(begin25());
  EXPECT(g_clock.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_8MA));
  EXPECT(g_bus.regs[16] == 0x0F);
  EXPECT(g_clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0));
  EXPECT(g_bus.regs[16] == 0x0F);
  EXPECT(region_is(42, kMs14, 8));
  EXPECT(g_clock.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT(g_bus.regs[16] == 0x4F);
  EXPECT(region_is(42, kMs10, 8));
  EXPECT(g_clock.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_4MA));
  EXPECT(g_bus.regs[16] == 0x4D);

  EXPECT(g_clock.set_freq(100000000UL, Si5351Lite::Clk::CLK1));
  EXPECT(region_is(50, kMs100, 8));
  EXPECT((g_bus.regs[17] & 0x40) != 0);
  EXPECT(g_clock.set_freq(500000UL, Si5351Lite::Clk::CLK2));
  EXPECT(region_is(58, kMs500k, 8));
  EXPECT((g_bus.regs[18] & 0x40) != 0);
  EXPECT(g_clock.set_freq(500001UL, Si5351Lite::Clk::CLK2));
  EXPECT((g_bus.regs[18] & 0x40) == 0);
  EXPECT(g_clock.set_freq(32000000UL, Si5351Lite::Clk::CLK3));
  EXPECT(region_is(66, kMs32, 8));
  EXPECT((g_bus.regs[19] & 0x40) == 0);

  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, 0));
  EXPECT(g_clock.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT((g_bus.regs[16] & 0x40) == 0);
  EXPECT(g_bus.regs[42] != kMs10[0] || g_bus.regs[45] != kMs10[3] ||
         memcmp(&g_bus.regs[42], kMs10, 8) != 0);
}

static void test_rebegin() {
  EXPECT(begin25());
  EXPECT(g_clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0));
  EXPECT(g_clock.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_8MA));
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 37));
  EXPECT(g_clock.pll_hz() == 800000024UL);
  EXPECT(g_bus.regs[3] == 0xFF);
  EXPECT(g_bus.regs[16] == 0x0C);
  EXPECT(g_clock.test_ms_set(0) == 0);
  Snap s = snap();
  EXPECT(!g_clock.output_enable(Si5351Lite::Clk::CLK0, true));
  EXPECT(quiet(s));
  EXPECT(g_clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0));
  EXPECT((g_bus.regs[3] & 0x01) == 0);

  uint32_t keep = g_clock.pll_hz();
  uint8_t oe = g_bus.regs[3];
  Snap held = snap();
  EXPECT(!g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 500001L));
  EXPECT(g_clock.pll_hz() == keep);
  EXPECT(g_bus.regs[3] == oe);
  EXPECT(quiet(held));

  g_bus.fail_on_end = (uint16_t)(g_bus.end_calls + 4);
  g_bus.partial_data = 0;
  EXPECT(!g_clock.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, 0));
  EXPECT(g_clock.pll_hz() == 0);
  EXPECT(g_clock.test_begun() == 0);
  g_bus.fail_on_end = 0;
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, 0));
  EXPECT(g_clock.pll_hz() == 800000010UL);
  EXPECT(g_clock.set_freq(7074000UL, Si5351Lite::Clk::CLK4));
  EXPECT(g_clock.test_ms_set(4) == 1);
  EXPECT(g_clock.test_ms_set(0) == 0);
}

static void test_ppb_edges() {
  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 500000L));
  EXPECT(g_clock.pll_hz() != 0);
  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, -500000L));
  EXPECT(g_clock.pll_hz() != 0);
  g_bus.reset();
  EXPECT(g_clock.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, -37));
  EXPECT(g_clock.pll_hz() == 800000014UL);
}

#ifndef SI5351LITE_AVR
static uint32_t ull_div(uint32_t a, uint32_t b, uint32_t d) {
  return (uint32_t)(((unsigned long long)a * b) / d);
}

static void test_muldiv_oracle() {
  uint32_t x = 0x12345678UL;
  for (uint32_t n = 0; n < 20000UL; n++) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    uint32_t a = x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    uint32_t b = x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    uint32_t d = (x % 1000000000UL) + 1UL;
    unsigned long long prod = (unsigned long long)a * b;
    if (prod / d > 0xFFFFFFFFULL) {
      continue;
    }
    EXPECT(Si5351Lite::test_muldiv_q(a, b, d) == (uint32_t)(prod / d));
    EXPECT(Si5351Lite::test_muldiv_r(a, b, d) == (uint32_t)(prod % d));
    if (g_fails > 8) {
      return;
    }
  }
  EXPECT(ull_div(108000000UL, 12345UL, 1000000000UL) ==
         Si5351Lite::test_muldiv_q(108000000UL, 12345UL, 1000000000UL));
}

static void test_error_table() {
  const int32_t ppb[] = {1, 9, 10, 37, 39};
  const double synth[] = {0.0, 0.0, 0.100, 0.050, 0.050};
  const double cal[] = {0.010, 0.090, 0.0, 0.070, 0.090};
  for (uint8_t i = 0; i < 5; i++) {
    Si5351Lite::TestPlan p;
    EXPECT(Si5351Lite::test_plan(25000000UL, ppb[i], p));
    uint32_t fout_q = 10000000UL << 2;
    uint32_t ms_a = p.pll_q / fout_q;
    uint32_t rem = p.pll_q - ms_a * fout_q;
    uint32_t ms_b = (uint32_t)(((unsigned long long)rem * 1000000ULL) / fout_q);
    double pr = (double)p.a + ((double)p.b / 1000000.0);
    double mr = (double)ms_a + ((double)ms_b / 1000000.0);
    double model = ((double)p.ref_q / 4.0) * pr / mr;
    double phys = 25000000.0 * (1.0 + ((double)ppb[i] / 1.0e9));
    double actual = phys * pr / mr;
    double s = model - 10000000.0;
    double c = actual - model;
    double tol = 0.00051;
    EXPECT(s > synth[i] - tol && s < synth[i] + tol);
    EXPECT(c > cal[i] - tol && c < cal[i] + tol);
  }

  Si5351Lite::TestPlan p;
  EXPECT(Si5351Lite::test_plan(25000000UL, 1, p));
  uint32_t hz = 14074000UL;
  uint32_t fq = hz << 2;
  uint32_t ms_a = p.pll_q / fq;
  uint32_t ms_b =
      (uint32_t)(((unsigned long long)(p.pll_q - ms_a * fq) * 1000000ULL) / fq);
  double pr = (double)p.a + ((double)p.b / 1000000.0);
  double mr = (double)ms_a + ((double)ms_b / 1000000.0);
  double model = ((double)p.ref_q / 4.0) * pr / mr;
  double phys = 25000000.0 * (1.0 + 1.0e-9);
  double s = model - (double)hz;
  double c = phys * pr / mr - model;
  EXPECT(s > 0.106 && s < 0.109);
  EXPECT(c > 0.013 && c < 0.015);

  EXPECT(Si5351Lite::test_plan(25000000UL, 37, p));
  ms_a = p.pll_q / fq;
  ms_b = (uint32_t)(((unsigned long long)(p.pll_q - ms_a * fq) * 1000000ULL) / fq);
  pr = (double)p.a + ((double)p.b / 1000000.0);
  mr = (double)ms_a + ((double)ms_b / 1000000.0);
  model = ((double)p.ref_q / 4.0) * pr / mr;
  phys = 25000000.0 * (1.0 + 37.0e-9);
  c = phys * pr / mr - model;
  EXPECT(c > 0.097 && c < 0.100);
}

static void test_two_objects() {
  TwoWire ba;
  TwoWire bb;
  Si5351Lite a(ba, 0x60);
  Si5351Lite b(bb, 0x61);
  EXPECT(a.begin(Si5351Lite::Load::LOAD_8PF, 25000000UL, 0));
  EXPECT(b.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, 12345));
  EXPECT(a.pll_hz() == 800000000UL);
  EXPECT(b.pll_hz() == 800000001UL);
  EXPECT(a.set_freq(10000000UL, Si5351Lite::Clk::CLK0));
  EXPECT(bytes_eq(kMs10, &ba.regs[42], 8));
  EXPECT(bb.regs[42] == 0xA5);
  EXPECT(ba.regs[26] != bb.regs[26] || ba.regs[29] != bb.regs[29] ||
         memcmp(&ba.regs[26], &bb.regs[26], 8) != 0);
}
#endif

int run_checks() {
  g_fails = 0;
  g_checks = 0;
  test_muldiv_literals();
  test_plan_literals();
  test_begin_image();
  test_loads_and_xtals();
  test_validation_silent();
  test_probe_bounds();
  test_partial_and_retry();
  test_tuning_independence();
  test_drive_and_integer();
  test_rebegin();
  test_ppb_edges();
#ifndef SI5351LITE_AVR
  test_muldiv_oracle();
  test_error_table();
  test_two_objects();
  printf("checks %d failures %d\n", g_checks, g_fails);
#else
  if (g_checks < 10) {
    g_fails++;
  }
#endif
  return g_fails;
}

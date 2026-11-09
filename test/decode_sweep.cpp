/* Host sweep over bytes the compiled library writes.

   Ratios are decoded from the AN619 parameter blocks (the inverse of
   P1/P2/P3). They are not taken from a second copy of the planner.
   Double is used only to print hertz. Pass/fail comparisons that can
   overflow 64 bits use __int128. This file is not firmware.
*/

#include "Si5351Lite.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct Ratio {
  uint32_t a;
  uint32_t b;
  uint32_t c;
};

static bool decode_block(const uint8_t *p, Ratio &r) {
  uint32_t p3 = ((uint32_t)(p[5] >> 4) << 16) | ((uint32_t)p[0] << 8) | p[1];
  uint32_t p2 = ((uint32_t)(p[5] & 0x0Fu) << 16) | ((uint32_t)p[6] << 8) | p[7];
  uint32_t p1 = ((uint32_t)(p[2] & 0x03u) << 16) | ((uint32_t)p[3] << 8) | p[4];
  if (p3 == 0) {
    return false;
  }
  uint32_t sum = p1 + 512u;
  uint32_t rem = sum % 128u;
  if ((sum - rem) % 128u != 0) {
    return false;
  }
  unsigned long long bnum = (unsigned long long)p3 * rem + p2;
  if ((bnum % 128ull) != 0ull) {
    return false;
  }
  unsigned long long b = bnum / 128ull;
  if (b > 0xffffffffull) {
    return false;
  }
  r.a = (sum - rem) / 128u;
  r.b = (uint32_t)b;
  r.c = p3;
  return true;
}

static int g_fails = 0;

static void expect(bool ok, const char *msg) {
  if (!ok) {
    if (g_fails < 16) {
      printf("FAIL %s\n", msg);
    }
    g_fails++;
  }
}

static bool decode_self_test() {
  const uint8_t pll25[8] = {0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00};
  const uint8_t pll27[8] = {0x42, 0x40, 0x00, 0x0c, 0xd0, 0xf9, 0x0b, 0x00};
  Ratio r;
  if (!decode_block(pll25, r) || r.a != 32 || r.b != 0 || r.c != 1) {
    return false;
  }
  if (!decode_block(pll27, r) || r.a != 29 || r.b != 629630u || r.c != 1000000u) {
    return false;
  }
  return true;
}

static const uint32_t kHz[] = {
    500000ul,    500001ul,    1800000ul,   3500000ul,   3560000ul,
    3573000ul,   7000000ul,   7074000ul,   8000000ul,   8000001ul,
    10000000ul,  10000001ul,  10100000ul,  10106000ul,  12500000ul,
    12500001ul,  14000000ul,  14074000ul,  16000000ul,  16000001ul,
    18068000ul,  18100000ul,  20000000ul,  20000001ul,  21000000ul,
    21074000ul,  24890000ul,  24915000ul,  25000000ul,  25000001ul,
    28000000ul,  28074000ul,  29000000ul,  40000000ul,  40000001ul,
    50000000ul,  50000001ul,  50125000ul,  50313000ul,  80000000ul,
    80000001ul,  99999999ul,  100000000ul};

static const int32_t kPpb[] = {0, 1, -1, 9, -9, 10, -10, 20, -20, 37, 38, 39,
                               40, 41, -37, -38, -39, -40, -41};

static bool is_focus(uint32_t xo, int32_t ppb, uint32_t hz) {
  return xo == 27000000ul &&
         ((ppb == 10 && hz == 3560000ul) || (ppb == -20 && hz == 14074000ul));
}

/* F_hw < hz/(R*1e6) + 0.25/R, written without rounding the comparison. */
static bool hw_under_full(uint32_t ref_q, const Ratio &pll, uint32_t hz,
                          uint32_t numer) {
  unsigned long long pnum =
      (unsigned long long)pll.a * pll.c + pll.b;
  __int128 lhs = (__int128)ref_q * pnum * 1000000;
  __int128 rhs = (__int128)4 * pll.c *
                 ((__int128)hz * (numer + 1u) + 250000);
  return lhs < rhs;
}

static bool stored_under_ms(uint32_t pll_q, uint32_t hz, uint32_t numer) {
  __int128 lhs = (__int128)pll_q * 1000000;
  __int128 rhs = (__int128)4 * hz * (numer + 1u);
  return lhs < rhs;
}

int main() {
  expect(decode_self_test(), "decoder self-test");

  TwoWire bus;
  Si5351Lite clock(bus, 0x60);
  const uint32_t nfreq = (uint32_t)(sizeof(kHz) / sizeof(kHz[0]));
  const uint32_t nppb = (uint32_t)(sizeof(kPpb) / sizeof(kPpb[0]));
  uint32_t cases = 0;
  uint32_t rejects = 0;
  uint32_t ms_only_exceed = 0;
  uint32_t focus_seen = 0;
  double max_hw = 0;
  double max_cal = 0;
  double max_total = 0;
  double max_gap = 0;

  printf("register-decoded sweep (compiled Si5351Lite bytes)\n");

  for (uint32_t xi = 0; xi < 2; xi++) {
    uint32_t xo = (xi == 0) ? 25000000ul : 27000000ul;
    for (uint32_t pi = 0; pi < nppb; pi++) {
      int32_t ppb = kPpb[pi];
      bus.reset();
      if (!clock.begin(Si5351Lite::Load::LOAD_8PF, xo, ppb)) {
        rejects++;
        printf("begin reject xo=%lu ppb=%ld\n", (unsigned long)xo, (long)ppb);
        continue;
      }
      uint8_t pll_bytes[8];
      memcpy(pll_bytes, &bus.regs[26], 8);
      if (memcmp(&bus.regs[26], &bus.regs[34], 8) != 0) {
        expect(false, "PLL A/B differ");
      }
      Ratio pll;
      if (!decode_block(pll_bytes, pll)) {
        expect(false, "PLL decode");
        continue;
      }
      uint32_t ref_q = clock.test_ref_q();
      uint32_t pll_q = clock.test_pll_q();
      unsigned long long pnum =
          (unsigned long long)pll.a * pll.c + pll.b;
      unsigned long long exact = (unsigned long long)ref_q * pnum;
      if ((exact / pll.c) != pll_q) {
        expect(false, "stored pll_q is not floor(decoded ratio)");
      }

      for (uint32_t fi = 0; fi < nfreq; fi++) {
        uint32_t hz = kHz[fi];
        uint8_t before[16];
        memcpy(before, &bus.regs[26], 16);
        uint8_t reset = bus.regs[177];
        if (!clock.set_freq(hz, Si5351Lite::Clk::CLK0)) {
          rejects++;
          printf("set reject xo=%lu ppb=%ld hz=%lu\n", (unsigned long)xo,
                 (long)ppb, (unsigned long)hz);
          continue;
        }
        if (memcmp(before, &bus.regs[26], 16) != 0 || bus.regs[177] != reset) {
          expect(false, "set_freq wrote PLL");
        }
        Ratio ms;
        if (!decode_block(&bus.regs[42], ms)) {
          expect(false, "MS decode");
          continue;
        }
        cases++;
        uint32_t numer = ms.a * 1000000u + ms.b;
        if (numer == 0) {
          expect(false, "MS numer");
          continue;
        }
        double r_ms = (double)ms.a + (double)ms.b / (double)ms.c;
        double r_pll = (double)pll.a + (double)pll.b / (double)pll.c;
        double model_ref = (double)ref_q / 4.0;
        double f_stored = ((double)pll_q / 4.0) / r_ms;
        double f_hw = model_ref * r_pll / r_ms;
        double phys = (double)xo * (1.0 + (double)ppb / 1.0e9);
        double f_phys = phys * r_pll / r_ms;
        double stored = f_stored - (double)hz;
        double gap = f_hw - f_stored;
        double hw = f_hw - (double)hz;
        double cal = f_phys - f_hw;
        double pll_hz = (double)pll_q / 4.0;
        double ms_bound = ((double)hz * (double)hz) / (pll_hz * 1000000.0);
        double full_bound = (double)hz / (r_ms * 1000000.0) + 0.25 / r_ms;
        double cal_bound = 0.25 * r_pll / r_ms;
        double design_cal = (double)hz * 0.25 / (double)xo;

        if (!stored_under_ms(pll_q, hz, numer)) {
          expect(false, "stored synth over MS bound");
        }
        if (stored < -1e-9 || stored > ms_bound + 1e-6) {
          expect(false, "stored synth double bound");
        }
        if (gap < -1e-9 || !(gap < 0.25 / r_ms)) {
          expect(false, "truncation gap out of range");
        }
        if (!hw_under_full(ref_q, pll, hz, numer) || !(hw < full_bound)) {
          printf("full fail xo=%lu ppb=%ld hz=%lu hw=%.9f full=%.9f\n",
                 (unsigned long)xo, (long)ppb, (unsigned long)hz, hw,
                 full_bound);
          expect(false, "full bound");
        }
        if (!(cal > -cal_bound && cal < cal_bound)) {
          expect(false, "cal bound");
        }
        if (hw > ms_bound + 1e-9) {
          ms_only_exceed++;
          if (stored > ms_bound + 1e-6) {
            expect(false, "exceed row stored synth too big");
          }
          double excess = hw - ms_bound;
          if (excess > gap + 1e-6) {
            expect(false, "excess larger than truncation gap");
          }
          printf(
              "MS-ONLY xo=%lu ppb=%ld hz=%lu stored=%.9f gap=%.9f hw=%.9f "
              "ms_bound=%.9f full=%.9f\n",
              (unsigned long)xo, (long)ppb, (unsigned long)hz, stored, gap, hw,
              ms_bound, full_bound);
        }
        if (is_focus(xo, ppb, hz)) {
          focus_seen++;
          printf(
              "FOCUS xo=%lu ppb=%ld hz=%lu\n"
              "  decoded PLL %lu + %lu/%lu  MS %lu + %lu/%lu\n"
              "  stored_synth %.9f  trunc_gap %.9f  hw_synth %.9f\n"
              "  ms_only_bound %.9f  full_bound %.9f  cal %.9f  cal_bound %.9f\n"
              "  design_cal_bound %.9f  total %.9f\n",
              (unsigned long)xo, (long)ppb, (unsigned long)hz,
              (unsigned long)pll.a, (unsigned long)pll.b, (unsigned long)pll.c,
              (unsigned long)ms.a, (unsigned long)ms.b, (unsigned long)ms.c,
              stored, gap, hw, ms_bound, full_bound, cal, cal_bound, design_cal,
              f_phys - (double)hz);
          if (!(hw > ms_bound)) {
            expect(false, "focus row should exceed MS-only bound");
          }
          if (!(cal > -design_cal && cal < design_cal)) {
            expect(false, "focus cal vs design step");
          }
        }
        double at = f_phys - (double)hz;
        if (at < 0) {
          at = -at;
        }
        if (at > max_total) {
          max_total = at;
        }
        if (hw < 0) {
          hw = -hw;
        }
        if (hw > max_hw) {
          max_hw = hw;
        }
        if (cal < 0) {
          cal = -cal;
        }
        if (cal > max_cal) {
          max_cal = cal;
        }
        if (gap > max_gap) {
          max_gap = gap;
        }
        (void)design_cal;
      }
    }
  }

  /* Worked 10 MHz rows, 25 MHz, from decoded ratios. */
  const int32_t tppb[] = {1, 9, 10, 37, 39};
  const double es[] = {0.0, 0.0, 0.100, 0.050, 0.050};
  const double ec[] = {0.010, 0.090, 0.0, 0.070, 0.090};
  for (int i = 0; i < 5; i++) {
    bus.reset();
    expect(clock.begin(Si5351Lite::Load::LOAD_8PF, 25000000ul, tppb[i]),
           "table begin");
    expect(clock.set_freq(10000000ul, Si5351Lite::Clk::CLK0), "table set");
    Ratio pll;
    Ratio ms;
    expect(decode_block(&bus.regs[26], pll), "table pll");
    expect(decode_block(&bus.regs[42], ms), "table ms");
    double r_pll = (double)pll.a + (double)pll.b / (double)pll.c;
    double r_ms = (double)ms.a + (double)ms.b / (double)ms.c;
    double model_ref = (double)clock.test_ref_q() / 4.0;
    double f_hw = model_ref * r_pll / r_ms;
    double phys = 25000000.0 * (1.0 + (double)tppb[i] / 1.0e9);
    double s = f_hw - 10000000.0;
    double c = phys * r_pll / r_ms - f_hw;
    if (!(s > es[i] - 0.00051 && s < es[i] + 0.00051 && c > ec[i] - 0.00051 &&
          c < ec[i] + 0.00051)) {
      printf("table ppb %ld synth %.6f cal %.6f\n", (long)tppb[i], s, c);
      expect(false, "decoded 10 MHz table");
    }
  }

  expect(focus_seen == 2, "both focus rows");
  expect(rejects == 0, "no rejects");
  printf("cases %lu rejects %lu\n", (unsigned long)cases, (unsigned long)rejects);
  printf("MS-only bound exceeded %lu (truncation gap, not a second planner)\n",
         (unsigned long)ms_only_exceed);
  printf("max |hw synth| %.6f  max |gap| %.6f  max |cal| %.6f  max |total| %.6f\n",
         max_hw, max_gap, max_cal, max_total);
  if (g_fails) {
    printf("DECODE SWEEP FAIL %d\n", g_fails);
    return 1;
  }
  printf("DECODE SWEEP PASS\n");
  return 0;
}

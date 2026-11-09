/*
 * Si5351Lite.cpp - Fixed-PLL Si5351 helper for Arduino
 *
 * Derived from the Etherkit Si5351Arduino library, v2.5.0
 * (commit bc2686967f8673d2202b69caabd5d219916f4e15).
 * This file is a modified work. The original copyright notices
 * and the GPL-3.0-or-later terms are unchanged.
 *
 * Copyright (C) 2015 - 2019 Jason Milldrum <milldrum@gmail.com>
 *                           Dana H. Myers <k6jq@comcast.net>
 *
 * Parameter-block byte order and register bit positions follow
 * clk-si5351.c / clk-si5351.h in the Linux kernel and the Etherkit
 * pack_reg_set() helper.
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Rabeeh Khoury <rabeeh@solid-run.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Si5351Lite.h"

/* AN619 / clk-si5351 map. Names are local; there is no public SI5351_* macro. */
static const uint8_t kRegStatus = 0;
static const uint8_t kSysInit = 0x80;
static const uint8_t kRegOe = 3;
static const uint8_t kRegPllSrc = 15;
static const uint8_t kRegClk0 = 16;
static const uint8_t kRegPllA = 26;
static const uint8_t kRegPllB = 34;
static const uint8_t kRegMs0 = 42;
static const uint8_t kRegSsc = 149;
static const uint8_t kSscEn = 0x80;
static const uint8_t kRegVcxo = 162;
static const uint8_t kRegPllReset = 177;
static const uint8_t kPllResetBoth = 0xA0; /* bit7 PLLB, bit5 PLLA */
static const uint8_t kRegXtal = 183;
static const uint8_t kXtalLowBits = 0x12; /* AN619: bits 5:0 written 010010b */
static const uint8_t kClkPd = 0x80;
static const uint8_t kClkInt = 0x40; /* MSx_INT, register 16-21 bit 6 */
/* Multisynth source, PLLA, 2 mA, powered, fractional. Bits 3:2 = 11. */
static const uint8_t kClkRun = 0x0C;
static const uint8_t kProbeTries = 32;
static const uint32_t kPllTargetQ = 3200000000UL; /* 800 MHz in quarter-hertz */
static const uint32_t kPllMaxQ = 3600000000UL;    /* 900 MHz in quarter-hertz */
static const uint32_t kFoutMin = 500000UL;
static const uint32_t kFoutMax = 100000000UL;
static const uint32_t kXoMin = 25000000UL;
static const uint32_t kXoMax = 27000000UL;
static const int32_t kPpbLimit = 500000L;
static const uint32_t kDenom = 1000000UL;
static const uint32_t kPpbDen = 1000000000UL;
static const uint32_t kPllAMin = 15UL;
static const uint32_t kPllAMax = 90UL;
static const uint32_t kMsAMin = 8UL;
static const uint32_t kMsAMax = 1800UL;

/*
 * floor((a * b) / d) and (a * b) % d.
 * 16-bit limbs build the product. A 64-step restoring division consumes it.
 * d must be 0 or below 2^31. The callers that pass a non-zero d also keep
 * the quotient inside uint32. Those are the only call sites.
 */
__attribute__((noinline)) Si5351Lite::QR Si5351Lite::muldiv_u32(uint32_t a,
                                                                uint32_t b,
                                                                uint32_t d) {
  QR out;
  out.q = 0;
  out.r = 0;
  if (d == 0) {
    return out;
  }

  uint32_t a0 = a & 0xFFFFUL;
  uint32_t a1 = a >> 16;
  uint32_t b0 = b & 0xFFFFUL;
  uint32_t b1 = b >> 16;

  uint32_t p00 = a0 * b0;
  uint32_t p01 = a0 * b1;
  uint32_t p10 = a1 * b0;
  uint32_t p11 = a1 * b1;

  uint32_t mid = (p00 >> 16) + (p01 & 0xFFFFUL) + (p10 & 0xFFFFUL);
  uint32_t lo = (p00 & 0xFFFFUL) | ((mid & 0xFFFFUL) << 16);
  uint32_t hi = p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);

  uint32_t rem = 0;
  uint32_t quot = 0;
  for (uint8_t step = 0; step < 64; step++) {
    uint8_t bit_i = (uint8_t)(63U - step);
    uint32_t bit;
    if (bit_i >= 32U) {
      bit = (hi >> (uint8_t)(bit_i - 32U)) & 1UL;
    } else {
      bit = (lo >> bit_i) & 1UL;
    }
    rem = (rem << 1) | bit;
    if (rem >= d) {
      rem -= d;
      if (bit_i < 32U) {
        quot |= (1UL << bit_i);
      }
    }
  }
  out.q = quot;
  out.r = rem;
  return out;
}

/* AN619 8-byte block. Layout matches Etherkit pack_reg_set() and clk-si5351. */
void Si5351Lite::pack(uint32_t a, uint32_t b, uint32_t c, uint8_t *params) {
  uint32_t frac = (128UL * b) / c;
  uint32_t p1 = (128UL * a) + frac - 512UL;
  uint32_t p2 = (128UL * b) - (c * frac);
  uint32_t p3 = c;
  params[0] = (uint8_t)((p3 >> 8) & 0xFFUL);
  params[1] = (uint8_t)(p3 & 0xFFUL);
  /* Bits 7:2 stay 0: Rx_DIV = 000 (divide by 1) and MSx_DIVBY4 = 00. */
  params[2] = (uint8_t)((p1 >> 16) & 0x03UL);
  params[3] = (uint8_t)((p1 >> 8) & 0xFFUL);
  params[4] = (uint8_t)(p1 & 0xFFUL);
  params[5] = (uint8_t)(((p3 >> 12) & 0xF0UL) | ((p2 >> 16) & 0x0FUL));
  params[6] = (uint8_t)((p2 >> 8) & 0xFFUL);
  params[7] = (uint8_t)(p2 & 0xFFUL);
}

/* AN619 §3.2: feedback a+b/c is from 15 through 90. 90 + b/c with b > 0
 * is above 90. 8791209 Hz at 0 ppb would otherwise be stored as
 * 90 + 999998/1000000. */
bool Si5351Lite::feedback_ok(uint32_t a, uint32_t b) {
  if (a < kPllAMin || a > kPllAMax) {
    return false;
  }
  if (a == kPllAMax && b != 0) {
    return false;
  }
  return true;
}

bool Si5351Lite::plan_pll(uint32_t xo_hz, int32_t corr_ppb, PllPlan &out) {
  if (xo_hz < kXoMin || xo_hz > kXoMax) {
    return false;
  }
  uint32_t base = xo_hz << 2;
  uint32_t mag = (corr_ppb < 0) ? (uint32_t)(-corr_ppb) : (uint32_t)corr_ppb;
  QR delta = muldiv_u32(base, mag, kPpbDen);
  uint32_t ref;
  if (corr_ppb >= 0) {
    ref = base + delta.q;
    if (ref < base) {
      return false;
    }
  } else {
    if (delta.q >= base) {
      return false;
    }
    ref = base - delta.q;
  }
  if (ref == 0 || ref > kPllTargetQ) {
    return false;
  }

  uint32_t a = kPllTargetQ / ref;
  uint32_t rem = kPllTargetQ - (a * ref);
  uint32_t b = 0;
  uint32_t c = 1;
  if (rem != 0) {
    QR part = muldiv_u32(rem, kDenom, ref);
    b = part.q;
    if (part.r != 0) {
      b++;
    }
    if (b >= kDenom) {
      if (a >= kPllAMax) {
        return false;
      }
      a++;
      b = 0;
      c = 1;
    } else {
      c = kDenom;
    }
  }
  if (!feedback_ok(a, b)) {
    return false;
  }
  /* a is 15..90 and ref is at most the 800 MHz target, so a*ref fits
   * in uint32. A division overflow test here is lowered to a widening
   * multiply on AVR and would pull in 64-bit helpers. */
  uint32_t pll_q = a * ref;
  if (b != 0) {
    QR frac = muldiv_u32(ref, b, c);
    uint32_t sum = pll_q + frac.q;
    if (sum < pll_q) {
      return false;
    }
    pll_q = sum;
  }
  if (pll_q < kPllTargetQ || pll_q > kPllMaxQ) {
    return false;
  }
  /* pll_q is floor(ref_q * (a + b/c)) in quarter-hertz. The PLL registers
   * store the exact rational. Multisynth numerators are computed from
   * pll_q, so the programmed VCO can sit up to, but not including, one
   * quarter-hertz above the value those numerators used. */
  out.ref_q = ref;
  out.pll_q = pll_q;
  out.a = a;
  out.b = b;
  out.c = c;
  return true;
}

bool Si5351Lite::plan_ms(uint32_t hz, MsPlan &ms) const {
  if (hz > (0xFFFFFFFFUL >> 2) || pll_q_ == 0) {
    return false;
  }
  uint32_t fout_q = hz << 2;
  if (fout_q == 0 || pll_q_ < fout_q) {
    return false;
  }
  uint32_t a = pll_q_ / fout_q;
  if (a < kMsAMin || a > kMsAMax) {
    return false;
  }
  uint32_t rem = pll_q_ - (a * fout_q);
  uint32_t b = 0;
  uint32_t c = 1;
  if (rem != 0) {
    QR part = muldiv_u32(rem, kDenom, fout_q);
    b = part.q;
    if (b != 0) {
      c = kDenom;
    }
  }
  ms.a = a;
  ms.b = b;
  ms.c = c;
  pack(a, b, c, ms.params);
  return true;
}

bool Si5351Lite::load_ok(Load load) {
  return load == Load::LOAD_6PF || load == Load::LOAD_8PF ||
         load == Load::LOAD_10PF;
}

bool Si5351Lite::drive_ok(Drive drive) {
  uint8_t v = static_cast<uint8_t>(drive);
  return v == static_cast<uint8_t>(Drive::DRIVE_2MA) ||
         v == static_cast<uint8_t>(Drive::DRIVE_4MA) ||
         v == static_cast<uint8_t>(Drive::DRIVE_6MA) ||
         v == static_cast<uint8_t>(Drive::DRIVE_8MA);
}

bool Si5351Lite::clk_ok(Clk clk) {
  return static_cast<uint8_t>(clk) <= static_cast<uint8_t>(Clk::CLK5);
}

bool Si5351Lite::ppb_ok(int32_t corr_ppb) {
  return corr_ppb <= kPpbLimit && corr_ppb >= -kPpbLimit;
}

bool Si5351Lite::addr_ok() const {
  return addr_ == 0x60 || addr_ == 0x61;
}

bool Si5351Lite::write8(uint8_t reg, uint8_t val) {
  bus_->beginTransmission(addr_);
  if (bus_->write(reg) != 1 || bus_->write(val) != 1) {
    (void)bus_->endTransmission();
    return false;
  }
  return bus_->endTransmission() == 0;
}

bool Si5351Lite::write_n(uint8_t reg, const uint8_t *data, uint8_t len) {
  bus_->beginTransmission(addr_);
  if (bus_->write(reg) != 1) {
    (void)bus_->endTransmission();
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    if (bus_->write(data[i]) != 1) {
      (void)bus_->endTransmission();
      return false;
    }
  }
  return bus_->endTransmission() == 0;
}

bool Si5351Lite::read8(uint8_t reg, uint8_t &val) {
  bus_->beginTransmission(addr_);
  if (bus_->write(reg) != 1) {
    (void)bus_->endTransmission();
    return false;
  }
  if (bus_->endTransmission() != 0) {
    return false;
  }
  if (bus_->requestFrom(addr_, (uint8_t)1) != 1) {
    return false;
  }
  int got = bus_->read();
  if (got < 0) {
    return false;
  }
  val = (uint8_t)got;
  return true;
}

bool Si5351Lite::probe() {
  bus_->beginTransmission(addr_);
  if (bus_->endTransmission() != 0) {
    return false;
  }
  for (uint8_t i = 0; i < kProbeTries; i++) {
    uint8_t status = kSysInit;
    if (!read8(kRegStatus, status)) {
      return false;
    }
    if ((status & kSysInit) == 0) {
      return true;
    }
  }
  return false;
}

/*
 * Safe order: disable pins, power the drivers down, select the crystal,
 * clear spread-spectrum enable, clear the VCXO word, program both PLLs,
 * reset both PLLs, then power CLK0-5 as multisynth/PLLA/2 mA.
 * CLK6-7 stay powered down. Register 3 is written again at the end so a
 * pin cannot be enabled before a multisynth is programmed.
 * Any false return may have committed a prefix of these writes.
 */
bool Si5351Lite::bringup(Load load, const PllPlan &plan) {
  if (!probe()) {
    return false;
  }
  if (!write8(kRegOe, 0xFF)) {
    return false;
  }

  uint8_t down[8];
  for (uint8_t i = 0; i < 8; i++) {
    down[i] = kClkPd;
  }
  if (!write_n(kRegClk0, down, 8)) {
    return false;
  }

  uint8_t xtal = (uint8_t)(static_cast<uint8_t>(load) | kXtalLowBits);
  if (!write8(kRegXtal, xtal)) {
    return false;
  }
  /* Both PLL source bits clear: XTAL, not CLKIN. Reset value of the
   * reserved bits is 0, which is what AN619 calls the default. */
  if (!write8(kRegPllSrc, 0x00)) {
    return false;
  }

  uint8_t ssc = 0;
  if (!read8(kRegSsc, ssc)) {
    return false;
  }
  if (!write8(kRegSsc, (uint8_t)(ssc & (uint8_t)~kSscEn))) {
    return false;
  }

  uint8_t vcxo[3];
  vcxo[0] = 0;
  vcxo[1] = 0;
  vcxo[2] = 0;
  if (!write_n(kRegVcxo, vcxo, 3)) {
    return false;
  }

  uint8_t pll[8];
  pack(plan.a, plan.b, plan.c, pll);
  if (!write_n(kRegPllA, pll, 8)) {
    return false;
  }
  if (!write_n(kRegPllB, pll, 8)) {
    return false;
  }
  if (!write8(kRegPllReset, kPllResetBoth)) {
    return false;
  }

  uint8_t ctrl[6];
  for (uint8_t i = 0; i < 6; i++) {
    ctrl[i] = kClkRun;
  }
  if (!write_n(kRegClk0, ctrl, 6)) {
    return false;
  }
  if (!write8(kRegOe, 0xFF)) {
    return false;
  }
  return true;
}

void Si5351Lite::commit(const PllPlan &plan) {
  ref_q_ = plan.ref_q;
  pll_q_ = plan.pll_q;
  oe_ = 0xFF;
  begun_ = 1;
  for (uint8_t i = 0; i < 6; i++) {
    ms_set_[i] = 0;
    clk_ctrl_[i] = kClkRun;
  }
}

void Si5351Lite::clear_runtime() {
  ref_q_ = 0;
  pll_q_ = 0;
  oe_ = 0xFF;
  begun_ = 0;
  for (uint8_t i = 0; i < 6; i++) {
    ms_set_[i] = 0;
    clk_ctrl_[i] = kClkRun;
  }
}

Si5351Lite::Si5351Lite(TwoWire &bus, uint8_t addr)
    : bus_(&bus), addr_(addr) {
  clear_runtime();
}

bool Si5351Lite::begin(Load load, uint32_t xo_hz, int32_t corr_ppb) {
  if (!load_ok(load) || !addr_ok() || !ppb_ok(corr_ppb)) {
    return false;
  }
  PllPlan plan;
  if (!plan_pll(xo_hz, corr_ppb, plan)) {
    return false;
  }
  bus_->begin();
  if (!bringup(load, plan)) {
    clear_runtime();
    return false;
  }
  commit(plan);
  return true;
}

bool Si5351Lite::set_freq(uint32_t hz, Clk clk) {
  if (!clk_ok(clk) || hz < kFoutMin || hz > kFoutMax || !begun_) {
    return false;
  }
  MsPlan ms;
  if (!plan_ms(hz, ms)) {
    return false;
  }
  uint8_t n = static_cast<uint8_t>(clk);
  uint8_t reg = (uint8_t)(kRegMs0 + (uint8_t)(n * 8));
  if (!write_n(reg, ms.params, 8)) {
    ms_set_[n] = 0;
    return false;
  }

  /* AN619 4.1.2.1: MSx_INT may be set for an even integer divider.
   * Odd integers stay with the bit clear. Fractional b != 0 clears it. */
  uint8_t ctrl = clk_ctrl_[n];
  if (ms.b == 0 && (ms.a & 1UL) == 0) {
    ctrl = (uint8_t)(ctrl | kClkInt);
  } else {
    ctrl = (uint8_t)(ctrl & (uint8_t)~kClkInt);
  }
  if (!write8((uint8_t)(kRegClk0 + n), ctrl)) {
    ms_set_[n] = 0;
    return false;
  }

  uint8_t bit = (uint8_t)(1U << n);
  uint8_t mask = (uint8_t)(oe_ & (uint8_t)~bit);
  if (!write8(kRegOe, mask)) {
    ms_set_[n] = 0;
    return false;
  }
  clk_ctrl_[n] = ctrl;
  oe_ = mask;
  ms_set_[n] = 1;
  return true;
}

bool Si5351Lite::output_enable(Clk clk, bool on) {
  if (!clk_ok(clk) || !begun_) {
    return false;
  }
  uint8_t n = static_cast<uint8_t>(clk);
  if (on && ms_set_[n] == 0) {
    return false;
  }
  uint8_t bit = (uint8_t)(1U << n);
  uint8_t mask = on ? (uint8_t)(oe_ & (uint8_t)~bit) : (uint8_t)(oe_ | bit);
  if (!write8(kRegOe, mask)) {
    return false;
  }
  oe_ = mask;
  return true;
}

bool Si5351Lite::set_drive(Clk clk, Drive drive) {
  if (!clk_ok(clk) || !drive_ok(drive) || !begun_) {
    return false;
  }
  uint8_t n = static_cast<uint8_t>(clk);
  uint8_t ctrl = (uint8_t)((clk_ctrl_[n] & (uint8_t)~0x03) |
                           static_cast<uint8_t>(drive));
  if (!write8((uint8_t)(kRegClk0 + n), ctrl)) {
    return false;
  }
  clk_ctrl_[n] = ctrl;
  return true;
}

uint32_t Si5351Lite::pll_hz() const {
  if (!begun_) {
    return 0;
  }
  return pll_q_ >> 2;
}

#ifdef SI5351LITE_TEST
uint32_t Si5351Lite::test_muldiv_q(uint32_t a, uint32_t b, uint32_t d) {
  return muldiv_u32(a, b, d).q;
}

uint32_t Si5351Lite::test_muldiv_r(uint32_t a, uint32_t b, uint32_t d) {
  return muldiv_u32(a, b, d).r;
}

bool Si5351Lite::test_feedback_ok(uint32_t a, uint32_t b) {
  return feedback_ok(a, b);
}

bool Si5351Lite::test_plan(uint32_t xo_hz, int32_t ppb, TestPlan &out) {
  PllPlan plan;
  if (!plan_pll(xo_hz, ppb, plan)) {
    return false;
  }
  out.ref_q = plan.ref_q;
  out.pll_q = plan.pll_q;
  out.a = plan.a;
  out.b = plan.b;
  out.c = plan.c;
  return true;
}
#endif

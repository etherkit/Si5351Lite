/*
 * Si5351Lite.h - Fixed-PLL Si5351 helper for Arduino
 *
 * Derived from the Etherkit Si5351Arduino library, v2.5.0
 * (commit bc2686967f8673d2202b69caabd5d219916f4e15).
 * This file is a modified work. The original copyright notices
 * and the GPL-3.0-or-later terms are unchanged.
 *
 * Copyright (C) 2015 - 2019 Jason Milldrum <milldrum@gmail.com>
 *                           Dana H. Myers <k6jq@comcast.net>
 *
 * Register bit positions used by this library are derived from
 * clk-si5351.h in the Linux kernel.
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

#ifndef SI5351LITE_H_
#define SI5351LITE_H_

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

/*
 * Whole-hertz CLK0-CLK5 helper. Calibration is applied only in begin().
 * set_freq() retunes one multisynth and does not write either PLL.
 *
 * Enumerators are scoped. The call form is Si5351Lite::Load::LOAD_8PF,
 * Si5351Lite::Clk::CLK0, and Si5351Lite::Drive::DRIVE_8MA.
 *
 * A false return is not a promise that the chip is unchanged once a
 * transaction has started. Only a validation failure avoids Wire.
 * See the library README and docs/DESIGN.md.
 */
class Si5351Lite {
 public:
  enum class Load : uint8_t {
    LOAD_6PF = 0x40,
    LOAD_8PF = 0x80,
    LOAD_10PF = 0xC0
  };

  enum class Clk : uint8_t {
    CLK0 = 0,
    CLK1,
    CLK2,
    CLK3,
    CLK4,
    CLK5
  };

  enum class Drive : uint8_t {
    DRIVE_2MA = 0,
    DRIVE_4MA = 1,
    DRIVE_6MA = 2,
    DRIVE_8MA = 3
  };

  explicit Si5351Lite(TwoWire &bus = Wire, uint8_t addr = 0x60);

  /* Crystal load, nominal crystal hertz, and ppb correction.
   * xo_hz is inclusive 25000000..27000000. Positive corr_ppb means the
   * crystal runs high (v2.5.0 sign). Accepted ppb is -500000..500000.
   * addr must be 0x60 or 0x61. Feedback a+b/c is from 15 through 90,
   * and 90 is allowed only when b is 0.
   * On success both PLLs hold one ratio and every output is disabled.
   * On a validation failure the previous successful begin() stays and
   * this call does not use the bus. On a bus failure the runtime model
   * is dropped (pll_hz() is 0). */
  bool begin(Load load, uint32_t xo_hz, int32_t corr_ppb = 0);

  /* Program one multisynth, then enable that output.
   * hz is inclusive 500000..100000000. The multisynth block forces
   * Rx_DIV to divide-by-1. Does not write PLL registers. */
  bool set_freq(uint32_t hz, Clk clk);

  /* on == true requires a successful set_freq on this clock since the
   * last begin(), and since the last set_freq that failed on the bus.
   * on == false disables the pin. The next successful set_freq enables it. */
  bool output_enable(Clk clk, bool on);

  /* Drive bits only. Integer-mode and source bits already stored are kept. */
  bool set_drive(Clk clk, Drive drive);

  /* Whole-hertz floor of the PLL model programmed by the last begin().
   * 0 when begin() has not succeeded or the last begin() failed on the bus. */
  uint32_t pll_hz() const;

#ifdef SI5351LITE_TEST
  struct TestPlan {
    uint32_t ref_q;
    uint32_t pll_q;
    uint32_t a;
    uint32_t b;
    uint32_t c;
  };

  static uint32_t test_muldiv_q(uint32_t a, uint32_t b, uint32_t d);
  static uint32_t test_muldiv_r(uint32_t a, uint32_t b, uint32_t d);
  static bool test_plan(uint32_t xo_hz, int32_t ppb, TestPlan &out);
  static bool test_feedback_ok(uint32_t a, uint32_t b);
  uint32_t test_ref_q() const { return ref_q_; }
  uint32_t test_pll_q() const { return pll_q_; }
  uint8_t test_begun() const { return begun_; }
  uint8_t test_ms_set(uint8_t n) const { return (n < 6) ? ms_set_[n] : 0; }
#endif

 private:
  struct QR {
    uint32_t q;
    uint32_t r;
  };

  struct PllPlan {
    uint32_t ref_q;
    uint32_t pll_q;
    uint32_t a;
    uint32_t b;
    uint32_t c;
  };

  struct MsPlan {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint8_t params[8];
  };

  static bool load_ok(Load load);
  static bool drive_ok(Drive drive);
  static bool clk_ok(Clk clk);
  static bool ppb_ok(int32_t corr_ppb);
  bool addr_ok() const;

  static QR muldiv_u32(uint32_t a, uint32_t b, uint32_t d);
  static bool feedback_ok(uint32_t a, uint32_t b);
  static bool plan_pll(uint32_t xo_hz, int32_t corr_ppb, PllPlan &out);
  bool plan_ms(uint32_t hz, MsPlan &ms) const;
  static void pack(uint32_t a, uint32_t b, uint32_t c, uint8_t *params);

  bool probe();
  bool bringup(Load load, const PllPlan &plan);
  bool write8(uint8_t reg, uint8_t val);
  bool write_n(uint8_t reg, const uint8_t *data, uint8_t len);
  bool read8(uint8_t reg, uint8_t &val);
  void commit(const PllPlan &plan);
  void clear_runtime();

  TwoWire *bus_;
  uint8_t addr_;
  uint8_t begun_;
  uint32_t ref_q_;
  uint32_t pll_q_;
  uint8_t ms_set_[6];
  uint8_t clk_ctrl_[6];
  uint8_t oe_;
};

#endif

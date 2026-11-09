/*
 * Runtime tuning fixture for an ATtiny3226 built with megaTinyCore.
 *
 * FQBN used for the size check:
 *   megaTinyCore:megaavr:atxy6:chip=3226,clock=20internal,millis=enabled,wiremode=mors,printf=default
 *
 * Default Wire pins on this 20-pin part are SDA PB1 and SCL PB0
 * (megaTinyCore pins_arduino.h). Wire.begin() inside begin() uses those.
 *
 * No Serial and no display. Volatile inputs keep calibration and the
 * frequency walk in the linked image. This sketch is a review build,
 * not a spectrum measurement and not a hardware proof.
 */

#include <Si5351Lite.h>

Si5351Lite clock;

static const uint32_t kFreqs[9] = {
    1800000UL,  3560000UL,  7074000UL, 10106000UL, 14074000UL,
    18068000UL, 21074000UL, 24915000UL, 28074000UL};

volatile uint8_t idx = 0;
volatile uint32_t nudge = 0;
volatile int32_t corr_ppb = 12345;
volatile uint32_t xo_hz = 25000000UL;
volatile uint8_t result = 0;
volatile uint32_t last_pll = 0;

static uint32_t next_hz() {
  uint8_t i = (uint8_t)(idx % 9);
  uint32_t hz = kFreqs[i] + nudge;
  if (hz < 500000UL) {
    hz = 500000UL;
  }
  if (hz > 100000000UL) {
    hz = 100000000UL;
  }
  return hz;
}

void setup() {
  if (!clock.begin(Si5351Lite::Load::LOAD_8PF, xo_hz, corr_ppb)) {
    result = 1;
    return;
  }
  last_pll = clock.pll_hz();
  if (last_pll == 0) {
    result = 2;
    return;
  }
  if (!clock.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_8MA)) {
    result = 3;
    return;
  }
  if (!clock.set_freq(next_hz(), Si5351Lite::Clk::CLK0)) {
    result = 4;
    return;
  }
  if (!clock.output_enable(Si5351Lite::Clk::CLK0, true)) {
    result = 5;
    return;
  }
  result = 0;
}

void loop() {
  idx = (uint8_t)((idx + 1) % 9);
  if (!clock.set_freq(next_hz(), Si5351Lite::Clk::CLK0)) {
    result = 6;
  }
  last_pll = clock.pll_hz();
}

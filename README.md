# Etherkit Si5351Lite

Small Arduino library for the Si5351: CLK0–CLK5, whole-hertz tuning,
one calibration at startup. It is derived from
[Si5351Arduino](https://github.com/etherkit/Si5351Arduino) v2.5.0
(`bc2686967f8673d2202b69caabd5d219916f4e15`). That repository is not
modified by this tree.

This tree is a review build. It is not a release, not a spectrum plot,
and not a hardware proof. GPL-3.0-or-later. Copyright notices for
Jason Milldrum, Dana H. Myers, Sebastian Hesselbarth, and Rabeeh Khoury
are in the sources and in `LICENSE`.

## Sketch

```cpp
#include <Si5351Lite.h>

Si5351Lite clock;

void setup() {
  if (!clock.begin(Si5351Lite::Load::LOAD_8PF, 27000000UL, 12345)) {
    return;
  }
  if (!clock.set_drive(Si5351Lite::Clk::CLK0, Si5351Lite::Drive::DRIVE_8MA)) {
    return;
  }
  if (!clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0)) {
    return;
  }
}

void loop() {
  clock.set_freq(14074000UL, Si5351Lite::Clk::CLK0);
}
```

`begin(load, xo_hz)` is zero correction. `set_freq` does not write either
PLL. Changing `corr_ppb` means calling `begin` again and then `set_freq`
for each output. See `docs/DESIGN.md` for the quarter-hertz model, the
register-decoded error bound, and what a `false` return does and does not
promise.

`examples/Tuning` is an ATtiny3226 walk of nine HF frequencies. No Serial
and no display. Default Wire pins on this part in megaTinyCore are SDA PB1
and SCL PB0.

## Reproduce

From this directory, with arduino-cli 1.5.1, megaTinyCore 2.6.11, and the
DxCore avr-gcc 7.3.0 toolchain on `PATH` or under `~/ .arduino15`:

```sh
bash tools/check.sh
```

That runs the host tests, the register-decoded sweep, the integer
cross-check, an atmega328p simavr run of the same checks (16-bit `int`),
and:

```sh
arduino-cli compile --fqbn megaTinyCore:megaavr:atxy6:chip=3226,clock=20internal,millis=enabled,wiremode=mors,printf=default --library . examples/Tuning
```

Flash claim for that compile is the CLI line **Sketch uses 4892 bytes**
(`.text + .rodata + .data`). RAM is **103 bytes** (`.data + .bss`).
Details and the reason `.text + .data` alone is the wrong flash total are
in `docs/DESIGN.md`.

`test/decode_sweep` is the bound evidence: it decodes PLL and multisynth
parameter blocks from the bytes the compiled library wrote. `test/sweep.py`
repeats the integer planner and the published 10 MHz rows; it is not a
substitute for that decode.

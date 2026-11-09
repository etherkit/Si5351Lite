# Si5351Lite contract

Review build. Upstream `etherkit/Si5351Arduino` v2.5.0
(`bc2686967f8673d2202b69caabd5d219916f4e15`) was read and not modified.
Nothing here is a spectrum measurement or a hardware proof.

Public behavior is the code in `src/Si5351Lite.h`. Enumerators are

```cpp
enum class Load : uint8_t { LOAD_6PF = 0x40, LOAD_8PF = 0x80, LOAD_10PF = 0xC0 };
enum class Clk : uint8_t { CLK0 = 0, CLK1, CLK2, CLK3, CLK4, CLK5 };
enum class Drive : uint8_t { DRIVE_2MA = 0, DRIVE_4MA = 1, DRIVE_6MA = 2, DRIVE_8MA = 3 };
```

Call sites use `Si5351Lite::Load::LOAD_8PF`, `Si5351Lite::Clk::CLK0`, and
`Si5351Lite::Drive::DRIVE_8MA`. A bare integer does not convert. A
`static_cast` outside those enumerators is rejected before any Wire call.

## Accepted inputs

| Argument | Accepted | Rejected with no Wire call |
|---|---|---|
| `xo_hz` | 25000000 through 27000000 inclusive | 24999999, 27000001, 8791209, 0 |
| `corr_ppb` | −500000 through +500000 | 500001, −500001 |
| `addr` | 0x60 or 0x61 | any other value |
| `load` | `LOAD_6PF`, `LOAD_8PF`, `LOAD_10PF` | `0`, and any other bit pattern |
| `hz` | 500000 through 100000000 inclusive | 499999, 100000001 |
| `clk` | CLK0–CLK5 | a cast of 6 or 255 |
| `drive` | the four drive enumerators | a cast of 4 |

Positive `corr_ppb` means the crystal runs high, the same sign as v2.5.0.
Nominal crystals between the two common values, including 26000000 Hz, are
inside the window. The 0.25 Hz reference step is 10 ppb at 25 MHz and the
first step at 27 MHz is also 10 ppb because the division truncates.

AN619 §3.2 limits feedback `a + b/c` to the closed range from 15 through 90.
`90 + b/c` with `b > 0` is above 90. `8791209` Hz at 0 ppb would be
`90 + 999998/1000000`. That crystal is outside the window, and
`feedback_ok` rejects `a == 90 && b != 0` even if a later edit widens the
window. Every nominal hertz from 25000000 through 27000000 at ppb
`{0, −500000, +500000}` — 6000003 plans — produces a legal ratio. That
sweep is necessary, not a sample of the error bound.

Validation failure does not call `Wire.begin()`, does not change a previous
successful PLL model, and does not clear clock marks. A `set_freq` bus
failure keeps that PLL model (`begun` stays set) and clears only that
clock's programmed mark; the chip may already hold a prefix of the new
multisynth bytes. Only a `begin` failure after the first bus transaction
clears the model, so `pll_hz()` becomes 0. That zero is not a muted pin.
`output_enable(false)` returns without a transfer while `begun` is clear,
including after that failed re-begin, so an output enabled by an earlier
`set_freq` can still be running.

## 500000 Hz

The inclusive low end is 500000 Hz, not "below 500 kHz" and not "above
500 kHz". 499999 Hz is rejected with no bus write. AN619 uses the output
multisynth from 500 kHz upward and the R divider for outputs below about
500 kHz. This library does not offer that R-divider path. Each multisynth
program writes the parameter byte at offset 2 with bits 7:2 clear, so
`Rx_DIV` is 000 (divide by 1) and `MSx_DIVBY4` is 00. A previous R divider
does not remain set on a clock this library programs.

## Output error

Two different PLL numbers are in play.

| Quantity | Meaning |
|---|---|
| Stored model | `pll_q = floor(ref_q * (a + b/c))` quarter-hertz. Multisynth numerators divide this integer. |
| Programmed ratio | `a + b/c` decoded from the PLL parameter block. AN619 packing is exact for denominator 1 or 1000000. |

`F_stored = (pll_q / 4) / R_ms` and `F_hw = (ref_q / 4) * R_pll / R_ms`,
with both ratios decoded from the bytes the library wrote.
`0 ≤ F_hw − F_stored < 0.25 / R_ms`.

The multisynth floor against the stored model is

```
0 ≤ F_stored − F_req < F_req / (R_ms * 1e6)
```

The programmed synthesizer error, still at the corrected reference, is

```
0 ≤ F_hw − F_req < F_req / (R_ms * 1e6) + 0.25 / R_ms
```

On the accepted range `R_ms ≥ 8` and `F_req ≤ 100000000`, so

```
0 ≤ F_hw − F_req < 12.53125 Hz
```

That 12.53125 Hz figure is `100000000 / (8 * 1e6) + 0.25 / 8`. It is the
contract ceiling for the synthesizer term. It is not a measured frequency.

Calibration is separate. The ppb division discards less than one
quarter-hertz of reference:

```
|F_phys − F_hw| < 0.25 * R_pll / R_ms
```

`F_phys` uses `xo * (1 + ppb/1e9)` and the same decoded ratios. Add that
remainder to the synthesizer term. Do not fold it into the 12.53125 Hz
ceiling.

A 1634-point grid (25 and 27 MHz, a short ppb list, a short frequency list)
is a regression sample. Its largest programmed synthesizer error is about
7.9 Hz and its largest total is about 8.26 Hz. Those numbers are not API
ceilings. Decoded registers at 25 MHz give:

| ppb | Hz | Synthesizer term | Total with calibration |
|---:|---:|---:|---:|
| −10 | 99993640 | 12.528064430 Hz | 12.528064430 Hz |
| +499999 | 99993890 | 12.511509001 Hz | 13.411004394 Hz |

Both synthesizer terms are under 12.53125 Hz. The second total is larger
because the calibration remainder is added.

Fifteen sample rows sit above the narrower ceiling
`F_req² / (F_stored * 1e6)`. That ceiling bounds `F_stored − F_req` only.
The excess is the truncation gap and is under `0.25 / R_ms`. Thirteen of
those rows cross by less than 2 µHz. Two are larger and are kept in the
test: 27 MHz at +10 ppb and 3560000 Hz (programmed synth 0.016053 Hz,
ceiling 0.015842 Hz), and 27 MHz at −20 ppb and 14074000 Hz (0.250081 Hz
versus 0.247597 Hz).

## Pins the registers do not override

Register 3 bit 1 disables that output; bit 0 enables it. This library
writes those bits. Register 9 resets to 0, which means the OEB pin still
controls each output. This library does not write register 9. A board that
holds OEB in the disable state keeps the pin off after `set_freq` clears
the register-3 bit. Registers 24 and 25 are left at their reset value, so a
disabled output is driven low.

`SSC_EN` (register 149 bit 7) is cleared. On Si5351A/B the spread-spectrum
enable is the logical OR of that bit and the SSEN pin. Clearing the
register does not turn spread spectrum off if the board holds SSEN high.
The library assumes SSEN is not asserted.

## Packages

The call `set_freq(hz, CLK0)` through `CLK5` is the API. It is not a claim
that every Si5351A bond-out has six clock pins.

| Part | Package | Pins this library can drive | Init requirement |
|---|---|---|---|
| Si5351A-B-GT | 10-MSOP, 3 outputs | CLK0, CLK1, CLK2 | AN619 register 19, 20, and 21: write `CLK3_PDN`, `CLK4_PDN`, and `CLK5_PDN` to 1. CLK6 and CLK7 have no pins either. |
| Si5351A 8-output | 20-QFN (and the 16-QFN programming note in AN1234, not repeated here) | CLK0–CLK5 are real pads. CLK6 and CLK7 exist and stay powered down; this library has no CLK6/7 API. | Power a CLK3, CLK4, or CLK5 driver only when that output is programmed. |

`begin()` writes `0x80` to registers 16–23, so every driver starts powered
down, including CLK4 and CLK5. `set_freq` clears power-down only on the one
CLK0–5 it programs and forces that driver to the multisynth. On a 10-MSOP,
call only CLK0–CLK2. Calling CLK3, CLK4, or CLK5 there powers a driver AN619
says to leave down; the API still accepts those enumerators for the
8-output packages.

## Initialization

`begin` checks load, address, ppb, the 25–27 MHz window, and the feedback
ratio before any Wire call. Then: address probe, at most 32 reads of
register 0 until `SYS_INIT` is clear, output-enable `0xFF`, power-down of
CLK0–7 (`0x80` in registers 16–23), crystal load in register 183 with bits
5:0 set to `010010b`, register 15 cleared so both PLLs use the crystal,
spread-spectrum `SSC_EN` cleared by a read-modify-write, registers 162–164
written 0, both PLL blocks, PLL reset `0xA0`, and output-enable `0xFF`
again. Drivers stay powered down. `set_freq` is what powers one CLK0–5
driver and then clears that output's disable bit.

A NACK can leave a prefix of those writes in the chip. The library does
not read the map back.

`set_freq` writes one multisynth block, then that clock's control byte,
then the output-enable bit. It does not write PLL parameters or the PLL
reset.

## Measured ATtiny3226 build

arduino-cli 1.5.1, megaTinyCore 2.6.11, DxCore avr-gcc
7.3.0-atmel3.6.1-azduino7b1. FQBN

```
megaTinyCore:megaavr:atxy6:chip=3226,clock=20internal,millis=enabled,wiremode=mors,printf=default
```

`examples/Tuning`, `avr-size -A`:

| Section | Bytes | Address |
|---|---:|---:|
| `.text` | 4834 | 0 |
| `.rodata` | 54 | 37602 (`0x8000 + 4834`) |
| `.data` | 8 | RAM initializer, also in flash |
| `.bss` | 95 | RAM |

CLI reports **Sketch uses 4896 bytes**. That is `.text + .rodata + .data`
and is the flash figure. RAM is `.data + .bss` = **103** of 3072. 4896 is
under 8192.

`avr-nm` shows `__mulsi3`, `__udivmodsi4`, and the 16×16→32 pieces
`__muluhisi3` and `__umulhisi3` that `__mulsi3` calls. There is no `*di3`
64-bit helper and no `*sf3` or `*df3` float helper.

## Identity

`tools/check.sh` rebuilds the host tests, the 6000003-plan window sweep,
the register-decoded sweep, the atmega328p simavr run, and this FQBN.
`tools/identity.sh` writes a source tar and a git bundle from `HEAD` and
checks every archived blob against `git ls-tree`.

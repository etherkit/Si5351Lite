# Si5351Lite design notes

Review build of a separate library. Upstream `etherkit/Si5351Arduino` at
v2.5.0 (`bc2686967f8673d2202b69caabd5d219916f4e15`) was read and not
modified. Nothing here is a spectrum measurement or a hardware proof.

The public contract is plan D from the design note: whole-hertz
`set_freq`, an internal quarter-hertz reference, both PLLs fixed after
`begin()`, CLK0–CLK5, 500 kHz–100 MHz inclusive.

## API

Enumerators are `enum class`. Call sites use `Si5351Lite::Load::LOAD_8PF`,
`Si5351Lite::Clk::CLK0`, and `Si5351Lite::Drive::DRIVE_8MA`. A bare
integer does not convert. A `static_cast` to a value outside the
enumerators is rejected before any Wire call. Address `0` is not a load;
only `0x40`, `0x80`, and `0xC0` are. I2C address must be `0x60` or `0x61`.
`corr_ppb` must be in −500000…+500000. Positive means the crystal runs
high, the same sign as v2.5.0.

`false` does not mean the chip was left unchanged once a transaction has
started. Validation failure is the only path with no Wire call, including
no `Wire.begin()`. A bus failure drops the runtime model (`pll_hz()` is 0,
`set_freq` refuses) even if an earlier `begin()` had succeeded. Recovery
is another `begin()`, then `set_freq` for each clock in use. A failed
`set_freq` does not clear `begun`, and it clears that clock's programmed
mark so `output_enable(true)` will not turn the pin on until a later
`set_freq` succeeds.

`set_freq` writes one eight-byte multisynth block, then the clock-control
byte, then the output-enable bit. It does not write PLL parameter blocks
or the PLL reset register. `output_enable(true)` before a successful
`set_freq` on that clock does not touch the bus. `output_enable(false)`
disables the pin; the next successful `set_freq` enables it again.

There is no heap and no file-scope calibration. Each object keeps its own
bus pointer, address, corrected reference, floored PLL model, and six
programmed marks.

## Reference and PLL

Corrected reference, quarter-hertz, truncation toward zero:

```
delta = floor((xo_hz * 4) * |ppb| / 1e9)
ref_q = xo_hz * 4 ± delta
```

One step is 0.25 Hz, which is 10 ppb at both 25 MHz and 27 MHz. Below
that threshold the reference does not move, and the whole correction
remains calibration error.

PLL target is 800 MHz. `a = floor(target / ref)`. `b` is the smallest
numerator with denominator 1,000,000 such that the rational VCO is at
least 800 MHz and at most 900 MHz. Both PLL register blocks get that
rational. For 25 MHz and 0 ppb the model is 800,000,000 Hz (`a = 32`,
`b = 0`). For 27 MHz and 0 ppb it is 800,000,010 Hz (`a = 29`,
`b = 629630`). `pll_hz()` returns the whole-hertz floor of the stored
quarter-hertz model, not the constant 800,000,000.

The value stored for multisynth math is

```
pll_q = floor(ref_q * (a + b/c))
```

in quarter-hertz. The registers still hold the exact `a + b/c`. Those are
not the same quantity.

## Why two rows beat the multisynth-only ceiling

The design ceiling

```
|synth| < F_req^2 / (F_pll * 1e6)
```

is the floor error of a multisynth whose numerator was computed from the
same PLL frequency used to score the output. This library scores two
different PLL values:

| Quantity | What it is |
|---|---|
| Stored model | `pll_q / 4` hertz. Multisynth `a` and `b` are `floor` divisions of `pll_q`. |
| Programmed ratio | `a + b/c` decoded from the PLL parameter block. Exact VCO is `ref_q * (a + b/c)` quarter-hertz, which is `pll_q` plus a fraction in `[0, 1)`. |

AN619 packing is algebraically exact for `a + b/c` (denominator 1 or
1,000,000). Decoding P1/P2/P3 does not add another rounding step. The
fraction discarded by `pll_q` is still inside the chip, because the chip
implements the rational, not the floor.

Let `R_ms` be the multisynth ratio decoded from the clock's parameter
block, and `R_pll` the PLL ratio decoded the same way.

```
F_stored = (pll_q / 4) / R_ms
F_hw     = (ref_q / 4) * R_pll / R_ms
trunc    = F_hw - F_stored
```

`0 ≤ trunc < 0.25 / R_ms`. The multisynth floor against the stored model
satisfies

```
0 ≤ F_stored - F_req < F_req / (R_ms * 1e6)
```

Adding the open quarter-hertz gap gives the programmed-ratio bound actually
checked by `test/decode_sweep`:

```
0 ≤ F_hw - F_req < F_req / (R_ms * 1e6) + 0.25 / R_ms
```

`F_req^2 / (F_stored * 1e6)` bounds `F_stored - F_req` only. It is not a
bound on `F_hw - F_req`. Rows where `F_stored` is already near that
ceiling cross it by less than `trunc`.

On the register-decoded grid (25 and 27 MHz, the design ppb set, 1,634
in-range frequencies, 0 rejects) **15** rows have `F_hw - F_req` above the
multisynth-only ceiling. Thirteen of those cross by less than 2 µHz.
The two crossings above 0.1 mHz are the ones the integer sweep flagged:

| Crystal | ppb | F_req | Decoded PLL | Decoded MS | Stored synth | Truncation gap | Programmed synth | MS-only ceiling |
|---|---:|---:|---|---|---:|---:|---:|---:|
| 27 MHz | +10 | 3.560 MHz | 29 + 629630/1e6 | 224 + 719105/1e6 | 0.015353 Hz | 0.000700 Hz | 0.016053 Hz | 0.015842 Hz |
| 27 MHz | −20 | 14.074 MHz | 29 + 629631/1e6 | 56 + 842405/1e6 | 0.246823 Hz | 0.003258 Hz | 0.250081 Hz | 0.247597 Hz |

In both rows the stored-model synth is under the ceiling and the excess of
the programmed synth is smaller than the truncation gap. Neither row was
removed from the grid. The test fails if either stops exceeding the
multisynth-only ceiling, and it fails if either exceeds the full bound.

Calibration is separate. The ppb division discards less than one
quarter-hertz of reference, so

```
|F_phys - F_hw| < 0.25 * R_pll / R_ms
```

with `F_phys` using `xo * (1 + ppb/1e9)` and the same decoded ratios.
Worst `|cal|` on this grid is 0.922 Hz. That is the calibration term, not
a synthesizer-floor claim and not a measured spur.

At 80,000,001 Hz the multisynth numerator can be 999,999. The programmed
error there reaches about 7.9 Hz and is still under `F_req / (R_ms * 1e6)`.
The design summary's 4.900 Hz all-slice maximum is lower than that grid
point. The floor formula is what the library implements.

`MSx_INT` (register 16–21 bit 6) is set only when `b = 0` and `a` is even.
AN619 4.1.2.1 allows that bit for an even integer divider. Odd integers are
programmed with P2 = 0 and P3 = 1 and the bit clear. CLK6/CLK7 stay `0x80`,
so `FBA_INT`/`FBB_INT` stay clear. There is no CLK6/7, VCXO, CLKIN, or
manual-PLL API. Register 15 is written 0 so both PLL source bits select
the crystal; the reserved bits' reset value is already 0.

## Initialization

`begin` validates load, address, ppb, and the PLL rational before any Wire
call. Then, in order: address probe, at most 32 reads of register 0 until
`SYS_INIT` is clear, output-enable `0xFF`, power-down CLK0–7 (`0x80` in
registers 16–23), crystal load in register 183 with bits 5:0 set to
`010010b`, register 15 cleared, spread-spectrum `SSC_EN` cleared by a
read-modify-write of register 149, registers 162–164 written 0, both PLL
blocks, PLL reset `0xA0` in register 177, CLK0–5 set to `0x0C`, and
output-enable `0xFF` again. CLK6–7 remain powered down. Outputs stay
disabled until `set_freq`.

A stuck `SYS_INIT` or a NACK stops the sequence. Earlier bytes may already
have been accepted. The library does not read the map back to repair that.

## Build size

ATtiny3226, megaTinyCore 2.6.11, DxCore avr-gcc 7.3.0-atmel3.6.1-azduino7b1,
FQBN

```
megaTinyCore:megaavr:atxy6:chip=3226,clock=20internal,millis=enabled,wiremode=mors,printf=default
```

`avr-size -A` on `examples/Tuning`:

| Section | Bytes | Role |
|---|---:|---|
| `.text` | 4830 | code at address 0 |
| `.rodata` | 54 | mapped flash at `0x8000 + .text` |
| `.data` | 8 | RAM initializers, also stored in flash |
| `.bss` | 95 | RAM |

Arduino CLI reports **Sketch uses 4892 bytes**. That is `.text + .rodata + .data`.
It is the flash figure for this part. `.text + .data = 4838` leaves out the
mapped `.rodata` and is not the claim. 4892 is under 8192. RAM is
`.data + .bss = 103` of 3072.

`avr-nm` on that ELF has `__mulsi3` and `__udivmodsi4`. `__mulsi3` calls
`__muluhisi3`, which calls `__umulhisi3`. Those last two are the 16×16→32
partial products inside the 32-bit multiply. There is no `*di3` 64-bit
helper and no `*sf3`/`*df3` float helper. Firmware sources do not use
`uint64_t`, `long long`, `float`, or `double`.

The 3282-byte scratch sketch is not this binary. This one links the real
`begin()` path, including both PLL blocks, the status probe, the
spread-spectrum update, and the VCXO clears.

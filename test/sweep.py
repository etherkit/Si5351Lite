#!/usr/bin/env python3
"""Integer sweep of plan D (and plan C for the worked rows).

Floor divisions use Python integers. Float is used only to score hertz.
This is not a hardware measurement.
"""

import sys

HF_MHZ = [
    1.8, 3.5, 3.56, 3.573, 7.0, 7.074, 10, 10.1, 10.106, 14, 14.074,
    18.068, 18.1, 21, 21.074, 24.89, 24.915, 28, 28.074, 29, 50,
    50.125, 50.313,
]
NEAR_MHZ = [8, 10, 12.5, 16, 20, 25, 40, 50, 80, 100]
PPB = [0, 1, -1, 9, -9, 10, -10, 20, -20] + list(range(37, 42)) + list(range(-41, -36))


def ceil_div(n, d):
    return (n + d - 1) // d


def plan(xo, ppb, mode):
    sign = 1 if ppb >= 0 else -1
    scale = 4 if mode == "D" else 1
    base = xo * scale
    delta = (base * abs(ppb)) // 1_000_000_000
    ref = base + sign * delta
    if ref <= 0:
        return None
    target = 800_000_000 * scale
    if ref > target:
        return None
    a = target // ref
    rem = target - a * ref
    if rem == 0:
        b = 0
        c = 1
    else:
        b = ceil_div(rem * 1_000_000, ref)
        if b >= 1_000_000:
            a += 1
            b = 0
            c = 1
        else:
            c = 1_000_000
    if not (15 <= a <= 90):
        return None
    pll = a * ref + ((ref * b) // 1_000_000 if b else 0)
    if pll < target or pll > 900_000_000 * scale:
        return None
    return ref, scale, a, b, c, pll


def ms(pll, hz, scale):
    fout = hz * scale
    if fout == 0 or pll < fout:
        return None
    a = pll // fout
    if not (8 <= a <= 1800):
        return None
    rem = pll - a * fout
    b = (rem * 1_000_000) // fout if rem else 0
    return a, b


def freqs():
    hf = {int(round(f * 1_000_000)) for f in HF_MHZ}
    near = set()
    for f in NEAR_MHZ:
        base = int(round(f * 1_000_000))
        near.add(base)
        near.add(base + 1)
    allf = set(hf) | set(near) | {500000, 500001, 99999999, 100000000}
    return allf, hf, near


def score_row(xo, ppb, hz, mode):
    got = plan(xo, ppb, mode)
    if got is None:
        return None
    ref, scale, a, b, _c, pll = got
    synth = ms(pll, hz, scale)
    if synth is None:
        return None
    msa, msb = synth
    pr = a + b / 1_000_000.0
    mr = msa + msb / 1_000_000.0
    model_ref = ref / scale
    model = model_ref * pr / mr
    # MS b was chosen from this floored quarter-hertz VCO, not from the
    # exact rational product. The design's synth bound applies here.
    trunc = pll / float(scale)
    phys = xo * (1.0 + ppb / 1e9)
    actual = phys * pr / mr
    return {
        "synth": model - hz,
        "synth_trunc": trunc / mr - hz,
        "cal": actual - model,
        "total": actual - hz,
        "pll": trunc,
        "bound": hz * (0.25 if mode == "D" else 1.0) / xo,
    }


def main():
    fails = 0

    def check(cond, msg):
        nonlocal fails
        if not cond:
            print("FAIL", msg)
            fails += 1

    # Worked 10 MHz rows from the design, plan D, 25 MHz crystal.
    table = {
        1: (0.0, 0.010, 0.010),
        9: (0.0, 0.090, 0.090),
        10: (0.100, 0.0, 0.100),
        37: (0.050, 0.070, 0.120),
        39: (0.050, 0.090, 0.140),
    }
    for ppb, (es, ec, et) in table.items():
        row = score_row(25_000_000, ppb, 10_000_000, "D")
        check(row is not None, "10 MHz ppb %s rejected" % ppb)
        if row:
            check(abs(row["synth"] - es) < 5.1e-4, "synth %s %s" % (ppb, row["synth"]))
            check(abs(row["cal"] - ec) < 5.1e-4, "cal %s %s" % (ppb, row["cal"]))
            check(abs(row["total"] - et) < 5.1e-4, "total %s %s" % (ppb, row["total"]))

    row = score_row(25_000_000, 1, 14_074_000, "D")
    check(row and 0.106 < row["synth"] < 0.109, "14.074 synth %s" % (row,))
    check(row and 0.013 < row["cal"] < 0.015, "14.074 cal %s" % (row,))
    row = score_row(25_000_000, 37, 14_074_000, "C")
    check(row and 0.515 < row["cal"] < 0.526, "C 14.074 cal %s" % (row,))
    row = score_row(25_000_000, 37, 14_074_000, "D")
    check(row and 0.097 < row["cal"] < 0.100, "D 14.074 cal %s" % (row,))

    # Threshold: 0.25 Hz is 10 ppb at both 25 and 27 MHz.
    for xo in (25_000_000, 27_000_000):
        base = plan(xo, 0, "D")[0]
        for ppb in (1, 9):
            check(plan(xo, ppb, "D")[0] == base, "step moved at %s ppb %s" % (xo, ppb))
            check(plan(xo, -ppb, "D")[0] == base, "neg step moved at %s ppb %s" % (xo, ppb))
        check(plan(xo, 10, "D")[0] == base + 1, "10 ppb did not step %s" % xo)
        check(plan(xo, -10, "D")[0] == base - 1, "-10 ppb did not step %s" % xo)

    allf, hf, near = freqs()
    in_range = {f for f in allf if 500_000 <= f <= 100_000_000}
    stats = {k: [0.0, 0.0, 0.0] for k in ("all", "hf", "near", "small")}
    n = 0
    rejected = 0
    bound_bad = 0
    small = {1, -1, 10, -10, 20, -20}
    for xo in (25_000_000, 27_000_000):
        for ppb in PPB:
            for hz in sorted(in_range):
                row = score_row(xo, ppb, hz, "D")
                if row is None:
                    rejected += 1
                    continue
                n += 1
                trip = (abs(row["total"]), abs(row["synth"]), abs(row["cal"]))
                keys = ["all"]
                if hz in hf:
                    keys.append("hf")
                if hz in near:
                    keys.append("near")
                if ppb in small:
                    keys.append("small")
                for key in keys:
                    for i in range(3):
                        if trip[i] > stats[key][i]:
                            stats[key][i] = trip[i]
                synth_bound = (hz * hz) / (row["pll"] * 1_000_000.0)
                gap = row["synth"] - row["synth_trunc"]
                # R is about pll/hz. 0.25/R is the open quarter-hertz gap.
                r_ms = row["pll"] / hz if row["synth_trunc"] == 0 else row["pll"] / (hz + row["synth_trunc"])
                full = hz / (r_ms * 1_000_000.0) + 0.25 / r_ms
                if row["synth_trunc"] < -1e-9 or row["synth_trunc"] > synth_bound + 1e-6:
                    bound_bad += 1
                if gap < -1e-9 or gap >= 0.25 / r_ms + 1e-9:
                    bound_bad += 1
                if row["synth"] >= full + 1e-6:
                    bound_bad += 1
                if abs(row["cal"]) > row["bound"] + 1e-6:
                    bound_bad += 1
                if row["synth"] > synth_bound + 1e-9:
                    # Counted, not discarded. The register sweep is authoritative.
                    pass
    focus = ((27_000_000, 10, 3_560_000), (27_000_000, -20, 14_074_000))
    for xo, ppb, hz in focus:
        row = score_row(xo, ppb, hz, "D")
        check(row is not None, "focus missing")
        if not row:
            continue
        bound = (hz * hz) / (row["pll"] * 1_000_000.0)
        gap = row["synth"] - row["synth_trunc"]
        check(row["synth"] > bound, "focus should exceed MS-only %s" % hz)
        check(row["synth_trunc"] <= bound + 1e-9, "focus stored over MS-only")
        check(gap > 0 and row["synth"] - bound <= gap + 1e-6, "focus gap")
    check(rejected == 0, "in-range rejects %s" % rejected)
    check(bound_bad == 0, "derived-bound violations %s" % bound_bad)
    check(0.90 < stats["all"][2] < 0.95, "max cal %s" % stats["all"][2])
    print("integer-model cases", n, "rejects", rejected, "derived-bound violations", bound_bad)
    for key, val in stats.items():
        print("%-5s total %.3f synth %.3f cal %.3f" % (key, val[0], val[1], val[2]))
    print(
        "This script checks the integer planner. "
        "Register-decoded evidence is test/decode_sweep, not this file."
    )
    if fails:
        print("SWEEP FAIL", fails)
        return 1
    print("SWEEP PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

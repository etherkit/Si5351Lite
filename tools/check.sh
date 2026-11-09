#!/bin/sh
# Repeatable author checks. Does not push, tag, or open a PR.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)

HOST_CXX=${HOST_CXX:-g++}
ARDUINO_CLI=${ARDUINO_CLI:-arduino-cli}
AVR_BIN=${AVR_BIN:-$HOME/.arduino15/packages/DxCore/tools/avr-gcc/7.3.0-atmel3.6.1-azduino7b1/bin}
if [ ! -x "$AVR_BIN/avr-g++" ]; then
  AVR_BIN=$(dirname "$(command -v avr-g++)")
fi

echo "== host tests =="
"$HOST_CXX" -std=gnu++11 -Wall -Wextra -Werror -Os -DSI5351LITE_TEST \
  -I "$ROOT/test/stubs" -I "$ROOT/src" \
  "$ROOT/test/host_main.cpp" "$ROOT/test/checks.cpp" \
  "$ROOT/test/stubs/Wire.cpp" "$ROOT/src/Si5351Lite.cpp" \
  -o /tmp/si5351lite_host
/tmp/si5351lite_host

echo "== register-decoded sweep =="
"$HOST_CXX" -std=gnu++11 -Wall -Wextra -Werror -Os -DSI5351LITE_TEST \
  -I "$ROOT/test/stubs" -I "$ROOT/src" \
  "$ROOT/test/decode_sweep.cpp" "$ROOT/test/stubs/Wire.cpp" \
  "$ROOT/src/Si5351Lite.cpp" \
  -o /tmp/si5351lite_decode
/tmp/si5351lite_decode

echo "== integer cross-check =="
python3 "$ROOT/test/sweep.py"

echo "== source scan =="
if grep -nE 'uint64|int64|long long|\bfloat\b|\bdouble\b|\bmalloc\b|\bnew\b|#define SI5351_' \
    "$ROOT/src/"*.h "$ROOT/src/"*.cpp; then
  echo "forbidden token in firmware sources" >&2
  exit 1
fi
echo "src scan clean"

# Require a finished simulator run: g_fails printed as 0 and g_checks >= 1.
sim_result_ok() {
  log=$1
  fails=$(awk '/^\$1 = / { print $3; exit }' "$log")
  checks=$(awk '/^\$2 = / { print $3; exit }' "$log")
  case "$fails" in
    0) ;;
    *)
      echo "simulator g_fails=${fails:-missing}" >&2
      return 1
      ;;
  esac
  case "$checks" in
    ''|*[!0-9]*)
      echo "simulator g_checks missing or not a number" >&2
      return 1
      ;;
  esac
  if [ "$checks" -lt 1 ]; then
    echo "simulator g_checks=$checks incomplete" >&2
    return 1
  fi
  echo "simulator g_fails=$fails g_checks=$checks"
}

echo "== simulator guard self-test =="
GUARD_DIR=$(mktemp -d)
printf '%s\n' '$1 = 4' '$2 = 12' > "$GUARD_DIR/nonzero.txt"
if sim_result_ok "$GUARD_DIR/nonzero.txt"; then
  echo "guard missed nonzero g_fails" >&2
  exit 1
fi
printf '%s\n' '$1 = 0' > "$GUARD_DIR/missing.txt"
if sim_result_ok "$GUARD_DIR/missing.txt"; then
  echo "guard missed missing g_checks" >&2
  exit 1
fi
printf '%s\n' '$1 = 0' '$2 = 0' > "$GUARD_DIR/zero.txt"
if sim_result_ok "$GUARD_DIR/zero.txt"; then
  echo "guard missed zero g_checks" >&2
  exit 1
fi
rm -rf "$GUARD_DIR"
echo "simulator guard rejected nonzero, missing, and zero results"

echo "== AVR 16-bit int simavr =="
"$AVR_BIN/avr-g++" -std=gnu++11 -Wall -Wextra -Werror -Os -g -mmcu=atmega328p \
  -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
  -DSI5351LITE_TEST -DSI5351LITE_AVR \
  -I "$ROOT/test/stubs" -I "$ROOT/src" \
  "$ROOT/test/avr16/main.cpp" "$ROOT/test/checks.cpp" \
  "$ROOT/test/stubs/Wire.cpp" "$ROOT/src/Si5351Lite.cpp" \
  -o /tmp/si5351lite_avr.elf -Wl,--gc-sections
"$AVR_BIN/avr-size" -C --mcu=atmega328p /tmp/si5351lite_avr.elf
pkill -f 'simavr -g' >/dev/null 2>&1 || true
simavr -g -m atmega328p /tmp/si5351lite_avr.elf > /tmp/si5351lite_simavr.log 2>&1 &
SIM_PID=$!
sleep 0.4
timeout 30 avr-gdb -batch -nx \
  -ex "set pagination off" \
  -ex "set confirm off" \
  -ex "file /tmp/si5351lite_avr.elf" \
  -ex "target remote :1234" \
  -ex "break test_done" \
  -ex "continue" \
  -ex "print g_fails" \
  -ex "print g_checks" \
  -ex "quit" > /tmp/si5351lite_gdb.txt
GDB_RC=$?
kill "$SIM_PID" >/dev/null 2>&1 || true
wait "$SIM_PID" >/dev/null 2>&1 || true
cat /tmp/si5351lite_gdb.txt
if [ "$GDB_RC" -ne 0 ]; then
  echo "avr-gdb exit $GDB_RC" >&2
  exit 1
fi
sim_result_ok /tmp/si5351lite_gdb.txt

echo "== ATtiny3226 =="
FQBN=megaTinyCore:megaavr:atxy6:chip=3226,clock=20internal,millis=enabled,wiremode=mors,printf=default
"$ARDUINO_CLI" compile --clean --warnings all --fqbn "$FQBN" \
  --library "$ROOT" --build-path /tmp/si5351lite-attiny \
  "$ROOT/examples/Tuning"
ELF=/tmp/si5351lite-attiny/Tuning.ino.elf
echo "== avr-size -A =="
"$AVR_BIN/avr-size" -A "$ELF"
echo "== nm wide/float scan =="
if "$AVR_BIN/avr-nm" "$ELF" | grep -E '__.*di3|__divdi|__udivdi|__muldi|__umulsidi|__float|df3|sf3'; then
  echo "64-bit or float helper linked" >&2
  exit 1
fi
echo "no 64-bit or float helper"
echo "32-bit multiply helpers (allowed):"
"$AVR_BIN/avr-nm" -C "$ELF" | grep -E '__mulsi3|__muluhisi3|__umulhisi3|__udivmodsi4' || true
echo "CHECK OK"

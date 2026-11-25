#!/usr/bin/env bash
# fps.sh - frames drawn per second in a match (P2 = CPU), measured from two RAM dumps.
#   idle: P1 stands still; turn: P1 turns; both: P1 walks + turns + fires
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; T="$ROOT/tests/.tmp"; A="$ROOT/tests/artifacts"
run() {  # name script
  "$T/harness" "$T/genesis_plus_gx_libretro.so" "$ROOT/out/rom.bin" 1000 "100-104:start,260-264:start,$2,ram@600,ram@900" "$A/fps_$1_" >/dev/null 2>&1
  a=$(python3 "$ROOT/tests/ramsym.py" "$A/fps_$1_600.ram" "$ROOT/out/release/symbol.txt" frames_drawn:2 | grep -o '= *[0-9]*' | head -1 | tr -dc 0-9)
  b=$(python3 "$ROOT/tests/ramsym.py" "$A/fps_$1_900.ram" "$ROOT/out/release/symbol.txt" frames_drawn:2 | grep -o '= *[0-9]*' | head -1 | tr -dc 0-9)
  echo "$1: $(python3 -c "print(round(($b-$a)/300*59.92,1))") fps"
}
run idle "shot@899"
run turn "450-950:right"
run both "450-950:up,450-950:left,450-950:b"

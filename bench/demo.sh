#!/usr/bin/env bash
# Live demo of riptide for the final presentation.
#
#   bench/demo.sh BUILD_DIR DATASET_DIR
#
# DATASET_DIR must contain silesia_x5.tar.gz (built by bench/datasets.py). The build is
# made first if BUILD_DIR has no riptide binary. The script pauses before each step
# (press Enter); NOPAUSE=1 runs straight through and NOOPEN=1 does not open the images.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD=${1:?build directory}
DATA=${2:?dataset directory}
FILE="$DATA/silesia_x5.tar.gz"
OUT="${TMPDIR:-/tmp}/riptide-demo"
RIPTIDE="$BUILD/riptide"

bold() { printf '\n\033[1;36m== %s\033[0m\n' "$1"; }
show() { printf '\033[1;32m$\033[0m %s\n' "$*"; }
pause() { [ -n "$NOPAUSE" ] || read -r -p "   [Enter] " _; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$1"; }

[ -f "$FILE" ] || { echo "No $FILE; build it with: python3 bench/datasets.py $DATA"; exit 1; }
if [ ! -x "$RIPTIDE" ]; then
    echo "Building riptide (first time only, about a minute)..."
    cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD" -j >/dev/null
fi
mkdir -p "$OUT"

# riptide holds the whole 1 GB output in memory (about 3 GB at peak). On battery,
# in Low Power Mode or with little free memory it runs much slower than measured.
if [ "$(uname)" = Darwin ]; then
    pmset -g batt | grep -q "AC Power" || warn "on battery; plug in the charger."
    pmset -g | grep -q "lowpowermode *1" && warn "Low Power Mode is on; turn it off in System Settings > Battery."
    swap_mb=$(sysctl -n vm.swapusage | sed -E 's/.*used = ([0-9]+)\..*/\1/')
    [ "${swap_mb:-0}" -gt 1024 ] && warn "${swap_mb} MB of memory is swapped out; quit other apps or restart first."
fi

# Read the file once so that both tools start with it in memory.
"$RIPTIDE" -t "$FILE" >/dev/null
gzip -dc "$FILE" >/dev/null

bold "1. The file: Silesia x5, a normal .gz file made by gzip"
show "ls -lh silesia_x5.tar.gz"
ls -lh "$FILE" | awk '{print $5, $NF}'
pause

bold "2. Same output as gzip, but faster (output thrown away, just timing)"
show "time gzip -dc silesia_x5.tar.gz > /dev/null"
time gzip -dc "$FILE" >/dev/null
show "time riptide silesia_x5.tar.gz > /dev/null"
time "$RIPTIDE" "$FILE" >/dev/null
pause

bold "3. Byte for byte the same as gzip"
show "gzip -dc silesia_x5.tar.gz | shasum -a 256"
gzip -dc "$FILE" | shasum -a 256
show "riptide silesia_x5.tar.gz | shasum -a 256"
"$RIPTIDE" "$FILE" | shasum -a 256
pause

bold "4. Where the time goes: the six stages"
show "riptide -v -t silesia_x5.tar.gz"
"$RIPTIDE" -v -t "$FILE"
pause

bold "5. M3: the window step with small chunks (64 KiB, 5,205 chunks)"
for w in serial blocked gpu-blocked; do
    show "riptide -v -t --chunk-size 64K --window $w silesia_x5.tar.gz"
    "$RIPTIDE" -v -t --repeat 3 --chunk-size 64K --window "$w" "$FILE" 2>&1 |
        grep -E "^riptide:|window resolution  "
done
pause

bold "6. Timelines of every thread (our tracer)"
for w in serial gpu-blocked; do
    show "riptide -t --chunk-size 64K --window $w --trace $w.json silesia_x5.tar.gz"
    "$RIPTIDE" -t --repeat 3 --chunk-size 64K --window "$w" --trace "$OUT/$w.json" "$FILE"
    python3 "$REPO/bench/plot_trace.py" "$OUT/$w.json" "$OUT/$w.png" --title "window strategy: $w"
done
echo "   Opening the two timelines. The JSON files can also be dropped into ui.perfetto.dev."
[ -n "$NOOPEN" ] || open "$OUT/serial.png" "$OUT/gpu-blocked.png"
pause

bold "7. Tests: all compared against zlib"
show "ctest --test-dir build"
ctest --test-dir "$BUILD" 2>&1 | tail -4

echo
echo "Trace files are in $OUT"

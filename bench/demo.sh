#!/usr/bin/env bash
# Live demo of riptide for the final presentation.
#
#   bench/demo.sh BUILD_DIR DATASET_DIR
#
# DATASET_DIR must contain silesia.tar.gz (the Silesia corpus), silesia_x5.tar.gz and
# logs.ndjson.gz (built by bench/datasets.py). The build is made first if BUILD_DIR has no riptide binary.
#
# Every step measures its numbers live (median of REPS runs, default 3) and prints them
# next to the report's numbers; the last step prints all of them on one screen.
# Before each step the script waits: Enter runs it, s skips it.
#
#   STEPS="1 2 5 12"  run only these steps        NOPAUSE=1  do not wait between steps
#   REPS=5            runs per measurement        NOOPEN=1   do not open the timelines
#   RAPIDGZIP=path    rapidgzip to compare with (default: the one on PATH, if any)
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD=${1:?build directory}
DATA=${2:?dataset directory}
SILESIA="$DATA/silesia.tar.gz"
FILE="$DATA/silesia_x5.tar.gz"
LOGS="$DATA/logs.ndjson.gz"
OUT="${TMPDIR:-/tmp}"; OUT="${OUT%/}/riptide-demo"
RIPTIDE="$BUILD/riptide"
STATS="python3 $REPO/bench/demo_stats.py"
export NUMBERS="$OUT/numbers.json"

show() { printf '\033[1;32m$\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$1"; }

# step N TITLE: prints the title and returns 0 if the step should run.
step() {
    if [ -n "$STEPS" ] && [[ " $STEPS " != *" $1 "* ]]; then return 1; fi
    printf '\n\033[1;36m== %s. %s\033[0m\n' "$1" "$2"
    [ -n "$NOPAUSE" ] && return 0
    read -r -p "   [Enter] run, [s] skip " answer
    [ "$answer" != s ]
}

for f in "$SILESIA" "$FILE" "$LOGS"; do
    [ -f "$f" ] || { echo "No $f; build it with: python3 bench/datasets.py $DATA"; exit 1; }
done
if [ ! -x "$RIPTIDE" ]; then
    echo "Building riptide (first time only, about a minute)..."
    cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD" -j >/dev/null
fi
mkdir -p "$OUT"
rm -f "$NUMBERS"

# riptide holds the whole 1 GB output in memory (about 3 GB at peak). On battery,
# in Low Power Mode or with little free memory it runs much slower than measured.
if [ "$(uname)" = Darwin ]; then
    pmset -g batt | grep -q "AC Power" || warn "on battery; plug in the charger."
    pmset -g | grep -q "lowpowermode *1" && warn "Low Power Mode is on; turn it off in System Settings > Battery."
    swap_mb=$(sysctl -n vm.swapusage | sed -E 's/.*used = ([0-9]+)\..*/\1/')
    [ "${swap_mb:-0}" -gt 1024 ] && warn "${swap_mb} MB of memory is swapped out; quit other apps or restart first."
fi

# Read the files once so that every tool starts with them in memory.
for f in "$SILESIA" "$FILE" "$LOGS"; do "$RIPTIDE" -t "$f" >/dev/null; done
gzip -dc "$SILESIA" >/dev/null
gzip -dc "$FILE" >/dev/null

if step 1 "The files: the Silesia corpus and Silesia x5, normal .gz files made by gzip"; then
    for f in "$SILESIA" "$FILE"; do
        show "gzip -l $(basename "$f")"
        gzip -l "$(readlink -f "$f")" | awk 'NR == 2 { printf "   %.0f MB compressed, %.0f MB unzipped, ratio %.2f\n", $1 / 1e6, $2 / 1e6, $2 / $1 }'
    done
    echo "   Silesia: the standard corpus of 12 real files (text, XML, executables, a database,"
    echo "   images, medical data), also used by the rapidgzip paper. Silesia x5 is the same"
    echo "   tarball five times in one gzip member. Made by plain gzip, with no index."
fi

if step 2 "Faster than gzip, on Silesia and Silesia x5"; then
    for f in "$SILESIA" "$FILE"; do
        name=$(basename "$f")
        printf '\n   \033[1m%s\033[0m\n' "$name"
        show "time gzip -dc $name > /dev/null"
        time gzip -dc "$f" >/dev/null
        show "time riptide $name > /dev/null"
        time "$RIPTIDE" "$f" >/dev/null
        echo
        echo "   Median of ${REPS:-3} runs each, taken in turns:"
        $STATS race "$RIPTIDE" "$f"
    done
fi

if step 3 "Byte for byte the same as gzip, on Silesia and Silesia x5"; then
    for f in "$SILESIA" "$FILE"; do
        name=$(basename "$f")
        show "gzip -dc $name | shasum -a 256"
        a=$(gzip -dc "$f" | shasum -a 256 | cut -d' ' -f1); echo "   $a"
        show "riptide $name | shasum -a 256"
        b=$("$RIPTIDE" "$f" | shasum -a 256 | cut -d' ' -f1); echo "   $b"
        if [ "$a" = "$b" ]; then echo "   Same hash: the output is identical."; else echo "   HASHES DIFFER"; exit 1; fi
    done
    echo "   report: 1,600 of 1,600 timed runs matched zlib byte for byte."
fi

if step 4 "Where the time goes: the six stages (4 MiB chunks)"; then
    show "riptide -t --json silesia_x5.tar.gz"
    $STATS stages "$RIPTIDE" "$FILE"
fi

if step 5 "Scaling: 1 to 10 threads"; then
    show "riptide -t -P N silesia_x5.tar.gz    (N = 1, 2, 4, 10)"
    $STATS threads "$RIPTIDE" "$FILE"
fi

if step 6 "M3: every window strategy with small chunks (64 KiB), about a minute"; then
    show "riptide -t --chunk-size 64K --window STRATEGY silesia_x5.tar.gz"
    $STATS window "$RIPTIDE" "$FILE"
fi

if step 7 "M3 across chunk sizes: serial, blocked, GPU blocked, about a minute"; then
    show "riptide -t --chunk-size SIZE --window STRATEGY silesia_x5.tar.gz"
    $STATS chunks "$RIPTIDE" "$FILE"
fi

if step 8 "M1: split chunks that would overflow (logs, 16 MiB chunks, 4x budget)"; then
    show "riptide -t --chunk-size 16M --max-expansion 4 --overflow serial|split logs.ndjson.gz"
    $STATS overflow "$RIPTIDE" "$LOGS"
fi

if step 9 "Peak memory: big against small chunks"; then
    show "/usr/bin/time -l riptide -t --chunk-size 4M|512K silesia_x5.tar.gz"
    $STATS memory "$RIPTIDE" "$FILE"
fi

if step 10 "Against rapidgzip 0.16.0 (the base paper's tool)"; then
    show "rapidgzip -t -P N silesia_x5.tar.gz"
    $STATS rapidgzip "$RIPTIDE" "$FILE"
fi

if step 11 "Pipeline visualizer: the run replayed from our tracer"; then
    for run in "4M blocked" "64K serial" "64K blocked" "64K gpu-blocked"; do
        set -- $run
        show "riptide -t --chunk-size $1 --window $2 --trace $1-$2.json silesia_x5.tar.gz"
        "$RIPTIDE" -t --repeat 2 --chunk-size "$1" --window "$2" --trace "$OUT/$1-$2.json" "$FILE"
    done
    traces=("$OUT/4M-blocked.json=4 MiB chunks, blocked (default)" "$OUT/64K-serial.json=64 KiB, serial window"
            "$OUT/64K-blocked.json=64 KiB, blocked scan" "$OUT/64K-gpu-blocked.json=64 KiB, GPU blocked scan")
    show "python3 bench/pipeline_viz.py pipeline.html ..."
    python3 "$REPO/bench/pipeline_viz.py" "$OUT/pipeline.html" "${traces[@]}"
    if python3 -c "import PIL" 2>/dev/null; then    # still images need Pillow; the visualizer does not
        for w in serial gpu-blocked; do
            python3 "$REPO/bench/plot_trace.py" "$OUT/64K-$w.json" "$OUT/$w.png" --title "window strategy: $w"
        done
        echo "   Still images: $OUT/serial.png and gpu-blocked.png"
    fi
    echo "   The JSON files also open in ui.perfetto.dev."
    how=b
    [ -n "$NOPAUSE" ] || read -r -p "   [t] replay in this terminal, [b] open in the browser, [Enter] both " how
    if [ "$how" != b ]; then
        show "python3 bench/pipeline_term.py ..."
        echo "   Keys: space pause, 1-4 pick a run, arrows step, z zoom to the stage, + - speed, q quit"
        sleep 2
        python3 "$REPO/bench/pipeline_term.py" "${traces[@]}"
    fi
    if [ "$how" != t ] && [ -z "$NOOPEN" ]; then
        echo "   Opening the visualizer in the browser: press Play, or click a stage to jump to it."
        open "$OUT/pipeline.html"
    fi
fi

if step 12 "Tests: all compared against zlib"; then
    show "ctest --test-dir build"
    ctest --test-dir "$BUILD" 2>&1 | tail -4
    echo "   report: 49 tests in 14 suites, also clean under ASan, UBSan and TSan."
fi

if step 13 "All the numbers from this run"; then
    $STATS summary
fi

echo
echo "Numbers and trace files are in $OUT"

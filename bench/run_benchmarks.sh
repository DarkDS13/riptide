#!/usr/bin/env bash
# Runs every experiment of the evaluation and writes one CSV per experiment.
#
#   bench/run_benchmarks.sh BUILD_DIR DATASET_DIR RESULTS_DIR [REPETITIONS]
#
# Experiments (see docs/evaluation.md for what each one answers):
#   E1 scaling.csv         throughput vs threads, all datasets, with zlib and sequential baselines
#   E2 window_chunks.csv   window strategies (M3) across chunk sizes, 10 threads
#   E3 window_threads.csv  serial vs blocked window resolution across thread counts, 64 KiB chunks
#   E4 m1_policies.csv     chunking and overflow policies (M1) on high-ratio and mixed data
#   E5 memory.csv          peak resident memory per configuration (via /usr/bin/time)
set -euo pipefail

BUILD=${1:?build directory}
DATA=${2:?dataset directory}
OUT=${3:?results directory}
REPS=${4:-10}
BENCH="$BUILD/riptide_bench"
CLI="$BUILD/riptide"
mkdir -p "$OUT"

THREADS="1,2,4,6,8,10"
ALL=("$DATA/silesia.tar.gz" "$DATA/silesia_x5.tar.gz" "$DATA/reads.fastq.gz" "$DATA/logs.ndjson.gz" "$DATA/mixed.tar.gz")

echo "E1: scaling"
"$BENCH" --baselines --threads "$THREADS" --chunk-sizes 4M --windows blocked \
    --repetitions "$REPS" --output "$OUT/scaling.csv" "${ALL[@]}"

echo "E2: window strategies across chunk sizes"
"$BENCH" --threads 10 --chunk-sizes 64K,128K,256K,512K,1M,2M,4M \
    --windows serial,hillis-steele,blelloch,blocked \
    --repetitions "$REPS" --output "$OUT/window_chunks.csv" \
    "$DATA/silesia.tar.gz" "$DATA/silesia_x5.tar.gz"

echo "E3: window strategies across thread counts"
"$BENCH" --threads "$THREADS" --chunk-sizes 64K --windows serial,blocked \
    --repetitions "$REPS" --output "$OUT/window_threads.csv" "$DATA/silesia_x5.tar.gz"

echo "E4: chunking and overflow policies"
"$BENCH" --threads 10 --chunk-sizes 4M --windows blocked \
    --chunking fixed,work-balanced --overflow serial,split \
    --repetitions "$REPS" --output "$OUT/m1_policies.csv" \
    "$DATA/logs.ndjson.gz" "$DATA/mixed.tar.gz" "$DATA/silesia_x5.tar.gz"

echo "E5: peak memory"
echo "file,chunk_size,window,overflow,repetition,max_rss_bytes,output_bytes" > "$OUT/memory.csv"
for file in "$DATA/silesia_x5.tar.gz" "$DATA/logs.ndjson.gz"; do
    output_bytes=$(gzip -l "$file" | awk 'NR == 2 { print $2 }')
    for chunk in 512K 4M; do
        for overflow in serial split; do
            for rep in 1 2 3; do
                rss=$( { /usr/bin/time -l "$CLI" -t --chunk-size "$chunk" --overflow "$overflow" \
                         "$file" ; } 2>&1 | awk '/maximum resident set size/ { print $1 }')
                echo "$(basename "$file"),$chunk,blocked,$overflow,$rep,$rss,$output_bytes" >> "$OUT/memory.csv"
            done
        done
    done
done
echo "done: results in $OUT"

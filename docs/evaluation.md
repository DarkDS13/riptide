# Riptide evaluation

How the experiments are run and what each one answers. Measured results are in
`results/summary.md`, generated from the raw CSVs in `results/` by `bench/analyze.py`.

## Setup

| Item | Value |
|---|---|
| Machine | Apple M4, 10 cores (4 performance + 6 efficiency), 16 GB unified memory |
| L2 cache | 16 MiB shared by the performance cluster, 4 MiB shared by the efficiency cluster |
| OS | macOS 27.0.1 |
| Compiler | Apple clang 21.0.0, `-O3 -DNDEBUG`, C++20 |
| zlib | system zlib, `inflate()` into a 1 MiB buffer appended to the output vector |
| Repetitions | 10 per configuration after one unmeasured warm-up run, so the memory-mapped input is in the page cache |

The cores are not identical: the six efficiency cores run at a lower clock and with
smaller caches than the four performance cores. Speedups above about 4 on this machine
therefore come from the efficiency cores, and parallel efficiency against 10 threads is
bounded well below 1 even for perfectly parallel work. We report thread counts up to 10
and do not extrapolate to larger machines.

## Datasets

Built by `bench/datasets.py` (seeded; rerunning it produces identical files).

| File | Content | Uncompressed | Ratio |
|---|---|---|---|
| `silesia.tar.gz` | Silesia corpus tarball, the base paper's benchmark | 212 MB | 3.11 |
| `silesia_x5.tar.gz` | five copies of the tarball in one member | 1.06 GB | 3.11 |
| `reads.fastq.gz` | synthetic 150 bp short reads with binned qualities | 1.08 GB | 4.11 |
| `logs.ndjson.gz` | synthetic JSON service logs | 1.08 GB | 23.1 |
| `mixed.tar.gz` | Silesia files interleaved with random data and logs | 444 MB | 3.51 |

`logs.ndjson.gz` is above the 20× expansion limit, the regime in which the base
paper's software decodes the rest of a chunk on one thread. `mixed.tar.gz` changes
expansion ratio sharply from file to file inside one stream.

## Correctness gate

Every timed run of every engine is compared byte for byte with zlib's output
(`identical` column of each CSV). A configuration that ever differs is reported as
failed, not timed. The unit and integration tests additionally check every chunk
size, window strategy and policy against zlib on generated data, multi-member files,
stored blocks, fixed-Huffman blocks and truncated or corrupted streams.

## Experiments

**E1 Scaling** (`scaling.csv`). Throughput of zlib, the sequential Riptide decoder and
the parallel decoder with 1–10 threads, 4 MiB chunks, blocked window scan, on all five
files. Reports speedup against the parallel decoder on one thread, against the
sequential decoder and against zlib, parallel efficiency, first-stage load imbalance
λ and the time of every pipeline stage.

**E2 Window resolution across chunk sizes** (`window_chunks.csv`). M3. All four window
strategies at 10 threads, chunk sizes from 64 KiB to 4 MiB, on Silesia and
Silesia × 5. Smaller chunks mean more chunks and a longer window chain, so this
shows where the serial chain starts to matter and how much each scan removes. The
time measured is the resolution step alone (`window_seconds`); building the maps and
replacing markers are timed separately.

**E3 Window resolution across thread counts** (`window_threads.csv`). M3. Serial and
blocked resolution with 1–10 threads at 64 KiB chunks on Silesia × 5 (16 000 chunks):
the serial chain's cost is fixed, while the scan's cost should fall with threads.

**E4 Chunking and overflow policies** (`m1_policies.csv`). M1. Fixed or work-balanced
chunking combined with serial or split overflow handling, 10 threads, 4 MiB chunks,
on the logs, mixed and Silesia × 5 files.

**E5 Peak memory** (`memory.csv`). Maximum resident set size from `/usr/bin/time -l`
for 512 KiB and 4 MiB chunks with both overflow policies.

## Statistics

Medians and interquartile ranges over 10 repetitions. Two configurations are compared
with the two-sided Mann–Whitney U test: the repetitions of two configurations are
independent samples, not pairs, so the Wilcoxon signed-rank test named in the
methodology does not apply. p-values are Holm–Bonferroni corrected over all
comparisons of an experiment, and effects are reported as the ratio of medians. With
10 repetitions per side the smallest attainable two-sided p-value is about 1.1 × 10⁻⁵.

## Reproducing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./scripts/get_silesia.sh
python3 bench/datasets.py ~/riptide-data
bench/run_benchmarks.sh build ~/riptide-data results 10
python3 bench/analyze.py results
```

The full suite takes about 25 minutes on the machine above.

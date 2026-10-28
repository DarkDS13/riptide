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
| Repetitions | 10 per configuration, interleaved round-robin across configurations, after one unmeasured warm-up run so the memory-mapped input is in the page cache |

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

## Findings

All numbers are medians of 10 interleaved runs from `results/summary.md`.

1. **Correctness.** 1200 of 1200 timed runs are byte-identical to zlib.
2. **Scaling.** With 10 threads and 4 MiB chunks Riptide decodes Silesia × 5 at
   3.01 GB/s: 3.3× its own single-thread time, 4.4× the sequential decoder and 2.5×
   zlib. Silesia reaches 2.60 GB/s (2.1× zlib), the mixed archive 2.84 GB/s (1.5×) and
   FASTQ 1.98 GB/s (1.2×). On the 23× log file Riptide reaches 3.75 GB/s and zlib
   6.04 GB/s: zlib's work there is mostly long memory copies, while Riptide must
   replace markers in 94% of the output (145 of 288 ms at 10 threads). Efficiency
   falls to about 0.3 at 10 threads: the six efficiency cores contribute less than the
   four performance cores, and marker replacement is memory-bound.
3. **M3, window resolution.** The blocked scan resolves the windows 2.9–3.9× faster
   than the serial chain at every chunk size on Silesia × 5 (all p < 0.001 after Holm
   correction). Whether that matters depends on how many chunks there are. At 64 KiB
   (5 205 chunks) the serial chain is 22% of the run and the scan cuts total time from
   1.26 s to 1.10 s (1.14×); on Silesia at 64 KiB from 311 ms to 257 ms (1.21×). From
   1 MiB up the chain is below 2% of the run and total time does not change
   measurably. With 64 KiB chunks the serial chain costs 183–261 ms whatever the
   thread count, while the scan falls from 221 ms with 2 threads to 74 ms with 10
   (3.5×); with one thread the scan is 2× slower, as its work is twice the chain's.
4. **Why small chunks.** Small chunks need less memory (E5: peak 1.90 GB instead of
   2.34 GB on Silesia × 5 with 512 KiB rather than 4 MiB chunks, and 2.09 GB instead of
   3.21 GB on the logs). The serial window chain is what makes small chunks expensive in
   the base paper's design; M3 removes most of that cost.
5. **M1, split-on-overflow.** Under a 4× output budget with 16 MiB chunks, the log file
   has three chunks for ten threads, and the serial policy turns each into a chain of
   continuations: λ = 5.6. Splitting brings λ to 1.3 and total time from 407 ms to
   256 ms (1.59×, p < 0.001). With many chunks per thread, or when the budget is rarely
   reached (Silesia), the difference is not significant.
6. **M1, work-balanced chunking: negative.** Sampling headers costs 10–74 ms and the
   decode stage does not get faster: λ on the mixed archive drops from 1.53 to 1.34, but
   the busiest worker's time is set by individual large chunks, not by the plan. Total
   time is 0.65–0.91× that of fixed chunks. With 4 MiB chunks there are enough chunks for
   work stealing to balance them.

## Threats to validity

- The test machine is a fanless laptop whose clock drops under sustained load. Before
  repetitions were interleaved, the same configuration measured up to 48% slower when
  it ran ten minutes later. Repetitions are now interleaved round-robin across all
  configurations of a file, in alternating order, so drift affects every configuration
  alike; absolute numbers are sustained rather than peak throughput.
- One machine. The heterogeneous cores make efficiency look worse than on a server.
- Three of the five datasets are synthetic. They are seeded and reproducible, but their
  statistics are ours, not a real sequencer's or service's.
- rapidgzip itself was not run; comparisons with it use the numbers in its paper.

## Reproducing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./scripts/get_silesia.sh
python3 bench/datasets.py ~/riptide-data
bench/run_benchmarks.sh build ~/riptide-data results 10
python3 bench/analyze.py results
```

The full suite takes about 20 minutes on the machine above.

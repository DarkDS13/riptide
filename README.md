# Riptide: Breaking the Serial Window Chain in Parallel Gzip Decompression

Course project for **IT 301 Parallel Computing**, Department of Information Technology, NIT Karnataka, Surathkal.

| Member | Roll no. |
|---|---|
| Devansh Hiren Shah | 241IT024 |
| Mantri Samarth Prakash | 241IT043 |
| Shubh Minesh Thakkar | 241IT076 |
| Ritesh Bhagat | 241IT063 |

## Overview

Decompressing a gzip file is normally a single-core task, because DEFLATE back-references can point up to 32 KiB into earlier output. Our base paper, **rapidgzip** (M. Knespel and H. Brunst, *HPDC '23*, [doi:10.1145/3588195.3592992](https://doi.org/10.1145/3588195.3592992)), decodes chunks of a gzip file in parallel by writing placeholders (markers) for bytes that belong to the previous chunk's window and filling them in later. It still resolves the 32 KiB windows between chunks one chunk after another; the paper states that this propagation cannot be parallelised (§2.2) and attributes its scaling limit to it (§4.5–4.6).

Our contributions:

- **M3 (primary): window resolution by parallel prefix scan.** Each chunk's final window is a map from the previous window. Composing such maps is associative, so the chain can be computed in ⌈log₂ N⌉ rounds instead of N − 1 sequential steps.
- **M1: work-sized chunking with split-on-overflow.** Estimate decode cost from Huffman headers, cut chunks of similar cost, and split oversized chunks across idle threads instead of decoding them serially.
- **M2: compact placeholders.** Store unresolved bytes more compactly than 16 bits each. Our first evaluation is negative (see below).

## Results so far

All measurements use the [Silesia corpus](https://sun.aei.polsl.pl/~sdeor/corpus/silesia.html), the base paper's benchmark. No speed results exist yet; these come from the Python prototype.

| Result | Value |
|---|---|
| Streams decoded bit-identically to zlib | 13 / 13 (12 files + tarball) |
| Tarball expansion ratio | 3.11 (base paper: 3.1) |
| M3 sequential steps vs scan rounds, 4 MiB chunks | 16 vs 5 (identical output) |
| M3 sequential steps vs scan rounds, 512 KiB chunks | 130 vs 8 (identical output) |
| Header-based expansion estimate, median error per block | 2.0 % |
| M1 load imbalance λ, 8 workers (oracle model) | 1.16 vs 1.00 |
| M2 run-length layout, memory change | +7 % (4 MiB), +36 % (512 KiB) |

## Repository layout

```
docs/
  problem-statement/   Component 1 report (LaTeX) and presentation
  methodology/         Component 2 report (LaTeX) and presentation
  final/               Draft final presentation
  team-guide/          Project guide (open in a browser)
pilot/
  profiler.py          Reference DEFLATE decoder that records block structure
  analyze.py           Per-file statistics and cost model
  window_scan.py       M3 prototype: sequential vs prefix-scan window resolution
  silesia_run.py       Runs the profiler and M3 prototype on Silesia
  scheduling.py        Thread-pool model for chunking policies (M1)
  m1_derived.py        M1 load-imbalance estimates and block snapshot
  header_estimate.py   Expansion estimate from Huffman headers (M1 stage 0)
  m2_runs.py           Run-length placeholder evaluation (M2)
  silesia/*.json       Results
scripts/
  get_silesia.sh       Downloads Silesia and builds the gzip test set
```

## Reproducing the results

Requires Python 3 and NumPy (`pip install -r requirements.txt`).

```bash
./scripts/get_silesia.sh
cd pilot
python3 silesia_run.py profile        # dataset statistics
python3 silesia_run.py scan 4096 512  # M3 prototype at 4 MiB and 512 KiB chunks
python3 m1_derived.py                 # M1 load-imbalance estimates
python3 header_estimate.py            # M1 header-based estimate
python3 m2_runs.py 4096 512           # M2 evaluation
```

Each step takes under a minute on a laptop.

## Branches

| Branch | Contents |
|---|---|
| `docs/problem-statement` | Component 1 report and presentation |
| `feature/deflate-profiler` | Reference decoder and statistics |
| `prototype/m3-window-scan` | M3 prototype |
| `experiments/silesia` | Silesia download script and results |
| `analysis/m1-load-balance` | M1 model and header estimate |
| `analysis/m2-placeholders` | M2 evaluation |
| `docs/methodology` | Component 2 report and presentation |
| `docs/final-presentation` | Draft final presentation |
| `docs/team-guide` | Project guide |

All branches are merged into `main`.

## Next steps

1. Build rapidgzip from source and time its serial window-propagation step on Silesia.
2. Implement M3 in a fork of rapidgzip and benchmark it against the unmodified version at the same commit.
3. Implement M1 split-on-overflow.
4. Evaluate further M2 layouts.

# Riptide design

This document describes how the C++ implementation in `src/riptide` decompresses a
gzip file in parallel, which parts are taken from the base paper (rapidgzip, Knespel
and Brunst, HPDC '23) and which are ours, and the concurrency problems we had to solve
on the way.

## 1. Why gzip decompression is hard to parallelise

A gzip member is one DEFLATE stream: a sequence of blocks, each either stored
(uncompressed), compressed with the fixed Huffman code, or compressed with a dynamic
Huffman code whose description is at the start of the block. Inside a compressed block,
every symbol is either a literal byte or a back-reference *(length, distance)* that
copies `length` bytes starting `distance ≤ 32 768` bytes back in the output.

Two dependencies make the stream serial:

1. **Bit-level boundaries.** Blocks are not byte-aligned and there is no index. A thread
   that starts at an arbitrary offset does not know where the next block begins.
2. **The 32 KiB window.** A back-reference near the start of a chunk may point into
   output that belongs to the previous chunk, which has not been decoded yet.

## 2. Pipeline

`ParallelDecompressor::decompress` (`pipeline/parallel_decompressor.cpp`) runs six
stages on one work-stealing thread pool. Every stage except chain linking is parallel.

```
compressed file (mmap)
  │
  ├─ 1. partition        fixed chunks, or work-balanced chunks from sampled headers (M1)
  ├─ 2. first-stage      one task per chunk: find a block start, decode with markers;
  │     decoding         chunks that overflow their output budget are split (M1)
  ├─ 3. chain linking    follow chunk ends to the next chunk's start; decode gaps
  ├─ 4. window           build one window map per chunk, resolve all windows with a
  │     resolution       parallel prefix scan (M3)
  ├─ 5. marker           replace markers with bytes and copy chunks into one buffer
  │     replacement
  └─ 6. verification     CRC-32 of 4 MiB blocks in parallel, combined per member
```

The sequential decoder (`pipeline/sequential_decompressor.cpp`) uses the same inflate
code with a known window and serves as the single-thread baseline.

### 2.1 Finding block starts (from the base paper)

`inflate/block_finder.cpp` scans bit offsets for something that parses as a block
header. Dynamic blocks are rejected cheaply first: the 3-bit header must be "not final,
dynamic" and the literal and distance code counts must be in range, which removes
about 89% of offsets with a 13-bit check. Next, the code-length code (up to nineteen
3-bit lengths, held in one 64-bit load) must be complete, its Kraft sum exactly one.
Only the few survivors have both Huffman codes read and checked. Stored blocks are
found by their `LEN` / `NLEN` pair at a byte boundary. The two searches run together in
64 KiB segments, stored first, so the cost of a search is proportional to the distance
to the first candidate whether the data is compressible (dynamic blocks only) or not
(stored blocks only).

A candidate can still be a false positive. The decoding task tries it, and if decoding
fails (an invalid code, a distance before the start of the stream, or more output than
`4 ×` the chunk budget) it moves on to the next candidate.

### 2.2 Decoding with markers (from the base paper)

A chunk that starts in the middle of the stream does not know its preceding 32 KiB.
`decode_chunk_with_markers` (`inflate/chunk_decoder.cpp`) gives the decoder a window of
*markers* instead: 16-bit symbols `0x8000 + j` meaning "byte `j` of the unknown
window". Output is 16-bit while any marker may still be copied. Once the most recent
32 KiB of output contain no marker, nothing later can refer back to the unknown window,
so the decoder switches to plain 8-bit output for the rest of the chunk. A chunk's
result is therefore a (usually short) marked prefix followed by plain bytes.

The decoder itself is a conventional table-driven inflater: a 10-bit root table and
5-bit subtables per Huffman code (`huffman/huffman_decoder.cpp`), a 64-bit bit buffer
(`common/bit_reader.hpp`), and match copies in 16-byte steps into an output buffer with
slack at the end, so short overlapping copies do not need a byte loop.

### 2.3 Chain linking

Every task stops at the first block boundary at or after the next chunk's planned
start and records that boundary. Linking walks from the first chunk: the next link is
the chunk that *started* exactly where the current one stopped. A chunk whose start
does not match any stop (a false positive that happened to decode, or a missed
boundary) is discarded, and a missing link is decoded serially. Stored blocks are keyed
by both their bit offset and a flag, because the header of a stored block and its
payload start at different offsets.

### 2.4 Window resolution by prefix scan (M3, ours)

After linking we know, for every chunk *k*, its last 32 KiB of output as symbols:
either a concrete byte or a marker that refers to the window chunk *k* started with.
That is a function from the previous window to the next one:

```
W_k = f_k(W_{k-1})
```

The base paper resolves these one after another: `W_1 = f_1(W_0)`, then `W_2`, and so
on. That is N − 1 dependent steps, each touching 32 KiB, and it states this
propagation is inherently sequential.

Our observation is that the maps compose. If `f` and `g` are window maps, `g ∘ f` is
again a window map: take `g`'s 32 KiB symbols and replace every marker `j` with
`f`'s symbol `j` (`window/window.cpp: compose`). Composition of functions is
associative, so the windows are the inclusive *prefix scan* of the maps:

```
W_k = (f_k ∘ … ∘ f_2 ∘ f_1)(W_0)
```

and any parallel scan algorithm applies (`parallel/prefix_scan.hpp`,
`window/window_resolver.cpp`):

| Strategy | Rounds (span) | Compositions (work) | Notes |
|---|---|---|---|
| `serial` | N − 1 | N − 1 | the base paper's propagation |
| `hillis-steele` | ⌈log₂ N⌉ | ≈ N log₂ N | every element updated each round |
| `blelloch` | 2⌈log₂ N⌉ | ≈ 4N | work-efficient up- and down-sweep, then every window applied |
| `blocked` | 2N/P + P, in 3 phases | ≈ 2N | compose each of P blocks in parallel, resolve P block windows in order, then expand each block in parallel |

On P cores a scan takes roughly work / P plus its span. Measured on 5 205 chunks
with P = 10: the serial chain performs 5 204 dependent steps; Hillis–Steele needs
only 14 rounds but 59 461 compositions, about 5 900 per core; Blelloch 28 rounds and
21 586 compositions (its down-sweep and final application each touch every element);
the blocked scan 10 417 compositions with a longest dependent chain of 1 051. The
blocked scan is the default: least work per core, three synchronisation points
instead of one per round, and every phase works on contiguous chunks.
A composition is a 32 KiB gather and is memory-bound, so the scan only pays off when
there are many chunks (small chunks or very large files); with 4 MiB chunks on a
200 MB file the whole step takes well under a millisecond either way. The evaluation
reports both regimes.

### 2.5 Work-sized chunking and split-on-overflow (M1, ours)

Fixed-size chunks have equal *compressed* size but not equal decode cost: a chunk of
highly compressible data expands far more and takes longer. Two mechanisms address this.

**Work-balanced partitioning** (`pipeline/partitioner.cpp`). Every
`sample_interval` (512 KiB) of input, a task finds the next dynamic block header and
estimates bytes per symbol and expansion from its Huffman code lengths: a symbol with
code length *L* occurs with probability about 2^−L (`inflate/expansion_estimator.cpp`).
The estimated cost density per compressed byte is integrated along the file and the
file is cut where the cumulative cost crosses multiples of the per-chunk target.

**Split-on-overflow** (`schedule_remainder`). Every chunk has an output budget of
`max_expansion × chunk_size` bytes (20 × by default). The base paper's implementation
falls back to decoding the rest of such a chunk on one thread. We stop the task at the
next block boundary after the budget and schedule the rest as up to P speculative
pieces, each searched for its own block start like any other chunk. The pieces are
linked by the same chain-linking step, so correctness does not depend on the split.

### 2.6 Marker replacement and verification

Each chunk's marked prefix is replaced using its resolved entering window, and its
plain part is copied, in 1 MiB and 4 MiB pieces scheduled on the pool. The CRC-32 of
each gzip member is computed in 4 MiB blocks in parallel and the block CRCs are joined
with `crc32_combine`, which multiplies by x^(8n) modulo the CRC polynomial
(`gzip/crc32.cpp`). On ARMv8 the per-block CRC uses the hardware `crc32` instructions;
elsewhere a slice-by-8 table implementation.

Multi-member files (concatenated gzip, as produced by `pigz` and many tools) are
handled inside the decoder: when a member ends, it parses the footer and the next
header and continues, recording where each member ends.

## 3. Parallel runtime

`parallel/thread_pool.cpp` is a work-stealing pool. Each worker owns a deque; it pushes
and pops its own tasks at the front (LIFO, so recently produced data stays in cache)
and idle workers steal from the back of other deques (FIFO, so the oldest and usually
largest tasks migrate). Each worker records how long it spent running tasks, which the
pipeline uses to report first-stage load imbalance λ = busiest worker / mean.

`parallel/task_group.hpp` provides fork–join on top of the pool: `run` adds a task,
`wait` *helps* by running pending tasks until the group is empty, so a task that waits
for sub-tasks (split-on-overflow schedules into the same group) can never deadlock the
pool. `parallel_for` splits an index range into about four pieces per worker.

## 4. Problems we had to solve

These came up during development and are fixed in the history; each has a regression
test.

- **TaskGroup destroyed while a worker still used it.** `wait()` returned as soon as
  the pending count reached zero, but the worker that decremented it was still about to
  notify the condition variable. The group, which lives on the waiter's stack, could be
  destroyed first. It showed up as `mutex lock failed: Invalid argument` in about 1 in 8
  runs. Fix: decrement and notify under the mutex, and make `wait()` take the mutex
  once more before returning (`fix/task-group-race`).
- **Statistics counted after the task.** `tasks_executed` was incremented after a task
  ran, so a waiter could observe a finished group with a stale count; the test was flaky
  in about 3 % of runs. Tasks are now counted when they start (`fix/task-group-race`).
- **Hang on truncated input.** Past the end of the input the bit reader returns zeros,
  which decode as valid literals forever. The inflater now checks for exhaustion every
  4096 symbols (`fix/truncated-input-hang`).
- **Block finder allocations.** Validating a candidate built full Huffman lookup tables,
  zero-filling them first. With 64 KiB chunks the search took 1.31 s on Silesia. Checking
  the Kraft sum before filling any table and reusing one scratch area per search cut it to
  0.25 s (`fix/block-finder-allocation`).
- **Decoder throughput.** Byte-wise match copies and table-driven CRC-32 kept the
  sequential decoder at 370 MB/s. Hardware CRC-32 and 16-byte overlapping copies raised
  it to about 670 MB/s (`perf/decoder-throughput`).
- **Peak memory of 7× the output.** The first full benchmark run used 7.6 GB to
  decompress the 1 GB log file, on a 16 GB machine, and its timings were dominated by
  memory pressure. Three causes: `std::vector::resize` zero-filled every chunk buffer as
  it grew, which made all of it resident; shrinking a vector to the final size kept its
  capacity, up to twice the data; and every decoded chunk stayed alive until the whole
  output was assembled. A `realloc`-backed `Buffer` (`common/buffer.hpp`) and releasing
  each chunk as soon as its last piece is copied brought it to 3.0 GB and the time from
  0.40 s to 0.20 s (`perf/memory-footprint`). The remaining memory is the 16-bit marked
  output: on the log file 94% of all output bytes are copies out of a chunk's unknown
  window, so markers never die out.
- **Block search in incompressible data.** The finder scanned the whole range for a
  dynamic header before looking for stored blocks. A chunk starting in incompressible
  (stored) data therefore scanned up to 32 million bit offsets for headers that do not
  exist; on the mixed archive the parallel decoder on one thread was 2.5× slower than the
  sequential one. Searching both kinds in 64 KiB segments fixed it, and checking the
  Kraft sum of the code-length code from one 64-bit load, before constructing any
  decoder, cut search time by another quarter (`fix/block-finder-incompressible`).
- **When splitting helps.** Split-on-overflow first appeared to gain 2.7× on the log
  file, but that single run was distorted by the memory problem above. Measured
  properly, it gains nothing when there are many more chunks than threads, because the
  continuations of different chunks already run in parallel; it matters when an output
  budget turns a few large chunks into chains of dependent continuations.

The test suite (`tests/`) runs under AddressSanitizer, UndefinedBehaviorSanitizer and
ThreadSanitizer (`-DRIPTIDE_SANITIZE=ON`, `-DRIPTIDE_SANITIZE_THREAD=ON`).

## 5. Source map

| Directory | Contents |
|---|---|
| `common/` | bit reader, DEFLATE tables, error types, stopwatch |
| `huffman/` | canonical Huffman decoder with two-level tables |
| `inflate/` | chunk decoder (markers or known window), dynamic header reader, block finder, expansion estimator |
| `gzip/` | gzip header and footer, CRC-32 and CRC combination |
| `io/` | memory-mapped input file |
| `parallel/` | work-stealing pool, task group and `parallel_for`, prefix scans |
| `window/` | window maps, composition, scan-based resolution |
| `pipeline/` | options, statistics, partitioner, parallel and sequential decompressors |

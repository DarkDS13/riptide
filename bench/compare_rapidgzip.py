#!/usr/bin/env python3
"""Compare the riptide and rapidgzip command-line tools.

Usage: bench/compare_rapidgzip.py RIPTIDE RAPIDGZIP DATASET_DIR RESULTS_DIR [REPETITIONS]

Both tools run as separate processes in test mode (decompress and verify the CRC-32,
write nothing): `riptide -t` and `rapidgzip -t`. Wall time is measured around the
process and peak resident memory is read from the process's own resource usage.
Repetitions are interleaved across all configurations of an experiment, as in
riptide_bench.

Experiments:
  R1 rapidgzip_scaling.csv  both tools, 1-10 threads, 4 MiB chunks, all datasets
  R2 rapidgzip_chunks.csv   both tools, 10 threads, 64 KiB-4 MiB chunks, Silesia files;
                            riptide with the serial and the blocked window strategy

The rapidgzip command is a Python entry point, so each of its runs includes starting
the interpreter and importing the module (about 15 ms), which riptide does not pay.
Every round therefore also runs both tools on an empty gzip file (rows with file
"empty.gz"); bench/analyze.py subtracts each tool's median start-up time.

rapidgzip's -t streams its output and never holds it whole, while riptide assembles
the complete output in memory; peak memory is therefore not comparable, and is
recorded for completeness.
"""

import csv
import gzip
import os
import subprocess
import sys
import tempfile
import time

FILES = ["silesia.tar.gz", "silesia_x5.tar.gz", "reads.fastq.gz", "logs.ndjson.gz", "mixed.tar.gz"]
THREADS = [1, 2, 4, 6, 8, 10]
CHUNKS_KIB = [64, 256, 1024, 4096]


def run(command):
    """Runs a command; returns wall seconds and peak resident bytes."""
    start = time.perf_counter()
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    _, status, usage = os.wait4(process.pid, 0)
    seconds = time.perf_counter() - start
    stderr = process.stderr.read().decode(errors="replace")
    process.stderr.close()
    if os.waitstatus_to_exitcode(status) != 0:
        raise RuntimeError(f"{' '.join(command)} failed: {stderr.strip()}")
    # ru_maxrss is in bytes on macOS and in kilobytes on Linux.
    rss = usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024
    return seconds, rss


def configurations_scaling(riptide, rapidgzip, data):
    for name in FILES:
        path = os.path.join(data, name)
        for threads in THREADS:
            yield ({"file": name, "tool": "riptide", "threads": threads, "chunk_kib": 4096, "window": "blocked"},
                   [riptide, "-t", "-P", str(threads), "--chunk-size", "4M", path])
            yield ({"file": name, "tool": "rapidgzip", "threads": threads, "chunk_kib": 4096, "window": "serial"},
                   [rapidgzip, "-t", "-P", str(threads), "--chunk-size", "4096", path])


def configurations_chunks(riptide, rapidgzip, data):
    for name in ["silesia.tar.gz", "silesia_x5.tar.gz"]:
        path = os.path.join(data, name)
        for kib in CHUNKS_KIB:
            for window in ["serial", "blocked"]:
                yield ({"file": name, "tool": "riptide", "threads": 10, "chunk_kib": kib, "window": window},
                       [riptide, "-t", "-P", "10", "--chunk-size", f"{kib}K", "--window", window, path])
            yield ({"file": name, "tool": "rapidgzip", "threads": 10, "chunk_kib": kib, "window": "serial"},
                   [rapidgzip, "-t", "-P", "10", "--chunk-size", str(kib), path])


def configurations_startup(riptide, rapidgzip, empty):
    for tool, binary in (("riptide", riptide), ("rapidgzip", rapidgzip)):
        yield ({"file": "empty.gz", "tool": tool, "threads": 10, "chunk_kib": 4096, "window": "-"},
               [binary, "-t", "-P", "10", empty])


def experiment(path, configurations, repetitions):
    configurations = list(configurations)
    fields = ["file", "tool", "threads", "chunk_kib", "window", "repetition", "seconds", "max_rss_bytes"]
    with open(path, "w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        for _, command in configurations:   # one unmeasured warm-up run each
            run(command)
        for r in range(repetitions):
            order = configurations if r % 2 == 0 else configurations[::-1]
            for row, command in order:
                seconds, rss = run(command)
                writer.writerow({**row, "repetition": r, "seconds": f"{seconds:.6f}", "max_rss_bytes": rss})
            out.flush()
            print(f"{os.path.basename(path)}: repetition {r + 1}/{repetitions}", flush=True)


def main():
    riptide, rapidgzip, data, results = sys.argv[1:5]
    repetitions = int(sys.argv[5]) if len(sys.argv) > 5 else 10
    os.makedirs(results, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        empty = os.path.join(scratch, "empty.gz")
        with open(empty, "wb") as f:
            f.write(gzip.compress(b""))
        experiment(os.path.join(results, "rapidgzip_scaling.csv"),
                   [*configurations_scaling(riptide, rapidgzip, data),
                    *configurations_startup(riptide, rapidgzip, empty)], repetitions)
        experiment(os.path.join(results, "rapidgzip_chunks.csv"),
                   [*configurations_chunks(riptide, rapidgzip, data),
                    *configurations_startup(riptide, rapidgzip, empty)], repetitions)


if __name__ == "__main__":
    main()

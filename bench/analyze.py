#!/usr/bin/env python3
"""Summarise benchmark CSVs: medians, IQRs, speedups and significance tests.

Usage: bench/analyze.py RESULTS_DIR

Reads the CSVs written by bench/run_benchmarks.sh and writes
  RESULTS_DIR/summary.json   every number used in the report and slides
  RESULTS_DIR/summary.md     the same as readable tables

Repeated runs of two configurations are independent samples, not pairs, so
configurations are compared with the two-sided Mann-Whitney U test (exact
distribution, or the normal approximation with tie correction when ranks tie),
with Holm-Bonferroni correction over every comparison of an experiment. Effect
sizes are the ratio of medians and the rank-biserial correlation.
"""

import csv
import json
import math
import os
import statistics
import sys
from collections import defaultdict
from functools import lru_cache

KEY = ("file", "engine", "threads", "chunk_size", "window", "chunking", "overflow")


# ---------------------------------------------------------------- loading

def load(path):
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            row["seconds"] = float(row["seconds"])
            row["threads"] = int(row["threads"])
            row["chunk_size"] = int(row["chunk_size"])
            row["output_bytes"] = int(row["output_bytes"])
            row["identical"] = row["identical"] == "true"
            row["stats"] = json.loads(row["statistics"]) if row["statistics"] else {}
            rows.append(row)
    return rows


def group(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[k] for k in KEY)].append(row)
    return groups


def median(values):
    return statistics.median(values)


def iqr(values):
    if len(values) < 2:
        return 0.0
    q = statistics.quantiles(values, n=4, method="inclusive")
    return q[2] - q[0]


def stat(runs, name):
    return [r["stats"].get(name, 0.0) for r in runs]


# ---------------------------------------------------------------- statistics

@lru_cache(maxsize=None)
def u_distribution(n1, n2):
    """Number of arrangements giving each U value, for samples of size n1 and n2."""
    # counts[m][k] = ways for m "x" items among m + k positions; built incrementally.
    @lru_cache(maxsize=None)
    def ways(m, k, u):
        if u < 0:
            return 0
        if m == 0 or k == 0:
            return 1 if u == 0 else 0
        return ways(m - 1, k, u - k) + ways(m, k - 1, u)
    return [ways(n1, n2, u) for u in range(n1 * n2 + 1)]


def mann_whitney(x, y):
    """Two-sided Mann-Whitney U test. Returns (U, p, rank-biserial r)."""
    n1, n2 = len(x), len(y)
    combined = sorted([(v, 0) for v in x] + [(v, 1) for v in y])
    ranks = [0.0] * len(combined)
    i = 0
    ties = False
    tie_term = 0.0
    while i < len(combined):
        j = i
        while j + 1 < len(combined) and combined[j + 1][0] == combined[i][0]:
            j += 1
        if j > i:
            ties = True
            t = j - i + 1
            tie_term += t ** 3 - t
        for k in range(i, j + 1):
            ranks[k] = (i + j) / 2 + 1
        i = j + 1
    rank_x = sum(r for r, (_, g) in zip(ranks, combined) if g == 0)
    u = rank_x - n1 * (n1 + 1) / 2
    if not ties and n1 * n2 <= 400:
        dist = u_distribution(n1, n2)
        total = sum(dist)
        lower = sum(dist[: int(u) + 1]) / total
        upper = sum(dist[int(u):]) / total
        p = min(1.0, 2 * min(lower, upper))
    else:
        n = n1 + n2
        mean = n1 * n2 / 2
        var = n1 * n2 / 12 * ((n + 1) - tie_term / (n * (n - 1)))
        z = (abs(u - mean) - 0.5) / math.sqrt(var) if var > 0 else 0.0
        p = min(1.0, math.erfc(z / math.sqrt(2)))
    r = 1 - 2 * u / (n1 * n2)
    return u, p, r


def holm(p_values):
    """Holm-Bonferroni adjusted p-values, in input order."""
    order = sorted(range(len(p_values)), key=lambda i: p_values[i])
    adjusted = [0.0] * len(p_values)
    running = 0.0
    m = len(p_values)
    for rank, i in enumerate(order):
        running = max(running, min(1.0, (m - rank) * p_values[i]))
        adjusted[i] = running
    return adjusted


def compare(name, baseline, candidate, metric):
    """Comparison record of `candidate` against `baseline` runs on one metric."""
    b, c = metric(baseline), metric(candidate)
    u, p, r = mann_whitney(c, b)
    return {"name": name, "baseline_median": median(b), "candidate_median": median(c),
            "ratio": median(b) / median(c) if median(c) > 0 else float("inf"),
            "U": u, "p": p, "rank_biserial": r}


def finish(comparisons):
    for c, adjusted in zip(comparisons, holm([c["p"] for c in comparisons])):
        c["p_holm"] = adjusted
        c["significant"] = adjusted < 0.05
    return comparisons


# ---------------------------------------------------------------- experiments

def seconds(runs):
    return [r["seconds"] for r in runs]


def scaling(rows):
    g = group(rows)
    out = {}
    for f in sorted({r["file"] for r in rows}):
        runs = {k: v for k, v in g.items() if k[0] == f}
        def pick(engine, threads=1):
            return next((v for k, v in runs.items() if k[1] == engine and k[2] == threads), None)
        zlib = pick("zlib")
        sequential = pick("riptide-sequential")
        size = zlib[0]["output_bytes"]
        entry = {
            "output_bytes": size,
            "compressed_bytes": next(r["stats"]["compressed_bytes"] for v in runs.values()
                                     for r in v if r["stats"]),
            "zlib_seconds": median(seconds(zlib)),
            "sequential_seconds": median(seconds(sequential)),
            "threads": [],
        }
        parallel_one = median(seconds(pick("riptide", 1)))
        for threads in sorted({k[2] for k in runs if k[1] == "riptide"}):
            v = pick("riptide", threads)
            t = median(seconds(v))
            entry["threads"].append({
                "threads": threads, "seconds": t, "iqr": iqr(seconds(v)),
                "throughput_mb_s": size / t / 1e6,
                "speedup_vs_parallel_1": parallel_one / t,
                "efficiency": parallel_one / t / threads,
                "speedup_vs_sequential": entry["sequential_seconds"] / t,
                "speedup_vs_zlib": entry["zlib_seconds"] / t,
                "decode_imbalance": median(stat(v, "decode_imbalance")),
                "stage_seconds": {s: median(stat(v, s + "_seconds")) for s in
                                  ("partition", "decode", "chain", "window_map", "window",
                                   "replace", "checksum")},
            })
        entry["compression_ratio"] = size / entry["compressed_bytes"]
        out[f] = entry
    return out


def window_chunks(rows):
    g = group(rows)
    out, comparisons = {}, []
    for f in sorted({r["file"] for r in rows}):
        out[f] = []
        for chunk in sorted({k[3] for k in g if k[0] == f}):
            cell = {k[4]: v for k, v in g.items() if k[0] == f and k[3] == chunk}
            serial = cell["serial"]
            item = {"chunk_size": chunk, "chunks": int(median(stat(serial, "chain_length"))), "strategies": {}}
            for strategy, v in cell.items():
                item["strategies"][strategy] = {
                    "window_seconds": median(stat(v, "window_seconds")),
                    "window_iqr": iqr(stat(v, "window_seconds")),
                    "total_seconds": median(seconds(v)),
                    "rounds": int(median(stat(v, "window_rounds"))),
                    "combinations": int(median(stat(v, "window_combinations"))),
                    "window_share": median([s / t for s, t in zip(stat(v, "window_seconds"), seconds(v))]),
                }
                if strategy != "serial":
                    comparisons.append(compare(f"{f} {chunk // 1024} KiB window step: {strategy} vs serial",
                                               serial, v, lambda runs: stat(runs, "window_seconds")))
                    comparisons.append(compare(f"{f} {chunk // 1024} KiB total: {strategy} vs serial",
                                               serial, v, seconds))
            out[f].append(item)
    return out, finish(comparisons)


def window_threads(rows):
    g = group(rows)
    out, comparisons = [], []
    for threads in sorted({k[2] for k in g}):
        cell = {k[4]: v for k, v in g.items() if k[2] == threads}
        out.append({"threads": threads,
                    **{f"{s}_window_seconds": median(stat(v, "window_seconds")) for s, v in cell.items()},
                    **{f"{s}_total_seconds": median(seconds(v)) for s, v in cell.items()}})
        comparisons.append(compare(f"{threads} threads window step: blocked vs serial",
                                   cell["serial"], cell["blocked"], lambda runs: stat(runs, "window_seconds")))
    return out, finish(comparisons)


def m1_policies(rows):
    g = group(rows)
    out, comparisons = {}, []
    for f in sorted({r["file"] for r in rows}):
        cell = {(k[5], k[6]): v for k, v in g.items() if k[0] == f}
        out[f] = {f"{c}/{o}": {
            "total_seconds": median(seconds(v)), "iqr": iqr(seconds(v)),
            "decode_seconds": median(stat(v, "decode_seconds")),
            "partition_seconds": median(stat(v, "partition_seconds")),
            "decode_imbalance": median(stat(v, "decode_imbalance")),
            "overflow_splits": int(median(stat(v, "overflow_splits"))),
            "chain_length": int(median(stat(v, "chain_length"))),
        } for (c, o), v in cell.items()}
        base = cell[("fixed", "serial")]
        for key in [("fixed", "split"), ("work-balanced", "serial"), ("work-balanced", "split")]:
            comparisons.append(compare(f"{f} total: {key[0]}/{key[1]} vs fixed/serial", base, cell[key], seconds))
    return out, finish(comparisons)


def memory(path):
    rows = list(csv.DictReader(open(path)))
    cells = defaultdict(list)
    for r in rows:
        cells[(r["file"], r["chunk_size"], r["overflow"])].append(r)
    return [{"file": f, "chunk_size": c, "overflow": o,
             "max_rss_mb": median([int(r["max_rss_bytes"]) for r in v]) / 1e6,
             "rss_over_output": median([int(r["max_rss_bytes"]) / int(r["output_bytes"]) for r in v])}
            for (f, c, o), v in sorted(cells.items())]


# ---------------------------------------------------------------- output

def correctness(all_rows):
    return {"runs": len(all_rows), "identical": sum(r["identical"] for r in all_rows)}


def markdown(summary):
    lines = ["# Benchmark summary", "",
             f"Correctness: {summary['correctness']['identical']} of {summary['correctness']['runs']} "
             "measured runs byte-identical to zlib.", ""]
    lines += ["## E1 Scaling (4 MiB chunks, blocked windows)", "",
              "| File | Ratio | zlib MB/s | Seq. MB/s | Threads | MB/s | Speedup vs 1 thread | Efficiency | vs zlib | λ |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    for f, e in summary["scaling"].items():
        zlib = e["output_bytes"] / e["zlib_seconds"] / 1e6
        seq = e["output_bytes"] / e["sequential_seconds"] / 1e6
        for t in e["threads"]:
            lines.append(f"| {f} | {e['compression_ratio']:.2f} | {zlib:.0f} | {seq:.0f} | {t['threads']} | "
                         f"{t['throughput_mb_s']:.0f} | {t['speedup_vs_parallel_1']:.2f} | {t['efficiency']:.2f} | "
                         f"{t['speedup_vs_zlib']:.2f} | {t['decode_imbalance']:.2f} |")
    lines += ["", "## E2 Window resolution across chunk sizes (10 threads)", "",
              "| File | Chunk | Chunks | Strategy | Window ms | Rounds | Window share of total | Total ms |",
              "|---|---|---|---|---|---|---|---|"]
    for f, items in summary["window_chunks"].items():
        for item in items:
            for s, v in item["strategies"].items():
                lines.append(f"| {f} | {item['chunk_size'] // 1024} KiB | {item['chunks']} | {s} | "
                             f"{v['window_seconds'] * 1e3:.2f} | {v['rounds']} | {v['window_share'] * 100:.1f}% | "
                             f"{v['total_seconds'] * 1e3:.1f} |")
    for title, key in (("E2 comparisons", "window_chunks_tests"), ("E3 comparisons", "window_threads_tests"),
                       ("E4 comparisons", "m1_tests")):
        lines += ["", f"## {title} (Mann-Whitney U, Holm-corrected)", "",
                  "| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |",
                  "|---|---|---|---|---|---|"]
        for c in summary[key]:
            lines.append(f"| {c['name']} | {c['baseline_median'] * 1e3:.3f} ms | {c['candidate_median'] * 1e3:.3f} ms | "
                         f"{c['ratio']:.2f}x | {c['p_holm']:.2g} | {'yes' if c['significant'] else 'no'} |")
    lines += ["", "## E3 Window resolution across threads (64 KiB chunks)", "",
              "| Threads | Serial window ms | Blocked window ms | Serial total ms | Blocked total ms |", "|---|---|---|---|---|"]
    for t in summary["window_threads"]:
        lines.append(f"| {t['threads']} | {t['serial_window_seconds'] * 1e3:.2f} | {t['blocked_window_seconds'] * 1e3:.2f} | "
                     f"{t['serial_total_seconds'] * 1e3:.1f} | {t['blocked_total_seconds'] * 1e3:.1f} |")
    lines += ["", "## E4 Chunking and overflow policies (10 threads, 4 MiB)", "",
              "| File | Chunking/overflow | Total ms | Decode ms | λ | Overflow splits | Chain |", "|---|---|---|---|---|---|---|"]
    for f, cells in summary["m1"].items():
        for k, v in cells.items():
            lines.append(f"| {f} | {k} | {v['total_seconds'] * 1e3:.1f} | {v['decode_seconds'] * 1e3:.1f} | "
                         f"{v['decode_imbalance']:.2f} | {v['overflow_splits']} | {v['chain_length']} |")
    if summary.get("memory"):
        lines += ["", "## E5 Peak memory (10 threads)", "", "| File | Chunk | Overflow | Peak RSS MB | RSS / output |",
                  "|---|---|---|---|---|"]
        for m in summary["memory"]:
            lines.append(f"| {m['file']} | {m['chunk_size']} | {m['overflow']} | {m['max_rss_mb']:.0f} | {m['rss_over_output']:.2f} |")
    return "\n".join(lines) + "\n"


def main():
    results = sys.argv[1]
    path = lambda name: os.path.join(results, name)
    scaling_rows = load(path("scaling.csv"))
    chunk_rows = load(path("window_chunks.csv"))
    thread_rows = load(path("window_threads.csv"))
    m1_rows = load(path("m1_policies.csv"))

    summary = {"correctness": correctness(scaling_rows + chunk_rows + thread_rows + m1_rows)}
    summary["scaling"] = scaling(scaling_rows)
    summary["window_chunks"], summary["window_chunks_tests"] = window_chunks(chunk_rows)
    summary["window_threads"], summary["window_threads_tests"] = window_threads(thread_rows)
    summary["m1"], summary["m1_tests"] = m1_policies(m1_rows)
    if os.path.exists(path("memory.csv")):
        summary["memory"] = memory(path("memory.csv"))

    json.dump(summary, open(path("summary.json"), "w"), indent=1)
    open(path("summary.md"), "w").write(markdown(summary))
    print(markdown(summary))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Measurements and tables for bench/demo.sh.

Usage: bench/demo_stats.py STEP RIPTIDE FILE [options]

Each step runs riptide (and gzip or rapidgzip where needed) a few times, takes the
median, and prints a small table with the speed-ups worked out. The "report" column
holds the medians of 10 runs from results/summary.md, so that the live numbers can be
read next to the ones on the slides. Every step also stores its numbers in
NUMBERS (default $TMPDIR/riptide-demo/numbers.json); the "summary" step prints them
all at the end.

Steps:
  race       gzip -dc against riptide, both writing to /dev/null
  stages     time of each pipeline stage at the default 4 MiB chunks
  threads    throughput and efficiency on 1, 2, 4 and 10 threads
  window     every window strategy at 64 KiB chunks (M3)
  chunks     serial, blocked and gpu-blocked at 64 KiB to 1 MiB chunks (M3)
  overflow   serial against split overflow with 16 MiB chunks and a 4x budget (M1)
  memory     peak memory with 4 MiB and 512 KiB chunks
  rapidgzip  riptide against rapidgzip on 10 and 1 threads, if rapidgzip is installed
  summary    every number measured so far, next to the report
"""

import json
import os
import shutil
import statistics
import subprocess
import sys
import time

REPS = int(os.environ.get("REPS", "3"))
NUMBERS = os.environ.get("NUMBERS", os.path.join(os.environ.get("TMPDIR", "/tmp"), "riptide-demo", "numbers.json"))


def load():
    try:
        with open(NUMBERS) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save(key, value):
    numbers = load()
    numbers[key] = value
    os.makedirs(os.path.dirname(NUMBERS), exist_ok=True)
    with open(NUMBERS, "w") as f:
        json.dump(numbers, f, indent=1)


def timed(command, stdout=subprocess.DEVNULL):
    """Runs command; returns wall seconds, CPU seconds and peak memory in bytes."""
    start = time.perf_counter()
    proc = subprocess.Popen(command, stdout=stdout, stderr=subprocess.PIPE)
    _, status, usage = os.wait4(proc.pid, 0)
    wall = time.perf_counter() - start
    err = proc.stderr.read().decode()
    proc.returncode = os.waitstatus_to_exitcode(status)
    if proc.returncode:
        sys.exit(f"failed ({proc.returncode}): {' '.join(command)}\n{err}")
    rss = usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024
    return wall, usage.ru_utime + usage.ru_stime, rss, err


def stats(riptide, file, *args, reps=REPS):
    """Median of every field of riptide's --json statistics over reps runs."""
    runs = []
    for _ in range(reps):
        err = timed([riptide, "-t", "--json", "--repeat", "2", *args, file])[3]
        runs.append(json.loads(next(l for l in reversed(err.splitlines()) if l.startswith("{"))))
    return {k: statistics.median(r[k] for r in runs) for k in runs[0]}


def table(header, rows, left=1):
    """Prints rows under header; the first `left` columns are left-aligned."""
    cols = list(zip(header, *rows))
    widths = [max(len(str(c)) for c in col) for col in cols]
    line = lambda r: "   " + "  ".join(str(c).rjust(w) if i >= left else str(c).ljust(w)
                                       for i, (c, w) in enumerate(zip(r, widths)))
    print(line(header))
    print("   " + "  ".join("-" * w for w in widths))
    for r in rows:
        print(line(r))


def ms(seconds):
    return f"{seconds * 1000:.1f}"


def x(a, b):
    return f"{a / b:.2f}x"


def race(riptide, file):
    gzip_runs, rip_runs = [], []
    for _ in range(REPS):           # interleaved, so that both see the same machine
        gzip_runs.append(timed(["gzip", "-dc", file]))
        rip_runs.append(timed([riptide, file]))
    out = stats(riptide, file, reps=1)["decompressed_bytes"]
    g = statistics.median(r[0] for r in gzip_runs)
    r = statistics.median(r[0] for r in rip_runs)
    cores = statistics.median(c / w for w, c, *_ in rip_runs)
    table(["", "wall s", "MB/s", "cores"],
          [["gzip -dc", f"{g:.3f}", f"{out / g / 1e6:,.0f}", "1"],
           ["riptide", f"{r:.3f}", f"{out / r / 1e6:,.0f}", f"{cores:.1f}"]])
    print(f"\n   riptide is {g / r:.2f}x faster than gzip (median of {REPS} runs each)")
    print("   report: 2.47x faster than zlib, 3,008 MB/s (Silesia x5, 10 threads)")
    save("race", {"gzip_s": g, "riptide_s": r, "speedup": g / r, "cores": cores,
                  "mb_s": out / r / 1e6})


def stages(riptide, file):
    s = stats(riptide, file)
    total = s["total_seconds"]
    parts = [("find chunk starts", "partition_seconds"), ("decode (first stage)", "decode_seconds"),
             ("link chunks", "chain_seconds"), ("window maps", "window_map_seconds"),
             ("window step", "window_seconds"), ("replace (second stage)", "replace_seconds"),
             ("CRC-32 check", "checksum_seconds")]
    table(["stage", "ms", "share"],
          [[name, ms(s[k]), f"{100 * s[k] / total:.1f}%"] for name, k in parts]
          + [["total", ms(total), "100%"]])
    print(f"\n   {s['decompressed_bytes'] / 1e6:,.0f} MB out, {s['throughput_mb_per_s']:,.0f} MB/s inside riptide")
    print(f"   {s['chain_length']:.0f} chunks, {s['rejected_candidates']:.0f} rejected start guesses, "
          f"load imbalance {s['decode_imbalance']:.2f}, "
          f"{s['tasks_executed']:.0f} tasks of which {s['tasks_stolen']:.0f} stolen")
    print(f"   window step: {s['window_rounds']:.0f} rounds, {s['window_combinations']:.0f} compositions")
    save("stages", {k: s[k] for k in ("decode_seconds", "window_seconds", "replace_seconds",
                                      "checksum_seconds", "total_seconds", "decode_imbalance")})


THREADS_REPORT = {1: 906, 2: 1295, 4: 1923, 10: 3008}


def threads(riptide, file):
    rows, base, out = [], None, {}
    for t in THREADS_REPORT:
        s = stats(riptide, file, "-P", str(t))
        mb = s["throughput_mb_per_s"]
        base = base or mb
        rows.append([t, f"{mb:,.0f}", x(mb, base), f"{mb / base / t:.2f}", f"{THREADS_REPORT[t]:,}"])
        out[t] = mb
    table(["threads", "MB/s", "speed-up", "efficiency", "report MB/s"], rows)
    print("\n   efficiency = speed-up / threads. Only 4 of the 10 cores are fast cores,")
    print("   and memory bandwidth is shared, so it falls to about 0.3 at 10 threads.")
    save("threads", out)


WINDOW_REPORT = {"serial": "183.9", "hillis-steele": "1.47x (E2)", "blelloch": "1.99x (E2)",
                 "blocked": "65.9", "gpu-serial": "36.2", "gpu-blocked": "26.4"}


def window(riptide, file):
    rows, out = [], {}
    for w in WINDOW_REPORT:
        s = stats(riptide, file, "--chunk-size", "64K", "--window", w)
        out[w] = s
    serial = out["serial"]["window_seconds"]
    for w, s in out.items():
        rows.append([w, ms(s["window_seconds"]), x(serial, s["window_seconds"]),
                     f"{s['window_rounds']:.0f}", f"{s['window_combinations']:,.0f}",
                     ms(s["total_seconds"]), WINDOW_REPORT[w]])
    print(f"   {out['serial']['chain_length']:.0f} chunks of 64 KiB, so {out['serial']['chain_length'] - 1:.0f} "
          "window hand-offs\n")
    table(["strategy", "window ms", "vs serial", "rounds", "merges", "whole run ms", "report"], rows)
    print("\n   rounds = steps that must wait for each other; merges = window compositions.")
    print("   Hillis-Steele and Blelloch wait for all threads after every round, so their")
    print("   times jump around from run to run; blocked waits only a few times.")
    print("   gpu-blocked against blocked: "
          + x(out["blocked"]["window_seconds"], out["gpu-blocked"]["window_seconds"])
          + " (report 2.49x). Whole run: "
          + x(out["serial"]["total_seconds"], out["gpu-blocked"]["total_seconds"])
          + " faster than serial.")
    save("window", {w: {"window_ms": s["window_seconds"] * 1000, "total_ms": s["total_seconds"] * 1000,
                        "rounds": s["window_rounds"]} for w, s in out.items()})


CHUNKS_REPORT = {"64K": (183.9, 65.9, 26.4), "128K": (96.9, 31.6, 14.2),
                 "256K": (57.4, 16.9, 10.5), "1M": (9.9, 3.5, 3.8)}


def chunks(riptide, file):
    rows, out = [], {}
    for size, report in CHUNKS_REPORT.items():
        v = [stats(riptide, file, "--chunk-size", size, "--window", w)["window_seconds"]
             for w in ("serial", "blocked", "gpu-blocked")]
        rows.append([size, ms(v[0]), ms(v[1]), ms(v[2]), x(v[0], v[1]), x(v[0], v[2]),
                     " / ".join(f"{r:g}" for r in report)])
        out[size] = [t * 1000 for t in v]
    table(["chunk", "serial ms", "blocked ms", "gpu ms", "CPU scan", "GPU scan", "report ms"], rows)
    print("\n   The scan wins at every chunk size; from 1 MiB up the window step is so small")
    print("   that the GPU's fixed cost of about 1 ms is as big as the work.")
    save("chunks", out)


def overflow(riptide, file):
    out = {}
    for policy in ("serial", "split"):
        out[policy] = stats(riptide, file, "--chunk-size", "16M", "--max-expansion", "4",
                            "--overflow", policy)
    rep = {"serial": ("407.0", "5.58"), "split": ("255.7", "1.29")}
    table(["overflow", "whole run ms", "decode ms", "imbalance", "splits", "report ms", "report imbalance"],
          [[p, ms(s["total_seconds"]), ms(s["decode_seconds"]), f"{s['decode_imbalance']:.2f}",
            f"{s['overflow_splits']:.0f}", *rep[p]] for p, s in out.items()])
    print(f"\n   split is {x(out['serial']['total_seconds'], out['split']['total_seconds'])} faster "
          "(report 1.59x). Imbalance = slowest thread / average thread.")
    save("overflow", {p: {"total_ms": s["total_seconds"] * 1000, "imbalance": s["decode_imbalance"]}
                      for p, s in out.items()})


def memory(riptide, file):
    out, rows = {}, []
    size = stats(riptide, file, reps=1)["decompressed_bytes"]
    for chunk, rep in (("4M", 2336), ("512K", 1896)):
        rss = statistics.median(timed([riptide, "-t", "--chunk-size", chunk, file])[2] for _ in range(REPS))
        out[chunk] = rss
        rows.append([chunk, f"{rss / 1e6:,.0f}", f"{rss / size:.2f}x", f"{rep:,}"])
    table(["chunk", "peak MB", "of output", "report MB"], rows)
    print(f"\n   512 KiB chunks use {100 * (1 - out['512K'] / out['4M']):.0f}% less memory "
          "(report 19%; 35% on the logs file).")
    print("   rapidgzip streams its output and uses 2-5x less; riptide keeps the whole output.")
    save("memory", {k: v / 1e6 for k, v in out.items()})


def rapidgzip(riptide, file):
    tool = os.environ.get("RAPIDGZIP") or shutil.which("rapidgzip")
    if not tool:
        print("   rapidgzip is not installed (pip install rapidgzip==0.16.0), so this step is skipped.")
        print("   report (Silesia x5, GB/s, riptide vs rapidgzip): 10 threads 2.50 vs 2.16,")
        print("   1 thread 0.86 vs 0.46. Level at 10 threads, riptide 1.2-1.9x faster on 1-2.")
        return
    rows, out = [], {}
    for t, rep in ((10, "2.50 vs 2.16"), (1, "0.86 vs 0.46")):
        r = statistics.median(timed([riptide, "-t", "-P", str(t), file])[0] for _ in range(REPS))
        g = statistics.median(timed([tool, "-t", "-P", str(t), file])[0] for _ in range(REPS))
        rows.append([t, f"{r:.3f}", f"{g:.3f}", x(g, r), rep])
        out[t] = {"riptide_s": r, "rapidgzip_s": g}
    table(["threads", "riptide s", "rapidgzip s", "riptide faster by", "report GB/s"], rows)
    print("\n   Wall times include starting each process (rapidgzip also starts Python).")
    save("rapidgzip", out)


def summary(riptide=None, file=None):
    n = load()
    rows = []
    if "race" in n:
        r = n["race"]
        rows += [["riptide vs gzip", f"{r['speedup']:.2f}x ({r['gzip_s']:.2f} s vs {r['riptide_s']:.2f} s)", "2.47x"],
                 ["throughput", f"{r['mb_s']:,.0f} MB/s, {r['cores']:.1f} cores busy", "3,008 MB/s"]]
    if "stages" in n:
        s = n["stages"]
        rows.append(["decode / replace share", f"{100 * s['decode_seconds'] / s['total_seconds']:.0f}% / "
                     f"{100 * s['replace_seconds'] / s['total_seconds']:.0f}%", "about 73% / 21%"])
    if "threads" in n:
        t = n["threads"]
        rows.append(["efficiency at 10 threads", f"{t['10'] / t['1'] / 10:.2f}", "0.33"])
    if "window" in n:
        w = {k: v["window_ms"] for k, v in n["window"].items()}
        rows += [["window step serial / blocked / GPU",
                  f"{w['serial']:.1f} / {w['blocked']:.1f} / {w['gpu-blocked']:.1f} ms", "183.9 / 65.9 / 26.4 ms"],
                 ["CPU scan vs serial", x(w["serial"], w["blocked"]), "2.79x (2.9-3.9x across chunk sizes)"],
                 ["GPU scan vs serial", x(w["serial"], w["gpu-blocked"]), "6.96x"],
                 ["Hillis-Steele / Blelloch vs serial",
                  f"{x(w['serial'], w['hillis-steele'])} / {x(w['serial'], w['blelloch'])}", "1.47x / 1.99x"],
                 ["whole run serial vs GPU", x(n["window"]["serial"]["total_ms"],
                                               n["window"]["gpu-blocked"]["total_ms"]), "1.22x (990 vs 813 ms)"]]
    if "overflow" in n:
        o = n["overflow"]
        rows.append(["M1 split vs serial overflow", f"{x(o['serial']['total_ms'], o['split']['total_ms'])}, "
                     f"imbalance {o['serial']['imbalance']:.2f} -> {o['split']['imbalance']:.2f}",
                     "1.59x, 5.58 -> 1.29"])
    if "memory" in n:
        m = n["memory"]
        rows.append(["peak memory 4M vs 512K", f"{m['4M']:,.0f} vs {m['512K']:,.0f} MB", "2,336 vs 1,896 MB"])
    if "rapidgzip" in n:
        g = n["rapidgzip"]
        rows.append(["vs rapidgzip, 10 / 1 threads",
                     f"{x(g['10']['rapidgzip_s'], g['10']['riptide_s'])} / {x(g['1']['rapidgzip_s'], g['1']['riptide_s'])}",
                     "level / 1.87x"])
    if not rows:
        print("   nothing measured yet")
        return
    table(["", "today", "report"], rows, left=3)


STEPS = {f.__name__: f for f in (race, stages, threads, window, chunks, overflow, memory, rapidgzip, summary)}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in STEPS:
        sys.exit(__doc__)
    STEPS[sys.argv[1]](*sys.argv[2:4])

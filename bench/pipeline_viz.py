#!/usr/bin/env python3
"""Turn riptide timelines (riptide --trace FILE) into an animated view of the pipeline.

Usage: bench/pipeline_viz.py OUTPUT.html TRACE.json[=LABEL] [TRACE.json[=LABEL] ...]

Writes one self-contained HTML page (no network needed) that replays each run in slow
motion: the six stages with their measured times, one strip per stage showing what
every chunk is doing, and the thread timeline. Each trace becomes a tab.

Everything shown comes from the trace. Chunks are matched to decode tasks by their
compressed offset and to replacement tasks by their chunk number. The window step is
traced per block, not per chunk; inside a block the chunks are handled one after
another with the same amount of work each (one composition of 32,768 entries), so
their times are spread evenly over the block's task.
"""

import json
import os
import sys

CATEGORIES = ["decode", "chain", "window", "gpu", "replace", "verify", "wait"]
STAGES = ["partition", "decode", "chain", "windows", "replace", "verify"]


def r(x):
    return round(x, 3)


def load(path, label):
    events = json.load(open(path))["traceEvents"]
    names = {e["tid"]: e["args"]["name"] for e in events if e.get("ph") == "M" and e["name"] == "thread_name"}
    order = {e["tid"]: e["args"]["sort_index"] for e in events
             if e.get("ph") == "M" and e["name"] == "thread_sort_index"}
    spans = [e for e in events if e.get("ph") == "X"]
    stage_spans = {e["name"]: e for e in spans if e["cat"] == "stage"}
    t0 = min(e["ts"] for e in stage_spans.values())
    ms = lambda ts: (ts - t0) / 1000.0
    total = max(ms(e["ts"] + e["dur"]) for e in stage_spans.values())

    tids = sorted(names, key=lambda t: order.get(t, 1 << 30))
    row = {t: i for i, t in enumerate(tids)}
    work = sorted(([row[e["tid"]], CATEGORIES.index(e["cat"]) if e["cat"] in CATEGORIES else 1,
                    e["name"], r(ms(e["ts"])), r(e["dur"] / 1000.0), e.get("args", {})]
                   for e in spans if e["cat"] != "stage" and e["tid"] in row), key=lambda w: w[3])
    steals = [[row[e["tid"]], r(ms(e["ts"]))] for e in events
              if e.get("ph") == "i" and e.get("name") == "steal" and e["tid"] in row]

    decode = sorted((e for e in spans if e["cat"] == "decode"), key=lambda e: e["args"]["compressed_offset"])
    replace = {}
    for e in spans:
        if e["cat"] == "replace" and "chunk" in e.get("args", {}):
            s, f = ms(e["ts"]), ms(e["ts"] + e["dur"])
            a, b = replace.get(e["args"]["chunk"], (s, f))
            replace[e["args"]["chunk"]] = (min(a, s), max(b, f))
    n = max(len(decode), max(replace, default=-1) + 1)

    def column(values):
        return [r(v) if v is not None else -1 for v in values]

    d_start = [None] * n
    d_end = [None] * n
    for i, e in enumerate(decode):
        d_start[i], d_end[i] = ms(e["ts"]), ms(e["ts"] + e["dur"])
    r_start = [replace.get(i, (None, None))[0] for i in range(n)]
    r_end = [replace.get(i, (None, None))[1] for i in range(n)]

    # Window maps: map k belongs to chunk k + 1, and its result is the window that
    # chunk k + 2 needs. Chunks 0 and 1 need nothing from the window step.
    windows = stage_spans.get("windows")
    w_begin = ms(windows["ts"]) if windows else 0.0
    w_finish = ms(windows["ts"] + windows["dur"]) if windows else 0.0
    maps = max(n - 1, 0)
    c_start, c_end = [None] * n, [None] * n
    k_start, k_end = [w_begin] * n, [w_begin] * n
    by_name = lambda name: [e for e in spans if e["name"] == name]
    strategy = "other"

    def spread(event, b0, b1, k, longest=None):
        s, f = ms(event["ts"]), ms(event["ts"] + event["dur"])
        length = longest or (b1 - b0)
        j = k - b0
        return s + (f - s) * j / length, s + (f - s) * (j + 1) / length

    def put(starts, ends, k, pair):
        if k + 2 < n:
            starts[k + 2], ends[k + 2] = pair

    if by_name("serial chain"):
        strategy = "serial"
        e = by_name("serial chain")[0]
        for k in range(maps):
            put(k_start, k_end, k, spread(e, 0, maps, k))
    elif by_name("compose block"):
        strategy = "blocked"
        compose = {e["args"]["block"]: e for e in by_name("compose block")}
        resolve = {e["args"]["block"]: e for e in by_name("resolve block")}
        blocks = len(compose)
        for b in range(blocks):
            b0, b1 = maps * b // blocks, maps * (b + 1) // blocks
            for k in range(b0, b1):
                put(c_start, c_end, k, spread(compose[b], b0, b1, k))
                put(k_start, k_end, k, spread(resolve[b], b0, b1, k))
    elif by_name("compose blocks"):
        strategy = "gpu-blocked"
        compose, resolve = by_name("compose blocks")[0], by_name("resolve blocks")[0]
        blocks = compose["args"]["blocks"]
        longest = -(-maps // blocks)
        for b in range(blocks):
            b0, b1 = maps * b // blocks, maps * (b + 1) // blocks
            for k in range(b0, b1):
                put(c_start, c_end, k, spread(compose, b0, b1, k, longest))
                put(k_start, k_end, k, spread(resolve, b0, b1, k, longest))
    else:
        for k in range(maps):            # strategies without per-block events
            put(k_start, k_end, k, (w_begin, w_finish))
        if any(w[1] == CATEGORIES.index("gpu") for w in work):
            strategy = "gpu"

    # Sub-steps of the window stage. riptide's window_seconds counts only the scan,
    # which starts when the maps are built; the same split is shown here.
    steps = []
    for name in ("window maps", "serial chain", "compose block", "carry between blocks", "resolve block",
                 "compose blocks", "resolve blocks"):
        found = by_name(name)
        if found:
            steps.append([name, r(min(ms(e["ts"]) for e in found)), r(max(ms(e["ts"] + e["dur"]) for e in found))])
    steps.sort(key=lambda step: step[1])
    maps_end = steps[0][2] if steps and steps[0][0] == "window maps" else w_begin
    scan_blocks = len(by_name("compose block"))
    if by_name("compose blocks"):
        scan_blocks = by_name("compose blocks")[0]["args"]["blocks"]

    crc = [[r(ms(e["ts"])), r(ms(e["ts"] + e["dur"]))] for e in spans if e["cat"] == "verify"]
    out_bytes = sum(e["args"].get("bytes", 0) for e in spans if e["cat"] == "replace")
    return {
        "label": label, "strategy": strategy, "total": r(total), "chunks": n, "output_bytes": out_bytes,
        "threads": [names[t] for t in tids],
        "stages": [[s, r(ms(stage_spans[s]["ts"])), r(ms(stage_spans[s]["ts"] + stage_spans[s]["dur"]))]
                   for s in STAGES if s in stage_spans],
        "work": work, "steals": steals, "crc": crc,
        "window_steps": steps, "scan": [r(maps_end), r(w_finish)], "blocks": scan_blocks,
        "chunk": {"ds": column(d_start), "de": column(d_end), "cs": column(c_start), "ce": column(c_end),
                  "ks": column(k_start), "ke": column(k_end), "rs": column(r_start), "re": column(r_end)},
    }


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    runs = []
    for arg in sys.argv[2:]:
        path, _, label = arg.partition("=")
        runs.append(load(path, label or os.path.basename(path)))
    data = json.dumps({"categories": CATEGORIES, "runs": runs}, separators=(",", ":"))
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "pipeline_viz.html")) as f:
        page = f.read().replace("/*DATA*/null", data)
    with open(sys.argv[1], "w") as f:
        f.write(page)
    print(f"   wrote {sys.argv[1]} ({len(runs)} runs)")


if __name__ == "__main__":
    main()

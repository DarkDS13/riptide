"""Run the profiler and the M3 window-scan prototype on the Silesia corpus.

Stage "profile": per-file statistics for the 12 Silesia files, plus the block
list of the Silesia tarball (cached to JSON).
Stage "scan": two-stage decode of the tarball in chunks of C compressed bytes,
cut the way the base paper cuts them (a chunk starts at the first block at or
after each multiple of C), then window resolution by the sequential chain and
by a parallel prefix scan, both checked against zlib.
"""
import glob
import json
import math
import os
import sys
import time
import zlib

import numpy as np

import analyze
import profiler as pf
from window_scan import W, compose, decode_chunk

HERE = os.path.dirname(os.path.abspath(__file__))
SIL = os.path.join(HERE, "silesia")


def stage_profile():
    files = []
    for path in sorted(glob.glob(f"{SIL}/gz/*.gz")):
        p = pf.profile(path)
        assert p["bit_identical_to_zlib"], path
        s = analyze.summarise(p)
        files.append({k: s[k] for k in ("file", "compressed_KB", "uncompressed_KB", "ratio", "blocks",
                                        "block_types", "rho", "pct_dist_gt_4KB", "literal_fraction",
                                        "bit_identical")})
        print(f"  profiled {s['file']:14s} blocks={s['blocks']:5d} ratio={s['ratio']:.2f}", flush=True)
    tar = pf.profile(f"{SIL}/silesia.tar.gz")
    assert tar["bit_identical_to_zlib"]
    json.dump({"files": files,
               "tar": {"compressed_bytes": tar["compressed_bytes"],
                       "uncompressed_bytes": tar["uncompressed_bytes"],
                       "blocks": tar["blocks"]}},
              open(f"{SIL}/profile.json", "w"))
    print(f"  tarball: {len(tar['blocks'])} blocks, bit-identical")


def chunk_bounds(blocks, chunk_bytes):
    base, step = blocks[0]["start_bit"], chunk_bytes * 8
    end = blocks[-1]["start_bit"] + blocks[-1]["in_bits"]
    starts, cut = [base], base + step
    for b in blocks[1:]:
        if b["start_bit"] >= cut:
            starts.append(b["start_bit"])
            while cut <= b["start_bit"]:
                cut += step
    return list(zip(starts, starts[1:] + [end]))


def stage_scan(chunk_bytes):
    data = open(f"{SIL}/silesia.tar.gz", "rb").read()
    reference = zlib.decompress(data, wbits=31)
    blocks = json.load(open(f"{SIL}/profile.json"))["tar"]["blocks"]
    bounds = chunk_bounds(blocks, chunk_bytes)
    n = len(bounds)
    t0 = time.time()
    outs, maps = [], []
    for a, b in bounds:
        o, m = decode_chunk(data, a, b)
        outs.append(o)
        maps.append(m)
    decode_s = time.time() - t0

    seq = [maps[0]]
    for k in range(1, n):
        seq.append(compose(maps[k], seq[-1]))
    g, d, rounds = list(maps), 1, 0
    while d < n:
        g = [g[k] if k < d else compose(g[k], g[k - d]) for k in range(n)]
        d, rounds = d * 2, rounds + 1
    same = all(np.array_equal(x, y) for x, y in zip(seq, g))

    pos, identical = 0, True
    for k, o in enumerate(outs):            # resolve and compare chunk by chunk
        r = o if k == 0 else compose(o, g[k - 1])
        if (r >= 256).any() or r.astype(np.uint8).tobytes() != reference[pos:pos + len(r)]:
            identical = False
        pos += len(r)
    identical = identical and pos == len(reference)

    win_unres = [float((m >= 256).mean()) for m in maps[1:]]
    out_markers = [float((o >= 256).mean()) for o in outs[1:]]
    sixteen_bit = []
    for o in outs[1:]:                      # share of chunk before its last marker
        idx = np.flatnonzero(o >= 256)
        sixteen_bit.append((idx[-1] + 1) / len(o) if len(idx) else 0.0)
    total_out = len(reference)
    serial_share = (n - 1) * W / total_out  # serial chain vs all marker-resolution work
    amdahl = {P: round(1 / (serial_share + (1 - serial_share) / P), 1) for P in (16, 64, 128)}
    return {
        "chunk_KiB": chunk_bytes // 1024, "chunks": n,
        "mean_chunk_out_MiB": round(total_out / n / 2**20, 2),
        "sequential_steps": n - 1, "scan_rounds": rounds,
        "scan_equals_sequential": same, "bit_identical_to_zlib": identical,
        "windows_with_markers_pct": round(100 * sum(u > 0 for u in win_unres) / len(win_unres), 1),
        "window_unresolved_mean_pct": round(100 * float(np.mean(win_unres)), 1),
        "window_unresolved_max_pct": round(100 * float(np.max(win_unres)), 1),
        "chunk_output_markers_mean_pct": round(100 * float(np.mean(out_markers)), 2),
        "chunk_16bit_region_mean_pct": round(100 * float(np.mean(sixteen_bit)), 1),
        "serial_share_of_resolution_pct": round(100 * serial_share, 3),
        "amdahl_bound_resolution_phase_sequential_chain": amdahl,
        "python_decode_seconds": round(decode_s, 1),
    }


if __name__ == "__main__":
    if sys.argv[1] == "profile":
        stage_profile()
    else:
        res = [stage_scan(int(c) * 1024) for c in sys.argv[2:]]
        json.dump(res, open(f"{SIL}/scan.json", "w"), indent=1)
        print(json.dumps(res, indent=1))

"""M2 estimate: how much memory would storing unresolved bytes as runs save?

rapidgzip keeps a chunk in 16-bit form from its start until its markers stop
(here: up to the last marker, which favours rapidgzip). M2 keeps every byte at
8 bits and records each run of unresolved bytes that point to consecutive
window positions as one record (start, window offset, length).
"""
import json
import sys

import numpy as np

from silesia_run import SIL, chunk_bounds
from window_scan import W, decode_chunk

RECORD_BYTES = 12   # start (8) + window offset (2) + length (2)


def runs_of(out16):
    """Maximal runs of markers whose window indices increase by exactly 1."""
    is_m = out16 >= 256
    if not is_m.any():
        return 0, 0, 0
    idx = out16.astype(np.int64) - 256
    pos = np.flatnonzero(is_m)
    # a new run starts where the position or the window index is not the previous one + 1
    new_run = np.ones(len(pos), dtype=bool)
    new_run[1:] = (np.diff(pos) != 1) | (np.diff(idx[pos]) != 1)
    starts = np.flatnonzero(new_run)
    lengths = np.diff(np.append(starts, len(pos)))
    return len(starts), int((lengths == 1).sum()), int(pos[-1]) + 1


def estimate(chunk_bytes):
    data = open(f"{SIL}/silesia.tar.gz", "rb").read()
    blocks = json.load(open(f"{SIL}/profile.json"))["tar"]["blocks"]
    rg_mem = m2_mem = out_total = markers = runs = singles = 0
    for a, b in chunk_bounds(blocks, chunk_bytes):
        out16, _ = decode_chunk(data, a, b)
        n = len(out16)
        r, s, sixteen_bit_end = runs_of(out16)
        rg_mem += 2 * sixteen_bit_end + (n - sixteen_bit_end)   # 16-bit prefix, then 8-bit
        m2_mem += n + RECORD_BYTES * r                           # 8-bit everywhere + run records
        out_total += n
        markers += int((out16 >= 256).sum())
        runs += r
        singles += s
    return {
        "chunk_KiB": chunk_bytes // 1024,
        "output_MB": round(out_total / 1e6, 1),
        "unresolved_bytes_MB": round(markers / 1e6, 1),
        "runs": runs,
        "avg_run_length": round(markers / runs, 1) if runs else 0,
        "runs_of_length_1_pct": round(100 * singles / runs, 1) if runs else 0,
        "rapidgzip_MB": round(rg_mem / 1e6, 1),
        "m2_MB": round(m2_mem / 1e6, 1),
        "saving_pct": round(100 * (rg_mem - m2_mem) / rg_mem, 1),
    }


if __name__ == "__main__":
    res = [estimate(int(c) * 1024) for c in sys.argv[1:]]
    json.dump(res, open(f"{SIL}/m2_runs.json", "w"), indent=1)
    print(json.dumps(res, indent=1))

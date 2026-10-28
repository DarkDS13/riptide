"""Prototype of M3: resolving cross-chunk windows with a parallel prefix scan.

Each chunk is decoded independently from a known block boundary, starting with
an unknown 32 KiB window filled with markers (256 + j = "byte j of the previous
window"). The window at the end of chunk k is then a map f_k from the previous
window to the next. Composing such maps is associative, so the chain
W_k = f_k(W_{k-1}) -- which the base paper resolves sequentially -- can be
computed by a parallel prefix scan in ceil(log2 N) rounds.

Both the sequential chain and the scan are checked against zlib.
"""
import array
import json
import math
import sys
import zlib

import numpy as np

import profiler as pf

W = 32768
MARKERS = array.array("H", range(256, 256 + W))


def decode_chunk(data, start_bit, stop_bit):
    """Two-stage (first-stage) decode of the blocks in [start_bit, stop_bit)."""
    br = pf.BitReader(data, start_bit // 8)
    br.bits(start_bit % 8)
    out = array.array("H", MARKERS)          # unknown window, as markers
    while br.bitpos() < stop_bit:
        final, btype = br.bits(1), br.bits(2)
        if btype == 0:
            br.align()
            ln, _ = br.bits(16), br.bits(16)
            k = 0
            while br.c >= 8 and k < ln:
                out.append(br.bits(8))
                k += 1
            out.extend(data[br.p:br.p + ln - k])
            br.p += ln - k
        else:
            (lt, lm), (dt, dm) = (pf.FIXED_LIT, pf.FIXED_DIST) if btype == 1 else pf.read_dynamic_tables(br)
            while True:
                sym = pf.decode_sym(br, lt, lm)
                if sym < 256:
                    out.append(sym)
                elif sym == 256:
                    break
                else:
                    li = sym - 257
                    length = pf.LBASE[li] + (br.bits(pf.LEXT[li]) if pf.LEXT[li] else 0)
                    ds = pf.decode_sym(br, dt, dm)
                    dist = pf.DBASE[ds] + (br.bits(pf.DEXT[ds]) if pf.DEXT[ds] else 0)
                    s = len(out) - dist           # may land inside the marker window: copies markers
                    if dist >= length:
                        out.extend(out[s:s + length])
                    else:
                        out.extend((out[s:s + dist] * (length // dist + 1))[:length])
        if final:
            break
    body = np.frombuffer(out, dtype=np.uint16)
    return body[W:].copy(), body[-W:].copy()   # chunk output, window map f_k


def compose(later, earlier):
    """(later o earlier): resolve `later`'s markers through `earlier` (a gather)."""
    idx = np.where(later >= 256, later.astype(np.int32) - 256, 0)
    return np.where(later < 256, later, earlier[idx])


def run(path, n_chunks):
    data = open(path, "rb").read()
    prof = pf.profile(path)
    blocks = prof["blocks"]
    reference = zlib.decompress(data, wbits=31)
    # chunk boundaries at real block starts, spread evenly over the block list
    starts = [blocks[round(i * len(blocks) / n_chunks)]["start_bit"] for i in range(n_chunks)]
    stops = starts[1:] + [blocks[-1]["start_bit"] + blocks[-1]["in_bits"]]
    outs, maps = zip(*(decode_chunk(data, a, b) for a, b in zip(starts, stops)))
    marker_frac = [float((m >= 256).mean()) for m in maps]
    assert not (outs[0] >= 256).any(), "chunk 0 starts at the stream start"

    # sequential chain (base paper): N-1 dependent steps
    windows_seq = [maps[0]]
    for k in range(1, n_chunks):
        windows_seq.append(compose(maps[k], windows_seq[-1]))

    # parallel prefix scan (Hillis-Steele): ceil(log2 N) rounds, each round fully parallel
    g, rounds = list(maps), 0
    d = 1
    while d < n_chunks:
        g = [g[k] if k < d else compose(g[k], g[k - d]) for k in range(n_chunks)]
        d *= 2
        rounds += 1
    windows_scan = g

    same_windows = all(np.array_equal(a, b) for a, b in zip(windows_seq, windows_scan))
    resolved = [outs[0]] + [compose(outs[k], windows_scan[k - 1]) for k in range(1, n_chunks)]
    full = np.concatenate(resolved)
    no_markers_left = not (full >= 256).any()
    bit_identical = no_markers_left and full.astype(np.uint8).tobytes() == reference
    return {
        "file": path.split("/")[-1], "chunks": n_chunks,
        "sequential_steps": n_chunks - 1, "scan_rounds": rounds,
        "scan_equals_sequential": same_windows, "bit_identical_to_zlib": bit_identical,
        "window_marker_fraction_mean": round(float(np.mean(marker_frac[1:])), 3),
        "window_marker_fraction_max": round(float(np.max(marker_frac[1:])), 3),
    }


if __name__ == "__main__":
    results = [run(p, int(n)) for p, n in zip(sys.argv[1::2], sys.argv[2::2])]
    print(json.dumps(results, indent=1))

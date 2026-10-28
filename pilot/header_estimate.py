"""Stage 0 check: can a block's Huffman header predict how much it expands?

Huffman code lengths approximate symbol frequencies (a symbol with an L-bit
code has frequency ~2^-L), so the header alone gives an estimate of the literal
share, the average match length and the average bits per symbol, hence of the
expansion ratio rho. We compare that estimate with the measured rho of every
block in the Silesia tarball, and with real chunk outputs.
"""
import json
import statistics as st
import sys

import profiler as pf
from silesia_run import SIL, chunk_bounds


def read_code_lengths(br):
    """Read a dynamic block's literal/length and distance code lengths."""
    hlit, hdist, hclen = br.bits(5) + 257, br.bits(5) + 1, br.bits(4) + 4
    cl = [0] * 19
    for i in range(hclen):
        cl[pf.CL_ORDER[i]] = br.bits(3)
    ctab, cmax = pf.build_table(cl)
    lens = []
    while len(lens) < hlit + hdist:
        sym = pf.decode_sym(br, ctab, cmax)
        if sym < 16:
            lens.append(sym)
        elif sym == 16:
            lens += [lens[-1]] * (3 + br.bits(2))
        elif sym == 17:
            lens += [0] * (3 + br.bits(3))
        else:
            lens += [0] * (11 + br.bits(7))
    return lens[:hlit], lens[hlit:hlit + hdist]


def estimate_rho(lit_lens, dist_lens):
    """Expected output bytes per compressed byte, from code lengths alone."""
    w = {s: 2.0 ** -L for s, L in enumerate(lit_lens) if L}
    z = sum(w.values())
    p_lit = sum(v for s, v in w.items() if s < 256) / z
    len_syms = {s: v for s, v in w.items() if 257 <= s <= 285}
    p_len = sum(len_syms.values()) / z
    if p_len > 0:
        avg_len = sum(v * (pf.LBASE[s - 257] + (2 ** pf.LEXT[s - 257] - 1) / 2)
                      for s, v in len_syms.items()) / sum(len_syms.values())
    else:
        avg_len = 0.0
    bits_litlen = sum(v * (lit_lens[s] + (pf.LEXT[s - 257] if 257 <= s <= 285 else 0))
                      for s, v in w.items()) / z
    dw = {d: 2.0 ** -L for d, L in enumerate(dist_lens) if L and d < 30}
    bits_dist = (sum(v * (dist_lens[d] + pf.DEXT[d]) for d, v in dw.items()) / sum(dw.values())) if dw else 0.0
    bytes_per_symbol = p_lit + p_len * avg_len
    bits_per_symbol = bits_litlen + p_len * bits_dist
    return 8 * bytes_per_symbol / bits_per_symbol


def main():
    data = open(f"{SIL}/silesia.tar.gz", "rb").read()
    blocks = json.load(open(f"{SIL}/profile.json"))["tar"]["blocks"]
    est = {}
    errs = []
    for b in blocks:
        br = pf.BitReader(data, b["start_bit"] // 8)
        br.bits(b["start_bit"] % 8)
        br.bits(1)
        btype = br.bits(2)
        actual = b["out_bytes"] / (b["in_bits"] / 8)
        if btype == 0:
            est[b["start_bit"]] = 1.0
        elif btype == 2:
            e = estimate_rho(*read_code_lengths(br))
            est[b["start_bit"]] = e
            errs.append(abs(e - actual) / actual)
        else:
            est[b["start_bit"]] = None             # fixed table: no information
    # chunk-level prediction: sample the first dynamic header in each chunk
    results = {"blocks_checked": len(errs),
               "block_median_error_pct": round(100 * st.median(errs), 1),
               "block_within_20pct": round(100 * sum(e <= 0.2 for e in errs) / len(errs), 1)}
    for kib in (4096, 512):
        cerr = []
        for a, z in chunk_bounds(blocks, kib * 1024):
            inside = [b for b in blocks if a <= b["start_bit"] < z]
            sample = next((est[b["start_bit"]] for b in inside if est[b["start_bit"]]), None)
            if sample is None:
                continue
            predicted = sample * (z - a) / 8
            actual = sum(b["out_bytes"] for b in inside)
            cerr.append(abs(predicted - actual) / actual)
        results[f"chunk_{kib}KiB_median_error_pct"] = round(100 * st.median(cerr), 1)
        results[f"chunk_{kib}KiB_within_20pct"] = round(100 * sum(e <= 0.2 for e in cerr) / len(cerr), 1)
        results[f"chunk_{kib}KiB_count"] = len(cerr)
    json.dump(results, open(f"{SIL}/header_estimate.json", "w"), indent=1)
    print(json.dumps(results, indent=1))


if __name__ == "__main__":
    main()

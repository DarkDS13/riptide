"""Summarise profiler output and estimate load imbalance under partitioning.

Load imbalance is *estimated* from measured per-block statistics using a
stated cost model; it is not a runtime measurement of any decoder.
"""
import json
import statistics as st
import sys

# Cost model: decoding a Huffman symbol costs 1 unit; copying an output byte
# costs COPY units; stored blocks cost COPY per byte (memcpy only).
COPY = 0.1
P_VALUES = [4, 8, 16, 32]


def block_cost(b):
    if b["type"] == "stored":
        return COPY * b["out_bytes"]
    return (b["literals"] + b["matches"]) + COPY * b["out_bytes"]


def static_partition(blocks, P):
    """Equal split of the *compressed* stream; a block goes to the worker
    whose range contains its first bit (what a compressed-domain splitter does)."""
    total_bits = blocks[-1]["start_bit"] + blocks[-1]["in_bits"] - blocks[0]["start_bit"]
    base = blocks[0]["start_bit"]
    loads = [0.0] * P
    for b in blocks:
        w = min(P - 1, int((b["start_bit"] - base) * P / total_bits))
        loads[w] += block_cost(b)
    return loads


def balanced_partition(blocks, P):
    """Contiguous split equalising cost, at block granularity (upper bound for M1)."""
    costs = [block_cost(b) for b in blocks]
    target = sum(costs) / P
    loads, acc, w = [0.0] * P, 0.0, 0
    for c in costs:
        if acc + c / 2 > target * (w + 1) and w < P - 1:
            w += 1
        loads[w] += c
        acc += c
    return loads


def imbalance(loads):
    busy = [x for x in loads]
    mean = sum(busy) / len(busy)
    return max(busy) / mean if mean else float("nan")


def describe(xs):
    return {"mean": st.mean(xs), "std": st.pstdev(xs), "min": min(xs), "max": max(xs)}


def summarise(f):
    blocks = f["blocks"]
    rho = [b["out_bytes"] / (b["in_bits"] / 8) for b in blocks if b["in_bits"] > 0]
    in_kb = [b["in_bits"] / 8 / 1024 for b in blocks]
    types = {t: sum(1 for b in blocks if b["type"] == t) for t in ("stored", "fixed", "dynamic")}
    dists = [d for d, n in f["dist_hist"].items() for _ in range(n)]
    dists = [int(d) for d in dists]
    lits = sum(b["literals"] for b in blocks if b["type"] != "stored")
    matches = sum(b["matches"] for b in blocks)
    return {
        "file": f["file"],
        "compressed_KB": f["compressed_bytes"] / 1024,
        "uncompressed_KB": f["uncompressed_bytes"] / 1024,
        "ratio": f["uncompressed_bytes"] / f["compressed_bytes"],
        "blocks": len(blocks),
        "block_types": types,
        "block_in_KB": describe(in_kb),
        "rho": describe(rho),
        "match_dist_mean": st.mean(dists) if dists else 0,
        "match_dist_median": st.median(dists) if dists else 0,
        "pct_dist_gt_4KB": 100 * sum(d > 4096 for d in dists) / len(dists) if dists else 0,
        "literal_fraction": lits / (lits + matches) if lits + matches else 1.0,
        "bit_identical": f["bit_identical_to_zlib"],
        "lambda_static": {P: imbalance(static_partition(blocks, P)) for P in P_VALUES},
        "lambda_balanced": {P: imbalance(balanced_partition(blocks, P)) for P in P_VALUES},
    }


if __name__ == "__main__":
    data = json.load(open(sys.argv[1]))
    out = [summarise(f) for f in data]
    json.dump(out, open(sys.argv[2], "w"), indent=1)
    for s in out:
        print(f"\n== {s['file']}  ({s['blocks']} blocks, ratio {s['ratio']:.2f}, identical={s['bit_identical']})")
        print(f"   types {s['block_types']}")
        r = s["rho"]
        print(f"   rho  mean {r['mean']:.2f}  std {r['std']:.2f}  min {r['min']:.2f}  max {r['max']:.2f}")
        b = s["block_in_KB"]
        print(f"   block KB  mean {b['mean']:.1f}  std {b['std']:.1f}  min {b['min']:.1f}  max {b['max']:.1f}")
        print(f"   dist mean {s['match_dist_mean']:.0f}  median {s['match_dist_median']:.0f}  >4KB {s['pct_dist_gt_4KB']:.1f}%  literal frac {s['literal_fraction']:.2f}")
        print("   P       " + "  ".join(f"{P:>6}" for P in P_VALUES))
        print("   static  " + "  ".join(f"{s['lambda_static'][P]:6.2f}" for P in P_VALUES))
        print("   balanced" + "  ".join(f"{s['lambda_balanced'][P]:6.2f}" for P in P_VALUES))

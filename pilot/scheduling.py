"""Estimate load imbalance of chunking policies under a FIFO thread pool.

Chunk costs come from measured per-block statistics (see analyze.block_cost);
results are model estimates, not runtime measurements.
"""
import heapq
import json
import sys

import analyze


def fixed_compressed_chunks(blocks, n):
    """Chunks of equal *compressed* size, cut at block granularity."""
    base = blocks[0]["start_bit"]
    total = blocks[-1]["start_bit"] + blocks[-1]["in_bits"] - base
    chunks = [[] for _ in range(n)]
    for b in blocks:
        chunks[min(n - 1, int((b["start_bit"] - base) * n / total))].append(b)
    return [c for c in chunks if c]


def work_sized_chunks(blocks, n):
    """Chunks of equal *decode cost* (the M1 target), cut at block granularity."""
    costs = [analyze.block_cost(b) for b in blocks]
    target, acc, k = sum(costs) / n, 0.0, 0
    chunks = [[] for _ in range(n)]
    for b, c in zip(blocks, costs):
        if acc + c / 2 > target * (k + 1) and k < n - 1:
            k += 1
        chunks[k].append(b)
        acc += c
    return [c for c in chunks if c]


def fifo_lambda(chunks, workers):
    """Makespan of a FIFO thread pool divided by the ideal (perfect balance)."""
    costs = [sum(analyze.block_cost(b) for b in c) for c in chunks]
    free = [0.0] * workers
    for c in costs:
        heapq.heappush(free, heapq.heappop(free) + c)
    return max(free) / (sum(costs) / workers)


if __name__ == "__main__":
    data = {f["file"]: f for f in json.load(open(sys.argv[1]))}
    P, K = 8, (1, 2, 4, 8)
    mixed = data["mixed_archive.tar.gz"]["blocks"]
    result = {"P": P, "chunks_per_worker": list(K), "copy_cost": analyze.COPY,
              "mixed": {"fixed": [], "work": []}, "homogeneous": {}, "robustness_k4": {}}
    for k in K:
        result["mixed"]["fixed"].append(round(fifo_lambda(fixed_compressed_chunks(mixed, P * k), P), 2))
        result["mixed"]["work"].append(round(fifo_lambda(work_sized_chunks(mixed, P * k), P), 2))
    text = data["dict_text.gz"]["blocks"]
    result["homogeneous"] = {
        "fixed": round(fifo_lambda(fixed_compressed_chunks(text, P), P), 2),
        "work": round(fifo_lambda(work_sized_chunks(text, P), P), 2)}
    for c in (0.05, 0.1, 0.25, 0.5, 1.0):
        analyze.COPY = c
        result["robustness_k4"][str(c)] = [
            round(fifo_lambda(fixed_compressed_chunks(mixed, P * 4), P), 2),
            round(fifo_lambda(work_sized_chunks(mixed, P * 4), P), 2)]
    json.dump(result, open(sys.argv[2], "w"), indent=1)
    print(json.dumps(result, indent=1))

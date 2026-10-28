"""M1 inputs on Silesia: load-imbalance estimates and the block snapshot.

1. Load imbalance (lambda = slowest worker / mean) for compressed-size chunks
   versus work-sized chunks, under a FIFO thread pool, using measured block
   statistics and the cost model in analyze.py. This is an oracle model: it
   assumes every block's true cost is known, so it is an upper bound on M1.
2. Five block records around the nci -> ooffice boundary in the tarball.
"""
import json
import os

import analyze
import scheduling
from silesia_run import SIL

ORDER = ["dickens", "mozilla", "mr", "nci", "ooffice", "osdb", "reymont",
         "samba", "sao", "webster", "x-ray", "xml"]


def main():
    analyze.COPY = 0.1
    blocks = json.load(open(f"{SIL}/profile.json"))["tar"]["blocks"]

    lam = {"P8": {}, "P16": {}}
    for P, key in ((8, "P8"), (16, "P16")):
        for k in (1, 2, 4, 8):
            fixed = scheduling.fifo_lambda(scheduling.fixed_compressed_chunks(blocks, P * k), P)
            work = scheduling.fifo_lambda(scheduling.work_sized_chunks(blocks, P * k), P)
            lam[key][k] = (round(fixed, 2), round(work, 2))

    robust = {}
    for c in (0.05, 0.1, 0.25, 0.5, 1.0):
        analyze.COPY = c
        robust[str(c)] = (round(scheduling.fifo_lambda(scheduling.fixed_compressed_chunks(blocks, 32), 8), 2),
                          round(scheduling.fifo_lambda(scheduling.work_sized_chunks(blocks, 32), 8), 2))
    analyze.COPY = 0.1

    # decompressed offset where ooffice's data starts inside the tar archive
    off, starts = 0, {}
    for name in ORDER:
        size = os.path.getsize(f"{SIL}/files/{name}")
        starts[name] = off + 512                       # after the 512-byte tar header
        off += 512 + (size + 511) // 512 * 512
    boundary, pos, idx = starts["ooffice"], 0, None
    for i, b in enumerate(blocks):
        if pos <= boundary < pos + b["out_bytes"]:
            idx = i
        pos += b["out_bytes"]
    snapshot = [[i, b["type"], b["in_bits"], b["out_bytes"], b["literals"], b["matches"],
                 round(b["out_bytes"] / (b["in_bits"] / 8), 2)]
                for i, b in ((i, blocks[i]) for i in range(idx - 2, idx + 3))]

    result = {"lambda": lam, "robustness_P8_k4": robust, "snapshot": snapshot,
              "snapshot_boundary": "nci -> ooffice"}
    json.dump(result, open(f"{SIL}/derived.json", "w"), indent=1)
    print(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()

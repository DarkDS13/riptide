#!/usr/bin/env python3
"""Build the benchmark datasets.

Real data:
  silesia.tar.gz      the Silesia corpus as one gzipped tarball (scripts/get_silesia.sh)
  silesia_x5.tar.gz   five copies of the Silesia tarball, one gzip member (about 1 GB
                      uncompressed), as the base paper scales files by concatenation

Synthetic data (fixed seeds, so every run produces identical files):
  reads.fastq.gz      FASTQ records in the style of short-read sequencers: 150 bp reads,
                      binned quality scores (about 1 GB uncompressed)
  logs.ndjson.gz      structured JSON service logs with a small vocabulary; compresses
                      by more than 20x, the regime where the base paper's software
                      decodes chunks serially (about 1 GB uncompressed)
  mixed.tar.gz        a heterogeneous archive interleaving Silesia files, random data
                      and log excerpts, so expansion varies strongly inside one stream

Usage: bench/datasets.py OUTPUT_DIR [--scale S]
"""

import argparse
import gzip
import io
import os
import subprocess
import sys
import tarfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SILESIA = os.path.join(ROOT, "pilot", "silesia")
MiB = 1 << 20


def gzip_file(source, destination, level=6):
    """Compress with the system gzip (zlib's deflate), as most real .gz files are made."""
    with open(destination, "wb") as out:
        subprocess.run(["gzip", f"-{level}", "-c", "-n", source], stdout=out, check=True)
    os.remove(source)


def silesia_tar():
    path = os.path.join(SILESIA, "silesia.tar.gz")
    if not os.path.exists(path):
        subprocess.run([os.path.join(ROOT, "scripts", "get_silesia.sh")], check=True)
    return path


def make_silesia_x5(out_dir):
    raw = gzip.decompress(open(silesia_tar(), "rb").read())
    tmp = os.path.join(out_dir, "silesia_x5.tar")
    with open(tmp, "wb") as f:
        for _ in range(5):
            f.write(raw)
    gzip_file(tmp, tmp + ".gz")


def make_fastq(out_dir, size, seed=1):
    """Short-read FASTQ: random bases with a slight GC bias, binned qualities."""
    rng = np.random.default_rng(seed)
    read_length = 150
    bases = np.frombuffer(b"ACGTN", dtype=np.uint8)
    base_p = [0.29, 0.21, 0.21, 0.29, 0.0]
    quality_values = np.frombuffer(b"F:,#", dtype=np.uint8)  # binned Phred+33 scores
    quality_p = [0.86, 0.09, 0.04, 0.01]
    tmp = os.path.join(out_dir, "reads.fastq")
    written, record = 0, 0
    batch = 20000
    with open(tmp, "wb") as f:
        while written < size:
            seq = bases[rng.choice(5, size=(batch, read_length), p=base_p)]
            qual = quality_values[rng.choice(4, size=(batch, read_length), p=quality_p)]
            # A few reads with a low-quality tail, as in real runs.
            tails = rng.random(batch) < 0.05
            qual[tails, -20:] = ord("#")
            seq[tails, -20:] = ord("N")
            out = io.BytesIO()
            for i in range(batch):
                record += 1
                header = f"@SRR22403185.{record} {record} length={read_length}".encode()
                out.write(header + b"\n" + seq[i].tobytes() + b"\n+\n" + qual[i].tobytes() + b"\n")
            chunk = out.getvalue()
            f.write(chunk)
            written += len(chunk)
    gzip_file(tmp, tmp + ".gz")


def log_lines(rng, count, start_ms):
    services = ["api", "auth", "billing", "search", "worker"]
    routes = ["/v1/items", "/v1/items/{id}", "/v1/users/{id}", "/v1/orders", "/health", "/v1/search"]
    levels = ["INFO"] * 18 + ["WARN", "ERROR"]
    status = [200] * 30 + [201, 204, 304, 400, 404, 500]
    s = rng.integers(0, len(services), count)
    r = rng.integers(0, len(routes), count)
    lv = rng.integers(0, len(levels), count)
    st = rng.integers(0, len(status), count)
    latency = rng.integers(1, 40, count)
    user = rng.integers(0, 50, count)
    lines = []
    for i in range(count):
        ms = start_ms + i * 7
        lines.append(
            '{"ts":"2026-10-29T%02d:%02d:%02d.%03dZ","level":"%s","service":"%s","route":"%s",'
            '"status":%d,"latency_ms":%d,"user":"u%02d"}\n'
            % ((ms // 3600000) % 24, (ms // 60000) % 60, (ms // 1000) % 60, ms % 1000,
               levels[lv[i]], services[s[i]], routes[r[i]], status[st[i]], latency[i], user[i]))
    return "".join(lines).encode()


def make_logs(out_dir, size, seed=2):
    rng = np.random.default_rng(seed)
    tmp = os.path.join(out_dir, "logs.ndjson")
    written, start = 0, 0
    with open(tmp, "wb") as f:
        while written < size:
            chunk = log_lines(rng, 50000, start)
            start += 50000 * 7
            f.write(chunk)
            written += len(chunk)
    gzip_file(tmp, tmp + ".gz")


def make_mixed(out_dir, seed=3):
    """Interleave Silesia members, incompressible data and highly compressible logs."""
    rng = np.random.default_rng(seed)
    silesia = tarfile.open(silesia_tar(), "r:gz")
    tmp = os.path.join(out_dir, "mixed.tar")
    with tarfile.open(tmp, "w", format=tarfile.USTAR_FORMAT) as out:
        for index, member in enumerate(silesia.getmembers()):
            data = silesia.extractfile(member).read()
            for name, payload in ((member.name, data),
                                  (f"random_{index:02d}.bin", rng.bytes(4 * MiB)),
                                  (f"logs_{index:02d}.ndjson", log_lines(rng, 120000, index * 10**7))):
                info = tarfile.TarInfo(name)
                info.size = len(payload)
                out.addfile(info, io.BytesIO(payload))
    gzip_file(tmp, tmp + ".gz")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output_dir")
    parser.add_argument("--scale", type=float, default=1.0, help="size multiplier for synthetic data")
    args = parser.parse_args()
    os.makedirs(args.output_dir, exist_ok=True)

    steps = [
        ("silesia_x5.tar.gz", lambda: make_silesia_x5(args.output_dir)),
        ("reads.fastq.gz", lambda: make_fastq(args.output_dir, int(1024 * MiB * args.scale))),
        ("logs.ndjson.gz", lambda: make_logs(args.output_dir, int(1024 * MiB * args.scale))),
        ("mixed.tar.gz", lambda: make_mixed(args.output_dir)),
    ]
    for name, build in steps:
        path = os.path.join(args.output_dir, name)
        if os.path.exists(path):
            print(f"{name}: exists")
            continue
        print(f"{name}: building...", flush=True)
        build()
        print(f"{name}: {os.path.getsize(path) / MiB:.1f} MiB")
    link = os.path.join(args.output_dir, "silesia.tar.gz")
    if not os.path.exists(link):
        os.symlink(silesia_tar(), link)


if __name__ == "__main__":
    sys.exit(main())

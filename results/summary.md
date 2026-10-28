# Benchmark summary

Correctness: 1200 of 1200 measured runs byte-identical to zlib.

## E1 Scaling (4 MiB chunks, blocked windows)

| File | Ratio | zlib MB/s | Seq. MB/s | Threads | MB/s | Speedup vs 1 thread | Efficiency | vs zlib | λ |
|---|---|---|---|---|---|---|---|---|---|
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 1 | 1854 | 1.00 | 1.00 | 0.29 | 1.00 |
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 2 | 2517 | 1.36 | 0.68 | 0.40 | 1.04 |
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 4 | 3429 | 1.85 | 0.46 | 0.54 | 1.12 |
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 6 | 3968 | 2.14 | 0.36 | 0.62 | 1.21 |
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 8 | 4321 | 2.33 | 0.29 | 0.68 | 1.40 |
| logs.ndjson.gz | 23.09 | 6359 | 2761 | 10 | 5096 | 2.75 | 0.27 | 0.80 | 1.03 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 1 | 1577 | 1.00 | 1.00 | 0.73 | 1.00 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 2 | 2184 | 1.38 | 0.69 | 1.01 | 1.05 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 4 | 2901 | 1.84 | 0.46 | 1.34 | 1.12 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 6 | 3060 | 1.94 | 0.32 | 1.41 | 1.33 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 8 | 3565 | 2.26 | 0.28 | 1.65 | 1.23 |
| mixed.tar.gz | 3.51 | 2164 | 1159 | 10 | 3551 | 2.25 | 0.23 | 1.64 | 1.56 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 1 | 759 | 1.00 | 1.00 | 0.46 | 1.00 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 2 | 1087 | 1.43 | 0.72 | 0.66 | 1.01 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 4 | 1530 | 2.01 | 0.50 | 0.93 | 1.04 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 6 | 1944 | 2.56 | 0.43 | 1.19 | 1.02 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 8 | 2340 | 3.08 | 0.39 | 1.43 | 1.02 |
| reads.fastq.gz | 4.11 | 1638 | 827 | 10 | 2502 | 3.29 | 0.33 | 1.53 | 1.05 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 1 | 894 | 1.00 | 1.00 | 0.73 | 1.00 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 2 | 1208 | 1.35 | 0.68 | 0.99 | 1.09 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 4 | 1834 | 2.05 | 0.51 | 1.50 | 1.14 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 6 | 2187 | 2.45 | 0.41 | 1.79 | 1.23 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 8 | 2499 | 2.80 | 0.35 | 2.05 | 1.32 |
| silesia.tar.gz | 3.11 | 1220 | 688 | 10 | 2562 | 2.87 | 0.29 | 2.10 | 1.42 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 1 | 916 | 1.00 | 1.00 | 0.76 | 1.00 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 2 | 1312 | 1.43 | 0.72 | 1.09 | 1.01 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 4 | 1905 | 2.08 | 0.52 | 1.58 | 1.02 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 6 | 2353 | 2.57 | 0.43 | 1.95 | 1.04 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 8 | 2763 | 3.02 | 0.38 | 2.29 | 1.05 |
| silesia_x5.tar.gz | 3.11 | 1206 | 664 | 10 | 2944 | 3.22 | 0.32 | 2.44 | 1.06 |

## E2 Window resolution across chunk sizes (10 threads)

| File | Chunk | Chunks | Strategy | Window ms | Rounds | Window share of total | Total ms |
|---|---|---|---|---|---|---|---|
| silesia.tar.gz | 64 KiB | 1041 | serial | 46.15 | 1040 | 21.4% | 216.2 |
| silesia.tar.gz | 64 KiB | 1041 | hillis-steele | 34.67 | 12 | 15.7% | 217.9 |
| silesia.tar.gz | 64 KiB | 1041 | blelloch | 24.06 | 24 | 11.5% | 208.7 |
| silesia.tar.gz | 64 KiB | 1041 | blocked | 17.12 | 217 | 8.5% | 201.4 |
| silesia.tar.gz | 128 KiB | 521 | serial | 24.61 | 520 | 14.6% | 168.6 |
| silesia.tar.gz | 128 KiB | 521 | hillis-steele | 14.42 | 11 | 8.7% | 166.5 |
| silesia.tar.gz | 128 KiB | 521 | blelloch | 12.40 | 22 | 7.4% | 167.6 |
| silesia.tar.gz | 128 KiB | 521 | blocked | 9.22 | 113 | 5.6% | 163.6 |
| silesia.tar.gz | 256 KiB | 261 | serial | 12.00 | 260 | 8.1% | 146.2 |
| silesia.tar.gz | 256 KiB | 261 | hillis-steele | 6.03 | 10 | 4.2% | 142.9 |
| silesia.tar.gz | 256 KiB | 261 | blelloch | 5.68 | 20 | 3.8% | 149.9 |
| silesia.tar.gz | 256 KiB | 261 | blocked | 4.18 | 61 | 2.9% | 146.6 |
| silesia.tar.gz | 512 KiB | 131 | serial | 4.88 | 130 | 3.6% | 136.1 |
| silesia.tar.gz | 512 KiB | 131 | hillis-steele | 2.84 | 9 | 2.1% | 134.0 |
| silesia.tar.gz | 512 KiB | 131 | blelloch | 2.63 | 18 | 2.0% | 134.3 |
| silesia.tar.gz | 512 KiB | 131 | blocked | 1.77 | 35 | 1.3% | 132.5 |
| silesia.tar.gz | 1024 KiB | 66 | serial | 1.95 | 65 | 1.6% | 122.4 |
| silesia.tar.gz | 1024 KiB | 66 | hillis-steele | 1.50 | 8 | 1.2% | 123.5 |
| silesia.tar.gz | 1024 KiB | 66 | blelloch | 1.74 | 16 | 1.3% | 132.2 |
| silesia.tar.gz | 1024 KiB | 66 | blocked | 1.00 | 23 | 0.7% | 134.8 |
| silesia.tar.gz | 2048 KiB | 33 | serial | 0.75 | 32 | 0.6% | 119.1 |
| silesia.tar.gz | 2048 KiB | 33 | hillis-steele | 0.73 | 6 | 0.6% | 117.4 |
| silesia.tar.gz | 2048 KiB | 33 | blelloch | 0.76 | 12 | 0.6% | 121.4 |
| silesia.tar.gz | 2048 KiB | 33 | blocked | 0.52 | 17 | 0.4% | 120.3 |
| silesia.tar.gz | 4096 KiB | 17 | serial | 0.30 | 16 | 0.3% | 114.5 |
| silesia.tar.gz | 4096 KiB | 17 | hillis-steele | 0.34 | 5 | 0.3% | 123.0 |
| silesia.tar.gz | 4096 KiB | 17 | blelloch | 0.39 | 10 | 0.3% | 128.6 |
| silesia.tar.gz | 4096 KiB | 17 | blocked | 0.31 | 13 | 0.2% | 126.1 |
| silesia_x5.tar.gz | 64 KiB | 5205 | serial | 269.83 | 5204 | 19.9% | 1354.5 |
| silesia_x5.tar.gz | 64 KiB | 5205 | hillis-steele | 186.36 | 14 | 15.0% | 1234.7 |
| silesia_x5.tar.gz | 64 KiB | 5205 | blelloch | 137.62 | 28 | 11.6% | 1190.3 |
| silesia_x5.tar.gz | 64 KiB | 5205 | blocked | 78.19 | 1051 | 6.9% | 1137.8 |
| silesia_x5.tar.gz | 128 KiB | 2603 | serial | 143.55 | 2602 | 15.1% | 949.2 |
| silesia_x5.tar.gz | 128 KiB | 2603 | hillis-steele | 75.04 | 13 | 8.0% | 936.5 |
| silesia_x5.tar.gz | 128 KiB | 2603 | blelloch | 61.30 | 26 | 6.6% | 932.6 |
| silesia_x5.tar.gz | 128 KiB | 2603 | blocked | 37.95 | 531 | 4.2% | 899.3 |
| silesia_x5.tar.gz | 256 KiB | 1302 | serial | 66.22 | 1301 | 8.2% | 806.8 |
| silesia_x5.tar.gz | 256 KiB | 1302 | hillis-steele | 29.19 | 12 | 3.7% | 788.8 |
| silesia_x5.tar.gz | 256 KiB | 1302 | blelloch | 26.03 | 24 | 3.3% | 786.1 |
| silesia_x5.tar.gz | 256 KiB | 1302 | blocked | 16.89 | 271 | 2.2% | 785.6 |
| silesia_x5.tar.gz | 512 KiB | 651 | serial | 26.68 | 650 | 3.9% | 692.1 |
| silesia_x5.tar.gz | 512 KiB | 651 | hillis-steele | 11.30 | 11 | 1.6% | 689.3 |
| silesia_x5.tar.gz | 512 KiB | 651 | blelloch | 11.43 | 22 | 1.7% | 682.9 |
| silesia_x5.tar.gz | 512 KiB | 651 | blocked | 7.28 | 139 | 1.1% | 677.4 |
| silesia_x5.tar.gz | 1024 KiB | 326 | serial | 10.26 | 325 | 1.7% | 615.5 |
| silesia_x5.tar.gz | 1024 KiB | 326 | hillis-steele | 4.69 | 10 | 0.8% | 617.1 |
| silesia_x5.tar.gz | 1024 KiB | 326 | blelloch | 4.81 | 20 | 0.8% | 615.4 |
| silesia_x5.tar.gz | 1024 KiB | 326 | blocked | 3.20 | 75 | 0.5% | 608.4 |
| silesia_x5.tar.gz | 2048 KiB | 163 | serial | 4.12 | 162 | 0.7% | 557.9 |
| silesia_x5.tar.gz | 2048 KiB | 163 | hillis-steele | 2.23 | 9 | 0.4% | 547.7 |
| silesia_x5.tar.gz | 2048 KiB | 163 | blelloch | 2.25 | 18 | 0.4% | 548.6 |
| silesia_x5.tar.gz | 2048 KiB | 163 | blocked | 1.42 | 43 | 0.3% | 551.6 |
| silesia_x5.tar.gz | 4096 KiB | 82 | serial | 1.69 | 81 | 0.3% | 496.3 |
| silesia_x5.tar.gz | 4096 KiB | 82 | hillis-steele | 1.36 | 8 | 0.3% | 500.1 |
| silesia_x5.tar.gz | 4096 KiB | 82 | blelloch | 1.33 | 16 | 0.3% | 496.3 |
| silesia_x5.tar.gz | 4096 KiB | 82 | blocked | 0.61 | 27 | 0.1% | 501.3 |

## E2 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| silesia.tar.gz 64 KiB window step: hillis-steele vs serial | 46.150 ms | 34.674 ms | 1.33x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: hillis-steele vs serial | 216.151 ms | 217.897 ms | 0.99x | 1 | no |
| silesia.tar.gz 64 KiB window step: blelloch vs serial | 46.150 ms | 24.058 ms | 1.92x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: blelloch vs serial | 216.151 ms | 208.711 ms | 1.04x | 0.28 | no |
| silesia.tar.gz 64 KiB window step: blocked vs serial | 46.150 ms | 17.119 ms | 2.70x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: blocked vs serial | 216.151 ms | 201.394 ms | 1.07x | 0.003 | yes |
| silesia.tar.gz 128 KiB window step: hillis-steele vs serial | 24.609 ms | 14.419 ms | 1.71x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: hillis-steele vs serial | 168.584 ms | 166.506 ms | 1.01x | 1 | no |
| silesia.tar.gz 128 KiB window step: blelloch vs serial | 24.609 ms | 12.403 ms | 1.98x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: blelloch vs serial | 168.584 ms | 167.575 ms | 1.01x | 1 | no |
| silesia.tar.gz 128 KiB window step: blocked vs serial | 24.609 ms | 9.222 ms | 2.67x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: blocked vs serial | 168.584 ms | 163.595 ms | 1.03x | 0.037 | yes |
| silesia.tar.gz 256 KiB window step: hillis-steele vs serial | 12.004 ms | 6.025 ms | 1.99x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: hillis-steele vs serial | 146.196 ms | 142.852 ms | 1.02x | 1 | no |
| silesia.tar.gz 256 KiB window step: blelloch vs serial | 12.004 ms | 5.676 ms | 2.11x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: blelloch vs serial | 146.196 ms | 149.889 ms | 0.98x | 0.52 | no |
| silesia.tar.gz 256 KiB window step: blocked vs serial | 12.004 ms | 4.181 ms | 2.87x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: blocked vs serial | 146.196 ms | 146.595 ms | 1.00x | 1 | no |
| silesia.tar.gz 512 KiB window step: hillis-steele vs serial | 4.877 ms | 2.838 ms | 1.72x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: hillis-steele vs serial | 136.076 ms | 134.043 ms | 1.02x | 1 | no |
| silesia.tar.gz 512 KiB window step: blelloch vs serial | 4.877 ms | 2.631 ms | 1.85x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: blelloch vs serial | 136.076 ms | 134.282 ms | 1.01x | 1 | no |
| silesia.tar.gz 512 KiB window step: blocked vs serial | 4.877 ms | 1.773 ms | 2.75x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: blocked vs serial | 136.076 ms | 132.454 ms | 1.03x | 0.43 | no |
| silesia.tar.gz 1024 KiB window step: hillis-steele vs serial | 1.948 ms | 1.504 ms | 1.30x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: hillis-steele vs serial | 122.406 ms | 123.544 ms | 0.99x | 1 | no |
| silesia.tar.gz 1024 KiB window step: blelloch vs serial | 1.948 ms | 1.740 ms | 1.12x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: blelloch vs serial | 122.406 ms | 132.242 ms | 0.93x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB window step: blocked vs serial | 1.948 ms | 0.997 ms | 1.95x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: blocked vs serial | 122.406 ms | 134.771 ms | 0.91x | 0.00091 | yes |
| silesia.tar.gz 2048 KiB window step: hillis-steele vs serial | 0.749 ms | 0.734 ms | 1.02x | 0.63 | no |
| silesia.tar.gz 2048 KiB total: hillis-steele vs serial | 119.091 ms | 117.422 ms | 1.01x | 1 | no |
| silesia.tar.gz 2048 KiB window step: blelloch vs serial | 0.749 ms | 0.763 ms | 0.98x | 1 | no |
| silesia.tar.gz 2048 KiB total: blelloch vs serial | 119.091 ms | 121.363 ms | 0.98x | 1 | no |
| silesia.tar.gz 2048 KiB window step: blocked vs serial | 0.749 ms | 0.519 ms | 1.44x | 0.00091 | yes |
| silesia.tar.gz 2048 KiB total: blocked vs serial | 119.091 ms | 120.291 ms | 0.99x | 1 | no |
| silesia.tar.gz 4096 KiB window step: hillis-steele vs serial | 0.302 ms | 0.335 ms | 0.90x | 0.00091 | yes |
| silesia.tar.gz 4096 KiB total: hillis-steele vs serial | 114.516 ms | 123.017 ms | 0.93x | 0.28 | no |
| silesia.tar.gz 4096 KiB window step: blelloch vs serial | 0.302 ms | 0.385 ms | 0.78x | 0.00091 | yes |
| silesia.tar.gz 4096 KiB total: blelloch vs serial | 114.516 ms | 128.618 ms | 0.89x | 0.027 | yes |
| silesia.tar.gz 4096 KiB window step: blocked vs serial | 0.302 ms | 0.309 ms | 0.98x | 1 | no |
| silesia.tar.gz 4096 KiB total: blocked vs serial | 114.516 ms | 126.084 ms | 0.91x | 0.071 | no |
| silesia_x5.tar.gz 64 KiB window step: hillis-steele vs serial | 269.827 ms | 186.363 ms | 1.45x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: hillis-steele vs serial | 1354.470 ms | 1234.695 ms | 1.10x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB window step: blelloch vs serial | 269.827 ms | 137.619 ms | 1.96x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: blelloch vs serial | 1354.470 ms | 1190.285 ms | 1.14x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB window step: blocked vs serial | 269.827 ms | 78.186 ms | 3.45x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: blocked vs serial | 1354.470 ms | 1137.845 ms | 1.19x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB window step: hillis-steele vs serial | 143.555 ms | 75.037 ms | 1.91x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: hillis-steele vs serial | 949.234 ms | 936.525 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 128 KiB window step: blelloch vs serial | 143.555 ms | 61.303 ms | 2.34x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: blelloch vs serial | 949.234 ms | 932.569 ms | 1.02x | 0.12 | no |
| silesia_x5.tar.gz 128 KiB window step: blocked vs serial | 143.555 ms | 37.952 ms | 3.78x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: blocked vs serial | 949.234 ms | 899.253 ms | 1.06x | 0.003 | yes |
| silesia_x5.tar.gz 256 KiB window step: hillis-steele vs serial | 66.217 ms | 29.190 ms | 2.27x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: hillis-steele vs serial | 806.787 ms | 788.841 ms | 1.02x | 0.027 | yes |
| silesia_x5.tar.gz 256 KiB window step: blelloch vs serial | 66.217 ms | 26.032 ms | 2.54x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: blelloch vs serial | 806.787 ms | 786.115 ms | 1.03x | 0.095 | no |
| silesia_x5.tar.gz 256 KiB window step: blocked vs serial | 66.217 ms | 16.887 ms | 3.92x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: blocked vs serial | 806.787 ms | 785.631 ms | 1.03x | 0.0049 | yes |
| silesia_x5.tar.gz 512 KiB window step: hillis-steele vs serial | 26.682 ms | 11.303 ms | 2.36x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: hillis-steele vs serial | 692.087 ms | 689.336 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 512 KiB window step: blelloch vs serial | 26.682 ms | 11.429 ms | 2.33x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: blelloch vs serial | 692.087 ms | 682.920 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 512 KiB window step: blocked vs serial | 26.682 ms | 7.281 ms | 3.66x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: blocked vs serial | 692.087 ms | 677.434 ms | 1.02x | 0.75 | no |
| silesia_x5.tar.gz 1024 KiB window step: hillis-steele vs serial | 10.258 ms | 4.692 ms | 2.19x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: hillis-steele vs serial | 615.546 ms | 617.142 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 1024 KiB window step: blelloch vs serial | 10.258 ms | 4.812 ms | 2.13x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: blelloch vs serial | 615.546 ms | 615.382 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 1024 KiB window step: blocked vs serial | 10.258 ms | 3.200 ms | 3.21x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: blocked vs serial | 615.546 ms | 608.396 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: hillis-steele vs serial | 4.116 ms | 2.229 ms | 1.85x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: hillis-steele vs serial | 557.920 ms | 547.725 ms | 1.02x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: blelloch vs serial | 4.116 ms | 2.248 ms | 1.83x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: blelloch vs serial | 557.920 ms | 548.581 ms | 1.02x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: blocked vs serial | 4.116 ms | 1.419 ms | 2.90x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: blocked vs serial | 557.920 ms | 551.616 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: hillis-steele vs serial | 1.694 ms | 1.365 ms | 1.24x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: hillis-steele vs serial | 496.311 ms | 500.084 ms | 0.99x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: blelloch vs serial | 1.694 ms | 1.332 ms | 1.27x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: blelloch vs serial | 496.311 ms | 496.261 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: blocked vs serial | 1.694 ms | 0.606 ms | 2.79x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: blocked vs serial | 496.311 ms | 501.324 ms | 0.99x | 1 | no |

## E3 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| 1 threads window step: blocked vs serial | 190.971 ms | 373.917 ms | 0.51x | 6.5e-05 | yes |
| 2 threads window step: blocked vs serial | 211.022 ms | 229.989 ms | 0.92x | 0.0015 | yes |
| 4 threads window step: blocked vs serial | 233.549 ms | 145.133 ms | 1.61x | 6.5e-05 | yes |
| 6 threads window step: blocked vs serial | 246.795 ms | 107.026 ms | 2.31x | 6.5e-05 | yes |
| 8 threads window step: blocked vs serial | 258.098 ms | 88.096 ms | 2.93x | 6.5e-05 | yes |
| 10 threads window step: blocked vs serial | 279.137 ms | 88.078 ms | 3.17x | 6.5e-05 | yes |

## E4 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB total: fixed/split vs fixed/serial | 272.068 ms | 311.774 ms | 0.87x | 1 | no |
| logs.ndjson.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 272.068 ms | 307.041 ms | 0.89x | 1 | no |
| logs.ndjson.gz 4096 KiB total: work-balanced/split vs fixed/serial | 272.068 ms | 308.917 ms | 0.88x | 1 | no |
| mixed.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 151.722 ms | 158.618 ms | 0.96x | 0.22 | no |
| mixed.tar.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 151.722 ms | 238.672 ms | 0.64x | 9.7e-05 | yes |
| mixed.tar.gz 4096 KiB total: work-balanced/split vs fixed/serial | 151.722 ms | 239.389 ms | 0.63x | 9.7e-05 | yes |
| silesia_x5.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 532.005 ms | 534.448 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 532.005 ms | 584.522 ms | 0.91x | 9.7e-05 | yes |
| silesia_x5.tar.gz 4096 KiB total: work-balanced/split vs fixed/serial | 532.005 ms | 600.949 ms | 0.89x | 9.7e-05 | yes |

## E4 budget comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB total: fixed/split vs fixed/serial | 302.548 ms | 318.877 ms | 0.95x | 0.012 | yes |
| logs.ndjson.gz 16384 KiB total: fixed/split vs fixed/serial | 453.276 ms | 317.701 ms | 1.43x | 0.006 | yes |
| silesia_x5.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 576.175 ms | 565.861 ms | 1.02x | 0.38 | no |
| silesia_x5.tar.gz 16384 KiB total: fixed/split vs fixed/serial | 451.141 ms | 446.199 ms | 1.01x | 0.38 | no |

## E3 Window resolution across threads (64 KiB chunks)

| Threads | Serial window ms | Blocked window ms | Serial total ms | Blocked total ms |
|---|---|---|---|---|
| 1 | 190.97 | 373.92 | 3045.9 | 3213.4 |
| 2 | 211.02 | 229.99 | 2487.8 | 2507.5 |
| 4 | 233.55 | 145.13 | 1868.7 | 1831.2 |
| 6 | 246.79 | 107.03 | 1501.9 | 1459.1 |
| 8 | 258.10 | 88.10 | 1294.4 | 1210.9 |
| 10 | 279.14 | 88.08 | 1305.8 | 1194.6 |

## E4 Chunking and overflow policies (10 threads, 20x output budget)

| File | Chunking/overflow | Total ms | Decode ms | λ | Overflow splits | Chain |
|---|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB | fixed/serial | 272.1 | 107.0 | 1.12 | 11 | 23 |
| logs.ndjson.gz 4096 KiB | fixed/split | 311.8 | 126.0 | 1.03 | 11 | 91 |
| logs.ndjson.gz 4096 KiB | work-balanced/serial | 307.0 | 142.6 | 1.40 | 12 | 24 |
| logs.ndjson.gz 4096 KiB | work-balanced/split | 308.9 | 140.9 | 1.36 | 12 | 46 |
| mixed.tar.gz 4096 KiB | fixed/serial | 151.7 | 116.6 | 1.56 | 0 | 31 |
| mixed.tar.gz 4096 KiB | fixed/split | 158.6 | 123.4 | 1.53 | 0 | 31 |
| mixed.tar.gz 4096 KiB | work-balanced/serial | 238.7 | 118.2 | 1.35 | 0 | 31 |
| mixed.tar.gz 4096 KiB | work-balanced/split | 239.4 | 118.2 | 1.35 | 0 | 31 |
| silesia_x5.tar.gz 4096 KiB | fixed/serial | 532.0 | 415.2 | 1.06 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | fixed/split | 534.4 | 420.4 | 1.08 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | work-balanced/serial | 584.5 | 427.2 | 1.10 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | work-balanced/split | 600.9 | 439.2 | 1.11 | 0 | 82 |

## E4 Overflow policies under a 4x output budget (10 threads)

| File | Chunking/overflow | Total ms | Decode ms | λ | Overflow splits | Chain |
|---|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB | fixed/serial | 302.5 | 130.2 | 1.08 | 55 | 67 |
| logs.ndjson.gz 4096 KiB | fixed/split | 318.9 | 138.3 | 1.04 | 11 | 122 |
| logs.ndjson.gz 16384 KiB | fixed/serial | 453.3 | 306.6 | 5.57 | 14 | 17 |
| logs.ndjson.gz 16384 KiB | fixed/split | 317.7 | 147.5 | 1.29 | 3 | 33 |
| silesia_x5.tar.gz 4096 KiB | fixed/serial | 576.2 | 445.8 | 1.06 | 7 | 89 |
| silesia_x5.tar.gz 4096 KiB | fixed/split | 565.9 | 441.1 | 1.07 | 6 | 141 |
| silesia_x5.tar.gz 16384 KiB | fixed/serial | 451.1 | 387.5 | 1.14 | 5 | 26 |
| silesia_x5.tar.gz 16384 KiB | fixed/split | 446.2 | 384.9 | 1.13 | 5 | 62 |

## E5 Peak memory (10 threads)

| File | Chunk | Overflow | Peak RSS MB | RSS / output |
|---|---|---|---|---|
| logs.ndjson.gz | 4M | serial | 3208 | 2.98 |
| logs.ndjson.gz | 4M | split | 2864 | 2.66 |
| logs.ndjson.gz | 512K | serial | 2108 | 1.96 |
| logs.ndjson.gz | 512K | split | 2078 | 1.93 |
| silesia_x5.tar.gz | 4M | serial | 2323 | 2.19 |
| silesia_x5.tar.gz | 4M | split | 2315 | 2.18 |
| silesia_x5.tar.gz | 512K | serial | 1932 | 1.82 |
| silesia_x5.tar.gz | 512K | split | 1896 | 1.79 |

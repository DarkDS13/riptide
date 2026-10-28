# Benchmark summary

Correctness: 1200 of 1200 measured runs byte-identical to zlib.

## E1 Scaling (4 MiB chunks, blocked windows)

| File | Ratio | zlib MB/s | Seq. MB/s | Threads | MB/s | Speedup vs 1 thread | Efficiency | vs zlib | λ |
|---|---|---|---|---|---|---|---|---|---|
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 1 | 1567 | 1.00 | 1.00 | 0.26 | 1.00 |
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 2 | 1490 | 0.95 | 0.48 | 0.25 | 1.03 |
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 4 | 2224 | 1.42 | 0.35 | 0.37 | 1.14 |
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 6 | 2728 | 1.74 | 0.29 | 0.45 | 1.22 |
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 8 | 2947 | 1.88 | 0.23 | 0.49 | 1.45 |
| logs.ndjson.gz | 23.09 | 6044 | 2466 | 10 | 3745 | 2.39 | 0.24 | 0.62 | 1.02 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 1 | 1116 | 1.00 | 1.00 | 0.58 | 1.00 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 2 | 1435 | 1.29 | 0.64 | 0.74 | 1.05 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 4 | 1979 | 1.77 | 0.44 | 1.02 | 1.11 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 6 | 2301 | 2.06 | 0.34 | 1.19 | 1.37 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 8 | 2945 | 2.64 | 0.33 | 1.52 | 1.34 |
| mixed.tar.gz | 3.51 | 1931 | 931 | 10 | 2843 | 2.55 | 0.25 | 1.47 | 1.54 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 1 | 746 | 1.00 | 1.00 | 0.46 | 1.00 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 2 | 1021 | 1.37 | 0.68 | 0.62 | 1.01 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 4 | 1268 | 1.70 | 0.42 | 0.77 | 1.03 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 6 | 1572 | 2.11 | 0.35 | 0.96 | 1.03 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 8 | 1869 | 2.50 | 0.31 | 1.14 | 1.03 |
| reads.fastq.gz | 4.11 | 1638 | 846 | 10 | 1979 | 2.65 | 0.27 | 1.21 | 1.06 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 1 | 901 | 1.00 | 1.00 | 0.74 | 1.00 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 2 | 1215 | 1.35 | 0.67 | 1.00 | 1.08 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 4 | 1891 | 2.10 | 0.52 | 1.55 | 1.14 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 6 | 2177 | 2.42 | 0.40 | 1.79 | 1.29 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 8 | 2540 | 2.82 | 0.35 | 2.09 | 1.29 |
| silesia.tar.gz | 3.11 | 1216 | 685 | 10 | 2600 | 2.89 | 0.29 | 2.14 | 1.48 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 1 | 906 | 1.00 | 1.00 | 0.75 | 1.00 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 2 | 1295 | 1.43 | 0.71 | 1.07 | 1.01 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 4 | 1923 | 2.12 | 0.53 | 1.58 | 1.02 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 6 | 2383 | 2.63 | 0.44 | 1.96 | 1.04 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 8 | 2830 | 3.12 | 0.39 | 2.33 | 1.05 |
| silesia_x5.tar.gz | 3.11 | 1216 | 688 | 10 | 3008 | 3.32 | 0.33 | 2.47 | 1.06 |

## E2 Window resolution across chunk sizes (10 threads)

| File | Chunk | Chunks | Strategy | Window ms | Rounds | Window share of total | Total ms |
|---|---|---|---|---|---|---|---|
| silesia.tar.gz | 64 KiB | 1041 | serial | 76.62 | 1040 | 24.7% | 311.3 |
| silesia.tar.gz | 64 KiB | 1041 | hillis-steele | 44.94 | 12 | 16.3% | 273.5 |
| silesia.tar.gz | 64 KiB | 1041 | blelloch | 31.39 | 24 | 11.8% | 263.9 |
| silesia.tar.gz | 64 KiB | 1041 | blocked | 25.57 | 217 | 9.7% | 256.8 |
| silesia.tar.gz | 128 KiB | 521 | serial | 37.43 | 520 | 16.3% | 230.7 |
| silesia.tar.gz | 128 KiB | 521 | hillis-steele | 18.55 | 11 | 8.9% | 207.5 |
| silesia.tar.gz | 128 KiB | 521 | blelloch | 15.40 | 22 | 7.3% | 208.8 |
| silesia.tar.gz | 128 KiB | 521 | blocked | 13.09 | 113 | 6.1% | 208.3 |
| silesia.tar.gz | 256 KiB | 261 | serial | 16.14 | 260 | 8.6% | 188.1 |
| silesia.tar.gz | 256 KiB | 261 | hillis-steele | 7.74 | 10 | 4.3% | 180.0 |
| silesia.tar.gz | 256 KiB | 261 | blelloch | 7.13 | 20 | 3.9% | 181.7 |
| silesia.tar.gz | 256 KiB | 261 | blocked | 5.24 | 61 | 3.0% | 182.8 |
| silesia.tar.gz | 512 KiB | 131 | serial | 6.56 | 130 | 4.0% | 163.9 |
| silesia.tar.gz | 512 KiB | 131 | hillis-steele | 3.34 | 9 | 2.1% | 159.9 |
| silesia.tar.gz | 512 KiB | 131 | blelloch | 3.32 | 18 | 2.0% | 159.5 |
| silesia.tar.gz | 512 KiB | 131 | blocked | 2.26 | 35 | 1.4% | 158.3 |
| silesia.tar.gz | 1024 KiB | 66 | serial | 2.62 | 65 | 1.8% | 148.3 |
| silesia.tar.gz | 1024 KiB | 66 | hillis-steele | 1.73 | 8 | 1.2% | 141.2 |
| silesia.tar.gz | 1024 KiB | 66 | blelloch | 1.96 | 16 | 1.4% | 141.3 |
| silesia.tar.gz | 1024 KiB | 66 | blocked | 1.14 | 23 | 0.8% | 143.7 |
| silesia.tar.gz | 2048 KiB | 33 | serial | 1.00 | 32 | 0.8% | 126.0 |
| silesia.tar.gz | 2048 KiB | 33 | hillis-steele | 0.81 | 6 | 0.6% | 125.0 |
| silesia.tar.gz | 2048 KiB | 33 | blelloch | 0.86 | 12 | 0.7% | 129.0 |
| silesia.tar.gz | 2048 KiB | 33 | blocked | 0.64 | 17 | 0.5% | 126.5 |
| silesia.tar.gz | 4096 KiB | 17 | serial | 0.40 | 16 | 0.3% | 135.7 |
| silesia.tar.gz | 4096 KiB | 17 | hillis-steele | 0.37 | 5 | 0.3% | 136.0 |
| silesia.tar.gz | 4096 KiB | 17 | blelloch | 0.39 | 10 | 0.3% | 140.3 |
| silesia.tar.gz | 4096 KiB | 17 | blocked | 0.36 | 13 | 0.3% | 138.3 |
| silesia_x5.tar.gz | 64 KiB | 5205 | serial | 270.80 | 5204 | 21.5% | 1257.8 |
| silesia_x5.tar.gz | 64 KiB | 5205 | hillis-steele | 184.20 | 14 | 15.4% | 1199.5 |
| silesia_x5.tar.gz | 64 KiB | 5205 | blelloch | 135.84 | 28 | 11.6% | 1167.5 |
| silesia_x5.tar.gz | 64 KiB | 5205 | blocked | 81.58 | 1051 | 7.2% | 1101.8 |
| silesia_x5.tar.gz | 128 KiB | 2603 | serial | 140.04 | 2602 | 14.7% | 952.5 |
| silesia_x5.tar.gz | 128 KiB | 2603 | hillis-steele | 74.28 | 13 | 8.3% | 896.5 |
| silesia_x5.tar.gz | 128 KiB | 2603 | blelloch | 61.63 | 26 | 7.0% | 899.2 |
| silesia_x5.tar.gz | 128 KiB | 2603 | blocked | 38.79 | 531 | 4.4% | 879.0 |
| silesia_x5.tar.gz | 256 KiB | 1302 | serial | 66.71 | 1301 | 8.4% | 790.0 |
| silesia_x5.tar.gz | 256 KiB | 1302 | hillis-steele | 29.01 | 12 | 3.8% | 767.2 |
| silesia_x5.tar.gz | 256 KiB | 1302 | blelloch | 26.76 | 24 | 3.5% | 761.5 |
| silesia_x5.tar.gz | 256 KiB | 1302 | blocked | 19.88 | 271 | 2.6% | 765.0 |
| silesia_x5.tar.gz | 512 KiB | 651 | serial | 28.25 | 650 | 4.1% | 679.5 |
| silesia_x5.tar.gz | 512 KiB | 651 | hillis-steele | 11.04 | 11 | 1.6% | 662.4 |
| silesia_x5.tar.gz | 512 KiB | 651 | blelloch | 11.34 | 22 | 1.7% | 662.4 |
| silesia_x5.tar.gz | 512 KiB | 651 | blocked | 7.23 | 139 | 1.1% | 655.8 |
| silesia_x5.tar.gz | 1024 KiB | 326 | serial | 10.60 | 325 | 1.8% | 598.3 |
| silesia_x5.tar.gz | 1024 KiB | 326 | hillis-steele | 4.42 | 10 | 0.7% | 591.4 |
| silesia_x5.tar.gz | 1024 KiB | 326 | blelloch | 4.49 | 20 | 0.8% | 593.2 |
| silesia_x5.tar.gz | 1024 KiB | 326 | blocked | 3.05 | 75 | 0.5% | 593.9 |
| silesia_x5.tar.gz | 2048 KiB | 163 | serial | 4.22 | 162 | 0.8% | 531.7 |
| silesia_x5.tar.gz | 2048 KiB | 163 | hillis-steele | 2.16 | 9 | 0.4% | 526.9 |
| silesia_x5.tar.gz | 2048 KiB | 163 | blelloch | 2.22 | 18 | 0.4% | 532.7 |
| silesia_x5.tar.gz | 2048 KiB | 163 | blocked | 1.26 | 43 | 0.2% | 529.3 |
| silesia_x5.tar.gz | 4096 KiB | 82 | serial | 1.73 | 81 | 0.4% | 482.1 |
| silesia_x5.tar.gz | 4096 KiB | 82 | hillis-steele | 1.24 | 8 | 0.3% | 485.1 |
| silesia_x5.tar.gz | 4096 KiB | 82 | blelloch | 1.27 | 16 | 0.3% | 485.1 |
| silesia_x5.tar.gz | 4096 KiB | 82 | blocked | 0.61 | 27 | 0.1% | 478.9 |

## E2 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| silesia.tar.gz 64 KiB window step: hillis-steele vs serial | 76.615 ms | 44.943 ms | 1.70x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: hillis-steele vs serial | 311.329 ms | 273.484 ms | 1.14x | 0.003 | yes |
| silesia.tar.gz 64 KiB window step: blelloch vs serial | 76.615 ms | 31.386 ms | 2.44x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: blelloch vs serial | 311.329 ms | 263.876 ms | 1.18x | 0.00091 | yes |
| silesia.tar.gz 64 KiB window step: blocked vs serial | 76.615 ms | 25.566 ms | 3.00x | 0.00091 | yes |
| silesia.tar.gz 64 KiB total: blocked vs serial | 311.329 ms | 256.840 ms | 1.21x | 0.00091 | yes |
| silesia.tar.gz 128 KiB window step: hillis-steele vs serial | 37.434 ms | 18.552 ms | 2.02x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: hillis-steele vs serial | 230.727 ms | 207.525 ms | 1.11x | 0.011 | yes |
| silesia.tar.gz 128 KiB window step: blelloch vs serial | 37.434 ms | 15.402 ms | 2.43x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: blelloch vs serial | 230.727 ms | 208.755 ms | 1.11x | 0.011 | yes |
| silesia.tar.gz 128 KiB window step: blocked vs serial | 37.434 ms | 13.090 ms | 2.86x | 0.00091 | yes |
| silesia.tar.gz 128 KiB total: blocked vs serial | 230.727 ms | 208.316 ms | 1.11x | 0.003 | yes |
| silesia.tar.gz 256 KiB window step: hillis-steele vs serial | 16.143 ms | 7.736 ms | 2.09x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: hillis-steele vs serial | 188.133 ms | 179.966 ms | 1.05x | 0.89 | no |
| silesia.tar.gz 256 KiB window step: blelloch vs serial | 16.143 ms | 7.135 ms | 2.26x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: blelloch vs serial | 188.133 ms | 181.688 ms | 1.04x | 1 | no |
| silesia.tar.gz 256 KiB window step: blocked vs serial | 16.143 ms | 5.236 ms | 3.08x | 0.00091 | yes |
| silesia.tar.gz 256 KiB total: blocked vs serial | 188.133 ms | 182.843 ms | 1.03x | 1 | no |
| silesia.tar.gz 512 KiB window step: hillis-steele vs serial | 6.561 ms | 3.338 ms | 1.97x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: hillis-steele vs serial | 163.874 ms | 159.946 ms | 1.02x | 1 | no |
| silesia.tar.gz 512 KiB window step: blelloch vs serial | 6.561 ms | 3.315 ms | 1.98x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: blelloch vs serial | 163.874 ms | 159.484 ms | 1.03x | 1 | no |
| silesia.tar.gz 512 KiB window step: blocked vs serial | 6.561 ms | 2.264 ms | 2.90x | 0.00091 | yes |
| silesia.tar.gz 512 KiB total: blocked vs serial | 163.874 ms | 158.311 ms | 1.04x | 1 | no |
| silesia.tar.gz 1024 KiB window step: hillis-steele vs serial | 2.616 ms | 1.732 ms | 1.51x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: hillis-steele vs serial | 148.302 ms | 141.162 ms | 1.05x | 1 | no |
| silesia.tar.gz 1024 KiB window step: blelloch vs serial | 2.616 ms | 1.962 ms | 1.33x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: blelloch vs serial | 148.302 ms | 141.294 ms | 1.05x | 0.2 | no |
| silesia.tar.gz 1024 KiB window step: blocked vs serial | 2.616 ms | 1.140 ms | 2.29x | 0.00091 | yes |
| silesia.tar.gz 1024 KiB total: blocked vs serial | 148.302 ms | 143.727 ms | 1.03x | 0.75 | no |
| silesia.tar.gz 2048 KiB window step: hillis-steele vs serial | 1.005 ms | 0.813 ms | 1.24x | 0.00091 | yes |
| silesia.tar.gz 2048 KiB total: hillis-steele vs serial | 126.028 ms | 125.036 ms | 1.01x | 1 | no |
| silesia.tar.gz 2048 KiB window step: blelloch vs serial | 1.005 ms | 0.858 ms | 1.17x | 0.00091 | yes |
| silesia.tar.gz 2048 KiB total: blelloch vs serial | 126.028 ms | 128.986 ms | 0.98x | 1 | no |
| silesia.tar.gz 2048 KiB window step: blocked vs serial | 1.005 ms | 0.641 ms | 1.57x | 0.00091 | yes |
| silesia.tar.gz 2048 KiB total: blocked vs serial | 126.028 ms | 126.497 ms | 1.00x | 1 | no |
| silesia.tar.gz 4096 KiB window step: hillis-steele vs serial | 0.396 ms | 0.370 ms | 1.07x | 1 | no |
| silesia.tar.gz 4096 KiB total: hillis-steele vs serial | 135.739 ms | 135.981 ms | 1.00x | 1 | no |
| silesia.tar.gz 4096 KiB window step: blelloch vs serial | 0.396 ms | 0.391 ms | 1.01x | 1 | no |
| silesia.tar.gz 4096 KiB total: blelloch vs serial | 135.739 ms | 140.337 ms | 0.97x | 1 | no |
| silesia.tar.gz 4096 KiB window step: blocked vs serial | 0.396 ms | 0.360 ms | 1.10x | 1 | no |
| silesia.tar.gz 4096 KiB total: blocked vs serial | 135.739 ms | 138.315 ms | 0.98x | 1 | no |
| silesia_x5.tar.gz 64 KiB window step: hillis-steele vs serial | 270.796 ms | 184.198 ms | 1.47x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: hillis-steele vs serial | 1257.820 ms | 1199.500 ms | 1.05x | 0.003 | yes |
| silesia_x5.tar.gz 64 KiB window step: blelloch vs serial | 270.796 ms | 135.839 ms | 1.99x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: blelloch vs serial | 1257.820 ms | 1167.475 ms | 1.08x | 0.0047 | yes |
| silesia_x5.tar.gz 64 KiB window step: blocked vs serial | 270.796 ms | 81.584 ms | 3.32x | 0.00091 | yes |
| silesia_x5.tar.gz 64 KiB total: blocked vs serial | 1257.820 ms | 1101.765 ms | 1.14x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB window step: hillis-steele vs serial | 140.036 ms | 74.276 ms | 1.89x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: hillis-steele vs serial | 952.457 ms | 896.460 ms | 1.06x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB window step: blelloch vs serial | 140.036 ms | 61.633 ms | 2.27x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: blelloch vs serial | 952.457 ms | 899.169 ms | 1.06x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB window step: blocked vs serial | 140.036 ms | 38.789 ms | 3.61x | 0.00091 | yes |
| silesia_x5.tar.gz 128 KiB total: blocked vs serial | 952.457 ms | 878.954 ms | 1.08x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB window step: hillis-steele vs serial | 66.710 ms | 29.008 ms | 2.30x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: hillis-steele vs serial | 789.998 ms | 767.210 ms | 1.03x | 0.086 | no |
| silesia_x5.tar.gz 256 KiB window step: blelloch vs serial | 66.710 ms | 26.755 ms | 2.49x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: blelloch vs serial | 789.998 ms | 761.514 ms | 1.04x | 0.011 | yes |
| silesia_x5.tar.gz 256 KiB window step: blocked vs serial | 66.710 ms | 19.880 ms | 3.36x | 0.00091 | yes |
| silesia_x5.tar.gz 256 KiB total: blocked vs serial | 789.998 ms | 765.041 ms | 1.03x | 0.0047 | yes |
| silesia_x5.tar.gz 512 KiB window step: hillis-steele vs serial | 28.254 ms | 11.041 ms | 2.56x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: hillis-steele vs serial | 679.535 ms | 662.438 ms | 1.03x | 0.5 | no |
| silesia_x5.tar.gz 512 KiB window step: blelloch vs serial | 28.254 ms | 11.337 ms | 2.49x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: blelloch vs serial | 679.535 ms | 662.410 ms | 1.03x | 0.2 | no |
| silesia_x5.tar.gz 512 KiB window step: blocked vs serial | 28.254 ms | 7.228 ms | 3.91x | 0.00091 | yes |
| silesia_x5.tar.gz 512 KiB total: blocked vs serial | 679.535 ms | 655.822 ms | 1.04x | 0.022 | yes |
| silesia_x5.tar.gz 1024 KiB window step: hillis-steele vs serial | 10.597 ms | 4.420 ms | 2.40x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: hillis-steele vs serial | 598.271 ms | 591.398 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 1024 KiB window step: blelloch vs serial | 10.597 ms | 4.490 ms | 2.36x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: blelloch vs serial | 598.271 ms | 593.233 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 1024 KiB window step: blocked vs serial | 10.597 ms | 3.054 ms | 3.47x | 0.00091 | yes |
| silesia_x5.tar.gz 1024 KiB total: blocked vs serial | 598.271 ms | 593.940 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: hillis-steele vs serial | 4.220 ms | 2.156 ms | 1.96x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: hillis-steele vs serial | 531.709 ms | 526.850 ms | 1.01x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: blelloch vs serial | 4.220 ms | 2.222 ms | 1.90x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: blelloch vs serial | 531.709 ms | 532.696 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 2048 KiB window step: blocked vs serial | 4.220 ms | 1.258 ms | 3.35x | 0.00091 | yes |
| silesia_x5.tar.gz 2048 KiB total: blocked vs serial | 531.709 ms | 529.265 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: hillis-steele vs serial | 1.732 ms | 1.238 ms | 1.40x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: hillis-steele vs serial | 482.075 ms | 485.129 ms | 0.99x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: blelloch vs serial | 1.732 ms | 1.271 ms | 1.36x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: blelloch vs serial | 482.075 ms | 485.063 ms | 0.99x | 1 | no |
| silesia_x5.tar.gz 4096 KiB window step: blocked vs serial | 1.732 ms | 0.605 ms | 2.86x | 0.00091 | yes |
| silesia_x5.tar.gz 4096 KiB total: blocked vs serial | 482.075 ms | 478.888 ms | 1.01x | 1 | no |

## E3 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| 1 threads window step: blocked vs serial | 182.653 ms | 368.451 ms | 0.50x | 6.5e-05 | yes |
| 2 threads window step: blocked vs serial | 209.460 ms | 220.780 ms | 0.95x | 6.5e-05 | yes |
| 4 threads window step: blocked vs serial | 225.551 ms | 142.286 ms | 1.59x | 6.5e-05 | yes |
| 6 threads window step: blocked vs serial | 242.853 ms | 102.291 ms | 2.37x | 6.5e-05 | yes |
| 8 threads window step: blocked vs serial | 260.618 ms | 86.895 ms | 3.00x | 6.5e-05 | yes |
| 10 threads window step: blocked vs serial | 260.811 ms | 74.276 ms | 3.51x | 6.5e-05 | yes |

## E4 comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB total: fixed/split vs fixed/serial | 248.319 ms | 252.877 ms | 0.98x | 0.37 | no |
| logs.ndjson.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 248.319 ms | 276.700 ms | 0.90x | 9.7e-05 | yes |
| logs.ndjson.gz 4096 KiB total: work-balanced/split vs fixed/serial | 248.319 ms | 277.156 ms | 0.90x | 9.7e-05 | yes |
| mixed.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 140.235 ms | 139.511 ms | 1.01x | 1 | no |
| mixed.tar.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 140.235 ms | 215.645 ms | 0.65x | 9.7e-05 | yes |
| mixed.tar.gz 4096 KiB total: work-balanced/split vs fixed/serial | 140.235 ms | 211.184 ms | 0.66x | 9.7e-05 | yes |
| silesia_x5.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 474.168 ms | 475.978 ms | 1.00x | 1 | no |
| silesia_x5.tar.gz 4096 KiB total: work-balanced/serial vs fixed/serial | 474.168 ms | 520.140 ms | 0.91x | 9.7e-05 | yes |
| silesia_x5.tar.gz 4096 KiB total: work-balanced/split vs fixed/serial | 474.168 ms | 524.384 ms | 0.90x | 9.7e-05 | yes |

## E4 budget comparisons (Mann-Whitney U, Holm-corrected)

| Comparison | Baseline median | Candidate median | Ratio | p (Holm) | Significant |
|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB total: fixed/split vs fixed/serial | 259.423 ms | 263.174 ms | 0.99x | 0.33 | no |
| logs.ndjson.gz 16384 KiB total: fixed/split vs fixed/serial | 407.046 ms | 255.663 ms | 1.59x | 4.3e-05 | yes |
| silesia_x5.tar.gz 4096 KiB total: fixed/split vs fixed/serial | 489.030 ms | 503.985 ms | 0.97x | 0.33 | no |
| silesia_x5.tar.gz 16384 KiB total: fixed/split vs fixed/serial | 414.184 ms | 397.168 ms | 1.04x | 0.07 | no |

## E3 Window resolution across threads (64 KiB chunks)

| Threads | Serial window ms | Blocked window ms | Serial total ms | Blocked total ms |
|---|---|---|---|---|
| 1 | 182.65 | 368.45 | 2915.2 | 3108.6 |
| 2 | 209.46 | 220.78 | 2361.9 | 2435.7 |
| 4 | 225.55 | 142.29 | 1820.7 | 1794.5 |
| 6 | 242.85 | 102.29 | 1493.5 | 1388.0 |
| 8 | 260.62 | 86.89 | 1315.0 | 1141.4 |
| 10 | 260.81 | 74.28 | 1250.3 | 1084.9 |

## E4 Chunking and overflow policies (10 threads, 20x output budget)

| File | Chunking/overflow | Total ms | Decode ms | λ | Overflow splits | Chain |
|---|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB | fixed/serial | 248.3 | 103.4 | 1.11 | 11 | 23 |
| logs.ndjson.gz 4096 KiB | fixed/split | 252.9 | 107.5 | 1.03 | 11 | 91 |
| logs.ndjson.gz 4096 KiB | work-balanced/serial | 276.7 | 128.9 | 1.38 | 12 | 24 |
| logs.ndjson.gz 4096 KiB | work-balanced/split | 277.2 | 124.8 | 1.33 | 12 | 46 |
| mixed.tar.gz 4096 KiB | fixed/serial | 140.2 | 108.4 | 1.53 | 0 | 31 |
| mixed.tar.gz 4096 KiB | fixed/split | 139.5 | 108.3 | 1.54 | 0 | 31 |
| mixed.tar.gz 4096 KiB | work-balanced/serial | 215.6 | 105.2 | 1.34 | 0 | 31 |
| mixed.tar.gz 4096 KiB | work-balanced/split | 211.2 | 102.8 | 1.30 | 0 | 31 |
| silesia_x5.tar.gz 4096 KiB | fixed/serial | 474.2 | 372.7 | 1.06 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | fixed/split | 476.0 | 376.5 | 1.07 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | work-balanced/serial | 520.1 | 379.7 | 1.10 | 0 | 82 |
| silesia_x5.tar.gz 4096 KiB | work-balanced/split | 524.4 | 379.7 | 1.08 | 0 | 82 |

## E4 Overflow policies under a 4x output budget (10 threads)

| File | Chunking/overflow | Total ms | Decode ms | λ | Overflow splits | Chain |
|---|---|---|---|---|---|---|
| logs.ndjson.gz 4096 KiB | fixed/serial | 259.4 | 111.2 | 1.11 | 55 | 67 |
| logs.ndjson.gz 4096 KiB | fixed/split | 263.2 | 113.2 | 1.04 | 11 | 122 |
| logs.ndjson.gz 16384 KiB | fixed/serial | 407.0 | 286.1 | 5.58 | 14 | 17 |
| logs.ndjson.gz 16384 KiB | fixed/split | 255.7 | 119.5 | 1.29 | 3 | 33 |
| silesia_x5.tar.gz 4096 KiB | fixed/serial | 489.0 | 381.1 | 1.06 | 7 | 89 |
| silesia_x5.tar.gz 4096 KiB | fixed/split | 504.0 | 389.0 | 1.06 | 6 | 141 |
| silesia_x5.tar.gz 16384 KiB | fixed/serial | 414.2 | 357.8 | 1.20 | 5 | 26 |
| silesia_x5.tar.gz 16384 KiB | fixed/split | 397.2 | 341.8 | 1.13 | 5 | 62 |

## E5 Peak memory (10 threads)

| File | Chunk | Overflow | Peak RSS MB | RSS / output |
|---|---|---|---|---|
| logs.ndjson.gz | 4M | serial | 3205 | 2.98 |
| logs.ndjson.gz | 4M | split | 3008 | 2.79 |
| logs.ndjson.gz | 512K | serial | 2090 | 1.94 |
| logs.ndjson.gz | 512K | split | 2099 | 1.95 |
| silesia_x5.tar.gz | 4M | serial | 2336 | 2.20 |
| silesia_x5.tar.gz | 4M | split | 2371 | 2.24 |
| silesia_x5.tar.gz | 512K | serial | 1896 | 1.79 |
| silesia_x5.tar.gz | 512K | split | 1912 | 1.80 |

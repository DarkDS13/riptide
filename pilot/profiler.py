"""Riptide Stage-1 stream profiler.

A reference DEFLATE/gzip decoder that records the structure of a stream:
every block boundary (as a bit offset), block type, per-block input and
output size, symbol counts and the match-distance / match-length profile.
Output is verified byte-for-byte against zlib, so the profiler doubles as
the correctness oracle for later parallel stages.
"""
import hashlib
import json
import struct
import sys
import zlib

LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
         59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
        5, 5, 5, 5, 0]
DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
         513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577]
DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
        11, 11, 12, 12, 13, 13]
CL_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]


class BitReader:
    """LSB-first bit reader over a bytes object."""
    __slots__ = ("d", "p", "b", "c", "n")

    def __init__(self, data, pos):
        self.d, self.p, self.b, self.c, self.n = data, pos, 0, 0, len(data)

    def fill(self, k):
        while self.c < k:
            if self.p < self.n:
                self.b |= self.d[self.p] << self.c
                self.p += 1
            self.c += 8  # zero-pad past end of input

    def bits(self, k):
        if self.c < k:
            self.fill(k)
        v = self.b & ((1 << k) - 1)
        self.b >>= k
        self.c -= k
        return v

    def bitpos(self):
        return self.p * 8 - self.c

    def align(self):
        drop = self.c & 7
        self.b >>= drop
        self.c -= drop


def build_table(lengths):
    """Canonical Huffman decode table indexed by the next maxlen input bits."""
    maxlen = max(lengths) if lengths else 0
    if maxlen == 0:
        return None, 0
    count = [0] * (maxlen + 1)
    for ln in lengths:
        if ln:
            count[ln] += 1
    code, nxt = 0, [0] * (maxlen + 2)
    for bits in range(1, maxlen + 1):
        code = (code + count[bits - 1]) << 1
        nxt[bits] = code
    table = [None] * (1 << maxlen)
    for sym, ln in enumerate(lengths):
        if not ln:
            continue
        c = nxt[ln]
        nxt[ln] += 1
        rev = 0
        for i in range(ln):
            rev = (rev << 1) | ((c >> i) & 1)
        for idx in range(rev, 1 << maxlen, 1 << ln):
            table[idx] = (sym, ln)
    return table, maxlen


def decode_sym(br, table, maxlen):
    if br.c < maxlen:
        br.fill(maxlen)
    e = table[br.b & ((1 << maxlen) - 1)]
    if e is None:
        raise ValueError(f"invalid Huffman code at bit {br.bitpos()}")
    br.b >>= e[1]
    br.c -= e[1]
    return e[0]


FIXED_LIT = build_table([8] * 144 + [9] * 112 + [7] * 24 + [8] * 8)
FIXED_DIST = build_table([5] * 30)


def read_dynamic_tables(br):
    hlit, hdist, hclen = br.bits(5) + 257, br.bits(5) + 1, br.bits(4) + 4
    cl = [0] * 19
    for i in range(hclen):
        cl[CL_ORDER[i]] = br.bits(3)
    ctab, cmax = build_table(cl)
    lens = []
    while len(lens) < hlit + hdist:
        sym = decode_sym(br, ctab, cmax)
        if sym < 16:
            lens.append(sym)
        elif sym == 16:
            lens += [lens[-1]] * (3 + br.bits(2))
        elif sym == 17:
            lens += [0] * (3 + br.bits(3))
        else:
            lens += [0] * (11 + br.bits(7))
    return build_table(lens[:hlit]), build_table(lens[hlit:hlit + hdist])


def inflate(data, pos, blocks, dist_hist, len_hist):
    """Inflate one raw DEFLATE stream starting at byte `pos`.

    Appends one record per block to `blocks`. Returns (output, end_byte).
    """
    br = BitReader(data, pos)
    out = bytearray()
    while True:
        start_bit = br.bitpos()
        out_before = len(out)
        final, btype = br.bits(1), br.bits(2)
        n_lit = n_match = 0
        if btype == 0:
            br.align()
            ln, nln = br.bits(16), br.bits(16)
            if ln != (~nln & 0xFFFF):
                raise ValueError("stored block length check failed")
            k = 0
            while br.c >= 8 and k < ln:
                out.append(br.bits(8))
                k += 1
            out += data[br.p:br.p + ln - k]
            br.p += ln - k
            n_lit = ln
        elif btype in (1, 2):
            (lt, lm), (dt, dm) = (FIXED_LIT, FIXED_DIST) if btype == 1 else read_dynamic_tables(br)
            while True:
                sym = decode_sym(br, lt, lm)
                if sym < 256:
                    out.append(sym)
                    n_lit += 1
                elif sym == 256:
                    break
                else:
                    li = sym - 257
                    length = LBASE[li] + (br.bits(LEXT[li]) if LEXT[li] else 0)
                    ds = decode_sym(br, dt, dm)
                    dist = DBASE[ds] + (br.bits(DEXT[ds]) if DEXT[ds] else 0)
                    start = len(out) - dist
                    if dist >= length:
                        out += out[start:start + length]
                    else:  # overlapping copy repeats the last `dist` bytes
                        pat = out[start:start + dist]
                        out += (pat * (length // dist + 1))[:length]
                    n_match += 1
                    dist_hist[dist] = dist_hist.get(dist, 0) + 1
                    len_hist[length] = len_hist.get(length, 0) + 1
        else:
            raise ValueError("reserved block type 3")
        blocks.append({
            "start_bit": start_bit,
            "in_bits": br.bitpos() - start_bit,
            "out_bytes": len(out) - out_before,
            "type": ("stored", "fixed", "dynamic")[btype],
            "literals": n_lit,
            "matches": n_match,
        })
        if final:
            break
    br.align()
    return out, br.p - br.c // 8


def gzip_payload_offset(d, off):
    if d[off:off + 3] != b"\x1f\x8b\x08":
        raise ValueError("not a gzip member")
    flg, p = d[off + 3], off + 10
    if flg & 4:
        p += 2 + (d[p] | d[p + 1] << 8)
    if flg & 8:
        p = d.index(0, p) + 1
    if flg & 16:
        p = d.index(0, p) + 1
    if flg & 2:
        p += 2
    return p


def profile(path):
    data = open(path, "rb").read()
    blocks, dist_hist, len_hist = [], {}, {}
    out, off, members = bytearray(), 0, 0
    while off < len(data):
        payload = gzip_payload_offset(data, off)
        member_out, end = inflate(data, payload, blocks, dist_hist, len_hist)
        crc, isize = struct.unpack("<II", data[end:end + 8])
        if crc != (zlib.crc32(member_out) & 0xFFFFFFFF) or isize != len(member_out) & 0xFFFFFFFF:
            raise ValueError("trailer CRC32/ISIZE mismatch")
        out += member_out
        off = end + 8
        members += 1
    reference = zlib.decompress(data, wbits=31) if members == 1 else _zlib_multi(data)
    return {
        "file": path.split("/")[-1],
        "compressed_bytes": len(data),
        "uncompressed_bytes": len(out),
        "members": members,
        "bit_identical_to_zlib": hashlib.sha256(out).digest() == hashlib.sha256(reference).digest(),
        "blocks": blocks,
        "dist_hist": dist_hist,
        "len_hist": len_hist,
    }


def _zlib_multi(data):
    out, d = bytearray(), zlib.decompressobj(wbits=31)
    while data:
        out += d.decompress(data)
        data = d.unused_data
        d = zlib.decompressobj(wbits=31)
    return bytes(out)


if __name__ == "__main__":
    results = [profile(p) for p in sys.argv[1:]]
    json.dump(results, sys.stdout)

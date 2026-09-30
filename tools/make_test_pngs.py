#!/usr/bin/env python3
"""Write PNG test fixtures (every colour type / depth / filter, dynamic-Huffman deflate from zlib) plus the expected
RGBA pixels of each, for tools/test_png.c. Standard library only.   usage: make_test_pngs.py OUTDIR"""
import os
import struct
import sys
import zlib


def chunk(t, body):
    return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filt(rows, bpp, ftype):
    out, prev = bytearray(), bytes(len(rows[0]))
    for i, row in enumerate(rows):
        ft = ftype if ftype < 5 else i % 5
        out.append(ft)
        for x, v in enumerate(row):
            a = row[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            pred = [0, a, b, (a + b) >> 1, paeth(a, b, c)][ft]
            out.append((v - pred) & 255)
        prev = row
    return bytes(out)


def png(w, h, depth, ctype, rows, bpp, ftype, plte=None, trns=None, level=9):
    data = chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    if plte:
        data += chunk(b"PLTE", plte)
    if trns:
        data += chunk(b"tRNS", trns)
    comp = zlib.compress(filt(rows, bpp, ftype), level)
    half = len(comp) // 2                          # two IDAT chunks
    data += chunk(b"IDAT", comp[:half]) + chunk(b"IDAT", comp[half:]) + chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + data


def main(out):
    os.makedirs(out, exist_ok=True)
    W, H = 37, 11
    cases = {}
    for ft in range(6):                            # 8-bit gray, every filter and a mix
        rows = [bytes((x * 7 + y * 13 + (x * y) % 5) & 255 for x in range(W)) for y in range(H)]
        cases[f"gray8_f{ft}"] = (png(W, H, 8, 0, rows, 1, ft), [[(r[x],) * 3 + (255,) for x in range(W)] for r in rows])
    rows = [bytes(v for x in range(W) for v in ((x * 5 + y) & 255, (x * 3 + y * 9) & 255, (255 - x * 4) & 255)) for y in range(H)]
    cases["rgb8_f5"] = (png(W, H, 8, 2, rows, 3, 5), [[tuple(r[3 * x:3 * x + 3]) + (255,) for x in range(W)] for r in rows])
    rows = [bytes(v for x in range(W) for v in ((x * 5) & 255, (y * 20) & 255, 200, (x * 6 + y) & 255)) for y in range(H)]
    cases["rgba8_f4"] = (png(W, H, 8, 6, rows, 4, 4), [[tuple(r[4 * x:4 * x + 4]) for x in range(W)] for r in rows])
    rows = [bytes(v for x in range(W) for v in ((x * 9) & 255, (x * 2 + y * 30) & 255)) for y in range(H)]
    cases["graya8_f3"] = (png(W, H, 8, 4, rows, 2, 3), [[(r[2 * x],) * 3 + (r[2 * x + 1],) for x in range(W)] for r in rows])
    rows = [bytes(v for x in range(W) for c in range(3) for v in (((x * 6 + c * 50 + y) & 255), 0x55)) for y in range(H)]
    cases["rgb16_f2"] = (png(W, H, 16, 2, rows, 6, 2), [[tuple(r[6 * x + 2 * c] for c in range(3)) + (255,) for x in range(W)] for r in rows])
    pal = bytes(v for i in range(16) for v in (i * 16, 255 - i * 16, (i * 40) & 255))
    trns = bytes([255] * 8 + [0] * 8)
    idx = [[(x + y) % 16 for x in range(W)] for y in range(H)]
    rows = []
    for r in idx:
        b = bytearray((W + 1) // 2)
        for x, v in enumerate(r):
            b[x // 2] |= v << (4 if x % 2 == 0 else 0)
        rows.append(bytes(b))
    cases["pal4_trns"] = (png(W, H, 4, 3, rows, 1, 1, plte=pal, trns=trns),
                          [[(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2], trns[i]) for i in r] for r in idx])
    bits = [[(x * 3 + y) % 2 for x in range(W)] for y in range(H)]
    rows = []
    for r in bits:
        b = bytearray((W + 7) // 8)
        for x, v in enumerate(r):
            b[x // 8] |= v << (7 - x % 8)
        rows.append(bytes(b))
    cases["gray1_f0"] = (png(W, H, 1, 0, rows, 1, 0), [[(v * 255,) * 3 + (255,) for v in r] for r in bits])
    q = [[(x + 2 * y) % 4 for x in range(W)] for y in range(H)]
    rows = []
    for r in q:
        b = bytearray((W + 3) // 4)
        for x, v in enumerate(r):
            b[x // 4] |= v << (6 - 2 * (x % 4))
        rows.append(bytes(b))
    cases["gray2_f2"] = (png(W, H, 2, 0, rows, 1, 2), [[(v * 85,) * 3 + (255,) for v in r] for r in q])
    rows = [bytes([(y // 8) * 30 % 256] * 300) for y in range(200)]        # long back-references
    cases["gray8_big"] = (png(300, 200, 8, 0, rows, 1, 4), [[(r[x],) * 3 + (255,) for x in range(300)] for r in rows])
    # handwriting: black on white, white on black, transparent background
    ink = [[1 if (x - 40) ** 2 + (y - 20) ** 2 < 150 else 0 for x in range(80)] for y in range(40)]
    rows = [bytes(255 - 255 * v for v in r) for r in ink]
    cases["ink_bw"] = (png(80, 40, 8, 0, rows, 1, 1), None)
    rows = [bytes(255 * v for v in r) for r in ink]
    cases["ink_wb"] = (png(80, 40, 8, 0, rows, 1, 1), None)
    rows = [bytes(x for v in r for x in (255, 255, 255, 255 * v)) for r in ink]
    cases["ink_alpha"] = (png(80, 40, 8, 6, rows, 4, 1), None)
    for name, (data, exp) in cases.items():
        open(os.path.join(out, name + ".png"), "wb").write(data)
        if exp:
            with open(os.path.join(out, name + ".txt"), "w") as f:
                f.write(f"{len(exp[0])} {len(exp)}\n")
                for row in exp:
                    f.write(" ".join(",".join(str(c) for c in px) for px in row) + "\n")
    print(len(cases), "fixtures in", out)


main(sys.argv[1] if len(sys.argv) > 1 else "png_fixtures")

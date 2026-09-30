#!/usr/bin/env python3
"""Small image utility for PathTracer-CPP outputs (Python standard library only).

  imgtool.py topng  <in.ppm|in.pfm> <out.png> [--exposure E] [--normalize]
  imgtool.py concat <out.png> <in1> <in2> [...] [--vertical] [--gap N]
  imgtool.py diff   <a.pfm> <b.pfm> <out.png> [--scale S]
  imgtool.py stats  <in.pfm>

PFM inputs are linear HDR; they are displayed with the same per-channel Reinhard + sRGB
transform as the renderer's PPM output (Color.h display_transform), optionally after an
exposure multiplier. --normalize maps [min, max] of the data to [0, 1] without tone mapping
(useful for AOVs such as depth or sample counts).
"""
import math
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    tokens = []
    i = 0
    # Header: magic, width, height, maxval (comments not produced by the renderer).
    while len(tokens) < 4:
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        tokens.append(data[i:j].decode())
        i = j
    magic, w, h, maxval = tokens[0], int(tokens[1]), int(tokens[2]), int(tokens[3])
    if magic == "P6":
        raw = data[i + 1:i + 1 + w * h * 3]
        vals = list(raw)
    elif magic == "P3":
        vals = [int(t) for t in data[i:].split()][: w * h * 3]
    else:
        raise ValueError("unsupported PPM " + magic)
    scale = 255.0 / maxval
    rows = []
    for y in range(h):
        row = bytearray(w * 3)
        base = y * w * 3
        for k in range(w * 3):
            row[k] = int(round(vals[base + k] * scale))
        rows.append(bytes(row))
    return w, h, rows


def read_pfm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"PF":
            raise ValueError("only RGB PFM supported")
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        scale = float(f.readline().strip())
        endian = "<" if scale < 0 else ">"
        raw = f.read(w * h * 12)
    floats = struct.unpack(endian + "%df" % (w * h * 3), raw)
    # PFM rows are bottom-to-top.
    pixels = []
    for y in range(h - 1, -1, -1):
        base = y * w * 3
        pixels.append(floats[base:base + w * 3])
    return w, h, pixels


def linear_to_srgb(x):
    if x <= 0.0:
        return 0.0
    if x <= 0.0031308:
        return 12.92 * x
    return 1.055 * x ** (1.0 / 2.4) - 0.055


def to_byte(v):
    return max(0, min(255, int(256 * min(max(v, 0.0), 0.999))))


def tonemap_rows(w, h, pixels, exposure=1.0, normalize=False):
    rows = []
    lo, hi = 0.0, 1.0
    if normalize:
        finite = [v for row in pixels for v in row if math.isfinite(v)]
        lo, hi = (min(finite), max(finite)) if finite else (0.0, 1.0)
        if hi <= lo:
            hi = lo + 1.0
    for row in pixels:
        out = bytearray(w * 3)
        for k, v in enumerate(row):
            if not math.isfinite(v):
                v = 0.0
            if normalize:
                d = (v - lo) / (hi - lo)
            else:
                v = max(v, 0.0) * exposure
                d = linear_to_srgb(v / (v + 1.0))
            out[k] = to_byte(d)
        rows.append(bytes(out))
    return rows


def load_display(path, exposure=1.0, normalize=False):
    if path.lower().endswith(".pfm"):
        w, h, px = read_pfm(path)
        return w, h, tonemap_rows(w, h, px, exposure, normalize)
    if path.lower().endswith(".png"):
        return read_png(path)
    return read_ppm(path)


def write_png(path, w, h, rows):
    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def read_png(path):
    """Reader for the 8-bit RGB, non-interlaced PNGs written by write_png / most tools."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 8
    idat = b""
    w = h = 0
    color_type = 2
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            w, h, depth, color_type = struct.unpack(">IIBB", payload[:10])
            if depth != 8 or color_type not in (2, 6):
                raise ValueError("unsupported PNG format")
        elif tag == b"IDAT":
            idat += payload
        pos += 12 + length
    bpp = 3 if color_type == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows = []
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        ftype = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 255
            elif ftype == 2:
                line[x] = (line[x] + b) & 255
            elif ftype == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        prev = line
        if bpp == 4:
            rgb = bytearray(w * 3)
            for x in range(w):
                rgb[3 * x:3 * x + 3] = line[4 * x:4 * x + 3]
            rows.append(bytes(rgb))
        else:
            rows.append(bytes(line))
    return w, h, rows


def cmd_topng(args):
    exposure = 1.0
    normalize = False
    rest = []
    it = iter(args)
    for a in it:
        if a == "--exposure":
            exposure = float(next(it))
        elif a == "--normalize":
            normalize = True
        else:
            rest.append(a)
    w, h, rows = load_display(rest[0], exposure, normalize)
    write_png(rest[1], w, h, rows)


def cmd_concat(args):
    vertical = False
    gap = 8
    files = []
    it = iter(args)
    for a in it:
        if a == "--vertical":
            vertical = True
        elif a == "--gap":
            gap = int(next(it))
        else:
            files.append(a)
    out = files[0]
    images = [load_display(p) for p in files[1:]]
    bg = b"\xff\xff\xff"
    if vertical:
        W = max(im[0] for im in images)
        rows = []
        for n, (w, h, r) in enumerate(images):
            if n:
                rows += [bg * W] * gap
            rows += [row + bg * (W - w) for row in r]
        write_png(out, W, len(rows), rows)
    else:
        H = max(im[1] for im in images)
        W = sum(im[0] for im in images) + gap * (len(images) - 1)
        rows = []
        for y in range(H):
            parts = []
            for n, (w, h, r) in enumerate(images):
                if n:
                    parts.append(bg * gap)
                parts.append(r[y] if y < h else bg * w)
            rows.append(b"".join(parts))
        write_png(out, W, H, rows)


def cmd_diff(args):
    scale = 4.0
    rest = []
    it = iter(args)
    for a in it:
        if a == "--scale":
            scale = float(next(it))
        else:
            rest.append(a)
    wa, ha, a = read_pfm(rest[0])
    wb, hb, b = read_pfm(rest[1])
    if (wa, ha) != (wb, hb):
        raise ValueError("size mismatch")
    rows = []
    for ra, rb in zip(a, b):
        out = bytearray(wa * 3)
        for x in range(wa):
            la = sum(ra[3 * x:3 * x + 3]) / 3.0
            lb = sum(rb[3 * x:3 * x + 3]) / 3.0
            d = (la - lb) * scale
            # red = a brighter, blue = b brighter
            out[3 * x + 0] = to_byte(max(d, 0.0))
            out[3 * x + 1] = 0
            out[3 * x + 2] = to_byte(max(-d, 0.0))
        rows.append(bytes(out))
    write_png(rest[2], wa, ha, rows)


def cmd_stats(args):
    w, h, px = read_pfm(args[0])
    vals = [v for row in px for v in row]
    finite = [v for v in vals if math.isfinite(v)]
    mean = sum(finite) / max(1, len(finite))
    print("%s %dx%d mean %.6f min %.6f max %.6f non-finite %d" % (
        args[0], w, h, mean, min(finite), max(finite), len(vals) - len(finite)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd, args = sys.argv[1], sys.argv[2:]
    {"topng": cmd_topng, "concat": cmd_concat, "diff": cmd_diff, "stats": cmd_stats}[cmd](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())

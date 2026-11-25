"""Shared helpers for the Star Frag 3D MD asset generator."""
import os, math
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
GEN = os.path.join(ROOT, 'src', 'gen')
RES = os.path.join(ROOT, 'res')
SHOTS = os.path.join(ROOT, 'shots')
for d in (GEN, RES, SHOTS):
    os.makedirs(d, exist_ok=True)

# ---------------------------------------------------------------- colour
LEVELS = [0, 36, 73, 109, 146, 182, 219, 255]   # Genesis 3-bit DAC steps (linear approx)

def snap(c):
    """24-bit -> nearest representable Genesis colour (still 24-bit)"""
    return tuple(LEVELS[min(7, max(0, int(round(v * 7 / 255.0))))] for v in c[:3])

def vdpcol(c):
    """24-bit -> VDP word 0000BBB0GGG0RRR0"""
    r, g, b = (min(7, max(0, int(round(v * 7 / 255.0)))) for v in c[:3])
    return (b << 9) | (g << 5) | (r << 1)

def cdist(a, b):
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    rm = (a[0] + b[0]) / 2
    return (2 + rm / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rm) / 256) * db * db

class Pal:
    """palette entries 1..15 (0 is transparent); nearest-colour lookup cached"""
    def __init__(self, cols):
        self.cols = [snap(c) for c in cols]
        while len(self.cols) < 15:
            self.cols.append((0, 0, 0))
        self.cache = {}
        self.allowed = list(range(1, 16))

    def nearest(self, c, allowed=None):
        key = (c[0] >> 2, c[1] >> 2, c[2] >> 2, None if allowed is None else tuple(allowed))
        r = self.cache.get(key)
        if r is None:
            best, bd = 1, 1e18
            for i in (allowed or self.allowed):
                d = cdist(c, self.cols[i - 1])
                if d < bd:
                    bd, best = d, i
            self.cache[key] = r = best
        return r

    def words(self):
        return [0] + [vdpcol(c) for c in self.cols[:15]]

    def rgb(self, i):
        return (0, 0, 0) if i == 0 else self.cols[i - 1]

def median_cut(pixels, n):
    """pixels: list of rgb -> n representative colours (median cut + 2 k-means passes)"""
    pixels = [snap(p) for p in pixels]
    if not pixels:
        return [(0, 0, 0)] * n
    boxes = [pixels]
    while len(boxes) < n:
        boxes.sort(key=lambda b: -max(max(p[i] for p in b) - min(p[i] for p in b) for i in range(3)) * len(b) ** 0.5)
        b = boxes.pop(0)
        if len(b) < 2:
            boxes.append(b)
            break
        ch = max(range(3), key=lambda i: max(p[i] for p in b) - min(p[i] for p in b))
        b.sort(key=lambda p: p[ch])
        m = len(b) // 2
        boxes += [b[:m], b[m:]]
    cents = [tuple(sum(p[i] for p in b) / len(b) for i in range(3)) for b in boxes]
    for _ in range(3):
        acc = [[0, 0, 0, 0] for _ in cents]
        for p in pixels[::max(1, len(pixels) // 20000)]:
            j = min(range(len(cents)), key=lambda k: cdist(p, cents[k]))
            a = acc[j]; a[0] += p[0]; a[1] += p[1]; a[2] += p[2]; a[3] += 1
        cents = [(a[0] / a[3], a[1] / a[3], a[2] / a[3]) if a[3] else c for a, c in zip(acc, cents)]
    out = []
    for c in cents:
        s = snap(c)
        if s not in out:
            out.append(s)
    while len(out) < n:
        out.append((0, 0, 0))
    return out[:n]

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

def quant_image(im, pal, dither=0.0, allowed=None, alpha_cut=128):
    """RGBA image -> 2D list of palette indices (0 = transparent)"""
    im = im.convert('RGBA')
    px = im.load()
    out = []
    for y in range(im.height):
        row = []
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < alpha_cut:
                row.append(0)
                continue
            if dither:
                t = (BAYER4[y & 3][x & 3] / 16.0 - 0.47) * dither
                r = min(255, max(0, r + t)); g = min(255, max(0, g + t)); b = min(255, max(0, b + t))
            row.append(pal.nearest((int(r), int(g), int(b)), allowed))
        out.append(row)
    return out

# ---------------------------------------------------------------- tiles
def tile_words(px, x0=0, y0=0):
    """8x8 block of indices -> 8 u32 rows (Genesis 4bpp)"""
    out = []
    for y in range(8):
        v = 0
        row = px[y0 + y] if 0 <= y0 + y < len(px) else None
        for x in range(8):
            c = 0
            if row is not None and 0 <= x0 + x < len(row):
                c = row[x0 + x] & 15
            v = (v << 4) | c
        out.append(v)
    return out

def c_u32(name, data, static=False):
    s = '%sconst u32 %s[%d] = {\n' % ('static ' if static else '', name, max(1, len(data)))
    for i in range(0, len(data), 8):
        s += '  ' + ','.join('0x%08X' % v for v in data[i:i + 8]) + ',\n'
    if not data:
        s += '  0\n'
    return s + '};\n'

def c_arr(name, data, typ='u8', per=24, static=False, align=0):
    al = ' __attribute__((aligned(%d)))' % align if align else ''
    s = '%sconst %s %s[%d]%s = {\n' % ('static ' if static else '', typ, name, max(1, len(data)), al)
    for i in range(0, len(data), per):
        s += '  ' + ','.join(str(v) for v in data[i:i + per]) + ',\n'
    if not data:
        s += '  0\n'
    return s + '};\n'

def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def write(path, text):
    with open(path, 'w') as f:
        f.write(text)

HDR = '/* generated by tools/gen.py - do not edit */\n#include <genesis.h>\n#include "gfxdefs.h"\n'

"""Arenas, wall textures (Brutal Doom), arena palettes, ray tables, compiled scalers."""
import math, io, os, random
from PIL import Image, ImageDraw, ImageFilter
from common import *
import bdlib
from wad import patch
from maps import ARENAS

TEXW, TEXH = 64, 32          # stored half texture: 64 columns x 32 rows (row 31 at the horizon)
NRAYS = 76                   # rays per view (2 px each, 152 px)
FOCAL = 143.0
HALF = 64                    # half view height
NTEXCOL = 11                 # arena palette entries 1..11 are wall colours
SKY_T, SKY_H, FLR_H, FLR_N = 12, 13, 14, 15

# ---------------------------------------------------------------- textures
def load_texture(name):
    if name == 'FORCE':
        return tex_force()
    path = bdlib.find(name)
    assert path, name
    d = open(path, 'rb').read()
    if d[:4] == b'\x89PNG':
        im = Image.open(io.BytesIO(bdlib.strip_chunks(d))).convert('RGBA')
    else:
        w, h, lo, to, px = patch(d)
        im = Image.new('RGBA', (w, h))
        for y in range(h):
            for x in range(w):
                c = px[y][x]
                if c is not None:
                    im.putpixel((x, y), bdlib.DPAL[c] + (255,))
    # transparent texels (fences) -> dark
    bg = Image.new('RGBA', im.size, (20, 20, 20, 255))
    bg.paste(im, (0, 0), im)
    im = bg.convert('RGB')
    w, h = im.size
    sq = im.crop((0, 0, w, min(w, h))) if h >= w else im
    big = sq.resize((TEXW, TEXW), Image.LANCZOS)
    return big.crop((0, 0, TEXW, TEXH))

def tex_force():
    im = Image.new('RGB', (TEXW, TEXH))
    px = im.load()
    for y in range(TEXH):
        for x in range(TEXW):
            v = 0.5 + 0.5 * math.sin((x * 0.35 + y * 0.6)) * math.cos(x * 0.13 - y * 0.21)
            r = int(20 + 60 * v); g = int(80 + 150 * v); b = int(160 + 95 * v)
            if (x * 7 + y * 3) % 23 == 0: r, g, b = 230, 255, 255
            px[x, y] = (r, g, b)
    for x in range(TEXW):
        px[x, 0] = (10, 10, 20)
        px[x, 1] = (60, 60, 70)
        px[x, 2] = (150, 150, 160) if x % 8 else (20, 20, 30)
    return im

def darken(im, f=0.62):
    return Image.eval(im, lambda v: int(v * f))

def gen_arenas():
    """-> C source (maps, objects, spawns, palettes, textures), and per-arena names"""
    src = HDR
    names = []
    tex_cache = {}
    tex_list = []          # (arena, texture name) -> global texture slot
    pal_words = []
    lvl_map_names, lvl_obj_names, lvl_spawn_names, lvl_tex_names = [], [], [], []
    texdata = []           # list of (cname, bytes)
    for li, (name, sky, flr, walls, rows) in enumerate(ARENAS):
        names.append(name)
        assert len(rows) <= 32 and all(len(r) == 32 for r in rows), name
        # ---- palette from this arena's textures
        wchars = sorted(walls)
        imgs = {c: load_texture(walls[c]) for c in wchars}
        pix = []
        for c in wchars:
            for im in (imgs[c], darken(imgs[c])):
                pix += list(im.getdata())
        cols = median_cut(pix, NTEXCOL)
        pal = Pal(cols + [sky[0], sky[1], flr[0], flr[1]])
        pal_words += pal.words()
        # ---- textures: slot per wall char (lit, dark)
        slot = {}
        tex_ptrs = []
        for k, c in enumerate(wchars):
            slot[c] = k + 1
            for vi, im in enumerate((imgs[c], darken(imgs[c]))):
                q = quant_image(im.convert('RGBA'), pal, dither=24, allowed=list(range(1, NTEXCOL + 1)))
                data = []
                for u in range(TEXW):              # column-major, doubled nibbles
                    for v in range(TEXH):
                        i = q[v][u]
                        data.append(i * 17)
                cn = 'tex_%d_%d_%d' % (li, k, vi)
                texdata.append((cn, data))
                tex_ptrs.append(cn)
            # preview
        prev = Image.new('RGB', (TEXW * len(wchars), TEXH * 4))
        for k, c in enumerate(wchars):
            for vi, im in enumerate((imgs[c], darken(imgs[c]))):
                q = quant_image(im.convert('RGBA'), pal, dither=24, allowed=list(range(1, NTEXCOL + 1)))
                for v in range(TEXH):
                    for u in range(TEXW):
                        prev.putpixel((k * TEXW + u, vi * TEXH * 2 + v), pal.rgb(q[v][u]))
                        prev.putpixel((k * TEXW + u, vi * TEXH * 2 + 2 * TEXH - 1 - v), pal.rgb(q[v][u]))
        prev.resize((prev.width * 2, prev.height * 2), Image.NEAREST).save(os.path.join(SHOTS, 'tex_arena%d.png' % li))
        assert len(wchars) <= 7
        src += 'static const u8 * const texp_%d[%d] = { %s };\n' % (li, 16, ', '.join(['0', '0'] + tex_ptrs + ['0'] * (14 - len(tex_ptrs))))
        lvl_tex_names.append('texp_%d' % li)
        # ---- map, objects, spawns
        m = [0] * 1024
        objs = []
        spawns = [None] * 6
        H = len(rows)
        for y in range(32):
            for x in range(32):
                ch = rows[y][x] if y < H else '#'
                if ch == '#' or ch.isupper():
                    m[y * 32 + x] = slot[ch]
                elif ch.isdigit():
                    spawns[int(ch) - 1] = (x, y)
                elif ch in ITEMCH:
                    objs += [ITEMCH[ch], x, y]
                else:
                    assert ch == '.', (name, ch)
        assert all(spawns), name
        check_arena(name, m, spawns, objs)
        src += c_arr('lvl_map_%d' % li, m, static=True)
        src += c_arr('lvl_obj_%d' % li, objs + [255], static=True)
        src += c_arr('lvl_spawn_%d' % li, sum(([x, y] for x, y in spawns), []), static=True)
    tex_src = HDR
    for cn, data in texdata:
        tex_src += c_arr(cn, data, per=32)
    src = src.replace(HDR, HDR + ''.join('extern const u8 %s[];\n' % cn for cn, _ in texdata), 1)
    n = len(ARENAS)
    src += 'const u8 * const lvl_maps[%d] = { %s };\n' % (n, ', '.join('lvl_map_%d' % i for i in range(n)))
    src += 'const u8 * const lvl_objs[%d] = { %s };\n' % (n, ', '.join('lvl_obj_%d' % i for i in range(n)))
    src += 'const u8 * const lvl_spawns[%d] = { %s };\n' % (n, ', '.join('lvl_spawn_%d' % i for i in range(n)))
    src += 'const u8 * const * const lvl_tex[%d] = { %s };\n' % (n, ', '.join(lvl_tex_names))
    src += c_arr('lvl_pals', pal_words, 'u16', per=16)
    src += 'const char * const level_names[%d] = { %s };\n' % (n, ', '.join(c_str(a[0]) for a in ARENAS))
    return src, tex_src, n

# item char -> object type (must match OT_* in inc/game.h)
ITEM_ORDER = ['h', 'a', 'm', 's', 'd', 'r', 'n', 'g', 'k', 'p', 'f', 'l', 'b', 'o']
ITEMCH = {c: i for i, c in enumerate(ITEM_ORDER)}

def check_arena(name, m, spawns, objs):
    for i in range(32):
        assert m[i] and m[31 * 32 + i] and m[i * 32] and m[i * 32 + 31], (name, 'open border')
    sx, sy = spawns[0]
    seen = {(sx, sy)}
    st = [(sx, sy)]
    while st:
        x, y = st.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 32 and 0 <= ny < 32 and not m[ny * 32 + nx] and (nx, ny) not in seen:
                seen.add((nx, ny)); st.append((nx, ny))
    for s in spawns:
        assert s in seen, (name, 'unreachable spawn', s)
    for i in range(0, len(objs), 3):
        assert (objs[i + 1], objs[i + 2]) in seen, (name, 'unreachable item', objs[i:i + 3])

# ---------------------------------------------------------------- tables
def gen_tables():
    src = HDR
    sin14 = [int(round(math.sin(i * 2 * math.pi / 1024) * 16384)) for i in range(1024)]
    sin14 = [min(16383, max(-16384, v)) for v in sin14]
    sin8 = [int(round(math.sin(i * 2 * math.pi / 1024) * 256)) for i in range(1024)]
    src += c_arr('sin14', sin14, 's16', per=16)
    src += c_arr('sin8', sin8, 's16', per=16)
    # 8.8 tiles travelled along the ray per unit step in x (index: angle)
    dd = []
    for i in range(1024):
        c = abs(math.cos(i * 2 * math.pi / 1024))
        dd.append(0x2000 if c < 1e-6 else min(0x2000, int(round(256 / c))))
    src += c_arr('ddtab', dd, 'u16', per=16)
    ro, rc = [], []
    for i in range(NRAYS):
        x = (i * 2 + 1) - NRAYS          # px from centre
        a = math.atan(x / FOCAL)
        ro.append(int(round(a * 1024 / (2 * math.pi))))
        rc.append(int(round(math.cos(a) * 256)))
    src += c_arr('rayoff', ro, 's8', per=19)
    src += c_arr('raycos', rc, 'u16', per=19)
    # projected half height (px) from perpendicular distance (8.8 tiles) >> 2
    hh = [255]
    for k in range(1, 2048):
        d = k * 4 / 256.0
        hh.append(min(255, int(round(FOCAL * 0.5 / d))))
    src += c_arr('hhtab', hh, 'u8', per=32)
    return src

# ---------------------------------------------------------------- scalers
def gen_scalers():
    """sc_tab[hh] -> routine writing rows max(0,64-hh)..63 of one byte column.
    a0 = texture column (32 bytes), a1 = row 0 of the destination byte column
    (stride 4).  Texels covering >= 3 rows are loaded once into d0."""
    s = ['/* generated by tools/gen.py: compiled wall scalers */', '    .text', '    .globl sc_tab']
    for hh in range(1, 256):
        y0 = max(0, HALF - hh)
        rows = [(y, ((y - (HALF - hh)) * TEXH) // hh) for y in range(y0, HALF)]
        s.append('    .align 2')
        s.append('sc_%d:' % hh)
        i = 0
        while i < len(rows):
            v = rows[i][1]
            j = i
            while j < len(rows) and rows[j][1] == v:
                j += 1
            run = rows[i:j]
            src = '(%a0)' if v == 0 else '%d(%%a0)' % v
            if len(run) >= 3:
                s.append('    move.b %s,%%d0' % src)
                for y, _ in run:
                    s.append('    move.b %%d0,%d(%%a1)' % (y * 4) if y else '    move.b %d0,(%a1)')
            else:
                for y, _ in run:
                    s.append('    move.b %s,%d(%%a1)' % (src, y * 4) if y else '    move.b %s,(%%a1)' % src)
            i = j
        s.append('    rts')
    s.append('    .align 2')
    s.append('sc_0:')
    s.append('    rts')
    s.append('    .section .rodata')
    s.append('    .align 2')
    s.append('sc_tab:')
    for hh in range(256):
        s.append('    .long sc_%d' % hh)
    return '\n'.join(s) + '\n'

# ---------------------------------------------------------------- sky / floor gradient tiles
def gen_gradient_tiles():
    """16 tiles: rows 0..7 of the sky (12 -> 13) and 8..15 of the floor (14 -> 15),
    ordered-dithered per pixel so the 3-bit colour steps don't band."""
    data = []
    for k in range(16):
        a, b = (SKY_T, SKY_H) if k < 8 else (FLR_H, FLR_N)
        px = []
        for y in range(8):
            t = ((k % 8) * 8 + y) / 63.0
            row = []
            for x in range(8):
                th = (BAYER4[y & 3][x & 3] + 0.5) / 16.0
                row.append(b if t > th else a)
            px.append(row)
        data += tile_words(px)
    return data

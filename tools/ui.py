"""Font, big HUD digits, HUD panel, divider, hit arrows, radar and the title screen."""
import math, os, random
from PIL import Image, ImageDraw, ImageFont
from common import *

GLYPHS = {
 '0': ["01110","10011","10101","10101","11001","10001","01110"],
 '1': ["00100","01100","00100","00100","00100","00100","01110"],
 '2': ["01110","10001","00001","00110","01000","10000","11111"],
 '3': ["11110","00001","00001","01110","00001","00001","11110"],
 '4': ["00010","00110","01010","10010","11111","00010","00010"],
 '5': ["11111","10000","11110","00001","00001","10001","01110"],
 '6': ["00110","01000","10000","11110","10001","10001","01110"],
 '7': ["11111","00001","00010","00100","01000","01000","01000"],
 '8': ["01110","10001","10001","01110","10001","10001","01110"],
 '9': ["01110","10001","10001","01111","00001","00010","01100"],
 'A': ["01110","10001","10001","11111","10001","10001","10001"],
 'B': ["11110","10001","10001","11110","10001","10001","11110"],
 'C': ["01110","10001","10000","10000","10000","10001","01110"],
 'D': ["11100","10010","10001","10001","10001","10010","11100"],
 'E': ["11111","10000","10000","11110","10000","10000","11111"],
 'F': ["11111","10000","10000","11110","10000","10000","10000"],
 'G': ["01110","10001","10000","10111","10001","10001","01111"],
 'H': ["10001","10001","10001","11111","10001","10001","10001"],
 'I': ["01110","00100","00100","00100","00100","00100","01110"],
 'J': ["00111","00010","00010","00010","00010","10010","01100"],
 'K': ["10001","10010","10100","11000","10100","10010","10001"],
 'L': ["10000","10000","10000","10000","10000","10000","11111"],
 'M': ["10001","11011","10101","10101","10001","10001","10001"],
 'N': ["10001","10001","11001","10101","10011","10001","10001"],
 'O': ["01110","10001","10001","10001","10001","10001","01110"],
 'P': ["11110","10001","10001","11110","10000","10000","10000"],
 'Q': ["01110","10001","10001","10001","10101","10010","01101"],
 'R': ["11110","10001","10001","11110","10100","10010","10001"],
 'S': ["01111","10000","10000","01110","00001","00001","11110"],
 'T': ["11111","00100","00100","00100","00100","00100","00100"],
 'U': ["10001","10001","10001","10001","10001","10001","01110"],
 'V': ["10001","10001","10001","10001","10001","01010","00100"],
 'W': ["10001","10001","10001","10101","10101","10101","01010"],
 'X': ["10001","10001","01010","00100","01010","10001","10001"],
 'Y': ["10001","10001","01010","00100","00100","00100","00100"],
 'Z': ["11111","00001","00010","00100","01000","10000","11111"],
 '!': ["00100","00100","00100","00100","00100","00000","00100"],
 '?': ["01110","10001","00001","00110","00100","00000","00100"],
 '.': ["00000","00000","00000","00000","00000","00000","00100"],
 ',': ["00000","00000","00000","00000","00000","00100","01000"],
 ':': ["00000","00100","00000","00000","00000","00100","00000"],
 '-': ["00000","00000","00000","01110","00000","00000","00000"],
 "'": ["00100","00100","01000","00000","00000","00000","00000"],
 '/': ["00001","00010","00010","00100","01000","01000","10000"],
 '<': ["00010","00100","01000","10000","01000","00100","00010"],
 '>': ["01000","00100","00010","00001","00010","00100","01000"],
 '+': ["00000","00100","00100","11111","00100","00100","00000"],
 '%': ["11001","11010","00010","00100","01000","01011","10011"],
 '=': ["00000","00000","11111","00000","11111","00000","00000"],
 '(': ["00010","00100","01000","01000","01000","00100","00010"],
 ')': ["01000","00100","00010","00010","00010","00100","01000"],
 '*': ["00000","10101","01110","11111","01110","10101","00000"],
}

def glyph(ch, fg, ol, shade=None):
    g = GLYPHS.get(ch)
    px = [[0] * 8 for _ in range(8)]
    if not g:
        return px
    on = {(x + 1, y) for y in range(7) for x in range(5) if g[y][x] == '1'}
    for (x, y) in on:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                xx, yy = x + dx, y + dy
                if 0 <= xx < 8 and 0 <= yy < 8 and (xx, yy) not in on:
                    px[yy][xx] = ol
    for (x, y) in on:
        px[y][x] = shade if (shade and y >= 4) else fg
    return px

def gen_fonts(gp):
    W = gp.nearest((240, 240, 240)); K = gp.nearest((0, 0, 0)); Y = gp.nearest((255, 219, 0))
    G = gp.nearest((170, 170, 170)); O = gp.nearest((255, 146, 0))
    data = []
    for fg, sh in ((W, G), (Y, O)):
        for c in range(32, 96):
            data += tile_words(glyph(chr(c), fg, K, sh))
    return data

def gen_bigdigits(gp):
    """13 glyphs (0-9, %, -, blank) 8x16, Doom-like red with dark shading; tiles: top, bottom"""
    R = gp.nearest((255, 36, 0)); D = gp.nearest((146, 0, 0)); K = gp.nearest((0, 0, 0)); Y = gp.nearest((255, 146, 0))
    data = []
    for ch in '0123456789%- ':
        px = [[0] * 8 for _ in range(16)]
        g = GLYPHS.get(ch)
        if g and ch != ' ':
            xm = [0, 1, 2, 2, 3, 4]
            on2 = {(ox + 1, oy + 1) for oy in range(14) for ox in range(6) if g[oy // 2][xm[ox]] == '1'}
            for (x, y) in on2:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < 8 and 0 <= yy < 16 and (xx, yy) not in on2:
                            px[yy][xx] = K
            for (x, y) in on2:
                px[y][x] = Y if y < 4 else (R if y < 11 else D)
        data += tile_words(px, 0, 0) + tile_words(px, 0, 8)
    return data

def metal_tile(gp, seed, base=(73, 73, 82), var=18):
    rnd = random.Random(seed)
    px = []
    for y in range(8):
        row = []
        for x in range(8):
            v = rnd.randint(-var, var) + (6 if (x + y * 3 + seed) % 7 == 0 else 0)
            row.append(gp.nearest(tuple(max(0, min(255, c + v)) for c in base)))
        px.append(row)
    return px

def gen_panel(gp):
    """HUD panel tiles (plane B): 0,1 metal; 2 bevel top; 3 bevel bottom; 4 rivet; 5 recess;
    6 divider-left; 7 divider-right; 8 panel-top-left corner..."""
    K = gp.nearest((0, 0, 0)); L = gp.nearest((182, 182, 190)); D = gp.nearest((36, 36, 40))
    M = gp.nearest((109, 109, 118))
    tiles = [metal_tile(gp, 1), metal_tile(gp, 2)]
    t = metal_tile(gp, 3)
    t[0] = [K] * 8; t[1] = [L] * 8; t[2] = [M] * 8
    tiles.append(t)
    t = metal_tile(gp, 4)
    t[7] = [K] * 8; t[6] = [D] * 8
    tiles.append(t)
    t = metal_tile(gp, 5)
    for (x, y, c) in ((3, 3, L), (4, 3, M), (3, 4, M), (4, 4, D)):
        t[y][x] = c
    tiles.append(t)
    rec = [[gp.nearest((20, 20, 30))] * 8 for _ in range(8)]
    tiles.append(rec)
    # divider (2 tiles wide): bevelled bar, darker in the middle
    for side in (0, 1):
        px = []
        for y in range(8):
            row = []
            for x in range(8):
                xx = side * 8 + x
                if xx in (0, 15): c = K
                elif xx in (1, 2): c = L
                elif xx in (13, 14): c = D
                else: c = M if (xx + y) % 5 else gp.nearest((146, 146, 150))
                row.append(c)
            px.append(row)
        tiles.append(px)
    data = []
    for t in tiles:
        data += tile_words(t)
    return data, len(tiles)

def gen_arrows(gp):
    """4 hit arrows (left, right, up, down), 16x16 sprites (column-major 2x2 tiles)"""
    R = gp.nearest((255, 36, 0)); K = gp.nearest((0, 0, 0)); Y = gp.nearest((255, 219, 0))
    data = []
    for d in range(4):
        im = Image.new('L', (16, 16), 0)
        dr = ImageDraw.Draw(im)
        pts = [(1, 8), (9, 1), (9, 5), (15, 5), (15, 11), (9, 11), (9, 15)]   # pointing left
        if d == 1: pts = [(16 - x, y) for x, y in pts]
        if d == 2: pts = [(y, x) for x, y in pts]
        if d == 3: pts = [(y, 16 - x) for x, y in pts]
        dr.polygon(pts, fill=2, outline=1)
        a = im.load()
        px = [[{0: 0, 1: K, 2: R}[a[x, y]] for x in range(16)] for y in range(16)]
        for tx in range(2):
            for ty in range(2):
                data += tile_words(px, tx * 8, ty * 8)
    return data

def gen_radar(gp):
    """32x32 radar background, 4x4 tiles row-major (plane A tiles)"""
    B = gp.nearest((0, 36, 0)); G = gp.nearest((0, 146, 36)); K = gp.nearest((0, 0, 0))
    D = gp.nearest((0, 73, 0))
    px = [[0] * 32 for _ in range(32)]
    for y in range(32):
        for x in range(32):
            r = math.hypot(x - 15.5, y - 15.5)
            if r < 15.5:
                c = B
                if abs(r - 15) < 0.8: c = G
                elif abs(r - 8) < 0.5: c = D
                elif x in (15, 16) or y in (15, 16): c = D
                px[y][x] = c
            elif r < 16.3:
                px[y][x] = K
    data = []
    for ty in range(4):
        for tx in range(4):
            data += tile_words(px, tx * 8, ty * 8)
    return data

# ---------------------------------------------------------------- title screen
LOGO_FONT = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'

def gen_title(skin_imgs=None):
    """320x224, 2 palettes: line 0 background (stars, nebula, text), line 1 the logo
    (metal ramp entries 9..15 rotated at run time for a sheen)."""
    W, H = 320, 224
    rnd = random.Random(7)
    bg = Image.new('RGB', (W, H), (0, 0, 0))
    px = bg.load()
    # nebula
    for y in range(H):
        for x in range(W):
            v = 0.5 + 0.5 * math.sin(x * 0.021 + math.sin(y * 0.03) * 2) * math.cos(y * 0.017 - x * 0.006)
            v = max(0.0, v - 0.45) * 1.8
            px[x, y] = (int(70 * v), int(10 * v), int(110 * v))
    stars = []
    for i in range(140):
        x, y = rnd.randrange(W), rnd.randrange(H)
        stars.append((x, y, rnd.randrange(4)))
    font = ImageFont.truetype(LOGO_FONT, 54)
    font2 = ImageFont.truetype(LOGO_FONT, 30)
    logo = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(logo)
    d.text((W // 2, 50), 'STAR FRAG', font=font, fill=255, anchor='mm')
    d.text((W // 2, 98), '3D', font=font, fill=255, anchor='mm')
    # background palette (line 0): 1 black, 2-4 nebula, 5-8 stars (twinkle), 10-15 text colours
    neb = median_cut([px[x, y] for y in range(0, H, 2) for x in range(0, W, 2)], 4)
    p0 = [(0, 0, 0)] + neb[:3] + [(73, 73, 109), (146, 146, 182), (219, 219, 255), (255, 255, 255)] + \
         [(36, 36, 36), (255, 219, 0), (255, 146, 0), (219, 219, 219), (146, 0, 0), (255, 36, 0), (109, 109, 109)]
    pal0 = Pal(p0[:15])
    # logo palette (line 1): 1 outline, 2 shadow, 9..15 metal ramp
    ramp = [(73, 73, 109), (109, 109, 146), (146, 146, 182), (182, 182, 219), (219, 219, 255), (255, 255, 255), (182, 219, 255)]
    p1 = [(0, 0, 0), (36, 0, 36), (146, 0, 0), (255, 36, 0), (255, 146, 0), (255, 219, 0), (73, 36, 0), (36, 36, 73)] + ramp
    pal1 = Pal(p1[:15])
    idx = [[0] * W for _ in range(H)]
    pl = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            c = px[x, y]
            idx[y][x] = pal0.nearest(c, allowed=[1, 2, 3, 4])
    for x, y, k in stars:
        idx[y][x] = 5 + k
    L = logo.load()
    # outline + bevel shading for the logo
    for y in range(H):
        for x in range(W):
            if L[x, y] > 110:
                # metal ramp by vertical position inside the letters + diagonal stripes
                t = ((y % 48) / 48.0 + (x % 23) / 90.0)
                k = int(t * 7) % 7
                idx[y][x] = 9 + k
                pl[y][x] = 1
            else:
                near = any(0 <= x + dx < W and 0 <= y + dy < H and L[x + dx, y + dy] > 110
                           for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
                if near:
                    idx[y][x] = 1 if not any(0 <= x + dx < W and 0 <= y + dy < H and L[x + dx, y + dy] > 110
                                             for dx in (-1, 0, 1) for dy in (-1, 0, 1)) else 3
                    pl[y][x] = 1
    # tile-level palette: a tile containing logo pixels uses line 1 (its background becomes outline colours)
    tiles, tmap, seen = [], [], {}
    for ty in range(H // 8):
        for tx in range(W // 8):
            use1 = any(pl[ty * 8 + y][tx * 8 + x] for y in range(8) for x in range(8))
            blk = []
            for y in range(8):
                row = []
                for x in range(8):
                    X, Y = tx * 8 + x, ty * 8 + y
                    v = idx[Y][X]
                    if use1 and not pl[Y][X]:
                        v = 2 if v in (1, 2, 3, 4) else 8   # dark backdrop inside logo tiles
                    row.append(v)
                blk.append(row)
            w = tuple(tile_words(blk))
            key = (w, use1)
            if w not in seen:
                seen[w] = len(tiles)
                tiles.append(w)
            tmap.append(seen[w] | ((1 if use1 else 0) << 13))
    data = [v for t in tiles for v in t]
    # preview
    prev = Image.new('RGB', (W, H))
    for ty in range(H // 8):
        for tx in range(W // 8):
            e = tmap[ty * (W // 8) + tx]
            t = tiles[e & 0x7FF]
            p = pal1 if e >> 13 else pal0
            for y in range(8):
                for x in range(8):
                    prev.putpixel((tx * 8 + x, ty * 8 + y), p.rgb((t[y] >> (28 - 4 * x)) & 15))
    prev.save(os.path.join(SHOTS, 'title_preview.png'))
    return data, tmap, pal0.words() + pal1.words(), len(tiles)

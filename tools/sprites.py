"""Pre-scaled sprite graphics: player skins (150 Doomguy Skins WAD), HUD faces,
items / projectiles / effects and first-person weapons (Brutal Doom pk3)."""
import math, os
from PIL import Image, ImageDraw, ImageFilter
from common import *
import bdlib
from wad import Wad, patch
from skinlib import load_pal, skin_index, find_frame, to_rgba

WADPATH = os.environ.get('SKINWAD', '/home/claude/untrusted/skins/150skins.wad')
DOOMPAL = bdlib.DPAL

# size ladder: player (96 units) from 8 to ~88 px
NLEVELS = 13
S0 = 8.0 / 96
RATIO = (88.0 / 8) ** (1.0 / (NLEVELS - 1))
SCALES = [S0 * RATIO ** i for i in range(NLEVELS)]
UNIT = 96.0 / 56          # world units per Doom sprite pixel (a Doom player is 56 px tall)
PLAYER_H = 96

# (wad skin, display name, face prefix, credit)
SKINS = [
    ('Base1', 'MARINE', 'BLM', 'Doomkid'),
    ('BaseFemale', 'RANGER', 'GAL', 'Virtue'),
    ('Phobos', 'PHOBOS', 'PHB', 'Rottking'),
    ('Cyborg', 'CYBORG', 'CYB', 'Cory Whittle'),
    ('Hazmat', 'HAZMAT', 'HLM', 'Enjay / Scalliano'),
    ('Stealth', 'STEALTH', 'SHA', '(as credited in the pack)'),
    ('DoomSlayer', 'SLAYER', 'DSL', '(as credited in the pack)'),
    ('Cowboy', 'COWBOY', 'CIG', '(as credited in the pack)'),
    ('Snake', 'SNAKE', 'FRE', '(as credited in the pack)'),
    ('Santa', 'SANTA', 'SSN', '(as credited in the pack)'),
    ('Sub-Zero', 'FROST', 'SBZ', '(as credited in the pack)'),
    ('PunkMarine', 'PUNK', 'PNK', '(as credited in the pack)'),
]
# per skin: walk A rot 1..5, walk C rot 1..5, attack F1, pain G1, dying H J L, dead N
SKIN_FRAMES = [('A', r) for r in range(1, 6)] + [('C', r) for r in range(1, 6)] + \
              [('F', 1), ('G', 1), ('H', 0), ('J', 0), ('L', 0), ('N', 0)]
NSKINGFX = len(SKIN_FRAMES)

# items in ITEM_ORDER (maps.py), then projectiles and effects.  (name, sprite lumps, world-unit scale)
ITEMS = [
    ('MEDKIT', ['MEDKA0']), ('ARMOR', ['ARM1A0']), ('AMMO', ['BPAKA0']),
    ('W_SHOTGUN', ['SHTCA0']), ('W_SSG', ['SGN2A0']), ('W_RIFLE', ['RIFLA0']), ('W_MINIGUN', ['MGUNA0']),
    ('W_GREN', ['GLAPA0']), ('W_ROCKET', ['RLAUA0']), ('W_PLASMA', ['PLASA0']), ('W_FLAME', ['FLMTF0']),
    ('W_RAIL', ['SRCGA0']), ('W_BFG', ['BFUGA0']), ('POWER', ['MEGAA0']),
]
EXTRA = [
    ('PLASMA', None), ('GRENADE', ['GRNDA0']), ('ROCKET', ['MISLA1']), ('FLAME0', ['FLMEA0']), ('FLAME1', ['FIR1A0']),
    ('BFGBALL', ['BFS1A0']), ('EXPLO0', ['EXPLA0']), ('EXPLO1', ['EXPLC0']), ('EXPLO2', ['EXPLE0']),
    ('PUFF', None), ('SPARK', None), ('TFOG0', ['TFOGA0']), ('TFOG1', ['TFOGD0']), ('BLOOD0', ['BLUDA0']),
    ('BLOOD1', ['BLUDB0']), ('MEGA1', ['MEGAB0']),
]
# world height overrides (units) for things whose natural size is wrong for us
WORLD_H = {'PLASMA': 22, 'BFGBALL': 52, 'EXPLO0': 80, 'EXPLO1': 88, 'EXPLO2': 92, 'PUFF': 18, 'SPARK': 18,
           'FLAME0': 40, 'FLAME1': 40, 'TFOG0': 84, 'TFOG1': 92, 'ROCKET': 24, 'GRENADE': 16,
           'BLOOD0': 26, 'BLOOD1': 26}

WEAPONS = [  # name, ready, fire1, fire2, hud name
    ('PISTOL', 'PISTF0', 'PISTA0', 'PISTB0'),
    ('SHOTGUN', 'SHTNA0', 'SHTAA0', 'SHTAB0'),
    ('SUPER SG', 'SHTZA0', 'SHOFA0', 'SHOFB0'),
    ('RIFLE', 'RIFGA0', 'RIFFA0', 'RIFFC0'),
    ('MINIGUN', 'CHAGA0', 'CHAFA0', 'CHAFB0'),
    ('GRENADES', 'GLANA0', 'GLAFA0', 'GLAFB0'),
    ('ROCKETS', 'RLNGA0', 'RLNFA0', 'RLNFB0'),
    ('PLASMA', 'PLSNA0', 'PLSTA0', 'PLSTB0'),
    ('FLAMER', 'FLMTA0', 'FLMTD0', 'FLMTE0'),
    ('RAILGUN', 'RAILA0', 'RAIFA0', 'RAIFB0'),
    ('BFG 9000', 'BFGNA0', 'BFGAA0', 'BFGAB0'),
]
WPN_MAXTILES = 56
VIEW_W, VIEW_H = 152, 128

# ---------------------------------------------------------------- image helpers
def doom_rgba(p, mirror=False):
    im, lo, to = to_rgba(p, DOOMPAL, mirror)
    return im, lo, to

def scale_img(im, lo, to, f):
    """scale an anchored RGBA image by f -> (img, left, top) relative to bottom-centre anchor"""
    w = max(1, int(round(im.width * f)))
    h = max(1, int(round(im.height * f)))
    # premultiplied resize to avoid dark fringes
    big = im.resize((w, h), Image.LANCZOS if f < 1 else Image.NEAREST)
    left = int(round(-lo * f))
    top = int(round(-to * f))
    return big, left, top

def to_strips(q, left, top):
    """index image -> (left, top, ncols, strips[(row0, n)], tile words) ; left aligned to 8 px"""
    h = len(q); w = len(q[0]) if h else 0
    ncols = (w + 7) // 8
    strips, words = [], []
    for c in range(ncols):
        rows = [y for y in range(h) if any(q[y][x] for x in range(c * 8, min(w, c * 8 + 8)))]
        if not rows:
            strips.append((0, 0))
            continue
        r0, r1 = rows[0] // 8, rows[-1] // 8
        strips.append((r0, r1 - r0 + 1))
        for r in range(r0, r1 + 1):
            words += tile_words(q, c * 8, r * 8)
    return left, top, ncols, strips, words

class GfxOut:
    """collects pre-scaled graphics into C"""
    def __init__(self):
        self.src = []
        self.names = []
        self.total = 0
        self.maxtiles = 0

    def add(self, gname, levels):
        """levels: list (NLEVELS) of (left, top, ncols, strips, words) or None"""
        ents = []
        for li, lv in enumerate(levels):
            if lv is None or not lv[4]:
                ents.append('{0,0,0,0,0,0}')
                continue
            left, top, ncols, strips, words = lv
            tn, sn = 'gt_%s_%d' % (gname, li), 'gs_%s_%d' % (gname, li)
            self.src.append(c_u32(tn, words, static=True))
            self.src.append(c_arr(sn, sum(([a, b] for a, b in strips), []), static=True))
            nt = len(words) // 8
            assert nt < 256 and ncols < 32, (gname, li, nt, ncols)
            self.maxtiles = max(self.maxtiles, nt)
            ents.append('{%s,%s,%d,%d,%d,%d}' % (tn, sn, left, top, ncols, nt))
            self.total += len(words) * 4 + len(strips) * 2 + 12
        self.src.append('static const GfxImg gi_%s[%d] = {%s};\n' % (gname, NLEVELS, ','.join(ents)))
        self.names.append(gname)
        return len(self.names) - 1

    def emit(self):
        s = HDR + ''.join(self.src)
        s += 'const GfxImg * const gfx_tab[%d] = { %s };\n' % (len(self.names), ', '.join('gi_' + n for n in self.names))
        return s

def levels_for(im, lo, to, units, pal, dither=0):
    """render an RGBA (anchored) image of `units` world height at every size level"""
    out = []
    for s in SCALES:
        f = units * s / im.height
        if im.height * f < 2.5:
            out.append(None)
            continue
        sim, left, top = scale_img(im, lo, to, f)
        q = quant_image(sim, pal, dither=dither if sim.height > 24 else 0, alpha_cut=110)
        # pad left to keep the image's x position exact on the 8 px grid
        out.append(to_strips(q, left, top))
    return out

# ---------------------------------------------------------------- procedural small things
def blob(size, cols, glow=True):
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    n = len(cols)
    for i, c in enumerate(cols):
        r = size / 2 * (1 - i / n)
        d.ellipse([size / 2 - r, size / 2 - r, size / 2 + r - 1, size / 2 + r - 1], fill=c + (255,))
    return im

def proc_image(name):
    if name == 'PLASMA':
        im = blob(24, [(20, 60, 200), (60, 140, 255), (160, 220, 255), (255, 255, 255)])
    elif name == 'PUFF':
        im = blob(16, [(90, 90, 90), (150, 150, 150), (210, 210, 210)])
    elif name == 'SPARK':
        im = blob(16, [(30, 80, 220), (120, 200, 255), (255, 255, 255)])
    else:
        raise KeyError(name)
    return im, im.width // 2, im.height + 12

def load_lump(names):
    for n in names:
        r = bdlib.load_sprite(n)
        if r:
            im, lo, to = r
            bb = im.getbbox()
            if bb:
                # crop to content, adjust offsets
                im2 = im.crop(bb)
                return im2, lo - bb[0], to - bb[1]
    raise KeyError(names)

# ---------------------------------------------------------------- general palette (line 1)
def general_palette():
    pix = []
    for n, lumps in ITEMS + [e for e in EXTRA if e[1]]:
        im = load_lump(lumps)[0]
        pix += [p[:3] for p in im.getdata() if p[3] > 128]
    for w in WEAPONS:
        for fr in w[1:4]:
            r = bdlib.load_sprite(fr)
            if r:
                d = [p[:3] for p in r[0].getdata() if p[3] > 128]
                pix += d * 2
    # keep HUD / font colours available
    fixed = [(0, 0, 0), (219, 219, 219), (255, 36, 0), (255, 219, 0), (36, 73, 182), (36, 182, 36), (219, 146, 109)]
    cols = median_cut(pix, 15 - len(fixed))
    allc = fixed + [c for c in cols if c not in fixed]
    return Pal(allc[:15])

# ---------------------------------------------------------------- skins
def skin_frames(wd, sk):
    out = []
    for fr, rot in SKIN_FRAMES:
        p, mir = find_frame(wd, sk, fr, rot)
        if p is None and rot:
            p, mir = find_frame(wd, sk, fr, 1)
        if p is None:   # death frame missing: nearest earlier letter
            for alt in 'NMLKJIH':
                p, mir = find_frame(wd, sk, alt, 0)
                if p: break
        assert p is not None, (sk.get('name'), fr, rot)
        out.append(doom_rgba(p, mir))
    return out

def face_imgs(wd, prefix):
    imgs = []
    for nm in ('ST01', 'ST11', 'ST21', 'ST31', 'ST41', 'OUCH0', 'EVL0', 'DEAD0'):
        lump = wd.get(prefix + nm)
        assert lump is not None, prefix + nm
        p = patch(lump)
        im, lo, to = doom_rgba(p)
        can = Image.new('RGBA', (24, 32), (0, 0, 0, 0))
        can.paste(im, ((24 - im.width) // 2, max(0, 31 - im.height)), im)
        imgs.append(can)
    return imgs

def gen_skins(out, wd):
    sks = {s.get('name'): s for s in skin_index(wd)}
    pal_words = []
    face_src = []
    gallery = []
    for si, (wname, disp, face, credit) in enumerate(SKINS):
        sk = sks[wname]
        frames = skin_frames(wd, sk)
        faces = face_imgs(wd, face)
        pix = []
        for im, lo, to in frames:
            pix += [p[:3] for p in im.getdata() if p[3] > 128]
        for f in faces:
            pix += [p[:3] for p in f.getdata() if p[3] > 128] * 3
        pal = Pal(median_cut(pix, 15))
        pal_words += pal.words()
        for k, (im, lo, to) in enumerate(frames):
            # Doom player sprites: 56 px tall standing; dead frames keep their real size
            out.add('S%d_%d' % (si, k), levels_for(im, lo, to, im.height * UNIT, pal, dither=10))
        fw = []
        for f in faces:
            q = quant_image(f, pal)
            for tx in range(3):            # column-major 3x4 tiles (plane B writes them row by row from a map)
                pass
            for ty in range(4):
                for tx in range(3):
                    fw += tile_words(q, tx * 8, ty * 8)
        face_src.append(c_u32('face_%d' % si, fw, static=True))
        gallery.append((frames, pal))
    src = ''.join(face_src)
    src += 'const u32 * const face_data[%d] = { %s };\n' % (len(SKINS), ', '.join('face_%d' % i for i in range(len(SKINS))))
    src += c_arr('skin_pals', pal_words, 'u16', per=16)
    src += 'const char * const skin_names[%d] = { %s };\n' % (len(SKINS), ', '.join(c_str(s[1]) for s in SKINS))
    # gallery preview
    W = 8 * 60
    g = Image.new('RGB', (W, 70 * len(SKINS)), (40, 40, 60))
    for si, (frames, pal) in enumerate(gallery):
        for k in range(8):
            im = frames[k][0]
            g.paste(im, (k * 60 + 2, si * 70 + 68 - im.height), im)
    g.save(os.path.join(SHOTS, 'skins_src.png'))
    return src

# ---------------------------------------------------------------- items etc.
def gen_items(out, gpal):
    for n, lumps in ITEMS + EXTRA:
        if lumps is None:
            im, lo, to = proc_image(n)
        else:
            im, lo, to = load_lump(lumps)
        units = WORLD_H.get(n, im.height * UNIT)
        if n in ('MEDKIT', 'ARMOR', 'AMMO', 'POWER', 'MEGA1') or n.startswith('W_'):
            lo, to = im.width / 2.0, im.height       # sit on the floor, centred
        else:
            lo, to = im.width / 2.0, im.height       # centred; lift comes from the game's z table
        out.add(n, levels_for(im, lo, to, units, gpal, dither=8))

# ---------------------------------------------------------------- first-person weapons
def gen_weapons(gpal):
    """each weapon: 3 frames of hardware-sprite pieces (x, y, w, h tiles, tile offset)"""
    src = ''
    frames_c = []
    preview = Image.new('RGB', (VIEW_W * 3, VIEW_H * len(WEAPONS)), (50, 50, 80))
    for wi, w in enumerate(WEAPONS):
        imgs = []
        for fr in w[1:4]:
            r = bdlib.load_sprite(fr)
            assert r, fr
            imgs.append(r)
        base = 0.6
        while True:
            built = [build_wframe(im, lo, to, base, gpal) for im, lo, to in imgs]
            if max(len(f[1]) // 8 for f in built) <= WPN_MAXTILES:
                break
            base *= 0.95
        for fi, (pieces, words, grid, x0, y0, tw, th) in enumerate(built):
            nt = len(words) // 8
            nm = 'wp_%d_%d' % (wi, fi)
            src += c_u32(nm + '_t', words, static=True)
            src += c_arr(nm + '_p', pieces, 's16', static=True)
            frames_c.append('{%s_t,%s_p,%d,%d}' % (nm, nm, len(pieces) // 5, nt))
            for y in range(th * 8):
                for x in range(tw * 8):
                    if grid[y][x] and y0 + y < VIEW_H:
                        preview.putpixel((fi * VIEW_W + x0 + x, wi * VIEW_H + y0 + y), gpal.rgb(grid[y][x]))
    preview.save(os.path.join(SHOTS, 'weapons_preview.png'))
    src += 'const WpnFrame wpn_frames[%d] = {%s};\n' % (len(frames_c), ','.join(frames_c))
    src += 'const char * const weapon_names[%d] = { %s };\n' % (len(WEAPONS), ', '.join(c_str(w[0]) for w in WEAPONS))
    return src

def build_wframe(im, lo, to, base, gpal):
    if True:
        if True:
            # Doom screen (320 x 168 view) -> our 152 x 128 view, bottom-aligned
            can = Image.new('RGBA', (320, 200), (0, 0, 0, 0))
            can.paste(im, (-lo, -to), im)
            can = can.crop((0, 0, 320, 168))
            sw, sh = int(320 * base), int(168 * base)
            sc = can.resize((sw, sh), Image.LANCZOS)
            ox = VIEW_W // 2 - sw // 2
            oy = VIEW_H - sh
            q = quant_image(sc, gpal, dither=12, alpha_cut=120)
            # bbox in view coords, snapped to 8
            xs = [x for y in range(sh) for x in range(sw) if q[y][x]]
            ys = [y for y in range(sh) for x in range(sw) if q[y][x]]
            x0 = (min(xs) + ox) // 8 * 8; x1 = max(xs) + ox
            y0 = (min(ys) + oy) // 8 * 8; y1 = min(VIEW_H - 1, max(ys) + oy)
            tw = (x1 - x0) // 8 + 1; th = (y1 - y0) // 8 + 1
            grid = [[0] * (tw * 8) for _ in range(th * 8)]
            for y in range(sh):
                for x in range(sw):
                    if q[y][x]:
                        gx, gy = x + ox - x0, y + oy - y0
                        if 0 <= gy < th * 8 and 0 <= gx < tw * 8:
                            grid[gy][gx] = q[y][x]
            pieces, words = [], []
            for py in range(0, th, 4):
                for px_ in range(0, tw, 4):
                    pw, ph = min(4, tw - px_), min(4, th - py)
                    # trim empty tile rows/cols of the piece
                    used = [(cx, cy) for cy in range(ph) for cx in range(pw)
                            if any(grid[(py + cy) * 8 + yy][(px_ + cx) * 8 + xx] for yy in range(8) for xx in range(8))]
                    if not used: continue
                    cx0 = min(u[0] for u in used); cx1 = max(u[0] for u in used)
                    cy0 = min(u[1] for u in used); cy1 = max(u[1] for u in used)
                    off = len(words) // 8
                    for cx in range(cx0, cx1 + 1):        # sprite tiles are column-major
                        for cy in range(cy0, cy1 + 1):
                            words += tile_words(grid, (px_ + cx) * 8, (py + cy) * 8)
                    pieces += [x0 + (px_ + cx0) * 8, y0 + (py + cy0) * 8, cx1 - cx0 + 1, cy1 - cy0 + 1, off]
            return pieces, words, grid, x0, y0, tw, th

def gen_all():
    wd = Wad(WADPATH)
    gpal = general_palette()
    out = GfxOut()
    gen_items(out, gpal)
    nitems = len(out.names)
    skin_src = gen_skins(out, wd)
    wsrc = gen_weapons(gpal)
    src = out.emit()
    src += skin_src
    src += wsrc
    src += c_arr('gen_pal', gpal.words(), 'u16', per=16)
    sizes = []
    for s in SCALES:
        sizes.append(int(round(s * 64 * 16)))    # scale relative to hh: px = units*hh/64 -> level when hh*16 ~ size
    src += c_arr('size_q', sizes, 'u16', per=16)
    info = dict(nitems=nitems, gskin=nitems, total=out.total, maxtiles=out.maxtiles, gpal=gpal)
    return src, info

"""Brutal Doom pk3 reader (data only): finds sprite / sound lumps by name in the
extracted pk3 tree and decodes PNG (with grAb offsets) or Doom-format patches."""
import os, struct, zlib, io
from PIL import Image
from wad import patch

HERE = os.path.dirname(os.path.abspath(__file__))
PK3 = os.environ.get('BDPK3', '/home/claude/untrusted/bd/pk3')

def load_pal(path=os.path.join(HERE, 'doompal.lmp')):
    d = open(path, 'rb').read()
    return [tuple(d[i * 3:i * 3 + 3]) for i in range(256)]

DPAL = load_pal()
_index = None

def index():
    global _index
    if _index is None:
        _index = {}
        for root, dirs, files in os.walk(PK3):
            for f in files:
                stem = os.path.splitext(f)[0].upper()
                full = os.path.join(root, f)
                # prefer files under SPRITES / SOUNDS, first found wins otherwise
                if stem not in _index or ('SPRITES' in full and 'SPRITES' not in _index[stem]):
                    _index[stem] = full
    return _index

def find(name):
    return index().get(name.upper())

def png_grab(data):
    """grAb chunk offsets (x, y) or None"""
    p = 8
    while p < len(data):
        n, t = struct.unpack_from('>I4s', data, p)
        if t == b'grAb':
            return struct.unpack_from('>ii', data, p + 8)
        p += 12 + n
    return None

def strip_chunks(data):
    """drop ancillary chunks PIL can't cope with (grAb/alPh before PLTE)"""
    out = data[:8]; p = 8
    while p < len(data):
        n, t = struct.unpack_from('>I4s', data, p)
        if t in (b'IHDR', b'PLTE', b'tRNS', b'IDAT', b'IEND'):
            out += data[p:p + 12 + n]
        p += 12 + n
    return out

def load_sprite(name):
    """-> (RGBA image, leftoffset, topoffset) or None"""
    path = find(name)
    if not path:
        return None
    data = open(path, 'rb').read()
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        try:
            im = Image.open(io.BytesIO(strip_chunks(data))); im.load()
        except Exception:
            return None
        if im.mode == 'P' and 'transparency' not in im.info:
            # Doom-palette PNG without transparency: treat cyan (0,255,255) as clear
            im = im.convert('RGBA')
            px = im.load()
            for y in range(im.height):
                for x in range(im.width):
                    if px[x, y][:3] == (0, 255, 255):
                        px[x, y] = (0, 0, 0, 0)
        else:
            im = im.convert('RGBA')
        g = png_grab(data)
        if g is None:
            g = (0, 0) if im.width >= 320 else (im.width // 2, im.height)
        return im, g[0], g[1]
    try:
        w, h, lo, to, px = patch(data)
    except Exception:
        return None
    if w == 0 or w > 2048 or h > 2048:
        return None
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            c = px[y][x]
            if c is not None:
                im.putpixel((x, y), DPAL[c] + (255,))
    return im, lo, to

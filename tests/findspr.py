"""findspr.py PPM... : count pixels in the P1 view whose colour only the P2 skin palette has"""
import sys, re
from PIL import Image
src = open('src/gen/gfx.c').read()
def pal_words(name, idx):
    m = re.search(r'const u16 %s\[\d+\] = \{([^}]*)\}' % name, src)
    v = [int(x) for x in m.group(1).replace('\n', '').split(',') if x.strip()]
    return v[idx * 16:(idx + 1) * 16]
def rgb(w):  # VDP word -> 8-bit as GPGX renders (approx, compare on 3-bit)
    return ((w >> 1) & 7, (w >> 5) & 7, (w >> 9) & 7)
skin = int(sys.argv[1]); files = sys.argv[2:]
sk = set(rgb(w) for w in pal_words('skin_pals', skin)[1:])
ar = open('src/gen/arenas.c').read()
m = re.search(r'const u16 lvl_pals\[\d+\] = \{([^}]*)\}', ar)
lv = [int(x) for x in m.group(1).replace('\n', '').split(',') if x.strip()][:16]
other = set(rgb(w) for w in lv) | set(rgb(w) for w in pal_words('gen_pal', 0))
only = sk - other
for f in files:
    im = Image.open(f).convert('RGB'); px = im.load()
    n = 0
    for y in range(128):
        for x in range(152):
            r, g, b = px[x, y]
            if (r >> 5, g >> 5, b >> 5) in only: n += 1
    print(f, n)

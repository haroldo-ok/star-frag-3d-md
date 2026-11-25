/* sprites.c - opponents, items, projectiles and effects as PRE-SCALED hardware
 * sprites in both views, first-person weapons and the hit arrows.
 *
 * Every graphic exists in ROM at NLEVELS_SZ sizes, as 8-px wide strips of tiles.
 * Each frame every object is projected into both views, the nearest size is
 * picked and each strip becomes one or more 8 x (8..32) sprites.  A strip is
 * hidden when the wall at its centre ray is nearer than the object.  Tiles come
 * from a VRAM cache shared by both views; misses are uploaded through the
 * VBlank job ring, within a per-frame budget.
 */
#include "game.h"

u16 sat_buf[2][80 * 4];
static u16 *sat;
static u8 nspr;

/* per object type: world height (1/128 wall) is baked into the graphics; z lift */
static const u8 type_z[NOBJTYPES] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* items */
                                      40, 8, 44, 30, 36,                          /* projectiles */
                                      10, 44, 44, 0, 50 };                        /* effects */

/* ------------------------------------------------------------ tile cache */
#define NCE 28
static u8 c_g[NCE], c_l[NCE], c_n[NCE];
static u16 c_t[NCE];
static u16 c_last[NCE];
static u8 own[CACHE_TILES];
static s16 budget;

void sprites_init(void)
{
    u16 i;
    for (i = 0; i < NCE; i++) c_g[i] = 0xFF;
    for (i = 0; i < CACHE_TILES; i++) own[i] = 0xFF;
    pl[0].cur_hand = pl[1].cur_hand = 0xFF;
}

static s16 find_run(u8 n)
{
    u16 i, run = 0;
    for (i = 0; i < CACHE_TILES; i++) {
        if (own[i] == 0xFF) {
            if (++run == n) return i + 1 - n;
        } else run = 0;
    }
    return -1;
}

static void evict(u8 e)
{
    u16 i;
    for (i = 0; i < c_n[e]; i++) own[c_t[e] + i] = 0xFF;
    c_g[e] = 0xFF;
}

/* evicts the least recently used entry not shown in the last 2 frames */
static u8 evict_lru(void)
{
    u8 e, best = 0xFF;
    u16 age = 0;
    for (e = 0; e < NCE; e++) {
        if (c_g[e] != 0xFF) {
            u16 a = frame_no - c_last[e];
            if (a >= 2 && a >= age) { age = a; best = e; }
        }
    }
    if (best == 0xFF) return 0;
    evict(best);
    return 1;
}

/* VRAM tile of graphic g at level *pl_; may switch to a cached neighbour level
   when the upload budget is spent.  Returns 0 when nothing can be shown. */
static u16 cache_get(u8 g, u8 *plv)
{
    u8 e, l = *plv, best = 0xFF, bd = 0xFF, n;
    s16 t;
    const GfxImg *img;
    for (e = 0; e < NCE; e++) {
        if (c_g[e] == g) {
            u8 d;
            if (c_l[e] == l) { c_last[e] = frame_no; return T_CACHE + c_t[e]; }
            d = c_l[e] > l ? c_l[e] - l : l - c_l[e];
            if (d < bd) { bd = d; best = e; }
        }
    }
    img = &gfx_tab[g][l];
    n = img->ntiles;
    if (!n) return 0;
    if (n > budget || n > CACHE_TILES) {
        if (best != 0xFF) { *plv = c_l[best]; c_last[best] = frame_no; return T_CACHE + c_t[best]; }
        return 0;
    }
    for (e = 0; e < NCE; e++) if (c_g[e] == 0xFF) break;
    if (e == NCE) {
        if (!evict_lru()) return 0;
        for (e = 0; e < NCE; e++) if (c_g[e] == 0xFF) break;
    }
    for (;;) {
        t = find_run(n);
        if (t >= 0) break;
        if (!evict_lru()) {
            if (best != 0xFF) { *plv = c_l[best]; c_last[best] = frame_no; return T_CACHE + c_t[best]; }
            return 0;
        }
    }
    c_g[e] = g; c_l[e] = l; c_n[e] = n; c_t[e] = t; c_last[e] = frame_no;
    {
        u16 i;
        for (i = 0; i < n; i++) own[t + i] = e;
    }
    vq_vram(img->tiles, (T_CACHE + t) * 32, n * 32);
    budget -= n;
    return T_CACHE + t;
}

/* ------------------------------------------------------------ projection */
static inline s32 muls16(s16 a, s16 b)
{
    s32 r = a;
    __asm__("muls.w %1,%0" : "+d"(r) : "dm"(b) : "cc");
    return r;
}

/* (dx,dy) 8.8 world offset -> depth ahead and lateral offset (right +), 8.8 */
void project(u16 a, s16 dx, s16 dy, s16 *depth, s16 *lat)
{
    s16 ca = sin14[(a + 256) & 1023], sa = sin14[a & 1023];
    *depth = (s16)((muls16(dx, ca) + muls16(dy, sa)) >> 14);
    *lat = (s16)((muls16(dy, ca) - muls16(dx, sa)) >> 14);
}

/* ------------------------------------------------------------ hardware sprites */
static inline void add_spr(s16 x, s16 y, u8 w, u8 h, u16 attr)
{
    u16 *s;
    if (nspr >= 80) return;
    s = sat + nspr * 4;
    s[0] = (y + 128) & 0x3FF;
    s[1] = (((w - 1) << 2 | (h - 1)) << 8) | (nspr + 1);
    s[2] = attr;
    s[3] = (x + 128) & 0x1FF;
    nspr++;
}

/* ------------------------------------------------------------ visibility lists */
#define NVIS 16
typedef struct { u8 g, lvl, flip, pal; s16 cx, bot; u16 depth; } Vis;
static Vis vis[2][NVIS];
static u8 nvis[2];

static void consider(u8 v, s16 dx, s16 dy, u8 g, u8 z, u8 flip, u8 pal)
{
    Player *V = &pl[v];
    s16 depth, lat, sx;
    u8 hh, lvl, j, n;
    u16 hq;
    project(V->a, dx, dy, &depth, &lat);
    if (depth < 48) return;
    {
        s32 q = muls16(lat, 143);
        if ((q < 0 ? -q : q) > (s32)depth * 120) return;     /* far outside the view */
        sx = (s16)(q / depth);
    }
    hh = hhtab[((u16)depth >> 2) > 2047 ? 2047 : ((u16)depth >> 2)];
    hq = (u16)hh << 4;
    lvl = 0;
    while (lvl < NLEVELS_SZ - 1 && size_q[lvl + 1] <= hq + (hq >> 3)) lvl++;
    if (!gfx_tab[g][lvl].ntiles) return;
    n = nvis[v];
    if (n == NVIS) {
        if (vis[v][NVIS - 1].depth <= (u16)depth) return;
        n = NVIS - 1;
    } else nvis[v]++;
    for (j = n; j > 0 && vis[v][j - 1].depth > (u16)depth; j--) vis[v][j] = vis[v][j - 1];
    vis[v][j].g = g; vis[v][j].lvl = lvl; vis[v][j].flip = flip; vis[v][j].pal = pal;
    vis[v][j].cx = (v ? VIEW1_X : 0) + 76 + sx;
    {
        s16 b = HORIZON + hh - (s16)(((u16)z * hh) >> 6);
        vis[v][j].bot = b > 250 ? 250 : b;
    }
    vis[v][j].depth = depth;
}

static u8 abs8(s16 v) { v = v < 0 ? -v : v; v >>= 4; return v > 255 ? 255 : (u8)v; }

/* which of the 8 Doom rotations of player o the viewer at (vx,vy) sees */
static u8 rotation(Player *o, u16 vx, u16 vy)
{
    s16 f, r;
    u8 af, ar, s;
    project(o->a, vx - o->x, vy - o->y, &f, &r);
    af = abs8(f); ar = abs8(r);
    if ((u16)ar * 106 < (u16)af * 44) s = f >= 0 ? 0 : 4;
    else if ((u16)af * 106 < (u16)ar * 44) s = r >= 0 ? 2 : 6;
    else if (f >= 0) s = r >= 0 ? 1 : 7;
    else s = r >= 0 ? 3 : 5;
    return (8 - s) & 7;
}

u8 player_graphic(Player *o, u8 rot, u8 *flip)
{
    u8 base = G_SKIN0 + o->skin * NSKINGFX;
    *flip = 0;
    if (o->dead) {
        u8 d = o->dead;
        return base + (d < 6 ? 12 : d < 12 ? 13 : d < 18 ? 14 : 15);
    }
    if (rot == 0 && o->attack_t) return base + 10;
    if (o->pain_t && (rot == 0 || rot == 1 || rot == 7)) return base + 11;
    if (rot > 4) { *flip = 1; rot = 8 - rot; }
    return base + rot + (((o->anim >> 3) & 1) ? 5 : 0);
}

static u8 obj_graphic(Obj *o)
{
    switch (o->type) {
    case OT_PLASMA: return G_PLASMA;
    case OT_GRENADE: return G_GRENADE;
    case OT_ROCKET: return G_ROCKET;
    case OT_FLAME: return o->timer > 12 ? G_FLAME0 : G_FLAME1;
    case OT_BFGBALL: return G_BFGBALL;
    case OT_EXPLO: return o->timer > 12 ? G_EXPLO0 : o->timer > 6 ? G_EXPLO1 : G_EXPLO2;
    case OT_PUFF: return G_PUFF;
    case OT_SPARK: return G_SPARK;
    case OT_TFOG: return o->timer > 12 ? G_TFOG0 : G_TFOG1;
    case OT_BLOOD: return o->timer > 8 ? G_BLOOD0 : G_BLOOD1;
    case OT_POWER: return (frame_no & 16) ? G_MEGA1 : G_POWER;
    default: return o->type;      /* items 0..12 follow the object types */
    }
}

static void build_view(u8 v)
{
    Player *V = &pl[v], *O = &pl[v ^ 1];
    u8 k;
    Obj *o;
    nvis[v] = 0;
    if (!shield[v ^ 1] || (frame_no & 1)) {
        u8 flip, g = player_graphic(O, rotation(O, V->x, V->y), &flip);
        if (!(O->dead && O->gib && O->dead > 4))
            consider(v, O->x - V->x, O->y - V->y, g, 0, flip, 2 + (v ^ 1));
    }
    for (k = 0, o = objs; k < MAXOBJ; k++, o++) {
        u8 t = o->type;
        s16 dx, dy;
        if (t == OT_NONE || o->state) continue;
        dx = o->x - V->x; dy = o->y - V->y;
        if (dx > 0x0C00 || dx < -0x0C00 || dy > 0x0C00 || dy < -0x0C00) continue;
        consider(v, dx, dy, obj_graphic(o), type_z[t], 0, 1);
    }
}

static void emit_view(u8 v)
{
    u8 i;
    s16 xlo = v ? VIEW1_X - 8 : -7, xhi = v ? 319 : VIEW_W + 7;
    s16 vx0 = v ? VIEW1_X : 0;
    for (i = 0; i < nvis[v]; i++) {
        Vis *s = &vis[v][i];
        u8 lvl = s->lvl, c;
        u16 t = cache_get(s->g, &lvl);
        const GfxImg *img;
        const u8 *st;
        if (!t) continue;
        img = &gfx_tab[s->g][lvl];
        st = img->strips;
        for (c = 0; c < img->ncols; c++, st += 2) {
            u8 r0 = st[0], n = st[1];
            s16 x, y;
            s16 ri;
            if (!n) continue;
            x = s->flip ? s->cx - img->left - (c + 1) * 8 : s->cx + img->left + c * 8;
            if (x >= xlo && x <= xhi) {
                ri = (x - vx0 + 4) >> 1;
                if (ri < 0) ri = 0;
                if (ri >= NRAYS) ri = NRAYS - 1;
                if (zbuf[v][ri] >= s->depth) {
                    y = s->bot + img->top + r0 * 8;
                    {
                        u16 tt = t;
                        u8 left = n;
                        while (left) {
                            u8 h = left > 4 ? 4 : left;
                            if (y < VIEW_H && y > -32)
                                add_spr(x, y, 1, h, TILE_ATTR_FULL(s->pal, 0, 0, s->flip, tt));
                            y += h * 8; tt += h; left -= h;
                        }
                    }
                }
            }
            t += n;
        }
    }
}

/* ------------------------------------------------------------ hand weapons */
void hand_upload(u8 p)
{
    Player *P = &pl[p];
    u8 f;
    P->cur_hand = P->weapon;
    for (f = 0; f < 3; f++) {
        const WpnFrame *w = &wpn_frames[P->weapon * 3 + f];
        vq_vram(w->tiles, (T_WPN + (p * 3 + f) * WPN_MAXTILES) * 32, w->ntiles * 32);
    }
}

static void emit_hand(u8 p)
{
    Player *P = &pl[p];
    const WpnFrame *w;
    const s16 *pc;
    u8 k, f = P->wframe;
    s16 ox = (p ? VIEW1_X : 0), oy = P->bob * 2;
    if (P->dead || P->cur_hand != P->weapon) return;
    if (f) { oy += 2; ox += (frame_no & 1); }
    w = &wpn_frames[P->weapon * 3 + f];
    pc = w->pieces;
    for (k = 0; k < w->npieces; k++, pc += 5)
        add_spr(ox + pc[0], oy + pc[1], pc[2], pc[3],
                TILE_ATTR_FULL(1, 0, 0, 0, T_WPN + (p * 3 + f) * WPN_MAXTILES + pc[4]));
}

static void emit_arrows(u8 p)
{
    s16 x0 = p ? VIEW1_X : 0;
    if (!hit_t[p] || pl[p].dead) return;
    if ((hit_t[p] & 4) == 0) return;      /* blink */
    switch (hit_dir[p]) {
    case HIT_LEFT:  add_spr(x0 + 2, 56, 2, 2, TILE_ATTR_FULL(1, 0, 0, 0, T_ARROW)); break;
    case HIT_RIGHT: add_spr(x0 + VIEW_W - 18, 56, 2, 2, TILE_ATTR_FULL(1, 0, 0, 0, T_ARROW + 4)); break;
    case HIT_FRONT: add_spr(x0 + 68, 2, 2, 2, TILE_ATTR_FULL(1, 0, 0, 0, T_ARROW + 8)); break;
    case HIT_BACK:  add_spr(x0 + 68, 110, 2, 2, TILE_ATTR_FULL(1, 0, 0, 0, T_ARROW + 12)); break;
    }
}

static void finish_sat(void)
{
    if (!nspr) {
        sat[0] = 0; sat[1] = 0; sat[2] = 0; sat[3] = 0;
    } else sat[(nspr - 1) * 4 + 1] &= 0xFF00;     /* end of the link list */
}

void build_sprites(void)
{
    u8 first;
    sat = sat_buf[cur_buf];
    nspr = 0;
    budget = 110;
    build_view(0);
    build_view(1);
    emit_arrows(0);
    emit_arrows(1);
    emit_hand(0);
    emit_hand(1);
    first = frame_no & 1;          /* the views take turns going first */
    emit_view(first);
    emit_view(first ^ 1);
    finish_sat();
}

/* menus: n graphics at their largest size, bottom-centred at (x, y) */
void menu_sprites(u8 n, const u8 *g, const s16 *x, const s16 *y, const u8 *flip, const u8 *pal)
{
    u8 k;
    sat = sat_buf[cur_buf];
    nspr = 0;
    budget = 200;
    for (k = 0; k < n; k++) {
        u8 lvl = NLEVELS_SZ - 1, c;
        u16 t = cache_get(g[k], &lvl);
        const GfxImg *img = &gfx_tab[g[k]][lvl];
        const u8 *st = img->strips;
        if (!t) continue;
        for (c = 0; c < img->ncols; c++, st += 2) {
            u8 r0 = st[0], nn = st[1];
            s16 xx = flip[k] ? x[k] - img->left - (c + 1) * 8 : x[k] + img->left + c * 8;
            s16 yy = y[k] + img->top + r0 * 8;
            u16 tt = t;
            while (nn) {
                u8 h = nn > 4 ? 4 : nn;
                add_spr(xx, yy, 1, h, TILE_ATTR_FULL(pal[k], 1, 0, flip[k], tt));
                yy += h * 8; tt += h; nn -= h;
            }
            t += st[1];
        }
    }
    finish_sat();
}

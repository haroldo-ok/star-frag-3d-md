/* render.c - two ray-cast views drawn into a RAM copy of their tiles.
 *
 * Only the top half (64 rows) of each 152 x 128 view is drawn: wall textures are
 * symmetric about the horizon, so plane A shows the bottom half with the same
 * tiles V-flipped.  Sky and floor pixels are colour 0 and let plane B's
 * gradient show through.  Each view is 76 rays, 2 px wide; a ray is one byte per
 * row in a column-major tile buffer (stride 4).  Walls are drawn by compiled
 * scalers (src/gen/scalers.s).
 *
 * There are two RAM buffers matching two VRAM banks.  A frame is drawn into
 * buffer b, its changed tile columns are uploaded to bank b, then a FLIP job
 * points plane A at bank b.  Per buffer we remember what each ray holds, so an
 * unchanged ray costs nothing and only changed columns are uploaded.
 */
#include "game.h"

#define COLBYTES 256
#define NCOLS    (2 * VIEW_TC)
#define BUFBYTES (NCOLS * COLBYTES)

static u8 vbuf0[BUFBYTES] __attribute__((aligned(4)));
static u8 vbuf1[BUFBYTES] __attribute__((aligned(4)));
u8 *viewbuf[2] = { vbuf0, vbuf1 };
u8 cur_buf;
u16 zbuf[2][NRAYS];

static u16 sig[2][2][NRAYS];      /* [buffer][view][ray]: hh | u << 8 | tex parity... */
static u8 stex[2][2][NRAYS];
static u8 ptop[2][2][NRAYS];      /* first wall row drawn (64 = none) */

/* cast results per view */
static u8 r_hh[2][NRAYS], r_u[2][NRAYS], r_tex[2][NRAYS];
static u16 r_cell[2][NRAYS];
static u16 cam_x[2], cam_y[2], cam_a[2];
static u8 cam_ok[2];
static const u8 * const *texp;

extern const void * const sc_tab[256];

void render_invalidate(void)
{
    u8 b, v, i;
    for (b = 0; b < 2; b++)
        for (v = 0; v < 2; v++)
            for (i = 0; i < NRAYS; i++) { sig[b][v][i] = 0xFFFF; stex[b][v][i] = 0xFF; }
    cam_ok[0] = cam_ok[1] = 0;
}

void render_init(u8 lvl)
{
    u8 b, v, i;
    texp = (const u8 * const *)lvl_tex[lvl];
    memset(vbuf0, 0, BUFBYTES);
    memset(vbuf1, 0, BUFBYTES);
    for (b = 0; b < 2; b++)
        for (v = 0; v < 2; v++)
            for (i = 0; i < NRAYS; i++) ptop[b][v][i] = 64;
    render_invalidate();
    cur_buf = 0;
}

static inline s32 muls16(s16 a, s16 b)
{
    s32 r = a;
    __asm__("muls.w %1,%0" : "+d"(r) : "dm"(b) : "cc");
    return r;
}
static inline u32 mulu16(u16 a, u16 b)
{
    u32 r = a;
    __asm__("mulu.w %1,%0" : "+d"(r) : "dm"(b) : "cc");
    return r;
}

/* one ray: fills hh, u, tex, cell, z for (v, i) */
static void cast_ray(u8 v, u8 i, u16 px, u16 py, u16 ang)
{
    s16 ca = sin14[(ang + 256) & 1023], sa = sin14[ang & 1023];
    u16 ddx = ddtab[ang & 1023], ddy = ddtab[(ang - 256) & 1023];
    u16 fx = px & 255, fy = py & 255;
    u16 sdx, sdy, dist;
    s16 sx, sy;
    u16 cell = ((py >> 8) << 5) | (px >> 8);
    u8 side, m;
    if (ca >= 0) { sx = 1; sdx = mulu16(256 - fx, ddx) >> 8; }
    else { sx = -1; sdx = mulu16(fx, ddx) >> 8; }
    if (sa >= 0) { sy = 32; sdy = mulu16(256 - fy, ddy) >> 8; }
    else { sy = -32; sdy = mulu16(fy, ddy) >> 8; }
    for (;;) {
        if (sdx < sdy) {
            cell += sx; dist = sdx; sdx += ddx; side = 0;
        } else {
            cell += sy; dist = sdy; sdy += ddy; side = 1;
        }
        cell &= 1023;
        m = map[cell];
        if (m) break;
    }
    {
        u16 perp = mulu16(dist, raycos[i]) >> 8;
        u16 k = perp >> 2;
        u16 u;
        if (k > 2047) k = 2047;
        r_hh[v][i] = hhtab[k];
        zbuf[v][i] = perp;
        if (side == 0) {
            u = (py + (s16)(muls16((s16)dist, sa) >> 14)) & 255;
            u >>= 2;
            if (ca < 0) u = 63 - u;
        } else {
            u = (px + (s16)(muls16((s16)dist, ca) >> 14)) & 255;
            u >>= 2;
            if (sa > 0) u = 63 - u;
        }
        r_u[v][i] = u;
        r_tex[v][i] = (m << 1) | side;
        r_cell[v][i] = cell | (side << 15);
    }
}

static void cast_view(u8 v)
{
    Player *P = &pl[v];
    u16 px = P->x, py = P->y, a = P->a;
    u8 i;
    for (i = 0; i < NRAYS; i += 2)
        cast_ray(v, i, px, py, (a + rayoff[i]) & 1023);
    for (i = 1; i < NRAYS; i += 2) {
        if (i + 1 < NRAYS && r_cell[v][i - 1] == r_cell[v][i + 1]) {
            u8 u0 = r_u[v][i - 1], u1 = r_u[v][i + 1];
            s16 du = (s16)u1 - u0;
            if (du > -12 && du < 12) {
                r_hh[v][i] = (r_hh[v][i - 1] + r_hh[v][i + 1] + 1) >> 1;
                r_u[v][i] = (u0 + u1) >> 1;
                r_tex[v][i] = r_tex[v][i - 1];
                r_cell[v][i] = r_cell[v][i - 1];
                zbuf[v][i] = (zbuf[v][i - 1] + zbuf[v][i + 1]) >> 1;
                continue;
            }
        }
        cast_ray(v, i, px, py, (a + rayoff[i]) & 1023);
    }
}

static inline void run_scaler(const void *fn, const u8 *src, u8 *dst)
{
    register const u8 *a0 __asm__("a0") = src;
    register u8 *a1 __asm__("a1") = dst;
    register const void *a2 __asm__("a2") = fn;
    __asm__ volatile("jsr (%2)" : "+a"(a0), "+a"(a1) : "a"(a2) : "d0", "cc", "memory");
}

/* draws both views into buffer cur_buf and queues the changed columns; returns
   the number of columns uploaded */
u8 render_views(void)
{
    u8 b = cur_buf, v, i, ncol = 0;
    u8 *buf = viewbuf[b];
    u32 dirty0 = 0, dirty1 = 0;        /* per view: tile column bits */
    for (v = 0; v < 2; v++) {
        Player *P = &pl[v];
        if (!cam_ok[v] || cam_x[v] != P->x || cam_y[v] != P->y || cam_a[v] != P->a) {
            cam_x[v] = P->x; cam_y[v] = P->y; cam_a[v] = P->a; cam_ok[v] = 1;
            cast_view(v);
        }
        for (i = 0; i < NRAYS; i++) {
            u8 hh = r_hh[v][i], u = r_u[v][i], t = r_tex[v][i];
            u16 s = hh | ((u16)u << 8);
            u8 top, old;
            u8 *dst;
            if (sig[b][v][i] == s && stex[b][v][i] == t) continue;
            sig[b][v][i] = s; stex[b][v][i] = t;
            dst = buf + (v * VIEW_TC + (i >> 2)) * COLBYTES + (i & 3);
            top = hh >= 64 ? 0 : 64 - hh;
            old = ptop[b][v][i];
            if (top > old) {
                u8 *p = dst + old * 4;
                u8 y;
                for (y = old; y < top; y++, p += 4) *p = 0;
            }
            ptop[b][v][i] = top;
            run_scaler(sc_tab[hh], texp[t] + (u << 5), dst);
            if (v) dirty1 |= 1UL << (i >> 2); else dirty0 |= 1UL << (i >> 2);
        }
    }
    /* queue runs of dirty columns (adjacent in RAM and VRAM) */
    for (v = 0; v < 2; v++) {
        u32 d = v ? dirty1 : dirty0;
        u8 c = 0;
        u16 tbase = (b ? T_VIEW1 : T_VIEW0) + v * VIEW_TC * 8;
        while (d) {
            u8 n;
            while (!(d & 1)) { d >>= 1; c++; }
            n = 0;
            while (d & 1) { d >>= 1; n++; }
            vq_vram(buf + (v * VIEW_TC + c) * COLBYTES, (tbase + c * 8) * 32, n * COLBYTES);
            ncol += n;
            c += n;
        }
    }
    return ncol;
}

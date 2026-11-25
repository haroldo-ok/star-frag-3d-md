/* Types of the generated graphics tables (src/gen). */
#ifndef GFXDEFS_H
#define GFXDEFS_H
#include <genesis.h>

/* one pre-scaled sprite image: 8-px wide strips of tiles, anchored bottom-centre */
typedef struct {
    const u32 *tiles;     /* strips one after another, each strip top to bottom */
    const u8 *strips;     /* per column: first tile row, number of tiles (0 = empty) */
    s16 left, top;        /* image top-left relative to the anchor (px) */
    u8 ncols, ntiles;
} GfxImg;

/* a first-person weapon frame: hardware-sprite pieces */
typedef struct {
    const u32 *tiles;
    const s16 *pieces;    /* x, y (view px), w, h (tiles), tile offset */
    u8 npieces, ntiles;
} WpnFrame;

#endif

/* vdpq.c - every VRAM/CRAM write during play goes through this job ring and
 * is done by DMA in the VBlank interrupt, with a byte budget per VBlank.
 * A FLIP job switches plane A to the other view bank and uploads the matching
 * sprite table, so a frame appears only after all of its tiles have landed.
 * The interrupt also latches both pads. */
#include "game.h"

#define NJOBS 96
#define BUDGET 6800
enum { J_VRAM, J_CRAM, J_FLIP };

typedef struct {
    const u8 *src;
    u16 dst;
    u16 len;          /* bytes */
    u8 type, arg;
} Job;

static Job jobs[NJOBS];
static volatile u8 jhead, jtail;
volatile u16 vbl_count;
volatile u16 flips_done;
volatile u8 pad_held[2], pad_press[2];
volatile u8 vq_halt;
static u16 joy_prev[2];

static u8 map_pad(u16 j)
{
    u8 k = 0;
    if (j & BUTTON_UP) k |= K_UP;
    if (j & BUTTON_DOWN) k |= K_DOWN;
    if (j & BUTTON_LEFT) k |= K_LEFT;
    if (j & BUTTON_RIGHT) k |= K_RIGHT;
    if (j & BUTTON_B) k |= K_FIRE;
    if (j & BUTTON_A) k |= K_STRAFE;
    if (j & (BUTTON_C | BUTTON_X | BUTTON_Y | BUTTON_Z)) k |= K_NEXT;
    if (j & BUTTON_START) k |= K_START;
    return k;
}

static void vint(void)
{
    s16 budget = BUDGET;
    u8 p;
    vbl_count++;
    while (!vq_halt && jhead != jtail && budget > 0) {
        Job *j = &jobs[jhead];
        if (j->type == J_FLIP) {
            VDP_setBGAAddress(j->arg ? PLANEA1_ADDR : PLANEA0_ADDR);
            DMA_doDma(DMA_VRAM, (void *)j->src, SAT_ADDR, 80 * 4, 2);
            budget -= 640;
            flips_done++;
            jhead = (jhead + 1) % NJOBS;
            continue;
        }
        {
            u16 n = j->len;
            if ((s16)n > budget) n = budget & ~1;
            if (j->type == J_VRAM) DMA_doDma(DMA_VRAM, (void *)j->src, j->dst, n >> 1, 2);
            else DMA_doDma(DMA_CRAM, (void *)j->src, j->dst, n >> 1, 2);
            budget -= n;
            if (n == j->len) jhead = (jhead + 1) % NJOBS;
            else { j->src += n; j->dst += n; j->len -= n; }
        }
    }
    JOY_update();
    for (p = 0; p < 2; p++) {
        u16 v = JOY_readJoypad(p ? JOY_2 : JOY_1);
        u8 k = map_pad(v);
        pad_press[p] |= k & ~map_pad(joy_prev[p]);
        pad_held[p] = k;
        joy_prev[p] = v;
    }
}

static void push(const void *src, u16 dst, u16 len, u8 type, u8 arg)
{
    u8 nt;
    for (;;) {
        nt = (jtail + 1) % NJOBS;
        if (nt != jhead) break;     /* ring full: wait for the VBlank to drain it */
    }
    jobs[jtail].src = src;
    jobs[jtail].dst = dst;
    jobs[jtail].len = len;
    jobs[jtail].type = type;
    jobs[jtail].arg = arg;
    jtail = nt;
}

void vq_vram(const void *src, u16 dst, u16 bytes) { if (bytes) push(src, dst, bytes, J_VRAM, 0); }
void vq_cram(const u16 *src, u16 index, u16 count) { push(src, index * 2, count * 2, J_CRAM, 0); }
void vq_flip(u8 bank, const void *sat) { push(sat, 0, 0, J_FLIP, bank); }
u16 vq_pending(void) { return (u8)(jtail - jhead + NJOBS) % NJOBS; }
void vq_wait_empty(void) { while (jhead != jtail) ; }
void vq_sync(void) { u16 v = vbl_count; while (v == vbl_count) ; }

void vq_init(void)
{
    jhead = jtail = 0;
    SYS_setVIntCallback(vint);
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, FALSE);
    VDP_setBGAAddress(PLANEA0_ADDR);
    VDP_setBGBAddress(PLANEB_ADDR);
    VDP_setWindowAddress(WINDOW_ADDR);
    VDP_setHScrollTableAddress(HSCROLL_ADDR);
    VDP_setSpriteListAddress(SAT_ADDR);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(FALSE, 0);
}

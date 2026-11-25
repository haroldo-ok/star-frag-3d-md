/* sound.c - XGM2: FM + PSG + DAC-drum music on PCM channel 1, Brutal Doom
 * effects on channel 2 (player 1) and 3 (player 2). */
#include "game.h"

extern const u8 * const sfx_data[];
extern const u32 sfx_len[];
extern const u8 * const mus_data[];

static u8 ch_prio[2];
static u16 ch_until[2];

/* higher wins while the channel is still busy */
static const u8 prio[NSFX] = {
    0,
    6, 7, 8, 6, 5, 6, 7, 6, 5, 8, 9,     /* weapons */
    10,                                  /* explode */
    4, 5, 9, 12, 7, 9, 3, 2, 2, 5, 12, 3, 3
};

void snd_init(void)
{
    XGM2_loadDriver(TRUE);
}

void sfx(u8 id, u8 player)
{
    u8 c = player & 1;
    u16 now = vbl_count;
    if (!id || id >= NSFX) return;
    /* a busy channel keeps its sound unless the new one matters as much */
    if ((s16)(ch_until[c] - now) > 0 && prio[id] < ch_prio[c]) {
        c ^= 1;
        if ((s16)(ch_until[c] - now) > 0 && prio[id] < ch_prio[c]) return;
    }
    ch_prio[c] = prio[id];
    ch_until[c] = now + (u16)(sfx_len[id] / 222);     /* 13300 Hz / 60 */
    SYS_disableInts();
    XGM2_playPCMEx(sfx_data[id], sfx_len[id], c ? SOUND_PCM_CH3 : SOUND_PCM_CH2, 8 + (prio[id] >> 1), FALSE, FALSE);
    SYS_enableInts();
}

void music_play(u8 song)
{
    SYS_disableInts();
    XGM2_play(mus_data[song]);
    SYS_enableInts();
}

void music_stop(void)
{
    SYS_disableInts();
    XGM2_stop();
    SYS_enableInts();
}

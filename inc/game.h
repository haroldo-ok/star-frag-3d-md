/* Star Frag 3D (Mega Drive) - shared definitions */
#ifndef GAME_H
#define GAME_H
#include <genesis.h>
#include "gfxdefs.h"
#include "src/gen/assets.h"

/* ---------------------------------------------------------------- VRAM layout */
#define PLANEA0_ADDR  0xC000
#define PLANEA1_ADDR  0xD000
#define PLANEB_ADDR   0xE000
#define HSCROLL_ADDR  0xF000
#define SAT_ADDR      0xF400
#define WINDOW_ADDR   0xF800
#define PLANE_W       64

#define T_GRAD     1       /* 16 sky/floor gradient tiles */
#define T_PANEL    17      /* HUD panel + divider tiles */
#define T_FONT     32      /* white font, ASCII 32..95 */
#define T_FONTY    96      /* yellow font */
#define T_BIGDIG   160     /* 13 big glyphs x 2 tiles */
#define T_VIEW0    192     /* view bank 0: 2 views x 19 tile columns x 8 rows */
#define T_VIEW1    496
#define T_ARROW    800     /* 4 x (2x2) */
#define T_RADAR    816     /* 2 x 16 */
#define T_FACE     848     /* 2 x 12 */
#define T_WPN      872     /* 2 players x 3 frames x WPN_MAXTILES */
#define T_CACHE    (T_WPN + 6 * WPN_MAXTILES)
#define T_CACHE_END 1536
#define CACHE_TILES (T_CACHE_END - T_CACHE)

/* ---------------------------------------------------------------- screen layout */
#define VIEW_TC     19     /* tile columns per view */
#define VIEW_W      152
#define VIEW_H      128
#define HORIZON     64
#define NRAYS       76     /* per view, 2 px each */
#define VIEW1_X     168    /* px: second view starts at tile column 21 */
#define HUD_ROW     16     /* first HUD tile row */
#define HUD_ROWS    12

/* pad bits (game-side) */
#define K_UP    1
#define K_DOWN  2
#define K_LEFT  4
#define K_RIGHT 8
#define K_FIRE  16
#define K_STRAFE 32
#define K_NEXT  64
#define K_START 128

/* ---------------------------------------------------------------- objects */
enum { OT_MEDKIT, OT_ARMOR, OT_AMMO, OT_W_SHOTGUN, OT_W_SSG, OT_W_RIFLE, OT_W_MINIGUN, OT_W_GREN,
       OT_W_ROCKET, OT_W_PLASMA, OT_W_FLAME, OT_W_RAIL, OT_W_BFG, OT_POWER,      /* items 0..13 */
       OT_PLASMA, OT_GRENADE, OT_ROCKET, OT_FLAME, OT_BFGBALL,                   /* projectiles */
       OT_EXPLO, OT_PUFF, OT_SPARK, OT_TFOG, OT_BLOOD,                          /* effects */
       NOBJTYPES, OT_NONE = 0xFF };
#define OT_LAST_ITEM OT_POWER
#define OT_LAST_PROJ OT_BFGBALL

enum { W_PISTOL, W_SHOTGUN, W_SSG, W_RIFLE, W_MINIGUN, W_GREN, W_ROCKET, W_PLASMA, W_FLAME, W_RAIL, W_BFG };

typedef struct {
    u16 x, y;          /* 8.8 tiles */
    u8 type;
    u8 state;          /* items: 0 present, 1 waiting to respawn */
    u8 owner;
    u8 dmg;
    u16 timer;
    s16 vx, vy;
    u8 anim, pad;
} Obj;
#define MAXOBJ 48

typedef struct {
    u16 x, y, a;
    u8 health, armor, weapon, pad0;
    u16 owned;
    u8 ammo[NWEAPONS];
    s8 frags;
    u8 cool, wframe, bob;
    u8 ouch, grin;
    u8 dead;
    u16 power;
    u8 anim, attack_t, pain_t;
    u8 skin;
    u8 keys, prev;
    u8 dirty;
    u8 msg, msg_t;
    u8 cur_face, cur_hand;
    u8 cpu;
    u8 gib;             /* died to an explosion */
} Player;

#define D_HEALTH 1
#define D_ARMOR  2
#define D_WEAPON 4
#define D_FACE   8
#define D_FRAGS  16
#define D_MSG    32
#define D_ALL    63

enum { MSG_NONE, MSG_FRAGGED, MSG_FRAGGEDBY, MSG_SUICIDE, MSG_RESPAWN, MSG_POWER, MSG_GOT, MSG_SUDDEN, MSG_SHIELD };
enum { HIT_LEFT = 1, HIT_RIGHT, HIT_FRONT, HIT_BACK };

/* ---------------------------------------------------------------- generated */
extern const s16 sin14[1024], sin8[1024];
extern const u16 ddtab[1024];
extern const s8 rayoff[NRAYS];
extern const u16 raycos[NRAYS];
extern const u8 hhtab[2048];
extern const GfxImg * const gfx_tab[];
extern const u32 * const face_data[];
extern const u16 skin_pals[];
extern const char * const skin_names[];
extern const WpnFrame wpn_frames[];
extern const char * const weapon_names[];
extern const u16 gen_pal[16];
extern const u16 size_q[];
extern const u32 font_tiles[], bigdig_tiles[], panel_tiles[], arrow_tiles[], radar_tiles[], grad_tiles[];
extern const u32 title_tiles[];
extern const u16 title_map[], title_pal[];
extern const u8 * const lvl_maps[], * const lvl_objs[], * const lvl_spawns[];
extern const u8 * const * const lvl_tex[];
extern const u16 lvl_pals[];
extern const char * const level_names[];

/* ---------------------------------------------------------------- vdpq.c */
void vq_init(void);
void vq_vram(const void *src, u16 dst, u16 bytes);
void vq_cram(const u16 *src, u16 index, u16 count);
void vq_flip(u8 bank, const void *sat);
void vq_wait_empty(void);
u16 vq_pending(void);
void vq_sync(void);                 /* wait for the next VBlank */
extern volatile u16 vbl_count;
extern volatile u16 flips_done;
extern volatile u8 pad_held[2], pad_press[2];
extern volatile u8 vq_halt;
void vdp_setup_game(void);
void vdp_setup_text(void);

/* ---------------------------------------------------------------- render.c */
extern u16 zbuf[2][NRAYS];
extern u8 cur_buf;
void render_init(u8 level);
void render_invalidate(void);
u8 render_views(void);              /* draws both views into RAM buffer cur_buf, queues uploads */
extern u8 *viewbuf[2];

/* ---------------------------------------------------------------- sprites.c */
void sprites_init(void);
void build_sprites(void);           /* fills the SAT buffer for cur_buf */
extern u16 sat_buf[2][80 * 4];
void project(u16 a, s16 dx, s16 dy, s16 *depth, s16 *lat);
void menu_sprites(u8 n, const u8 *g, const s16 *x, const s16 *y, const u8 *flip, const u8 *pal);
void hand_upload(u8 p);
u8 player_graphic(Player *o, u8 rot, u8 *flip);

/* ---------------------------------------------------------------- game.c */
extern u8 map[1024];
extern Obj objs[MAXOBJ];
extern Player pl[2];
extern u8 level, frag_limit, game_result, time_limit, sudden_death, bot_skill;
extern u8 shield[2], hit_t[2], hit_dir[2];
extern u16 time_left;
void level_load(u8 lvl);
void match_new(void);
void game_tick(u8 n);
u8 rand8(void);
u8 los(u16 x0, u16 y0, u16 x1, u16 y1);
u16 approx_dist(s16 dx, s16 dy);
void select_best_weapon(u8 p);
u8 weapon_ammo_ok(Player *P, u8 w);

/* ---------------------------------------------------------------- bot.c */
u8 bot_keys(u8 p);
void bot_reset(void);

/* ---------------------------------------------------------------- hud.c */
extern u16 hud_map[HUD_ROWS][40];
void hud_init(void);
void hud_draw(void);
void hud_full(void);
void text_at(u16 *row, u8 x, const char *s, u8 yellow);
void print_at(u8 x, u8 y, const char *s, u8 yellow);   /* text screens: plane A0 */

/* ---------------------------------------------------------------- sound.c */
void snd_init(void);
void sfx(u8 id, u8 player);
void music_play(u8 song);
void music_stop(void);

/* ---------------------------------------------------------------- main.c */
extern u8 game_state;
extern u16 frame_no;
#endif

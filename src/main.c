/* Star Frag 3D - Mega Drive edition.  A two-player split-screen deathmatch.
 *
 * main.c: VDP set-up, title, match setup, the frame loop, pause and results.
 */
#include "game.h"

u8 game_state;          /* 0 title, 1 menu, 2 playing, 3 results, 4 paused (for tests) */
u16 frame_no;
u16 frames_drawn;
static u16 flips_issued;
static u8 menu_sel;
static u8 opt_skin[2] = { 0, 3 };
static u8 opt_cpu = 1;
static u8 time_idx, lim_idx = 1;
static const u8 limits[] = { 5, 10, 15, 20, 30 };
static const u8 time_mins[] = { 0, 3, 5, 10 };
static const u8 arena_song[] = { MUS_ARENA, MUS_DM05, MUS_DM06, MUS_DM07, MUS_DM10 };

#define T_TITLE 600

/* ------------------------------------------------------------ helpers */
static void clear_press(void) { pad_press[0] = pad_press[1] = 0; }

static void wait_flip_slot(void)
{
    while ((u16)(flips_issued - flips_done) > 1) ;
}

static void present(void)
{
    vq_flip(cur_buf, sat_buf[cur_buf]);
    flips_issued++;
    cur_buf ^= 1;
    frame_no++;
}

static void load_common_tiles(void)
{
    VDP_fillTileData(0, 0, 1, TRUE);
    VDP_loadTileData(font_tiles, T_FONT, 128, DMA);
    VDP_loadTileData(bigdig_tiles, T_BIGDIG, 26, DMA);
    VDP_loadTileData(panel_tiles, T_PANEL, NPANEL_TILES, DMA);
    VDP_loadTileData(grad_tiles, T_GRAD, 16, DMA);
    VDP_loadTileData(arrow_tiles, T_ARROW, 16, DMA);
    VDP_loadTileData(radar_tiles, T_RADAR, 16, DMA);
    VDP_loadTileData(radar_tiles, T_RADAR + 16, 16, DMA);
    PAL_setColors(16, gen_pal, 16, DMA);
}

static void begin_setup(void)
{
    vq_wait_empty();
    vq_halt = 1;
    SYS_disableInts();
    VDP_setEnable(FALSE);
    SYS_enableInts();
    memset(sat_buf, 0, sizeof(sat_buf));
    DMA_doDma(DMA_VRAM, sat_buf[0], SAT_ADDR, 80 * 4, 2);
}

static void end_setup(void)
{
    SYS_disableInts();
    VDP_setEnable(TRUE);
    SYS_enableInts();
    vq_halt = 0;
    flips_issued = flips_done;
    cur_buf = 0;
}

static void fill_plane(u16 addr, u16 v)
{
    static u16 row[PLANE_W];
    u8 y, x;
    for (x = 0; x < PLANE_W; x++) row[x] = v;
    for (y = 0; y < 32; y++) VDP_setTileMapData(addr, row, y * PLANE_W, PLANE_W, 2, DMA);
}

/* plane B: metal panel everywhere (text screens) or around the views (game) */
static void panel_plane(u8 game)
{
    static u16 row[PLANE_W];
    u8 y, x;
    for (y = 0; y < 28; y++) {
        for (x = 0; x < PLANE_W; x++) {
            u16 t;
            if (game && y < HUD_ROW) {
                if (x == 19 || x == 20) t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + 6 + (x - 19));
                else t = TILE_ATTR_FULL(0, 0, 0, 0, T_GRAD + y);
            } else {
                if (game && (x == 19 || x == 20)) t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + 6 + (x - 19));
                else if (y == (game ? HUD_ROW : 0)) t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + 2);
                else if (y == 27) t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + 3);
                else if (!game && (x == 0 || x == 39)) t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + 6 + (x == 39));
                else t = TILE_ATTR_FULL(1, 1, 0, 0, T_PANEL + ((x + y) & 1));
            }
            row[x] = t;
        }
        VDP_setTileMapData(PLANEB_ADDR, row, y * PLANE_W, PLANE_W, 2, DMA);
    }
}

/* ------------------------------------------------------------ game screen */
static void game_screen(void)
{
    static u16 row[PLANE_W];
    u8 b, y, x;
    begin_setup();
    load_common_tiles();
    VDP_fillTileData(0, T_VIEW0, 2 * 2 * VIEW_TC * 8, TRUE);
    PAL_setColors(0, &lvl_pals[level * 16], 16, DMA);
    PAL_setColors(32, &skin_pals[pl[0].skin * 16], 16, DMA);
    PAL_setColors(48, &skin_pals[pl[1].skin * 16], 16, DMA);
    VDP_setBackgroundColor(17);
    panel_plane(1);
    for (b = 0; b < 2; b++) {
        u16 base = b ? T_VIEW1 : T_VIEW0;
        u16 addr = b ? PLANEA1_ADDR : PLANEA0_ADDR;
        fill_plane(addr, 0);
        for (y = 0; y < 16; y++) {
            for (x = 0; x < PLANE_W; x++) row[x] = 0;
            for (x = 0; x < 40; x++) {
                u8 v = x < 19 ? 0 : 1, tc = x < 19 ? x : x - 21;
                if (x == 19 || x == 20) continue;
                if (y < 8) row[x] = TILE_ATTR_FULL(0, 0, 0, 0, base + (v * VIEW_TC + tc) * 8 + y);
                else row[x] = TILE_ATTR_FULL(0, 0, 1, 0, base + (v * VIEW_TC + tc) * 8 + 15 - y);
            }
            VDP_setTileMapData(addr, row, y * PLANE_W, PLANE_W, 2, DMA);
        }
    }
    VDP_setBGAAddress(PLANEA0_ADDR);
    render_init(level);
    sprites_init();
    end_setup();
    hud_full();
}

/* ------------------------------------------------------------ text screens */
static void text_screen(void)
{
    begin_setup();
    load_common_tiles();
    PAL_setColors(32, &skin_pals[opt_skin[0] * 16], 16, DMA);
    PAL_setColors(48, &skin_pals[opt_skin[1] * 16], 16, DMA);
    VDP_setBackgroundColor(17);
    VDP_setBGAAddress(PLANEA0_ADDR);
    fill_plane(PLANEA0_ADDR, 0);
    panel_plane(0);
    sprites_init();
    end_setup();
}

static void center(u8 y, const char *s, u8 yellow)
{
    u8 n = 0;
    while (s[n]) n++;
    print_at((40 - n) >> 1, y, s, yellow);
}

/* ------------------------------------------------------------ title */
static void title_screen(void)
{
    u16 t = 0, i;
    static u16 pal[64];
    static u16 row[PLANE_W];
    game_state = 0;
    begin_setup();
    load_common_tiles();
    VDP_loadTileData(title_tiles, T_TITLE, NTITLE_TILES, DMA);
    for (i = 0; i < 28; i++) {
        u8 x;
        for (x = 0; x < 40; x++) {
            u16 e = title_map[i * 40 + x];
            row[x] = TILE_ATTR_FULL(2 + (e >> 13), 0, 0, 0, T_TITLE + (e & 0x7FF));
        }
        for (; x < PLANE_W; x++) row[x] = 0;
        VDP_setTileMapData(PLANEB_ADDR, row, i * PLANE_W, PLANE_W, 2, DMA);
    }
    fill_plane(PLANEA0_ADDR, 0);
    VDP_setBGAAddress(PLANEA0_ADDR);
    memcpy(pal, title_pal, 64);
    PAL_setColors(32, title_pal, 32, DMA);
    VDP_setBackgroundColor(32 + 1);
    end_setup();
    center(16, "MEGA DRIVE EDITION", 1);
    center(18, "WEAPONS AND SOUNDS FROM BRUTAL DOOM", 0);
    center(24, "1 OR 2 PLAYERS - SPLIT SCREEN", 0);
    music_play(MUS_TITLE);
    clear_press();
    for (;;) {
        vq_sync();
        t++;
        if ((t & 3) == 0) {
            u8 ph = (t >> 2) % 7;
            for (i = 0; i < 7; i++) pal[16 + 9 + i] = title_pal[16 + 9 + (i + ph) % 7];
            for (i = 0; i < 4; i++) pal[5 + i] = title_pal[5 + ((i + (t >> 3)) & 3)];
            vq_cram(pal, 32, 32);
        }
        if (t == 1 || (t & 31) == 0)
            print_at(15, 21, (t & 32) ? "          " : "PRESS START", 1);
        if (t > 30 && ((pad_press[0] | pad_press[1]) & (K_START | K_FIRE | K_NEXT))) break;
        if (t > 1800) break;          /* attract: go to the menu anyway */
        clear_press();
    }
    vq_wait_empty();
    sfx(SFX_RESPAWN, 0);
}

/* ------------------------------------------------------------ match setup */
static void opt_line(u8 row, const char *label, const char *val, u8 sel)
{
    char buf[18];
    u8 i = 0;
    print_at(8, row, sel ? ">" : " ", 1);
    print_at(10, row, label, sel);
    buf[0] = '<';
    while (val[i] && i < 14) { buf[i + 1] = val[i]; i++; }
    while (i < 14) buf[++i] = ' ';
    buf[15] = '>';
    buf[16] = 0;
    print_at(22, row, buf, 0);
}

static const char * const cpu_names[] = { "HUMAN", "CPU" };
static const char * const skill_names[] = { "EASY", "NORMAL", "HARD" };
static const char * const time_names[] = { "NONE", "3 MIN", "5 MIN", "10 MIN" };
#define MENU_START 7

static void menu_draw(void)
{
    char num[3];
    num[0] = '0' + limits[lim_idx] / 10; num[1] = '0' + limits[lim_idx] % 10; num[2] = 0;
    opt_line(6, "PLAYER 1", skin_names[opt_skin[0]], menu_sel == 0);
    opt_line(7, "PLAYER 2", skin_names[opt_skin[1]], menu_sel == 1);
    opt_line(9, "PLAYER 2 IS", cpu_names[opt_cpu], menu_sel == 2);
    opt_line(10, "CPU SKILL", opt_cpu ? skill_names[bot_skill] : "-", menu_sel == 3);
    opt_line(12, "ARENA", level_names[level], menu_sel == 4);
    opt_line(13, "FRAG LIMIT", num, menu_sel == 5);
    opt_line(14, "TIME LIMIT", time_names[time_idx], menu_sel == 6);
    print_at(8, 16, menu_sel == MENU_START ? ">" : " ", 1);
    print_at(10, 16, "START MATCH", menu_sel == MENU_START);
}

static void menu_screen(void)
{
    u16 t = 0;
    u8 k, dirty = 1, ps0 = 0xFF, ps1 = 0xFF;
    game_state = 1;
    text_screen();
    center(2, "STAR FRAG 3D - MATCH SETUP", 1);
    center(19, "UP/DOWN: PICK", 0);
    center(20, "LEFT/RIGHT: CHANGE", 0);
    center(21, "START: FIGHT!", 1);
    center(23, "PAD 2 LEFT/RIGHT:", 0);
    center(24, "JOIN AS PLAYER 2", 0);
    print_at(4, 26, "P1", 1);
    print_at(34, 26, "P2", 1);
    clear_press();
    for (;;) {
        u8 rot = (t >> 4) & 7;
        u8 g[2], fl[2], pal[2] = { 2, 3 };
        s16 x[2] = { 36, 284 }, y[2] = { 212, 212 };
        Player tmp;
        u8 r2 = (8 - rot) & 7;
        if (dirty) { menu_draw(); dirty = 0; }
        if (ps0 != opt_skin[0] || ps1 != opt_skin[1]) {
            ps0 = opt_skin[0]; ps1 = opt_skin[1];
            vq_cram(&skin_pals[ps0 * 16], 32, 16);
            vq_cram(&skin_pals[ps1 * 16], 48, 16);
        }
        memset(&tmp, 0, sizeof(tmp));
        tmp.skin = opt_skin[0]; g[0] = player_graphic(&tmp, rot, &fl[0]);
        tmp.skin = opt_skin[1]; g[1] = player_graphic(&tmp, r2, &fl[1]);
        wait_flip_slot();
        menu_sprites(2, g, x, y, fl, pal);
        vq_flip(0, sat_buf[cur_buf]);
        flips_issued++;
        cur_buf ^= 1;
        frame_no++;
        vq_sync();
        t++;
        k = pad_press[0];
        if (k & K_UP) { menu_sel = menu_sel ? menu_sel - 1 : MENU_START; dirty = 1; }
        if (k & K_DOWN) { menu_sel = menu_sel == MENU_START ? 0 : menu_sel + 1; dirty = 1; }
        if (k & (K_LEFT | K_RIGHT)) {
            u8 r = (k & K_RIGHT) ? 1 : 0;
            switch (menu_sel) {
            case 0: case 1: opt_skin[menu_sel] = r ? (opt_skin[menu_sel] + 1) % NSKINS
                                                   : (opt_skin[menu_sel] + NSKINS - 1) % NSKINS; break;
            case 2: opt_cpu ^= 1; break;
            case 3: bot_skill = r ? (bot_skill + 1) % 3 : (bot_skill + 2) % 3; break;
            case 4: level = r ? (level + 1) % NARENAS : (level + NARENAS - 1) % NARENAS; break;
            case 5: lim_idx = r ? (lim_idx + 1) % 5 : (lim_idx + 4) % 5; break;
            case 6: time_idx = r ? (time_idx + 1) % 4 : (time_idx + 3) % 4; break;
            }
            dirty = 1;
        }
        if (pad_press[1] & (K_LEFT | K_RIGHT)) {
            opt_skin[1] = (pad_press[1] & K_RIGHT) ? (opt_skin[1] + 1) % NSKINS
                                                   : (opt_skin[1] + NSKINS - 1) % NSKINS;
            opt_cpu = 0;
            dirty = 1;
        }
        if (dirty) sfx(SFX_MENU, 0);
        if ((k | pad_press[1]) & K_START) break;
        if (k & (K_FIRE | K_NEXT)) {
            if (menu_sel == MENU_START) break;
            menu_sel++;
            dirty = 1;
        }
        clear_press();
    }
    frag_limit = limits[lim_idx];
    time_limit = time_mins[time_idx];
    music_stop();
    sfx(SFX_RESPAWN, 0);
}

/* ------------------------------------------------------------ playing */
static void hud_row_text(u8 x, const char *s)
{
    text_at(hud_map[0], x, s, 1);
    vq_vram(hud_map[0], PLANEA0_ADDR + HUD_ROW * PLANE_W * 2, 80);
    vq_vram(hud_map[0], PLANEA1_ADDR + HUD_ROW * PLANE_W * 2, 80);
}

static void do_pause(void)
{
    u8 s = game_state;
    game_state = 4;
    hud_row_text(17, "PAUSED");
    music_stop();
    clear_press();
    while (!((pad_press[0] | pad_press[1]) & K_START)) vq_sync();
    clear_press();
    hud_row_text(17, "      ");
    music_play(arena_song[level % 5]);
    game_state = s;
}

static void play_match(void)
{
    u16 last;
    u8 n, end_t = 0;
    level_load(level);
    pl[0].skin = opt_skin[0];
    pl[1].skin = opt_skin[1];
    pl[0].cpu = 0;
    pl[1].cpu = opt_cpu;
    match_new();
    game_screen();
    game_state = 2;
    music_play(arena_song[level % 5]);
    last = vbl_count;
    for (;;) {
        u16 now = vbl_count;
        n = (u8)(now - last > 12 ? 12 : now - last);
        last = now;
        if ((pad_press[0] | (opt_cpu ? 0 : pad_press[1])) & K_START) {
            pad_press[0] &= ~K_START; pad_press[1] &= ~K_START;
            do_pause();
            last = vbl_count;
            continue;
        }
        if (!n) { vq_sync(); continue; }
        game_tick(n);
        if (game_result) {
            end_t += n;
            if (end_t > 100) break;
        }
        wait_flip_slot();
        render_views();
        build_sprites();
        hud_draw();
        present();
        frames_drawn++;
    }
    vq_wait_empty();
    music_stop();
}

static void results_screen(void)
{
    u8 w = game_result - 1, ready = 0;
    u16 t, start;
    char buf[4];
    game_state = 3;
    opt_skin[0] = pl[0].skin; opt_skin[1] = pl[1].skin;
    text_screen();
    center(3, "MATCH OVER", 1);
    center(5, w ? "PLAYER 2 WINS!" : "PLAYER 1 WINS!", 0);
    center(7, level_names[level], 1);
    print_at(10, 10, "P1", 1);
    print_at(13, 10, skin_names[pl[0].skin], 0);
    print_at(10, 12, "P2", 1);
    print_at(13, 12, skin_names[pl[1].skin], 0);
    buf[0] = pl[0].frags < 0 ? '-' : ' ';
    buf[1] = '0' + (pl[0].frags < 0 ? -pl[0].frags : pl[0].frags) / 10 % 10;
    buf[2] = '0' + (pl[0].frags < 0 ? -pl[0].frags : pl[0].frags) % 10; buf[3] = 0;
    print_at(22, 10, buf, 0); print_at(26, 10, "FRAGS", 1);
    buf[0] = pl[1].frags < 0 ? '-' : ' ';
    buf[1] = '0' + (pl[1].frags < 0 ? -pl[1].frags : pl[1].frags) / 10 % 10;
    buf[2] = '0' + (pl[1].frags < 0 ? -pl[1].frags : pl[1].frags) % 10; buf[3] = 0;
    print_at(22, 12, buf, 0); print_at(26, 12, "FRAGS", 1);
    center(15, "PRESS START", 1);
    music_play(MUS_WIN);
    clear_press();
    start = vbl_count;
    for (t = 0; ; t++) {
        u8 g[1], fl[1], pal[1];
        s16 x[1] = { 160 }, y[1] = { 212 };
        Player tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.skin = pl[w].skin;
        if (t & 32) { tmp.attack_t = 1; g[0] = player_graphic(&tmp, 0, &fl[0]); }
        else g[0] = player_graphic(&tmp, (t >> 3) & 7, &fl[0]);
        pal[0] = 2 + w;
        wait_flip_slot();
        menu_sprites(1, g, x, y, fl, pal);
        vq_flip(0, sat_buf[cur_buf]);
        flips_issued++;
        cur_buf ^= 1;
        frame_no++;
        vq_sync();
        if (!ready && (u16)(vbl_count - start) > 60) ready = 1;
        if (ready && ((pad_press[0] | pad_press[1]) & (K_START | K_FIRE))) break;
        if (ready && (u16)(vbl_count - start) > 1800) break;
        clear_press();
    }
    vq_wait_empty();
}

int main(bool hard)
{
    (void)hard;
    SYS_disableInts();
    vq_init();
    SYS_enableInts();
    snd_init();
    title_screen();
    for (;;) {
        menu_screen();
        play_match();
        results_screen();
    }
    return 0;
}

/* hud.c - the two players' HUD halves (plane A rows 16-27, both view maps),
 * text screens, faces, big numbers, radar and the arms list. */
#include "game.h"

u16 hud_map[HUD_ROWS][40];
static u16 hud_dirty;
static u32 radar_buf[2][16 * 8];
static u8 radar_turn;
static s8 dot_x[2], dot_y[2];
static u16 clock_sec = 0xFFFF;

#define HROW(y) (hud_map[(y) - HUD_ROW])
#define ATTR_TXT(t) TILE_ATTR_FULL(1, 1, 0, 0, (t))

void text_at(u16 *row, u8 x, const char *s, u8 yellow)
{
    u16 base = yellow ? T_FONTY : T_FONT;
    while (*s && x < 40) {
        u8 c = (u8)*s++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = 32;
        row[x++] = c == ' ' ? 0 : ATTR_TXT(base + c - 32);
    }
}

static void hud_text(u8 y, u8 x, const char *s, u8 yellow)
{
    text_at(HROW(y), x, s, yellow);
    hud_dirty |= 1 << (y - HUD_ROW);
}

static void hud_pad(u8 y, u8 x, const char *s, u8 w, u8 yellow)
{
    char buf[24];
    u8 i = 0;
    while (s[i] && i < w) { buf[i] = s[i]; i++; }
    while (i < w) buf[i++] = ' ';
    buf[w] = 0;
    hud_text(y, x, buf, yellow);
}

static void num_str(char *buf, s16 v, u8 digits, u8 blank)
{
    u8 i = digits, neg = v < 0;
    if (neg) v = -v;
    buf[digits] = 0;
    while (i) {
        i--;
        if (blank && v == 0 && i != digits - 1) buf[i] = ' ';
        else buf[i] = '0' + v % 10;
        v /= 10;
    }
    if (neg) buf[0] = '-';
}

/* big digits: glyphs 0-9, 10 '%', 11 '-', 12 blank; 2 tiles high */
static void big_num(u8 x, u16 v, u8 digits, u8 pct)
{
    char buf[6];
    u8 i;
    num_str(buf, v, digits, 1);
    for (i = 0; i < digits + pct; i++) {
        u8 g = i < digits ? (buf[i] == ' ' ? 12 : buf[i] - '0') : 10;
        HROW(18)[x + i] = ATTR_TXT(T_BIGDIG + g * 2);
        HROW(19)[x + i] = ATTR_TXT(T_BIGDIG + g * 2 + 1);
    }
    hud_dirty |= 3 << (18 - HUD_ROW);
}

static void face_update(u8 p)
{
    Player *P = &pl[p];
    u8 f;
    if (P->dead) f = 7;
    else if (P->ouch) f = 5;
    else if (P->grin) f = 6;
    else if (P->health > 80) f = 0;
    else if (P->health > 60) f = 1;
    else if (P->health > 40) f = 2;
    else if (P->health > 20) f = 3;
    else f = 4;
    if (f != P->cur_face) {
        P->cur_face = f;
        vq_vram(face_data[P->skin] + f * 96, (T_FACE + p * 12) * 32, 384);
    }
}

static void msg_line(u8 p)
{
    Player *P = &pl[p];
    u8 c = p * 21 + 1;
    const char *s = "";
    switch (P->msg) {
    case MSG_FRAGGED:   s = p ? "FRAGGED P1!" : "FRAGGED P2!"; break;
    case MSG_FRAGGEDBY: s = p ? "FRAGGED BY P1" : "FRAGGED BY P2"; break;
    case MSG_SUICIDE:   s = "SUICIDE!"; break;
    case MSG_RESPAWN:   s = "RESPAWNED"; break;
    case MSG_POWER:     s = "BERSERK!"; break;
    case MSG_SUDDEN:    s = "SUDDEN DEATH!"; break;
    case MSG_GOT: {
        char buf[16];
        const char *w = weapon_names[P->weapon];
        u8 i = 0;
        buf[0] = 'G'; buf[1] = 'O'; buf[2] = 'T'; buf[3] = ' ';
        while (w[i] && i < 9) { buf[4 + i] = w[i]; i++; }
        buf[4 + i] = 0;
        hud_pad(22, c, buf, 13, 1);
        goto power;
    }
    }
    if (P->dead > 45 && P->msg == MSG_NONE) s = "PRESS B";
    hud_pad(22, c, s, 13, 1);
power:
    if (shield[p]) hud_pad(23, c, "SHIELD", 13, 0);
    else if (P->power) {
        char buf[14] = "BERSERK  ";
        num_str(buf + 8, P->power / 60, 2, 0);
        hud_pad(23, c, buf, 13, 0);
    } else hud_pad(23, c, "", 13, 0);
}

static void arms(u8 p)
{
    static const char keys[] = "1234567890B";
    Player *P = &pl[p];
    u8 c = p * 21 + 1, w;
    u16 *row = HROW(26);
    for (w = 0; w < NWEAPONS; w++) {
        u8 ch = keys[w];
        if (!(P->owned & (1 << w))) row[c + w] = ATTR_TXT(T_FONT + '.' - 32);
        else row[c + w] = ATTR_TXT((w == P->weapon ? T_FONTY : T_FONT) + ch - 32);
    }
    hud_dirty |= 1 << (26 - HUD_ROW);
}

static void radar(u8 p)
{
    Player *P = &pl[p], *O = &pl[p ^ 1];
    s8 nx = -1, ny = -1;
    if (!O->dead) {
        s16 dep, lat;
        project(P->a, O->x - P->x, O->y - P->y, &dep, &lat);
        dep >>= 7; lat >>= 7;
        if (lat > 13) lat = 13;
        if (lat < -13) lat = -13;
        if (dep > 13) dep = 13;
        if (dep < -13) dep = -13;
        nx = 15 + lat; ny = 15 - dep;
    }
    if (nx == dot_x[p] && ny == dot_y[p]) return;
    dot_x[p] = nx; dot_y[p] = ny;
    memcpy(radar_buf[p], radar_tiles, 512);
    {
        /* self: white 2x2 at the centre; opponent: red 2x2 */
        u8 k;
        for (k = 0; k < 2; k++) {
            s8 x0 = k ? nx : 15, y0 = k ? ny : 15;
            u8 col = k ? 3 : 2;       /* gen palette: 2 white-ish, 3 red (fixed entries) */
            u8 xx, yy;
            if (x0 < 0) continue;
            for (yy = y0; yy < y0 + 2; yy++)
                for (xx = x0; xx < x0 + 2; xx++) {
                    u32 *w = &radar_buf[p][((yy >> 3) * 4 + (xx >> 3)) * 8 + (yy & 7)];
                    u8 sh = 28 - 4 * (xx & 7);
                    *w = (*w & ~(0xFUL << sh)) | ((u32)col << sh);
                }
        }
    }
    vq_vram(radar_buf[p], (T_RADAR + p * 16) * 32, 512);
}

static void clock_draw(void)
{
    u16 sec;
    u8 p;
    char buf[12];
    if (!time_limit) return;
    sec = (time_left + 59) / 60;
    if (sec == clock_sec) return;
    clock_sec = sec;
    buf[0] = 'T'; buf[1] = 'I'; buf[2] = 'M'; buf[3] = 'E'; buf[4] = ' ';
    buf[5] = '0' + sec / 600; buf[6] = '0' + (sec / 60) % 10; buf[7] = ':';
    buf[8] = '0' + (sec % 60) / 10; buf[9] = '0' + sec % 10; buf[10] = 0;
    for (p = 0; p < 2; p++) hud_text(24, p * 21 + 1, buf, 0);
}

static void hud_player(u8 p)
{
    Player *P = &pl[p];
    u8 d = P->dirty, c = p * 21;
    if (P->ouch && P->cur_face != 5) d |= D_FACE;
    if (P->dead > 45 && P->dead < 52) d |= D_MSG;
    if (P->cur_hand != P->weapon) hand_upload(p);
    if (!d) return;
    P->dirty = 0;
    if (d & D_HEALTH) big_num(c + 5, P->health, 3, 1);
    if (d & D_ARMOR) big_num(c + 10, P->armor, 3, 1);
    if (d & D_WEAPON) {
        if (P->weapon == W_PISTOL) {
            u8 i;
            for (i = 0; i < 3; i++) {
                HROW(18)[c + 15 + i] = ATTR_TXT(T_BIGDIG + 11 * 2);
                HROW(19)[c + 15 + i] = ATTR_TXT(T_BIGDIG + 11 * 2 + 1);
            }
        } else big_num(c + 15, P->ammo[P->weapon], 3, 0);
        hud_pad(21, c + 5, weapon_names[P->weapon], 10, 0);
        arms(p);
    }
    if (d & D_FRAGS) {
        char buf[4];
        num_str(buf, P->frags, 3, 1);
        hud_text(17, c + 16, buf, 0);
    }
    if (d & D_MSG) msg_line(p);
    if (d & D_FACE) face_update(p);
}

static void flush_rows(void)
{
    u8 r;
    if (!hud_dirty) return;
    for (r = 0; r < HUD_ROWS; r++)
        if (hud_dirty & (1 << r)) {
            u16 a = (HUD_ROW + r) * PLANE_W * 2;
            vq_vram(hud_map[r], PLANEA0_ADDR + a, 80);
            vq_vram(hud_map[r], PLANEA1_ADDR + a, 80);
        }
    hud_dirty = 0;
}

void hud_draw(void)
{
    hud_player(0);
    hud_player(1);
    radar_turn ^= 1;
    radar(radar_turn);
    clock_draw();
    flush_rows();
}

void hud_init(void)
{
    memset(hud_map, 0, sizeof(hud_map));
}

void hud_full(void)
{
    u8 p, x, y;
    clock_sec = 0xFFFF;
    hud_init();
    for (p = 0; p < 2; p++) {
        Player *P = &pl[p];
        u8 c = p * 21;
        hud_text(17, c + 1, p ? "P2" : "P1", 1);
        hud_pad(17, c + 4, skin_names[P->skin], 8, 0);
        hud_text(17, c + 13, "FR", 1);
        hud_text(20, c + 5, "HLTH", 1);
        hud_text(20, c + 10, "ARMR", 1);
        hud_text(20, c + 15, "AMMO", 1);
        hud_text(25, c + 1, "ARMS", 1);
        for (y = 0; y < 4; y++)
            for (x = 0; x < 3; x++)
                HROW(18 + y)[c + 1 + x] = TILE_ATTR_FULL(2 + p, 1, 0, 0, T_FACE + p * 12 + y * 3 + x);
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                HROW(22 + y)[c + 15 + x] = ATTR_TXT(T_RADAR + p * 16 + y * 4 + x);
        dot_x[p] = dot_y[p] = -2;
        P->cur_face = 0xFF;
        P->dirty = D_ALL;
        hud_player(p);
        radar(p);
    }
    hud_dirty = 0xFFF;
    flush_rows();
}

/* text screens: writes plane A0 directly (interrupts held off around VDP access) */
void print_at(u8 x, u8 y, const char *s, u8 yellow)
{
    u16 row[40];
    u8 n = 0;
    while (s[n] && x + n < 40) n++;
    text_at(row, 0, s, yellow);
    SYS_disableInts();
    VDP_setTileMapDataRow(BG_A, row, y, x, n, CPU);
    SYS_enableInts();
}

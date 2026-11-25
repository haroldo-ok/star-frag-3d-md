/* game.c - deathmatch rules: two players, Brutal Doom weapons, projectiles,
 * items, frags (ported from the Master System version, with 11 weapons) */
#include "game.h"

u8 map[1024];
Obj objs[MAXOBJ];
Player pl[2];
u8 level, frag_limit = 10, game_result;
u8 shield[2], hit_t[2], hit_dir[2];
u16 time_left;
u8 time_limit, sudden_death, bot_skill = 1;
static u16 dmg_sx, dmg_sy;
static u8 dmg_explosive;
#define SHIELD_TICKS 90
static u8 tn;
static u8 tickno;
static u8 spawns[12];
static u8 mover = 0xFF;
static u16 rng = 0x1234;

#define PRAD 0x50
#define PICK_R 0xB0

/*                           PIST SHOT SSG RIFLE MINI GREN ROCK PLAS FLAME RAIL BFG */
static const u8 w_cool[]  = { 14,  38,  56,  7,    3,   36,  42,  9,   4,    70,  80 };
static const u8 w_dmg[]   = { 12,  7,   7,   10,   7,   90,  100, 15,  6,    80,  160 };
static const u8 w_pell[]  = { 1,   7,   14,  1,    1,   0,   0,   0,   0,    1,   0 };
static const u8 w_spread[]= { 0,   20,  34,  0,    6,   0,   0,   0,   0,    0,   0 };
static const u8 w_use[]   = { 0,   1,   2,   1,    1,   1,   1,   1,   1,    1,   10 };
static const u8 w_max[]   = { 0,   50,  50,  200,  250, 30,  30,  150, 200,  25,  60 };
static const u8 w_pick[]  = { 0,   16,  16,  60,   80,  8,   8,   40,  60,   10,  40 };
static const u8 w_box[]   = { 0,   8,   8,   30,   40,  4,   4,   20,  30,   5,   20 };
static const u8 w_sfx[]   = { SFX_PISTOL, SFX_SHOTGUN, SFX_SSG, SFX_RIFLE, SFX_MINIGUN, SFX_GREN,
                              SFX_ROCKET, SFX_PLASMA, SFX_FLAME, SFX_RAIL, SFX_BFG };
static const u8 w_pref[]  = { W_BFG, W_RAIL, W_ROCKET, W_SSG, W_PLASMA, W_MINIGUN, W_RIFLE,
                              W_SHOTGUN, W_FLAME, W_GREN, W_PISTOL };

u8 rand8(void)
{
    rng = rng * 109 + 89;
    return (u8)(rng >> 8);
}

static u8 dec_sat(u8 v, u8 n) { return v > n ? v - n : 0; }

static inline u8 cell(u16 x, u16 y) { return map[(((y >> 8) & 31) << 5) | ((x >> 8) & 31)]; }

static Obj *new_obj(void)
{
    u8 i;
    for (i = 0; i < MAXOBJ; i++)
        if (objs[i].type == OT_NONE) {
            objs[i].state = 0; objs[i].timer = 0; objs[i].anim = 0;
            objs[i].vx = objs[i].vy = 0;
            return &objs[i];
        }
    return 0;
}

static void effect(u8 type, u16 x, u16 y, u8 t)
{
    Obj *o = new_obj();
    if (!o) return;
    o->type = type; o->x = x; o->y = y; o->timer = t;
}

u16 approx_dist(s16 dx, s16 dy)
{
    u16 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax + (ay >> 1) - (ay >> 3) : ay + (ax >> 1) - (ax >> 3);
}

u8 los(u16 x0, u16 y0, u16 x1, u16 y1)
{
    s16 dx = (s16)(x1 - x0), dy = (s16)(y1 - y0);
    u16 d = approx_dist(dx, dy);
    u8 n = (u8)(d >> 6) + 1, i;
    s16 sx = dx / n, sy = dy / n;
    for (i = 1; i < n; i++) {
        x0 += sx; y0 += sy;
        if (cell(x0, y0)) return 0;
    }
    return 1;
}

static u8 blocked(u16 x, u16 y, u16 r)
{
    if (cell(x - r, y - r) || cell(x + r, y - r) || cell(x - r, y + r) || cell(x + r, y + r)) return 1;
    if (mover < 2) {
        Player *O = &pl[mover ^ 1];
        if (!O->dead) {
            s16 dx = (s16)(x - O->x), dy = (s16)(y - O->y);
            if (dx > -0x90 && dx < 0x90 && dy > -0x90 && dy < 0x90) return 1;
        }
    }
    return 0;
}

static u8 try_move(u16 *x, u16 *y, s16 mx, s16 my, u16 r)
{
    u8 bumped = 0;
    if (mx) { if (!blocked(*x + mx, *y, r)) *x += mx; else bumped = 1; }
    if (my) { if (!blocked(*x, *y + my, r)) *y += my; else bumped |= 2; }
    return bumped;
}

/* ------------------------------------------------------------ level / players */
static void spawn_player(u8 p, u8 avoid_other)
{
    Player *P = &pl[p], *O = &pl[p ^ 1];
    u8 i, best = 0, k;
    u16 bd = 0;
    k = rand8() % 6;
    for (i = 0; i < 6; i++, k = k == 5 ? 0 : k + 1) {
        u16 x = ((u16)spawns[k * 2] << 8) | 0x80, y = ((u16)spawns[k * 2 + 1] << 8) | 0x80;
        u16 d = avoid_other ? approx_dist((s16)(x - O->x), (s16)(y - O->y)) : 0x7FFF - i;
        if (d > bd) { bd = d; best = k; }
    }
    P->x = ((u16)spawns[best * 2] << 8) | 0x80;
    P->y = ((u16)spawns[best * 2 + 1] << 8) | 0x80;
    {
        static const s8 step[4] = { 1, 32, -1, -32 };
        u16 c0 = ((P->y >> 8) << 5) | (P->x >> 8);
        u8 dir, len, bl = 0;
        P->a = 0;
        for (dir = 0; dir < 4; dir++) {
            u16 c = c0;
            for (len = 0; len < 12; len++) {
                c = (c + step[dir]) & 1023;
                if (map[c]) break;
            }
            if (len > bl) { bl = len; P->a = dir * 256; }
        }
    }
    P->health = 100; P->armor = 0;
    P->weapon = W_PISTOL; P->owned = 1;
    for (i = 0; i < NWEAPONS; i++) P->ammo[i] = 0;
    P->power = 0; P->dead = 0; P->gib = 0;
    shield[p] = SHIELD_TICKS;
    hit_t[p] = 0;
    P->cool = 10; P->wframe = 0; P->bob = 0;
    P->ouch = P->grin = P->attack_t = P->pain_t = 0;
    P->dirty = D_ALL;
    effect(OT_TFOG, P->x, P->y, 24);
}

void level_load(u8 lvl)
{
    const u8 *m = lvl_maps[lvl], *ob = lvl_objs[lvl], *sp = lvl_spawns[lvl];
    u16 i;
    for (i = 0; i < 1024; i++) map[i] = m[i];
    for (i = 0; i < 12; i++) spawns[i] = sp[i];
    for (i = 0; i < MAXOBJ; i++) objs[i].type = OT_NONE;
    while (*ob != 0xFF) {
        Obj *o = new_obj();
        if (!o) break;
        o->type = ob[0];
        o->x = ((u16)ob[1] << 8) | 0x80;
        o->y = ((u16)ob[2] << 8) | 0x80;
        ob += 3;
    }
    game_result = 0;
}

void match_new(void)
{
    u8 p;
    rng ^= vbl_count;
    for (p = 0; p < 2; p++) {
        pl[p].frags = 0;
        pl[p].msg = MSG_NONE; pl[p].msg_t = 0;
        pl[p].prev = 0xFF;
        pl[p].anim = 0;
    }
    pad_press[0] = pad_press[1] = 0;
    pl[0].x = pl[0].y = pl[1].x = pl[1].y = 0;
    spawn_player(0, 0);
    spawn_player(1, 1);
    for (p = 0; p < MAXOBJ; p++) if (objs[p].type == OT_TFOG) objs[p].type = OT_NONE;
    time_left = (u16)time_limit * 3600;
    sudden_death = 0;
    bot_reset();
}

static void message(u8 p, u8 m)
{
    pl[p].msg = m;
    pl[p].msg_t = 150;
    pl[p].dirty |= D_MSG;
}

/* ------------------------------------------------------------ damage */
static void kill(u8 q, u8 a)
{
    Player *Q = &pl[q];
    Q->dead = 1;
    Q->health = 0;
    Q->gib = dmg_explosive;
    Q->dirty |= D_HEALTH | D_FACE;
    sfx(dmg_explosive ? SFX_GIB : SFX_DIE, q);
    if (dmg_explosive) {
        effect(OT_BLOOD, Q->x, Q->y, 16);
        effect(OT_BLOOD, Q->x + 0x30, Q->y - 0x20, 14);
        effect(OT_BLOOD, Q->x - 0x28, Q->y + 0x20, 12);
    }
    if (a == q) {
        Q->frags--;
        message(q, MSG_SUICIDE);
    } else {
        pl[a].frags++;
        pl[a].grin = 70;
        pl[a].dirty |= D_FACE;
        message(a, MSG_FRAGGED);
        message(q, MSG_FRAGGEDBY);
        if ((pl[a].frags >= (s8)frag_limit || sudden_death) && !game_result) game_result = a + 1;
    }
    pl[0].dirty |= D_FRAGS;
    pl[1].dirty |= D_FRAGS;
}

static void damage(u8 q, u8 a, u8 dmg)
{
    Player *Q = &pl[q];
    if (Q->dead || !dmg || shield[q]) return;
    if (pl[a].power && a != q) dmg = dmg > 127 ? 255 : dmg * 2;
    {
        s16 dep, lat;
        project(Q->a, dmg_sx - Q->x, dmg_sy - Q->y, &dep, &lat);
        if ((lat < 0 ? -lat : lat) > (dep < 0 ? -dep : dep)) hit_dir[q] = lat > 0 ? HIT_RIGHT : HIT_LEFT;
        else hit_dir[q] = dep >= 0 ? HIT_FRONT : HIT_BACK;
        hit_t[q] = 60;
    }
    if (Q->armor) {
        u8 save = dmg - dmg / 3;
        if (save > Q->armor) save = Q->armor;
        Q->armor -= save;
        dmg -= save;
        Q->dirty |= D_ARMOR;
    }
    Q->health = Q->health > dmg ? Q->health - dmg : 0;
    Q->ouch = 20;
    Q->pain_t = 10;
    Q->dirty |= D_HEALTH | D_FACE;
    if (Q->health == 0) kill(q, a);
    else sfx(SFX_HURT, q);
}

static void explode(Obj *o, u16 radius)
{
    u8 q;
    for (q = 0; q < 2; q++) {
        Player *Q = &pl[q];
        u16 d;
        if (Q->dead) continue;
        d = approx_dist((s16)(Q->x - o->x), (s16)(Q->y - o->y));
        if (d < radius && los(o->x, o->y, Q->x, Q->y)) {
            u8 dmg = (u8)(((u32)o->dmg * (radius - d)) / radius);
            if (q == o->owner) dmg >>= 1;
            dmg_sx = o->x; dmg_sy = o->y;
            dmg_explosive = 1;
            damage(q, o->owner, dmg);
            dmg_explosive = 0;
        }
    }
    o->type = OT_EXPLO;
    o->timer = 18;
    sfx(SFX_EXPLODE, o->owner);
}

/* ------------------------------------------------------------ weapons */
u8 weapon_ammo_ok(Player *P, u8 w)
{
    return (P->owned & (1 << w)) && (w == W_PISTOL || P->ammo[w] >= w_use[w]);
}

void select_best_weapon(u8 p)
{
    Player *P = &pl[p];
    u8 i;
    for (i = 0; i < NWEAPONS; i++)
        if (weapon_ammo_ok(P, w_pref[i])) break;
    if (i == NWEAPONS) i = NWEAPONS - 1;
    if (P->weapon != w_pref[i]) {
        P->weapon = w_pref[i];
        P->dirty |= D_WEAPON;
        P->cool = 8;
    }
}

static void set_weapon(u8 p, u8 w)
{
    Player *P = &pl[p];
    if (P->weapon != w) { P->weapon = w; P->dirty |= D_WEAPON; P->cool = 8; }
}

static void next_weapon(u8 p)
{
    Player *P = &pl[p];
    u8 w = P->weapon, i;
    for (i = 0; i < NWEAPONS; i++) {
        w = w == NWEAPONS - 1 ? 0 : w + 1;
        if (weapon_ammo_ok(P, w)) break;
    }
    set_weapon(p, w);
    sfx(SFX_SELECT, p);
}

/* hitscan with pellets: each pellet is an angle offset; hit if the opponent's
   lateral offset at its depth is within reach (a little auto-aim) */
static void hitscan(u8 p, u8 w)
{
    Player *P = &pl[p], *Q = &pl[p ^ 1];
    u16 wall = zbuf[p][NRAYS / 2];
    u16 hitd = wall > 0x30 ? wall - 0x30 : 0x10;
    s16 ca = sin8[(P->a + 256) & 1023], sa = sin8[P->a & 1023];
    u8 hits = 0, k, n = w_pell[w];
    s16 depth = 0, lat = 0;
    u8 cansee = 0;
    if (!Q->dead) {
        project(P->a, Q->x - P->x, Q->y - P->y, &depth, &lat);
        cansee = depth > 0x30 && (u16)depth < wall + 0x60 && los(P->x, P->y, Q->x, Q->y);
    }
    for (k = 0; k < n; k++) {
        s16 off = w_spread[w] ? (s16)(rand8() % (2 * w_spread[w] + 1)) - w_spread[w] : 0;   /* 1/1024 turns */
        if (cansee) {
            /* lateral miss at the target's depth: depth * tan(off) ~ depth * off * 6 / 1024 */
            s16 l = lat - (s16)(((s32)depth * off * 6) >> 10);
            if (l < 0) l = -l;
            if ((u16)l < 0x38 + ((u16)depth >> 3)) hits++;
        }
    }
    if (hits) {
        u16 dmg = (u16)w_dmg[w] * hits;
        hitd = depth;
        dmg_sx = P->x; dmg_sy = P->y;
        damage(p ^ 1, p, dmg > 255 ? 255 : dmg);
        effect(OT_BLOOD, Q->x, Q->y, 12);
    }
    if (w == W_RAIL) {
        u16 d = 0x100;
        u8 m = 0;
        while (d < hitd && m < 6) {
            effect(OT_SPARK, P->x + (s16)((ca * (s16)(d >> 4)) >> 4), P->y + (s16)((sa * (s16)(d >> 4)) >> 4), 20 + m * 3);
            d += 0x100; m++;
        }
    }
    if (!hits) {
        effect(OT_PUFF, P->x + (s16)((ca * (s16)(hitd >> 4)) >> 4), P->y + (s16)((sa * (s16)(hitd >> 4)) >> 4), 10);
        if ((rand8() & 3) == 0) sfx(SFX_PUFF, p);
    }
}

static void launch(u8 p, u8 type, u8 speed, u8 life)
{
    Player *P = &pl[p];
    Obj *o = new_obj();
    u16 a = P->a;
    s16 ca, sa;
    if (!o) return;
    if (type == OT_FLAME) a = (a + (rand8() & 15) - 8) & 1023;
    ca = sin8[(a + 256) & 1023]; sa = sin8[a & 1023];
    o->type = type;
    o->owner = p;
    o->dmg = w_dmg[P->weapon];
    o->timer = life;
    o->vx = (ca * speed) >> 8;
    o->vy = (sa * speed) >> 8;
    o->x = P->x + (ca >> 2);
    o->y = P->y + (sa >> 2);
    if (cell(o->x, o->y)) {
        o->x = P->x; o->y = P->y;
        if (type == OT_ROCKET || type == OT_GRENADE || type == OT_BFGBALL) explode(o, type == OT_BFGBALL ? 0x300 : 0x200);
        else o->type = OT_NONE;
    }
}

static void fire(u8 p)
{
    Player *P = &pl[p];
    u8 w = P->weapon;
    if (w != W_PISTOL) {
        if (P->ammo[w] < w_use[w]) { sfx(SFX_EMPTY, p); select_best_weapon(p); return; }
        P->ammo[w] -= w_use[w];
    }
    P->cool = w_cool[w];
    if (shield[p]) { shield[p] = 0; P->dirty |= D_MSG; }
    P->wframe = 1;
    P->attack_t = 8;
    P->dirty |= D_WEAPON;
    sfx(w_sfx[w], p);
    switch (w) {
    case W_PLASMA:  launch(p, OT_PLASMA, 0x30, 70); break;
    case W_GREN:    launch(p, OT_GRENADE, 0x1A, 80); break;
    case W_ROCKET:  launch(p, OT_ROCKET, 0x2C, 120); break;
    case W_FLAME:   launch(p, OT_FLAME, 0x1C, 22); break;
    case W_BFG:     launch(p, OT_BFGBALL, 0x22, 150); break;
    default:        hitscan(p, w); break;
    }
    if (w != W_PISTOL && P->ammo[w] < w_use[w]) select_best_weapon(p);
}

/* ------------------------------------------------------------ objects */
static void projectile_tick(Obj *o)
{
    u8 t, q;
    for (t = 0; t < tn; t++) {
        u16 nx = o->x + o->vx, ny = o->y + o->vy;
        u8 big = o->type == OT_ROCKET || o->type == OT_GRENADE || o->type == OT_BFGBALL;
        u16 rad = o->type == OT_BFGBALL ? 0x300 : 0x200;
        if (o->type == OT_GRENADE) {
            u8 b = 0;
            if (cell(nx, o->y)) { o->vx = -o->vx + (o->vx >> 2); b = 1; } else o->x = nx;
            if (cell(o->x, ny)) { o->vy = -o->vy + (o->vy >> 2); b = 1; } else o->y = ny;
            if (b) sfx(SFX_BOUNCE, o->owner);
            if ((o->timer & 7) == 0) { o->vx -= o->vx >> 3; o->vy -= o->vy >> 3; }
        } else if (cell(nx, ny)) {
            if (big) explode(o, rad);
            else if (o->type == OT_PLASMA) { o->type = OT_SPARK; o->timer = 10; }
            else o->type = OT_NONE;
            return;
        } else {
            o->x = nx; o->y = ny;
        }
        o->anim++;
        for (q = 0; q < 2; q++) {
            Player *Q = &pl[q];
            s16 dx, dy;
            if (Q->dead || (q == o->owner && o->anim < 12)) continue;
            dx = (s16)(Q->x - o->x); dy = (s16)(Q->y - o->y);
            if (dx > -0x48 && dx < 0x48 && dy > -0x48 && dy < 0x48) {
                dmg_sx = o->x - o->vx * 4; dmg_sy = o->y - o->vy * 4;
                if (big) {
                    damage(q, o->owner, o->dmg >> 2);
                    explode(o, rad);
                } else {
                    damage(q, o->owner, o->dmg);
                    if (o->type == OT_PLASMA) { o->type = OT_SPARK; o->timer = 10; }
                    else o->type = OT_NONE;
                }
                return;
            }
        }
        if (--o->timer == 0) {
            if (o->type == OT_GRENADE) explode(o, rad);
            else o->type = OT_NONE;
            return;
        }
    }
}

static void pickup(Player *P, u8 p, Obj *o)
{
    u8 t = o->type, took = 0;
    if (t == OT_MEDKIT) {
        if (P->health < 100) { P->health = P->health > 75 ? 100 : P->health + 25; P->dirty |= D_HEALTH | D_FACE; took = 1; }
    } else if (t == OT_ARMOR) {
        if (P->armor < 100) { P->armor = P->armor > 50 ? 100 : P->armor + 50; P->dirty |= D_ARMOR; took = 4; }
    } else if (t == OT_AMMO) {
        u8 w;
        for (w = 1; w < NWEAPONS; w++)
            if ((P->owned & (1 << w)) && P->ammo[w] < w_max[w]) {
                u16 v = P->ammo[w] + w_box[w];
                P->ammo[w] = v > w_max[w] ? w_max[w] : v;
                took = 1;
            }
        if (took) P->dirty |= D_WEAPON;
    } else if (t <= OT_W_BFG) {
        u8 w = t - OT_W_SHOTGUN + W_SHOTGUN;
        u16 mask = 1 << w;
        if (!(P->owned & mask) || P->ammo[w] < w_max[w]) {
            u16 v = P->ammo[w] + ((P->owned & mask) ? w_pick[w] >> 1 : w_pick[w]);
            P->ammo[w] = v > w_max[w] ? w_max[w] : v;
            if (!(P->owned & mask)) {
                P->owned |= mask;
                set_weapon(p, w);
                message(p, MSG_GOT);
            }
            P->dirty |= D_WEAPON;
            took = 2;
        }
    } else {
        P->power = 1200;
        message(p, MSG_POWER);
        took = 3;
        sfx(SFX_POWER, p);
    }
    if (!took) return;
    if (took != 3) sfx(took == 2 ? SFX_WEAPON : took == 4 ? SFX_ARMOR : SFX_PICK, p);
    o->state = 1;
    o->timer = t == OT_POWER ? 2400 : (t >= OT_W_SHOTGUN ? 900 : 600);
}

static u8 pcx[2], pcy[2];

static void item_tick(Obj *o)
{
    u8 p, ox, oy;
    if (o->state) {
        if (o->timer > tn) o->timer -= tn;
        else { o->state = 0; effect(OT_TFOG, o->x, o->y, 16); }
        return;
    }
    ox = o->x >> 8; oy = o->y >> 8;
    for (p = 0; p < 2; p++) {
        Player *P;
        s16 dx, dy;
        if ((u8)(pcx[p] - ox + 1) > 2 || (u8)(pcy[p] - oy + 1) > 2) continue;
        P = &pl[p];
        dx = (s16)(P->x - o->x); dy = (s16)(P->y - o->y);
        if (dx > -PICK_R && dx < PICK_R && dy > -PICK_R && dy < PICK_R) {
            pickup(P, p, o);
            if (o->state) return;
        }
    }
}

/* ------------------------------------------------------------ players */
static void player_tick(u8 p)
{
    Player *P = &pl[p];
    u8 keys, press, n = tn;
    s16 ca, sa;
    if (P->cpu) {
        keys = bot_keys(p);
        press = keys & ~P->prev;
    } else {
        keys = pad_held[p];
        press = pad_press[p];
        pad_press[p] = 0;
        keys |= press & K_FIRE;
    }
    P->prev = keys;
    if (P->msg_t) { P->msg_t = dec_sat(P->msg_t, n); if (!P->msg_t) { P->msg = MSG_NONE; P->dirty |= D_MSG; } }
    P->attack_t = dec_sat(P->attack_t, n);
    P->pain_t = dec_sat(P->pain_t, n);
    if (P->ouch) { P->ouch = dec_sat(P->ouch, n); if (!P->ouch) P->dirty |= D_FACE; }
    if (P->grin) { P->grin = dec_sat(P->grin, n); if (!P->grin) P->dirty |= D_FACE; }
    if (P->power) {
        u8 before = (u8)(P->power / 60);
        P->power = P->power > n ? P->power - n : 0;
        if ((u8)(P->power / 60) != before) P->dirty |= D_MSG;
    }
    if (P->dead) {
        u16 d = (u16)P->dead + n;
        P->dead = d > 250 ? 250 : (u8)d;
        if ((P->dead > 45 && (press & K_FIRE)) || P->dead >= 240) {
            spawn_player(p, 1);
            sfx(SFX_RESPAWN, p);
            message(p, MSG_RESPAWN);
            P->msg_t = 60;
        }
        return;
    }
    if (P->cool) {
        P->cool = dec_sat(P->cool, n);
        {
            u8 e = w_cool[P->weapon] - P->cool;
            if (w_cool[P->weapon] < 8) P->wframe = P->cool ? 1 + (tickno >> 1 & 1) : 0;
            else P->wframe = e < 4 ? 1 : e < 8 ? 2 : 0;
        }
    }
    if (press & K_NEXT) {
        if (!P->cpu) next_weapon(p);
    }
    mover = p;
    if (keys & K_STRAFE) {
        s16 rx = (sin8[(P->a + 512) & 1023] * 3) >> 6;
        s16 ry = (sin8[(P->a + 256) & 1023] * 3) >> 6;
        rx *= n; ry *= n;
        if (keys & K_LEFT) try_move(&P->x, &P->y, -rx, -ry, PRAD);
        if (keys & K_RIGHT) try_move(&P->x, &P->y, rx, ry, PRAD);
    } else {
        if (keys & K_LEFT) P->a = (P->a - 7 * n) & 1023;
        if (keys & K_RIGHT) P->a = (P->a + 7 * n) & 1023;
    }
    ca = sin8[(P->a + 256) & 1023];
    sa = sin8[P->a & 1023];
    if (keys & (K_UP | K_DOWN)) {
        s16 mx = ((ca * 3) >> 6) * n, my = ((sa * 3) >> 6) * n;
        if (keys & K_DOWN) { mx = -mx; my = -my; }
        try_move(&P->x, &P->y, mx, my, PRAD);
    }
    if (keys & (K_UP | K_DOWN | K_LEFT | K_RIGHT)) {
        P->anim += n;
        P->bob = (tickno >> 2) & 3;
        if (P->bob == 3) P->bob = 1;
    }
    mover = 0xFF;
    if ((keys & K_FIRE) && !P->cool) fire(p);
}

void game_tick(u8 n)
{
    u8 i;
    Obj *o;
    tn = n;
    tickno += n;
    for (i = 0; i < 2; i++) {
        if (shield[i]) { shield[i] = dec_sat(shield[i], n); if (!shield[i]) pl[i].dirty |= D_MSG; }
        hit_t[i] = dec_sat(hit_t[i], n);
    }
    if (time_left && !game_result) {
        if (time_left > n) time_left -= n;
        else {
            time_left = 0;
            if (pl[0].frags != pl[1].frags) game_result = pl[0].frags > pl[1].frags ? 1 : 2;
            else {
                sudden_death = 1;
                message(0, MSG_SUDDEN);
                message(1, MSG_SUDDEN);
                sfx(SFX_POWER, 0);
            }
        }
    }
    player_tick(0);
    player_tick(1);
    for (i = 0; i < 2; i++) {
        pcx[i] = pl[i].dead ? 0x80 : pl[i].x >> 8;
        pcy[i] = pl[i].dead ? 0x80 : pl[i].y >> 8;
    }
    for (i = 0, o = objs; i < MAXOBJ; i++, o++) {
        u8 t = o->type;
        if (t == OT_NONE) continue;
        if (t <= OT_LAST_ITEM) item_tick(o);
        else if (t <= OT_LAST_PROJ) projectile_tick(o);
        else {
            if (o->timer > n) o->timer -= n;
            else o->type = OT_NONE;
        }
    }
}

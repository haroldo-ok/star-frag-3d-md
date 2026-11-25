/* bot.c - the CPU opponent.  It produces pad bits, so it moves, turns and fires
 * under exactly the same rules as a human player. */
#include "game.h"

static u16 last_x, last_y;
static u8 stuck_t, turn_t, turn_dir, strafe_t, think, vis, seen;
static u16 seen_at;

/*                                 EASY NORMAL HARD */
static const u8 skill_react[3] = { 50,  22,    8 };
static const u8 skill_cone[3]  = { 0x14, 0x30, 0x48 };
static const u8 skill_strafe[3] = { 0, 1, 1 };

static u8 dist[1024];
static u16 bfs_q[1024];
static u16 bfs_target = 0xFFFF;
static u8 bfs_age;

void bot_reset(void)
{
    bfs_target = 0xFFFF;
    seen = 0; turn_t = 0; stuck_t = 0;
}

static void bfs(u16 t)
{
    u16 i, head = 0, tail = 0;
    for (i = 0; i < 1024; i++) dist[i] = 0xFF;
    bfs_target = t;
    bfs_age = 0;
    if (map[t]) return;
    dist[t] = 0;
    bfs_q[tail++] = t;
    while (head != tail) {
        u16 c = bfs_q[head++];
        u8 d = dist[c] + 1;
        if (d == 0xFF) d = 0xFE;
        if (!map[c - 1] && dist[c - 1] == 0xFF) { dist[c - 1] = d; bfs_q[tail++] = c - 1; }
        if (!map[c + 1] && dist[c + 1] == 0xFF) { dist[c + 1] = d; bfs_q[tail++] = c + 1; }
        if (c >= 32 && !map[c - 32] && dist[c - 32] == 0xFF) { dist[c - 32] = d; bfs_q[tail++] = c - 32; }
        if (c + 32 < 1024 && !map[c + 32] && dist[c + 32] == 0xFF) { dist[c + 32] = d; bfs_q[tail++] = c + 32; }
    }
}

static u16 cell_of(u16 x, u16 y) { return (((y >> 8) & 31) << 5) | ((x >> 8) & 31); }

static void next_step(Player *P, u16 *gx, u16 *gy)
{
    u16 c = cell_of(P->x, P->y), best = c;
    u8 bd = dist[c];
    if (dist[c - 1] < bd) { bd = dist[c - 1]; best = c - 1; }
    if (dist[c + 1] < bd) { bd = dist[c + 1]; best = c + 1; }
    if (c >= 32 && dist[c - 32] < bd) { bd = dist[c - 32]; best = c - 32; }
    if (c + 32 < 1024 && dist[c + 32] < bd) { bd = dist[c + 32]; best = c + 32; }
    *gx = ((best & 31) << 8) | 0x80;
    *gy = ((best >> 5) << 8) | 0x80;
}

static u8 steer(Player *P, u16 x, u16 y, u8 *ahead)
{
    s16 depth, lat;
    project(P->a, x - P->x, y - P->y, &depth, &lat);
    *ahead = depth > 0 && (lat < 0 ? -lat : lat) < skill_cone[bot_skill] + (depth >> 3);
    if (depth <= 0) return lat >= 0 ? K_RIGHT : K_LEFT;
    if (lat > (depth >> 4) + 8) return K_RIGHT;
    if (lat < -(depth >> 4) - 8) return K_LEFT;
    return 0;
}

u8 bot_keys(u8 p)
{
    Player *P = &pl[p], *O = &pl[p ^ 1];
    u8 keys = 0, ahead;
    u16 d, gx, gy;
    if (P->dead) { bfs_target = 0xFFFF; return (P->dead > 70 && (P->dead & 8)) ? K_FIRE : 0; }
    if (bfs_age < 255) bfs_age++;
    if (++think >= 4) {
        think = 0;
        vis = !O->dead && los(P->x, P->y, O->x, O->y);
        select_best_weapon(p);
    }
    d = approx_dist((s16)(O->x - P->x), (s16)(O->y - P->y));
    if (vis) {
        keys = steer(P, O->x, O->y, &ahead);
        if (!seen) { seen = 1; seen_at = vbl_count; }
        if (seen == 1 && (u16)(vbl_count - seen_at) > skill_react[bot_skill]) seen = 2;
        if (ahead && seen == 2) keys |= K_FIRE;
        if (d > 0x380) keys |= K_UP;
        else if (d < 0x180 && P->weapon != W_FLAME && P->weapon != W_SSG && P->weapon != W_SHOTGUN) keys |= K_DOWN;
        else if (P->weapon == W_FLAME || P->weapon == W_SSG) keys |= K_UP;
        if (++strafe_t > 90) strafe_t = 0;
        if (skill_strafe[bot_skill] && ahead && strafe_t > 50)
            keys = (keys & ~(K_LEFT | K_RIGHT)) | K_STRAFE | (strafe_t > 70 ? K_LEFT : K_RIGHT);
    } else {
        u16 t;
        seen = 0;
        t = cell_of(O->x, O->y);
        if (O->dead && bfs_target != 0xFFFF) t = bfs_target;
        if (bfs_target == 0xFFFF || (t != bfs_target && bfs_age > 20)) bfs(t);
        next_step(P, &gx, &gy);
        keys = steer(P, gx, gy, &ahead);
        if (ahead || !(keys & (K_LEFT | K_RIGHT))) keys |= K_UP;
        if (turn_t) {
            turn_t--;
            keys = (keys & ~(K_LEFT | K_RIGHT | K_UP)) | turn_dir | K_DOWN;
        }
    }
    if ((keys & K_UP) && approx_dist((s16)(P->x - last_x), (s16)(P->y - last_y)) < 2) {
        if (++stuck_t > 6) {
            stuck_t = 0;
            turn_t = 12 + (rand8() & 31);
            turn_dir = (rand8() & 1) ? K_LEFT : K_RIGHT;
        }
    } else stuck_t = 0;
    last_x = P->x; last_y = P->y;
    return keys;
}

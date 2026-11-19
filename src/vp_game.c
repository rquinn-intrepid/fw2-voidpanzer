#include "vp_game.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f

static uint32_t rnd(vp_game_t *g)
{
    uint32_t s = g->rng ? g->rng : 0x2545F491u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    g->rng = s;
    return s;
}
static float rndf(vp_game_t *g, float lo, float hi)
{
    return lo + (hi - lo) * (float)(rnd(g) & 0xFFFFFF) / (float)0x1000000;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float dist2(float ax, float az, float bx, float bz)
{
    float dx = ax - bx, dz = az - bz;
    return dx * dx + dz * dz;
}

float vp_wrap_angle(float a)
{
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

bool vp_blocked(const vp_game_t *g, float x, float z, float r)
{
    if (fabsf(x) > VP_ARENA - r || fabsf(z) > VP_ARENA - r) return true;
    for (int i = 0; i < g->nobst; i++) {
        float rr = g->obst[i].r + r;
        if (dist2(x, z, g->obst[i].x, g->obst[i].z) < rr * rr) return true;
    }
    return false;
}

// Does the segment pass through any obstacle footprint?
bool vp_los(const vp_game_t *g, float x0, float z0, float x1, float z1)
{
    float dx = x1 - x0, dz = z1 - z0, len2 = dx * dx + dz * dz;
    for (int i = 0; i < g->nobst; i++) {
        const vp_obst_t *o = &g->obst[i];
        float t = len2 > 0 ? ((o->x - x0) * dx + (o->z - z0) * dz) / len2 : 0;
        t = clampf(t, 0, 1);
        if (dist2(x0 + t * dx, z0 + t * dz, o->x, o->z) < o->r * o->r) return false;
    }
    return true;
}

int vp_enemies_alive(const vp_game_t *g)
{
    int n = 0;
    for (int i = 0; i < VP_MAX_ENEMIES; i++) n += g->enemy[i].alive;
    return n;
}

int vp_locked(const vp_game_t *g)
{
    for (int i = 0; i < VP_MAX_ENEMIES; i++) {
        const vp_enemy_t *e = &g->enemy[i];
        if (!e->alive) continue;
        float d2 = dist2(e->x, e->z, g->x, g->z);
        if (d2 > VP_LOCK_RANGE * VP_LOCK_RANGE) continue;
        float bearing = atan2f(e->x - g->x, e->z - g->z);
        float tol = VP_LOCK_ANGLE + atanf(VP_ENEMY_R / sqrtf(d2 + 1e-3f));   // wider when close
        if (fabsf(vp_wrap_angle(bearing - g->yaw)) < tol && vp_los(g, g->x, g->z, e->x, e->z))
            return i;
    }
    return -1;
}

static void gen_obstacles(vp_game_t *g)
{
    g->nobst = 0;
    for (int tries = 0; g->nobst < VP_NUM_OBST && tries < 400; tries++) {
        vp_obst_t o;
        o.kind = (uint8_t)(rnd(g) % 3u);
        o.x = rndf(g, -VP_ARENA + 20, VP_ARENA - 20);
        o.z = rndf(g, -VP_ARENA + 20, VP_ARENA - 20);
        if (o.kind == VP_MONOLITH) { o.r = rndf(g, 1.8f, 2.6f); o.h = rndf(g, 14, 20); }
        else { o.r = rndf(g, 3.0f, 6.0f); o.h = o.kind == VP_CUBE ? 2.0f * o.r : 2.2f * o.r; }
        if (dist2(o.x, o.z, 0, 0) < 30.0f * 30.0f) continue;          // keep the start clear
        bool clash = false;
        for (int i = 0; i < g->nobst; i++) {
            float rr = g->obst[i].r + o.r + 12.0f;
            if (dist2(o.x, o.z, g->obst[i].x, g->obst[i].z) < rr * rr) clash = true;
        }
        if (!clash) g->obst[g->nobst++] = o;
    }
}

static bool spawn_enemy(vp_game_t *g)
{
    for (int i = 0; i < VP_MAX_ENEMIES; i++) {
        vp_enemy_t *e = &g->enemy[i];
        if (e->alive) continue;
        for (int tries = 0; tries < 40; tries++) {
            float a = rndf(g, 0, 2 * PI), d = rndf(g, 70, 110);
            float x = g->x + sinf(a) * d, z = g->z + cosf(a) * d;
            if (vp_blocked(g, x, z, VP_ENEMY_R + 2)) continue;
            memset(e, 0, sizeof *e);
            e->alive = true;
            e->x = x; e->z = z;
            e->yaw = atan2f(g->x - x, g->z - z) + rndf(g, -1.0f, 1.0f);
            e->fire_cd = rndf(g, 5.0f, 8.0f);          // a moment before the first shot
            return true;
        }
        return false;
    }
    return false;
}

static void shatter(vp_game_t *g, float x, float z, int n, float power)
{
    for (int k = 0; k < VP_MAX_SHARDS && n > 0; k++) {
        vp_shard_t *s = &g->shard[k];
        if (s->live) continue;
        float a = rndf(g, 0, 2 * PI), sp = rndf(g, 3, 9) * power;
        *s = (vp_shard_t){ true, x, rndf(g, 0.5f, 2.0f), z, sinf(a) * sp, rndf(g, 4, 10) * power,
                           cosf(a) * sp, rndf(g, 0, 2 * PI), rndf(g, -8, 8), rndf(g, 1.0f, 2.5f),
                           rndf(g, 1.2f, 2.0f) };
        n--;
    }
}

static void kill_enemy(vp_game_t *g, int i)
{
    vp_enemy_t *e = &g->enemy[i];
    if (!e->alive) return;
    e->alive = false;
    g->score += VP_KILL_POINTS;
    g->kills++;
    g->events |= VP_EV_KILL;
    shatter(g, e->x, e->z, 14, 1.0f);
    if (g->spawn_t <= 0.0f) g->spawn_t = VP_SPAWN_DELAY;
}

static void hit_player(vp_game_t *g)
{
    if (g->invuln > 0.0f || g->phase != VP_PLAYING) return;
    g->lives--;
    g->events |= VP_EV_PLAYER_HIT;
    g->hit_flash = 0.4f;
    g->invuln = VP_INVULN;
    g->missiles = VP_MISSILES;                    // a new tank, a full rack
    for (int i = 0; i < VP_MAX_SHELLS; i++) if (g->shell[i].enemy) g->shell[i].live = false;
    // Breathing room for the new tank: the enemies withdraw and come back
    // from a distance after a pause (no points for this).
    for (int i = 0; i < VP_MAX_ENEMIES; i++) g->enemy[i].alive = false;
    g->spawn_t = VP_RESPAWN_PAUSE;
    if (g->lives <= 0) { g->lives = 0; g->phase = VP_GAMEOVER; }
}

static void add_shell(vp_game_t *g, float x, float z, float yaw, bool enemy)
{
    for (int i = 0; i < VP_MAX_SHELLS; i++) {
        vp_shell_t *s = &g->shell[i];
        if (s->live) continue;
        float fx = sinf(yaw), fz = cosf(yaw), off = enemy ? VP_ENEMY_R + 0.5f : VP_TANK_R + 0.5f;
        *s = (vp_shell_t){ true, enemy, x + fx * off, z + fz * off,
                           fx * VP_SHELL_SPEED, fz * VP_SHELL_SPEED, VP_SHELL_LIFE };
        return;
    }
}

static void detonate(vp_game_t *g, vp_missile_t *m)
{
    m->live = false;
    g->events |= VP_EV_BOOM;
    shatter(g, m->x, m->z, 8, 1.4f);
    for (int i = 0; i < VP_MAX_ENEMIES; i++) {
        vp_enemy_t *e = &g->enemy[i];
        if (e->alive && dist2(e->x, e->z, m->x, m->z) < VP_BLAST_R * VP_BLAST_R) kill_enemy(g, i);
    }
}

void vp_new_game(vp_game_t *g, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1u;
    g->lives = VP_LIVES;
    g->missiles = VP_MISSILES;
    gen_obstacles(g);
    g->spawn_t = 1.5f;
}

static void step_enemy(vp_game_t *g, int i, float dt)
{
    vp_enemy_t *e = &g->enemy[i];
    float dx = g->x - e->x, dz = g->z - e->z, d = sqrtf(dx * dx + dz * dz);
    float diff = vp_wrap_angle(atan2f(dx, dz) - e->yaw);
    if (e->avoid_t > 0.0f) {
        e->avoid_t -= dt;
        e->yaw = vp_wrap_angle(e->yaw + e->avoid_dir * VP_ENEMY_TURN * dt);
    } else {
        e->yaw = vp_wrap_angle(e->yaw + clampf(diff, -VP_ENEMY_TURN * dt, VP_ENEMY_TURN * dt));
    }
    if (d > 35.0f || e->avoid_t > 0.0f) {
        float nx = e->x + sinf(e->yaw) * VP_ENEMY_SPEED * dt;
        float nz = e->z + cosf(e->yaw) * VP_ENEMY_SPEED * dt;
        bool block = vp_blocked(g, nx, nz, VP_ENEMY_R) ||
                     dist2(nx, nz, g->x, g->z) < (VP_ENEMY_R + VP_TANK_R) * (VP_ENEMY_R + VP_TANK_R);
        for (int j = 0; j < VP_MAX_ENEMIES; j++)
            if (j != i && g->enemy[j].alive &&
                dist2(nx, nz, g->enemy[j].x, g->enemy[j].z) < 4 * VP_ENEMY_R * VP_ENEMY_R) block = true;
        if (block) {
            if (e->avoid_t <= 0.0f) { e->avoid_t = 1.2f; e->avoid_dir = (rnd(g) & 1) ? 1.0f : -1.0f; }
        } else {
            e->x = nx; e->z = nz;
        }
    }
    e->fire_cd -= dt;
    if (e->fire_cd <= 0.0f && fabsf(diff) < 0.12f && d < 80.0f &&
        vp_los(g, e->x, e->z, g->x, g->z)) {
        add_shell(g, e->x, e->z, e->yaw + rndf(g, -VP_ENEMY_SPREAD, VP_ENEMY_SPREAD), true);
        g->events |= VP_EV_ENEMY_FIRE;
        e->fire_cd = rndf(g, 5.0f, 8.0f);
    }
}

void vp_step(vp_game_t *g, float dt, const vp_input_t *in)
{
    g->events = 0;
    g->time += dt;
    if (g->hit_flash > 0) g->hit_flash -= dt;

    // Shards keep flying even after game over.
    for (int k = 0; k < VP_MAX_SHARDS; k++) {
        vp_shard_t *s = &g->shard[k];
        if (!s->live) continue;
        s->life -= dt;
        if (s->life <= 0) { s->live = false; continue; }
        s->vy -= 9.8f * dt;
        s->x += s->vx * dt; s->y += s->vy * dt; s->z += s->vz * dt; s->a += s->va * dt;
        if (s->y < 0.05f) { s->y = 0.05f; s->vy = 0; s->vx *= 0.8f; s->vz *= 0.8f; s->va *= 0.8f; }
    }
    if (g->phase == VP_GAMEOVER) return;

    if (g->reload > 0) g->reload -= dt;
    if (g->invuln > 0) g->invuln -= dt;

    // Player: turn, then move unless the new spot is blocked (stop cleanly).
    g->yaw = vp_wrap_angle(g->yaw + clampf(in->turn, -1, 1) * VP_TURN_RATE * dt);
    if (in->drive) {
        float sp = in->drive > 0 ? VP_FWD_SPEED : -VP_REV_SPEED;
        float nx = g->x + sinf(g->yaw) * sp * dt, nz = g->z + cosf(g->yaw) * sp * dt;
        bool block = vp_blocked(g, nx, nz, VP_TANK_R);
        for (int i = 0; i < VP_MAX_ENEMIES; i++)
            if (g->enemy[i].alive && dist2(nx, nz, g->enemy[i].x, g->enemy[i].z) <
                (VP_ENEMY_R + VP_TANK_R) * (VP_ENEMY_R + VP_TANK_R)) block = true;
        if (!block) { g->x = nx; g->z = nz; }
    }

    // Weapons: one cannon shell per press (with reload), missiles while stocked.
    if (in->fire && g->reload <= 0.0f) {
        add_shell(g, g->x, g->z, g->yaw, false);
        g->reload = VP_RELOAD;
        g->events |= VP_EV_FIRE;
    }
    if (in->missile) {
        if (g->missiles > 0) {
            for (int i = 0; i < VP_MISSILES; i++) {
                vp_missile_t *m = &g->missile[i];
                if (m->live) continue;
                float fx = sinf(g->yaw), fz = cosf(g->yaw);
                *m = (vp_missile_t){ true, g->x + fx * 3, g->z + fz * 3,
                                     fx * VP_MISSILE_SPEED, fz * VP_MISSILE_SPEED, VP_MISSILE_LIFE };
                g->missiles--;
                g->events |= VP_EV_MISSILE;
                break;
            }
        } else {
            g->events |= VP_EV_EMPTY;
        }
    }

    // Enemies.
    if (g->spawn_t > 0) g->spawn_t -= dt;
    int want = g->score < 3000 ? 1 : 2;
    if (vp_enemies_alive(g) < want && g->spawn_t <= 0) {
        spawn_enemy(g);
        if (vp_enemies_alive(g) < want) g->spawn_t = 1.0f;   // second one a moment later
    }
    for (int i = 0; i < VP_MAX_ENEMIES; i++) if (g->enemy[i].alive) step_enemy(g, i, dt);

    // Shells.
    for (int k = 0; k < VP_MAX_SHELLS; k++) {
        vp_shell_t *s = &g->shell[k];
        if (!s->live) continue;
        s->life -= dt;
        s->x += s->dx * dt; s->z += s->dz * dt;
        if (s->life <= 0 || vp_blocked(g, s->x, s->z, 0.0f)) { s->live = false; continue; }
        if (s->enemy) {
            if (dist2(s->x, s->z, g->x, g->z) < VP_TANK_R * VP_TANK_R) { s->live = false; hit_player(g); }
        } else {
            for (int i = 0; i < VP_MAX_ENEMIES; i++)
                if (g->enemy[i].alive &&
                    dist2(s->x, s->z, g->enemy[i].x, g->enemy[i].z) < VP_ENEMY_R * VP_ENEMY_R) {
                    s->live = false; kill_enemy(g, i); break;
                }
        }
    }

    // Missiles: proximity fuse, obstacle impact, or end of flight -> detonate.
    for (int k = 0; k < VP_MISSILES; k++) {
        vp_missile_t *m = &g->missile[k];
        if (!m->live) continue;
        m->life -= dt;
        m->x += m->dx * dt; m->z += m->dz * dt;
        bool near = false;
        for (int i = 0; i < VP_MAX_ENEMIES; i++)
            if (g->enemy[i].alive &&
                dist2(m->x, m->z, g->enemy[i].x, g->enemy[i].z) < VP_PROX_R * VP_PROX_R) near = true;
        if (near || m->life <= 0 || vp_blocked(g, m->x, m->z, 0.0f)) detonate(g, m);
    }
}

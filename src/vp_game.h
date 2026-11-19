// vp_game — VoidPanzer game rules. Pure C, no Pico SDK, no hardware.
//
// World: metres. x to the right, z forward (north), y up. Headings (yaw) are
// radians, 0 = facing +z, positive turns right (clockwise seen from above):
// forward = (sin yaw, cos yaw).
#ifndef VP_GAME_H
#define VP_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define VP_ARENA         220.0f  // play area is |x|,|z| <= this
#define VP_TANK_R        2.5f
#define VP_FWD_SPEED     9.0f    // m/s
#define VP_REV_SPEED     5.0f
#define VP_TURN_RATE     1.3f    // rad/s at full steering input
#define VP_SHELL_SPEED   70.0f
#define VP_SHELL_LIFE    2.2f
#define VP_RELOAD        0.5f    // s between cannon shots (one shot per press)
#define VP_MISSILE_SPEED 50.0f
#define VP_MISSILE_LIFE  3.0f
#define VP_PROX_R        8.0f    // proximity fuse: detonates this close to an enemy
#define VP_BLAST_R       12.0f   // everything this close to the detonation dies
#define VP_ENEMY_R       3.0f
#define VP_ENEMY_SPEED   5.0f
#define VP_ENEMY_TURN    0.7f
#define VP_LIVES         3
#define VP_MISSILES      4       // per life
#define VP_INVULN        2.0f    // s of grace after being hit
#define VP_SPAWN_DELAY   2.0f
#define VP_RESPAWN_PAUSE 3.0f    // after the player is hit, enemies return after this
#define VP_ENEMY_SPREAD  0.10f   // enemy aim error, +/- rad
#define VP_KILL_POINTS   1000u
#define VP_LOCK_ANGLE    0.05f   // ~3 deg: reticle lights up
#define VP_LOCK_RANGE    160.0f

#define VP_MAX_OBST      16
#define VP_NUM_OBST      7       // obstacles per battlefield
#define VP_MAX_ENEMIES   2
#define VP_MAX_SHELLS    16
#define VP_MAX_SHARDS    48

enum { VP_CUBE = 0, VP_PYRAMID, VP_MONOLITH };
typedef struct { float x, z, r, h; uint8_t kind; } vp_obst_t;
typedef struct { bool alive; float x, z, yaw, fire_cd, avoid_t, avoid_dir; } vp_enemy_t;
typedef struct { bool live, enemy; float x, z, dx, dz, life; } vp_shell_t;
typedef struct { bool live; float x, z, dx, dz, life; } vp_missile_t;
typedef struct { bool live; float x, y, z, vx, vy, vz, a, va, len, life; } vp_shard_t;

typedef enum { VP_PLAYING = 0, VP_GAMEOVER } vp_phase_t;

// One-shot events for sound / LEDs, set during the vp_step that caused them.
enum {
    VP_EV_FIRE = 1, VP_EV_MISSILE = 2, VP_EV_KILL = 4, VP_EV_PLAYER_HIT = 8,
    VP_EV_ENEMY_FIRE = 16, VP_EV_EMPTY = 32, VP_EV_BOOM = 64,
};

typedef struct {
    vp_obst_t obst[VP_MAX_OBST]; int nobst;
    float x, z, yaw;                // player
    int lives, missiles;
    uint32_t score;
    float reload, invuln, hit_flash, spawn_t, time;
    vp_enemy_t enemy[VP_MAX_ENEMIES];
    vp_shell_t shell[VP_MAX_SHELLS];
    vp_missile_t missile[VP_MISSILES];
    vp_shard_t shard[VP_MAX_SHARDS];
    vp_phase_t phase;
    uint32_t events, rng;
    int kills;
} vp_game_t;

typedef struct {
    float turn;     // -1 (left) .. +1 (right)
    int   drive;    // -1 reverse, 0 stop, +1 forward
    bool  fire;     // RED pressed this step (edge, not level)
    bool  missile;  // GRAY pressed this step (edge)
} vp_input_t;

void vp_new_game(vp_game_t *g, uint32_t seed);
void vp_step(vp_game_t *g, float dt, const vp_input_t *in);
bool vp_blocked(const vp_game_t *g, float x, float z, float r);       // obstacle or arena edge
bool vp_los(const vp_game_t *g, float x0, float z0, float x1, float z1); // no obstacle between
int  vp_locked(const vp_game_t *g);                                    // enemy in the crosshairs, or -1
int  vp_enemies_alive(const vp_game_t *g);
float vp_wrap_angle(float a);                                          // to -pi..pi

#endif

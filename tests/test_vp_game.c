// Host tests for src/vp_game.c.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vp_game.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const vp_input_t k_idle = { 0, 0, false, false };

// A game with no obstacles and no enemies, for controlled set-ups.
static void empty_game(vp_game_t *g)
{
    vp_new_game(g, 99);
    g->nobst = 0;
    g->spawn_t = 1e9f;          // no automatic spawns
}

static void place_enemy(vp_game_t *g, int i, float x, float z)
{
    memset(&g->enemy[i], 0, sizeof g->enemy[i]);
    g->enemy[i].alive = true;
    g->enemy[i].x = x; g->enemy[i].z = z;
    g->enemy[i].yaw = 3.14159f;     // facing the player at the origin
    g->enemy[i].fire_cd = 1e9f;     // not shooting unless a test wants it
}

static void run(vp_game_t *g, int steps, vp_input_t in)
{
    for (int i = 0; i < steps; i++) { vp_step(g, 0.01f, &in); in.fire = in.missile = false; }
}

static void test_world(void)
{
    for (uint32_t seed = 1; seed < 200; seed++) {
        vp_game_t g; vp_new_game(&g, seed);
        CHECK(g.nobst >= 8, "seed %u only %d obstacles", seed, g.nobst);
        CHECK(!vp_blocked(&g, 0, 0, VP_TANK_R), "seed %u start blocked", seed);
        CHECK(g.lives == VP_LIVES && g.missiles == VP_MISSILES, "start stock");
    }
}

static void test_driving(void)
{
    vp_game_t g; empty_game(&g);
    vp_input_t fwd = { 0, 1, false, false };
    run(&g, 100, fwd);
    CHECK(fabsf(g.z - VP_FWD_SPEED) < 0.05f && fabsf(g.x) < 1e-3f, "forward 1 s z=%f", (double)g.z);
    vp_input_t rev = { 0, -1, false, false };
    run(&g, 100, rev);
    CHECK(fabsf(g.z - (VP_FWD_SPEED - VP_REV_SPEED)) < 0.05f, "reverse 1 s z=%f", (double)g.z);
    vp_input_t right = { 1, 0, false, false };
    float y0 = g.yaw;
    run(&g, 50, right);
    CHECK(fabsf(g.yaw - y0 - VP_TURN_RATE * 0.5f) < 0.01f, "turn rate");

    // Obstacle dead ahead stops the tank cleanly, short of it.
    empty_game(&g);
    g.obst[0] = (vp_obst_t){ 0, 10, 3, 6, VP_CUBE }; g.nobst = 1;
    run(&g, 300, fwd);
    CHECK(g.z < 10 - 3 - VP_TANK_R + 0.2f && g.z > 4.0f, "stopped at obstacle z=%f", (double)g.z);
}

static void test_cannon(void)
{
    vp_game_t g; empty_game(&g);
    place_enemy(&g, 0, 0, 50);
    vp_input_t fire = { 0, 0, true, false };
    vp_step(&g, 0.01f, &fire);
    CHECK(g.events & VP_EV_FIRE, "fire event");
    run(&g, 100, k_idle);
    CHECK(!g.enemy[0].alive && g.score == VP_KILL_POINTS, "shell kills enemy (score %u)", g.score);

    // One shell per press; reload blocks an immediate second press.
    empty_game(&g);
    vp_step(&g, 0.01f, &fire);
    vp_step(&g, 0.01f, &fire);
    int live = 0;
    for (int i = 0; i < VP_MAX_SHELLS; i++) live += g.shell[i].live;
    CHECK(live == 1, "reload: %d shells after two quick presses", live);
    run(&g, 60, k_idle);
    vp_step(&g, 0.01f, &fire);
    live = 0;
    for (int i = 0; i < VP_MAX_SHELLS; i++) live += g.shell[i].live;
    CHECK(live == 2, "fires again after reload (%d)", live);

    // Holding (level) does nothing extra: only edges fire.
    empty_game(&g);
    run(&g, 200, k_idle);
    CHECK(g.reload <= 0, "idle never fires");

    // Obstacles stop shells.
    empty_game(&g);
    g.obst[0] = (vp_obst_t){ 0, 20, 3, 6, VP_CUBE }; g.nobst = 1;
    place_enemy(&g, 0, 0, 50);
    vp_step(&g, 0.01f, &fire);
    run(&g, 100, k_idle);
    CHECK(g.enemy[0].alive, "shell must not pass through an obstacle");
}

static void test_missiles(void)
{
    vp_game_t g; empty_game(&g);
    vp_input_t ms = { 0, 0, false, true };
    // Near miss: enemy 6 m to the side of the flight path - proximity kills it.
    place_enemy(&g, 0, 6, 60);
    vp_step(&g, 0.01f, &ms);
    CHECK(g.missiles == VP_MISSILES - 1 && (g.events & VP_EV_MISSILE), "missile launched");
    run(&g, 200, k_idle);
    CHECK(!g.enemy[0].alive, "proximity fuse must kill a near miss");

    // A wide miss (15 m) survives.
    empty_game(&g);
    place_enemy(&g, 0, 15, 60);
    vp_step(&g, 0.01f, &ms);
    run(&g, 400, k_idle);
    CHECK(g.enemy[0].alive, "15 m wide miss should survive");

    // One blast takes out two enemies close together.
    empty_game(&g);
    place_enemy(&g, 0, 5, 60);
    place_enemy(&g, 1, -4, 63);     // ~10 m from where the fuse trips (~z 54)
    vp_step(&g, 0.01f, &ms);
    run(&g, 200, k_idle);
    CHECK(!g.enemy[0].alive && !g.enemy[1].alive && g.score == 2 * VP_KILL_POINTS,
          "blast should kill both (score %u)", g.score);

    // Four, then empty.
    empty_game(&g);
    for (int i = 0; i < 4; i++) { vp_step(&g, 0.01f, &ms); run(&g, 400, k_idle); }
    CHECK(g.missiles == 0, "all four used");
    vp_step(&g, 0.01f, &ms);
    CHECK((g.events & VP_EV_EMPTY) && !(g.events & VP_EV_MISSILE), "empty rack");
}

static void test_getting_hit(void)
{
    vp_game_t g; empty_game(&g);
    g.missiles = 1;
    place_enemy(&g, 0, 0, 40);
    g.enemy[0].fire_cd = 0;                  // shoot now (it faces us)
    run(&g, 100, k_idle);
    CHECK(g.lives == VP_LIVES - 1, "hit costs a life (%d)", g.lives);
    CHECK(g.missiles == VP_MISSILES, "new tank gets a full rack");
    CHECK(g.invuln > 0 && g.hit_flash > -1, "grace period");
    CHECK(vp_enemies_alive(&g) == 0 && g.spawn_t > 2.0f, "enemies withdraw after a hit");
    g.spawn_t = 0.5f;
    run(&g, 400, k_idle);
    CHECK(vp_enemies_alive(&g) == 1, "enemies come back after the pause");

    g.lives = 1; g.invuln = 0;
    place_enemy(&g, 0, 0, 40);
    place_enemy(&g, 1, 0, -1000);   // keep the second slot out of the way
    g.enemy[1].alive = false;
    g.spawn_t = 1e9f;
    g.enemy[0].fire_cd = 0;
    run(&g, 200, k_idle);
    CHECK(g.phase == VP_GAMEOVER && g.lives == 0, "last life -> game over");
}

static void test_enemies(void)
{
    vp_game_t g; vp_new_game(&g, 5);
    run(&g, 300, k_idle);
    CHECK(vp_enemies_alive(&g) == 1, "one enemy at the start (%d)", vp_enemies_alive(&g));
    for (int i = 0; i < VP_MAX_ENEMIES; i++)
        if (g.enemy[i].alive)
            CHECK(hypotf(g.enemy[i].x, g.enemy[i].z) > 50, "spawned at a distance");

    // It turns toward the player.
    empty_game(&g);
    place_enemy(&g, 0, 80, 0);
    g.enemy[0].yaw = 0;                      // facing +z, player is to its west
    run(&g, 300, k_idle);
    float want = atan2f(-80, 0);
    CHECK(fabsf(vp_wrap_angle(g.enemy[0].yaw - want)) < 0.2f, "enemy turns to face player");

    // Two at a time after 3000 points.
    vp_new_game(&g, 5);
    g.score = 3000;
    run(&g, 500, k_idle);
    CHECK(vp_enemies_alive(&g) == 2, "two enemies after 3000 (%d)", vp_enemies_alive(&g));
}

static void test_lock(void)
{
    vp_game_t g; empty_game(&g);
    place_enemy(&g, 0, 1, 60);
    CHECK(vp_locked(&g) == 0, "enemy dead ahead is locked");
    g.enemy[0].x = 30;
    CHECK(vp_locked(&g) == -1, "off-axis enemy not locked");
    g.enemy[0].x = 0;
    g.obst[0] = (vp_obst_t){ 0, 30, 3, 6, VP_CUBE }; g.nobst = 1;
    CHECK(vp_locked(&g) == -1, "blocked line of sight not locked");
}

int main(void)
{
    test_world();
    test_driving();
    test_cannon();
    test_missiles();
    test_getting_hit();
    test_enemies();
    test_lock();
    if (g_fail) { printf("%d failure(s)\n", g_fail); return 1; }
    printf("vp_game: all passed\n");
    return 0;
}

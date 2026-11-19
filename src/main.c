// fw2-voidpanzer — first-person wireframe tank game for the FreeWili 2.
//
//   Turn / tilt the board .. turn the tank (BMI323; learned at setup)
//   GREEN or D-pad UP ...... drive forward      YELLOW or D-pad DOWN ... reverse
//   D-pad LEFT / RIGHT ..... turn               D-pad CENTRE ........... re-centre steering
//   RED (press) ............ cannon, one shell per press, unlimited
//                            (red held 6 s powers the board off - tap it)
//   GRAY (press) ........... proximity missile, 4 per tank
//   Hold PAGE 5 s .......... About (game pauses)   Hold HOME 5 s ... exit
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "vp_game.h"

#define BE(c) ((uint16_t)((((c) >> 8) & 0xFF) | (((c) & 0xFF) << 8)))
#define RAD2DEG 57.29578f
#define KEY_FRESH_MS 1200     // > the keyboard's ~500 ms held-button report cadence

#define FB_W 480
#define FB_H 296
#define HZ   150              // horizon row
#define FOCAL 260.0f
#define EYE  2.0f
#define NEAR 0.4f

#define K_BLACK  BE(0x0000)
#define K_GREEN  BE(0x07E8)   // #00ff41
#define K_GLOW   BE(0x0200)
#define K_GRID   BE(0x0320)
#define K_DIM    BE(0x0260)
#define K_WHITE  BE(0xFFFF)
#define K_AMBER  BE(0xFD20)
#define K_RED    BE(0xF8A0)
#define K_HITBG  BE(0x3000)
#define BTN_GRAY_BG   0x9AD6
#define BTN_YELLOW_BG 0x06FF
#define BTN_GREEN_BG  0x0012
#define BTN_RED_BG    0x0780

static uint16_t s_fb[FB_W * FB_H];

// ================================================================ raster
static inline void px(int x, int y, uint16_t c)
{
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) s_fb[y * FB_W + x] = c;
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 1200; guard++) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Cohen-Sutherland clip to the framebuffer so far-off endpoints cost nothing.
static int outcode(float x, float y)
{
    return (x < 0) | ((x > FB_W - 1) << 1) | ((y < 0) << 2) | ((y > FB_H - 1) << 3);
}
static bool clip2d(float *x0, float *y0, float *x1, float *y1)
{
    int c0 = outcode(*x0, *y0), c1 = outcode(*x1, *y1);
    for (int it = 0; it < 8; it++) {
        if (!(c0 | c1)) return true;
        if (c0 & c1) return false;
        int c = c0 ? c0 : c1;
        float x, y;
        if (c & 8)      { x = *x0 + (*x1 - *x0) * (FB_H - 1 - *y0) / (*y1 - *y0); y = FB_H - 1; }
        else if (c & 4) { x = *x0 + (*x1 - *x0) * (0 - *y0) / (*y1 - *y0);        y = 0; }
        else if (c & 2) { y = *y0 + (*y1 - *y0) * (FB_W - 1 - *x0) / (*x1 - *x0); x = FB_W - 1; }
        else            { y = *y0 + (*y1 - *y0) * (0 - *x0) / (*x1 - *x0);        x = 0; }
        if (c == c0) { *x0 = x; *y0 = y; c0 = outcode(x, y); }
        else         { *x1 = x; *y1 = y; c1 = outcode(x, y); }
    }
    return false;
}

static void seg2d(float x0, float y0, float x1, float y1, uint16_t c, bool glow)
{
    if (!clip2d(&x0, &y0, &x1, &y1)) return;
    int a = (int)lroundf(x0), b = (int)lroundf(y0), d = (int)lroundf(x1), e = (int)lroundf(y1);
    if (glow) {
        line(a + 1, b, d + 1, e, K_GLOW); line(a - 1, b, d - 1, e, K_GLOW);
        line(a, b + 1, d, e + 1, K_GLOW); line(a, b - 1, d, e - 1, K_GLOW);
    }
    line(a, b, d, e, c);
}

// ================================================================ stroke font (4x6 grid)
static const char *glyph(char ch)
{
    switch (ch) {
    case '0': return "00404046460606004006";
    case '1': return "202611201636";
    case '2': return "00404043430303060646";
    case '3': return "0040404606461343";
    case '4': return "000303434046";
    case '5': return "40000003034343464606";
    case '6': return "40000006064646434303";
    case '7': return "00404016";
    case '8': return "00404046460606000343";
    case '9': return "03430300004040464606";
    case 'A': return "062020461434";
    case 'B': return "060000303041413203323243434536363606";
    case 'C': return "400000060646";
    case 'D': return "060000303042424444363606";
    case 'E': return "4000000606460333";
    case 'F': return "400000060333";
    case 'G': return "40000006064646434323";
    case 'H': return "000640460343";
    case 'I': return "004020260646";
    case 'K': return "000603400346";
    case 'L': return "00060646";
    case 'M': return "0600002323404046";
    case 'N': return "060000464640";
    case 'O': return "0040404646060600";
    case 'P': return "0600004040434303";
    case 'R': return "06000040404343032346";
    case 'S': return "40000003034343464606";
    case 'T': return "00402026";
    case 'U': return "000606464640";
    case 'V': return "00262640";
    case 'W': return "0006062323464640";
    case 'X': return "00464006";
    case 'Y': return "002323402326";
    case 'Z': return "004040060646";
    case '!': return "20242526";
    case '-': return "1333";
    case ':': return "21222425";
    default:  return "";
    }
}
static void vtext(float x, float y, float u, uint16_t c, const char *s)
{
    for (; *s; s++, x += 6.0f * u)
        for (const char *g = glyph(*s); g[0] && g[1] && g[2] && g[3]; g += 4)
            seg2d(x + (g[0] - '0') * u, y + (g[1] - '0') * u,
                  x + (g[2] - '0') * u, y + (g[3] - '0') * u, c, true);
}
static void vtext_c(float cx, float y, float u, uint16_t c, const char *s)
{
    vtext(cx - ((float)strlen(s) * 6.0f - 2.0f) * u / 2.0f, y, u, c, s);
}

// ================================================================ 3D
static float s_px, s_pz, s_cr, s_sr;   // camera

static void cam_set(const vp_game_t *g)
{
    s_px = g->x; s_pz = g->z; s_cr = cosf(g->yaw); s_sr = sinf(g->yaw);
}
static void to_cam(float wx, float wy, float wz, float *cx, float *cy, float *cz)
{
    float dx = wx - s_px, dz = wz - s_pz;
    *cx = dx * s_cr - dz * s_sr;
    *cz = dx * s_sr + dz * s_cr;
    *cy = wy - EYE;
}
static void seg3d(float ax, float ay, float az, float bx, float by, float bz, uint16_t c, bool glow)
{
    float x0, y0, z0, x1, y1, z1;
    to_cam(ax, ay, az, &x0, &y0, &z0);
    to_cam(bx, by, bz, &x1, &y1, &z1);
    if (z0 < NEAR && z1 < NEAR) return;
    if (z0 < NEAR) { float t = (NEAR - z0) / (z1 - z0); x0 += t * (x1 - x0); y0 += t * (y1 - y0); z0 = NEAR; }
    if (z1 < NEAR) { float t = (NEAR - z1) / (z0 - z1); x1 += t * (x0 - x1); y1 += t * (y0 - y1); z1 = NEAR; }
    seg2d(FB_W / 2 + x0 * FOCAL / z0, HZ - y0 * FOCAL / z0,
          FB_W / 2 + x1 * FOCAL / z1, HZ - y1 * FOCAL / z1, c, glow);
}

// A model: vertex list + edge list, placed at (x, z) with heading yaw.
typedef struct { const float (*v)[3]; const uint8_t (*e)[2]; int ne; } model_t;

static void draw_model(const model_t *m, float x, float z, float yaw, float sx, float sy, float sz, uint16_t c)
{
    float cy = cosf(yaw), sy_ = sinf(yaw);
    for (int i = 0; i < m->ne; i++) {
        const float *a = m->v[m->e[i][0]], *b = m->v[m->e[i][1]];
        float ax = a[0] * sx, az = a[2] * sz, bx = b[0] * sx, bz = b[2] * sz;
        seg3d(x + ax * cy + az * sy_, a[1] * sy, z - ax * sy_ + az * cy,
              x + bx * cy + bz * sy_, b[1] * sy, z - bx * sy_ + bz * cy, c, true);
    }
}

static const float k_box_v[8][3] = {
    {-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1},{-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1} };
static const uint8_t k_box_e[12][2] = {
    {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
static const float k_pyr_v[5][3] = { {-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1},{0,1,0} };
static const uint8_t k_pyr_e[8][2] = { {0,1},{1,2},{2,3},{3,0},{0,4},{1,4},{2,4},{3,4} };
static const model_t k_box = { k_box_v, k_box_e, 12 }, k_pyr = { k_pyr_v, k_pyr_e, 8 };

// Enemy tank (original design): sloped hull, offset turret, long barrel.
static const float k_tank_v[][3] = {
    {-1.6f,0.2f,-2.2f},{1.6f,0.2f,-2.2f},{1.6f,0.2f,2.4f},{-1.6f,0.2f,2.4f},      // 0-3 hull bottom
    {-1.3f,1.1f,-1.8f},{1.3f,1.1f,-1.8f},{1.3f,1.1f,1.6f},{-1.3f,1.1f,1.6f},      // 4-7 hull top
    {-0.8f,1.1f,-0.9f},{0.8f,1.1f,-0.9f},{0.6f,1.1f,0.8f},{-0.6f,1.1f,0.8f},      // 8-11 turret base
    {-0.6f,1.8f,-0.7f},{0.6f,1.8f,-0.7f},{0.4f,1.8f,0.5f},{-0.4f,1.8f,0.5f},      // 12-15 turret top
    {0.0f,1.5f,0.6f},{0.0f,1.5f,4.0f},                                            // 16-17 barrel
    {-1.7f,0.0f,-2.0f},{-1.7f,0.0f,2.2f},{1.7f,0.0f,-2.0f},{1.7f,0.0f,2.2f},      // 18-21 tracks
};
static const uint8_t k_tank_e[][2] = {
    {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7},
    {8,9},{9,10},{10,11},{11,8},{12,13},{13,14},{14,15},{15,12},{8,12},{9,13},{10,14},{11,15},
    {16,17},{18,19},{20,21},{18,0},{19,3},{20,1},{21,2},
};
static const model_t k_tank = { k_tank_v, k_tank_e, (int)(sizeof k_tank_e / sizeof k_tank_e[0]) };

static void draw_world(const vp_game_t *g, uint32_t frame)
{
    cam_set(g);
    // Ground grid around the tank.
    const float G = 12.0f, R = 132.0f;
    float x0 = floorf((g->x - R) / G) * G, z0 = floorf((g->z - R) / G) * G;
    for (float x = x0; x <= g->x + R; x += G) seg3d(x, 0, g->z - R, x, 0, g->z + R, K_GRID, false);
    for (float z = z0; z <= g->z + R; z += G) seg3d(g->x - R, 0, z, g->x + R, 0, z, K_GRID, false);
    // Horizon + mountains (depend only on heading).
    seg2d(0, HZ, FB_W - 1, HZ, K_DIM, false);
    float lx = -1, ly = 0;
    for (int sx = 0; sx <= FB_W; sx += 4) {
        float a = g->yaw + atanf((sx - FB_W / 2) / FOCAL);
        float h = 16 + 9 * sinf(3 * a + 0.5f) + 7 * sinf(7 * a + 1.3f) + 4 * fabsf(sinf(13 * a + 2.1f))
                  + 6 * fmaxf(0.0f, sinf(2 * a - 0.7f));
        float y = HZ - h;
        if (lx >= 0) seg2d(lx, ly, (float)sx, y, K_GREEN, true);
        lx = (float)sx; ly = y;
    }
    // Obstacles.
    for (int i = 0; i < g->nobst; i++) {
        const vp_obst_t *o = &g->obst[i];
        float dx = o->x - g->x, dz = o->z - g->z;
        if (dx * dx + dz * dz > 260.0f * 260.0f) continue;
        draw_model(o->kind == VP_PYRAMID ? &k_pyr : &k_box, o->x, o->z, 0.4f * (float)i,
                   o->r * 0.75f, o->h, o->r * 0.75f, K_GREEN);
    }
    // Enemies.
    for (int i = 0; i < VP_MAX_ENEMIES; i++)
        if (g->enemy[i].alive) draw_model(&k_tank, g->enemy[i].x, g->enemy[i].z, g->enemy[i].yaw, 1, 1, 1, K_GREEN);
    // Shells with trails, missiles.
    for (int i = 0; i < VP_MAX_SHELLS; i++) {
        const vp_shell_t *s = &g->shell[i];
        if (!s->live) continue;
        uint16_t c = s->enemy ? K_RED : K_WHITE;
        seg3d(s->x, 1.5f, s->z, s->x - s->dx * 0.03f, 1.5f, s->z - s->dz * 0.03f, c, true);
        seg3d(s->x - s->dx * 0.03f, 1.5f, s->z - s->dz * 0.03f, s->x - s->dx * 0.09f, 1.5f,
              s->z - s->dz * 0.09f, K_DIM, false);
    }
    for (int i = 0; i < VP_MISSILES; i++) {
        const vp_missile_t *m = &g->missile[i];
        if (!m->live) continue;
        seg3d(m->x, 1.2f, m->z, m->x - m->dx * 0.04f, 1.2f, m->z - m->dz * 0.04f, K_AMBER, true);
        seg3d(m->x, 0.8f, m->z, m->x, 1.6f, m->z, K_AMBER, true);
        float fl = (frame & 1) ? 0.12f : 0.18f;
        seg3d(m->x - m->dx * 0.04f, 1.2f, m->z - m->dz * 0.04f, m->x - m->dx * fl, 1.2f,
              m->z - m->dz * fl, K_WHITE, false);
    }
    // Explosion shards.
    for (int i = 0; i < VP_MAX_SHARDS; i++) {
        const vp_shard_t *s = &g->shard[i];
        if (!s->live) continue;
        float c = cosf(s->a) * s->len / 2, sn = sinf(s->a) * s->len / 2;
        seg3d(s->x - c, s->y - sn, s->z, s->x + c, s->y + sn, s->z + c * 0.5f, i % 3 ? K_GREEN : K_AMBER, true);
    }
}

// ================================================================ HUD
#define RAD_X 240
#define RAD_Y 38
#define RAD_R 30
#define RAD_RANGE 200.0f

static void draw_hud(const vp_game_t *g, bool locked, float sweep, bool red_warn, float empty_t)
{
    char b[32];
    snprintf(b, sizeof b, "SCORE %06u", (unsigned)g->score);
    vtext(6, 6, 1.6f, K_GREEN, b);
    vtext(6, 22, 1.6f, K_GREEN, "TANKS");
    for (int i = 0; i < g->lives; i++) {                      // little tank icons
        float x = 64 + i * 18.0f;
        seg2d(x, 32, x + 12, 32, K_GREEN, true); seg2d(x + 2, 27, x + 10, 27, K_GREEN, true);
        seg2d(x, 32, x + 2, 27, K_GREEN, true); seg2d(x + 12, 32, x + 10, 27, K_GREEN, true);
        seg2d(x + 6, 27, x + 13, 25, K_GREEN, true);
    }
    vtext(FB_W - 112, 6, 1.6f, K_GREEN, "MISSILES");
    for (int i = 0; i < VP_MISSILES; i++) {
        float x = FB_W - 104 + i * 22.0f;
        uint16_t c = i < g->missiles ? K_AMBER : K_DIM;
        seg2d(x, 32, x + 14, 22, c, i < g->missiles);
        seg2d(x + 14, 22, x + 10, 22, c, false); seg2d(x + 14, 22, x + 13, 26, c, false);
    }
    // Radar.
    for (int k = 0; k < 24; k++) {
        float a0 = k * 6.2832f / 24, a1 = (k + 1) * 6.2832f / 24;
        seg2d(RAD_X + RAD_R * sinf(a0), RAD_Y - RAD_R * cosf(a0),
              RAD_X + RAD_R * sinf(a1), RAD_Y - RAD_R * cosf(a1), K_GREEN, true);
    }
    seg2d(RAD_X, RAD_Y, RAD_X + RAD_R * sinf(sweep), RAD_Y - RAD_R * cosf(sweep), K_DIM, false);
    seg2d(RAD_X - 3, RAD_Y + 3, RAD_X, RAD_Y - 3, K_WHITE, false);
    seg2d(RAD_X + 3, RAD_Y + 3, RAD_X, RAD_Y - 3, K_WHITE, false);
    cam_set(g);
    for (int i = 0; i < VP_MAX_ENEMIES; i++) {
        if (!g->enemy[i].alive) continue;
        float cx, cy, cz;
        to_cam(g->enemy[i].x, 0, g->enemy[i].z, &cx, &cy, &cz);
        float s = RAD_R / RAD_RANGE, rx = cx * s, ry = cz * s, d = sqrtf(rx * rx + ry * ry);
        if (d > RAD_R - 2) { rx *= (RAD_R - 2) / d; ry *= (RAD_R - 2) / d; }
        int x = RAD_X + (int)rx, y = RAD_Y - (int)ry;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) px(x + dx, y + dy, K_RED);
    }
    // Reticle.
    uint16_t rc = locked ? K_WHITE : K_DIM;
    const int cx = FB_W / 2, cy = HZ;
    seg2d(cx - 22, cy - 12, cx - 22, cy + 12, rc, locked); seg2d(cx - 22, cy - 12, cx - 15, cy - 12, rc, locked);
    seg2d(cx - 22, cy + 12, cx - 15, cy + 12, rc, locked); seg2d(cx + 22, cy - 12, cx + 22, cy + 12, rc, locked);
    seg2d(cx + 22, cy - 12, cx + 15, cy - 12, rc, locked); seg2d(cx + 22, cy + 12, cx + 15, cy + 12, rc, locked);
    seg2d(cx - 4, cy, cx + 4, cy, rc, false); seg2d(cx, cy - 4, cx, cy + 4, rc, false);
    if (locked) vtext_c(cx, cy + 20, 1.4f, K_WHITE, "LOCK");
    if (g->invuln > 0 && g->phase == VP_PLAYING) vtext_c(cx, 200, 2.0f, K_GREEN, "NEW TANK");
    if (empty_t > 0) vtext_c(cx, 230, 2.0f, K_AMBER, "EMPTY");
    if (red_warn) vtext_c(cx, 255, 2.4f, K_RED, "RELEASE RED");
}

static void blit(void) { st7796_blit_rect(0, 0, FB_W - 1, FB_H - 1, s_fb); }

// ================================================================ button bar
static void btn(int slot, uint16_t bg, uint16_t fg, const char *l)
{
    const int w = (ST7796_W - 12) / 5, x = slot * 96;
    st7796_fill_rect(x, 296, w, 24, bg);
    st7796_draw_text(x + (w - (int)strlen(l) * 12) / 2, 300, 2, fg, bg, l);
}
static int s_bar = -1;
static void button_bar(int mode)   // 0 = start/confirm, 1 = playing
{
    if (s_bar == mode) return;
    s_bar = mode;
    st7796_fill_rect(0, 296, ST7796_W, 24, K_BLACK);
    if (mode == 0) { btn(2, BTN_GREEN_BG, K_WHITE, "START"); return; }
    btn(0, BTN_GRAY_BG, K_BLACK, "MISSILE");
    btn(1, BTN_YELLOW_BG, K_BLACK, "REV");
    btn(2, BTN_GREEN_BG, K_WHITE, "DRIVE");
    btn(4, BTN_RED_BG, K_WHITE, "FIRE");
}

// ================================================================ IMU steering (as moonhop)
static bool  s_have_imu;
static float s_g[3], s_n[3] = { 0, 0, 1 }, s_u[3] = { 1, 0, 0 };
static float s_bias[3], s_rot[3], s_k[3] = { 0, 0, 1 }, s_turn;
static bool  s_turn_mode;
static const char *s_mode_name = "TILT";

static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static float len3(const float *a) { return sqrtf(dot3(a, a)); }
static void norm3(float *a) { float m = len3(a); if (m > 1e-6f) { a[0] /= m; a[1] /= m; a[2] /= m; } }

static void imu_poll(float dt, bool learn_bias, bool learn_rot)
{
    bmi323_reading_t r;
    if (!s_have_imu || !bmi323_read(&r) || !r.valid) return;
    float a[3] = { r.ax, r.ay, r.az }, w[3] = { r.gx, r.gy, r.gz };
    for (int i = 0; i < 3; i++) {
        s_g[i] += 0.3f * (a[i] - s_g[i]);
        if (learn_bias) s_bias[i] += 0.02f * (w[i] - s_bias[i]);
        if (learn_rot)  s_rot[i] += (w[i] - s_bias[i]) * dt;
    }
    if (s_turn_mode) {
        float rate = 0;
        for (int i = 0; i < 3; i++) rate += (w[i] - s_bias[i]) * s_k[i];
        if (fabsf(rate) > 0.8f) s_turn += rate * dt / RAD2DEG;
    }
}
static void cal_level(void) { memcpy(s_n, s_g, sizeof s_n); norm3(s_n); memset(s_rot, 0, sizeof s_rot); }
static void cal_right(void)
{
    float gn[3]; memcpy(gn, s_g, sizeof gn); norm3(gn);
    float tilt_deg = acosf(fmaxf(-1.0f, fminf(1.0f, dot3(gn, s_n)))) * RAD2DEG;
    float rot_deg = len3(s_rot), k[3]; memcpy(k, s_rot, sizeof k); norm3(k);
    DIAG("voidpanzer: cal tilt=%d rot=%d axis.n=%d/1000\n", (int)tilt_deg, (int)rot_deg, (int)(dot3(k, s_n) * 1000));
    if (rot_deg > 10.0f && fabsf(dot3(k, s_n)) > 0.7f && tilt_deg < 0.5f * rot_deg) {
        s_turn_mode = true; s_mode_name = "TURN"; memcpy(s_k, k, sizeof s_k); s_turn = 0; return;
    }
    s_turn_mode = false; s_mode_name = "TILT";
    float u[3]; memcpy(u, s_g, sizeof u);
    float d = dot3(u, s_n);
    for (int i = 0; i < 3; i++) u[i] -= d * s_n[i];
    if (len3(u) < 0.15f) {
        int best = 0;
        for (int i = 1; i < 3; i++) if (fabsf(s_n[i]) < fabsf(s_n[best])) best = i;
        float e[3] = { 0, 0, 0 }; e[best] = 1.0f; d = dot3(e, s_n);
        for (int i = 0; i < 3; i++) u[i] = e[i] - d * s_n[i];
        s_mode_name = "TILT?";
    }
    norm3(u); memcpy(s_u, u, sizeof s_u);
}
// Steering angle from the board, rad (signed, right positive).
static float steer_rad(void)
{
    if (!s_have_imu) return 0;
    if (s_turn_mode) return s_turn;
    return atan2f(dot3(s_g, s_u), dot3(s_g, s_n));
}
// Turn input -1..1: centre deadzone, then proportional to the angle.
static float steer_input(void)
{
    const float dead = 4.0f / RAD2DEG, full = 30.0f / RAD2DEG;
    float a = steer_rad(), m = fabsf(a) - dead;
    if (m <= 0) return 0;
    float t = m / (full - dead);
    if (t > 1) t = 1;
    return a > 0 ? t : -t;
}

// ================================================================ sound mixer
// One 4096-frame ring (256 ms at 16 kHz) is rewritten each frame as a mix of
// low-passed noise (rumble / whoosh / boom), bright noise (cannon) and a
// 1 kHz sine (radar ping; 4096 frames hold exactly 256 periods).
#define SND_N 4096
static uint32_t s_snd[SND_N] __attribute__((aligned(SND_N * 4)));
static int16_t  s_lo[SND_N], s_hi[SND_N];
static bool  s_audio_ok, s_snd_on;
static float s_e_fire, s_e_efire, s_e_msl, s_e_boom, s_e_ping, s_mix_lo, s_mix_hi, s_mix_sin;
static uint32_t s_quiet_ms;
#define SND_MAX 0.24f                          // keep well inside the 0.5 W speaker

static void audio_setup(void)
{
    picpwr_keep_awake(picpwr_zone_bit(PICPWR_ZONE_AUDIO));
    absolute_time_t give_up = make_timeout_time_ms(6000), no_frames = make_timeout_time_ms(3000);
    uint32_t rails;
    while (!time_reached(give_up)) {
        fw2_app_recovery_task(); picpwr_task();
        if (picpwr_rails(&rails)) { if (rails & picpwr_zone_bit(PICPWR_ZONE_AUDIO)) break; }
        else if (time_reached(no_frames)) break;
        fw2_app_recovery_sleep_ms(25);
    }
    codec_nau88c10_init();
    codec_nau88c10_dac_mute(false);
    audio_i2s_duplex_init(16000);
    codec_nau88c10_speaker_low_power();
    // Two noise colours, each normalised (three passes over one RNG sequence).
    for (int which = 0; which < 2; which++) {
        float k = which ? 0.85f : 0.12f, peak = 1e-3f;
        for (int pass = 0; pass < 3; pass++) {
            uint32_t r = which ? 0xBEEFu : 0x1202u;
            float lp = 0;
            for (int rep = 0; rep < 2; rep++)
                for (int i = 0; i < SND_N; i++) {
                    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                    lp += k * ((float)(int32_t)r / 2147483648.0f - lp);
                    if (!rep) continue;
                    if (pass == 1 && fabsf(lp) > peak) peak = fabsf(lp);
                    if (pass == 2) (which ? s_hi : s_lo)[i] = (int16_t)(lp / peak * 32000.0f);
                }
        }
    }
    s_audio_ok = true;
}

static float decay(float e, float dt, float tau) { e -= dt / tau; return e < 0 ? 0 : e; }

static void audio_update(float dt, uint32_t ev, bool driving, bool ping)
{
    if (!s_audio_ok) return;
    if (ev & VP_EV_FIRE) s_e_fire = 1;
    if (ev & VP_EV_ENEMY_FIRE) s_e_efire = 1;
    if (ev & VP_EV_MISSILE) s_e_msl = 1;
    if (ev & (VP_EV_KILL | VP_EV_BOOM | VP_EV_PLAYER_HIT)) s_e_boom = 1;
    if (ping) s_e_ping = 1;
    s_e_fire = decay(s_e_fire, dt, 0.22f);  s_e_efire = decay(s_e_efire, dt, 0.3f);
    s_e_msl = decay(s_e_msl, dt, 0.7f);     s_e_boom = decay(s_e_boom, dt, 1.2f);
    s_e_ping = decay(s_e_ping, dt, 0.09f);

    float lo = (driving ? 0.05f : 0) + 0.12f * s_e_msl + 0.20f * s_e_boom * s_e_boom;
    float hi = 0.20f * s_e_fire * s_e_fire + 0.08f * s_e_efire + 0.05f * s_e_msl + 0.06f * s_e_boom;
    float sn = 0.05f * s_e_ping;
    float tot = lo + hi + sn;
    if (tot > SND_MAX) { float k = SND_MAX / tot; lo *= k; hi *= k; sn *= k; tot = SND_MAX; }

    if (tot < 0.002f) {
        s_quiet_ms += (uint32_t)(dt * 1000);
        if (s_snd_on && s_quiet_ms > 400) {    // power the speaker path down when silent
            audio_i2s_duplex_play_stop();
            codec_nau88c10_speaker_low_power();
            s_snd_on = false;
            s_mix_lo = s_mix_hi = s_mix_sin = 0;
        }
        return;
    }
    s_quiet_ms = 0;
    if (fabsf(lo - s_mix_lo) + fabsf(hi - s_mix_hi) + fabsf(sn - s_mix_sin) > 0.003f || !s_snd_on) {
        s_mix_lo = lo; s_mix_hi = hi; s_mix_sin = sn;
        static const int16_t k_sin16[16] = { 0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
                                             0, -12539, -23170, -30273, -32767, -30273, -23170, -12539 };
        for (int i = 0; i < SND_N; i++) {
            float v = s_lo[i] * lo + s_hi[i] * hi + k_sin16[i & 15] * sn;
            uint16_t u = (uint16_t)(int16_t)v;
            s_snd[i] = ((uint32_t)u << 16) | u;
        }
    }
    if (!s_snd_on) {
        codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
        audio_i2s_duplex_play_loop(s_snd, SND_N);
        s_snd_on = true;
    }
}

// ================================================================ main
typedef enum { ST_CAL_LEVEL, ST_CAL_RIGHT, ST_PLAY } state_t;

int main(void)
{
    board_init();
    fw2_app_recovery_init();
    size_t psram_bytes = psram_init();
    st7796_init();
    fw2_app_about_use_lcd();
    fw2kb_t kb;
    fw2kb_init(&kb);
    bool agentio = psram_bytes >= (size_t)ST7796_W * ST7796_H * 2;
    if (agentio) { agentio_init(); agentio_bind_keyboard(&kb); }
    st7796_fill_screen(K_BLACK);              // whole surface before the backlight
    board_backlight_set(1);
    picpwr_release_unused();

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear(); ws2812_show();

    memset(s_fb, 0, sizeof s_fb);
    vtext_c(240, 120, 4.0f, K_GREEN, "VOIDPANZER");
    blit();

    uint64_t t0 = time_us_64();
    while (!(s_have_imu = bmi323_init()) && time_us_64() - t0 < 2000000) {
        fw2_app_recovery_task(); picpwr_task();
        if (agentio) agentio_task();
        fw2_app_recovery_sleep_ms(50);
    }
    audio_setup();
    DIAG("voidpanzer: v001 up, imu=%d agentio=%d\n", s_have_imu, agentio);

    static vp_game_t g;
    uint32_t seed = 0x7A4Bu;
    vp_new_game(&g, seed);
    state_t st = s_have_imu ? ST_CAL_LEVEL : ST_PLAY;
    float acc = 0, sweep = 0, empty_t = 0, red_held_s = 0, led_t = 0;
    uint32_t frame = 0, ev_accum = 0;
    rgb_t led_c = { 0 };
    uint64_t last = time_us_64(), t_draw = 0, t_diag = 0;

    for (;;) {
        fw2_app_recovery_task();
        picpwr_task();
        uint64_t now = time_us_64();
        float dt = (float)(now - last) * 1e-6f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        imu_poll(dt, st == ST_CAL_LEVEL, st == ST_CAL_RIGHT);

        bool green = false, fire = false, missile = false;
        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev)) {
            if (!ev.pressed) continue;
            if (ev.btn == UARTKBD_BTN_GREEN) green = true;
            if (ev.btn == UARTKBD_BTN_RED) fire = true;          // one shell per press
            if (ev.btn == UARTKBD_BTN_GREY) missile = true;
            if (ev.btn == UARTKBD_BTN_NAV_CENTER) s_turn = 0;
        }
        bool fwd = uartkbd_button_down_fresh(UARTKBD_BTN_GREEN, KEY_FRESH_MS) ||
                   uartkbd_button_down_fresh(UARTKBD_BTN_NAV_UP, KEY_FRESH_MS);
        bool rev = uartkbd_button_down_fresh(UARTKBD_BTN_YELLOW, KEY_FRESH_MS) ||
                   uartkbd_button_down_fresh(UARTKBD_BTN_NAV_DOWN, KEY_FRESH_MS);
        float pad = (uartkbd_button_down_fresh(UARTKBD_BTN_NAV_RIGHT, KEY_FRESH_MS) ? 1.0f : 0.0f) -
                    (uartkbd_button_down_fresh(UARTKBD_BTN_NAV_LEFT, KEY_FRESH_MS) ? 1.0f : 0.0f);
        bool red_down = uartkbd_button_down_fresh(UARTKBD_BTN_RED, KEY_FRESH_MS);
        red_held_s = red_down ? red_held_s + dt : 0;

        if (st != ST_PLAY) {
            if (green) {
                if (st == ST_CAL_LEVEL) { cal_level(); st = ST_CAL_RIGHT; }
                else { cal_right(); st = ST_PLAY; vp_new_game(&g, seed); }
            }
        } else if (g.phase == VP_GAMEOVER) {
            if (green) { seed = seed * 1664525u + 1013904223u; vp_new_game(&g, seed); s_turn = 0; }
            vp_input_t idle = { 0, 0, false, false };
            vp_step(&g, dt, &idle);                              // let the debris settle
        } else if (uartkbd_button_down_fresh(UARTKBD_BTN_PAGE, KEY_FRESH_MS)) {
            acc = 0;                                             // paused while About is up
        } else {
            float turn = steer_input() + pad;
            vp_input_t in = { turn > 1 ? 1 : (turn < -1 ? -1 : turn), fwd ? 1 : (rev ? -1 : 0), fire, missile };
            acc += dt;
            while (acc >= 0.01f) {
                acc -= 0.01f;
                vp_step(&g, 0.01f, &in);
                in.fire = in.missile = false;                    // edges count once
                ev_accum |= g.events;
                if (g.events & VP_EV_KILL) DIAG("voidpanzer: KILL score=%u\n", (unsigned)g.score);
                if (g.events & VP_EV_PLAYER_HIT) DIAG("voidpanzer: HIT lives=%d\n", g.lives);
                if (g.events & VP_EV_MISSILE) DIAG("voidpanzer: MISSILE left=%d\n", g.missiles);
                if (g.events & VP_EV_EMPTY) empty_t = 1.0f;
            }
        }
        if (empty_t > 0) empty_t -= dt;

        // Radar sweep and ping when it passes an enemy.
        bool ping = false;
        float sweep_prev = sweep;
        sweep += 2.5f * dt;
        if (sweep > 6.2832f) sweep -= 6.2832f;
        if (st == ST_PLAY && g.phase == VP_PLAYING)
            for (int i = 0; i < VP_MAX_ENEMIES; i++) {
                if (!g.enemy[i].alive) continue;
                float b = vp_wrap_angle(atan2f(g.enemy[i].x - g.x, g.enemy[i].z - g.z) - g.yaw);
                if (b < 0) b += 6.2832f;
                if ((sweep_prev <= b && b < sweep) || (sweep < sweep_prev && (b >= sweep_prev || b < sweep)))
                    ping = true;
            }
        bool driving = st == ST_PLAY && g.phase == VP_PLAYING && (fwd || rev);
        audio_update(dt, ev_accum, driving, ping);

        // LEDs.
        if (ev_accum & VP_EV_PLAYER_HIT)      { led_c = (rgb_t){ .r = 255 };           led_t = 0.5f; }
        else if (ev_accum & VP_EV_KILL)       { led_c = (rgb_t){ .g = 255 };           led_t = 0.4f; }
        else if (ev_accum & VP_EV_MISSILE)    { led_c = (rgb_t){ .r = 255, .g = 110 }; led_t = 0.25f; }
        else if (ev_accum & VP_EV_FIRE)       { led_c = (rgb_t){ .r = 180, .g = 255, .b = 180 }; led_t = 0.08f; }
        if (ev_accum) { ws2812_fill(led_c); ws2812_show(); }
        if (led_t > 0) { led_t -= dt; if (led_t <= 0) { ws2812_clear(); ws2812_show(); } }
        ev_accum = 0;

        if (now - t_draw > 33000) {
            t_draw = now; frame++;
            bool flash = g.hit_flash > 0 && st == ST_PLAY;
            uint16_t bg = flash ? K_HITBG : K_BLACK;
            for (int i = 0; i < FB_W * FB_H; i++) s_fb[i] = bg;
            if (st != ST_PLAY) {
                button_bar(0);
                vtext_c(240, 50, 4.0f, K_GREEN, "VOIDPANZER");
                char b[40];
                if (st == ST_CAL_LEVEL) {
                    vtext_c(240, 125, 2.0f, K_WHITE, "HOLD IT HOW YOU WILL PLAY");
                    vtext_c(240, 155, 2.0f, K_WHITE, "KEEP STILL - PRESS GREEN");
                    snprintf(b, sizeof b, "X %05d Y %05d Z %05d",
                             (int)(s_g[0] * 1000), (int)(s_g[1] * 1000), (int)(s_g[2] * 1000));
                } else {
                    vtext_c(240, 125, 2.0f, K_WHITE, "NOW TURN OR TILT IT RIGHT");
                    vtext_c(240, 155, 2.0f, K_WHITE, "HOLD THERE - PRESS GREEN");
                    snprintf(b, sizeof b, "TURNED %d DEG", (int)len3(s_rot));
                }
                vtext_c(240, 200, 1.5f, K_GREEN, b);
                vtext_c(240, 240, 1.3f, K_DIM, "D-PAD UP DOWN DRIVES  LEFT RIGHT TURNS");
            } else {
                draw_world(&g, frame);
                if (g.phase == VP_GAMEOVER) {
                    button_bar(0);
                    char b[32];
                    vtext_c(240, 175, 4.0f, K_RED, "GAME OVER");
                    snprintf(b, sizeof b, "SCORE %u", (unsigned)g.score);
                    vtext_c(240, 220, 2.0f, K_GREEN, b);
                    vtext_c(240, 250, 2.0f, K_WHITE, "PRESS GREEN");
                    draw_hud(&g, false, sweep, red_held_s > 3.0f, 0);
                } else {
                    button_bar(1);
                    draw_hud(&g, vp_locked(&g) >= 0, sweep, red_held_s > 3.0f, empty_t);
                }
            }
            blit();
        }

        if (now - t_diag > 500000) {
            t_diag = now;
            int brg = 999, rng = 0;                  // nearest enemy: bearing off the nose, range
            float best = 1e9f;
            for (int i = 0; i < VP_MAX_ENEMIES; i++) {
                if (!g.enemy[i].alive) continue;
                float dx = g.enemy[i].x - g.x, dz = g.enemy[i].z - g.z, d = sqrtf(dx * dx + dz * dz);
                if (d < best) {
                    best = d; rng = (int)d;
                    brg = (int)(vp_wrap_angle(atan2f(dx, dz) - g.yaw) * RAD2DEG);
                }
            }
            DIAG("st=%d ph=%d pos=%d,%d yaw=%d steer=%d(%s) lives=%d msl=%d score=%u enemies=%d brg=%d rng=%d lock=%d\n",
                 st, g.phase, (int)g.x, (int)g.z, (int)(g.yaw * RAD2DEG), (int)(steer_rad() * RAD2DEG),
                 s_mode_name, g.lives, g.missiles, (unsigned)g.score, vp_enemies_alive(&g), brg, rng,
                 vp_locked(&g));
        }
        if (agentio) agentio_task();
    }
}

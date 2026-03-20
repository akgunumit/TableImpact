// space_impact.c — Space Impact: Nokia 3310 style side-scrolling shooter
// Tilt screen to move ship, tap desk to shoot.
//
// Build: make space_impact
// Run:   sudo ./space_impact

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_BULLETS     32
#define MAX_ENEMIES     16
#define MAX_PERKS       4

#define SHIP_COL        4
#define SHIP_W          7
#define INITIAL_LIVES   3
#define INVULN_FRAMES   60      // 2 sec at 30fps

#define LERP_FACTOR     0.3f

#define SCORE_PER_LEVEL 500
#define BASE_SPAWN_CD   25      // frames between enemy spawns
#define MIN_SPAWN_CD    8

#define ET_BASIC  0
#define ET_FAST   1
#define ET_TANK   2

#define BT_NORMAL 0
#define BT_HEAVY  1
#define BT_BOMB   2     // small bomb 3x3
#define BT_BOMB_M 3     // medium bomb 5x5
#define BT_BOMB_L 4     // large bomb 7x5

#define TAP_BUF_SIZE  8
#define LASER_FRAMES  12      // ~0.4s at 30fps
#define LASER_HALF_H  7       // 15 rows total (±7 from center)

// ── types ─────────────────────────────────────────────────────────

typedef struct { int x, y, type, damage, w, h, speed, active; } Bullet;
typedef struct { int x, y, type, hp, w, h, speed, active; } Enemy;
typedef struct { int x, y, active; } Perk;
typedef struct { int active, row, frames_left; } Laser;

// ── globals ───────────────────────────────────────────────────────

static int score, lives, invuln_timer, spawn_timer;
static int ship_row;
static float ship_row_f;

static Bullet   bullets[MAX_BULLETS];
static Enemy    enemies[MAX_ENEMIES];
static Perk     perks[MAX_PERKS];
static Laser    laser;

static int laser_ammo = 3;

// tap combo ring buffer
static double tap_times[TAP_BUF_SIZE];
static int    tap_idx = 0;

// ── ship ──────────────────────────────────────────────────────────

static void draw_ship(void) {
    if (invuln_timer > 0 && (frame_num % 4 < 2)) return;   // blink
    grid_str(SHIP_COL + 3, ship_row - 2, "/\\",     C_CYAN);
    grid_str(SHIP_COL + 1, ship_row - 1, "/===>",   C_CYAN);
    grid_str(SHIP_COL,     ship_row,     "|/===>>",  C_HICYAN);
    grid_str(SHIP_COL + 1, ship_row + 1, "\\===>",  C_CYAN);
    grid_str(SHIP_COL + 3, ship_row + 2, "\\/",     C_CYAN);
}

// ── bullets ───────────────────────────────────────────────────────

static void spawn_weapon(double force) {
    int type, damage, h, speed, w;
    if (force > 0.075) {
        type = BT_BOMB_L; damage = 99; h = 5; speed = 1; w = 7;
    } else if (force > 0.070) {
        type = BT_BOMB_M; damage = 50; h = 5; speed = 1; w = 5;
    } else if (force > 0.066) {
        type = BT_BOMB; damage = 5; h = 3; speed = 1; w = 3;
    } else if (force > 0.060) {
        type = BT_HEAVY; damage = 2; h = 1; speed = 2; w = 4;
    } else {
        type = BT_NORMAL; damage = 1; h = 1; speed = 2; w = 2;
    }

    int spawn_y = (h > 1) ? ship_row - h / 2 : ship_row;
    if (spawn_y < 0) spawn_y = 0;

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i] = (Bullet){
                .x = SHIP_COL + SHIP_W,
                .y = spawn_y,
                .type = type,
                .damage = damage,
                .w = w,
                .h = h,
                .speed = speed,
                .active = 1
            };
            return;
        }
    }
}

static void update_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        bullets[i].x += bullets[i].speed;
        if (bullets[i].x >= term_w) bullets[i].active = 0;
    }
}

static void draw_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        int x = bullets[i].x, y = bullets[i].y;
        switch (bullets[i].type) {
            case BT_HEAVY:
                grid_str(x, y, "===>", C_YELLOW);
                break;
            case BT_BOMB:
                grid_str(x, y,     "\\*\\", C_HIRED);
                grid_str(x, y + 1, "(*)",   C_HIRED);
                grid_str(x, y + 2, "/*/",   C_HIRED);
                break;
            case BT_BOMB_M:
                grid_str(x + 1, y,     "\\@/",   C_HIRED);
                grid_str(x,     y + 1, "\\@*@/", C_HIRED);
                grid_str(x,     y + 2, "(@*@)",  C_HIRED);
                grid_str(x,     y + 3, "/@*@\\", C_HIRED);
                grid_str(x + 1, y + 4, "/@\\",   C_HIRED);
                break;
            case BT_BOMB_L:
                grid_str(x + 1, y,     "\\@@@/",   C_HIRED);
                grid_str(x,     y + 1, "\\@@@@@/", C_HIRED);
                grid_str(x,     y + 2, "(@@*@@)",  C_HIRED);
                grid_str(x,     y + 3, "/@@@@@\\", C_HIRED);
                grid_str(x + 1, y + 4, "/@@@\\",   C_HIRED);
                break;
            default:
                grid_str(x, y, "--", C_YELLOW);
                break;
        }
    }
}

// ── enemies ───────────────────────────────────────────────────────

static void spawn_enemy(void) {
    int diff = score / SCORE_PER_LEVEL;
    int type = ET_BASIC;
    int r = rand() % 100;
    if (diff >= 2 && r < 20)       type = ET_TANK;
    else if (diff >= 1 && r < 40)  type = ET_FAST;

    int w, h, hp, speed;
    switch (type) {
        case ET_FAST:  w = 2; h = 2; hp = 1; speed = 2; break;
        case ET_TANK:  w = 5; h = 3; hp = 3; speed = 1; break;
        default:       w = 3; h = 3; hp = 1; speed = 1; break;
    }

    int max_y = play_h - h;
    if (max_y < 0) max_y = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        enemies[i] = (Enemy){
            .x = term_w,
            .y = rand() % (max_y + 1),
            .type = type,
            .hp = hp,
            .w = w,
            .h = h,
            .speed = speed,
            .active = 1
        };
        return;
    }
}

static void update_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        enemies[i].x -= enemies[i].speed;
        if (enemies[i].x + enemies[i].w < 0) { enemies[i].active = 0; continue; }

        // vertical bob: ±1 every ~10 frames, staggered per enemy
        if (frame_num % 10 == (i * 3) % 10) {
            int dir = (sinf((float)frame_num * 0.12f + i * 2.5f) > 0) ? 1 : -1;
            int ny = enemies[i].y + dir;
            if (ny >= 0 && ny + enemies[i].h <= play_h)
                enemies[i].y = ny;
        }
    }
}

static void draw_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        int x = enemies[i].x, y = enemies[i].y;
        switch (enemies[i].type) {
            case ET_FAST:
                grid_str(x, y,     "=>", C_HIRED);
                grid_str(x, y + 1, "=>", C_HIRED);
                break;
            case ET_TANK:
                grid_str(x, y,     "[===]", C_MAGENTA);
                grid_str(x, y + 1, "[###]", C_MAGENTA);
                grid_str(x, y + 2, "[===]", C_MAGENTA);
                break;
            default:
                grid_str(x, y,     "/~\\", C_RED);
                grid_str(x, y + 1, "<*>",  C_RED);
                grid_str(x, y + 2, "\\~/", C_RED);
                break;
        }
    }
}

// ── perks ─────────────────────────────────────────────────────────

static void spawn_perk(void) {
    int max_y = play_h - 1;
    if (max_y < 0) max_y = 0;
    for (int i = 0; i < MAX_PERKS; i++) {
        if (perks[i].active) continue;
        perks[i] = (Perk){
            .x = term_w,
            .y = rand() % (max_y + 1),
            .active = 1
        };
        return;
    }
}

static void update_perks(void) {
    for (int i = 0; i < MAX_PERKS; i++) {
        if (!perks[i].active) continue;
        perks[i].x--;
        if (perks[i].x + 3 < 0) perks[i].active = 0;
    }
}

static void draw_perks(void) {
    for (int i = 0; i < MAX_PERKS; i++) {
        if (!perks[i].active) continue;
        grid_str(perks[i].x, perks[i].y, "[L]", C_HIGREEN);
    }
}

// ── laser ─────────────────────────────────────────────────────────

static void fire_laser(void) {
    laser_ammo--;
    laser.active = 1;
    laser.row = ship_row;
    laser.frames_left = LASER_FRAMES;
}

static void update_laser(void) {
    if (!laser.active) return;

    // damage on first frame only — 15-row beam
    if (laser.frames_left == LASER_FRAMES) {
        int beam_top = laser.row - LASER_HALF_H;
        int beam_bot = laser.row + LASER_HALF_H;
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].active) continue;
            if (enemies[e].y + enemies[e].h > beam_top &&
                enemies[e].y <= beam_bot) {
                enemies[e].active = 0;
                int pts = enemies[e].type == ET_TANK ? 30 :
                          enemies[e].type == ET_FAST ? 20 : 10;
                score += pts;
                int pc = enemies[e].type == ET_TANK ? 12 :
                         enemies[e].type == ET_BASIC ? 8 : 6;
                spawn_particles_ex(enemies[e].x + enemies[e].w / 2,
                                   enemies[e].y + enemies[e].h / 2,
                                   pc, enemies[e].w * 0.5f, C_HICYAN);
                spawn_flash(enemies[e].x + enemies[e].w / 2,
                            enemies[e].y + enemies[e].h / 2);
            }
        }
    }

    laser.frames_left--;
    if (laser.frames_left <= 0) laser.active = 0;
}

static void draw_laser(void) {
    if (!laser.active) return;
    int x0 = SHIP_COL + SHIP_W;
    for (int dy = -LASER_HALF_H; dy <= LASER_HALF_H; dy++) {
        int y = laser.row + dy;
        if (y < 0 || y >= play_h) continue;
        int dist = abs(dy);
        char ch;
        int color;
        if (dist <= 1)      { ch = '#'; color = C_WHITE; }
        else if (dist <= 3) { ch = '='; color = C_HICYAN; }
        else if (dist <= 5) { ch = '-'; color = C_CYAN; }
        else                { ch = '~'; color = C_CYAN; }
        for (int x = x0; x < term_w; x++)
            grid_put(x, y, ch, color);
    }
}

// ── tap combo ─────────────────────────────────────────────────────

static void record_tap(void) {
    tap_times[tap_idx % TAP_BUF_SIZE] = now_sec();
    tap_idx++;
}

static int check_combo(void) {
    // 4 rapid taps — all gaps < 0.35s
    if (tap_idx < 4) return 0;
    int i0 = (tap_idx - 4) % TAP_BUF_SIZE;
    int i1 = (tap_idx - 3) % TAP_BUF_SIZE;
    int i2 = (tap_idx - 2) % TAP_BUF_SIZE;
    int i3 = (tap_idx - 1) % TAP_BUF_SIZE;
    double g1 = tap_times[i1] - tap_times[i0];
    double g2 = tap_times[i2] - tap_times[i1];
    double g3 = tap_times[i3] - tap_times[i2];
    if (g1 > 0 && g1 < 0.35 &&
        g2 > 0 && g2 < 0.35 &&
        g3 > 0 && g3 < 0.35) {
        tap_idx = 0;
        memset(tap_times, 0, sizeof(tap_times));
        return 1;
    }
    return 0;
}

// ── collision ─────────────────────────────────────────────────────

static void kill_enemy(int e) {
    enemies[e].active = 0;
    int pts = enemies[e].type == ET_TANK ? 30 :
              enemies[e].type == ET_FAST ? 20 : 10;
    score += pts;
    int pc = enemies[e].type == ET_TANK ? 12 :
             enemies[e].type == ET_BASIC ? 8 : 6;
    float spread = enemies[e].w * 0.5f;
    if (spread < 1.0f) spread = 1.0f;
    spawn_particles_ex(enemies[e].x + enemies[e].w / 2,
                       enemies[e].y + enemies[e].h / 2,
                       pc, spread, 0);
    spawn_flash(enemies[e].x + enemies[e].w / 2,
                enemies[e].y + enemies[e].h / 2);
}

static void check_collisions(void) {
    // ship vs perk — fly into it to collect
    for (int p = 0; p < MAX_PERKS; p++) {
        if (!perks[p].active) continue;
        if (perks[p].x + 3 > SHIP_COL &&
            perks[p].x < SHIP_COL + SHIP_W &&
            perks[p].y + 1 > ship_row - 2 &&
            perks[p].y < ship_row + 3) {
            perks[p].active = 0;
            laser_ammo++;
            spawn_particles_ex(perks[p].x + 1, perks[p].y,
                               8, 1.5f, C_HIGREEN);
        }
    }

    // bullet vs enemy
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active) continue;
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].active) continue;
            if (bullets[b].x + bullets[b].w > enemies[e].x &&
                bullets[b].x < enemies[e].x + enemies[e].w &&
                bullets[b].y + bullets[b].h > enemies[e].y &&
                bullets[b].y < enemies[e].y + enemies[e].h) {

                if (bullets[b].type >= BT_BOMB) {
                    int aoe_hw, aoe_hh, pcnt;
                    float spread;
                    switch (bullets[b].type) {
                        case BT_BOMB_L: aoe_hw=6; aoe_hh=3; pcnt=24; spread=3.0f; break;
                        case BT_BOMB_M: aoe_hw=4; aoe_hh=2; pcnt=16; spread=2.5f; break;
                        default:        aoe_hw=2; aoe_hh=1; pcnt=10; spread=2.0f; break;
                    }

                    int cx = enemies[e].x + enemies[e].w / 2;
                    int cy = enemies[e].y + enemies[e].h / 2;

                    for (int e2 = 0; e2 < MAX_ENEMIES; e2++) {
                        if (!enemies[e2].active) continue;
                        if (enemies[e2].x + enemies[e2].w > cx - aoe_hw &&
                            enemies[e2].x < cx + aoe_hw + 1 &&
                            enemies[e2].y + enemies[e2].h > cy - aoe_hh &&
                            enemies[e2].y < cy + aoe_hh + 1) {
                            enemies[e2].hp -= bullets[b].damage;
                            if (enemies[e2].hp <= 0) kill_enemy(e2);
                        }
                    }
                    spawn_particles_ex(cx, cy, pcnt, spread, C_YELLOW);
                    spawn_flash(cx, cy);
                } else {
                    enemies[e].hp -= bullets[b].damage;
                    if (enemies[e].hp <= 0) kill_enemy(e);
                }
                bullets[b].active = 0;
                break;
            }
        }
    }

    // ship vs enemy (5-row ship: ship_row ±2)
    if (invuln_timer > 0) return;
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!enemies[e].active) continue;
        if (enemies[e].x + enemies[e].w > SHIP_COL &&
            enemies[e].x < SHIP_COL + SHIP_W &&
            enemies[e].y + enemies[e].h > ship_row - 2 &&
            enemies[e].y < ship_row + 3) {
            enemies[e].active = 0;
            lives--;
            invuln_timer = INVULN_FRAMES;
            spawn_particles(SHIP_COL + 3, ship_row, 10);
            if (lives <= 0) game_state = ST_OVER;
            break;
        }
    }
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_hud(void) {
    int y = term_h - 1;
    int diff = score / SCORE_PER_LEVEL + 1;
    char buf[128];
    snprintf(buf, sizeof(buf),
             " SCORE: %-8d  LIVES: %-4d  LASER: %-3d  LVL: %d",
             score, lives, laser_ammo, diff);
    int len = (int)strlen(buf);
    for (int x = 0; x < term_w; x++)
        grid_put(x, y, x < len ? buf[x] : ' ', C_WHITE);

    // sensor info — bottom right
    char info[64];
    int lid_ang = g_lid.valid ? g_lid.angle : 0;
    snprintf(info, sizeof(info), "TILT:%d  HIT:%.3fg",
             lid_ang, g_accel.last_impact_mag);
    int ilen = (int)strlen(info);
    int ix = term_w - ilen - 1;
    if (ix < 0) ix = 0;
    for (int i = 0; i < ilen; i++)
        grid_put(ix + i, y, info[i], C_GRAY);
}

// ── game reset ────────────────────────────────────────────────────

static void reset_game(void) {
    score = 0;
    lives = INITIAL_LIVES;
    invuln_timer = 0;
    spawn_timer  = 0;
    ship_row   = play_h / 2;
    ship_row_f = (float)ship_row;
    laser_ammo = 3;
    laser = (Laser){0};
    tap_idx = 0;
    memset(tap_times,  0, sizeof(tap_times));
    memset(bullets,    0, sizeof(bullets));
    memset(enemies,    0, sizeof(enemies));
    memset(particles,  0, sizeof(particles));
    memset(perks,      0, sizeof(perks));
    memset(flashes,    0, sizeof(flashes));
}

// ── input ─────────────────────────────────────────────────────────

static void process_input(void) {
    poll_lid();
    if (!g_lid.valid) return;
    float lid = (float)g_lid.angle;
    float t = 1.0f - (lid - LID_MIN) / (LID_MAX - LID_MIN);  // reversed
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float target = 1.0f + t * (float)(play_h - 3);
    ship_row_f += (target - ship_row_f) * LERP_FACTOR;
    ship_row = (int)(ship_row_f + 0.5f);
    if (ship_row < 2)          ship_row = 2;
    if (ship_row > play_h - 3) ship_row = play_h - 3;
}

// ── game tick ─────────────────────────────────────────────────────

static void game_tick(CFRunLoopTimerRef timer, void *info) {
    (void)timer; (void)info;

    get_term_size();
    grid_alloc();
    grid_clear();
    frame_num++;
    check_launcher_exit();

    switch (game_state) {

    // ── calibration ───────────────────────────────────────────────
    case ST_CAL:
        poll_lid();
        update_stars();
        draw_stars();

        if (!has_accel) {
            game_state = ST_TITLE;
            break;
        }

        {
            int cy = play_h / 2;
            draw_center(cy - 1, "Calibrating sensors...", C_CYAN);
            draw_center(cy + 1, "Keep laptop still", C_WHITE);

            if (g_accel.active && g_accel.cal_target > 0) {
                int pct = g_accel.cal_count * 100 / g_accel.cal_target;
                if (pct > 100) pct = 100;
                int bw = 20;
                int filled = pct * bw / 100;
                char bar[32];
                bar[0] = '[';
                for (int i = 0; i < bw; i++)
                    bar[i + 1] = i < filled ? '#' : '-';
                bar[bw + 1] = ']';
                bar[bw + 2] = '\0';
                draw_center(cy + 3, bar, C_YELLOW);
            }

            if (g_accel.cal_done) {
                game_state = ST_TITLE;
                consume_impact();
            }
        }
        break;

    // ── title screen ──────────────────────────────────────────────
    case ST_TITLE:
        poll_lid();
        update_stars();
        draw_stars();
        {
            int cy = play_h / 2;

            // 3D rotating UMIT
            float lid_angle = g_lid.valid ? (float)g_lid.angle : 105.0f;
            if (!umit_base_set) { umit_base_angle = lid_angle; umit_base_set = 1; }
            float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
            if (rot > PIf / 3.0f) rot = PIf / 3.0f;
            if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
            float cos_r = cosf(rot), sin_r = sinf(rot);

            draw_center(cy - 9, "S P A C E   I M P A C T", C_HICYAN);
            draw_center(cy - 8, "-----------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tilt screen to move ship", C_WHITE);
            draw_center(cy + 8, "Tap desk to shoot", C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 10, "[ TAP DESK TO START ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_PLAY;
                    reset_game();
                }
            } else {
                draw_center(cy + 10,
                    "(no accelerometer - auto mode)", C_YELLOW);
                if (++auto_timer > GAME_FPS * 3) {
                    auto_timer = 0;
                    game_state = ST_PLAY;
                    reset_game();
                }
            }
        }
        break;

    // ── playing ───────────────────────────────────────────────────
    case ST_PLAY:
        process_input();

        // fire
        if (has_accel) {
            double force = consume_impact_force();
            if (force > 0) {
                record_tap();
                if (check_combo() && laser_ammo > 0) {
                    fire_laser();
                } else {
                    spawn_weapon(force);
                }
            }
        } else {
            if (frame_num % 15 == 0) spawn_weapon(0.1);
        }

        // spawn enemies / perks
        {
            int diff = score / SCORE_PER_LEVEL;
            int cd = BASE_SPAWN_CD - diff * 3;
            if (cd < MIN_SPAWN_CD) cd = MIN_SPAWN_CD;
            if (++spawn_timer >= cd) {
                spawn_timer = 0;
                if (rand() % 100 < 10)
                    spawn_perk();
                else
                    spawn_enemy();
            }
        }

        update_bullets();
        update_enemies();
        update_perks();
        update_particles();
        update_stars();
        update_laser();
        update_flashes();
        if (invuln_timer > 0) invuln_timer--;

        check_collisions();

        draw_stars();
        draw_bullets();
        draw_enemies();
        draw_perks();
        draw_laser();
        draw_particles();
        draw_ship();
        draw_flashes();
        draw_hud();
        break;

    // ── game over ─────────────────────────────────────────────────
    case ST_OVER:
        poll_lid();
        update_stars();
        update_particles();
        draw_stars();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 2, "G A M E   O V E R", C_RED);

            char sb[48];
            snprintf(sb, sizeof(sb), "Final Score: %d", score);
            draw_center(cy, sb, C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 3,
                        "[ TAP DESK TO RESTART ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_TITLE;
                    auto_timer = 0;
                }
            } else {
                if (++auto_timer > GAME_FPS * 3) {
                    auto_timer = 0;
                    game_state = ST_TITLE;
                }
            }
        }
        draw_hud();
        break;
    }

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "Space Impact — Starting...\n");

    if (getuid() != 0)
        fprintf(stderr,
            "Warning: not running as root — SPU access needs sudo.\n");

    srand((unsigned)time(NULL));
    if (getenv("TABLEIMPACT_LAUNCHER")) from_launcher = 1;
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    atexit(cleanup);

    get_term_size();
    grid_alloc();
    init_stars();
    ship_row   = play_h / 2;
    ship_row_f = (float)ship_row;

    // sensors
    setup_lid();
    has_accel = setup_accel();

    // suppress sensor debug output during gameplay
    freopen("/dev/null", "w", stderr);

    // clear screen
    write(STDOUT_FILENO, "\033[2J\033[H", 7);

    // game timer at 30 fps
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.1,
        1.0 / GAME_FPS,
        0, 0, game_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopDefaultMode);

    CFRunLoopRun();
    return 0;
}

// arkanoid.c — Arkanoid: classic brick breaker
// Tilt screen to move paddle left/right, tap desk to launch ball.
//
// Build: make arkanoid
// Run:   sudo ./arkanoid

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W       80

#define PADDLE_W         9
#define PADDLE_W_WIDE    15
#define PADDLE_Y_OFFSET  3

#define MAX_BALLS        3
#define BALL_SPEED       1.2f
#define BALL_SPEED_SLOW  0.7f

#define BRICK_W          5
#define BRICK_H          1
#define MAX_BRICK_COLS   14
#define MAX_BRICK_ROWS   8
#define BRICK_TOP        2
#define BRICK_GAP_X      1
#define BRICK_ROW_GAP    1

#define BK_NONE          0
#define BK_NORMAL        1
#define BK_STRONG        2
#define BK_INDESTRUCTIBLE 3

#define MAX_POWERUPS     4
#define POWERUP_DROP_PCT 20
#define POWERUP_SPEED    0.3f
#define WIDE_DURATION    300
#define SLOW_DURATION    300

#define PW_WIDE          0
#define PW_MULTI         1
#define PW_SLOW          2

#define INITIAL_LIVES    3
#define NUM_LEVELS       5
#define LEVEL_BONUS      500

#define LERP_FACTOR      0.25f

// ── types ─────────────────────────────────────────────────────────

typedef struct { float x, y, vx, vy; int active, stuck; } Ball;
typedef struct { int type, hp, color; } Brick;
typedef struct { float x, y; int type, active; } Powerup;

// ── level data ────────────────────────────────────────────────────

static const int level_data[NUM_LEVELS][MAX_BRICK_ROWS][MAX_BRICK_COLS] = {
    // Level 1: 4 solid rows
    {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    },
    // Level 2: Checkerboard with strong bricks
    {
        {2,0,2,0,2,0,2,0,2,0,2,0,2,0},
        {0,1,0,1,0,1,0,1,0,1,0,1,0,1},
        {2,0,2,0,2,0,2,0,2,0,2,0,2,0},
        {0,1,0,1,0,1,0,1,0,1,0,1,0,1},
        {2,0,2,0,2,0,2,0,2,0,2,0,2,0},
        {0,1,0,1,0,1,0,1,0,1,0,1,0,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    },
    // Level 3: Diamond with indestructible border
    {
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {3,0,0,0,0,0,1,1,0,0,0,0,0,3},
        {3,0,0,0,0,1,2,2,1,0,0,0,0,3},
        {3,0,0,0,1,2,2,2,2,1,0,0,0,3},
        {3,0,0,0,0,1,2,2,1,0,0,0,0,3},
        {3,0,0,0,0,0,1,1,0,0,0,0,0,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    },
    // Level 4: Full alternating grid
    {
        {2,1,2,1,2,1,2,1,2,1,2,1,2,1},
        {1,2,1,2,1,2,1,2,1,2,1,2,1,2},
        {2,1,2,1,2,1,2,1,2,1,2,1,2,1},
        {1,2,1,2,1,2,1,2,1,2,1,2,1,2},
        {2,1,2,1,2,1,2,1,2,1,2,1,2,1},
        {1,2,1,2,1,2,1,2,1,2,1,2,1,2},
        {2,1,2,1,2,1,2,1,2,1,2,1,2,1},
        {1,2,1,2,1,2,1,2,1,2,1,2,1,2},
    },
    // Level 5: Fortress
    {
        {0,0,3,3,3,3,3,3,3,3,3,3,0,0},
        {0,3,0,2,2,2,2,2,2,2,2,0,3,0},
        {3,0,0,0,2,2,2,2,2,2,0,0,0,3},
        {3,0,0,0,0,2,2,2,2,0,0,0,0,3},
        {3,0,0,0,2,2,2,2,2,2,0,0,0,3},
        {0,3,0,2,2,2,2,2,2,2,2,0,3,0},
        {0,0,3,3,3,3,3,3,3,3,3,3,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    },
};

static const int row_colors[MAX_BRICK_ROWS] = {
    C_HIRED, C_RED, C_YELLOW, C_GREEN,
    C_CYAN, C_MAGENTA, C_HIGREEN, C_HICYAN,
};

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;

// paddle
static int   paddle_col;
static float paddle_col_f;
static int   paddle_w;
static int   paddle_row;

// balls
static Ball balls[MAX_BALLS];

// bricks
static Brick bricks[MAX_BRICK_ROWS][MAX_BRICK_COLS];
static int   bricks_alive;
static int   brick_cols;
static int   brick_area_x;

// powerups
static Powerup powerups[MAX_POWERUPS];
static int wide_timer;
static int slow_timer;

// game
static int score, best_score;
static int lives;
static int level;
static int ball_launched;
static int auto_launch_timer;

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
    paddle_row = play_h - PADDLE_Y_OFFSET;

    brick_cols = (play_w - 2) / (BRICK_W + BRICK_GAP_X);
    if (brick_cols > MAX_BRICK_COLS) brick_cols = MAX_BRICK_COLS;
    if (brick_cols < 1) brick_cols = 1;

    int grid_w = brick_cols * (BRICK_W + BRICK_GAP_X) - BRICK_GAP_X;
    brick_area_x = play_x + (play_w - grid_w) / 2;
}

// ── brick helpers ─────────────────────────────────────────────────

static int brick_sx(int col) {
    return brick_area_x + col * (BRICK_W + BRICK_GAP_X);
}

static int brick_sy(int row) {
    return BRICK_TOP + row * (BRICK_H + BRICK_ROW_GAP);
}

// ── level loading ─────────────────────────────────────────────────

static void load_level(int lvl) {
    int idx = (lvl - 1) % NUM_LEVELS;
    bricks_alive = 0;

    for (int r = 0; r < MAX_BRICK_ROWS; r++)
        for (int c = 0; c < MAX_BRICK_COLS; c++) {
            if (c >= brick_cols) {
                bricks[r][c] = (Brick){0};
                continue;
            }
            int t = level_data[idx][r][c];
            int hp = 0, color = 0;
            switch (t) {
            case BK_NORMAL:
                hp = 1;
                color = row_colors[r];
                bricks_alive++;
                break;
            case BK_STRONG:
                hp = 2;
                color = row_colors[r];
                bricks_alive++;
                break;
            case BK_INDESTRUCTIBLE:
                hp = 999;
                color = C_GRAY;
                break;
            }
            bricks[r][c] = (Brick){ .type = t, .hp = hp, .color = color };
        }
}

// ── drawing ───────────────────────────────────────────────────────

static void draw_borders(void) {
    if (play_w >= term_w) return;
    int lx = play_x - 1;
    int rx = play_x + play_w;
    for (int y = 0; y < play_h; y++) {
        if (lx >= 0)      grid_put(lx, y, '|', C_GRAY);
        if (rx < term_w)  grid_put(rx, y, '|', C_GRAY);
    }
}

static void draw_top_wall(void) {
    for (int x = play_x; x < play_x + play_w; x++)
        grid_put(x, 0, '=', C_GRAY);
}

static void draw_paddle(void) {
    int color = wide_timer > 0 ? C_HIGREEN : C_HICYAN;
    grid_put(paddle_col, paddle_row, '[', color);
    for (int i = 1; i < paddle_w - 1; i++)
        grid_put(paddle_col + i, paddle_row, '=', color);
    grid_put(paddle_col + paddle_w - 1, paddle_row, ']', color);
}

static void draw_balls(void) {
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active) continue;
        int bx = (int)(balls[i].x + 0.5f);
        int by = (int)(balls[i].y + 0.5f);
        grid_put(bx, by, 'O', C_WHITE);
    }
}

static void draw_bricks(void) {
    for (int r = 0; r < MAX_BRICK_ROWS; r++)
        for (int c = 0; c < brick_cols; c++) {
            if (bricks[r][c].type == BK_NONE) continue;
            int sx = brick_sx(c);
            int sy = brick_sy(r);
            int color = bricks[r][c].color;

            // damaged strong brick → white
            if (bricks[r][c].type == BK_STRONG && bricks[r][c].hp == 1)
                color = C_WHITE;

            char fill = '#';
            if (bricks[r][c].type == BK_STRONG && bricks[r][c].hp == 2)
                fill = '=';
            else if (bricks[r][c].type == BK_INDESTRUCTIBLE)
                fill = 'X';
            else if (bricks[r][c].type == BK_STRONG && bricks[r][c].hp == 1)
                fill = '-';

            grid_put(sx, sy, '[', color);
            for (int i = 1; i < BRICK_W - 1; i++)
                grid_put(sx + i, sy, fill, color);
            grid_put(sx + BRICK_W - 1, sy, ']', color);
        }
}

static void draw_powerups(void) {
    static const char icons[] = "WMS";
    static const int  colors[] = { C_HIGREEN, C_MAGENTA, C_CYAN };
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) continue;
        int px = (int)(powerups[i].x + 0.5f);
        int py = (int)(powerups[i].y + 0.5f);
        int t = powerups[i].type;
        grid_put(px - 1, py, '[', colors[t]);
        grid_put(px,     py, icons[t], colors[t]);
        grid_put(px + 1, py, ']', colors[t]);
    }
}

static void draw_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf),
             " SCORE: %-8d  LIVES: %-4d  LVL: %d",
             score, lives, level);
    int len = (int)strlen(buf);
    for (int x = 0; x < term_w; x++)
        grid_put(x, y, x < len ? buf[x] : ' ', C_WHITE);

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

// ── ball launch ───────────────────────────────────────────────────

static void launch_ball(void) {
    if (!balls[0].active) return;
    balls[0].stuck = 0;
    // angle between -75 and -105 degrees (upward with slight randomness)
    float angle = -PIf / 2.0f + ((rand() % 100) - 50) / 50.0f * (PIf / 6.0f);
    balls[0].vx = BALL_SPEED * cosf(angle);
    balls[0].vy = BALL_SPEED * sinf(angle) * 0.7f;
}

// ── collision: walls ──────────────────────────────────────────────

static void collide_walls(Ball *b) {
    // left wall
    if (b->x < (float)play_x) {
        b->x = (float)play_x;
        b->vx = -b->vx;
    }
    // right wall
    if (b->x >= (float)(play_x + play_w - 1)) {
        b->x = (float)(play_x + play_w - 1) - 0.01f;
        b->vx = -b->vx;
    }
    // top wall
    if (b->y < 1.0f) {
        b->y = 1.0f;
        b->vy = -b->vy;
    }
}

// ── collision: paddle ─────────────────────────────────────────────

static void collide_paddle(Ball *b) {
    if (b->vy <= 0) return;  // only when moving down

    int bx = (int)(b->x + 0.5f);
    int by = (int)(b->y + 0.5f);

    if (by >= paddle_row && by <= paddle_row + 1 &&
        bx >= paddle_col && bx < paddle_col + paddle_w) {

        float hit = (b->x - (float)paddle_col) / (float)(paddle_w - 1);
        if (hit < 0.0f) hit = 0.0f;
        if (hit > 1.0f) hit = 1.0f;

        // map hit position to angle: left=150° center=90° right=30°
        float angle = PIf * (5.0f / 6.0f - hit * 2.0f / 3.0f);
        float speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (speed < 0.1f) speed = BALL_SPEED;

        b->vx = speed * cosf(angle);
        b->vy = -fabsf(speed * sinf(angle)) * 0.7f;

        b->y = (float)(paddle_row - 1);

        spawn_particles_ex(bx, paddle_row, 2, 0.3f, C_HICYAN);
    }
}

// ── collision: bricks ─────────────────────────────────────────────

static void maybe_spawn_powerup(int sx, int sy);

static void collide_bricks(Ball *b) {
    int bx = (int)(b->x + 0.5f);
    int by = (int)(b->y + 0.5f);

    for (int r = 0; r < MAX_BRICK_ROWS; r++) {
        for (int c = 0; c < brick_cols; c++) {
            if (bricks[r][c].type == BK_NONE) continue;

            int rx = brick_sx(c);
            int ry = brick_sy(r);

            // AABB check
            if (bx < rx || bx >= rx + BRICK_W ||
                by < ry || by >= ry + BRICK_H)
                continue;

            // determine reflection axis from previous position
            float prev_x = b->x - b->vx;
            float prev_y = b->y - b->vy;
            int was_left  = (int)(prev_x + 0.5f) < rx;
            int was_right = (int)(prev_x + 0.5f) >= rx + BRICK_W;
            int was_above = (int)(prev_y + 0.5f) < ry;
            int was_below = (int)(prev_y + 0.5f) >= ry + BRICK_H;

            int h_reflect = was_left || was_right;
            int v_reflect = was_above || was_below;

            if (h_reflect) b->vx = -b->vx;
            if (v_reflect) b->vy = -b->vy;
            if (!h_reflect && !v_reflect) {
                // embedded — reflect both
                b->vx = -b->vx;
                b->vy = -b->vy;
            }

            // damage brick
            if (bricks[r][c].type != BK_INDESTRUCTIBLE) {
                bricks[r][c].hp--;
                score += 10;
                if (bricks[r][c].hp <= 0) {
                    int old_color = bricks[r][c].color;
                    bricks[r][c].type = BK_NONE;
                    bricks_alive--;
                    spawn_particles_ex(rx + BRICK_W / 2, ry, 6, 1.0f,
                                       old_color);
                    spawn_flash(rx + BRICK_W / 2, ry);
                    maybe_spawn_powerup(rx + BRICK_W / 2, ry);
                } else {
                    spawn_particles_ex(rx + BRICK_W / 2, ry, 2, 0.5f,
                                       C_WHITE);
                }
            } else {
                spawn_particles_ex(rx + BRICK_W / 2, ry, 2, 0.3f, C_GRAY);
            }

            return;  // one brick per ball per frame
        }
    }
}

// ── powerups ──────────────────────────────────────────────────────

static void maybe_spawn_powerup(int sx, int sy) {
    if (rand() % 100 >= POWERUP_DROP_PCT) return;
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) {
            powerups[i] = (Powerup){
                .x = (float)sx, .y = (float)sy,
                .type = rand() % 3,
                .active = 1
            };
            return;
        }
    }
}

static void update_powerups(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) continue;
        powerups[i].y += POWERUP_SPEED;
        if (powerups[i].y > (float)play_h)
            powerups[i].active = 0;
    }
}

static void collide_powerups(void) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) continue;
        int px = (int)(powerups[i].x + 0.5f);
        int py = (int)(powerups[i].y + 0.5f);

        if (py >= paddle_row && py <= paddle_row + 1 &&
            px >= paddle_col - 1 && px < paddle_col + paddle_w + 1) {
            powerups[i].active = 0;
            spawn_flash(px, py);

            switch (powerups[i].type) {
            case PW_WIDE:
                paddle_w = PADDLE_W_WIDE;
                wide_timer = WIDE_DURATION;
                break;
            case PW_MULTI:
                // split: spawn 2 extra balls from first active ball
                for (int b = 0; b < MAX_BALLS; b++) {
                    if (!balls[b].active || balls[b].stuck) continue;
                    float speed = sqrtf(balls[b].vx * balls[b].vx +
                                        balls[b].vy * balls[b].vy);
                    if (speed < 0.1f) speed = BALL_SPEED;
                    for (int n = 0; n < MAX_BALLS; n++) {
                        if (balls[n].active) continue;
                        float off = (n == 1) ? PIf / 6.0f : -PIf / 6.0f;
                        float ang = atan2f(balls[b].vy, balls[b].vx) + off;
                        balls[n] = (Ball){
                            .x = balls[b].x, .y = balls[b].y,
                            .vx = speed * cosf(ang),
                            .vy = speed * sinf(ang),
                            .active = 1, .stuck = 0
                        };
                    }
                    break;
                }
                break;
            case PW_SLOW:
                slow_timer = SLOW_DURATION;
                break;
            }
        }
    }
}

static void update_powerup_timers(void) {
    if (wide_timer > 0) {
        wide_timer--;
        if (wide_timer <= 0) {
            paddle_w = PADDLE_W;
            // clamp paddle position
            if (paddle_col > play_x + play_w - paddle_w)
                paddle_col = play_x + play_w - paddle_w;
        }
    }
    if (slow_timer > 0)
        slow_timer--;
}

// ── input ─────────────────────────────────────────────────────────

static void process_input(void) {
    poll_lid();
    if (!g_lid.valid) return;
    float lid = (float)g_lid.angle;
    float t = 1.0f - (lid - LID_MIN) / (LID_MAX - LID_MIN);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float target = (float)play_x + t * (float)(play_w - paddle_w);
    paddle_col_f += (target - paddle_col_f) * LERP_FACTOR;
    paddle_col = (int)(paddle_col_f + 0.5f);
    if (paddle_col < play_x) paddle_col = play_x;
    if (paddle_col > play_x + play_w - paddle_w)
        paddle_col = play_x + play_w - paddle_w;
}

// ── auto-pilot AI ─────────────────────────────────────────────────

static void auto_paddle_ai(void) {
    // find lowest active ball (most urgent)
    int best = -1;
    float best_y = -1.0f;
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active || balls[i].stuck) continue;
        if (balls[i].y > best_y) {
            best_y = balls[i].y;
            best = i;
        }
    }

    float target;
    if (best >= 0)
        target = balls[best].x - (float)(paddle_w / 2);
    else
        target = (float)(play_x + play_w / 2 - paddle_w / 2);

    paddle_col_f += (target - paddle_col_f) * 0.08f;
    paddle_col = (int)(paddle_col_f + 0.5f);
    if (paddle_col < play_x) paddle_col = play_x;
    if (paddle_col > play_x + play_w - paddle_w)
        paddle_col = play_x + play_w - paddle_w;
}

// ── game reset ────────────────────────────────────────────────────

static void reset_game(void) {
    score = 0;
    lives = INITIAL_LIVES;
    level = 1;
    ball_launched = 0;
    auto_launch_timer = 0;
    paddle_w = PADDLE_W;
    wide_timer = 0;
    slow_timer = 0;
    paddle_col = play_x + play_w / 2 - paddle_w / 2;
    paddle_col_f = (float)paddle_col;

    memset(balls, 0, sizeof(balls));
    balls[0] = (Ball){
        .x = (float)(paddle_col + paddle_w / 2),
        .y = (float)(paddle_row - 1),
        .active = 1, .stuck = 1
    };
    memset(powerups, 0, sizeof(powerups));
    memset(particles, 0, sizeof(particles));
    memset(flashes, 0, sizeof(flashes));

    load_level(level);
}

// ── game tick ─────────────────────────────────────────────────────

static void game_tick(CFRunLoopTimerRef timer, void *info) {
    (void)timer; (void)info;

    get_term_size();
    update_play_area();
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
        draw_borders();

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
        draw_borders();
        {
            int cy = play_h / 2;

            float lid_angle = g_lid.valid ? (float)g_lid.angle : 105.0f;
            if (!umit_base_set) { umit_base_angle = lid_angle; umit_base_set = 1; }
            float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
            if (rot > PIf / 3.0f) rot = PIf / 3.0f;
            if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
            float cos_r = cosf(rot), sin_r = sinf(rot);

            draw_center(cy - 9, "A R K A N O I D", C_HICYAN);
            draw_center(cy - 8, "----------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tilt screen to move paddle", C_WHITE);
            draw_center(cy + 8, "Tap desk to launch ball", C_WHITE);

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
        // input
        if (has_accel) {
            process_input();
            if (consume_impact()) {
                if (!ball_launched) {
                    launch_ball();
                    ball_launched = 1;
                }
            }
        } else {
            auto_paddle_ai();
            if (!ball_launched) {
                if (++auto_launch_timer >= GAME_FPS) {
                    launch_ball();
                    ball_launched = 1;
                    auto_launch_timer = 0;
                }
            }
        }

        // ball stuck on paddle follows it
        for (int i = 0; i < MAX_BALLS; i++) {
            if (balls[i].active && balls[i].stuck) {
                balls[i].x = (float)(paddle_col + paddle_w / 2);
                balls[i].y = (float)(paddle_row - 1);
            }
        }

        // update balls
        {
            float speed_mult = (slow_timer > 0) ?
                (BALL_SPEED_SLOW / BALL_SPEED) : 1.0f;

            for (int i = 0; i < MAX_BALLS; i++) {
                if (!balls[i].active || balls[i].stuck) continue;
                balls[i].x += balls[i].vx * speed_mult;
                balls[i].y += balls[i].vy * speed_mult;
                collide_walls(&balls[i]);
                collide_paddle(&balls[i]);
                collide_bricks(&balls[i]);

                // fallen below paddle?
                if (balls[i].y > (float)(paddle_row + 1))
                    balls[i].active = 0;
            }
        }

        // count active balls
        {
            int active = 0;
            for (int i = 0; i < MAX_BALLS; i++)
                if (balls[i].active) active++;

            // all balls lost?
            if (ball_launched && active == 0) {
                lives--;
                if (lives <= 0) {
                    game_state = ST_OVER;
                    if (score > best_score) best_score = score;
                    spawn_particles(paddle_col + paddle_w / 2,
                                    paddle_row, 15);
                    spawn_flash(paddle_col + paddle_w / 2, paddle_row);
                } else {
                    // reset ball on paddle
                    ball_launched = 0;
                    auto_launch_timer = 0;
                    memset(balls, 0, sizeof(balls));
                    balls[0] = (Ball){
                        .x = (float)(paddle_col + paddle_w / 2),
                        .y = (float)(paddle_row - 1),
                        .active = 1, .stuck = 1
                    };
                    // clear powerup effects
                    wide_timer = 0;
                    paddle_w = PADDLE_W;
                    slow_timer = 0;
                    memset(powerups, 0, sizeof(powerups));
                }
            }
        }

        // level clear?
        if (bricks_alive <= 0 && ball_launched) {
            score += LEVEL_BONUS;
            level++;
            ball_launched = 0;
            auto_launch_timer = 0;
            memset(balls, 0, sizeof(balls));
            balls[0] = (Ball){
                .x = (float)(paddle_col + paddle_w / 2),
                .y = (float)(paddle_row - 1),
                .active = 1, .stuck = 1
            };
            memset(powerups, 0, sizeof(powerups));
            wide_timer = 0;
            paddle_w = PADDLE_W;
            slow_timer = 0;
            load_level(level);
            spawn_flash(term_w / 2, play_h / 2);
            spawn_particles(term_w / 2, play_h / 2, 20);
        }

        // powerups
        update_powerups();
        collide_powerups();
        update_powerup_timers();

        // effects
        update_particles();
        update_stars();
        update_flashes();

        // draw
        draw_stars();
        draw_borders();
        draw_top_wall();
        draw_bricks();
        draw_powerups();
        draw_paddle();
        draw_balls();
        draw_particles();
        draw_flashes();
        draw_hud();
        break;

    // ── game over ─────────────────────────────────────────────────
    case ST_OVER:
        poll_lid();
        update_stars();
        update_particles();
        draw_stars();
        draw_borders();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            char sb[48];
            snprintf(sb, sizeof(sb), "Score: %d", score);
            draw_center(cy, sb, C_YELLOW);

            snprintf(sb, sizeof(sb), "Level: %d", level);
            draw_center(cy + 1, sb, C_WHITE);

            snprintf(sb, sizeof(sb), "Best:  %d", best_score);
            draw_center(cy + 2, sb, C_GRAY);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 4,
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
    fprintf(stderr, "Arkanoid — Starting...\n");

    if (getuid() != 0)
        fprintf(stderr,
            "Warning: not running as root — SPU access needs sudo.\n");

    srand((unsigned)time(NULL));
    if (getenv("TABLEIMPACT_LAUNCHER")) from_launcher = 1;
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    atexit(cleanup);

    get_term_size();
    update_play_area();
    grid_alloc();
    init_stars();

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

// dino.c — Dino Runner: Chrome-style dinosaur endless runner
// Tap desk to jump over obstacles. That's it.
//
// Build: make dino
// Run:   sudo ./dino

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W      9999
#define DINO_COL        10
#define DINO_W          6
#define DINO_H          4

#define JUMP_VY        -1.4f
#define GRAVITY         0.12f
#define TERMINAL_VY     1.5f

#define MAX_OBSTACLES   8
#define OBS_CACTUS_S    0
#define OBS_CACTUS_L    1
#define OBS_PTERO       2

#define BASE_SPEED      1
#define MAX_SPEED       1
#define SPEED_INC_SCORE 300

#define BASE_SPAWN_CD   45
#define MIN_SPAWN_CD    35

#define PTERO_THRESHOLD 200
#define SCORE_INTERVAL  3

#define DINO_ANIM_RATE  4
#define PTERO_ANIM_RATE 6

// ── types ─────────────────────────────────────────────────────────

typedef struct {
    int type, x, y, w, h, scored, active;
} Obstacle;

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;
static int ground_y;
static int ground_scroll;

static float dino_y, dino_vy;
static int   dino_row, dino_on_ground;
static int   dino_score, best_score;
static int   game_speed, spawn_timer, score_timer;
static Obstacle obstacles[MAX_OBSTACLES];

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
    ground_y = play_h - 3;
}

// ── dino sprite ───────────────────────────────────────────────────
//  Frame 0:        Frame 1:
//   .--@            .--@
//  /#--'           /#--'
//   ##              ##
//   | |            |   |

static void draw_dino(void) {
    int x = play_x + DINO_COL;
    int y = dino_row;
    int anim = (frame_num / DINO_ANIM_RATE) % 2;

    // rows 0-2 are the same for both frames
    grid_str(x + 1, y - 3, ".--@", C_HIGREEN);
    grid_str(x,     y - 2, "/#--'", C_HIGREEN);
    grid_str(x + 1, y - 1, "##", C_HIGREEN);

    // legs: freeze frame 0 when airborne
    if (!dino_on_ground || anim == 0) {
        grid_str(x + 1, y, "| |", C_HIGREEN);
    } else {
        grid_str(x, y, "|   |", C_HIGREEN);
    }
}

// ── dino physics ──────────────────────────────────────────────────

static void update_dino(void) {
    dino_vy += GRAVITY;
    if (dino_vy > TERMINAL_VY) dino_vy = TERMINAL_VY;
    dino_y += dino_vy;
    dino_row = (int)(dino_y + 0.5f);
    if (dino_row >= ground_y) {
        dino_row = ground_y;
        dino_y = (float)ground_y;
        dino_vy = 0;
        dino_on_ground = 1;
    } else {
        dino_on_ground = 0;
    }
}

// ── obstacles ─────────────────────────────────────────────────────

static void spawn_obstacle(void) {
    int type;
    if (dino_score >= PTERO_THRESHOLD && rand() % 100 < 30)
        type = OBS_PTERO;
    else if (rand() % 2 == 0)
        type = OBS_CACTUS_S;
    else
        type = OBS_CACTUS_L;

    int w, h, y;
    switch (type) {
    case OBS_CACTUS_S:
        w = 2; h = 3;
        y = ground_y - (h - 1);
        break;
    case OBS_CACTUS_L:
        w = 5; h = 4;
        y = ground_y - (h - 1);
        break;
    case OBS_PTERO:
        w = 5; h = 2;
        y = ground_y - 3;  // low-flying, jumpable
        break;
    default:
        return;
    }

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            obstacles[i] = (Obstacle){
                .type = type,
                .x = play_x + play_w,
                .y = y,
                .w = w,
                .h = h,
                .scored = 0,
                .active = 1
            };
            return;
        }
    }
}

static void update_obstacles(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) continue;
        obstacles[i].x -= game_speed;
        if (obstacles[i].x + obstacles[i].w < play_x) {
            obstacles[i].active = 0;
            continue;
        }
        // score when dino passes obstacle
        if (!obstacles[i].scored &&
            obstacles[i].x + obstacles[i].w < play_x + DINO_COL) {
            obstacles[i].scored = 1;
        }
    }
}

static void draw_obstacles(void) {
    int ptero_anim = (frame_num / PTERO_ANIM_RATE) % 2;

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) continue;
        int x = obstacles[i].x;
        int y = obstacles[i].y;

        switch (obstacles[i].type) {
        case OBS_CACTUS_S:
            // 2x3 small cactus
            grid_str(x, y,     "||", C_GREEN);
            grid_str(x, y + 1, "{}", C_GREEN);
            grid_str(x, y + 2, "||", C_GREEN);
            break;

        case OBS_CACTUS_L:
            // 5x4 large cactus
            grid_str(x, y,     " | | ", C_GREEN);
            grid_str(x, y + 1, "}|||{", C_GREEN);
            grid_str(x, y + 2, " ||| ", C_GREEN);
            grid_str(x, y + 3, " ||| ", C_GREEN);
            break;

        case OBS_PTERO:
            // 5x2 pterodactyl with wing animation
            if (ptero_anim == 0) {
                grid_str(x, y,     "\\v^v/", C_YELLOW);
                grid_str(x, y + 1, " \\>/ ", C_YELLOW);
            } else {
                grid_str(x, y,     " />\\ ", C_YELLOW);
                grid_str(x, y + 1, "/v_v\\", C_YELLOW);
            }
            break;
        }
    }
}

// ── ground ────────────────────────────────────────────────────────

static void draw_ground(void) {
    // solid ground line
    int gy = ground_y + 1;
    for (int x = play_x; x < play_x + play_w; x++)
        grid_put(x, gy, '-', C_WHITE);

    // scrolling texture below ground
    static const char tex[] = "_.-=-._.-";
    int tex_len = (int)(sizeof(tex) - 1);
    int ty = ground_y + 2;
    for (int x = play_x; x < play_x + play_w; x++) {
        int idx = ((x - play_x) + ground_scroll) % tex_len;
        if (idx < 0) idx += tex_len;
        grid_put(x, ty, tex[idx], C_GRAY);
    }
}

// ── collision ─────────────────────────────────────────────────────

static int check_dino_collision(void) {
    int dx1 = play_x + DINO_COL;
    int dx2 = dx1 + DINO_W;
    int dy1 = dino_row - (DINO_H - 1);
    int dy2 = dino_row + 1;

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) continue;
        int ox1 = obstacles[i].x;
        int ox2 = ox1 + obstacles[i].w;
        int oy1 = obstacles[i].y;
        int oy2 = oy1 + obstacles[i].h;

        if (dx2 > ox1 && dx1 < ox2 && dy2 > oy1 && dy1 < oy2)
            return 1;
    }
    return 0;
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_dino_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf), " SCORE: %-8d  BEST: %-8d  SPEED: %d",
             dino_score, best_score, game_speed);
    int len = (int)strlen(buf);
    for (int x = 0; x < term_w; x++)
        grid_put(x, y, x < len ? buf[x] : ' ', C_WHITE);

    // sensor info — bottom right
    char info[64];
    snprintf(info, sizeof(info), "HIT:%.3fg", g_accel.last_impact_mag);
    int ilen = (int)strlen(info);
    int ix = term_w - ilen - 1;
    if (ix < 0) ix = 0;
    for (int i = 0; i < ilen; i++)
        grid_put(ix + i, y, info[i], C_GRAY);
}

// ── borders ───────────────────────────────────────────────────────

static void draw_borders(void) {
    if (play_w >= term_w) return;
    int lx = play_x - 1;
    int rx = play_x + play_w;
    for (int y = 0; y < play_h; y++) {
        if (lx >= 0)      grid_put(lx, y, '|', C_GRAY);
        if (rx < term_w)  grid_put(rx, y, '|', C_GRAY);
    }
}

// ── reset ─────────────────────────────────────────────────────────

static void reset_dino(void) {
    dino_y = (float)ground_y;
    dino_vy = 0;
    dino_row = ground_y;
    dino_on_ground = 1;
    dino_score = 0;
    game_speed = BASE_SPEED;
    spawn_timer = 0;
    score_timer = 0;
    ground_scroll = 0;
    memset(obstacles,  0, sizeof(obstacles));
    memset(particles,  0, sizeof(particles));
    memset(flashes,    0, sizeof(flashes));
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
        draw_ground();
        {
            int cy = play_h / 2;

            // 3D rotating UMIT
            float lid_angle = g_lid.valid ? (float)g_lid.angle : 105.0f;
            if (!umit_base_set) { umit_base_angle = lid_angle; umit_base_set = 1; }
            float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
            if (rot > PIf / 3.0f) rot = PIf / 3.0f;
            if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
            float cos_r = cosf(rot), sin_r = sinf(rot);

            draw_center(cy - 9, "D I N O   R U N N E R", C_HIGREEN);
            draw_center(cy - 8, "---------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tap desk to jump", C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 9, "[ TAP DESK TO START ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_PLAY;
                    reset_dino();
                }
            } else {
                draw_center(cy + 9,
                    "(no accelerometer - auto mode)", C_YELLOW);
                if (++auto_timer > GAME_FPS * 3) {
                    auto_timer = 0;
                    game_state = ST_PLAY;
                    reset_dino();
                }
            }
        }
        break;

    // ── playing ───────────────────────────────────────────────────
    case ST_PLAY:
        // jump on impact (only when grounded)
        if (has_accel) {
            if (consume_impact() && dino_on_ground) {
                dino_vy = JUMP_VY;
                dino_on_ground = 0;
                spawn_particles_ex(play_x + DINO_COL + 2, dino_row,
                                   4, 0.5f, C_WHITE);
            }
        } else {
            // auto-jump when nearest obstacle is close
            int nearest = 9999;
            for (int i = 0; i < MAX_OBSTACLES; i++) {
                if (!obstacles[i].active) continue;
                int dist = obstacles[i].x - (play_x + DINO_COL + DINO_W);
                if (dist > 0 && dist < nearest)
                    nearest = dist;
            }
            int react_dist = 8 + game_speed * 3;
            if (dino_on_ground && nearest <= react_dist) {
                dino_vy = JUMP_VY;
                dino_on_ground = 0;
            }
        }

        update_dino();

        // scoring: +1 every SCORE_INTERVAL frames
        if (++score_timer >= SCORE_INTERVAL) {
            score_timer = 0;
            dino_score++;

            // milestone flash every 100 points
            if (dino_score % 100 == 0)
                spawn_flash(play_x + DINO_COL + DINO_W / 2,
                            dino_row - DINO_H / 2);
        }

        // difficulty
        game_speed = BASE_SPEED + dino_score / SPEED_INC_SCORE;
        if (game_speed > MAX_SPEED) game_speed = MAX_SPEED;

        // spawn obstacles (cooldown decreases with score)
        {
            int cd = BASE_SPAWN_CD - dino_score / 30;
            if (cd < MIN_SPAWN_CD) cd = MIN_SPAWN_CD;
            if (++spawn_timer >= cd) {
                spawn_timer = 0;
                spawn_obstacle();
            }
        }

        // scroll ground
        ground_scroll += game_speed;

        update_obstacles();
        update_particles();
        update_stars();
        update_flashes();

        // collision
        if (check_dino_collision()) {
            game_state = ST_OVER;
            if (dino_score > best_score) best_score = dino_score;
            spawn_particles_ex(play_x + DINO_COL + DINO_W / 2,
                               dino_row - DINO_H / 2, 15, 2.0f, C_YELLOW);
            spawn_flash(play_x + DINO_COL + DINO_W / 2,
                        dino_row - DINO_H / 2);
        }

        draw_stars();
        draw_borders();
        draw_ground();
        draw_obstacles();
        draw_dino();
        draw_particles();
        draw_flashes();
        draw_dino_hud();
        break;

    // ── game over ─────────────────────────────────────────────────
    case ST_OVER:
        poll_lid();
        update_stars();
        update_particles();
        draw_stars();
        draw_borders();
        draw_ground();
        draw_obstacles();
        draw_dino();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            char sb[48];
            snprintf(sb, sizeof(sb), "Score: %d", dino_score);
            draw_center(cy, sb, C_YELLOW);

            snprintf(sb, sizeof(sb), "Best:  %d", best_score);
            draw_center(cy + 1, sb, C_WHITE);

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
        draw_dino_hud();
        break;
    }

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "Dino Runner — Starting...\n");

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

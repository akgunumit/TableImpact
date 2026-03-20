// flappy_bird.c — Flappy Bird: tap desk to flap through pipes
// Tap desk to flap. That's it.
//
// Build: make flappy_bird
// Run:   sudo ./flappy_bird

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PIPES           8
#define PIPE_W              5
#define BIRD_COL            15
#define PIPE_SPAWN_INTERVAL 60
#define INITIAL_GAP         12
#define MIN_GAP             6
#define GRAVITY             0.08f
#define FLAP_VY            -1.0f
#define TERMINAL_VY         1.5f

// ── types ─────────────────────────────────────────────────────────

typedef struct { int x, gap_y, gap_h, scored, active; } Pipe;

// ── globals ───────────────────────────────────────────────────────

static float bird_y, bird_vy;
static int   bird_row;
static int   flappy_score;
static int   best_score;
static int   pipe_timer;
static Pipe  pipes[MAX_PIPES];

// ── bird ──────────────────────────────────────────────────────────

static void draw_bird(void) {
    int x = BIRD_COL, y = bird_row;
    if (frame_num % 8 < 4) {
        // wings up
        grid_str(x, y - 1, " /'^", C_YELLOW);
        grid_str(x, y,     "(o=>", C_YELLOW);
        grid_str(x, y + 1, " \\_,", C_YELLOW);
    } else {
        // wings down
        grid_str(x, y - 1, " ---", C_YELLOW);
        grid_str(x, y,     "(o=>", C_YELLOW);
        grid_str(x, y + 1, " ---", C_YELLOW);
    }
}

// ── pipes ─────────────────────────────────────────────────────────

static void spawn_pipe(void) {
    int gap_h = INITIAL_GAP - flappy_score / 5;
    if (gap_h < MIN_GAP) gap_h = MIN_GAP;

    int min_y = 2;
    int max_y = play_h - gap_h - 2;
    if (max_y < min_y) max_y = min_y;
    int gap_y = min_y + rand() % (max_y - min_y + 1);

    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) {
            pipes[i] = (Pipe){
                .x = term_w,
                .gap_y = gap_y,
                .gap_h = gap_h,
                .scored = 0,
                .active = 1
            };
            return;
        }
    }
}

static void update_pipes(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) continue;
        pipes[i].x--;
        if (pipes[i].x + PIPE_W < 0) {
            pipes[i].active = 0;
            continue;
        }
        // score when bird passes the right edge of pipe
        if (!pipes[i].scored && pipes[i].x + PIPE_W < BIRD_COL) {
            pipes[i].scored = 1;
            flappy_score++;
        }
    }
}

static void draw_pipes(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) continue;
        int x = pipes[i].x;
        int gap_y = pipes[i].gap_y;
        int gap_h = pipes[i].gap_h;
        int gap_bot = gap_y + gap_h;

        // top pipe
        for (int row = 0; row < gap_y; row++) {
            if (row == gap_y - 1) {
                // cap
                for (int c = 0; c < PIPE_W; c++)
                    grid_put(x + c, row, '=', C_HIGREEN);
            } else {
                // body
                grid_put(x, row, '|', C_GREEN);
                for (int c = 1; c < PIPE_W - 1; c++)
                    grid_put(x + c, row, ' ', C_GREEN);
                grid_put(x + PIPE_W - 1, row, '|', C_GREEN);
            }
        }

        // bottom pipe
        for (int row = gap_bot; row < play_h; row++) {
            if (row == gap_bot) {
                // cap
                for (int c = 0; c < PIPE_W; c++)
                    grid_put(x + c, row, '=', C_HIGREEN);
            } else {
                // body
                grid_put(x, row, '|', C_GREEN);
                for (int c = 1; c < PIPE_W - 1; c++)
                    grid_put(x + c, row, ' ', C_GREEN);
                grid_put(x + PIPE_W - 1, row, '|', C_GREEN);
            }
        }
    }
}

// ── bird physics ──────────────────────────────────────────────────

static void update_bird(void) {
    bird_vy += GRAVITY;
    if (bird_vy > TERMINAL_VY) bird_vy = TERMINAL_VY;
    bird_y += bird_vy;
    bird_row = (int)(bird_y + 0.5f);
}

static int check_bird_collision(void) {
    // floor / ceiling
    if (bird_row <= 0 || bird_row >= play_h - 1) return 1;

    // pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) continue;
        int bx1 = BIRD_COL, bx2 = BIRD_COL + 4;
        int by1 = bird_row - 1, by2 = bird_row + 1;
        int px1 = pipes[i].x, px2 = pipes[i].x + PIPE_W;

        // check horizontal overlap
        if (bx2 <= px1 || bx1 >= px2) continue;

        // check if bird is outside the gap
        int gap_top = pipes[i].gap_y;
        int gap_bot = pipes[i].gap_y + pipes[i].gap_h;
        if (by1 < gap_top || by2 >= gap_bot) return 1;
    }
    return 0;
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_flappy_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf), " SCORE: %d", flappy_score);
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

// ── reset ─────────────────────────────────────────────────────────

static void reset_flappy(void) {
    bird_y = (float)(play_h / 2);
    bird_vy = 0;
    bird_row = play_h / 2;
    flappy_score = 0;
    pipe_timer = 0;
    memset(pipes,     0, sizeof(pipes));
    memset(particles, 0, sizeof(particles));
    memset(flashes,   0, sizeof(flashes));
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

            draw_center(cy - 9, "F L A P P Y   B I R D", C_YELLOW);
            draw_center(cy - 8, "---------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tilt screen to rotate title", C_WHITE);
            draw_center(cy + 8, "Tap desk to flap", C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 10, "[ TAP DESK TO START ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_PLAY;
                    reset_flappy();
                }
            } else {
                draw_center(cy + 10,
                    "(no accelerometer - auto mode)", C_YELLOW);
                if (++auto_timer > GAME_FPS * 3) {
                    auto_timer = 0;
                    game_state = ST_PLAY;
                    reset_flappy();
                }
            }
        }
        break;

    // ── playing ───────────────────────────────────────────────────
    case ST_PLAY:
        // flap on impact
        if (has_accel) {
            if (consume_impact()) {
                bird_vy = FLAP_VY;
                spawn_particles_ex(BIRD_COL, bird_row, 4, 0.5f, C_WHITE);
            }
        } else {
            // auto flap every ~20 frames
            if (frame_num % 20 == 0) bird_vy = FLAP_VY;
        }

        update_bird();

        // spawn pipes
        if (++pipe_timer >= PIPE_SPAWN_INTERVAL) {
            pipe_timer = 0;
            spawn_pipe();
        }

        update_pipes();
        update_particles();
        update_stars();
        update_flashes();

        // check collision
        if (check_bird_collision()) {
            game_state = ST_OVER;
            spawn_particles_ex(BIRD_COL, bird_row, 15, 2.0f, C_YELLOW);
            spawn_flash(BIRD_COL, bird_row);
        }

        draw_stars();
        draw_pipes();
        draw_bird();
        draw_particles();
        draw_flashes();
        draw_flappy_hud();
        break;

    // ── game over ─────────────────────────────────────────────────
    case ST_OVER:
        poll_lid();
        update_stars();
        update_particles();
        draw_stars();
        draw_particles();
        draw_pipes();

        {
            int cy = play_h / 2;
            if (flappy_score > best_score) best_score = flappy_score;

            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            char sb[48];
            snprintf(sb, sizeof(sb), "Score: %d", flappy_score);
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
        draw_flappy_hud();
        break;
    }

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "Flappy Bird — Starting...\n");

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

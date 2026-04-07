// stack_tower.c — Stack Tower: Tap to stack blocks
// Blocks slide left/right, tap to drop. Overhang gets trimmed.
//
// Build: make stack_tower
// Run:   sudo ./stack_tower

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W       80
#define MAX_TOWER        200
#define INITIAL_WIDTH    14
#define MAX_BLOCK_WIDTH  20
#define PERFECT_EXPAND   2
#define BASE_SPEED       3      // frames between slide steps (slower)
#define MIN_SPEED        1      // fastest (every frame)
#define SPEED_INTERVAL   8      // blocks between speed-ups
#define PERFECT_FRAMES   20     // duration of "PERFECT!" text

// ── types ─────────────────────────────────────────────────────────

typedef struct {
    int col;
    int width;
    int color;
} Block;

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;
static int base_row;

static Block tower[MAX_TOWER];
static int   tower_height;

static int   slide_col;
static int   slide_width;
static int   slide_dir;
static int   slide_color;
static int   slide_timer;

static int   stack_score;
static int   best_score;
static int   perfect_streak;
static int   perfect_timer;
static int   camera_offset;

static int   auto_tol;         // auto-play tolerance per block

static const int block_colors[] = {
    C_HIRED, C_YELLOW, C_HIGREEN, C_HICYAN, C_MAGENTA, C_CYAN, C_WHITE
};
#define NUM_COLORS 7

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
    base_row = play_h - 2;
}

// ── block drawing ─────────────────────────────────────────────────

static void draw_block(int col, int row, int width, int color) {
    if (row < 0 || row >= play_h || width <= 0) return;
    if (width == 1) {
        grid_put(col, row, '#', color);
    } else if (width == 2) {
        grid_put(col, row, '[', color);
        grid_put(col + 1, row, ']', color);
    } else {
        grid_put(col, row, '[', color);
        for (int i = 1; i < width - 1; i++)
            grid_put(col + i, row, '=', color);
        grid_put(col + width - 1, row, ']', color);
    }
}

// ── tower drawing ─────────────────────────────────────────────────

static void draw_tower(void) {
    for (int i = 0; i < tower_height; i++) {
        int row = base_row - i + camera_offset;
        draw_block(tower[i].col, row, tower[i].width, tower[i].color);
    }
}

// ── sliding block ─────────────────────────────────────────────────

static void setup_slide(void) {
    slide_width = tower[tower_height - 1].width;
    slide_color = block_colors[tower_height % NUM_COLORS];
    slide_dir   = (tower_height % 2 == 0) ? 1 : -1;
    slide_col   = (slide_dir > 0) ? play_x : play_x + play_w - slide_width;
    slide_timer = 0;
    auto_tol    = (rand() % 3 == 0) ? 1 : 0;
}

static void update_slide(void) {
    int speed = BASE_SPEED - stack_score / SPEED_INTERVAL;
    if (speed < MIN_SPEED) speed = MIN_SPEED;

    if (++slide_timer >= speed) {
        slide_timer = 0;
        slide_col += slide_dir;

        if (slide_col + slide_width > play_x + play_w) {
            slide_col = play_x + play_w - slide_width;
            slide_dir = -1;
        }
        if (slide_col < play_x) {
            slide_col = play_x;
            slide_dir = 1;
        }
    }
}

static void draw_slide(void) {
    int row = base_row - tower_height + camera_offset;
    draw_block(slide_col, row, slide_width, slide_color);
}

// ── camera ────────────────────────────────────────────────────────

static void update_camera(void) {
    int needed = tower_height - (base_row - 3);
    camera_offset = (needed > 0) ? needed : 0;
}

// ── placement ─────────────────────────────────────────────────────

static int place_block(void) {
    Block *prev = &tower[tower_height - 1];

    int new_left    = slide_col > prev->col ? slide_col : prev->col;
    int slide_right = slide_col + slide_width;
    int prev_right  = prev->col + prev->width;
    int new_right   = slide_right < prev_right ? slide_right : prev_right;
    int overlap     = new_right - new_left;

    if (overlap <= 0) {
        // complete miss — game over
        int row = base_row - tower_height + camera_offset;
        int cx  = slide_col + slide_width / 2;
        spawn_particles_ex(cx, row, 15, 2.0f, C_HIRED);
        spawn_flash(cx, row);
        return 0;
    }

    int is_perfect = (slide_col == prev->col && slide_width == prev->width);
    int new_col, new_width;
    int slide_row = base_row - tower_height + camera_offset;

    if (is_perfect) {
        perfect_streak++;
        perfect_timer = PERFECT_FRAMES;

        new_width = prev->width + PERFECT_EXPAND;
        if (new_width > MAX_BLOCK_WIDTH) new_width = MAX_BLOCK_WIDTH;
        new_col = prev->col - PERFECT_EXPAND / 2;
        if (new_col < play_x) new_col = play_x;
        if (new_col + new_width > play_x + play_w)
            new_width = play_x + play_w - new_col;

        int cx = new_col + new_width / 2;
        spawn_flash(cx, slide_row);
        spawn_particles_ex(cx, slide_row, 10, 1.5f, C_HICYAN);
    } else {
        perfect_streak = 0;
        new_col   = new_left;
        new_width = overlap;

        // particles at trimmed edges
        if (slide_col < prev->col)
            spawn_particles_ex(slide_col + 1, slide_row, 4, 0.8f,
                               slide_color);
        if (slide_right > prev_right)
            spawn_particles_ex(prev_right - 1, slide_row, 4, 0.8f,
                               slide_color);
    }

    if (tower_height >= MAX_TOWER) return 0;

    tower[tower_height].col   = new_col;
    tower[tower_height].width = new_width;
    tower[tower_height].color = slide_color;
    tower_height++;
    stack_score++;

    setup_slide();
    return 1;
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf), " SCORE: %-8d  BEST: %-8d",
             stack_score, best_score);
    int len = (int)strlen(buf);
    for (int x = 0; x < term_w; x++)
        grid_put(x, y, x < len ? buf[x] : ' ', C_WHITE);

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
        if (lx >= 0)     grid_put(lx, y, '|', C_GRAY);
        if (rx < term_w) grid_put(rx, y, '|', C_GRAY);
    }
}

// ── perfect text ──────────────────────────────────────────────────

static void draw_perfect_text(void) {
    if (perfect_timer <= 0) return;
    perfect_timer--;

    int row = base_row - tower_height + camera_offset - 1;
    if (row < 0) row = 0;
    if (row >= play_h) return;

    char buf[32];
    if (perfect_streak > 1)
        snprintf(buf, sizeof(buf), "PERFECT x%d!", perfect_streak);
    else
        snprintf(buf, sizeof(buf), "PERFECT!");

    int len = (int)strlen(buf);
    int cx  = play_x + play_w / 2 - len / 2;
    grid_str(cx, row, buf, C_HICYAN);
}

// ── reset ─────────────────────────────────────────────────────────

static void reset_game(void) {
    tower_height   = 0;
    stack_score    = 0;
    perfect_streak = 0;
    perfect_timer  = 0;
    camera_offset  = 0;
    memset(tower,     0, sizeof(tower));
    memset(particles, 0, sizeof(particles));
    memset(flashes,   0, sizeof(flashes));

    // foundation block — centered, full width
    tower[0].col   = play_x + play_w / 2 - INITIAL_WIDTH / 2;
    tower[0].width = INITIAL_WIDTH;
    tower[0].color = block_colors[0];
    tower_height   = 1;

    setup_slide();
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
            if (!umit_base_set) {
                umit_base_angle = lid_angle;
                umit_base_set = 1;
            }
            float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
            if (rot >  PIf / 3.0f) rot =  PIf / 3.0f;
            if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
            float cos_r = cosf(rot), sin_r = sinf(rot);

            draw_center(cy - 9, "S T A C K   T O W E R", C_HICYAN);
            draw_center(cy - 8, "---------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tap desk to stack blocks", C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 9,
                        "[ TAP DESK TO START ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_PLAY;
                    reset_game();
                }
            } else {
                draw_center(cy + 9,
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
        update_slide();

        if (has_accel) {
            if (consume_impact()) {
                if (!place_block()) {
                    game_state = ST_OVER;
                    if (stack_score > best_score)
                        best_score = stack_score;
                }
            }
        } else {
            // auto-play: place when aligned within tolerance
            int target = tower[tower_height - 1].col;
            int diff   = slide_col - target;
            if (diff < 0) diff = -diff;
            if (diff <= auto_tol) {
                if (!place_block()) {
                    game_state = ST_OVER;
                    if (stack_score > best_score)
                        best_score = stack_score;
                }
            }
        }

        update_camera();
        update_particles();
        update_stars();
        update_flashes();

        draw_stars();
        draw_borders();
        draw_tower();
        draw_slide();
        draw_perfect_text();
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
        draw_tower();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            char sb[48];
            snprintf(sb, sizeof(sb), "Score: %d", stack_score);
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
        draw_hud();
        break;
    }

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "Stack Tower — Starting...\n");

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

    setup_lid();
    has_accel = setup_accel();

    freopen("/dev/null", "w", stderr);

    write(STDOUT_FILENO, "\033[2J\033[H", 7);

    CFRunLoopTimerRef t = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.1,
        1.0 / GAME_FPS,
        0, 0, game_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopDefaultMode);

    CFRunLoopRun();
    return 0;
}

// lumberjack.c — Lumberjack: Chop fast, dodge branches
// Tap to chop bottom log & switch sides. Don't stand under a branch!
// Timer drains constantly — chop faster to stay alive.
//
// Build: make lumberjack
// Run:   sudo ./lumberjack

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W      80
#define MAX_SEGS        20
#define VISIBLE_CAP     14

#define BR_NONE         0
#define BR_LEFT         1
#define BR_RIGHT        2

#define TIMER_MAX       300
#define TIMER_BAR_W     30

// difficulty phases by chop_score
#define PHASE1          20
#define PHASE2          50

// ── types ─────────────────────────────────────────────────────────

typedef struct { int branch; } Seg;

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;
static int trunk_x;        // x position of first char in |##|
static int chop_y;         // row of bottom tree segment
static int visible;        // visible segment count

static Seg tree[MAX_SEGS];
static int player_side;    // 0=left, 1=right
static int gen_side;       // tracks which side player will be when reaching new seg
static int chop_score, best_score;
static int timer_val;
static int death_cause;    // 0=branch, 1=timer

static int chop_anim;      // frames left for chop visual
static int auto_cd, auto_count;

// ── difficulty helpers ────────────────────────────────────────────

static int get_drain(void) {
    return chop_score < PHASE1 ? 1 : chop_score < PHASE2 ? 2 : 3;
}

static int get_refill(void) {
    return chop_score < PHASE1 ? 40 : chop_score < PHASE2 ? 30 : 22;
}

static int get_branch_pct(void) {
    return chop_score < PHASE1 ? 35 : chop_score < PHASE2 ? 50 : 65;
}

// ── tree generation ───────────────────────────────────────────────
// Branch is always placed on the OPPOSITE side from where the player
// will be when reaching that segment, so the tree is always survivable.

static Seg gen_seg(void) {
    Seg s = { BR_NONE };
    if (rand() % 100 < get_branch_pct())
        s.branch = (gen_side == 0) ? BR_RIGHT : BR_LEFT;
    gen_side = 1 - gen_side;
    return s;
}

static void init_tree(void) {
    player_side = 0;        // start on left
    gen_side = 1;           // after first chop player will be on right
    tree[0].branch = BR_NONE;
    for (int i = 1; i < MAX_SEGS; i++)
        tree[i] = gen_seg();
}

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
    trunk_x = term_w / 2 - 2;
    chop_y = play_h - 4;
    visible = chop_y - 1;
    if (visible > VISIBLE_CAP) visible = VISIBLE_CAP;
    if (visible > MAX_SEGS) visible = MAX_SEGS;
    if (visible < 4) visible = 4;
}

// ── chop ──────────────────────────────────────────────────────────

static void do_chop(void) {
    // shift tree down (remove bottom)
    for (int i = 0; i < MAX_SEGS - 1; i++)
        tree[i] = tree[i + 1];
    tree[MAX_SEGS - 1] = gen_seg();

    player_side = 1 - player_side;
    chop_score++;
    chop_anim = 3;

    timer_val += get_refill();
    if (timer_val > TIMER_MAX) timer_val = TIMER_MAX;

    // chop particles
    spawn_particles_ex(trunk_x + 2, chop_y, 6, 1.0f, C_YELLOW);

    // death check (should never trigger with safe generation, but just in case)
    if ((tree[0].branch == BR_LEFT  && player_side == 0) ||
        (tree[0].branch == BR_RIGHT && player_side == 1)) {
        death_cause = 0;
        game_state = ST_OVER;
        if (chop_score > best_score) best_score = chop_score;
        spawn_particles_ex(trunk_x + 2, chop_y, 15, 2.0f, C_HIRED);
        spawn_flash(trunk_x + 2, chop_y);
    }
}

// ── drawing ───────────────────────────────────────────────────────

static void draw_tree(void) {
    for (int i = 0; i < visible && i < MAX_SEGS; i++) {
        int y = chop_y - i;
        if (y < 0) break;

        // trunk
        grid_put(trunk_x,     y, '|', C_YELLOW);
        grid_put(trunk_x + 1, y, '#', C_YELLOW);
        grid_put(trunk_x + 2, y, '#', C_YELLOW);
        grid_put(trunk_x + 3, y, '|', C_YELLOW);

        // branches
        if (tree[i].branch == BR_LEFT) {
            grid_put(trunk_x - 1, y, '{', C_GREEN);
            grid_put(trunk_x - 2, y, '=', C_GREEN);
            grid_put(trunk_x - 3, y, '=', C_GREEN);
            grid_put(trunk_x - 4, y, '=', C_GREEN);
        } else if (tree[i].branch == BR_RIGHT) {
            grid_put(trunk_x + 4, y, '}', C_GREEN);
            grid_put(trunk_x + 5, y, '=', C_GREEN);
            grid_put(trunk_x + 6, y, '=', C_GREEN);
            grid_put(trunk_x + 7, y, '=', C_GREEN);
        }
    }

    // crown
    int cy = chop_y - visible;
    if (cy >= 0) {
        grid_put(trunk_x - 1, cy, '/', C_GREEN);
        grid_put(trunk_x,     cy, '#', C_GREEN);
        grid_put(trunk_x + 1, cy, '#', C_GREEN);
        grid_put(trunk_x + 2, cy, '#', C_GREEN);
        grid_put(trunk_x + 3, cy, '#', C_GREEN);
        grid_put(trunk_x + 4, cy, '\\', C_GREEN);
    }
    if (cy - 1 >= 0) {
        grid_put(trunk_x,     cy - 1, '/', C_GREEN);
        grid_put(trunk_x + 1, cy - 1, '#', C_GREEN);
        grid_put(trunk_x + 2, cy - 1, '#', C_GREEN);
        grid_put(trunk_x + 3, cy - 1, '\\', C_GREEN);
    }
}

static void draw_stump(void) {
    int y = chop_y + 1;
    grid_str(trunk_x - 3, y, "===|##|===", C_YELLOW);
}

static void draw_ground_line(void) {
    int y = chop_y + 2;
    for (int x = play_x; x < play_x + play_w; x++)
        grid_put(x, y, '-', C_GRAY);
}

static void draw_player(void) {
    int px;
    int color = (game_state == ST_OVER && death_cause == 0) ? C_HIRED : C_HICYAN;

    if (player_side == 0) {
        // left side
        px = trunk_x - 6;
        grid_put(px + 1, chop_y - 1, '@', color);
        grid_str(px,     chop_y,     "/|\\", color);
    } else {
        // right side
        px = trunk_x + 8;
        grid_put(px + 1, chop_y - 1, '@', color);
        grid_str(px,     chop_y,     "/|\\", color);
    }
}

static void draw_timer_bar(void) {
    int y = chop_y + 3;
    if (y >= play_h) return;

    int filled = timer_val * TIMER_BAR_W / TIMER_MAX;
    if (filled < 0) filled = 0;
    if (filled > TIMER_BAR_W) filled = TIMER_BAR_W;

    int pct = timer_val * 100 / TIMER_MAX;
    int color = pct > 60 ? C_HIGREEN : pct > 30 ? C_YELLOW : C_HIRED;

    int bx = term_w / 2 - TIMER_BAR_W / 2 - 1;
    grid_put(bx, y, '[', color);
    for (int i = 0; i < TIMER_BAR_W; i++)
        grid_put(bx + 1 + i, y, i < filled ? '#' : '-',
                 i < filled ? color : C_GRAY);
    grid_put(bx + TIMER_BAR_W + 1, y, ']', color);
}

static void draw_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf), " CHOPS: %-8d  BEST: %-8d",
             chop_score, best_score);
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

static void draw_borders(void) {
    if (play_w >= term_w) return;
    int lx = play_x - 1;
    int rx = play_x + play_w;
    for (int y = 0; y < play_h; y++) {
        if (lx >= 0)     grid_put(lx, y, '|', C_GRAY);
        if (rx < term_w) grid_put(rx, y, '|', C_GRAY);
    }
}

// ── reset ─────────────────────────────────────────────────────────

static void reset_game(void) {
    chop_score  = 0;
    timer_val   = TIMER_MAX;
    death_cause = -1;
    chop_anim   = 0;
    auto_cd     = 10;
    auto_count  = 0;
    memset(particles, 0, sizeof(particles));
    memset(flashes,   0, sizeof(flashes));
    init_tree();
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

            draw_center(cy - 9, "L U M B E R J A C K", C_YELLOW);
            draw_center(cy - 8, "-------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tap desk to chop", C_WHITE);

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
        // drain timer
        timer_val -= get_drain();
        if (timer_val <= 0) {
            timer_val = 0;
            death_cause = 1;
            game_state = ST_OVER;
            if (chop_score > best_score) best_score = chop_score;
            spawn_particles_ex(trunk_x + 2, chop_y, 10, 1.5f, C_HIRED);
        }

        // input
        if (game_state == ST_PLAY) {
            if (has_accel) {
                if (consume_impact())
                    do_chop();
            } else {
                // auto-play: chop at a decreasing interval
                if (--auto_cd <= 0) {
                    int spd;
                    if (auto_count > 80)
                        spd = 15;   // slow down to die from timer
                    else {
                        spd = 10 - auto_count / 5;
                        if (spd < 5) spd = 5;
                    }
                    auto_cd = spd;
                    do_chop();
                    auto_count++;
                }
            }
        }

        if (chop_anim > 0) chop_anim--;
        update_particles();
        update_stars();
        update_flashes();

        draw_stars();
        draw_borders();
        draw_tree();
        draw_stump();
        draw_ground_line();
        draw_player();
        draw_timer_bar();
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
        draw_tree();
        draw_stump();
        draw_ground_line();
        draw_player();
        draw_timer_bar();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            if (death_cause == 1)
                draw_center(cy - 1, "Time ran out!", C_HIRED);
            else
                draw_center(cy - 1, "Crushed by branch!", C_HIRED);

            char sb[48];
            snprintf(sb, sizeof(sb), "Chops: %d", chop_score);
            draw_center(cy + 1, sb, C_YELLOW);
            snprintf(sb, sizeof(sb), "Best:  %d", best_score);
            draw_center(cy + 2, sb, C_WHITE);

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
    fprintf(stderr, "Lumberjack — Starting...\n");

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

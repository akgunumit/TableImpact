// space_invaders.c — Space Invaders: classic alien shooter
// Tilt screen to move cannon left/right, tap desk to fire upward.
//
// Build: make space_invaders
// Run:   sudo ./space_invaders

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W      80

#define ALIEN_ROWS      5
#define ALIEN_COLS      8
#define ALIEN_W         5
#define ALIEN_H         2
#define ALIEN_GAP_X     2
#define ALIEN_GAP_Y     1

#define MAX_PBULLETS    8
#define MAX_ABULLETS    4

#define SHIELD_COUNT    4
#define SHIELD_W        7
#define SHIELD_H        4

#define INITIAL_LIVES   3
#define INVULN_FRAMES   45

#define LERP_FACTOR     0.25f

#define BASE_MOVE_CD    20
#define MIN_MOVE_CD     3
#define BASE_SHOOT_CD   40
#define MIN_SHOOT_CD    15

#define SHIP_W          7
#define SHIP_H          3

// ── types ─────────────────────────────────────────────────────────

typedef struct { int x, y, active; } PBullet;
typedef struct { int x, y, active; } ABullet;
typedef struct { char cells[SHIELD_H][SHIELD_W]; int x, y; } Shield;

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;

static int aliens[ALIEN_ROWS][ALIEN_COLS];
static int alien_x, alien_y;
static int alien_dir;
static int alien_move_timer, alien_anim;
static int aliens_alive;

static PBullet pbullets[MAX_PBULLETS];
static ABullet abullets[MAX_ABULLETS];

static Shield shields[SHIELD_COUNT];

static int score, lives, wave, invuln_timer;
static int ship_col, ship_row;
static float ship_col_f;

static int alien_shoot_timer;

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
}

// ── alien type ────────────────────────────────────────────────────

static int alien_type(int row) {
    if (row == 0)           return 0;   // squid
    if (row <= 2)           return 1;   // crab
    return 2;                           // jelly
}

// ── aliens ────────────────────────────────────────────────────────

static void init_aliens(void) {
    aliens_alive = 0;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++) {
            aliens[r][c] = 1;
            aliens_alive++;
        }

    int block_w = ALIEN_COLS * (ALIEN_W + ALIEN_GAP_X) - ALIEN_GAP_X;
    alien_x = play_x + (play_w - block_w) / 2;
    alien_y = 2 + (wave < 4 ? wave - 1 : 3);
    alien_dir = 1;
    alien_move_timer = 0;
    alien_anim = 0;
    alien_shoot_timer = 0;
}

static void update_aliens(void) {
    int total = ALIEN_ROWS * ALIEN_COLS;
    int dead = total - aliens_alive;
    int interval = BASE_MOVE_CD - dead * (BASE_MOVE_CD - MIN_MOVE_CD) / total;
    if (interval < MIN_MOVE_CD) interval = MIN_MOVE_CD;

    if (++alien_move_timer < interval) return;
    alien_move_timer = 0;
    alien_anim ^= 1;

    // find leftmost and rightmost alive columns
    int left_col = ALIEN_COLS, right_col = -1;
    for (int c = 0; c < ALIEN_COLS; c++)
        for (int r = 0; r < ALIEN_ROWS; r++)
            if (aliens[r][c]) {
                if (c < left_col) left_col = c;
                if (c > right_col) right_col = c;
                break;
            }
    if (right_col < 0) return;

    int left_px = alien_x + left_col * (ALIEN_W + ALIEN_GAP_X);
    int right_px = alien_x + right_col * (ALIEN_W + ALIEN_GAP_X) + ALIEN_W - 1;

    if ((alien_dir > 0 && right_px + 1 >= play_x + play_w - 1) ||
        (alien_dir < 0 && left_px - 1 <= play_x)) {
        alien_dir = -alien_dir;
        alien_y++;

        // check if aliens reached player
        int bot_row = -1;
        for (int r = ALIEN_ROWS - 1; r >= 0; r--)
            for (int c = 0; c < ALIEN_COLS; c++)
                if (aliens[r][c]) { bot_row = r; goto found_bot; }
        found_bot:
        if (bot_row >= 0) {
            int bot_y = alien_y + bot_row * (ALIEN_H + ALIEN_GAP_Y)
                        + ALIEN_H - 1;
            if (bot_y >= ship_row - (SHIP_H - 1)) {
                game_state = ST_OVER;
                spawn_particles(ship_col + SHIP_W / 2, ship_row, 15);
            }
        }
    } else {
        alien_x += alien_dir;
    }
}

static void draw_aliens(void) {
    // sprites[type][anim_frame][row]
    static const char *sprites[3][2][ALIEN_H] = {
        // squid
        {{"/^^^\\", "|/ \\|"}, {"\\^^^/", "|\\ /|"}},
        // crab
        {{"{o-o}", "/| |\\"}, {"[o-o]", "\\| |/"}},
        // jelly
        {{"^o-o^", "/   \\"}, {"~o-o~", "\\   /"}},
    };
    static const int colors[3] = { C_HIRED, C_MAGENTA, C_GREEN };

    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!aliens[r][c]) continue;
            int ax = alien_x + c * (ALIEN_W + ALIEN_GAP_X);
            int ay = alien_y + r * (ALIEN_H + ALIEN_GAP_Y);
            int t = alien_type(r);
            for (int row = 0; row < ALIEN_H; row++)
                grid_str(ax, ay + row, sprites[t][alien_anim][row],
                         colors[t]);
        }
}

// ── alien shooting ────────────────────────────────────────────────

static void alien_shoot(void) {
    int cd = BASE_SHOOT_CD - (wave - 1) * 3;
    if (cd < MIN_SHOOT_CD) cd = MIN_SHOOT_CD;

    if (++alien_shoot_timer < cd) return;
    alien_shoot_timer = 0;

    // collect columns with alive aliens
    int cols[ALIEN_COLS], ncols = 0;
    for (int c = 0; c < ALIEN_COLS; c++)
        for (int r = ALIEN_ROWS - 1; r >= 0; r--)
            if (aliens[r][c]) { cols[ncols++] = c; break; }

    if (ncols == 0) return;

    int pick = cols[rand() % ncols];
    // find bottom-most alive in this column
    for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
        if (!aliens[r][pick]) continue;
        int ax = alien_x + pick * (ALIEN_W + ALIEN_GAP_X) + ALIEN_W / 2;
        int ay = alien_y + r * (ALIEN_H + ALIEN_GAP_Y) + ALIEN_H;
        for (int i = 0; i < MAX_ABULLETS; i++) {
            if (!abullets[i].active) {
                abullets[i] = (ABullet){ .x = ax, .y = ay, .active = 1 };
                break;
            }
        }
        break;
    }
}

// ── alien bullets ─────────────────────────────────────────────────

static void update_abullets(void) {
    for (int i = 0; i < MAX_ABULLETS; i++) {
        if (!abullets[i].active) continue;
        if (frame_num % 2 == 0) abullets[i].y++;
        if (abullets[i].y >= term_h) abullets[i].active = 0;
    }
}

static void draw_abullets(void) {
    for (int i = 0; i < MAX_ABULLETS; i++) {
        if (!abullets[i].active) continue;
        grid_put(abullets[i].x, abullets[i].y, '!', C_HIRED);
    }
}

// ── player ship ───────────────────────────────────────────────────
//    ^          row ship_row-2
//  /===\        row ship_row-1
// |=====|       row ship_row

static void draw_ship(void) {
    if (invuln_timer > 0 && (frame_num % 4 < 2)) return;
    grid_put(ship_col + 3, ship_row - 2, '^', C_HICYAN);
    grid_str(ship_col + 1, ship_row - 1, "/===\\", C_CYAN);
    grid_str(ship_col, ship_row, "|=====|", C_HICYAN);
}

// ── player bullets ────────────────────────────────────────────────

static void spawn_pbullet(void) {
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!pbullets[i].active) {
            pbullets[i] = (PBullet){
                .x = ship_col + SHIP_W / 2,
                .y = ship_row - SHIP_H,
                .active = 1
            };
            return;
        }
    }
}

static void update_pbullets(void) {
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!pbullets[i].active) continue;
        pbullets[i].y--;
        if (pbullets[i].y < 0) pbullets[i].active = 0;
    }
}

static void draw_pbullets(void) {
    for (int i = 0; i < MAX_PBULLETS; i++) {
        if (!pbullets[i].active) continue;
        grid_put(pbullets[i].x, pbullets[i].y, '|', C_YELLOW);
    }
}

// ── shields ───────────────────────────────────────────────────────

static void init_shields(void) {
    //  /###\       (backslashes display as single char)
    //  /#####\
    //  #######
    //  ### ###
    static const char pattern[SHIELD_H][SHIELD_W] = {
        {' ', '/', '#', '#', '#', '\\', ' '},
        {'/', '#', '#', '#', '#', '#', '\\'},
        {'#', '#', '#', '#', '#', '#', '#'},
        {'#', '#', '#', ' ', '#', '#', '#'},
    };

    int shield_y = ship_row - SHIP_H - SHIELD_H;
    int total_sw = SHIELD_COUNT * SHIELD_W;
    int spacing = (play_w - total_sw) / (SHIELD_COUNT + 1);
    if (spacing < 2) spacing = 2;

    for (int s = 0; s < SHIELD_COUNT; s++) {
        shields[s].x = play_x + spacing + s * (SHIELD_W + spacing);
        shields[s].y = shield_y;
        memcpy(shields[s].cells, pattern, sizeof(pattern));
    }
}

static void draw_shields(void) {
    for (int s = 0; s < SHIELD_COUNT; s++)
        for (int r = 0; r < SHIELD_H; r++)
            for (int c = 0; c < SHIELD_W; c++)
                if (shields[s].cells[r][c] != ' ')
                    grid_put(shields[s].x + c, shields[s].y + r,
                             shields[s].cells[r][c], C_GREEN);
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

// ── collision ─────────────────────────────────────────────────────

static void check_collisions(void) {
    // 1. player bullet vs alien
    for (int b = 0; b < MAX_PBULLETS; b++) {
        if (!pbullets[b].active) continue;
        for (int r = 0; r < ALIEN_ROWS; r++)
            for (int c = 0; c < ALIEN_COLS; c++) {
                if (!aliens[r][c]) continue;
                int ax = alien_x + c * (ALIEN_W + ALIEN_GAP_X);
                int ay = alien_y + r * (ALIEN_H + ALIEN_GAP_Y);
                if (pbullets[b].x >= ax && pbullets[b].x < ax + ALIEN_W &&
                    pbullets[b].y >= ay && pbullets[b].y < ay + ALIEN_H) {
                    pbullets[b].active = 0;
                    aliens[r][c] = 0;
                    aliens_alive--;
                    int pts = (alien_type(r) == 0) ? 30 :
                              (alien_type(r) == 1) ? 20 : 10;
                    score += pts;
                    spawn_particles_ex(ax + ALIEN_W / 2, ay, 6, 1.0f, 0);
                    spawn_flash(ax + ALIEN_W / 2, ay);
                    goto next_pbullet;
                }
            }

        // 2. player bullet vs shield
        for (int s = 0; s < SHIELD_COUNT; s++)
            for (int r = 0; r < SHIELD_H; r++)
                for (int c = 0; c < SHIELD_W; c++) {
                    if (shields[s].cells[r][c] == ' ') continue;
                    if (pbullets[b].x == shields[s].x + c &&
                        pbullets[b].y == shields[s].y + r) {
                        shields[s].cells[r][c] = ' ';
                        pbullets[b].active = 0;
                        goto next_pbullet;
                    }
                }
        next_pbullet:;
    }

    // 3. alien bullet vs shield / player
    for (int i = 0; i < MAX_ABULLETS; i++) {
        if (!abullets[i].active) continue;

        // vs shield (4)
        for (int s = 0; s < SHIELD_COUNT; s++)
            for (int r = 0; r < SHIELD_H; r++)
                for (int c = 0; c < SHIELD_W; c++) {
                    if (shields[s].cells[r][c] == ' ') continue;
                    if (abullets[i].x == shields[s].x + c &&
                        abullets[i].y == shields[s].y + r) {
                        shields[s].cells[r][c] = ' ';
                        abullets[i].active = 0;
                        goto next_abullet;
                    }
                }

        // vs player (3)
        if (invuln_timer <= 0 &&
            abullets[i].x >= ship_col &&
            abullets[i].x < ship_col + SHIP_W &&
            abullets[i].y >= ship_row - (SHIP_H - 1) &&
            abullets[i].y <= ship_row) {
            abullets[i].active = 0;
            lives--;
            invuln_timer = INVULN_FRAMES;
            spawn_particles(ship_col + SHIP_W / 2, ship_row, 10);
            if (lives <= 0) game_state = ST_OVER;
        }
        next_abullet:;
    }
}

// ── wave ──────────────────────────────────────────────────────────

static void check_wave_clear(void) {
    if (aliens_alive > 0) return;
    wave++;
    init_aliens();
    init_shields();
}

// ── input ─────────────────────────────────────────────────────────

static void process_input(void) {
    poll_lid();
    if (!g_lid.valid) return;
    float lid = (float)g_lid.angle;
    float t = 1.0f - (lid - LID_MIN) / (LID_MAX - LID_MIN);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    // t=0 when high tilt (100°) → left side
    // t=1 when low tilt (60°)  → right side
    float target = (float)play_x + t * (float)(play_w - SHIP_W);
    ship_col_f += (target - ship_col_f) * LERP_FACTOR;
    ship_col = (int)(ship_col_f + 0.5f);
    if (ship_col < play_x)                    ship_col = play_x;
    if (ship_col > play_x + play_w - SHIP_W)  ship_col = play_x + play_w - SHIP_W;
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_hud(void) {
    int y = term_h - 1;
    char buf[128];
    snprintf(buf, sizeof(buf),
             " SCORE: %-8d  LIVES: %-4d  WAVE: %d",
             score, lives, wave);
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
    wave = 1;
    invuln_timer = 0;
    alien_shoot_timer = 0;
    ship_row = play_h - 2;
    ship_col = play_x + play_w / 2 - SHIP_W / 2;
    ship_col_f = (float)ship_col;
    memset(pbullets,  0, sizeof(pbullets));
    memset(abullets,  0, sizeof(abullets));
    memset(particles, 0, sizeof(particles));
    memset(flashes,   0, sizeof(flashes));
    init_aliens();
    init_shields();
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

            // 3D rotating UMIT
            float lid_angle = g_lid.valid ? (float)g_lid.angle : 105.0f;
            if (!umit_base_set) { umit_base_angle = lid_angle; umit_base_set = 1; }
            float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
            if (rot > PIf / 3.0f) rot = PIf / 3.0f;
            if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
            float cos_r = cosf(rot), sin_r = sinf(rot);

            draw_center(cy - 9, "S P A C E   I N V A D E R S", C_HICYAN);
            draw_center(cy - 8, "----------------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Tilt screen to move cannon", C_WHITE);
            draw_center(cy + 8, "Tap desk to fire", C_WHITE);

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
            if (consume_impact())
                spawn_pbullet();
        } else {
            // auto: aim at nearest alien column, fire periodically
            if (frame_num % 12 == 0)
                spawn_pbullet();
            // auto movement: track nearest alien column
            {
                int best_c = -1, best_dist = 9999;
                for (int c = 0; c < ALIEN_COLS; c++)
                    for (int r = ALIEN_ROWS - 1; r >= 0; r--)
                        if (aliens[r][c]) {
                            int ax = alien_x + c * (ALIEN_W + ALIEN_GAP_X)
                                     + ALIEN_W / 2;
                            int dist = abs(ax - (ship_col + SHIP_W / 2));
                            if (dist < best_dist) {
                                best_dist = dist;
                                best_c = ax;
                            }
                            break;
                        }
                if (best_c >= 0) {
                    float target = (float)(best_c - SHIP_W / 2);
                    ship_col_f += (target - ship_col_f) * 0.1f;
                    ship_col = (int)(ship_col_f + 0.5f);
                    if (ship_col < play_x)
                        ship_col = play_x;
                    if (ship_col > play_x + play_w - SHIP_W)
                        ship_col = play_x + play_w - SHIP_W;
                }
            }
        }

        update_aliens();
        alien_shoot();
        update_abullets();
        update_pbullets();
        update_particles();
        update_stars();
        update_flashes();
        if (invuln_timer > 0) invuln_timer--;

        check_collisions();
        check_wave_clear();

        draw_stars();
        draw_borders();
        draw_shields();
        draw_aliens();
        draw_abullets();
        draw_pbullets();
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
        draw_borders();
        draw_particles();

        {
            int cy = play_h / 2;
            draw_center(cy - 3, "G A M E   O V E R", C_RED);
            draw_center(cy - 2, "------------------", C_GRAY);

            char sb[48];
            snprintf(sb, sizeof(sb), "Score: %d", score);
            draw_center(cy, sb, C_YELLOW);

            snprintf(sb, sizeof(sb), "Wave:  %d", wave);
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
    fprintf(stderr, "Space Invaders — Starting...\n");

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
    ship_row = play_h - 2;
    ship_col = play_x + play_w / 2 - SHIP_W / 2;
    ship_col_f = (float)ship_col;

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

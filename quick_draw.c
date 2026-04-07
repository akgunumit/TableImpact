// quick_draw.c — Quick Draw: Western reaction duel tournament
// Wait for "DRAW!" then tap as fast as possible. Beat 8 opponents.
//
// Build: make quick_draw
// Run:   sudo ./quick_draw

#include "game_engine.h"

// ── constants ─────────────────────────────────────────────────────

#define MAX_PLAY_W       80
#define NUM_OPPONENTS    8

// duel sub-states (within ST_PLAY)
#define DUEL_INTRO       0
#define DUEL_WAIT        1
#define DUEL_DRAW        2
#define DUEL_RESULT      3
#define DUEL_BRACKET     4

// timing
#define WAIT_MIN         1.5
#define WAIT_MAX         5.0
#define INTRO_FRAMES     (GAME_FPS * 2)
#define RESULT_FRAMES    (GAME_FPS * 3)
#define BRACKET_FRAMES   (GAME_FPS * 3)
#define DRAW_TIMEOUT     2.0

// ── types ─────────────────────────────────────────────────────────

typedef struct {
    const char *name;
    int react_ms;
} Opponent;

typedef struct {
    int won;            // 1=player won, 0=lost, -1=not played
    int player_ms;
    int opponent_ms;
} RoundResult;

// ── globals ───────────────────────────────────────────────────────

static int play_w, play_x;
static int ground_y;

static const Opponent opponents[NUM_OPPONENTS] = {
    { "Slowpoke Sam",     900 },
    { "Dusty Dan",        700 },
    { "Barrel Bob",       550 },
    { "Cactus Kate",      420 },
    { "Rattlesnake Roy",  330 },
    { "Iron Jane",        270 },
    { "The Kid",          220 },
    { "El Diablo",        180 },
};

// tournament
static int current_opp;
static RoundResult results[NUM_OPPONENTS];
static int tournament_won;

// duel
static int duel_phase;
static int phase_timer;
static double wait_duration;
static double draw_start;
static int player_reacted;
static int player_ms;
static int opp_ms;           // opponent's actual react (with jitter)
static int duel_winner;      // 0=player, 1=opponent
static int too_early;
static int player_drawn;     // gun drawn animation
static int opp_drawn;
static int player_fallen;
static int opp_fallen;

// stats
static int best_react_ms;
static int rounds_won;

// visuals
static int tumble_x;

// auto-play
static int auto_react_frames;

// ── play area ─────────────────────────────────────────────────────

static void update_play_area(void) {
    play_w = term_w < MAX_PLAY_W ? term_w : MAX_PLAY_W;
    play_x = (term_w - play_w) / 2;
    ground_y = play_h - 6;
}

// ── duel phase transitions ────────────────────────────────────────

static void set_phase(int phase) {
    duel_phase = phase;
    phase_timer = 0;

    switch (phase) {
    case DUEL_INTRO:
        player_drawn = 0;
        opp_drawn = 0;
        player_fallen = 0;
        opp_fallen = 0;
        too_early = 0;
        player_reacted = 0;
        break;

    case DUEL_WAIT:
        wait_duration = WAIT_MIN +
            (rand() % 1000) / 1000.0 * (WAIT_MAX - WAIT_MIN);
        break;

    case DUEL_DRAW:
        draw_start = now_sec();
        opp_ms = opponents[current_opp].react_ms + (rand() % 61) - 30;
        if (opp_ms < 50) opp_ms = 50;
        // auto-play react time: 200-500ms
        auto_react_frames = GAME_FPS * (200 + rand() % 300) / 1000;
        if (auto_react_frames < 4) auto_react_frames = 4;
        break;

    case DUEL_RESULT:
        if (duel_winner == 0) {
            opp_fallen = 1;
            int fx = play_x + play_w * 3 / 4;
            spawn_particles_ex(fx, ground_y - 2, 12, 1.5f, C_YELLOW);
            spawn_flash(fx, ground_y - 2);
        } else {
            player_fallen = 1;
            int fx = play_x + play_w / 4;
            spawn_particles_ex(fx, ground_y - 2, 12, 1.5f, C_HIRED);
            spawn_flash(fx, ground_y - 2);
        }
        break;

    case DUEL_BRACKET:
        break;
    }
}

// ── cowboy drawing ────────────────────────────────────────────────

static void draw_cowboy_left(int x, int y, int drawn, int fallen,
                             int color) {
    if (fallen) {
        grid_str(x,     y + 1, "_@/", color);
        grid_str(x - 1, y + 2, "__|__", color);
        return;
    }
    // hat
    grid_put(x + 1, y - 1, '~', color);
    // head
    grid_put(x + 1, y, '@', color);
    // body
    if (drawn) {
        grid_str(x, y + 1, "/|---=", color);
    } else {
        grid_str(x, y + 1, "/|\\", color);
    }
    // legs
    grid_str(x, y + 2, "/ \\", color);
}

static void draw_cowboy_right(int x, int y, int drawn, int fallen,
                              int color) {
    if (fallen) {
        grid_str(x,     y + 1, "\\@_", color);
        grid_str(x - 1, y + 2, "__|__", color);
        return;
    }
    // hat
    grid_put(x + 1, y - 1, '~', color);
    // head
    grid_put(x + 1, y, '@', color);
    // body
    if (drawn) {
        grid_str(x - 3, y + 1, "=---|\\", color);
    } else {
        grid_str(x, y + 1, "/|\\", color);
    }
    // legs
    grid_str(x, y + 2, "/ \\", color);
}

// ── scene drawing ─────────────────────────────────────────────────

static void draw_ground_line(void) {
    for (int x = play_x; x < play_x + play_w; x++)
        grid_put(x, ground_y + 3, '-', C_YELLOW);
    // sand texture
    static const char tex[] = "~.~_.~._";
    int tl = (int)(sizeof(tex) - 1);
    for (int x = play_x; x < play_x + play_w; x++) {
        int idx = ((x - play_x) + frame_num / 3) % tl;
        grid_put(x, ground_y + 4, tex[idx], C_GRAY);
    }
}

static void draw_tumbleweed(void) {
    tumble_x--;
    if (tumble_x < play_x - 3) tumble_x = play_x + play_w;
    grid_str(tumble_x, ground_y + 2, "(@)", C_YELLOW);
}

static void draw_scene(void) {
    draw_ground_line();
    draw_tumbleweed();

    // opponent color cycles
    static const int opp_colors[] = {
        C_RED, C_YELLOW, C_MAGENTA, C_CYAN,
        C_GREEN, C_HIRED, C_WHITE, C_HIRED
    };

    int lx = play_x + play_w / 4;
    int rx = play_x + play_w * 3 / 4;

    draw_cowboy_left(lx, ground_y, player_drawn, player_fallen,
                     C_HICYAN);
    draw_cowboy_right(rx, ground_y, opp_drawn, opp_fallen,
                      opp_colors[current_opp % 8]);
}

// ── bracket drawing ───────────────────────────────────────────────

static void draw_bracket(void) {
    int x0 = term_w / 2 - 25;
    if (x0 < 2) x0 = 2;
    int y0 = 2;

    grid_str(x0, y0, "TOURNAMENT PROGRESS", C_HICYAN);
    grid_str(x0, y0 + 1, "===================", C_GRAY);

    for (int i = 0; i < NUM_OPPONENTS; i++) {
        int y = y0 + 3 + i * 2;
        if (y >= play_h - 1) break;

        char line[80];
        if (results[i].won == 1) {
            snprintf(line, sizeof(line), "%d. %-18s (%dms)  WIN - %dms",
                     i + 1, opponents[i].name, opponents[i].react_ms,
                     results[i].player_ms);
            grid_str(x0, y, line, C_HIGREEN);
        } else if (results[i].won == 0) {
            snprintf(line, sizeof(line), "%d. %-18s (%dms)  LOST",
                     i + 1, opponents[i].name, opponents[i].react_ms);
            grid_str(x0, y, line, C_HIRED);
        } else if (i == current_opp) {
            snprintf(line, sizeof(line), "%d. %-18s (%dms)  >>> NEXT <<<",
                     i + 1, opponents[i].name, opponents[i].react_ms);
            grid_str(x0, y, line, C_YELLOW);
        } else {
            snprintf(line, sizeof(line), "%d. %-18s (%dms)  [ --- ]",
                     i + 1, opponents[i].name, opponents[i].react_ms);
            grid_str(x0, y, line, C_GRAY);
        }
    }
}

// ── HUD ───────────────────────────────────────────────────────────

static void draw_hud(void) {
    int y = term_h - 1;
    char buf[128];
    if (best_react_ms > 0)
        snprintf(buf, sizeof(buf),
                 " ROUND: %d/%d  BEST: %dms",
                 current_opp + 1, NUM_OPPONENTS, best_react_ms);
    else
        snprintf(buf, sizeof(buf),
                 " ROUND: %d/%d",
                 current_opp + 1, NUM_OPPONENTS);
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

static void reset_tournament(void) {
    current_opp = 0;
    tournament_won = 0;
    rounds_won = 0;
    for (int i = 0; i < NUM_OPPONENTS; i++)
        results[i].won = -1;
    memset(particles, 0, sizeof(particles));
    memset(flashes,   0, sizeof(flashes));
    tumble_x = play_x + play_w;
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

            draw_center(cy - 9, "Q U I C K   D R A W", C_HIRED);
            draw_center(cy - 8, "-------------------", C_GRAY);

            draw_center(cy - 5, "By", C_WHITE);
            draw_3d_umit(term_w / 2, cy - 3, cos_r, sin_r);
            draw_center(cy + 5, "umit.cc", C_GRAY);

            draw_center(cy + 7, "Wait... then DRAW! Tap to shoot", C_WHITE);

            if (has_accel) {
                if (frame_num % 20 < 14)
                    draw_center(cy + 9,
                        "[ TAP DESK TO START ]", C_YELLOW);
                if (consume_impact()) {
                    game_state = ST_PLAY;
                    reset_tournament();
                    set_phase(DUEL_INTRO);
                }
            } else {
                draw_center(cy + 9,
                    "(no accelerometer - auto mode)", C_YELLOW);
                if (++auto_timer > GAME_FPS * 3) {
                    auto_timer = 0;
                    game_state = ST_PLAY;
                    reset_tournament();
                    set_phase(DUEL_INTRO);
                }
            }
        }
        break;

    // ── playing ───────────────────────────────────────────────────
    case ST_PLAY:
        phase_timer++;
        update_particles();
        update_flashes();
        update_stars();

        switch (duel_phase) {

        case DUEL_INTRO:
            draw_stars();
            draw_borders();
            draw_scene();
            {
                int cy = play_h / 3;
                char buf[64];
                snprintf(buf, sizeof(buf), "Round %d / %d",
                         current_opp + 1, NUM_OPPONENTS);
                draw_center(cy - 2, buf, C_WHITE);

                draw_center(cy, opponents[current_opp].name, C_HIRED);

                snprintf(buf, sizeof(buf), "Reaction: %dms",
                         opponents[current_opp].react_ms);
                draw_center(cy + 2, buf, C_YELLOW);
            }
            if (phase_timer >= INTRO_FRAMES)
                set_phase(DUEL_WAIT);
            break;

        case DUEL_WAIT:
            draw_stars();
            draw_borders();
            draw_scene();
            {
                int cy = play_h / 3;
                // animate dots
                int dots = (phase_timer / (GAME_FPS / 2)) % 4;
                char txt[32] = "W A I T";
                for (int d = 0; d < dots; d++) strcat(txt, " .");
                draw_center(cy, txt, C_YELLOW);
            }
            // premature tap = disqualified
            if (has_accel) {
                if (consume_impact()) {
                    too_early = 1;
                    duel_winner = 1;
                    results[current_opp] = (RoundResult){
                        .won = 0, .player_ms = -1, .opponent_ms = 0
                    };
                    set_phase(DUEL_RESULT);
                }
            }
            // check if wait time elapsed
            {
                double elapsed = (double)phase_timer / GAME_FPS;
                if (elapsed >= wait_duration)
                    set_phase(DUEL_DRAW);
            }
            break;

        case DUEL_DRAW:
            draw_stars();
            draw_borders();
            draw_scene();
            {
                int cy = play_h / 3;
                // flashing DRAW!
                int flash = (phase_timer < 6) ?
                    ((phase_timer % 2) ? C_WHITE : C_HIRED) : C_HIRED;
                draw_center(cy, "D R A W !", flash);

                double elapsed_sec = now_sec() - draw_start;
                int elapsed_ms = (int)(elapsed_sec * 1000.0);

                // opponent draws when their time is up
                if (elapsed_ms >= opp_ms && !opp_drawn)
                    opp_drawn = 1;

                // player input
                if (has_accel) {
                    if (consume_impact() && !player_reacted) {
                        player_reacted = 1;
                        player_ms = elapsed_ms;
                        player_drawn = 1;

                        duel_winner = (player_ms <= opp_ms) ? 0 : 1;
                        results[current_opp] = (RoundResult){
                            .won = (duel_winner == 0) ? 1 : 0,
                            .player_ms = player_ms,
                            .opponent_ms = opp_ms
                        };
                        if (duel_winner == 0) rounds_won++;
                        if (best_react_ms == 0 ||
                            player_ms < best_react_ms)
                            best_react_ms = player_ms;
                        set_phase(DUEL_RESULT);
                    }
                } else {
                    // auto-play
                    if (phase_timer >= auto_react_frames &&
                        !player_reacted) {
                        player_reacted = 1;
                        player_ms = (int)((double)phase_timer /
                                          GAME_FPS * 1000.0);
                        player_drawn = 1;
                        duel_winner = (player_ms <= opp_ms) ? 0 : 1;
                        results[current_opp] = (RoundResult){
                            .won = (duel_winner == 0) ? 1 : 0,
                            .player_ms = player_ms,
                            .opponent_ms = opp_ms
                        };
                        if (duel_winner == 0) rounds_won++;
                        set_phase(DUEL_RESULT);
                    }
                }

                // timeout
                if (!player_reacted && elapsed_sec >= DRAW_TIMEOUT) {
                    player_ms = (int)(DRAW_TIMEOUT * 1000);
                    duel_winner = 1;
                    results[current_opp] = (RoundResult){
                        .won = 0,
                        .player_ms = player_ms,
                        .opponent_ms = opp_ms
                    };
                    set_phase(DUEL_RESULT);
                }
            }
            break;

        case DUEL_RESULT:
            draw_stars();
            draw_borders();
            draw_scene();
            {
                int cy = play_h / 3;

                if (too_early) {
                    draw_center(cy - 1, "T O O   E A R L Y !", C_HIRED);
                    draw_center(cy + 1, "Disqualified!", C_RED);
                } else if (duel_winner == 0) {
                    draw_center(cy - 1, "Y O U   W I N !", C_HIGREEN);
                    char buf[48];
                    snprintf(buf, sizeof(buf), "Your time: %dms", player_ms);
                    draw_center(cy + 1, buf, C_HICYAN);
                    snprintf(buf, sizeof(buf), "%s: %dms",
                             opponents[current_opp].name, opp_ms);
                    draw_center(cy + 2, buf, C_GRAY);
                } else {
                    draw_center(cy - 1, "Y O U   L O S E !", C_HIRED);
                    char buf[48];
                    if (player_ms >= 0) {
                        snprintf(buf, sizeof(buf), "Your time: %dms",
                                 player_ms);
                        draw_center(cy + 1, buf, C_RED);
                    } else {
                        draw_center(cy + 1, "Too slow!", C_RED);
                    }
                    snprintf(buf, sizeof(buf), "%s: %dms",
                             opponents[current_opp].name, opp_ms);
                    draw_center(cy + 2, buf, C_GRAY);
                }
            }
            if (phase_timer >= RESULT_FRAMES) {
                if (duel_winner == 1) {
                    // player lost — game over
                    game_state = ST_OVER;
                } else {
                    current_opp++;
                    if (current_opp >= NUM_OPPONENTS) {
                        tournament_won = 1;
                        game_state = ST_OVER;
                    } else {
                        set_phase(DUEL_BRACKET);
                    }
                }
            }
            break;

        case DUEL_BRACKET:
            draw_stars();
            draw_borders();
            draw_bracket();
            if (phase_timer >= BRACKET_FRAMES)
                set_phase(DUEL_INTRO);
            break;
        }

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

        {
            int cy = play_h / 2;

            if (tournament_won) {
                draw_center(cy - 4, "C H A M P I O N !", C_HIGREEN);
                draw_center(cy - 3, "-----------------", C_GRAY);
                draw_center(cy - 1, "All 8 opponents defeated!", C_WHITE);
            } else {
                draw_center(cy - 4, "G A M E   O V E R", C_RED);
                draw_center(cy - 3, "------------------", C_GRAY);

                char sb[48];
                snprintf(sb, sizeof(sb), "Defeated by: %s",
                         opponents[current_opp].name);
                draw_center(cy - 1, sb, C_WHITE);
            }

            char sb[48];
            snprintf(sb, sizeof(sb), "Rounds won: %d / %d",
                     rounds_won, NUM_OPPONENTS);
            draw_center(cy + 1, sb, C_YELLOW);

            if (best_react_ms > 0) {
                snprintf(sb, sizeof(sb), "Best reaction: %dms",
                         best_react_ms);
                draw_center(cy + 2, sb, C_HICYAN);
            }

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
        draw_particles();
        draw_hud();
        break;
    }

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "Quick Draw — Starting...\n");

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

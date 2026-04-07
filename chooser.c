// chooser.c — Game launcher menu for TableImpact
// Tilt screen to select a game, tap desk to launch.
//
// Build: make chooser
// Run:   sudo ./chooser

#include "game_engine.h"
#include <sys/wait.h>

// ── game table ───────────────────────────────────────────────────

typedef struct {
    const char *name;
    const char *binary;
    const char *desc;
} GameEntry;

static const GameEntry games[8] = {
    { "S P A C E   I M P A C T",     "./space_impact",    "Side-scrolling shooter" },
    { "F L A P P Y   B I R D",       "./flappy_bird",     "Tap to flap" },
    { "S P A C E   I N V A D E R S", "./space_invaders",  "Classic alien shooter" },
    { "D I N O   R U N N E R",       "./dino",            "Endless runner" },
    { "A R K A N O I D",             "./arkanoid",        "Brick breaker" },
    { "S T A C K   T O W E R",       "./stack_tower",     "Tap to stack blocks" },
    { "L U M B E R J A C K",         "./lumberjack",      "Chop fast, dodge branches" },
    { "Q U I C K   D R A W",         "./quick_draw",      "Western reaction duel" },
};

#define NUM_GAMES 8
#define SHIMMER_PERIOD 90

static void game_tick(CFRunLoopTimerRef timer, void *info);

// ── globals ───────────────────────────────────────────────────────

static int selected = 0;
static float selected_f = 0.0f;
static CFRunLoopTimerRef g_timer = NULL;

static int auto_select_timer = 0;
static int auto_launch_timer = 0;
static int last_auto_selected = -1;

// ── shimmer drawing ──────────────────────────────────────────────

static void draw_shimmer(int y, const char *text, int cx) {
    int len = (int)strlen(text);
    int x0 = cx - len / 2;

    // shimmer position sweeps across text width
    float phase = (float)(frame_num % SHIMMER_PERIOD) / (float)SHIMMER_PERIOD;
    float highlight = -2.0f + phase * (float)(len + 4);

    for (int i = 0; i < len; i++) {
        if (text[i] == ' ') {
            grid_put(x0 + i, y, ' ', C_NONE);
            continue;
        }
        float dist = fabsf((float)i - highlight);
        int color;
        if (dist < 1.5f)       color = C_HICYAN;
        else if (dist < 3.0f)  color = C_WHITE;
        else if (dist < 5.0f)  color = C_CYAN;
        else                    color = C_GRAY;
        grid_put(x0 + i, y, text[i], color);
    }
}

// ── launch game ──────────────────────────────────────────────────

static void launch_game(int idx) {
    // stop timer
    if (g_timer) {
        CFRunLoopTimerInvalidate(g_timer);
        CFRelease(g_timer);
        g_timer = NULL;
    }

    // close sensors
    close_accel();
    close_lid();

    // reset terminal
    cleanup();

    pid_t pid = fork();
    if (pid == 0) {
        // child
        setenv("TABLEIMPACT_LAUNCHER", "1", 1);
        execl(games[idx].binary, games[idx].binary, (char *)NULL);
        _exit(127);
    }

    // parent: wait for child
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }

    // re-setup
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    write(STDOUT_FILENO, "\033[?25l", 6);

    setup_lid();
    has_accel = setup_accel();
    freopen("/dev/null", "w", stderr);

    // clear stale impact + reset UMIT baseline for new starting angle
    consume_impact();
    umit_base_set = 0;

    // reset auto timers
    auto_select_timer = 0;
    auto_launch_timer = 0;
    last_auto_selected = -1;

    // restart timer
    g_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.05,
        1.0 / GAME_FPS,
        0, 0, game_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), g_timer, kCFRunLoopDefaultMode);
}

// ── game tick ────────────────────────────────────────────────────

static void game_tick(CFRunLoopTimerRef timer, void *info) {
    (void)timer; (void)info;

    get_term_size();
    grid_alloc();
    grid_clear();
    frame_num++;

    // ── calibration overlay ──────────────────────────────────────
    if (has_accel && !g_accel.cal_done) {
        poll_lid();
        update_stars();
        draw_stars();

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

        if (g_accel.cal_done)
            consume_impact();

        render();
        return;
    }

    // ── tilt to select ───────────────────────────────────────────
    if (has_accel) {
        poll_lid();
        if (g_lid.valid) {
            float lid = (float)g_lid.angle;
            float t = 1.0f - (lid - LID_MIN) / (LID_MAX - LID_MIN);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float target = t * (float)(NUM_GAMES - 1);
            selected_f += (target - selected_f) * 0.15f;
            selected = (int)(selected_f + 0.5f);
            if (selected < 0) selected = 0;
            if (selected >= NUM_GAMES) selected = NUM_GAMES - 1;
        }

        // tap to launch
        if (consume_impact()) {
            launch_game(selected);
            render();
            return;
        }
    } else {
        // auto mode: cycle every 2 seconds
        if (++auto_select_timer >= GAME_FPS * 2) {
            auto_select_timer = 0;
            selected = (selected + 1) % NUM_GAMES;
            selected_f = (float)selected;
        }

        // auto-launch after 3 seconds on a game
        if (selected != last_auto_selected) {
            last_auto_selected = selected;
            auto_launch_timer = 0;
        }
        if (++auto_launch_timer >= GAME_FPS * 3) {
            auto_launch_timer = 0;
            launch_game(selected);
            render();
            return;
        }
    }

    // ── draw menu ────────────────────────────────────────────────
    update_stars();
    draw_stars();

    int cy = play_h / 2;
    int cx = term_w / 2;

    // title — pushed up for more room
    draw_center(cy - 11, "T A B L E   I M P A C T", C_HICYAN);
    draw_center(cy - 10, "-----------------------", C_GRAY);

    // 3D UMIT — front-facing at startup angle, rotates when you tilt
    float lid_angle = g_lid.valid ? (float)g_lid.angle : 105.0f;
    if (!umit_base_set) { umit_base_angle = lid_angle; umit_base_set = 1; }
    float rot = (lid_angle - umit_base_angle) * (PIf / 60.0f);
    if (rot > PIf / 3.0f) rot = PIf / 3.0f;
    if (rot < -PIf / 3.0f) rot = -PIf / 3.0f;
    float cos_r = cosf(rot), sin_r = sinf(rot);

    draw_center(cy - 7, "By", C_WHITE);
    draw_3d_umit(cx, cy - 5, cos_r, sin_r);
    draw_center(cy + 3, "umit.cc", C_GRAY);

    // game list — two-column grid (4 per column)
    int list_y = cy + 5;
    int lcx = term_w / 4;          // left column center
    int rcx = term_w * 3 / 4;      // right column center

    for (int i = 0; i < NUM_GAMES; i++) {
        int col = i / 4;           // 0=left, 1=right
        int ri  = i % 4;           // row within column
        int row = list_y + ri;
        int ccx = (col == 0) ? lcx : rcx;
        int nlen = (int)strlen(games[i].name);
        int x0 = ccx - nlen / 2;

        if (i == selected) {
            grid_str(x0 - 3, row, ">>", C_HICYAN);
            draw_shimmer(row, games[i].name, ccx);
        } else {
            grid_str(x0, row, games[i].name, C_GRAY);
        }
    }

    // description of selected game — centered below the grid
    int desc_y = list_y + 5;
    draw_center(desc_y, games[selected].desc, C_WHITE);

    // controls hint
    int hint_y = desc_y + 2;
    if (has_accel)
        draw_center(hint_y, "TILT to select  |  TAP to launch", C_GRAY);
    else
        draw_center(hint_y, "(no accelerometer - auto mode)", C_YELLOW);

    // sensor info on bottom
    {
        int y = term_h - 1;
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

    render();
}

// ── main ──────────────────────────────────────────────────────────

int main(void) {
    fprintf(stderr, "TableImpact Chooser — Starting...\n");

    if (getuid() != 0)
        fprintf(stderr,
            "Warning: not running as root — SPU access needs sudo.\n");

    srand((unsigned)time(NULL));
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    atexit(cleanup);

    get_term_size();
    grid_alloc();
    init_stars();

    // sensors
    setup_lid();
    has_accel = setup_accel();

    // suppress sensor debug output
    freopen("/dev/null", "w", stderr);

    // clear screen
    write(STDOUT_FILENO, "\033[2J\033[H", 7);

    // game timer at 30 fps
    g_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 0.1,
        1.0 / GAME_FPS,
        0, 0, game_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), g_timer, kCFRunLoopDefaultMode);

    CFRunLoopRun();
    return 0;
}

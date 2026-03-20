// game_engine.h — Shared game engine for TableImpact terminal games
// Header-only: #include "game_engine.h"
// Provides: terminal grid rendering, starfield, particles, flashes,
//           output buffering, 3D UMIT title, and common game infrastructure.

#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "sensors.h"
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>

// ── constants ─────────────────────────────────────────────────────

#define GAME_FPS        30
#define MAX_PARTICLES   96
#define MAX_STARS       60
#define MAX_FLASHES     8

#define ST_CAL    0
#define ST_TITLE  1
#define ST_PLAY   2
#define ST_OVER   3

#define EXIT_TILT_ANGLE  20

#define LID_MIN   60.0f
#define LID_MAX   100.0f

#define C_NONE    0
#define C_RED     31
#define C_GREEN   32
#define C_YELLOW  33
#define C_MAGENTA 35
#define C_CYAN    36
#define C_WHITE   37
#define C_GRAY    90
#define C_HIRED   91
#define C_HIGREEN 92
#define C_HICYAN  96

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define PIf ((float)M_PI)

// ── types ─────────────────────────────────────────────────────────

typedef struct { char ch; int color; } Cell;
typedef struct { int x, y; char ch; } Star;
typedef struct { float x, y, vx, vy; int life, color, active; char ch; } Particle;
typedef struct { int x, y, frames_left, active; } Flash;

// ── globals ───────────────────────────────────────────────────────

static int term_w = 80, term_h = 24;
static int play_h;

static int game_state  = ST_CAL;
static int has_accel   = 0;
static int frame_num   = 0;
static int auto_timer  = 0;
static int from_launcher = 0;

static float umit_base_angle = 105.0f;
static int   umit_base_set   = 0;

static Cell     *grid    = NULL;
static char     *out_buf = NULL;
static int       out_cap = 0;
static int       out_len = 0;

static Star     stars[MAX_STARS];
static Particle particles[MAX_PARTICLES];
static Flash    flashes[MAX_FLASHES];

// ── terminal ──────────────────────────────────────────────────────

static void get_term_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0) {
        term_w = ws.ws_col;
        term_h = ws.ws_row;
    }
    play_h = term_h - 1;
    if (play_h < 3) play_h = 3;
}

static void cleanup(void) {
    const char *s = "\033[?25h\033[0m\033[2J\033[H";
    write(STDOUT_FILENO, s, strlen(s));
}

static void sig_handler(int sig) {
    (void)sig;
    cleanup();
    _exit(0);
}

// ── output buffer ─────────────────────────────────────────────────

static void buf_grow(int need) {
    if (out_len + need > out_cap) {
        out_cap = (out_len + need) * 2;
        out_buf = realloc(out_buf, out_cap);
    }
}

static void buf_ch(char c) {
    buf_grow(1);
    out_buf[out_len++] = c;
}

static void buf_str(const char *s) {
    int n = (int)strlen(s);
    buf_grow(n);
    memcpy(out_buf + out_len, s, n);
    out_len += n;
}

static void buf_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
static void buf_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int avail = out_cap - out_len;
    if (avail < 64) { buf_grow(256); avail = out_cap - out_len; }
    int n = vsnprintf(out_buf + out_len, avail, fmt, ap);
    va_end(ap);
    if (n >= avail) {
        buf_grow(n + 1);
        va_start(ap, fmt);
        n = vsnprintf(out_buf + out_len, out_cap - out_len, fmt, ap);
        va_end(ap);
    }
    if (n > 0) out_len += n;
}

// ── grid ──────────────────────────────────────────────────────────

static void grid_alloc(void) {
    grid = realloc(grid, term_w * term_h * sizeof(Cell));
}

static void grid_clear(void) {
    for (int i = 0; i < term_w * term_h; i++) {
        grid[i].ch = ' ';
        grid[i].color = C_NONE;
    }
}

static void grid_put(int x, int y, char c, int color) {
    if (x >= 0 && x < term_w && y >= 0 && y < term_h) {
        grid[y * term_w + x].ch = c;
        grid[y * term_w + x].color = color;
    }
}

static void grid_str(int x, int y, const char *s, int color) {
    while (*s) grid_put(x++, y, *s++, color);
}

// ── starfield ─────────────────────────────────────────────────────

static void init_stars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x  = rand() % term_w;
        stars[i].y  = rand() % play_h;
        stars[i].ch = (rand() % 3 == 0) ? '+' : '.';
    }
}

static void update_stars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x--;
        if (stars[i].x < 0) {
            stars[i].x  = term_w - 1;
            stars[i].y  = rand() % play_h;
            stars[i].ch = (rand() % 3 == 0) ? '+' : '.';
        }
    }
}

static void draw_stars(void) {
    for (int i = 0; i < MAX_STARS; i++)
        grid_put(stars[i].x, stars[i].y, stars[i].ch, C_GRAY);
}

// ── particles ─────────────────────────────────────────────────────

static void spawn_particles_ex(int px, int py, int count,
                                float spread, int color) {
    const char glyphs[] = "*#.+x";
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        if (particles[i].active) continue;
        particles[i] = (Particle){
            .x = (float)px, .y = (float)py,
            .vx = ((rand() % 100) - 50) / 25.0f * spread,
            .vy = ((rand() % 100) - 50) / 50.0f * spread,
            .life = 4 + rand() % 6,
            .color = color,
            .ch = glyphs[rand() % 5],
            .active = 1
        };
        count--;
    }
}

static void spawn_particles(int px, int py, int count) {
    spawn_particles_ex(px, py, count, 1.0f, 0);
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        if (--particles[i].life <= 0) particles[i].active = 0;
    }
}

static void draw_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        int color = particles[i].color;
        if (color == 0)
            color = particles[i].life > 3 ? C_YELLOW : C_HIRED;
        grid_put((int)particles[i].x, (int)particles[i].y,
                 particles[i].ch, color);
    }
}

// ── flashes ───────────────────────────────────────────────────────

static void spawn_flash(int x, int y) {
    for (int i = 0; i < MAX_FLASHES; i++) {
        if (!flashes[i].active) {
            flashes[i] = (Flash){
                .x = x, .y = y, .frames_left = 2, .active = 1
            };
            return;
        }
    }
}

static void update_flashes(void) {
    for (int i = 0; i < MAX_FLASHES; i++) {
        if (!flashes[i].active) continue;
        if (--flashes[i].frames_left <= 0) flashes[i].active = 0;
    }
}

static void draw_flashes(void) {
    for (int i = 0; i < MAX_FLASHES; i++) {
        if (!flashes[i].active) continue;
        int x = flashes[i].x, y = flashes[i].y;
        grid_put(x,   y,   '*', C_WHITE);
        grid_put(x-1, y,   '*', C_WHITE);
        grid_put(x+1, y,   '*', C_WHITE);
        grid_put(x,   y-1, '*', C_WHITE);
        grid_put(x,   y+1, '*', C_WHITE);
    }
}

// ── render ────────────────────────────────────────────────────────

static void render(void) {
    out_len = 0;
    buf_str("\033[?25l\033[H");

    int last_color = -1;
    for (int y = 0; y < term_h; y++) {
        for (int x = 0; x < term_w; x++) {
            Cell *c = &grid[y * term_w + x];
            if (c->color != last_color) {
                if (c->color == C_NONE)
                    buf_str("\033[0m");
                else
                    buf_printf("\033[%dm", c->color);
                last_color = c->color;
            }
            buf_ch(c->ch);
        }
        if (y < term_h - 1) buf_str("\r\n");
    }
    if (last_color != C_NONE) buf_str("\033[0m");

    write(STDOUT_FILENO, out_buf, out_len);
}

// ── centered text helper ──────────────────────────────────────────

static void draw_center(int y, const char *s, int color) {
    int len = (int)strlen(s);
    grid_str(term_w / 2 - len / 2, y, s, color);
}

static void check_launcher_exit(void) {
    if (!from_launcher) return;
    poll_lid();
    if (g_lid.valid && g_lid.angle <= EXIT_TILT_ANGLE) {
        cleanup();
        _exit(0);
    }
}

// ── 3D UMIT title letters ─────────────────────────────────────────

#define UMIT_LW  8
#define UMIT_LH  8
#define UMIT_SC  2

static const uint8_t umit_bmp[4][UMIT_LH] = {
    {0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x66, 0x3C},  // U
    {0xC3, 0xE7, 0xFF, 0xDB, 0xC3, 0xC3, 0xC3, 0xC3},  // M
    {0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xFF},  // I
    {0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},  // T
};

#define BMP_ON(li, r, c) \
    ((r) >= 0 && (r) < UMIT_LH && (c) >= 0 && (c) < UMIT_LW && \
     (umit_bmp[(li)][(r)] & (1 << (UMIT_LW - 1 - (c)))))

static void draw_3d_umit(int cx, int base_y, float cos_r, float sin_r) {
    int gap = 4;
    int letter_sw = UMIT_LW * UMIT_SC;
    int spacing = letter_sw + gap;
    int total = 4 * letter_sw + 3 * gap;
    float half = total / 2.0f;
    int depth = 3;

    for (int li = 0; li < 4; li++) {
        int vx0 = li * spacing;
        for (int row = 0; row < UMIT_LH; row++) {
            for (int col = 0; col < UMIT_LW; col++) {
                if (!BMP_ON(li, row, col)) continue;
                if (BMP_ON(li, row - 1, col)) continue;
                for (int sc = 0; sc < UMIT_SC; sc++) {
                    float vcol = vx0 + col * UMIT_SC + sc - half;
                    for (int d = 0; d <= depth; d++) {
                        int sx = cx + (int)(vcol * cos_r + d * sin_r);
                        grid_put(sx, base_y + row - 1, '_', C_CYAN);
                    }
                }
            }
        }
    }

    for (int li = 0; li < 4; li++) {
        int vx0 = li * spacing;
        for (int row = 0; row < UMIT_LH; row++) {
            for (int col = 0; col < UMIT_LW; col++) {
                if (!BMP_ON(li, row, col)) continue;
                for (int sc = 0; sc < UMIT_SC; sc++) {
                    float vcol = vx0 + col * UMIT_SC + sc - half;
                    for (int d = 1; d <= depth; d++) {
                        int sx = cx + (int)(vcol * cos_r + d * sin_r);
                        grid_put(sx, base_y + row, '#', C_CYAN);
                    }
                }
            }
        }
    }

    for (int li = 0; li < 4; li++) {
        int vx0 = li * spacing;
        for (int row = 0; row < UMIT_LH; row++) {
            for (int col = 0; col < UMIT_LW; col++) {
                if (!BMP_ON(li, row, col)) continue;
                for (int sc = 0; sc < UMIT_SC; sc++) {
                    float vcol = vx0 + col * UMIT_SC + sc - half;
                    int sx = cx + (int)(vcol * cos_r);
                    grid_put(sx, base_y + row, '#', C_HICYAN);
                }
            }
        }
    }
}

#undef BMP_ON

#endif // GAME_ENGINE_H

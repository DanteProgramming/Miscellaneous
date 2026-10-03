/*
 * FIREWORKS OVER THE HARBOR
 *
 * A endless, hands-off fireworks show in your terminal: rockets, glowing
 * trails, six kinds of shells (including hearts), a twinkling sky, a city
 * skyline and a rippling water reflection.
 *
 * Compile:  gcc -O2 fireworks.c -o fireworks -lm
 * Run:      ./fireworks     (.\fireworks on Windows)      Ctrl+C to quit
 *
 * Tips: maximize the terminal window BEFORE running (the size is read at
 * startup), and use Windows Terminal / any modern terminal with true color.
 */
#ifdef _WIN32
  #ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0A00
  #endif
#else
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

#define PI        3.14159265f
#define MAX_W     200          /* max columns                         */
#define MAX_PH    120          /* max pixel rows (2 per text row)     */
#define MAX_P     8000         /* max live particles                  */
#define MAX_FLASH 16
#define MAX_STARS 600
#define FRAME_MS  33.3         /* ~30 fps                             */

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, y, vx, vy;
    float c0[3], c1[3];        /* color at birth -> color when dying   */
    float life, maxlife, drag, grav;
    int   rocket, twinkle;
} Particle;

typedef struct { float x, y, col[3], i; } Flash;

static int W, ROWS, PH, gy;    /* width, text rows, pixel rows, ground line */

static Particle P[MAX_P];
static int      np;
static Flash    flashes[MAX_FLASH];
static int      nflash;

static float glow[MAX_PH][MAX_W][3];   /* persistent light (gives trails) */
static float tmpb[MAX_PH][MAX_W][3];
static float fb  [MAX_PH][MAX_W][3];   /* final frame, floating point     */

static int   skyH[MAX_W];              /* skyline height per column       */
static struct { int x, y; float b, ph; } stars[MAX_STARS];
static int   nstars;

static char *outbuf;
static volatile sig_atomic_t running = 1;

static void on_sigint(int s) { (void)s; running = 0; }

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static float frand(void)                { return rand() / (RAND_MAX + 1.0f); }
static float frange(float a, float b)   { return a + (b - a) * frand(); }

static void hsv(float h, float s, float v, float *r, float *g, float *b) {
    float i = floorf(h * 6.0f), f = h * 6.0f - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (((int)i) % 6) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static double now_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static void sleep_ms(double ms) {
#ifdef _WIN32
    /* High-resolution timer for smooth frame pacing (Win10 1803+) */
    static HANDLE t;
    if (!t) t = CreateWaitableTimerExW(NULL, NULL, 0x00000002, 0x1F0003);
    if (t) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ms * 10000.0);
        SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(t, INFINITE);
    } else {
        Sleep((DWORD)ms);
    }
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000.0);
    ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1e6);
    nanosleep(&ts, NULL);
#endif
}

static void term_size(int *cols, int *rows) {
    *cols = 100; *rows = 30;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        *cols = info.srWindow.Right  - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top  + 1;
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        *cols = w.ws_col; *rows = w.ws_row;
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Scenery                                                             */
/* ------------------------------------------------------------------ */
static int is_building(int x, int y) {
    return x >= 0 && x < W && y < gy && y >= gy - skyH[x];
}

static int win_lit(int x, int y) {
    int h = gy - y;
    if (x % 3 != 1 || h % 3 != 1 || y <= gy - skyH[x] + 1) return 0;
    unsigned v = (unsigned)x * 73856093u ^ (unsigned)h * 19349663u;
    v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
    return (v % 5) == 0;
}

static void build_scene(void) {
    int x = 0;
    while (x < W) {
        int bw = 3 + rand() % 7;
        int bh = 3 + rand() % (int)(gy * 0.20f);
        if (rand() % 10 == 0) bh = (int)(bh * 1.5f);          /* the odd tower */
        for (int i = 0; i < bw && x < W; i++) skyH[x++] = bh;
    }
    nstars = (W * gy) / 45;
    if (nstars > MAX_STARS) nstars = MAX_STARS;
    for (int i = 0; i < nstars; i++) {
        int sx, sy, tries = 0;
        do {
            sx = rand() % W;
            sy = (int)(frand() * gy * 0.85f);
        } while (is_building(sx, sy) && ++tries < 20);
        stars[i].x = sx; stars[i].y = sy;
        stars[i].b = frange(0.25f, 0.9f);
        stars[i].ph = frange(0.0f, 6.28f);
    }
}

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */
static Particle *newp(void) { return np < MAX_P ? &P[np++] : NULL; }

static void spark(float x, float y, float vx, float vy,
                  const float *c0, const float *c1,
                  float life, float drag, float grav, int tw) {
    Particle *p = newp();
    if (!p) return;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    memcpy(p->c0, c0, sizeof p->c0);
    memcpy(p->c1, c1, sizeof p->c1);
    p->life = p->maxlife = life;
    p->drag = drag; p->grav = grav;
    p->rocket = 0; p->twinkle = tw;
}

static void addflash(float x, float y, const float *c) {
    if (nflash >= MAX_FLASH) return;
    Flash *f = &flashes[nflash++];
    f->x = x; f->y = y; f->i = 1.0f;
    f->col[0] = c[0]; f->col[1] = c[1]; f->col[2] = c[2];
}

/* Embers cool toward warm orange as they die. */
static void ember(const float *c, float *out) {
    out[0] = c[0] * 0.5f + 0.5f;
    out[1] = c[1] * 0.35f + 0.10f;
    out[2] = c[2] * 0.15f;
}

static void sphere_dir(float *dx, float *dy) {
    float z = 2.0f * frand() - 1.0f, a = 2.0f * PI * frand();
    float s = sqrtf(1.0f - z * z);
    *dx = s * cosf(a); *dy = s * sinf(a);
}

static void explode(float x, float y) {
    float c[3], c2[3], e[3], e2[3];
    float h = frand();
    hsv(h, frange(0.60f, 0.95f), 1.0f, &c[0], &c[1], &c[2]);
    hsv(fmodf(h + frange(0.30f, 0.60f), 1.0f), 0.85f, 1.0f, &c2[0], &c2[1], &c2[2]);
    ember(c, e); ember(c2, e2);
    addflash(x, y, c);

    float R = frange(0.26f, 0.38f) * gy;      /* final burst radius in pixels */
    int   n = (int)(gy * 2.2f); if (n > 150) n = 150;
    int   roll = rand() % 100;
    float dx, dy;

    if (roll < 28) {                                   /* PEONY: classic sphere */
        float drag = 0.95f, v = R * (1 - drag);
        for (int i = 0; i < n; i++) {
            sphere_dir(&dx, &dy);
            float s = v * frange(0.92f, 1.0f);
            spark(x, y, dx * s, dy * s, c, e, frange(55, 80), drag, 0.012f, 0);
        }
    } else if (roll < 42) {                            /* RING: tilted double ring */
        float drag = 0.95f, v = R * (1 - drag);
        float tilt = frange(0.25f, 1.0f), rot = frange(-0.7f, 0.7f);
        for (int i = 0; i < 2 * n / 3; i++) {
            float a = 2 * PI * i / (2 * n / 3);
            float ux = cosf(a), uy = sinf(a) * tilt;
            float vx = ux * cosf(rot) - uy * sinf(rot);
            float vy = ux * sinf(rot) + uy * cosf(rot);
            spark(x, y, vx * v, vy * v, c, e, frange(60, 75), drag, 0.008f, 0);
            if (i % 2 == 0)
                spark(x, y, vx * v * 0.55f, vy * v * 0.55f, c2, e2, frange(55, 70), drag, 0.008f, 0);
        }
    } else if (roll < 58) {                            /* WILLOW: golden drooping rain */
        float g0[3] = {1.0f, 0.78f, 0.35f}, g1[3] = {1.0f, 0.28f, 0.04f};
        float drag = 0.968f, v = R * (1 - drag) * 0.9f;
        for (int i = 0; i < n * 6 / 10; i++) {
            sphere_dir(&dx, &dy);
            float s = v * frange(0.7f, 1.0f);
            spark(x, y, dx * s, dy * s, g0, g1, frange(95, 130), drag, 0.016f, 0);
        }
    } else if (roll < 72) {                            /* TWO-TONE: core + shell */
        float drag = 0.95f, v = R * (1 - drag);
        for (int i = 0; i < n; i++) {
            sphere_dir(&dx, &dy);
            int inner = (i % 3 == 0);
            float s = v * (inner ? 0.55f : 1.0f) * frange(0.92f, 1.0f);
            spark(x, y, dx * s, dy * s, inner ? c2 : c, inner ? e2 : e,
                  frange(55, 80), drag, 0.012f, 0);
        }
    } else if (roll < 90) {                            /* CRACKLE: white-hot, glittering */
        float w0[3] = {1.0f, 1.0f, 0.9f};
        float drag = 0.95f, v = R * (1 - drag);
        for (int i = 0; i < n; i++) {
            sphere_dir(&dx, &dy);
            float s = v * frange(0.85f, 1.0f);
            spark(x, y, dx * s, dy * s, w0, c, frange(55, 80), drag, 0.010f, 1);
        }
    } else {                                           /* HEART! */
        float p0[3] = {1.0f, 0.25f, 0.45f}, p1[3] = {1.0f, 0.1f, 0.15f};
        float p2[3] = {1.0f, 0.65f, 0.75f};
        float drag = 0.95f, sc = R * (1 - drag) / 17.0f;
        int m = 90;
        for (int layer = 0; layer < 2; layer++) {
            for (int i = 0; i < m; i++) {
                float t  = 2 * PI * i / m;
                float hx = 16.0f * powf(sinf(t), 3.0f);
                float hy = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
                float k  = layer ? 0.6f : 1.0f;
                spark(x, y, hx * sc * k + frange(-0.02f, 0.02f),
                            -(hy - 2.0f) * sc * k + frange(-0.02f, 0.02f),
                      layer ? p2 : p0, p1, frange(65, 80), drag, 0.004f, 0);
            }
        }
    }
}

static void launch(void) {
    Particle *p = newp();
    if (!p) return;
    float ty   = frange(0.12f, 0.50f) * gy;        /* apex height */
    float rise = gy - ty;
    p->x  = frange(0.12f, 0.88f) * W;
    p->y  = (float)gy - 1.0f;
    p->vx = frange(-0.15f, 0.15f);
    p->vy = -sqrtf(2.0f * 0.03f * rise);
    p->rocket = 1; p->life = p->maxlife = 1.0f;
    p->drag = 1.0f; p->grav = 0.03f; p->twinkle = 0;
}

/* Add light to the glow buffer with sub-pixel (bilinear) precision. */
static void splat(float x, float y, float r, float g, float b) {
    int   x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - x0, fy = y - y0;
    float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    for (int i = 0; i < 4; i++) {
        int px = x0 + (i & 1), py = y0 + (i >> 1);
        if (px < 0 || px >= W || py < 0 || py >= gy) continue;
        glow[py][px][0] += r * w[i];
        glow[py][px][1] += g * w[i];
        glow[py][px][2] += b * w[i];
    }
}

static void fade_blur(void) {
    const float DECAY = 0.84f;
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++) {
                float v = glow[y][x][c] * 0.90f;
                v += 0.025f * ((y > 0      ? glow[y-1][x][c] : 0.0f) +
                               (y < PH - 1 ? glow[y+1][x][c] : 0.0f) +
                               (x > 0      ? glow[y][x-1][c] : 0.0f) +
                               (x < W - 1  ? glow[y][x+1][c] : 0.0f));
                tmpb[y][x][c] = v * DECAY;
            }
    memcpy(glow, tmpb, sizeof glow);
}

static void update_particles(void) {
    for (int i = 0; i < np; ) {
        Particle *p = &P[i];

        if (p->rocket) {
            p->vy += p->grav; p->x += p->vx; p->y += p->vy;
            splat(p->x, p->y, 1.1f, 0.95f, 0.7f);
            for (int k = 0; k < 2; k++) {                 /* sparkling exhaust */
                float c0[3] = {1.0f, 0.75f, 0.35f}, c1[3] = {1.0f, 0.30f, 0.05f};
                spark(p->x, p->y + 0.5f, p->vx * 0.2f + frange(-0.12f, 0.12f),
                      frange(0.05f, 0.35f), c0, c1, frange(8, 20), 0.9f, 0.01f, 0);
            }
            if (p->vy > -0.22f) {                         /* apex: boom */
                float ex = p->x, ey = p->y;
                P[i] = P[--np];
                explode(ex, ey);
                continue;
            }
            i++;
            continue;
        }

        p->vx *= p->drag;
        p->vy  = p->vy * p->drag + p->grav;
        p->x  += p->vx;
        p->y  += p->vy;
        p->life -= 1.0f;

        if (p->life <= 0 || p->y >= gy || p->x < -10 || p->x > W + 10 || p->y < -30) {
            P[i] = P[--np];
            continue;
        }

        float k  = p->life / p->maxlife;
        float br = k;
        if (p->twinkle && frand() < 0.5f) br *= 0.15f;
        float m = 0.95f * br;
        splat(p->x, p->y,
              (p->c0[0] + (p->c1[0] - p->c0[0]) * (1 - k)) * m,
              (p->c0[1] + (p->c1[1] - p->c0[1]) * (1 - k)) * m,
              (p->c0[2] + (p->c1[2] - p->c0[2]) * (1 - k)) * m);
        i++;
    }
}

static void update_flashes(void) {
    for (int i = 0; i < nflash; ) {
        flashes[i].i *= 0.84f;
        if (flashes[i].i < 0.03f) flashes[i] = flashes[--nflash];
        else i++;
    }
}

/* ------------------------------------------------------------------ */
/* Compose the frame                                                   */
/* ------------------------------------------------------------------ */
static void compose(int frame) {
    static const float skyTop[3] = {0.008f, 0.012f, 0.045f};
    static const float skyHor[3] = {0.075f, 0.055f, 0.150f};
    static const float watTop[3] = {0.040f, 0.035f, 0.100f};
    static const float watBot[3] = {0.006f, 0.008f, 0.030f};
    const float R2 = (gy * 0.45f) * (gy * 0.45f);
    const int   wh = PH - gy;

    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < W; x++) {
            float *c = fb[y][x];

            if (y < gy) {
                float t = (float)y / gy; t *= t;
                for (int i = 0; i < 3; i++)
                    c[i] = skyTop[i] + (skyHor[i] - skyTop[i]) * t + glow[y][x][i];
            } else {
                float d = (float)(y - gy) / (float)wh;
                for (int i = 0; i < 3; i++)
                    c[i] = watTop[i] + (watBot[i] - watTop[i]) * d;

                /* mirrored, rippling reflection of everything above */
                int dd = y - gy, sy = gy - 1 - dd;
                if (sy >= 0) {
                    float wob = sinf(dd * 0.9f + frame * 0.15f + x * 0.05f);
                    int   sx  = x + (int)floorf(wob * 1.3f + 0.5f);
                    if (sx < 0) sx = 0;
                    if (sx >= W) sx = W - 1;
                    float shimmer = 0.80f + 0.20f * sinf(y * 1.7f + frame * 0.2f + x * 0.3f);
                    float refl = 0.45f * (1.0f - 0.6f * d) * shimmer;
                    if (is_building(sx, sy)) {
                        if (win_lit(sx, sy)) {
                            c[0] += 0.30f * refl; c[1] += 0.22f * refl; c[2] += 0.10f * refl;
                        }
                    } else {
                        for (int i = 0; i < 3; i++) c[i] += glow[sy][sx][i] * refl;
                    }
                }
            }
        }
    }

    /* twinkling stars */
    for (int s = 0; s < nstars; s++) {
        if (is_building(stars[s].x, stars[s].y)) continue;
        float b = stars[s].b * (0.5f + 0.5f * sinf(frame * 0.06f + stars[s].ph)) * 0.5f;
        float *c = fb[stars[s].y][stars[s].x];
        c[0] += 0.75f * b; c[1] += 0.80f * b; c[2] += 1.00f * b;
    }

    /* explosion light washing over the sky and water */
    for (int f = 0; f < nflash; f++) {
        for (int y = 0; y < PH; y++) {
            float fy = (y < gy) ? (float)y : (float)(2 * gy - 1 - y);
            float k  = (y < gy) ? 0.45f : 0.22f;
            float dy = fy - flashes[f].y;
            for (int x = 0; x < W; x++) {
                float dx = x - flashes[f].x;
                float w  = flashes[f].i * k / (1.0f + (dx * dx + dy * dy) / R2);
                float *c = fb[y][x];
                c[0] += flashes[f].col[0] * w;
                c[1] += flashes[f].col[1] * w;
                c[2] += flashes[f].col[2] * w;
            }
        }
    }

    /* city silhouette with a few lit windows */
    for (int x = 0; x < W; x++) {
        for (int y = gy - skyH[x]; y < gy; y++) {
            float *c = fb[y][x];
            if (win_lit(x, y)) { c[0] = 0.22f; c[1] = 0.16f; c[2] = 0.07f; }
            else               { c[0] = 0.010f; c[1] = 0.010f; c[2] = 0.025f; }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Output: each text cell = two pixels via the "upper half block"      */
/* ------------------------------------------------------------------ */
static int pack(const float *c) {
    int v[3];
    for (int i = 0; i < 3; i++) {
        float t = 1.0f - expf(-1.4f * c[i]);              /* soft tone-mapping */
        int q = (int)(t * 255.0f + 0.5f);
        v[i] = q < 0 ? 0 : (q > 255 ? 255 : q);
    }
    return (v[0] << 16) | (v[1] << 8) | v[2];
}

static char *ap_num(char *p, int v) {
    if (v >= 100) { *p++ = '0' + v / 100; v %= 100; *p++ = '0' + v / 10; *p++ = '0' + v % 10; }
    else if (v >= 10) { *p++ = '0' + v / 10; *p++ = '0' + v % 10; }
    else *p++ = '0' + v;
    return p;
}

static char *ap_color(char *p, int which, int rgb) {
    *p++ = 27; *p++ = '['; *p++ = '0' + which; *p++ = '8'; *p++ = ';'; *p++ = '2'; *p++ = ';';
    p = ap_num(p, (rgb >> 16) & 255); *p++ = ';';
    p = ap_num(p, (rgb >> 8) & 255);  *p++ = ';';
    p = ap_num(p, rgb & 255);         *p++ = 'm';
    return p;
}

static void emit(void) {
    char *p = outbuf;
    for (int r = 0; r < ROWS; r++) {
        *p++ = 27; *p++ = '['; p = ap_num(p, r + 1); *p++ = ';'; *p++ = '1'; *p++ = 'H';
        int pf = -1, pb = -1;
        for (int x = 0; x < W; x++) {
            int f = pack(fb[2 * r][x]), b = pack(fb[2 * r + 1][x]);
            if (f != pf) { p = ap_color(p, 3, f); pf = f; }
            if (b != pb) { p = ap_color(p, 4, b); pb = b; }
            *p++ = (char)0xE2; *p++ = (char)0x96; *p++ = (char)0x80;   /* U+2580 */
        }
    }
    fwrite(outbuf, 1, (size_t)(p - outbuf), stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);                                  /* UTF-8 */
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hout, &mode))
        SetConsoleMode(hout, mode | 0x0004);                    /* ANSI colors */
#endif
    int cols, rows;
    term_size(&cols, &rows);
    W    = cols < 40 ? 40 : (cols > MAX_W ? MAX_W : cols);
    ROWS = rows - 1;                                            /* keep last line free */
    if (ROWS < 12) ROWS = 12;
    if (ROWS > MAX_PH / 2) ROWS = MAX_PH / 2;
    PH   = ROWS * 2;
    gy   = (int)(PH * 0.78f);

    srand((unsigned)time(NULL));
    build_scene();
    outbuf = (char *)malloc((size_t)W * ROWS * 44 + (size_t)ROWS * 16 + 64);
    if (!outbuf) return 1;

    signal(SIGINT, on_sigint);
    printf("\x1b[?1049h\x1b[?25l\x1b[2J");                      /* alt screen, hide cursor */

    int    frame = 0, cooldown = 0;
    double next  = now_ms();

    while (running) {
        if (--cooldown <= 0) {
            launch();
            cooldown = (rand() % 100 < 22) ? (int)frange(3, 8)      /* quick salvo */
                                           : (int)frange(20, 55);
        }
        fade_blur();
        update_particles();
        update_flashes();
        compose(frame);
        emit();
        frame++;

        next += FRAME_MS;
        double rem = next - now_ms();
        if (rem > 0) sleep_ms(rem); else next = now_ms();
    }

    printf("\x1b[0m\x1b[?25h\x1b[?1049l");                      /* restore terminal */
    fflush(stdout);
    free(outbuf);
    return 0;
}

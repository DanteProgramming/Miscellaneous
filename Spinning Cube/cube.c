/*
 * Spinning ASCII cube in C
 *
 * Compile:  gcc cube.c -o cube -lm
 * Run:      ./cube   (or .\cube on Windows)   Ctrl+C to quit
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#define WIDTH  80
#define HEIGHT 24

/* Each face: a normal direction plus two axes that span the face. */
static const float NORMALS[6][3] = {
    { 0, 0, 1}, { 0, 0,-1}, { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
};
static const float U_AXES[6][3] = {
    {1,0,0}, {1,0,0}, {0,1,0}, {0,1,0}, {1,0,0}, {1,0,0}
};
static const float V_AXES[6][3] = {
    {0,1,0}, {0,1,0}, {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}
};

/* A different character for each face so the cube's sides are easy to see. */
static const char FACE_CHARS[6] = { '.', ':', '+', '*', '#', '%' };

/* Rotate point p around the X, then Y, then Z axes. */
static void rotate(float p[3], float a, float b, float c) {
    float x = p[0], y = p[1], z = p[2], t;

    /* around X */
    t = y * cosf(a) - z * sinf(a);
    z = y * sinf(a) + z * cosf(a);
    y = t;

    /* around Y */
    t = x * cosf(b) + z * sinf(b);
    z = -x * sinf(b) + z * cosf(b);
    x = t;

    /* around Z */
    t = x * cosf(c) - y * sinf(c);
    y = x * sinf(c) + y * cosf(c);
    x = t;

    p[0] = x; p[1] = y; p[2] = z;
}

int main(void) {
    const float SIZE = 1.3f;   /* half the length of a cube edge */
    const float K2   = 5.0f;   /* distance from viewer to cube   */
    const float K1   = 40.0f;  /* projection scale (zoom)        */
    const float STEP = 0.025f; /* sampling density on each face  */

    float A = 0.0f, B = 0.0f, C = 0.0f;
    char  output[WIDTH * HEIGHT];
    float zbuffer[WIDTH * HEIGHT];

    printf("\x1b[2J\x1b[?25l");  /* clear screen, hide cursor */

    for (;;) {
        memset(output, ' ', sizeof output);
        memset(zbuffer, 0, sizeof zbuffer);

        for (int f = 0; f < 6; f++) {
            for (float u = -1.0f; u <= 1.0f; u += STEP) {
                for (float v = -1.0f; v <= 1.0f; v += STEP) {
                    /* Point on the face: center + u*axis1 + v*axis2 */
                    float p[3];
                    for (int i = 0; i < 3; i++) {
                        p[i] = SIZE * (NORMALS[f][i] + u * U_AXES[f][i] + v * V_AXES[f][i]);
                    }

                    rotate(p, A, B, C);

                    float z   = p[2] + K2;
                    float ooz = 1.0f / z;  /* bigger = closer to the viewer */

                    /* Perspective projection (0.5 fixes tall characters) */
                    int xp = (int)(WIDTH  / 2 + K1 * ooz * p[0]);
                    int yp = (int)(HEIGHT / 2 - K1 * ooz * p[1] * 0.5f);

                    if (xp < 0 || xp >= WIDTH || yp < 0 || yp >= HEIGHT) continue;

                    int idx = xp + yp * WIDTH;
                    if (ooz > zbuffer[idx]) {
                        zbuffer[idx] = ooz;
                        /* Draw edges bright so the cube outline stands out */
                        int edge = (fabsf(u) > 0.95f || fabsf(v) > 0.95f);
                        output[idx] = edge ? '@' : FACE_CHARS[f];
                    }
                }
            }
        }

        printf("\x1b[H");  /* cursor to top-left */
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            putchar(i % WIDTH ? output[i] : '\n');
        }
        fflush(stdout);

        A += 0.03f;
        B += 0.05f;
        C += 0.02f;
        SLEEP_MS(30);
    }

    return 0;
}

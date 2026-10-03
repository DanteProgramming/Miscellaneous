/*
 * Spinning ASCII donut in C
 *
 * Compile:  gcc donut.c -o donut -lm
 * Run:      ./donut        (Ctrl+C to quit)
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define WIDTH  80
#define HEIGHT 24

int main(void) {
    const float R1 = 1.0f;   /* radius of the tube (the "thickness") */
    const float R2 = 2.0f;   /* distance from donut center to tube center */
    const float K2 = 5.0f;   /* distance from viewer to donut */
    /* Scale so the donut fits the screen */
    const float K1 = WIDTH * K2 * 3.0f / (8.0f * (R1 + R2));

    const char *shades = ".,-~:;=!*#$@";  /* dim -> bright */

    float A = 0.0f, B = 0.0f;  /* rotation angles around the X and Z axes */
    char  output[WIDTH * HEIGHT];
    float zbuffer[WIDTH * HEIGHT];

    printf("\x1b[2J\x1b[?25l");  /* clear screen, hide cursor */

    for (;;) {
        memset(output, ' ', sizeof output);
        memset(zbuffer, 0, sizeof zbuffer);

        float cA = cosf(A), sA = sinf(A);
        float cB = cosf(B), sB = sinf(B);

        /* theta sweeps around the tube's cross-section circle */
        for (float theta = 0; theta < 2 * M_PI; theta += 0.07f) {
            float ct = cosf(theta), st = sinf(theta);

            /* phi sweeps that circle around the donut's central axis */
            for (float phi = 0; phi < 2 * M_PI; phi += 0.02f) {
                float cp = cosf(phi), sp = sinf(phi);

                /* 2D circle before revolving */
                float cx = R2 + R1 * ct;
                float cy = R1 * st;

                /* 3D position after revolving and rotating by A and B */
                float x = cx * (cB * cp + sA * sB * sp) - cy * cA * sB;
                float y = cx * (sB * cp - sA * cB * sp) + cy * cA * cB;
                float z = K2 + cA * cx * sp + cy * sA;
                float ooz = 1.0f / z;  /* "one over z" for depth testing */

                /* Project 3D to 2D (0.5 compensates for tall characters) */
                int xp = (int)(WIDTH  / 2 + K1 * ooz * x);
                int yp = (int)(HEIGHT / 2 - K1 * ooz * y * 0.5f);

                /* Surface brightness: dot product of normal and light direction */
                float L = cp * ct * sB - cA * ct * sp - sA * st
                        + cB * (cA * st - ct * sA * sp);

                if (L > 0 && xp >= 0 && xp < WIDTH && yp >= 0 && yp < HEIGHT) {
                    int idx = xp + yp * WIDTH;
                    if (ooz > zbuffer[idx]) {  /* closer than what's there? */
                        zbuffer[idx] = ooz;
                        int shade = (int)(L * 8.0f);
                        if (shade > 11) shade = 11;
                        output[idx] = shades[shade];
                    }
                }
            }
        }

        /* Draw the frame */
        printf("\x1b[H");  /* move cursor to top-left */
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            putchar(i % WIDTH ? output[i] : '\n');
        }
        fflush(stdout);

        A += 0.04f;
        B += 0.02f;
        usleep(30000);  /* ~30 ms per frame */
    }

    return 0;
}

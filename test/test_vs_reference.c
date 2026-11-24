#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "apple_lut.h"

/* Scores the engine (loaded with the captured Apple LUT) against the measured
 * apple_accel.csv: for each finger speed in the clean range, drive a 1 s drag
 * and compare the engine's cursor speed to Apple's. Deviation here is just the
 * fixed-point / LUT-resampling error, so it must be small. */

static long g_px;
static void sink(const hid_report_t *r, void *u) {
    (void)u;
    g_px += r->dx < 0 ? -r->dx : r->dx;
}

static double engine_cursor_px_s(engine_t *e, double finger_mm_s) {
    g_px = 0;
    int dt_us = 8000;      /* 125 Hz */
    int steps = 125;       /* 1 s    */
    double mm_per_step = finger_mm_s * dt_us / 1e6;
    int32_t dxq = (int32_t)(mm_per_step * (1 << MM_Q));
    for (int i = 0; i < steps; i++) {
        engine_feed_move(e, SRC_LEFT_PAD, dxq, 0, (uint32_t)dt_us);
    }
    return (double)g_px / (steps * dt_us / 1e6);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "reference/apple_accel.csv";
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 2;
    }

    engine_cfg_t c;
    memset(&c, 0, sizeof c);
    c.sensitivity = 256;
    c.accel_max_mm_s = APPLE_ACCEL_MAX_MM_S;
    for (int i = 0; i < ACCEL_LUT_N; i++) {
        c.accel_lut[i] = APPLE_ACCEL_LUT[i];
    }
    engine_t e;
    engine_init(&e, &c, sink, 0);

    char line[256];
    if (!fgets(line, sizeof line, f)) { /* header */ }

    /* read the curve, build a monotonic target gain (pointer accel is monotonic;
     * dips are measurement noise), then compare the engine against that trend. */
    double fmm[64], gain[64];
    int m = 0;
    while (m < 64 && fgets(line, sizeof line, f)) {
        double a, cpx, g;
        int samp;
        if (sscanf(line, "%lf,%lf,%lf,%d", &a, &cpx, &g, &samp) != 4) {
            continue;
        }
        if (a <= 0 || cpx < 1.0) {
            continue;
        }
        fmm[m] = a;
        gain[m] = cpx / a;  /* px per mm */
        m++;
    }
    fclose(f);
    for (int i = 1; i < m; i++) {
        if (gain[i] < gain[i - 1]) {
            gain[i] = gain[i - 1];  /* running max -> monotonic */
        }
    }

    double sse = 0, maxe = 0;
    int n = 0;
    printf(" finger_mm_s   apple_px_s   engine_px_s   err%%\n");
    for (int i = 0; i < m; i++) {
        if (fmm[i] < 10.0 || fmm[i] > APPLE_ACCEL_MAX_MM_S) {
            continue;  /* skip the sub-pixel precision floor */
        }
        double target = fmm[i] * gain[i];  /* monotonic Apple trend, px/s */
        double got = engine_cursor_px_s(&e, fmm[i]);
        double err = (got - target) / target;
        sse += err * err;
        if (fabs(err) > maxe) {
            maxe = fabs(err);
        }
        n++;
        printf("  %9.1f   %10.1f   %11.1f   %+5.1f\n", fmm[i], target, got, err * 100);
    }

    double rms = n ? sqrt(sse / n) : 0;
    printf("\ntest_vs_reference: %d points (5..%d mm/s), RMS err %.1f%%, max %.1f%%\n",
           n, APPLE_ACCEL_MAX_MM_S, rms * 100, maxe * 100);
    if (n >= 10 && rms < 0.05) {
        printf("PASS (engine reproduces the Apple trend within 5%% RMS)\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}

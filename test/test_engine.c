#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"
#include "apple_lut.h"

static long g_px, g_py, g_wheel, g_zoom, g_rotate;
static int g_calls, g_left, g_right, g_btn_prev;

static void sink(const hid_report_t *r, void *u) {
    (void)u;
    g_px += r->dx;
    g_py += r->dy;
    g_wheel += r->wheel;
    g_zoom += r->zoom;
    g_rotate += r->rotate;
    if ((r->buttons & 1) && !(g_btn_prev & 1)) {
        g_left++;
    }
    if ((r->buttons & 2) && !(g_btn_prev & 2)) {
        g_right++;
    }
    g_btn_prev = r->buttons;
    g_calls++;
}

static engine_cfg_t apple_cfg(void) {
    engine_cfg_t c;
    memset(&c, 0, sizeof c);
    c.sensitivity = 256;  /* 1.0x */
    c.accel_max_mm_s = APPLE_ACCEL_MAX_MM_S;
    for (int i = 0; i < ACCEL_LUT_N; i++) {
        c.accel_lut[i] = APPLE_ACCEL_LUT[i];
    }
    c.momentum_tau_ms = 225;   /* captured from the Magic Trackpad */
    c.momentum_min = 10;       /* stop threshold, units/s */
    c.filter_alpha = 26214;    /* ~0.4 Q16, scroll-velocity smoothing */
    c.tap_ms = 180;            /* max tap duration */
    c.drag_ms = 300;           /* tap-and-drag window */
    c.scroll_step = 5;         /* scroll units per mm of two-finger travel */
    c.natural_scroll = 1;
    c.two_finger_right_click = 1;
    c.two_hand_zoom = 1;
    c.two_hand_zoom_gain = 256;
    return c;
}

/* straight horizontal drag at `mm_s` for `ms`, events at 125 Hz; returns total cursor px */
static long drag(engine_t *e, double mm_s, int ms) {
    g_px = g_py = 0;
    g_calls = 0;
    int dt_us = 8000;
    int steps = ms * 1000 / dt_us;
    double mm_per_step = mm_s * dt_us / 1e6;
    int32_t dxq = (int32_t)(mm_per_step * (1 << MM_Q));
    for (int i = 0; i < steps; i++) {
        engine_feed_move(e, SRC_LEFT_PAD, dxq, 0, dt_us);
    }
    return g_px;
}

int main(void) {
    engine_cfg_t c = apple_cfg();
    engine_t e;

    /* zero input -> zero output */
    engine_init(&e, &c, sink, 0);
    g_px = g_py = 0;
    g_calls = 0;
    engine_feed_move(&e, SRC_LEFT_PAD, 0, 0, 8000);
    assert(g_px == 0 && g_py == 0 && g_calls == 0);

    /* acceleration: a fast drag covers far more pixels than a slow one of equal duration */
    engine_init(&e, &c, sink, 0);
    long slow = drag(&e, 20.0, 500);    /* 20 mm/s, 0.5 s -> 10 mm finger  */
    engine_init(&e, &c, sink, 0);
    long fast = drag(&e, 200.0, 500);   /* 200 mm/s, 0.5 s -> 100 mm finger */
    printf("slow(20mm/s,0.5s)=%ld px   fast(200mm/s,0.5s)=%ld px\n", slow, fast);
    assert(fast > slow * 5);

    /* gain (px per mm of finger travel) rises with speed */
    double gslow = slow / 10.0;
    double gfast = fast / 100.0;
    printf("gain  slow=%.1f px/mm   fast=%.1f px/mm\n", gslow, gfast);
    assert(gfast > gslow);

    /* sub-pixel: slow motion still produces output (no per-event rounding loss) */
    engine_init(&e, &c, sink, 0);
    long many = drag(&e, 10.0, 1000);   /* 10 mm/s, 1 s -> 10 mm */
    printf("sub-pixel: 10 mm at 10 mm/s accumulates to %ld px\n", many);
    assert(many > 0);

    /* momentum: a two-finger flick then release -> inertial scroll that decays at ~tau */
    {
        engine_init(&e, &c, sink, 0);
        g_wheel = 0;
        uint32_t t = 0;
        for (int i = 0; i < 12; i++) {  /* ~100 ms flick, 5 units / 8 ms */
            engine_feed_scroll(&e, SRC_LEFT_PAD, (int32_t)(5 << MM_Q), 8000);
            t += 8000;
        }
        long active = g_wheel;
        assert(active > 0);
        engine_scroll_end(&e, SRC_LEFT_PAD, t);

        long before = g_wheel;
        uint32_t coast0 = t;
        long cum[400];
        uint32_t tms[400];
        int nt = 0;
        for (int k = 0; k < 400; k++) {
            t += 8000;
            int alive = engine_tick(&e, t);
            cum[nt] = g_wheel - before;
            tms[nt] = (t - coast0) / 1000;
            nt++;
            if (!alive) {
                break;
            }
        }
        long coasted = g_wheel - before;
        int coast_ms = (int)tms[nt - 1];
        int t_half = -1;
        for (int k = 0; k < nt; k++) {
            if (cum[k] * 2 >= coasted) {
                t_half = (int)tms[k];
                break;
            }
        }
        printf("momentum: active=%ld, coasted=%ld units over %d ms, half-dist at %d ms "
               "(tau*ln2 = 156 ms)\n", active, coasted, coast_ms, t_half);
        assert(coasted > 0);
        assert(coast_ms > 300 && coast_ms < 2000);
        assert(t_half > 90 && t_half < 260);
    }

    /* gestures */
    {
        uint32_t t;

        /* 1-finger tap -> left click */
        engine_init(&e, &c, sink, 0);
        g_left = g_right = g_btn_prev = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(50), MM(50), 0);
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_UP, MM(50), MM(50), 80000);
        assert(g_left == 1 && g_right == 0);

        /* 1-finger move -> pointer motion, no click */
        engine_init(&e, &c, sink, 0);
        g_left = g_right = g_btn_prev = 0;
        g_px = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(20), MM(20), t);
        for (int i = 0; i < 10; i++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(20) + (i + 1) * MM(2), MM(20), t);
        }
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_UP, MM(20) + MM(20), MM(20), t + 8000);
        assert(g_px > 0 && g_left == 0);

        /* 2-finger scroll -> wheel + momentum, no click */
        engine_init(&e, &c, sink, 0);
        g_left = g_right = g_btn_prev = 0;
        g_wheel = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(40), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_DOWN, MM(50), MM(40), t);
        for (int i = 0; i < 10; i++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(40), MM(40) + (i + 1) * MM(2), t);
            engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_MOVE, MM(50), MM(40) + (i + 1) * MM(2), t);
        }
        long sc_active = g_wheel;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_UP, MM(40), MM(40) + MM(20), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_UP, MM(50), MM(40) + MM(20), t);
        assert(sc_active != 0 && g_left == 0 && g_right == 0);
        long sc_before = g_wheel;
        for (int k = 0; k < 400; k++) {
            t += 8000;
            if (!engine_tick(&e, t)) {
                break;
            }
        }
        assert(g_wheel - sc_before != 0);

        /* 2-finger tap -> right click */
        engine_init(&e, &c, sink, 0);
        g_left = g_right = g_btn_prev = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(40), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_DOWN, MM(50), MM(40), t);
        t += 60000;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_UP, MM(40), MM(40), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_UP, MM(50), MM(40), t);
        assert(g_right == 1 && g_left == 0);

        printf("gestures: tap->L, move->pointer, 2f-scroll->wheel+coast, 2f-tap->R  OK\n");
    }

    /* zoom + rotate */
    {
        uint32_t t;

        /* single-pad pinch (two fingers apart on one pad) -> zoom */
        engine_init(&e, &c, sink, 0);
        g_zoom = g_rotate = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(50), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_DOWN, MM(60), MM(50), t);
        for (int k = 1; k <= 10; k++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(40) - k * MM(1), MM(50), t);
            engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_MOVE, MM(60) + k * MM(1), MM(50), t);
        }
        assert(g_zoom > 0);  /* fingers moved apart */

        /* single-pad rotation (fingers rotate around centroid) -> rotate */
        engine_init(&e, &c, sink, 0);
        g_zoom = g_rotate = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(50), t);
        engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_DOWN, MM(60), MM(50), t);
        for (int k = 1; k <= 10; k++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(40), MM(50) - k * MM(1), t);
            engine_feed_contact(&e, SRC_LEFT_PAD, 2, GC_MOVE, MM(60), MM(50) + k * MM(1), t);
        }
        assert(g_rotate != 0);

        /* two-hand zoom: left pad finger moves left, right pad finger moves right */
        engine_init(&e, &c, sink, 0);
        g_zoom = g_rotate = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(40), t);
        engine_feed_contact(&e, SRC_RIGHT_PAD, 2, GC_DOWN, MM(40), MM(40), t);
        for (int k = 1; k <= 10; k++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(40) - k * MM(2), MM(40), t);
            engine_feed_contact(&e, SRC_RIGHT_PAD, 2, GC_MOVE, MM(40) + k * MM(2), MM(40), t);
        }
        assert(g_zoom > 0);

        /* two-hand rotate: left finger up, right finger down */
        engine_init(&e, &c, sink, 0);
        g_zoom = g_rotate = 0;
        t = 0;
        engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_DOWN, MM(40), MM(40), t);
        engine_feed_contact(&e, SRC_RIGHT_PAD, 2, GC_DOWN, MM(40), MM(40), t);
        for (int k = 1; k <= 10; k++) {
            t += 8000;
            engine_feed_contact(&e, SRC_LEFT_PAD, 1, GC_MOVE, MM(40), MM(40) - k * MM(2), t);
            engine_feed_contact(&e, SRC_RIGHT_PAD, 2, GC_MOVE, MM(40), MM(40) + k * MM(2), t);
        }
        assert(g_rotate != 0);

        printf("zoom/rotate: 1pad-pinch->zoom, 1pad-rotate, 2hand-zoom, 2hand-rotate  OK\n");
    }

    printf("test_engine: OK\n");
    return 0;
}

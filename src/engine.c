#include "engine.h"

/* Output is finger_mm (Q16.16) * gain (Q8.8) = pixels in Q24. */
#define OUT_Q (MM_Q + GAIN_Q)

/* exp(-x) in Q16, x = i * 8/64, i = 0..64. For momentum decay without float. */
#define EXPN_N 65
#define EXPN_XMAX 8
static const uint16_t EXPN[EXPN_N] = {
    65535, 57835, 51039, 45042, 39750, 35079, 30957, 27319,
    24109, 21276, 18776, 16570, 14623, 12905, 11388, 10050,
    8869, 7827, 6907, 6096, 5380, 4747, 4190, 3697,
    3263, 2879, 2541, 2243, 1979, 1746, 1541, 1360,
    1200, 1059, 935, 825, 728, 642, 567, 500,
    442, 390, 344, 303, 268, 236, 209, 184,
    162, 143, 127, 112, 99, 87, 77, 68,
    60, 53, 47, 41, 36, 32, 28, 25,
    22,
};

static uint32_t exp_neg_q16(uint32_t elapsed_us, uint32_t tau_us) {
    uint64_t num = (uint64_t)elapsed_us * (EXPN_N - 1);
    uint64_t den = (uint64_t)tau_us * EXPN_XMAX;
    uint32_t idx = (uint32_t)(num / den);
    if (idx >= EXPN_N - 1) {
        return 0;
    }
    uint32_t rem = (uint32_t)(num - (uint64_t)idx * den);
    uint32_t frac = (uint32_t)((uint64_t)rem * 256 / den);  /* 0..256 */
    int32_t g0 = EXPN[idx], g1 = EXPN[idx + 1];
    return (uint32_t)(g0 + (((g1 - g0) * (int32_t)frac) >> 8));
}

static void emit_wheel(engine_t *e, int32_t delta_units_q16) {
    e->wheel_acc += delta_units_q16;
    int32_t w = (int32_t)(e->wheel_acc >> MM_Q);
    e->wheel_acc -= (int64_t)w << MM_Q;
    if (w) {
        hid_report_t r = {0};
        r.wheel = (int16_t)w;
        e->out(&r, e->out_user);
    }
}

void engine_init(engine_t *e, const engine_cfg_t *cfg, engine_out_fn out, void *user) {
    e->cfg = cfg;
    e->out = out;
    e->out_user = user;
    e->acc_x = 0;
    e->acc_y = 0;
    e->scroll_v = 0;
    e->coast_v0 = 0;
    e->coast_vprev = 0;
    e->wheel_acc = 0;
    e->coast_t0_us = 0;
    e->coasting = 0;

    for (int i = 0; i < 3; i++) {
        e->gc[i].used = 0;
    }
    e->gc_n = 0;
    e->max_fingers = 0;
    e->did_scroll = 0;
    e->gbtn = 0;
    e->drag = 0;
    e->gesture_t0 = 0;
    e->gesture_moved = 0;
    e->last_tap_t = 0;
    e->cen_x = e->cen_y = 0;
    e->cen_t = 0;
    e->zoom_acc = 0;
    e->rotate_acc = 0;
}

void engine_set_config(engine_t *e, const engine_cfg_t *cfg) {
    e->cfg = cfg;
}

/* Approximate |(a,b)| without sqrt: max + 0.5*min (alpha-max-beta-min). Good
 * enough for picking a speed bucket; exact on a single-axis move. */
static int32_t amag(int32_t a, int32_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    int32_t mx = a > b ? a : b;
    int32_t mn = a > b ? b : a;
    return mx + (mn >> 1);
}

/* gain(px/mm, Q8.8) at a finger speed (mm/s), linearly interpolated in the LUT. */
static uint16_t lut_gain(const engine_cfg_t *c, int32_t speed_mm_s) {
    if (speed_mm_s <= 0) {
        return c->accel_lut[0];
    }
    int64_t pos = (int64_t)speed_mm_s * (ACCEL_LUT_N - 1);
    int32_t idx = (int32_t)(pos / c->accel_max_mm_s);
    if (idx >= ACCEL_LUT_N - 1) {
        return c->accel_lut[ACCEL_LUT_N - 1];
    }
    int32_t rem = (int32_t)(pos % c->accel_max_mm_s);
    int32_t frac = (int32_t)((int64_t)rem * 256 / c->accel_max_mm_s);  /* 0..256 */
    int32_t g0 = c->accel_lut[idx];
    int32_t g1 = c->accel_lut[idx + 1];
    return (uint16_t)(g0 + (((g1 - g0) * frac) >> 8));
}

void engine_feed_move(engine_t *e, engine_src_t src,
                      int32_t dx, int32_t dy, uint32_t dt_us) {
    (void)src;  /* ballistics is source-agnostic */
    if (dt_us == 0) {
        dt_us = 1;
    }
    int32_t mag = amag(dx, dy);  /* Q16.16 mm */
    int32_t speed = (int32_t)((int64_t)mag * 1000000LL /
                              ((int64_t)(1 << MM_Q) * dt_us));  /* mm/s */

    uint32_t gain = lut_gain(e->cfg, speed);                    /* Q8.8 px/mm */
    gain = (gain * e->cfg->sensitivity) >> 8;                   /* apply sensitivity (Q8.8) */

    e->acc_x += (int64_t)dx * (int32_t)gain;                    /* Q24 px */
    e->acc_y += (int64_t)dy * (int32_t)gain;

    int32_t px = (int32_t)(e->acc_x >> OUT_Q);
    int32_t py = (int32_t)(e->acc_y >> OUT_Q);
    e->acc_x -= (int64_t)px << OUT_Q;
    e->acc_y -= (int64_t)py << OUT_Q;

    if (px || py) {
        hid_report_t r = {0};
        r.dx = (int16_t)px;
        r.dy = (int16_t)py;
        e->out(&r, e->out_user);
    }
}

void engine_feed_scroll(engine_t *e, engine_src_t src, int32_t d_units, uint32_t dt_us) {
    (void)src;
    if (dt_us == 0) {
        dt_us = 1;
    }
    e->coasting = 0;                      /* active scroll cancels any coast */
    emit_wheel(e, d_units);               /* pass active scroll through */

    int32_t inst_v = (int32_t)((int64_t)d_units * 1000000LL / dt_us);  /* Q16.16 units/s */
    int32_t alpha = e->cfg->filter_alpha ? e->cfg->filter_alpha : 26214;  /* ~0.4 Q16 */
    e->scroll_v += (int32_t)(((int64_t)(inst_v - e->scroll_v) * alpha) >> 16);
}

void engine_scroll_end(engine_t *e, engine_src_t src, uint32_t now_us) {
    (void)src;
    int32_t vmag = e->scroll_v < 0 ? -e->scroll_v : e->scroll_v;
    if ((vmag >> MM_Q) >= e->cfg->momentum_min) {
        e->coasting = 1;
        e->coast_v0 = e->scroll_v;
        e->coast_vprev = e->scroll_v;
        e->coast_t0_us = now_us;
    } else {
        e->coasting = 0;
    }
}

int engine_tick(engine_t *e, uint32_t now_us) {
    if (!e->coasting) {
        return 0;
    }
    uint32_t tau_us = (uint32_t)e->cfg->momentum_tau_ms * 1000u;
    if (tau_us == 0) {
        e->coasting = 0;
        return 0;
    }
    uint32_t elapsed = now_us - e->coast_t0_us;
    uint32_t ex = exp_neg_q16(elapsed, tau_us);                         /* Q16 */
    int32_t v_cur = (int32_t)(((int64_t)e->coast_v0 * ex) >> 16);        /* Q16.16 units/s */

    /* displacement since last tick = tau * (v_prev - v_cur) */
    int64_t dv = (int64_t)e->coast_vprev - v_cur;
    int32_t delta = (int32_t)(dv * (int32_t)e->cfg->momentum_tau_ms / 1000);  /* Q16.16 units */
    e->coast_vprev = v_cur;
    emit_wheel(e, delta);

    int32_t vmag = v_cur < 0 ? -v_cur : v_cur;
    if (v_cur == 0 || (vmag >> MM_Q) < e->cfg->momentum_min) {
        e->coasting = 0;
        return 0;
    }
    return 1;
}

/* ---- gesture recognizer ---- */
#define TAP_MOVE_Q 131072  /* ~2 mm in Q16.16: max finger travel for a tap */

static int gc_find(engine_t *e, uint8_t id) {
    for (int i = 0; i < 3; i++) {
        if (e->gc[i].used && e->gc[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int gc_alloc(engine_t *e, uint8_t id) {
    for (int i = 0; i < 3; i++) {
        if (!e->gc[i].used) {
            e->gc[i].used = 1;
            e->gc[i].id = id;
            return i;
        }
    }
    return -1;
}

static void emit_button(engine_t *e, uint8_t btn) {
    if (btn != e->gbtn) {
        e->gbtn = btn;
        hid_report_t r = {0};
        r.buttons = btn;
        e->out(&r, e->out_user);
    }
}

static void click(engine_t *e, uint8_t mask) {
    hid_report_t down = {0};
    hid_report_t up = {0};
    down.buttons = mask;
    e->out(&down, e->out_user);
    e->out(&up, e->out_user);
}

static void emit_zoom(engine_t *e, int32_t d_q16) {
    int32_t gain = e->cfg->two_hand_zoom_gain ? e->cfg->two_hand_zoom_gain : 256;
    e->zoom_acc += ((int64_t)d_q16 * gain) >> 8;
    int32_t z = (int32_t)(e->zoom_acc >> MM_Q);
    e->zoom_acc -= (int64_t)z << MM_Q;
    if (z) {
        hid_report_t r = {0};
        r.zoom = (int16_t)z;
        e->out(&r, e->out_user);
    }
}

static void emit_rotate(engine_t *e, int32_t d_q16) {
    e->rotate_acc += d_q16;
    int32_t rr = (int32_t)(e->rotate_acc >> MM_Q);
    e->rotate_acc -= (int64_t)rr << MM_Q;
    if (rr) {
        hid_report_t r = {0};
        r.rotate = (int16_t)rr;
        e->out(&r, e->out_user);
    }
}

static int gc_other(engine_t *e, int i) {
    for (int k = 0; k < 3; k++) {
        if (e->gc[k].used && k != i) {
            return k;
        }
    }
    return -1;
}

static void centroid(engine_t *e, int32_t *cx, int32_t *cy) {
    int64_t sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < 3; i++) {
        if (e->gc[i].used) {
            sx += e->gc[i].x;
            sy += e->gc[i].y;
            n++;
        }
    }
    *cx = n ? (int32_t)(sx / n) : 0;
    *cy = n ? (int32_t)(sy / n) : 0;
}

void engine_feed_contact(engine_t *e, engine_src_t src, uint8_t id,
                         gesture_phase_t phase, int32_t x, int32_t y, uint32_t now) {
    if (phase == GC_DOWN) {
        if (e->gc_n == 0) {
            e->gesture_t0 = now;
            e->gesture_moved = 0;
            e->max_fingers = 0;
            e->did_scroll = 0;
        }
        int i = gc_alloc(e, id);
        if (i < 0) {
            return;
        }
        e->gc[i].x = x;
        e->gc[i].y = y;
        e->gc[i].moved = 0;
        e->gc[i].src = (uint8_t)src;
        e->gc[i].t_last = now;
        e->gc_n++;
        if (e->gc_n > e->max_fingers) {
            e->max_fingers = e->gc_n;
        }
        if (e->gc_n == 1 && e->last_tap_t != 0 &&
            now - e->last_tap_t < (uint32_t)e->cfg->drag_ms * 1000u) {
            e->drag = 1;  /* tap-and-drag: a fresh press right after a tap */
        }
        if (e->gc_n == 2) {
            centroid(e, &e->cen_x, &e->cen_y);
            e->cen_t = now;
        }
        return;
    }

    int i = gc_find(e, id);
    if (i < 0) {
        return;
    }

    if (phase == GC_MOVE) {
        int32_t oix = e->gc[i].x, oiy = e->gc[i].y;
        uint32_t ot = e->gc[i].t_last;
        int32_t dx = x - oix, dy = y - oiy;
        e->gc[i].moved += amag(dx, dy);
        if (e->gc[i].moved > e->gesture_moved) {
            e->gesture_moved = e->gc[i].moved;
        }
        e->gc[i].x = x;
        e->gc[i].y = y;
        e->gc[i].t_last = now;
        uint32_t dt = now - ot;
        if (!dt) {
            dt = 1;
        }

        if (e->gc_n == 1) {
            if (e->drag && !e->gbtn) {
                emit_button(e, 1);
            }
            engine_feed_move(e, src, dx, dy, dt);
            return;
        }
        if (e->gc_n != 2) {
            return;
        }

        int j = gc_other(e, i);
        if (j < 0) {
            return;
        }
        if (e->gc[i].src == e->gc[j].src) {
            /* single pad, two fingers: scroll + pinch + rotate, which separate by geometry */
            int32_t cx, cy;
            centroid(e, &cx, &cy);
            int32_t dcy = cy - e->cen_y;
            int32_t units = dcy * (e->cfg->scroll_step ? e->cfg->scroll_step : 5);
            if (e->cfg->natural_scroll) {
                units = -units;
            }
            if (units) {
                engine_feed_scroll(e, src, units, dt);
                e->did_scroll = 1;
            }
            e->cen_x = cx;
            e->cen_y = cy;
            e->cen_t = now;

            int32_t jx = e->gc[j].x, jy = e->gc[j].y;
            int32_t sep_ox = jx - oix, sep_oy = jy - oiy;  /* separation, before i moved */
            int32_t sep_nx = jx - x, sep_ny = jy - y;       /* separation, after          */
            emit_zoom(e, amag(sep_nx, sep_ny) - amag(sep_ox, sep_oy));  /* distance change */

            int64_t cross = (int64_t)sep_ox * sep_ny - (int64_t)sep_oy * sep_nx;
            int64_t mag2 = (int64_t)sep_ox * sep_ox + (int64_t)sep_oy * sep_oy;
            if (mag2 > 0) {
                int32_t rot = (int32_t)((cross << MM_Q) / mag2);  /* Q16.16 ~ sin(dtheta) */
                emit_rotate(e, rot << 4);
            }
        } else if (e->cfg->two_hand_zoom) {
            /* two hands (one finger per pad): relative motion -> zoom + rotate */
            int32_t zc = (e->gc[i].src == SRC_LEFT_PAD) ? -dx : dx;  /* apart = + */
            int32_t rc = (e->gc[i].src == SRC_LEFT_PAD) ? dy : -dy;  /* opposite vertical */
            emit_zoom(e, zc);
            emit_rotate(e, rc);
        }
        return;
    }

    /* GC_UP */
    e->gc[i].used = 0;
    e->gc_n--;
    if (e->did_scroll && e->gc_n < 2 && !e->coasting && e->scroll_v != 0) {
        engine_scroll_end(e, src, now);
    }
    if (e->gc_n >= 2) {
        centroid(e, &e->cen_x, &e->cen_y);
        e->cen_t = now;
    }
    if (e->gc_n == 0) {
        uint8_t had_btn = e->gbtn;
        if (e->gbtn) {
            emit_button(e, 0);
        }
        e->drag = 0;
        uint32_t gdur = now - e->gesture_t0;
        if (!e->did_scroll && !had_btn && e->gesture_moved < TAP_MOVE_Q &&
            gdur < (uint32_t)e->cfg->tap_ms * 1000u) {
            if (e->max_fingers == 1) {
                click(e, 1);
                e->last_tap_t = now;
            } else if (e->max_fingers == 2 && e->cfg->two_finger_right_click) {
                click(e, 2);
            }
        }
        e->max_fingers = 0;
        e->did_scroll = 0;
    }
}

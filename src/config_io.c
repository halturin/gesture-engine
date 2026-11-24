#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_io.h"

#define CFG_MAGIC 0x4743u  /* "GC" */
#define CFG_VER 1

void cfg_accel_param(engine_cfg_t *c, uint16_t lo, uint16_t hi, uint8_t expn) {
    if (expn < 1) {
        expn = 1;
    }
    for (int i = 0; i < ACCEL_LUT_N; i++) {
        uint32_t x = (uint32_t)i * 65536u / (ACCEL_LUT_N - 1);  /* Q16 in [0,1] */
        uint32_t xp = 65536u;                                   /* Q16 = 1.0   */
        for (int k = 0; k < expn; k++) {
            xp = (uint32_t)(((uint64_t)xp * x) >> 16);
        }
        int32_t g = (int32_t)lo + (int32_t)(((int64_t)(hi - lo) * xp) >> 16);
        c->accel_lut[i] = (uint16_t)(g < 0 ? 0 : (g > 65535 ? 65535 : g));
    }
}

void cfg_defaults(engine_cfg_t *c) {
    memset(c, 0, sizeof *c);
    c->sensitivity = 256;              /* 1.0x */
    c->accel_max_mm_s = 520;
    cfg_accel_param(c, 2 * 256, 61 * 256, 2);  /* Apple-ish: 2..61 px/mm, quadratic */
    c->filter_alpha = 26214;           /* ~0.4 Q16 */
    c->momentum_tau_ms = 225;          /* captured */
    c->momentum_min = 10;
    c->scroll_step = 5;
    c->tap_ms = 180;
    c->drag_ms = 300;
    c->natural_scroll = 1;
    c->two_finger_right_click = 1;
    c->os = 0;
    c->zoom_mode = 0;
    c->two_hand_zoom = 1;
    c->two_hand_zoom_gain = 256;
}

static void w16(uint8_t **p, uint16_t v) {
    (*p)[0] = (uint8_t)v;
    (*p)[1] = (uint8_t)(v >> 8);
    *p += 2;
}

static uint16_t r16(const uint8_t **p) {
    uint16_t v = (uint16_t)((*p)[0] | ((*p)[1] << 8));
    *p += 2;
    return v;
}

int cfg_serialize(const engine_cfg_t *c, uint8_t *buf, size_t len) {
    if (len < (size_t)CFG_BLOB_SIZE) {
        return -1;
    }
    uint8_t *p = buf;
    w16(&p, CFG_MAGIC);
    *p++ = CFG_VER;
    *p++ = ACCEL_LUT_N;
    w16(&p, c->sensitivity);
    w16(&p, c->accel_max_mm_s);
    for (int i = 0; i < ACCEL_LUT_N; i++) {
        w16(&p, c->accel_lut[i]);
    }
    w16(&p, c->filter_alpha);
    w16(&p, c->momentum_tau_ms);
    w16(&p, c->momentum_min);
    w16(&p, c->scroll_step);
    w16(&p, c->tap_ms);
    w16(&p, c->drag_ms);
    *p++ = c->natural_scroll;
    *p++ = c->two_finger_right_click;
    *p++ = c->os;
    *p++ = c->zoom_mode;
    *p++ = c->two_hand_zoom;
    w16(&p, c->two_hand_zoom_gain);
    return (int)(p - buf);
}

int cfg_deserialize(const uint8_t *buf, size_t len, engine_cfg_t *c) {
    if (len < (size_t)CFG_BLOB_SIZE) {
        return -1;
    }
    const uint8_t *p = buf;
    if (r16(&p) != CFG_MAGIC) {
        return -1;
    }
    uint8_t ver = *p++;
    uint8_t n = *p++;
    if (ver != CFG_VER || n != ACCEL_LUT_N) {
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->sensitivity = r16(&p);
    c->accel_max_mm_s = r16(&p);
    for (int i = 0; i < ACCEL_LUT_N; i++) {
        c->accel_lut[i] = r16(&p);
    }
    c->filter_alpha = r16(&p);
    c->momentum_tau_ms = r16(&p);
    c->momentum_min = r16(&p);
    c->scroll_step = r16(&p);
    c->tap_ms = r16(&p);
    c->drag_ms = r16(&p);
    c->natural_scroll = *p++;
    c->two_finger_right_click = *p++;
    c->os = *p++;
    c->zoom_mode = *p++;
    c->two_hand_zoom = *p++;
    c->two_hand_zoom_gain = r16(&p);
    return 0;
}

int cfg_cmd(engine_cfg_t *c, const char *line, char *reply, size_t rlen) {
    char cmd[32] = {0}, a[32] = {0}, b[32] = {0}, d[32] = {0}, ee[32] = {0};
    int n = sscanf(line, "%31s %31s %31s %31s %31s", cmd, a, b, d, ee);
    if (n < 1) {
        snprintf(reply, rlen, "err empty");
        return -1;
    }
    if (!strcmp(cmd, "get")) {
        snprintf(reply, rlen,
                 "sens=%u accel_max=%u tau=%u min=%u scroll=%u tap=%u nat=%u 2hz=%u",
                 c->sensitivity, c->accel_max_mm_s, c->momentum_tau_ms, c->momentum_min,
                 c->scroll_step, c->tap_ms, c->natural_scroll, c->two_hand_zoom);
        return 0;
    }
    if (!strcmp(cmd, "set") && n >= 3) {
        long v = atol(b);
        if (!strcmp(a, "sens")) {
            c->sensitivity = (uint16_t)v;
        } else if (!strcmp(a, "tau")) {
            c->momentum_tau_ms = (uint16_t)v;
        } else if (!strcmp(a, "min")) {
            c->momentum_min = (uint16_t)v;
        } else if (!strcmp(a, "scroll")) {
            c->scroll_step = (uint16_t)v;
        } else if (!strcmp(a, "tap")) {
            c->tap_ms = (uint16_t)v;
        } else if (!strcmp(a, "natural")) {
            c->natural_scroll = (uint8_t)v;
        } else if (!strcmp(a, "zoom")) {
            c->two_hand_zoom = (uint8_t)v;
        } else {
            snprintf(reply, rlen, "err unknown param");
            return -1;
        }
        snprintf(reply, rlen, "ok %s=%ld", a, v);
        return 0;
    }
    if (!strcmp(cmd, "accel") && n >= 5 && !strcmp(a, "param")) {
        cfg_accel_param(c, (uint16_t)atol(b), (uint16_t)atol(d), (uint8_t)atol(ee));
        snprintf(reply, rlen, "ok accel param %s %s %s", b, d, ee);
        return 0;
    }
    if (!strcmp(cmd, "accel") && n >= 3 && !strcmp(a, "max")) {
        c->accel_max_mm_s = (uint16_t)atol(b);
        snprintf(reply, rlen, "ok accel max=%u", c->accel_max_mm_s);
        return 0;
    }
    snprintf(reply, rlen, "err unknown");
    return -1;
}

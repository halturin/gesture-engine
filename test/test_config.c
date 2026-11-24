#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_io.h"

int main(void) {
    engine_cfg_t c, d;
    cfg_defaults(&c);

    /* parametric accel LUT: monotonic, rising, endpoints at lo/hi */
    assert(c.accel_lut[0] == 2 * 256);
    assert(c.accel_lut[ACCEL_LUT_N - 1] == 61 * 256);
    for (int i = 1; i < ACCEL_LUT_N; i++) {
        assert(c.accel_lut[i] >= c.accel_lut[i - 1]);
    }

    /* serialize round-trips exactly */
    uint8_t buf[CFG_BLOB_MAX];
    int n = cfg_serialize(&c, buf, sizeof buf);
    assert(n > 0 && (size_t)n <= sizeof buf);
    assert(cfg_deserialize(buf, (size_t)n, &d) == 0);
    assert(memcmp(&c, &d, sizeof c) == 0);

    /* a short/corrupt blob is rejected */
    assert(cfg_deserialize(buf, 3, &d) < 0);
    uint8_t bad[CFG_BLOB_MAX];
    memcpy(bad, buf, sizeof bad);
    bad[0] ^= 0xFF;  /* break the magic */
    assert(cfg_deserialize(bad, sizeof bad, &d) < 0);

    /* control commands */
    char r[160];
    assert(cfg_cmd(&c, "set tau 300", r, sizeof r) == 0 && c.momentum_tau_ms == 300);
    assert(cfg_cmd(&c, "set natural 0", r, sizeof r) == 0 && c.natural_scroll == 0);
    assert(cfg_cmd(&c, "get", r, sizeof r) == 0);
    printf("get -> %s\n", r);

    uint16_t top_before = c.accel_lut[ACCEL_LUT_N - 1];
    assert(cfg_cmd(&c, "accel param 512 20000 3", r, sizeof r) == 0);
    assert(c.accel_lut[0] == 512);
    assert(c.accel_lut[ACCEL_LUT_N - 1] == 20000);
    assert(c.accel_lut[ACCEL_LUT_N - 1] != top_before);
    for (int i = 1; i < ACCEL_LUT_N; i++) {
        assert(c.accel_lut[i] >= c.accel_lut[i - 1]);
    }

    assert(cfg_cmd(&c, "accel max 600", r, sizeof r) == 0 && c.accel_max_mm_s == 600);
    assert(cfg_cmd(&c, "frobnicate", r, sizeof r) < 0);
    assert(cfg_cmd(&c, "set nope 1", r, sizeof r) < 0);

    printf("test_config: OK\n");
    return 0;
}

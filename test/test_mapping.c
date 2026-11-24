#include <assert.h>
#include <stdio.h>
#include "config_io.h"
#include "mapping.h"

static long m_dx, m_wheel;
static int m_buttons;
static void mouse_sink(int16_t dx, int16_t dy, int16_t wheel, int16_t pan,
                       uint8_t b, void *u) {
    (void)u; (void)dy; (void)pan;
    m_dx += dx;
    m_wheel += wheel;
    m_buttons |= b;
}

static int k_ctrl_down, k_ctrl_up;
static void key_sink(uint8_t mod, uint8_t kc, uint8_t pr, void *u) {
    (void)u; (void)kc;
    if ((mod & HID_MOD_CTRL) && pr) {
        k_ctrl_down++;
    }
    if ((mod & HID_MOD_CTRL) && !pr) {
        k_ctrl_up++;
    }
}

int main(void) {
    engine_cfg_t c;
    cfg_defaults(&c);
    mapper_t m;
    map_init(&m, &c, mouse_sink, key_sink, 0);

    /* pointer + buttons pass through */
    m_dx = m_wheel = m_buttons = 0;
    k_ctrl_down = k_ctrl_up = 0;
    hid_report_t pr = {0};
    pr.dx = 5;
    pr.buttons = 1;
    map_report(&m, &pr);
    assert(m_dx == 5 && (m_buttons & 1));

    /* zoom -> Ctrl held once + wheel per report, released on idle */
    hid_report_t z = {0};
    z.zoom = 3;
    map_report(&m, &z);
    assert(k_ctrl_down == 1 && m_wheel == 3);
    map_report(&m, &z);
    assert(k_ctrl_down == 1 && m_wheel == 6);
    map_idle(&m);
    assert(k_ctrl_up == 1 && m.zoom_mod_held == 0);

    /* OS defaults */
    map_os_defaults(&c, 0);
    assert(c.natural_scroll == 1 && c.os == 0);
    map_os_defaults(&c, 1);
    assert(c.natural_scroll == 0 && c.os == 1);

    printf("test_mapping: OK\n");
    return 0;
}

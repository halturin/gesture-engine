#include "mapping.h"

void map_init(mapper_t *m, const engine_cfg_t *cfg,
              map_mouse_fn mouse, map_key_fn key, void *user) {
    m->cfg = cfg;
    m->mouse = mouse;
    m->key = key;
    m->user = user;
    m->zoom_mod_held = 0;
}

void map_report(mapper_t *m, const hid_report_t *r) {
    if (r->dx || r->dy || r->wheel || r->pan || r->buttons) {
        m->mouse(r->dx, r->dy, r->wheel, r->pan, r->buttons, m->user);
    }
    if (r->zoom) {
        if (!m->zoom_mod_held) {
            m->key(HID_MOD_CTRL, 0, 1, m->user);  /* hold Ctrl for the zoom */
            m->zoom_mod_held = 1;
        }
        m->mouse(0, 0, r->zoom, 0, 0, m->user);   /* Ctrl+scroll = zoom */
    }
    /* r->rotate is carried; binding it to a key/combo is a device/app choice. */
}

void map_idle(mapper_t *m) {
    if (m->zoom_mod_held) {
        m->key(HID_MOD_CTRL, 0, 0, m->user);
        m->zoom_mod_held = 0;
    }
}

void map_os_defaults(engine_cfg_t *c, uint8_t os) {
    c->os = os;
    c->natural_scroll = (os == 0) ? 1 : 0;  /* macOS natural on, Windows traditional off */
    c->two_finger_right_click = 1;
}

#ifndef GE_MAPPING_H
#define GE_MAPPING_H
#include <stdint.h>
#include "config.h"
#include "events.h"

/* L5 output mapping: engine reports -> actual host HID, with OS-specific choices.
 * Pointer / buttons / scroll pass straight to the mouse HID; zoom becomes
 * Ctrl+scroll (zoom in most apps on macOS/Windows/Linux); rotate is carried for
 * a device/app-specific binding (there is no universal HID rotate). */

#define HID_MOD_CTRL 0x01  /* HID keyboard left-Ctrl modifier bit */

typedef void (*map_mouse_fn)(int16_t dx, int16_t dy, int16_t wheel, int16_t pan,
                             uint8_t buttons, void *user);
typedef void (*map_key_fn)(uint8_t modifiers, uint8_t keycode, uint8_t pressed, void *user);

typedef struct {
    const engine_cfg_t *cfg;
    map_mouse_fn mouse;
    map_key_fn key;
    void *user;
    uint8_t zoom_mod_held;
} mapper_t;

void map_init(mapper_t *m, const engine_cfg_t *cfg,
              map_mouse_fn mouse, map_key_fn key, void *user);

/* Translate one engine report into host HID. Set the mapper as the engine's out. */
void map_report(mapper_t *m, const hid_report_t *r);

/* Gesture idle (all fingers up): release any held modifier (the Ctrl held for zoom). */
void map_idle(mapper_t *m);

/* OS-dependent config defaults (natural scroll, etc.). os: 0 macOS, 1 Windows. */
void map_os_defaults(engine_cfg_t *c, uint8_t os);

#endif

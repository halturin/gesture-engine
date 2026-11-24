#ifndef GE_EVENTS_H
#define GE_EVENTS_H
#include <stdint.h>

/* Input is tagged by source so multi-source gestures (e.g. two-hand zoom) can
 * read both pads. */
typedef enum {
    SRC_LEFT_PAD = 0,
    SRC_RIGHT_PAD = 1,
    SRC_DIAL = 2,
} engine_src_t;

/* Engine output: HID-ready deltas plus zoom/rotate signals. The L5/L0 mapping
 * turns zoom/rotate into actual HID (e.g. ctrl+scroll, a keybind). */
typedef struct {
    int16_t dx, dy;      /* pointer delta, pixels            */
    int16_t wheel, pan;  /* scroll, wheel units              */
    int16_t zoom;        /* pinch / two-hand zoom signal     */
    int16_t rotate;      /* rotate signal                    */
    uint8_t buttons;     /* bit0 = left, bit1 = right        */
} hid_report_t;

#endif

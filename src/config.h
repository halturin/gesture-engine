#ifndef GE_CONFIG_H
#define GE_CONFIG_H
#include <stdint.h>

/* Runtime-tunable engine config. All behavior lives here; the core has no
 * hard-coded tunables. Serialized to a blob for the control port and NVS. */

#define ACCEL_LUT_N 64

typedef struct {
    uint16_t sensitivity;              /* Q8.8 overall scale, 256 = 1.0x          */
    uint16_t accel_lut[ACCEL_LUT_N];   /* gain px/mm, Q8.8; index = speed bucket  */
    uint16_t accel_max_mm_s;           /* finger speed mapped to the top bucket   */
    uint16_t filter_alpha;             /* scroll-velocity IIR, Q16                */
    uint16_t momentum_tau_ms;          /* inertial-scroll decay constant          */
    uint16_t momentum_min;             /* momentum stop threshold, units/s        */
    uint16_t scroll_step;              /* scroll units per mm of two-finger travel*/
    uint16_t tap_ms;                   /* max tap duration                        */
    uint16_t drag_ms;                  /* tap-and-drag window after a tap          */
    uint8_t  natural_scroll;
    uint8_t  two_finger_right_click;
    uint8_t  os;                       /* 0 = macOS, 1 = Windows (for defaults)   */
    uint8_t  zoom_mode;                /* reserved: how L5 maps zoom              */
    uint8_t  two_hand_zoom;            /* enable the two-hand gestures            */
    uint16_t two_hand_zoom_gain;       /* zoom/pinch scale, Q8.8                   */
} engine_cfg_t;

#endif

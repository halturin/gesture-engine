#ifndef GE_ENGINE_H
#define GE_ENGINE_H
#include "config.h"
#include "events.h"

/* Fixed-point: finger displacement is Q16.16 mm, gain is Q8.8 px/mm. */
#define MM_Q 16
#define GAIN_Q 8
#define MM(x) ((int32_t)((x) * (double)(1 << MM_Q)))  /* mm -> Q16.16 (test helper) */

typedef void (*engine_out_fn)(const hid_report_t *rep, void *user);

typedef struct {
    const engine_cfg_t *cfg;
    engine_out_fn out;
    void *out_user;
    int64_t acc_x, acc_y;    /* pointer sub-pixel accumulators, Q24 pixels       */

    /* scroll + momentum */
    int32_t scroll_v;        /* smoothed scroll velocity, Q16.16 units/s         */
    int32_t coast_v0;        /* velocity at momentum start, Q16.16               */
    int32_t coast_vprev;     /* velocity at the last tick, Q16.16                */
    int64_t wheel_acc;       /* sub-unit wheel accumulator, Q16.16               */
    uint32_t coast_t0_us;    /* momentum start time                             */
    uint8_t coasting;

    /* gesture recognizer (contact-based) */
    struct { uint8_t used, id, src; int32_t x, y, moved; uint32_t t_last; } gc[3];
    uint8_t gc_n, max_fingers, did_scroll, gbtn, drag;
    uint32_t gesture_t0;
    int32_t gesture_moved;
    uint32_t last_tap_t;
    int32_t cen_x, cen_y;
    uint32_t cen_t;
    int64_t zoom_acc, rotate_acc;  /* sub-unit zoom/rotate accumulators, Q16.16 */
} engine_t;

/* Contact lifecycle for the gesture recognizer. x,y are finger position in mm
 * (Q16.16). Fed by the raw path (per-finger contacts); the cooked module path
 * can synthesize these from its tap/scroll events. */
typedef enum { GC_DOWN = 0, GC_MOVE = 1, GC_UP = 2 } gesture_phase_t;

void engine_init(engine_t *e, const engine_cfg_t *cfg, engine_out_fn out, void *user);
void engine_set_config(engine_t *e, const engine_cfg_t *cfg);

/* Pointer move: finger displacement dx,dy in mm (Q16.16), dt in microseconds. */
void engine_feed_move(engine_t *e, engine_src_t src,
                      int32_t dx_mm_q16, int32_t dy_mm_q16, uint32_t dt_us);

/* Active two-finger scroll: d_units is scroll motion this frame (Q16.16 units),
 * dt in microseconds. Passes through to the wheel and tracks velocity. */
void engine_feed_scroll(engine_t *e, engine_src_t src, int32_t d_units_q16, uint32_t dt_us);

/* Fingers lifted: arm inertial scroll from the current velocity. */
void engine_scroll_end(engine_t *e, engine_src_t src, uint32_t now_us);

/* Advance momentum; emits decaying wheel events. Returns 1 while still coasting
 * (keep ticking), 0 when it has stopped (nothing scheduled, the MCU can sleep). */
int engine_tick(engine_t *e, uint32_t now_us);

/* Per-finger contact for the gesture recognizer: tap->click, drag, two-finger
 * scroll (+momentum) and two-finger tap->right-click. */
void engine_feed_contact(engine_t *e, engine_src_t src, uint8_t id,
                         gesture_phase_t phase, int32_t x_mm_q16, int32_t y_mm_q16,
                         uint32_t now_us);

#endif

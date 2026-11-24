#ifndef GE_CONFIG_IO_H
#define GE_CONFIG_IO_H
#include <stddef.h>
#include "config.h"

/* Config channel (DESIGN.md §11): defaults, a parametric accel generator, a
 * compact serialized blob (for the CDC control port and NVS), and a one-line
 * text command protocol. All pure and host-testable; the CDC/NVS wiring is a
 * thin device adapter on top. */

void cfg_defaults(engine_cfg_t *c);

/* Generate the accel LUT from a few knobs: gain rises lo..hi over 0..accel_max
 * with integer exponent expn (slow start for expn > 1). lo, hi are Q8.8 px/mm. */
void cfg_accel_param(engine_cfg_t *c, uint16_t lo_q8, uint16_t hi_q8, uint8_t expn);

#define CFG_BLOB_SIZE (27 + 2 * ACCEL_LUT_N)  /* exact serialized length */
#define CFG_BLOB_MAX (CFG_BLOB_SIZE + 16)      /* buffer size with headroom */
int cfg_serialize(const engine_cfg_t *c, uint8_t *buf, size_t len);   /* bytes, or <0 */
int cfg_deserialize(const uint8_t *buf, size_t len, engine_cfg_t *c); /* 0, or <0     */

/* One control line. Mutates c, writes a text reply. Returns 0 ok, <0 error.
 * get | set <name> <val> | accel param <lo> <hi> <exp> | accel max <mm_s>. */
int cfg_cmd(engine_cfg_t *c, const char *line, char *reply, size_t rlen);

#endif

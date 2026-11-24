#!/usr/bin/env python3
"""Generate reference/apple_lut.h: the captured Apple accel curve as a Q8.8 LUT
that the engine (and the reference test) load. Resamples apple_accel.csv's
gain(finger_speed) onto ACCEL_LUT_N uniform speed buckets over 0..MAX mm/s.
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.normpath(os.path.join(HERE, "..", "reference"))
N = 64
MAX = 520  # mm/s mapped to the top bucket (the cleanly captured range)


def main():
    pts = []
    with open(os.path.join(REF, "apple_accel.csv")) as fh:
        for row in csv.DictReader(fh):
            pts.append((float(row["finger_mm_s"]), float(row["gain_px_per_mm"])))
    pts.sort()
    if not pts:
        raise SystemExit("apple_accel.csv is empty; capture the accel reference first")

    def gain_at(s):
        if s <= pts[0][0]:
            return pts[0][1]
        if s >= pts[-1][0]:
            return pts[-1][1]
        for i in range(1, len(pts)):
            if pts[i][0] >= s:
                (x0, y0), (x1, y1) = pts[i - 1], pts[i]
                t = (s - x0) / (x1 - x0) if x1 > x0 else 0.0
                return y0 + (y1 - y0) * t
        return pts[-1][1]

    lut = []
    for i in range(N):
        s = MAX * i / (N - 1)
        q = round(gain_at(s) * 256)
        lut.append(max(0, min(65535, q)))
    # Pointer-accel gain is physically monotonic; measured dips are noise. Enforce
    # non-decreasing gain so the LUT carries the clean trend, not sampling jitter.
    for i in range(1, N):
        if lut[i] < lut[i - 1]:
            lut[i] = lut[i - 1]

    out = os.path.join(REF, "apple_lut.h")
    with open(out, "w") as fh:
        fh.write("// Generated from apple_accel.csv by tools/gen_config.py.\n")
        fh.write("// Apple pointer-accel curve as Q8.8 px/mm, uniform speed buckets.\n")
        fh.write("#ifndef APPLE_LUT_H\n#define APPLE_LUT_H\n#include <stdint.h>\n")
        fh.write(f"#define APPLE_ACCEL_MAX_MM_S {MAX}\n")
        fh.write(f"static const uint16_t APPLE_ACCEL_LUT[{N}] = {{\n  ")
        fh.write(", ".join(str(v) for v in lut))
        fh.write("\n};\n#endif\n")
    print(f"wrote {out}: {N} buckets, max {MAX} mm/s, "
          f"gain {lut[0] / 256:.1f}..{lut[-1] / 256:.1f} px/mm")


if __name__ == "__main__":
    main()

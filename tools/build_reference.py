#!/usr/bin/env python3
"""Build golden reference curves from raw captures in reference/raw/.

Currently produces the pointer acceleration curve:
  finger speed (mm/s)  ->  cursor speed (px/s)
into reference/apple_accel.csv. Both speeds are computed by differencing
position over the same frame interval, so they are directly comparable.

Usage: build_reference.py [accel]
"""
import glob
import json
import math
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.normpath(os.path.join(HERE, "..", "reference"))
RAW = os.path.join(REF, "raw")
PAD_MM = (160.0, 114.0)  # Magic Trackpad 2 active area


def load(pattern):
    rows = []
    for path in sorted(glob.glob(os.path.join(RAW, pattern))):
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if line:
                    rows.append(json.loads(line))
    return rows


def accel():
    samples = load("accel-*.jsonl")
    if not samples:
        print("No accel-*.jsonl in reference/raw/. Capture first.")
        return
    # Accumulate inside a continuous single-finger pass, emit a sample once the cursor
    # has moved enough whole pixels (clean gain) OR a max window elapsed. Adaptive: at
    # slow speed the window stretches to gather >= MIN_PX (kills sub-pixel noise); at
    # fast speed MIN_PX is reached in one frame, so resolution stays fine.
    MIN_PX = 6.0
    MAX_WINDOW = 0.15
    pairs = []  # (finger_mm_s, cursor_px_s)
    acc_t = 0.0
    acc_f = [0.0, 0.0]
    acc_c = [0.0, 0.0]
    prev = None
    for s in samples:
        cur = None
        if s.get("n") == 1:
            f = s["fingers"][0]
            cur = (s["t"], f[2], f[3], s["cursor"][0], s["cursor"][1], f[0])  # t,nx,ny,cx,cy,id
        if cur and prev and cur[5] == prev[5] and 0.0 < cur[0] - prev[0] <= 0.05:
            acc_t += cur[0] - prev[0]
            acc_f[0] += (cur[1] - prev[1]) * PAD_MM[0]
            acc_f[1] += (cur[2] - prev[2]) * PAD_MM[1]
            acc_c[0] += cur[3] - prev[3]
            acc_c[1] += cur[4] - prev[4]
            c_dist = math.hypot(acc_c[0], acc_c[1])
            if acc_t >= 0.003 and (c_dist >= MIN_PX or acc_t >= MAX_WINDOW):
                fspd = math.hypot(acc_f[0], acc_f[1]) / acc_t
                cspd = c_dist / acc_t
                if fspd > 0.5:
                    pairs.append((fspd, cspd))
                acc_t, acc_f, acc_c = 0.0, [0.0, 0.0], [0.0, 0.0]
        else:  # gap or new finger: reset the window
            acc_t, acc_f, acc_c = 0.0, [0.0, 0.0], [0.0, 0.0]
        prev = cur

    if len(pairs) < 50:
        print(f"Only {len(pairs)} usable pairs. Capture a longer / more varied sweep.")
        if not pairs:
            return

    fmin = min(p[0] for p in pairs)
    fmax = max(p[0] for p in pairs)
    nbins = 24
    # log-spaced bins over the finger-speed range
    lo, hi = math.log(max(fmin, 1.0)), math.log(fmax)
    edges = [math.exp(lo + (hi - lo) * i / nbins) for i in range(nbins + 1)]
    bins = [[] for _ in range(nbins)]
    for fspd, cspd in pairs:
        k = min(nbins - 1, max(0, int((math.log(fspd) - lo) / (hi - lo) * nbins)))
        bins[k].append((fspd, cspd))

    os.makedirs(REF, exist_ok=True)
    out = os.path.join(REF, "apple_accel.csv")
    rows = []
    for k in range(nbins):
        b = bins[k]
        if len(b) < 3:
            continue
        f_med = statistics.median(p[0] for p in b)
        c_med = statistics.median(p[1] for p in b)
        gain = c_med / f_med if f_med else 0.0
        rows.append((f_med, c_med, gain, len(b)))

    with open(out, "w") as fh:
        fh.write("finger_mm_s,cursor_px_s,gain_px_per_mm,samples\n")
        for f_med, c_med, gain, n in rows:
            fh.write(f"{f_med:.2f},{c_med:.2f},{gain:.3f},{n}\n")

    print(f"pairs: {len(pairs)}  finger speed range: {fmin:.0f}..{fmax:.0f} mm/s")
    print(f"wrote {out} ({len(rows)} bins)")
    print("\n finger_mm_s  cursor_px_s   gain(px/mm)  n")
    for f_med, c_med, gain, n in rows:
        bar = "#" * min(40, int(gain * 2))
        print(f"  {f_med:8.1f}   {c_med:9.1f}   {gain:8.3f}  {n:4d}  {bar}")


def momentum():
    rows = load("scroll-*.jsonl")
    if not rows:
        print("No scroll-*.jsonl. Capture: capture_reference.py scroll")
        return
    # momentum phase != 0 is the inertial tail after the fingers lift (1 begin, 2 continue, 3 end)
    runs, cur = [], []
    for e in rows:
        if e.get("mp", 0) != 0:
            cur.append(e)
        else:
            if len(cur) >= 5:
                runs.append(cur)
            cur = []
    if len(cur) >= 5:
        runs.append(cur)
    if not runs:
        print(f"{len(rows)} scroll events but no momentum runs. Flick harder and let it coast.")
        return
    taus = []
    for run in runs:
        t0 = run[0]["wall"]
        pts = [(e["wall"] - t0, abs(e["dy"])) for e in run if abs(e["dy"]) > 0.3]
        if len(pts) < 5:
            continue
        i0 = max(range(len(pts)), key=lambda i: pts[i][1])  # fit from the peak onward
        tail = [(x, math.log(y)) for x, y in pts[i0:] if y > 0.3]
        if len(tail) < 4:
            continue
        n = len(tail)
        sx = sum(p[0] for p in tail)
        sy = sum(p[1] for p in tail)
        sxx = sum(p[0] * p[0] for p in tail)
        sxy = sum(p[0] * p[1] for p in tail)
        den = n * sxx - sx * sx
        if den == 0:
            continue
        slope = (n * sxy - sx * sy) / den  # ln(v) = ln(v0) - t/tau
        if slope < -1e-6:
            taus.append(-1.0 / slope)
    if not taus:
        print(f"{len(runs)} runs but decay fit failed.")
        return
    tau = statistics.median(taus)
    coast = statistics.median([r[-1]["wall"] - r[0]["wall"] for r in runs])
    out = {"model": "v = v0 * exp(-t/tau)", "tau_ms": round(tau * 1000, 1),
           "median_coast_ms": round(coast * 1000, 1), "runs": len(runs), "fits": len(taus)}
    with open(os.path.join(REF, "apple_momentum.json"), "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"momentum: {len(runs)} runs, tau = {tau * 1000:.0f} ms, "
          f"median coast {coast * 1000:.0f} ms -> apple_momentum.json")


def timings():
    samples = load("tap-*.jsonl")
    if not samples:
        print("No tap-*.jsonl. Capture: capture_reference.py tap")
        return
    active = {}      # finger id -> [t_first, t_last, xlast, ylast, path_mm]
    contacts = []    # (duration_s, movement_mm)
    GAP = 0.12       # a same-id finger reappearing after this gap is a new contact (lift + retap)
    for s in samples:
        now = set()
        for f in s.get("fingers", []):
            fid = f[0]
            x, y = f[2] * PAD_MM[0], f[3] * PAD_MM[1]
            now.add(fid)
            if fid in active and s["t"] - active[fid][1] > GAP:
                a = active.pop(fid)
                contacts.append((a[1] - a[0], a[4]))
            if fid not in active:
                active[fid] = [s["t"], s["t"], x, y, 0.0]
            else:
                a = active[fid]
                a[4] += math.hypot(x - a[2], y - a[3])
                a[1], a[2], a[3] = s["t"], x, y
        for fid in [i for i in active if i not in now]:
            a = active.pop(fid)
            contacts.append((a[1] - a[0], a[4]))
    for fid in list(active):
        a = active.pop(fid)
        contacts.append((a[1] - a[0], a[4]))
    print(f"  contacts: {len(contacts)}; "
          f"durations(ms): {sorted(round(d*1000) for d, _ in contacts)[:20]}")
    taps = sorted(d for d, m in contacts if m < 4.0 and d < 0.45)
    if not taps:
        print(f"{len(contacts)} contacts but none look like taps. Do clean single taps.")
        return
    med = statistics.median(taps)
    p90 = taps[int(0.9 * (len(taps) - 1))]
    out = {"tap_count": len(taps), "tap_ms_median": round(med * 1000, 1),
           "tap_ms_p90": round(p90 * 1000, 1), "tap_ms_max": round(max(taps) * 1000, 1),
           "tap_move_mm_thresh": 3.0}
    with open(os.path.join(REF, "apple_timings.json"), "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"timings: {len(taps)} taps, median {med * 1000:.0f} ms, p90 {p90 * 1000:.0f} ms "
          f"-> apple_timings.json")


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("accel", "all"):
        accel()
    if which in ("momentum", "all"):
        momentum()
    if which in ("timings", "all"):
        timings()
    if which not in ("accel", "momentum", "timings", "all"):
        print(__doc__)


if __name__ == "__main__":
    main()

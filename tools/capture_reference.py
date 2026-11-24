#!/usr/bin/env python3
"""Capture Apple Magic Trackpad reference data for the gesture engine.

Reads raw finger contacts from the private MultitouchSupport.framework and the
cursor position from CoreGraphics, synchronized, with no extra dependencies
(stdlib + ctypes). Writes raw samples under reference/raw/ that build_reference.py
turns into the golden curves.

macOS only. Finger + cursor capture is permission-free. (Scroll/momentum capture
needs an event tap and Input Monitoring permission; added separately.)

Usage:
  capture_reference.py check            verify the trackpad binding (touch the pad)
  capture_reference.py accel [secs]     pointer sweeps      -> raw/accel-<ts>.jsonl
  capture_reference.py tap   [secs]     taps / drags        -> raw/tap-<ts>.jsonl
"""
import ctypes
import json
import os
import sys
import time
from ctypes import (CFUNCTYPE, POINTER, Structure, c_double, c_float, c_int,
                    c_long, c_uint32, c_uint64, c_void_p)

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.normpath(os.path.join(HERE, "..", "reference", "raw"))

# Magic Trackpad 2 active area in millimeters (normalized 0..1 -> mm).
PAD_MM = (160.0, 114.0)

# ---- CoreFoundation + run loop ----
CF = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
CF.CFArrayGetCount.restype = c_long
CF.CFArrayGetCount.argtypes = [c_void_p]
CF.CFArrayGetValueAtIndex.restype = c_void_p
CF.CFArrayGetValueAtIndex.argtypes = [c_void_p, c_long]
CF.CFRunLoopRunInMode.restype = c_int
CF.CFRunLoopRunInMode.argtypes = [c_void_p, c_double, c_int]
CF.CFRelease.argtypes = [c_void_p]
kDefaultMode = c_void_p.in_dll(CF, "kCFRunLoopDefaultMode")

# ---- CoreGraphics cursor (permission-free read) ----
AS = ctypes.CDLL("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices")

class CGPoint(Structure):
    _fields_ = [("x", c_double), ("y", c_double)]

AS.CGEventCreate.restype = c_void_p
AS.CGEventCreate.argtypes = [c_void_p]
AS.CGEventGetLocation.restype = CGPoint
AS.CGEventGetLocation.argtypes = [c_void_p]

def cursor():
    ev = AS.CGEventCreate(None)
    p = AS.CGEventGetLocation(ev)
    CF.CFRelease(ev)
    return (p.x, p.y)

# ---- MultitouchSupport (private) ----
MT = ctypes.CDLL("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport")

class MTPoint(Structure):
    _fields_ = [("x", c_float), ("y", c_float)]

class MTVector(Structure):
    _fields_ = [("pos", MTPoint), ("vel", MTPoint)]

# Finger record layout (widely used reverse-engineered struct). If `check` shows
# garbage (pos outside 0..1, size huge), the layout for this macOS differs and
# this struct is where to fix it.
class MTData(Structure):
    _fields_ = [
        ("frame", c_int), ("timestamp", c_double),
        ("ident", c_int), ("state", c_int), ("u1", c_int), ("u2", c_int),
        ("norm", MTVector), ("size", c_float), ("u3", c_int),
        ("angle", c_float), ("major", c_float), ("minor", c_float),
        ("abs", MTVector), ("u4", c_int), ("u5", c_int), ("density", c_float),
    ]

MTContactCallback = CFUNCTYPE(c_int, c_void_p, POINTER(MTData), c_int, c_double, c_int)
MT.MTDeviceCreateList.restype = c_void_p
MT.MTRegisterContactFrameCallback.argtypes = [c_void_p, MTContactCallback]
MT.MTDeviceStart.argtypes = [c_void_p, c_int]
MT.MTDeviceStop.argtypes = [c_void_p]

def mt_devices():
    lst = MT.MTDeviceCreateList()
    n = CF.CFArrayGetCount(lst)
    return [CF.CFArrayGetValueAtIndex(lst, i) for i in range(n)]

# ---- capture ----
_samples = []

def _callback(device, data, n, ts, frame):
    cx, cy = cursor()
    fingers = []
    for i in range(n):
        f = data[i]
        fingers.append([f.ident, f.state,
                        round(f.norm.pos.x, 5), round(f.norm.pos.y, 5),
                        round(f.norm.vel.x, 4), round(f.norm.vel.y, 4),
                        round(f.size, 3)])
    _samples.append({"t": ts, "wall": time.time(), "cursor": [cx, cy],
                     "n": n, "fingers": fingers})
    return 0

_CB = MTContactCallback(_callback)  # keep alive

def start_all():
    devs = mt_devices()
    for d in devs:
        MT.MTRegisterContactFrameCallback(d, _CB)
        MT.MTDeviceStart(d, 0)
    return devs

def stop_all(devs):
    for d in devs:
        MT.MTDeviceStop(d)

def pump(seconds):
    # rc 1 = no sources on the run loop yet; fall back to chunked pumping
    if CF.CFRunLoopRunInMode(kDefaultMode, seconds, False) == 1:
        end = time.time() + seconds
        while time.time() < end:
            CF.CFRunLoopRunInMode(kDefaultMode, 0.05, False)
            time.sleep(0.005)

def save(prefix):
    return save_rows(prefix, _samples)


def save_rows(prefix, rows):
    os.makedirs(RAW, exist_ok=True)
    path = os.path.join(RAW, f"{prefix}-{int(time.time())}.jsonl")
    with open(path, "w") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    return path


# ---- scroll capture via CGEventTap (momentum; needs Input Monitoring permission) ----
kCGEventScrollWheel = 22
CGEventTapCallBack = CFUNCTYPE(c_void_p, c_void_p, c_uint32, c_void_p, c_void_p)
AS.CGEventTapCreate.restype = c_void_p
AS.CGEventTapCreate.argtypes = [c_uint32, c_uint32, c_uint32, c_uint64, CGEventTapCallBack, c_void_p]
AS.CGEventTapEnable.argtypes = [c_void_p, c_int]
AS.CGEventTapIsEnabled.restype = c_int
AS.CGEventTapIsEnabled.argtypes = [c_void_p]
AS.CGEventGetDoubleValueField.restype = c_double
AS.CGEventGetDoubleValueField.argtypes = [c_void_p, c_uint32]
CF.CFMachPortCreateRunLoopSource.restype = c_void_p
CF.CFMachPortCreateRunLoopSource.argtypes = [c_void_p, c_void_p, c_long]
CF.CFRunLoopGetCurrent.restype = c_void_p
CF.CFRunLoopAddSource.argtypes = [c_void_p, c_void_p, c_void_p]
kCommonModes = c_void_p.in_dll(CF, "kCFRunLoopCommonModes")

# scroll field ids: pixel delta Y/X, scroll phase, momentum phase
F_DY, F_DX, F_SPHASE, F_MPHASE = 96, 97, 99, 123
_scroll = []

def _scroll_cb(proxy, etype, event, userinfo):
    if etype == kCGEventScrollWheel:
        _scroll.append({"wall": time.time(),
                        "dy": AS.CGEventGetDoubleValueField(event, F_DY),
                        "dx": AS.CGEventGetDoubleValueField(event, F_DX),
                        "sp": AS.CGEventGetDoubleValueField(event, F_SPHASE),
                        "mp": AS.CGEventGetDoubleValueField(event, F_MPHASE)})
    return event

_SCB = CGEventTapCallBack(_scroll_cb)

def capture_scroll(seconds):
    tap = AS.CGEventTapCreate(1, 0, 1, c_uint64(1 << kCGEventScrollWheel), _SCB, None)
    if not tap:
        print("Event tap denied. Grant Input Monitoring, then rerun:")
        print("  System Settings > Privacy & Security > Input Monitoring")
        print("  add your terminal app, toggle it on, restart the terminal.")
        return False
    src = CF.CFMachPortCreateRunLoopSource(None, tap, 0)
    CF.CFRunLoopAddSource(CF.CFRunLoopGetCurrent(), src, kCommonModes)
    AS.CGEventTapEnable(tap, 1)
    if not AS.CGEventTapIsEnabled(tap):
        print("Event tap disabled (no Input Monitoring permission).")
        print("  System Settings > Privacy & Security > Input Monitoring -> add your terminal.")
        return False
    print(f"[scroll] capturing {seconds:.0f}s. Two-finger flicks: flick and release, let it coast.")
    pump(seconds)
    return True


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "check"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else (6.0 if mode == "check" else 20.0)

    if mode == "check":
        devs = mt_devices()
        print(f"MT devices found: {len(devs)}")
        if not devs:
            print("No multitouch devices. Is the Magic Trackpad connected?")
            return
        start_all()
        print(f"Touch the Magic Trackpad now, for {secs:.0f}s ...")
        pump(secs)
        stop_all(devs)
        touched = [s for s in _samples if s["n"] > 0]
        print(f"frames: {len(_samples)} total, {len(touched)} with a finger down")
        sane = 0
        for s in touched[:12]:
            f = s["fingers"][0]
            ok = 0.0 <= f[2] <= 1.0 and 0.0 <= f[3] <= 1.0 and f[6] < 100
            sane += ok
            print(f"  n={s['n']} f0 pos=({f[2]:.3f},{f[3]:.3f}) "
                  f"vel=({f[4]:+.2f},{f[5]:+.2f}) size={f[6]:.2f} "
                  f"cursor=({s['cursor'][0]:.0f},{s['cursor'][1]:.0f}) {'OK' if ok else 'BAD'}")
        if touched and sane == 0:
            print("STRUCT LAYOUT LOOKS WRONG (pos not in 0..1). Fix MTData for this macOS.")
        elif touched:
            print("Binding looks good: finger positions are normalized, cursor reads.")
        return

    if mode == "scroll":
        if capture_scroll(secs):
            path = save_rows("scroll", _scroll)
            moms = sum(1 for e in _scroll if e["mp"] != 0)
            print(f"saved {path}: {len(_scroll)} scroll events, {moms} in momentum phase.")
        return

    if mode in ("accel", "tap"):
        devs = start_all()
        hint = ("Do single-finger drags across the pad: several SLOW and several FAST passes."
                if mode == "accel" else
                "Do taps and tap-drags: single taps, two-finger taps, a tap-and-drag.")
        print(f"[{mode}] capturing {secs:.0f}s. {hint}")
        pump(secs)
        stop_all(devs)
        path = save(mode)
        touched = sum(1 for s in _samples if s["n"] > 0)
        print(f"saved {path}: {len(_samples)} frames, {touched} with a finger.")
        return

    print(__doc__)

if __name__ == "__main__":
    main()

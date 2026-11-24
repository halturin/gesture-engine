# Gesture engine for the Naya Create (ZMK)

A portable, fixed-point pointer and gesture engine for the Naya Create split
keyboard running open firmware (ZMK). It turns the Create's Touch modules into
pointer motion, inertial scrolling, taps, pinch zoom and rotation, with a feel
tuned to match a real Apple Magic Trackpad.

The core is self-contained C with no floating point, no heap, and no platform or
ZMK dependencies, so it builds and runs its full test suite on a host machine.
Pointer acceleration and scroll inertia are validated numerically against a curve
captured from a real Magic Trackpad.

## Status

The engine core is complete and host-tested:

- Pointer acceleration, matching the captured Apple curve within 1.6% RMS over 10..490 mm/s.
- Inertial scrolling with exponential decay (tau measured from the trackpad).
- Gestures: tap to click, pointer drag, tap-and-drag, two-finger scroll, two-finger right-click.
- Pinch zoom and rotation on one pad, plus two-hand zoom and rotation across both halves.
- Runtime configuration: a serialized config blob and a text command protocol.

Device integration (parsing the module dock protocol, carrying the peripheral
pad over the split link, the control serial port, NVS persistence, and the ZMK
input processor) is the remaining work. It lives in a ZMK fork, not here.

## Architecture

A pure core (L1 to L6) with thin platform adapters (L0, device only):

```
L0  adapters       dock frames / split events -> contacts;  engine out -> HID
L1  contact model  per-source finger tracking
L2  filter         jitter smoothing
L3  ballistics     non-linear acceleration (LUT) + sub-pixel accumulation
L3  scroll         pass-through + inertial momentum
L4  gestures       tap, drag, two-finger scroll, pinch, rotate, two-hand
L5  mapping        zoom -> ctrl+scroll, natural scroll, macOS/Windows defaults
L6  config         serialization + text command protocol + parametric generator
```

The core is event driven: it does work only on input, and inertia schedules ticks
only while coasting, so an idle device does nothing. Input is tagged by source
(left pad, right pad, dial); the gesture layer reads both pads, which is what makes
two-hand zoom and rotation possible. On a split keyboard the left half is the BLE
central and sees its own pad directly and the right pad over the split link.

## Two-hand zoom and rotation

A split keyboard carries a touch module under each hand, and the engine treats the
two pads as one gesture surface. Rest a finger on each pad and move your hands
apart to zoom in, together to zoom out; move them in opposite directions
vertically to rotate. This is a gesture a single trackpad cannot offer, and it is
more comfortable than pinching two fingers on one small pad.

It works because the left half is the BLE central: it sees its own pad directly
and receives the right pad over the split link, so a single engine instance has
both streams. Every input event is tagged with its source (left pad or right pad),
and the gesture layer reads both. The classic single-pad versions are supported
too: two fingers on one module pinch to zoom and rotate. The two-hand versions are
the same gestures driven by one finger on each pad, computed from the relative
motion of the two contacts rather than their absolute positions, so they work even
on the cooked single-finger stream the stock modules already provide.

Zoom and rotate are separate engine outputs. The mapping layer turns zoom into
Ctrl+scroll, which most applications read as zoom; rotate is carried for a
device-specific binding. Toggle the two-hand gestures with the `two_hand_zoom`
config flag and scale them with `two_hand_zoom_gain`.

## Build and test

```
make test
```

No dependencies beyond a C compiler. The Apple reference LUT is generated from the
captured curve on first build.

## The Apple reference

"Apple-level" is measured, not asserted. The tools capture a real Magic Trackpad
and the engine is scored against it.

- `tools/capture_reference.py` records, on macOS, the trackpad's raw finger
  contacts (via the private MultitouchSupport framework) together with the cursor
  position (via CoreGraphics), with no extra dependencies. From that it derives the
  acceleration curve (finger mm/s to cursor px/s) and the scroll momentum decay.
- `tools/build_reference.py` turns the raw captures into `reference/apple_accel.csv`
  and `reference/apple_momentum.json`.
- `tools/gen_config.py` resamples the curve into the engine's fixed-point LUT
  (`reference/apple_lut.h`).
- `test/test_vs_reference.c` runs the engine against the captured curve and fails
  if it deviates by more than 5% RMS.

## Configuration

All tunable behavior lives in `engine_cfg_t`. `src/config_io.c` provides:

- a compact serialized blob for the control port and NVS,
- a one-line text command protocol: `get`, `set <param> <value>`,
  `accel param <lo> <hi> <exp>`, `accel max <mm/s>`,
- a parametric accel-curve generator, and sane defaults.

The engine reads config live; `engine_set_config` swaps it without a rebuild.

## Layout

```
src/        engine core + headers, config_io.c (config), mapping.c (output)
test/       host test suite (make test)
tools/      reference capture + LUT generator (macOS)
reference/  captured Apple curves + generated LUT
```

## Notes

This is independent, community work for keyboards left behind after Naya ceased
operations. It is not affiliated with or endorsed by Naya. The captured curves are
measurements of pointer behavior, not vendor firmware.

## License

MIT. See [LICENSE](LICENSE).

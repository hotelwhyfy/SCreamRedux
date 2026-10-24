# Camera and view

This page describes the panorama camera of the original SCream engine (`scream.exe`, the game
*Scratches*): how a node is projected, how the player turns, and how zoom images are placed.
It describes only the original engine. The sc* functions are in [`api.md`](api.md) and the
file formats in [`formats.md`](formats.md).

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables listed in [api.md](api.md#sources),
> the shipped scripts decompiled with unluac, and measurements of the shipped files. A person
> has not yet checked it. A source tag says where a claim can be checked; it is not a
> guarantee. Check a claim against its source before you build on it, and treat
> `[unverified]` claims as open questions.

## Sources

The tags are those of [`api.md`](api.md#sources). In short:

| Tag | Means |
|---|---|
| `[exe DC 0x…]` | code or data at that address in the DC update's `scream.exe` (MD5 `f696d90b1f1fe5eb050beda06781dcc8`) |
| `[exe 2006 0x…]` | the same in the patched 2006 `scream.exe` (MD5 `5de7958817eecb3815d816d123ab9217`) |
| `[script DC: name]`, `[script 2006: name]` | that function, or the top level, of the shipped scripts |
| `[data FILE …]` | a shipped file; "data scream.exe @VA" is a constant in the exe image |
| `[unverified: …]` | not confirmed |

Float constants were read from the exe images at the virtual address given. Numbers marked
"derived" are worked out from the cited constants, not read from the exe.

## Summary

| | DC | 2006 |
|---|---|---|
| Default screen | 1024×768 [exe DC 0x40f266] [data dc/scream.cfg] | 800×600 [exe 2006 0x40d3e6] [data 2006/scream.cfg] |
| 3D viewport | 0, 64, W, H−128 [exe DC 0x40e041] | the same [exe 2006 0x40c416] |
| Vertical FOV | `SC_FOV`, default 45° [exe DC 0x40f2b1] | default 53° [exe 2006 0x40d43c] |
| Aspect | W / (H−128) [exe DC 0x40e055–0x40e061] | W / H [exe 2006 0x40c42a–0x40c436] |
| Near, far | 0.1, 1000 [exe DC 0x40e063–0x40e06f] | 0.1, 1000 [exe 2006 0x40c438–0x40c444] |
| Face box | 1 × 1 × 1 (`SC_RATIOX` × `SC_RATIOY`) | 30 × 40 × 30 (`SC_RATIOX` × `SC_RATIOY`) |
| Second camera value | look-vector y | pitch angle in radians |
| Edge-zone turning | yes | yes |
| Alternate control | yes, on by default | no |
| Slideshow mode | yes | yes |

## Projection and viewport

- The 3D view is drawn into a viewport at x 0, y 64 (from the bottom), W wide and H−128 tall
  [exe DC 0x40df60–0x40e041]. This leaves a 64-pixel bar at the top and at the bottom of the
  screen. W and H are the `WIDTH` and `HEIGHT` settings [exe DC 0x40df6d, 0x40df77].
- The projection is a perspective with:
  - vertical field of view = the FOV setting, in degrees;
  - aspect W / (H−128);
  - near plane 0.1, far plane 1000.

  [exe DC 0x40e046–0x40e086]
- At 1024×768 the viewport is 1024×640, aspect 1.6. A 45° vertical FOV then gives about
  67.0° horizontally (derived).
- The FOV is the settings field at offset 0x028 (0x4df8b8c). `scSet(SC_FOV, n)` writes it
  [exe DC 0x40ffc0–0x40ffc6]. The debug overlay prints it as `vision_angle: %d`
  [exe DC 0x40e672–0x40e686].
- The FOV is rewritten **every frame**. With `SC_DREAM` off it is set back to 45, so a
  script's `SC_FOV` never lasts [exe DC 0x407c68, 0x407cee]. No shipped script sets
  `SC_FOV` [script DC: all] [script 2006: all].
- The 2D layer (cursor, overlays, crossfade) uses a full-screen orthographic projection: x 0
  to W, y 0 to H from the bottom, z −1 to 1, viewport 0, 0, W, H [exe DC 0x40b620–0x40b68e].

**2006.**

- The viewport is the same: 0, 64, W, H−128 [exe 2006 0x40c370–0x40c416].
- But the aspect passed to the projection is W / H, not the viewport's ratio
  [exe 2006 0x40c42a–0x40c436]. At 800×600 that is 1.333 for an 800×472 viewport (1.695).
- The default FOV is 53 [exe 2006 0x40d43c–0x40d45f], and it is rewritten to 53 every frame
  while `SC_DREAM` is off [exe 2006 0x4078fe].
- Near is 0.1 and far is 1000 [exe 2006 0x40c438–0x40c444].
- The horizontal FOV is about 67.2°, almost the same as DC (derived from aspect 4/3 and 53°).

## The six faces

The faces are drawn as the inside of a box centred on the camera. The camera never moves; it
only rotates.

- The box spans ±RATIOX/2 on x and z, and ±RATIOY/2 on y [exe DC 0x40c1ab–0x40c1dc]. RATIOX
  and RATIOY are the settings `SC_RATIOX` and `SC_RATIOY`, at offsets 0x00c and 0x010. Both
  default to 1 in DC, so the box is a unit cube [exe DC 0x40f2e0, 0x40f2e6, 0x40fffb, 0x410006].
- No shipped script sets them [script DC: all]. `scream.sc` defines `SC_DEF_RATIOX = 30` and
  `SC_DEF_RATIOY = 40`, but nothing reads these script constants [script DC: scream.sc].
- `scTexture(face, …)` stores face 1–6 at node record +0x34, +0x64, +0x94, +0xc4, +0xf4,
  +0x124 [exe DC 0x4149c7–0x414e07]. Entering the node loads them into texture slots 0–5 in
  that order, and their masks into slots 6–11 [exe DC 0x415af7–0x415bb5].
- Each face is a quad with the image upright and not mirrored, seen from inside
  [exe DC 0x40c242–0x40c835]:

| Face | Slot | Plane | Seen when yaw is |
|---|---|---|---|
| `SC_FRONT` (1) | 0 | z = +½ | 3.14 |
| `SC_BACK` (2) | 1 | z = −½ | 0 |
| `SC_LEFT` (3) | 2 | x = +½ | 1.57 |
| `SC_RIGHT` (4) | 3 | x = −½ | 4.71 |
| `SC_UP` (5) | 4 | y = +½ | looking up |
| `SC_DOWN` (6) | 5 | y = −½ | looking down |

  The yaw column follows from the view direction below.
- **Texture coordinates.** On each face, texture t = 0 is at the top edge (+y for the side
  faces), and on the side faces s grows to the viewer's right [exe DC 0x40c242–0x40c835]. The
  exact coordinate ranges, and how a 1024-pixel face fits a 1280 texture, are in
  [rendering.md](rendering.md#the-cube). `SC_TEX_SIZE` defaults to 1280 and becomes 2048 without
  non-power-of-two support [exe DC 0x40f37a, 0x407030; "Non power of two" warning].
- **Hotspots** are drawn on the same faces, in a box of half-size IMG_SIZE/2 (0x4df8d20,
  default 1024) [exe DC 0x40b872–0x40b88a]. `scPlace`'s face numbers map to the same planes:
  1 → z+, 2 → z−, 3 → x+, 4 → x−, 5 → y+, 6 → y− [exe DC 0x41692d–0x4169cd, jump table
  0x40b840].

**2006.**

- The box is built the same way from RATIOX and RATIOY, which default to 30 and 40
  [exe 2006 0x40b7cb–0x40b7f0, 0x40d3f5–0x40d421]. So the box is 30 wide and deep and 40 tall,
  and the image's vertical tangent scale is 4/3 of the horizontal one (derived).
- Combined with the W/H aspect, an image pixel at 800×600 shows about 0.95 times as wide as
  it is tall (derived).

[unverified: the 2006 per-face vertex and texture-coordinate order was not decoded; only the
extents were checked]

## Camera angles

DC keeps two floats: **yaw** at 0x48bae8 and **look-y** at 0x48baec. Every frame the view is
set with eye (0, 0, 0), up (0, 1, 0) and the direction

> (sin yaw, look-y, −cos yaw)

[exe DC 0x407f79–0x407fc2, 0x407dcf–0x407e12].

- **Yaw** is in radians. It starts at 3.14 (`SC_FRONT`) [data scream.exe @0x48bae8 = 3.14].
  - Increasing yaw turns right. From 3.14, yaw 4.71 faces x = −½, which is `SC_RIGHT`
    (derived).
  - The per-frame turn routine wraps it: above 6.28 it becomes 0, and below 0 it becomes
    6.28. This is not a modulo [exe DC 0x407f4d–0x407f6f, 0x40820c].
  - Other writers (keys, `scLookAt`, slideshow) do not wrap. Their value is wrapped the next
    time the turn routine runs.
  - The engine uses 3.14, 1.57, 4.71 and 6.28, not exact multiples of π
    [jump tables data scream.exe @0x41263c, @0x412654].
- **Look-y** is not an angle. It is the y component of a direction whose horizontal part has
  length 1, so the elevation is atan(look-y) (derived). It starts at 0.
- `scLookAt(face, SC_INSTANT)` sets the pair directly
  [exe DC 0x412685–0x4126cd; jump table @0x41263c]:

| Face | Yaw | Look-y | Elevation (derived) |
|---|---|---|---|
| `SC_FRONT` | 3.14 | 0 | 0° |
| `SC_BACK` | 0 | 0 | 0° |
| `SC_LEFT` | 1.57 | 0 | 0° |
| `SC_RIGHT` | 4.71 | 0 | 0° |
| `SC_UP` | 3.14 | 6.28 | 81.0° |
| `SC_DOWN` | 3.14 | −1.57 | −57.5° |

- The save file stores yaw and then look-y ([`formats.md`](formats.md) §5)
  [exe DC 0x411605–0x411643, 0x407d30].
- Turning the menu's slideshow or alternate-control option on or off resets the camera to
  yaw 3.14, look-y 0 [exe DC 0x413cb2–0x413cb9, 0x413cdd–0x413ce4, 0x413d1a–0x413d21,
  0x413d45–0x413d4c].

### Limits

| Who changes it | Look-y may still rise while | Look-y may still fall while | Elevation limit (derived) |
|---|---|---|---|
| Edge zones | look-y ≤ 1.75 | look-y ≥ −1.75 | about ±60.3°, plus one step |
| Alternate control | look-y ≤ 1.75 | look-y ≥ −2.0 | about +60.3° / −63.4°, plus one step |
| Slideshow click | see [Slideshow mode](#slideshow-mode) | | ±63.4° |
| `W`/`S` keys | set to ±1.25 | | ±51.3° |

[exe DC 0x407f12–0x407f21, 0x40801d–0x40802c, 0x408099–0x4080a8, 0x40818d–0x40819c; data
scream.exe @0x483230 = 1.75, @0x483234 = −1.75, @0x48323c = −2.0]

The test happens before the step, so the value can pass the limit by one step. Yaw has no
limit.

**2006.**

- The pair is yaw (0x4722b8) and **pitch** (0x4722bc). Pitch is an angle in radians.
- The direction is (sin yaw, sin pitch, −cos yaw), so the elevation is atan(sin pitch)
  [exe 2006 0x407b5c–0x407ba2]. The horizontal part is not shortened, so pitch 1.57 gives 45°
  (derived).
- Both start at 3.14 and 0 [data scream.exe 2006 @0x4722b8, @0x4722bc].
- The edge zones allow rising while pitch ≤ 1.57 and falling while pitch ≥ −1.57
  [exe 2006 0x407b1c, 0x407cb0; data @0x46a098 = 1.57, @0x46a09c = −1.57 (double)].
- The `scLookAt` instant values are the same numbers as DC [exe 2006 0x4103e6–0x41042f]. As a
  pitch, `SC_UP`'s 6.28 is level (sin 6.28 ≈ 0), and `SC_DOWN`'s −1.57 is 45° down
  (derived). The 2006 script calls `scLookAt(SC_UP, SC_INSTANT)` once and
  `scLookAt(SC_DOWN, SC_INSTANT)` once [script 2006: all]. The DC script calls only the
  `SC_DOWN` one [script DC: all].

## Mouse look: edge zones

This is the default mode when alternate control is off, and the only one in 2006.

### Zones

`0x40df60` sets the zone edges whenever the projection is set up, which happens every frame
[exe DC 0x40df60–0x40e021, 0x407cad]. Mouse x and y are window coordinates with y measured
from the top [exe DC 0x419c28–0x419d79, 0x40b400].

| Zone | Active when | At 1024×768 | At 800×600 (2006) |
|---|---|---|---|
| Left | x < ⌊W·15/100⌋ | x < 153 | x < 120 |
| Right | x > W − ⌊W·15/100⌋ | x > 871 | x > 680 |
| Up | y < 64 + ⌊(H−192)·15/100⌋ | y < 150 | y < 125 |
| Down | y > (H−128) − ⌊(H−192)·15/100⌋ | y > 554 | y > 411 |

- The zones are not symmetric vertically. The up zone reaches 86 pixels into the view at
  1024×768, and the down zone reaches 150 (derived).
- Before the test, x is clamped to 1…W−1 and y to 1…H−1 [exe DC 0x407e2e–0x407e5c].
- A horizontal and a vertical zone can both act in the same frame [exe DC 0x40808b].

### Rate

Each frame in node view, if the motion flag is on and slideshow mode is off
[exe DC 0x416a5d–0x416a87]:

> step = d × 0.005 × CAM_SPEED × 0.001 × ramp × frame_scale

- d is the distance in pixels past the zone edge.
- The step is subtracted from yaw for the left zone and added for the right. It is added to
  look-y for the up zone and subtracted for the down zone [exe DC 0x408051–0x4081fc; data
  scream.exe @0x483220 = 0.001, @0x483224 = 0.005].
- **CAM_SPEED** is settings offset 0x1a4 (0x4df8d08), default 50 [exe DC 0x40f3d8].
  - `scSet(SC_CAM_SPEED, v)` maps `SC_LO`/`SC_MED`/`SC_HI` to 25/50/75 and stores any other
    value as given [exe DC 0x40fe13–0x40fe25, 0x410050–0x410075].
  - The **F9** key cycles 25 → 50 → 75 → 25, and any other value → 50. It shows "camera speed
    SLOW / NORMAL / FAST" [exe DC 0x41771d–0x41773d, 0x417aea–0x417b21].
  - The menu's "camera speed" item sends the same key [exe DC 0x413c49–0x413c4e].
- **Ramp** (0x48bb84) [exe DC 0x407e5f–0x407e7f, 0x407fd8; data scream.exe @0x48321c = 0.025]:
  - If the previous frame did not turn, it is reset to 0, so the first frame of a turn does
    not move.
  - Otherwise, while it is below 1.0, it grows by 0.025 per frame. After 41 frames it stops,
    at about 1.025 (derived).
  - It is counted in frames, not time.
- **frame_scale** is the engine's speed factor, 60 ÷ the mean frame rate since start-up
  (settings offset 0x020), defined in [mainloop.md](mainloop.md#frame-timing)
  [exe DC 0x40e7f0–0x40e890]. It makes the per-second turn rate independent of the frame
  rate (derived).
- **Example** (derived): with CAM_SPEED 50 and full ramp, the pointer at x = 1 on a
  1024-wide screen (d = 152) turns 152 × 0.005 × 0.05 × 60 ≈ 2.28 rad/s, about 131°/s.

### When it does not run

- The camera is not turned while the flag at 0x48bae4 is set. The inventory view sets it, and
  the end of any crossfade clears it [exe DC 0x407e9d, 0x409902, 0x409665, 0x40aac1].
- The turn runs only in states 4000 (node) and 4006 [exe DC 0x416a56; jump table data
  scream.exe @0x4169d0].
- With the motion flag off (`scStill`, or during `scSleep`), or in slideshow mode, the view
  is set from the current angles without turning [exe DC 0x416a62–0x416a76, 0x416cac–0x416cbc].

**2006.**

- The formula and constants are the same, with pitch in place of look-y
  [exe 2006 0x407a40–0x407d09; data @0x46a084 = 0.025, @0x46a088 = 0.001, @0x46a08c = 0.005].
- CAM_SPEED is at 0x2f0bcd8, default 50 [exe 2006 0x40d49f, 0x40d531].
- The zone edges use the same formulas [exe 2006 0x40c370–0x40c3ee].
- frame_scale is computed the same way [exe 2006 0x40ca40; data @0x46a358 = 60,
  @0x46a354 = 300].

## Alternate control (DC only)

The menu item "alternate control" toggles the flag at 0x4df8e10. It shows "alternate control
ENABLED / DISABLED" [exe DC 0x413cf6–0x413d4c; data scream.exe @0x483b0b, @0x483af0].

- **It is on by default** [exe DC 0x40f451]. The flag is settings offset 0x2ac, so save files
  carry it ([`formats.md`](formats.md) §3).
- `SC_FPS_STYLE` (1041) is defined in the DC `scream.sc` [script DC: scream.sc], but no script
  uses it. `scEnable`, `scDisable` and `scSet` ignore key 1041 [exe DC 0x40fa60, 0x40fbf0,
  0x40fefd]. No `scream.cfg` key sets the flag either: the cfg key names in the exe are only `SCRIPT` …
  `LANGUAGE` [data scream.exe: cfg key strings].
  [unverified: that `SC_FPS_STYLE` was meant for this mode]

When it is on:

- The zone edges move to the screen centre: left/right at W/2 − 1 and W/2 + 1, up/down at
  H/2 − 1 and H/2 + 1 [exe DC 0x40e0a1–0x40e0bb, 0x40e007–0x40e021].
- **Turning.** Each frame:
  - if x < W/2: yaw −= (W/2 − 1 − x) × 0.0003;
  - if x > W/2: yaw += (x − (W/2 + 1)) × 0.0003;
  - if y < H/2 and look-y ≤ 1.75: look-y += (H/2 − 1 − y) × 0.0003;
  - if y > H/2 and look-y ≥ −2.0: look-y −= (y − (H/2 + 1)) × 0.0003.

  There is no CAM_SPEED, ramp or frame_scale [exe DC 0x407eba–0x407f47, 0x407fe3–0x40804c;
  data scream.exe @0x483238 = 0.0003].
- **Re-centring.** In node view, while camera motion is allowed (`scMotion`), slideshow mode is
  off and input is unlocked, and when more than 30 ms (`GetTickCount`) have passed since the
  last re-centre with no capture or blocking turn running, the pointer moves toward (W/2, H/2)
  [exe DC 0x41c1f0, 0x416a62]
  [exe DC 0x416a19–0x416a3e, 0x416a8c–0x416b5d, 0x416c29–0x416ca7; data scream.exe
  @0x483cf8 = 0.001, @0x483cfc = 0.1, @0x483d00 = 0.003]:
  - each axis moves by ⌊distance/10⌋ pixels, at least 1;
  - this uses `SetCursorPos`.
- **Cursor.** The cursor is drawn at the screen centre, not at the pointer
  [exe DC 0x416b91–0x416bbe].
- **Other centring.** The pointer is also set to the centre when the last zoom is closed
  [exe DC 0x415d98–0x415dc2], when the right button or Esc leaves the inventory or book views
  [exe DC 0x417466–0x417491, 0x417985–0x4179af, 0x4179ce–0x4179f9], and after three menu
  actions [exe DC 0x4136fc, 0x4137d0, 0x413855].

Hover and click picking still use the pointer position stored from the last mouse message,
not the screen centre [exe DC 0x412038–0x412047, 0x40b72c–0x40b7f9] (see
[input.md](input.md#node-view)).

## Slideshow mode

The menu item "slideshow mode" toggles the flag at 0x4df8ddc (settings offset 0x278, default
0). It shows "slideshow mode ON / OFF" [exe DC 0x413c8e–0x413ce4, 0x40f465; data scream.exe
@0x483ade, @0x483acb].

- Continuous turning stops [exe DC 0x416a6f].
- **Hover.** In node view, with no object under the pointer, the zone sets a direction cursor
  [exe DC 0x414448–0x4144c7; jump table data scream.exe @0x414268]:
  - left → cursor 0x3a, right → 0x3b, up → 0x3c, down → 0x3d;
  - here the up zone counts only while look-y ≤ 1.25, and the down zone only while
    look-y ≥ −1.25 [exe DC 0x407d50–0x407dbf; data @0x483214 = 1.25, @0x483218 = −1.25].
- **Click.** On a left-button release in node view, if the pointer is in a zone, the engine
  captures the frame for a crossfade and makes one turn step [exe DC 0x4173be–0x417403]:
  - left: yaw −0.7855; right: yaw +0.7855 (45°) [data @0x48322c, @0x483228];
  - up: if look-y equals −1.75 it becomes 0, otherwise 2.0 [exe DC 0x4080ae–0x4080d6, 0x408139];
  - down: if look-y equals 1.75 it becomes 0, otherwise −2.0 [exe DC 0x4081a2–0x4081d8].
  - The click is not passed on to objects. There is no test for an object under the pointer
    [exe DC 0x417408–0x41740a].
- As coded, DC look-y only reaches 0, 2.0 and −2.0 by clicks, and never equals ±1.75. So from
  level, up gives 2.0; from 2.0, down gives −2.0; from −2.0, up gives 2.0. Clicks never return
  to level. The `E`, Space and Numpad-5 keys do (see below).
  [unverified: observe in the running game]
- The turn step uses the same routine as continuous turning, and that routine tests
  alternate control first. With both modes on, a click applies the
  [alternate-control formula](#alternate-control-dc-only) once instead of the 45° step. The
  zone edges are then at the centre, so almost every click lands in a zone
  [exe DC 0x407eba, 0x40df60]. [unverified: observe in the running game]

**2006.**

- The flag is 0x2f0bda8 [exe 2006 0x411793–0x4117e9].
- The click steps are yaw ±0.7855, and pitch to 1.57 / −1.57 or back to 0
  [exe 2006 0x407af9, 0x407c34, 0x407b3e–0x407b4d, 0x407cce–0x407cd9].
- The equality test uses −1.57 and 1.57, the same values the clicks set. So in 2006, up from
  looking down returns to level, and the reverse [data @0x46a0a4 = −1.57, @0x46a098 = 1.57].
- The hover limits are ±1.25 (as pitch) [exe 2006 0x407960–0x4079d2; data @0x46a078,
  @0x46a07c].
- There is no alternate control.

## Keyboard

The window procedure passes the `wParam` of `WM_KEYDOWN`, a Windows virtual-key code, to the
key handler [exe DC 0x419cbf–0x419ccc, 0x4105ff–0x410604].

- Each camera key first captures the frame, which starts a crossfade (see below). It then
  sets the angles directly. Nothing animates, and there is no frame scaling
  [exe DC 0x41760c–0x417634, 0x41787a–0x4178b0, 0x4178ea–0x417915, 0x417bc8–0x417bfe,
  0x417c0c–0x417c37].
- The handler does not test the view state before these keys.

| Code | Key | Effect |
|---|---|---|
| 0x41, 0x25, 0x61 | `A`, Left arrow, Numpad 1 | yaw − 1.57 |
| 0x44, 0x26, 0x64 | `D`, **Up arrow**, Numpad 4 | yaw + 1.57 |
| 0x57, 0x27, 0x77 | `W`, **Right arrow**, F8 | look-y = 1.25 |
| 0x53, 0x28, 0x73 | `S`, Down arrow, F4 | look-y = −1.25 |
| 0x45, 0x20, 0x65 | `E`, Space, Numpad 5 | look-y = 0 |
| 0x78 | F9 | cycle CAM_SPEED (see above) |

[exe DC 0x4175b8–0x41765f, 0x417857–0x417932; data scream.exe @0x483e18 = 1.57, @0x483e1c =
−1.57]

The codes 0x61, 0x64, 0x65, 0x73 and 0x77 are also lowercase `a d e s w`. The arrow
assignment (Up turns right, Right looks up) is as coded.
[unverified: whether the arrow keys behave this way in the running game]

**2006.** The key table is the same [exe 2006 0x414a09–0x414d8f]. `W` and `S` set the pitch
to ±1.25 rad, which is about ±43.5° (derived) [exe 2006 0x414d68, 0x41505f]. The yaw steps are
±1.57 [data @0x46aae4, @0x46aae8].

## Crossfade after a capture

The camera keys and the slideshow edge click start the **short** crossfade: the captured
frame starts at 25% opacity (progress 0.75) and fades out in 10 frames at the default
`SC_MOV_SPEED` [exe DC 0x407b20, 0x407b5b; callers 0x41761a, 0x417888, 0x4178f8, 0x417bd2,
0x417c1a, 0x4173f3]. Every other view change uses the full crossfade from progress 0
(40 frames). Both are described, with all their triggers, in
[rendering.md](rendering.md#view-crossfades).
- **Fixed coordinates.** The overlay quad is at x 0–1024, y 64–960, with texture v 0.0625–0.9375,
  whatever W and H are [exe DC 0x4095c6–0x409633]. With a 1024 capture this maps
  frame-buffer pixels 1:1 (derived).
- 2006 uses the same fixed quad [exe 2006 0x408ee5–0x408f52].

## Smooth `scLookAt`

See [`api.md`](api.md#sclookat) for the call. Targets [exe DC 0x412816; jump table data
scream.exe @0x412654]:

| Face | Target yaw | Target look-y |
|---|---|---|
| `SC_FRONT` | 3.14 | 0 |
| `SC_BACK` | 6.28 if the current yaw is above 3.14, else 0 | 0 |
| `SC_LEFT` | 1.57 | 0 |
| `SC_RIGHT` | 4.71 | 0 |
| `SC_UP` | 3.14 | 6.28 |
| `SC_DOWN` | 3.14 (same as `SC_FRONT`) | 0 |

- **Start.** The current yaw is first reduced to [0, 6.28): (yaw/6.28 − floor(yaw/6.28)) ×
  2 × 3.14 [exe DC 0x4126e4–0x41270c; data @0x4839b4 = 0.15924, @0x4839b8 = 2, @0x4839bc =
  3.14].
- **Each frame** [exe DC 0x41272a–0x412811, 0x4128ab–0x4128e3; data @0x4839c0 = 0.01]:
  - d = target yaw − yaw, and dy = target look-y − look-y;
  - the yaw step is (1 + d) × 0.01 × frame_scale. It is added if d > 0 and subtracted
    otherwise;
  - the yaw is done once the step is at least |d| when d ≥ 0, or at least 1 − d when d < 0;
  - the look-y step is (1 + dy) × 0.01 × frame_scale. It is added while look-y < target and
    subtracted while look-y > target;
  - look-y is done when it reaches or passes the target.
  - The frame is drawn, and the per-frame routine runs [exe DC 0x41280c].
- **Feel** (derived). For d > 0 the turn eases out: d(t) = (1 + d₀)·e^(−0.6 t) − 1 at any
  frame rate. A 90° turn takes about 1.6 s and a 180° turn about 2.4 s. It ends up to about
  0.01 rad past the target; it does not snap to it.
- **As coded:**
  - When d < −1 (or dy < −1), the step is negative and moves away from the target, so the
    loop does not converge.
  - When look-y already equals its target exactly, neither branch runs. The vertical part is
    never marked done, so the loop never ends [exe DC 0x4127c3–0x4127d2, 0x4128c4,
    0x41288f–0x412891].

  [unverified: whether either case happens in play; observe a smooth turn from `SC_RIGHT` to
  `SC_FRONT`, and a smooth turn issued just after `scLookAt(…, SC_INSTANT)` with no vertical
  mouse movement]
- **Dream.** While `SC_DREAM` is on, each frame of the turn moves the FOV 0.5° toward 45 and
  rebuilds the projection [exe DC 0x4127f2, 0x4128e8–0x41292b; data @0x4839c4 = 0.5,
  @0x4839c8 = −0.5].

**2006.**

- The targets are the same [exe 2006 0x410568; jump table @0x4103b8].
- The step is a constant 0.015 × frame_scale for both yaw and pitch, so the turn runs at an
  even 0.9 rad/s (derived) [exe 2006 0x4104b6–0x4104ca, 0x410509–0x410515; data @0x46a74c =
  0.015].
- The same exact-equality test for the vertical part is present [exe 2006 0x410519–0x410524,
  0x41062f].
- In dream mode the FOV moves 0.5° toward 53 [exe 2006 0x410653–0x410696].

## Dream field of view (`SC_DREAM`)

Every frame, before drawing [exe DC 0x407c68–0x407cf8; data @0x483210 = 0.005,
@0x48320c = −0.005]:

- **Off:** FOV = 45.
- **On:** the FOV moves by 0.005° per frame, between 35 and 55. The direction state
  (0x48bb7c) starts as "falling" [data @0x48bb7c = 2]:
  - when falling, it subtracts 0.005 and switches to rising once the FOV is ≤ 35;
  - when rising, it adds 0.005 and switches to falling once the FOV is ≥ 55.
  - The step is per frame, not frame-scaled. One sweep of 20° takes 4000 frames, about 67 s at
    60 fps (derived).
  - The direction is not reset when the effect is switched off [exe DC 0x407cee].
- The projection, and with it the zone edges, is rebuilt every frame [exe DC 0x407cad].
- What `SC_DREAM` does to the right click is in [`api.md`](api.md#scenable--scdisable).

**2006.** The same, but centred on 53: off → 53, on → between 43 and 63, steps of 0.005
[exe 2006 0x407878–0x4078fe; data @0x46a070, @0x46a074].

## Zoom views

A zoom image is drawn as a flat 2D quad in the full-screen orthographic layer. The 3D camera
is not used for it [exe DC 0x40a830, 0x40a8a2–0x40a8bc, 0x409960].

Where the quad lies for each image size is in [rendering.md](rendering.md#zoom-quad-placement)
(DC) and [rendering.md](rendering.md#zoom-frame-2006) (2006); none of it depends on the camera. The zoom
texture is allocated at `SC_IMG_SIZE` (default 1024) square [exe DC 0x4074d3–0x40778b,
0x40f384].

The zoom views do not turn. The turn routine runs only in node view [exe DC 0x416a56].

## What depends on WIDTH and HEIGHT

| Uses W and H | Fixed at 1024-based values |
|---|---|
| 3D viewport and aspect [exe DC 0x40df60] | crossfade overlay quad [exe DC 0x4095c6] |
| edge-zone edges [exe DC 0x40df60] | DC zoom quad [exe DC 0x409971] |
| 2D orthographic layer [exe DC 0x40b620] | capture size = IMG_SIZE, not W×H [exe DC 0x407b34] |
| pointer clamp to 0…W, 0…H [exe DC 0x41729c–0x41734d] | |
| alternate-control centre and re-centring [exe DC 0x416ab9] | |
| cursor drawn at (x, H − y) [exe DC 0x40b400] | |

`SC_VIEWX` and `SC_VIEWY` (offsets 0x014, 0x018, defaults 16 and 9) are stored by `scSet`, but
nothing in the DC listing reads them [exe DC 0x410011, 0x41001c, 0x40f289–0x40f293; no other
reader].

2006 centres the zoom on the screen, so its zooms follow W and H. In 2006, `scSwitch` also
moves the pointer to the centre of the window when it enters node view
[exe 2006 0x4131f8–0x413207, 0x413613–0x413622, 0x417180–0x4171a0].

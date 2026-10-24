# Rendering and visual effects

This page describes how the original SCream engine (`scream.exe`) draws a frame of *Scratches*:
node and zoom views, the sky, fog, rain, the lighting effects, the ambience colours, view
crossfades, gamma, texture set-up and video placement. It describes only what the original
executables and shipped files define. Where the 2006 engine differs from the Director's Cut
(DC), the difference is given in [2006 differences](#2006-differences). The `sc*` functions
are in [`api.md`](api.md) and the file formats in [`formats.md`](formats.md).

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables listed in [api.md](api.md#sources),
> the shipped scripts decompiled with unluac, and measurements of the shipped files. A person
> has not yet checked it. A source tag says where a claim can be checked; it is not a
> guarantee. Check a claim against its source before you build on it, and treat
> `[unverified]` claims as open questions.

## Sources

The tags are those of [api.md](api.md#sources) and [formats.md](formats.md#sources):

| Tag | Means |
|---|---|
| `[exe DC 0x…]` | code or data at that address in the DC update's `scream.exe` (MD5 `f696d90b1f1fe5eb050beda06781dcc8`) |
| `[exe 2006 0x…]` | the same in the patched 2006 `scream.exe` (MD5 `5de7958817eecb3815d816d123ab9217`) |
| `[script DC: name]`, `[script 2006: name]` | the decompiled shipped scripts |
| `[data FILE …]` | a measurement of a shipped file: DC `scratches.res` (MD5 `74e6a98a3fede88c26c36d4d2ca77d7f`) or `scream.res` (MD5 `0f7030261ca035786985ed87cb12d05a`) |
| `[unverified: …]` | not confirmed in any of the above |

Addresses are virtual addresses (image base 0x400000). Float constants are read from the data
section at the address given. GL functions are named from the import table: DC thunks
0x47f800–0x47f992, 2006 thunks 0x466d3c–0x466f34 [exe DC 0x47f8e4–0x47f992] [exe 2006
0x466df0–0x466f34]. A blend is written `(src, dst)` for `glBlendFunc(src, dst)`.
"Derived" marks a number computed from the cited constants, not read directly.

**Coordinates.** Unless a section says otherwise:

- 2D quads are drawn in an orthographic projection of window pixels: `glOrtho(0, W, 0, H, −1, 1)`
  with a full-window viewport. *y* = 0 is the bottom edge of the window [exe DC 0x40b620].
- W and H are the `WIDTH` and `HEIGHT` settings (DC default 1024 × 768, shipped DC cfg
  1024 × 768) [exe DC 0x40f260] [data scream.cfg].
- The quad coordinates below are fixed numbers, written for a 1024 × 768 window.
  They do not scale with W and H [exe DC 0x409960, 0x4082a0].


A claim marked "(derived)" follows from the cited sources by reasoning, and was not observed in the running game.

---

## The frame

### Frame start

Every frame starts the same way [exe DC 0x407c30–0x407cf8]:

1. The frame-rate measurement is updated (see [Frame-time factor](#frame-time-factor)).
2. The colour buffer is cleared to black. The clear colour is set to (0, 0, 0, 0) once at
   start-up [exe DC 0x407061, 0x407c4a].
3. The current colour is set to opaque white [exe DC 0x407c4f–0x407c63].
4. The field of view is updated for `SC_DREAM` (see [Dream](#dream)).
5. The 3D projection is set:
   - the viewport is x 0, y 64, width W, height H − 128;
   - `gluPerspective(fov, W / (H − 128), 0.1, 1000)`;
   - the model-view matrix is reset.

   [exe DC 0x40df60–0x40e095]

At 1024 × 768 the 3D view is therefore a 1024 × 640 band, with 64-pixel strips above and
below it (derived). The default field of view is 45° [exe DC 0x40f2b1, 0x40e07d].

**Fixed state.** At start-up the engine disables depth testing and enables 2D texturing and
blending [exe DC 0x40703b–0x407054]. No `glTexEnv` function is imported [exe DC: import
table], so textures use OpenGL's default modulate mode. Each drawn colour is therefore
texture × current colour.

### Frame by state

The per-frame routine picks the frame function by game state, then draws the text and the
cursor over it [exe DC 0x416a10]:

| State | Drawing | Source |
|---|---|---|
| node | [node frame](#node-frame) | [exe DC 0x416b62] |
| zoom | [zoom frame](#zoom-frame), drawn in the 2D projection | [exe DC 0x416d94–0x416d99] |
| overlay | the frame of the previous state (node, zoom or other), then the [overlay](#overlays) on top | [exe DC 0x416ec6–0x416f38] |
| video | [video frame](#video-frames-sccutscene) | [exe DC 0x416d00–0x416d31] |

The inventory, journal and menu screens have their own frame functions [exe DC 0x409680,
0x40b0a0, 0x408550 respectively]; they are described in [screens.md](screens.md).

---

## Texture slots

Textures live in a fixed table of slots, 32 bytes each:

- the GL handle at +0;
- a "loaded" flag at +4;
- the pixel buffer at +8;
- the width and height of the last loaded image at +0x10 and +0x14.

Slots 0–64 start at 0x4df3bc4, and higher slots at 0x4df2f44 + (slot − 64) × 32
[exe DC 0x407880–0x4078ce, 0x40bd00]. Every slot is created at `scInit` with a fixed square
size [exe DC 0x407460–0x40779c]:

| Slot | Contents | Size | Channels |
|---|---|---|---|
| 0–5 | node faces: front, back, left, right, up, down (`SC_FRONT`..`SC_DOWN` − 1) | setting 0x1b8, default 1280 | 3 |
| 6–11 | node masks, same face order | setting 0x1c0, default 512 | 1 |
| 0x1a | video frame | setting 0x1bc, default 1024 | 3 |
| 0x1b | zoom image | setting 0x1bc | 3 |
| 0x1c | overlay image | setting 0x1bc | 4 |
| 0x1e, 0x1f | `clouds1.tga`, `clouds2.tga` | setting 0x1c4, default 512 | 4 |
| 0x20 | filled with 0xFF: opaque white | 32 | 4 |
| 0x21 | zero-filled: transparent black | 32 | 4 |
| 0x22 | crossfade capture | setting 0x1bc | 3 |
| 0x23 | gamma capture | setting 0x1bc | 3 |
| 0x24 | not loaded by the code above | 64 | 3 |
| 0x25 | `rain_def.tga` | (loaded in `scInit`) | — |
| 0x38 | zoom mask | setting 0x1c0 | 1 |
| 0x3e | `gradient.tga` | 256 | 4 |

Sources:

- slot sizes: [exe DC 0x4074d3–0x40778b]; default sizes: [exe DC 0x40f37a, 0x40f384, 0x40f407, 0x40f40d];
- faces and masks: [exe DC 0x415af7–0x415bb5] [exe DC 0x4149c0: face *n* → record +0x34 + 0x30 (*n* − 1)];
- zoom image and mask: [exe DC 0x415c61–0x415c80]; overlay: [exe DC 0x4159d5–0x4159df];
- clouds and gradient: [exe DC 0x4076d7–0x407704]; rain: [exe DC 0x41040e, see formats.md];
- white and black buffers: [exe DC 0x40bd00: slot 0x20 filled with 0xFF; other buffers come from 0x41c1d0, which clears with 0x41ccb0].

**Uploads.**

- Each slot's texture is allocated once at start-up from a zeroed buffer, so it starts
  transparent black [exe DC 0x40bd00, 0x40be74]. An image is then copied into its slot with
  `glTexSubImage2D` at (0, 0), at its own size, so outside the new image the slot keeps
  whatever an earlier, larger image left there [exe DC 0x4078fe–0x407934, 0x4079df–0x407a05].
- TGA pixels are swapped from BGR to RGB. Rows are uploaded in file order, and the loader does
  not test the TGA origin bit [exe DC 0x404630, 0x404750].
- The shipped masks, clouds, gradient and rain files store the top row first [data
  `gradient.tga`, `clouds1.tga`, `clouds2.tga`, `rain_def.tga`, `garden_n1f_msk.tga`: TGA
  descriptor 0x28 / 0x20].
- A 1-channel slot is created as `GL_LUMINANCE`, 3 channels as `GL_RGB` and 4 as `GL_RGBA`
  [exe DC 0x40bbe0]. A mask therefore has no alpha: drawn, it gives mask × colour, with the
  alpha of the current colour.

**Missing files.** The "loaded" flag is cleared when a file cannot be read:

- a TGA that is not found [exe DC 0x4078e2];
- a JPEG that is not found, except `black.jpg` [exe DC 0x4079bf–0x4079cf].

The archive lookup still reports the type from the extension when the name is missing
[exe DC 0x40eb9f–0x40ec26]. Every textured cube face falls back to the white slot 0x20 when
its slot's flag is clear [exe DC 0x40c20f–0x40c216, 0x40c8fd]. So:

- a face with no `_msk.tga` file draws as a white (opaque) mask;
- a missing face image draws white.

**`black.jpg`.** `black.jpg` is in none of the archives (see [formats.md](formats.md#images)).
When its JPEG decode fails, the loader compares the name with `black.jpg`. On a match it
uploads a zero-filled buffer of the slot's width × height and sets the flag
[exe DC 0x407944–0x4079b1]. The built-in `black` zoom therefore shows a black image.

**Filtering and wrapping.** Set once per slot, at creation [exe DC 0x40bd00]:

- `QUALITY` = 0 gives `GL_NEAREST`, any other value gives `GL_LINEAR`, for both the minifying
  and the magnifying filter. There are no mipmaps.
- Slots 0x1e, 0x1f (clouds) and 0x25 (rain) use `GL_REPEAT`.
- Every other slot uses `GL_CLAMP_TO_EDGE` if the driver reports
  `GL_ARB_texture_border_clamp`, else `GL_CLAMP` [exe DC 0x407070–0x40729d; strings 0x48306c,
  0x483088].
- If `GL_ARB_texture_non_power_of_two` is missing, the face-slot size is forced to 2048
  [exe DC 0x4070b5–0x4070d5; string 0x4830df].

**Hints.** With `QUALITY` ≠ 0, the engine asks for `GL_NICEST` perspective correction and
point, line and polygon smoothing. With `QUALITY` = 0, it asks for `GL_NICEST` perspective
correction and `GL_FASTEST` for the three smoothing hints. The fog hint is `GL_FASTEST` in
both cases [exe DC 0x4070e8–0x40713b, 0x4072c6–0x4072f8]. `scSet(SC_QUALITY, …)` after
`scInit` does not change textures that already exist [exe DC 0x40bd00 runs only from
0x407460].

---

## The cube

A node is drawn as a cube of size 1 × 1 × 1 centred on the camera. The size comes from the
settings at 0x00c and 0x010, both default 1 [exe DC 0x40c1a0; 0x40f2e0–0x40f2e6; constant
0x483458 = 0.5]. The same routine draws every six-slot set: faces 0–5, masks 6–11, the white
set 0x20–0x25, and the gradient set 0x3e. Each face of a set binds slot (base + face index)
[exe DC 0x40c1a0].

- The first face drawn (back, slot base + 1) lies in the plane *z* = −0.5.
- Its texture is upright: *t* = 0 at the top (+*y*), *s* = 0 at −*x*.
- The face drawn with slot base + 4 lies in *y* = +0.5, the top.

[exe DC 0x40c242–0x40c2fa, 0x40c547–0x40c5ff]

**Texture coordinates** [exe DC 0x40c1ab–0x40c206, 0x40c844–0x40c8f8; constants 0x48345c,
0x483460]:

| Set | *s*, *t* range |
|---|---|
| faces, white set (0, 0x20) | 1/(2T) … (2T − 1)/(2T), with T = face-slot size. If the loaded front face is narrower than 1280, the upper limit is × 0.8. If NPOT textures are missing, it is × 0.625 as well |
| masks (6) | 1/T … (T − 1)/T, with T = face-slot size (not the mask size) |
| gradient (0x3e) | 1/T … (T − 1)/T, with an offset added to the top-edge *t* (see [Sky](#sky-dc)) |

A 1024-pixel face in the 1280 slot thus maps to its full image (1024 / 1280 = 0.8, derived).

**Display lists.** Two display lists hold the cube draws:

- one with the mask set (slot base 6) [exe DC 0x40c150–0x40c16c];
- one with the face set (slot base 0) [exe DC 0x40c171–0x40c189].

They are rebuilt when the node changes [exe DC 0x4168f0 → 0x408220 → 0x40c150]. A third list
holds the hotspot geometry. It begins with blend (`GL_ZERO`, `GL_ONE`), which leaves the
screen unchanged [exe DC 0x408220–0x408263]. With `SC_SHOW_HOTSPOTS` on, the hotspot quads are
drawn additively in colour (0.5, 1, 1) instead [exe DC 0x40b88c–0x40b8b7] (see
[input.md](input.md#node-view)).

---

## Node frame

The node frame is drawn in the 3D projection [exe DC 0x409460]. The flags are those of
`scEnable` (see [api.md](api.md#scenable--scdisable)). The steps below run in order.

1. **Blend** (`GL_ONE`, `GL_ONE`) onto the black buffer [exe DC 0x40947b].
2. **Backdrop.** Only the first enabled flag of `SC_SKY`, `SC_FOG` and `SC_RAIN` is drawn
   [exe DC 0x409480–0x409494, 0x40954c]:

   | Flag | Backdrop | Mask blend |
   |---|---|---|
   | `SC_SKY` | the [sky](#sky-dc) | (`GL_SRC_COLOR`, `GL_ONE`) |
   | `SC_FOG` | the white set drawn in grey (0.25, 0.25, 0.25), then the [fog](#fog) dome twice | (`GL_SRC_COLOR`, `GL_ONE`) |
   | `SC_RAIN` | the white set drawn in white, then the [rain](#rain) | (`GL_DST_COLOR`, `GL_ONE`) |
   | none | nothing | no mask is drawn |

   Sources: sky [exe DC 0x409524–0x409547]; fog [exe DC 0x40949a–0x4094bd]; rain [exe DC
   0x409555–0x40957f].
3. **Mask.** If a backdrop was drawn, the mask set is drawn with the blend in the table
   [exe DC 0x4094c2–0x4094d3]:
   - with (`GL_SRC_COLOR`, `GL_ONE`): result = backdrop + mask² (clamped);
   - with (`GL_DST_COLOR`, `GL_ONE`): result = backdrop × (1 + mask) (clamped).

   The blend is then set to (`GL_DST_COLOR`, `GL_ZERO`), which multiplies
   [exe DC 0x4094d8–0x4094df].
4. **Faces and lighting.** The [lighting pass](#lighting-pass) draws the face cube with the
   current blend, then its effects [exe DC 0x4094ea].
   - With a backdrop, each face pixel is multiplied by (backdrop + mask term). Where the mask
     is white, the sum saturates to 1 and the face shows unchanged. Where it is black, the
     face is multiplied by the backdrop.
   - With no backdrop, the faces are added to black, so they show unchanged.

   (Derived from the blends.)
5. **Hotspots.** The hotspot list is replayed [exe DC 0x4094ef–0x4094f6].
6. **2D overlays.** In the 2D projection, the frame draws [gamma](#gamma-f12) if it is on, then
   the [crossfade](#view-crossfades) if one is running [exe DC 0x4094fb–0x409671].

**What the masks hold.** In the shipped masks, black marks the sky openings. For example,
`garden_n1f_msk.tga` is black over its top 32% and white over 67%. There, the face image
under the black part is mid-grey (mean 142 / 255), and under the white part it is dark (mean
32) [data garden_n1f_msk.tga, garden_n1f.jpg]. So the backdrop shows through, multiplied by
the painted sky of the face.

---

## Zoom frame

The zoom frame is entered in the 2D projection [exe DC 0x416d94]. It differs from the node
frame as follows [exe DC 0x40a830]:

- **Start.** It sets blend (`GL_ONE`, `GL_ONE`) and opaque white [exe DC 0x40a87b–0x40a894].
- **`SC_NIGHT` on.** All the steps below are skipped. The zoom image is drawn straight into
  the lighting pass, added to black [exe DC 0x40a899–0x40a8bc].
- **Otherwise:**
  1. It returns to the 3D projection. If the zoom has a mask (slot 0x38 loaded), it also
     applies the current camera direction, so the backdrop turns with the view
     [exe DC 0x40a8e6–0x40a8fd, 0x407dcf–0x407e12].
  2. It draws the backdrop:

     | Flag | Backdrop | Mask blend |
     |---|---|---|
     | `SC_SKY` | the sky, as in the node frame | (`GL_SRC_COLOR`, `GL_ONE`) |
     | `SC_FOG` | the fog dome **once**, with no grey cube | (`GL_DST_COLOR`, `GL_ONE`) |
     | `SC_RAIN` | the white set, then the rain | (`GL_SRC_COLOR`, `GL_DST_COLOR`) |

     Sources: sky [exe DC 0x40a902–0x40a935]; fog [exe DC 0x40a9af–0x40a9c7]; rain [exe DC
     0x40a984–0x40a9ad].
  3. In the 2D projection, it draws the zoom mask (or white if the zoom has none) as a quad,
     with the mask blend above [exe DC 0x40a93a–0x40a965, 0x40a9cc].
  4. It sets blend (`GL_DST_COLOR`, `GL_ZERO`) and draws the zoom image through the lighting
     pass [exe DC 0x40a96a–0x40a8bc].
- **End.** Gamma, then the crossfade, as in the node frame [exe DC 0x40a8c1–0x40aab9].

### Zoom quad placement

The quad routine 0x409960 draws in window pixels. Texture coordinates run over the whole
square slot, so the image (stored in the slot's top-left corner) lands where shown
[exe DC 0x409960; jump table 0x409948]:

| Mode | When | Quad corners (x, y) | *s*, *t* | Where an image lands (derived) |
|---|---|---|---|---|
| 0, zoom slot width < 1024 | zoom images 800 × 472 | (0, 704) – (1312, −685) | 0–1, 0–1 | an 800 × 472 image in the 1024 slot covers x 0–1025, y 64–704 |
| 0, zoom slot width ≥ 1024 | zoom images 1024 × 640 | (0, 704) – (1024, −320) | 0–1, 0–1 | a 1024 × 640 image covers x 0–1024, y 64–704 |
| 4 | the zoom mask quad | as mode 0 with width < 1024 | 0–1, 0–1 | a 400 × 236 mask in the 512 slot covers x 0–1025, y 64–704 |
| 2 | video narrower than 800 | (0, 704) – (1312, −685) | 0–0.8, 0–0.745 | a 640 × 352 frame in the 1024 slot covers x 0–1025, y 63–704 |
| 3 | video 800–1023 wide | (0, 704) – (1312, −662) | 0–1, 0–1 | an 800 × 480 frame covers x 0–1025, y 64–704 |

*t* = 0 is at the top edge (*y* = 704). Mode 0 picks its size from the **zoom** slot's width,
whatever texture is bound [exe DC 0x409971].

---

## Lighting pass

The lighting pass draws the scene image and the night, rain-flash, screen-tint and candle
effects [exe DC 0x409b50]. Its first argument is the mode:

- 0 = node: the image is the face cube list.
- ≥ 1 = a 2D quad: 1 = zoom, 2–4 = video.

In the table, "image" means the face cube for mode 0, or the quad for mode ≥ 1. The quad mode
is mode − 1 for modes 1, 3 and 4, and 2 for mode 2 [exe DC 0x40a2f3–0x40a3e5].

The first matching branch runs:

| Condition | Drawing |
|---|---|
| `SC_NIGHT` on | the image with the current blend. Then blend (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) with colour (0.1, 0.1, 0.5, 0.5), and the image again. Result = 0.5 · scene + 0.5 · image · (0.1, 0.1, 0.5) [exe DC 0x409b62–0x409bbe] |
| `SC_RAIN` on and `SC_CANDLE` off | the [lightning cycle](#rain-and-lightning) [exe DC 0x409f0e–0x40a2ed] |
| otherwise | the image with the current blend, then the [screen tint](#screen-tint) [exe DC 0x40a2ef–0x40a43c] |

After any branch, the [candle flicker](#candle-and-lamp) runs if `SC_CANDLE` or `SC_LAMP` is
on [exe DC 0x409bc3–0x409bca, 0x40a441–0x40a448].

So night replaces both the rain flashes and the screen tint (derived).

---

## Sky (DC)

The sky is drawn only in the node and zoom frames, only while `SC_SKY` is on. It has three
layers, drawn in this order:

1. **Sky colour.** The white set is drawn in the current sky colour A, added to the black
   buffer [exe DC 0x40cdfc–0x40ce18, 0x40d089–0x40d09b].
   - Five of its faces are white.
   - The down face shows slot 0x25 (`rain_def.tga`), since that slot is loaded [exe DC
     0x40c1a0, 0x41040e].
2. **Horizon colour.** The gradient set is drawn in horizon colour B, with blend
   (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) [exe DC 0x40ce2e–0x40ce40, 0x40d0a0–0x40d0b2;
   0x409529–0x40953d]:
   - the four side faces bind slot 0x3e (`gradient.tga`);
   - the up face binds slot 0x21 (transparent black), so A stays there;
   - the down face binds slot 0x20 (white), so B covers it.

   [exe DC 0x40c22d–0x40c23d, 0x40c440–0x40c451, 0x40c532–0x40c542]
3. **Clouds.** The two [cloud layers](#clouds-dc) are drawn [exe DC 0x409542].

**The gradient image.** `gradient.tga` is 256 × 256, 32-bit. Its top quarter is black with
alpha 0. Below that it is white, with alpha rising toward the bottom: 1, 32, 77, 128, 180, 225
at rows 64, 96, … 224, and up to 254 [data gradient.tga]. On the side faces, B therefore
covers the bottom and fades out upward (derived).

**Gradient slide.** The top-edge *t* of the gradient faces starts at 1/T. Each time the
gradient set is drawn, an offset is added to it: 0.001 per draw while the offset is ≤ 0.25.
It ends at about 0.251, after about 251 frames, and is never reset [exe DC 0x40c1d7–0x40c1f8,
0x40c85b–0x40c893; constants 0x483464 = 0.25, 0x483468 = 0.001].

### Ambience colours

Each of the three ambience targets has a target preset and a current preset. All six start
at `SC_MORNING` (311) [exe DC 0x407243–0x40727f].

`scSetAmbience` (see [api.md](api.md#scsetambience)) maps its target and stores the preset in
the settings block [exe DC 0x418640–0x418683]:

| Target | Id | Settings offset | Target / current preset |
|---|---|---|---|
| `SC_SKY_COLOR` (1042) | 400 | 0x2b0 | 0x4df2d84 / 0x4df2d88 |
| `SC_CLOUD_COLOR` (1043) | 401 | 0x2b4 | 0x4df2d94 / 0x4df2d98 |
| `SC_SCREEN_COLOR` (1044) | 402 | 0x2b8 | 0x4df2d8c / 0x4df2d90 |

With `now` ≠ 0, the current preset is also set [exe DC 0x40ca50–0x40ca94].

The presets are `SC_DAWN` 309, `SC_MORNING` 311, `SC_DUSK` 317 and `SC_STORM` 319 [script DC:
scream.sc]. The tables are selected by `preset − 309` through 11-entry jump tables. The values
310, 312–316 and 318 fall through and keep the previous colour values [exe DC 0x40cb05–0x40cb14,
0x40ca9c; 0x40d124–0x40d133, 0x40d0c0; 0x40d4e6–0x40d4f5, 0x40d480].

**Sky** (sky colour A, horizon colour B) [exe DC 0x40cb1b–0x40cb4b, 0x40ce4b–0x40cef4]:

| Preset | A: sky (R, G, B) | B: horizon (R, G, B) |
|---|---|---|
| `SC_DAWN` | 1.0, 0.8, 0.55 | 0.8, 0.05, 0.05 |
| `SC_MORNING` | 0.9, 0.9, 0.9 | 0.55, 0.65, 0.8 |
| `SC_DUSK` | 0.45, 0.5, 0.6 | 1.0, 0.0, 0.0 |
| `SC_STORM` | 0.7, 0.7, 0.9 | 0.15, 0.15, 0.8 |

**Cloud colour** [exe DC 0x40d13a–0x40d14e, 0x40d31f–0x40d372]:

| Preset | R, G, B |
|---|---|
| `SC_DAWN` | 1.0, 0.7, 0.55 |
| `SC_MORNING` | 0.75, 0.75, 0.75 |
| `SC_DUSK` | 0.95, 0.55, 0.45 |
| `SC_STORM` | 0.3, 0.3, 0.3 |

**Screen tint** [exe DC 0x40d4fc–0x40d51b, 0x40d7c7–0x40d82c; jump table 0x40d480]:

| Preset | R, G, B | alpha |
|---|---|---|
| `SC_DAWN` | 1.0, 0.55, 0.55 | 0.025 |
| `SC_MORNING` | 1.0, 1.0, 1.0 | 0.0 |
| `SC_DUSK` | 1.0, 0.25, 0.25 | 0.02 |
| `SC_STORM` | 0.0, 0.0, 0.15 | 0.15 |

### Colour fades

When a target differs from its current preset, the drawn colour moves toward the target once
per draw. The steps are fixed per frame, **not** scaled by the frame-time factor:

- **Sky and cloud.** Each channel steps by ±0.0001 per draw. It counts as arrived when it is
  within 0.001. When all channels (six for the sky, three for the clouds) have arrived, the
  current preset becomes the target [exe DC 0x40cba6–0x40cdeb; constants 0x483470,
  0x48346c, 0x483474; 0x40d195–0x40d2cd; constants 0x483480, 0x48347c, 0x483484].
  - So a change of 0.45 (for example sky R from `SC_MORNING` 0.9 to `SC_DUSK` 0.45) takes
    about 4500 draws, 75 s at 60 frames per second (derived).
  - The colours fade only while they are drawn: the sky colour while the sky is drawn, the
    cloud colour while the clouds are drawn [exe DC 0x409524, 0x40d990].
- **Screen tint.** At the start of a fade, each of the four channels gets a step of
  |target − current| × 10 × 0.0001 = |Δ| / 1000. So every channel arrives after about 1000
  draws, about 17 s at 60 fps, whatever the distance [exe DC 0x40d576–0x40d60b; constants
  0x48348c = 10.0, 0x483494 = 0.0001; arrival 0x48349c = 0.001].
- **During a fade**, the colour drawn is the moving one. When settled, it is the target
  preset's table value [exe DC 0x40cdeb–0x40ce0e, 0x40d2d2–0x40d313, 0x40d444–0x40d474].

### Clouds (DC)

The clouds are drawn on a dome mesh [exe DC 0x40d990]. The dome is built once at start-up
with radius 150, 5° steps in both angles, and texture repeats 12 (*u*) and 4 (*v*) [exe DC
0x40717e–0x407199 → 0x418690]:

- **Mesh.** It is a hemisphere around its local +*z* axis, *φ* from 0° to 85° and *θ* from 0°
  to 355° in 5° steps. Each cell gives four vertices, (*φ*, *θ*), (*φ* + 5, *θ*),
  (*φ*, *θ* + 5) and (*φ* + 5, *θ* + 5), with x = r sin*φ* cos*θ*, y = r sin*φ* sin*θ*,
  z = r cos*φ*. There are 72 × 18 × 4 = 5184 vertices, all drawn as **one triangle strip**
  [exe DC 0x4186aa–0x4186dc, 0x418787–0x418969; constants 0x483e34 = 360, 0x483e38 = 90,
  0x483e3c = 4; strip: 0x40da70 `glBegin(5)`].
- **Texture coordinates.** For the unit vector *n* of each vertex,
  *u* = 12 · atan2(*n*ₓ, *n*_z) / 2π + 0.5 and *v* = 4 · asin(*n*ᵧ) / π + 0.5. A second pass
  adds 1.0 to a coordinate whenever it trails a neighbour by more than 0.9 (*u*) or 0.8
  (*v*) [exe DC 0x4188b0–0x4188fd; constants 0x483e44 = 1/2π, 0x483e4c = 1/π, 0x483e5c = 0.5,
  0x483e54 = 0.9, 0x483e58 = 0.8].
- **Placement.** For the clouds, the dome is moved by (0, −100, 0) and rotated 270° about
  *x*. Its pole points up, its rim lies 100 units below the camera, and its top is 50 above
  [exe DC 0x40da1d–0x40da3e].

**Drawing.** Two passes, both with blend (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) [exe DC
0x40da43–0x40db3d]:

1. `clouds1.tga` (slot 0x1e), colour = cloud colour, alpha = α₁;
2. `clouds2.tga` (slot 0x1f), colour = cloud colour − 0.05 per channel, alpha = α₂.

**Scrolling.** Both layers share one *v* offset. Per frame it grows by
f × 0.00005 × ⌊`SC_SKY_SPEED` / 10⌋, where f is the [frame-time factor](#frame-time-factor)
[exe DC 0x40d998–0x40d9c5; constant 0x4834a4]. The default `SC_SKY_SPEED` is 15, so the
factor is 1 [exe DC 0x40f370]. *v* follows the latitude about the dome's local *y* axis,
which the rotation lays along world *z*. So the clouds drift along one horizontal axis,
passing overhead (derived).

**Crossfade between the layers.** α₁ starts at 1.0 and α₂ at 0.0 [data scream.exe @VA
0x48bbb8, 0x48bbbc]. Each frame, a six-phase cycle steps them. The steps are not scaled by the
frame-time factor [exe DC 0x40d9cb–0x40dc6f; constants 0x4834a8 = 0.002, 0x4834b0 = −0.002,
0x4834ac = 0.015, 0x4834b4 = 0.5]:

| Phase | Change per frame | Ends when |
|---|---|---|
| 0 | α₂ + 0.002 | α₂ ≥ 0.5 |
| 1 | α₁ − 0.002, α₂ + 0.002 | α₂ ≥ 1.0 |
| 3 | hold: a timer + 0.015 | timer ≥ 1.0 (67 frames) |
| 4 | α₁ + 0.002 | α₁ ≥ 0.5 |
| 5 | α₂ − 0.002, α₁ + 0.002 | α₁ ≥ 1.0 |
| 6 | hold, as in phase 3 | timer ≥ 1.0, then back to phase 0 |

There is no phase 2. Simulated in single-precision floats, the first pass of phases 0–1 takes
500 frames. After that, the cycle swings between about (α₁, α₂) = (0.5, 1.0) and (1.0, 0.5):

- about 250 frames for each crossfade;
- 67 frames for each hold;
- about 635 frames per cycle, roughly 10.6 s at 60 fps.

The lower value creeps up by about 0.004 per cycle (derived: float32 simulation of the cited
constants).

---

## Fog

`SC_FOG` draws a fog dome [exe DC 0x40dc80]:

- the cloud dome mesh, moved and rotated as for the clouds;
- `clouds1.tga`, colour (1, 1, 1, 0.5), blend (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`).

Its own *v* offset grows by f × 0.00005 × ⌊`SC_SKY_SPEED` / 10⌋ per call [exe DC
0x40dc9e–0x40dcbf; constant 0x4834b8].

- **Node frame.** The fog is drawn over grey (0.25), twice, so its offset advances twice per
  frame [exe DC 0x40949a–0x4094bd].
- **Zoom frame.** It is drawn once, over black [exe DC 0x40a9af–0x40a9bb].

---

## Rain and lightning

### Rain

`SC_RAIN` draws the dome mesh twice, unmoved, so it is centred on the camera [exe DC
0x40dda0]:

1. rotated 270° about +*y*, blend (`GL_DST_COLOR`, `GL_ONE_MINUS_SRC_ALPHA`);
2. rotated 270° about −*y*, blend (`GL_ONE`, `GL_ONE_MINUS_SRC_ALPHA`).

[exe DC 0x40ddbc–0x40dde9, 0x40de87–0x40deb1]

The two hemispheres face −*x* and +*x*, so together they surround the view (derived).

- **Texture.** Both passes use `rain_def.tga` (slot 0x25), colour (1, 1, 1, 0.5), with the dome
  coordinates × 2.
- **Scrolling.** The *v* offset grows by f × 0.035 per frame [exe DC 0x40dda4–0x40ddb6;
  constants 0x4834bc = 0.035, 0x4834c0 = 2.0].
- **The image.** `rain_def.tga` is 256 × 256, fully opaque (alpha 255). Its colour is
  near-black: channel means 7–12, maximum 105 [data scream.res: rain_def.tga]. Over the white
  backdrop, the two passes give about 0.25 + 1.5 × texel (derived from the blends).

### Lightning cycle

The cycle runs in the lighting pass while `SC_RAIN` is on and `SC_CANDLE` and `SC_NIGHT` are
off. It is timed in real milliseconds (`GetTickCount`), not frames [exe DC 0x409b5d,
0x41c1f0].

**Schedule.** Seconds since the cycle start set the flash state [exe DC 0x409f28–0x40a058;
constants 0x48334c = 0.001, 0x483354–0x4833ac]:

| From (s) | 10.0 | 10.1 | 10.15 | 10.35 | 15.0 | 15.5 | 20.0 | 20.1 | 20.2 | 20.3 | 30.0 | 30.5 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| State | 1 | 0 | 2 | 0 | 1 | 0 | 1 | 0 | 1 | 0 | 3 | 0, restart |

The cycle start is stored only at 30.5 s. When rain is first switched on after a long gap,
the first frame resets it, so the first flash comes about 10 s later (derived from
0x40a03e–0x40a058).

**Drawing.** First the image is drawn [exe DC 0x40a05d–0x40a132]:

- mode 0: the faces, with the current blend;
- mode 1: colour (0.5, 0.5, 0.5, 0.5) and blend (`GL_DST_COLOR`, `GL_SRC_COLOR`), which
  multiplies by the texture;
- mode ≥ 2: added in white, blend (`GL_ONE`, `GL_ONE`).

Then, by state [jump table 0x409b38; exe DC 0x40a081–0x40a2ed]:

| State | Pass 1 | Pass 2: alpha-blended white | Visible result (derived) |
|---|---|---|---|
| 0 (no flash) | image again, alpha blend, colour (0.5, 0.5, 0.5, 0.5) | — | the scene darkened to 0.5 · scene + 0.25 · image |
| 1 | image added (`GL_ONE`, `GL_ONE`), colour 0.25 | alpha 0.05 | brighter, slightly whitened |
| 2 | image added, colour 0.5 | alpha 0.15 | bright flash |
| 3 | image added, colour 0.75 | alpha 0.2 | brightest flash |

The white pass draws the white set (node) or the white slot as a quad (zoom and video). In
the node, the white set's down face shows the rain texture (see [Sky](#sky-dc)).

[unverified: whether thunder sounds are synchronised to this cycle; no sound call is in
0x409b50]

---

## Screen tint

In the lighting pass's last branch, after the image, the screen colour is applied with blend
(`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) [exe DC 0x40a2ef–0x40a43c → 0x40d4e0]:

- **Node:** the mask set is drawn in the screen colour.
- **Zoom and video:** the zoom mask, or white if the zoom has none, is drawn as a quad.

Because the mask is luminance, the result is lerp(scene, mask × RGB, alpha) (derived):

- where the mask is white, the scene is mixed toward the tint colour;
- where it is black (sky openings), the scene is mixed toward black;
- a face or zoom with no mask is tinted uniformly.

The `SC_MORNING` tint has alpha 0, so it changes nothing.

---

## Candle and lamp

The flicker runs after the lighting branch while `SC_CANDLE` or `SC_LAMP` is on [exe DC
0x409bc3–0x40a6fc]. L is the `SC_LAMP` flag value: `scEnable` sets 1 and `scDisable` 0 [exe DC
0x40fbb9, 0x40fa60]. The timer is real time, `GetTickCount` ms × 0.001.

**Schedule.** Seconds since the cycle start, offset by 0.25 · L, set the flicker state [exe DC
0x409bd0–0x409ec4; constants 0x4833b4 = 0.25, 0x4833b8–0x483400]:

| From (s, + 0.25 L) | 0.1 | 0.2 | 0.3 | 0.4 | 0.75 | 0.85 | 0.9 | 1.0 | 1.1 | 1.2 | 1.3 | 1.4 | 1.8 | 1.9 | 2.0 | 2.15 | 2.2 | 2.3 | 2.4 | 2.5 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| State | 1 | 0 | 1 | 0 | 1 | 0 | 1 | 2 | 1 | 0 | 1 | 0 | 1 | 0 | 1 | 0 | 1 | 2 | 1 | 0, restart |

**Drawing.** Two additive passes, blend (`GL_SRC_ALPHA`, `GL_ONE`), colour (1.0, 0.75, 0.5, a)
[exe DC 0x40a465–0x40a6e7; constants 0x483404–0x483420]:

1. the white set (node), or the white slot as a quad (zoom and video), at alpha a₁;
2. the image again, at alpha a₂.

| State | a₁ | a₂ | a₁, a₂ with `SC_CANDLE` (L = 0) | a₁, a₂ with `SC_LAMP` (L = 1) |
|---|---|---|---|---|
| 0 | 0.01 − 0.05 L | 0.1 − 0.5 L | 0.01, 0.1 | below 0, so clamped to 0 [unverified: clamping] |
| 1 | 0.05 − 0.035 L | 0.5 − 0.35 L | 0.05, 0.5 | 0.015, 0.15 |
| 2 | 0.075 − 0.055 L | 0.75 − 0.55 L | 0.075, 0.75 | 0.02, 0.2 |

A candle thus adds a warm glow of up to 0.075 flat plus 0.75 × image. The lamp is weaker,
and its cycle is 0.25 s longer. With both flags on, L = 1 (derived).

For zoom and video, the second pass rebinds the zoom image (mode 1) or the video slot
(other modes) [exe DC 0x40a5cd–0x40a5f0, 0x40a6ba–0x40a6dd].

---

## Dream

`SC_DREAM` only changes the field of view: it sweeps between 35° and 55° (2006: 43° and 63°),
and is described in [camera.md](camera.md#dream-field-of-view-sc_dream) [exe DC
0x407c68–0x407cf8].

---

## Glow

`SC_GLOW` sets a flag that no drawing code reads [exe DC 0x40fb8d; the flag 0x4df8df0 is
only written, at 0x40f3ba, 0x40fb8d, 0x40fd58]. No glow is drawn.

---

## Gamma (F12)

F12 (virtual key 0x7b) steps the gamma setting (settings offset 0x24, default 0) and shows a
message [exe DC 0x41777b–0x417bbf; 0x40f2ec]:

| Level | Message (string address) | Alpha |
|---|---|---|
| 0 → 1 | `gamma correction LOW` (0x483dbb) | 0.33 |
| 1 → 2 | `gamma correction NORMAL` (0x483dd0) | 0.66 |
| 2 → 3 | `gamma correction HIGH` (0x483de8) | 1.0 |
| 3 → 0 | `gamma correction DISABLED` (0x483dfe) | (off) |

While the level is > 0, the frame does the following after the scene and before the
crossfade [exe DC 0x4082a0; alphas at 0x40838e = 0.33, 0x40839a = 0.66, 0x4083a6 = 1.0]:

1. It copies the window's lower-left 1024 × 1024 pixels into slot 0x23.
2. It adds that copy back over itself with blend (`GL_SRC_ALPHA`, `GL_ONE`), as the quad
   x 0–1024, y 64–960, *t* 0.0625–0.9375.

The quad maps the copy 1:1 onto the screen. Every pixel above the bottom 64 rows becomes
min(1, c × (1 + alpha)): a linear brightening by 1.33, 1.66 or 2.0 (derived), not a gamma
curve.

The same routine runs in the node, zoom, overlay, video, menu and journal frames
[exe DC 0x409500, 0x40a8ca, 0x40ab82, 0x40a814, 0x408c4e, 0x40b11d]. The 2006 build does the
same [exe 2006 0x407db0].

---

## View crossfades

### Capture

A crossfade starts when the engine captures the screen. Both capture routines copy the
window's lower-left S × S pixels into slot 0x22, where S is `SC_IMG_SIZE` (settings offset
0x1bc, default 1024), and set the fade running. They differ only in the starting progress p:

| Kind | Starts at | Length at the default `SC_MOV_SPEED` (derived) | Used by |
|---|---|---|---|
| full crossfade | p = 0 [exe DC 0x407ad0, 0x407b0e] | 40 frames | every view change (see [Triggers](#triggers)) |
| short crossfade | p = 0.75 [exe DC 0x407b20, 0x407b5b = 0x3f400000] | 10 frames | the camera keys and the slideshow-mode edge click only |

Before capturing, the callers draw one more frame of the old view with a flag set that skips
the text and the cursor [exe DC 0x4159e4–0x4159f3, 0x416b71–0x416b8b, 0x416bf5–0x416c22].

### Each frame

While the fade runs, the frame draws slot 0x22 as the quad x 0–1024, y 64–960 (*t*
0.0625–0.9375). It uses colour (1, 1, 1, 1 − p) and blend (`GL_SRC_ALPHA`,
`GL_ONE_MINUS_SRC_ALPHA`). The quad is drawn after gamma, and before the text and cursor
[exe DC 0x409584–0x409633, 0x40a9d8–0x40aa8f, 0x40aae0]. The old frame thus fades out over
the live new view.

**Progress.** p grows by `SC_MOV_SPEED` × 0.001 × f per frame. At p ≥ 1 the fade ends [exe DC
0x409467–0x409477, 0x409638–0x40966b; constant 0x48333c = 0.001]:

| `SC_MOV_SPEED` | Duration at f = 1 (derived) |
|---|---|
| 25, the default [exe DC 0x40f366] | 40 frames, about 0.67 s |
| 2, set by the DC around its intro images [script DC: newGameTLV] | 500 frames, about 8.3 s |
| `SC_LO` / `SC_MED` / `SC_HI` = 25 / 50 / 75 (see [api.md](api.md#scset)) | 40 / 20 / 14 frames |

### Triggers

These are all the callers of the two capture routines in the DC exe [exe DC: every `CALL
0x407ad0` and `CALL 0x407b20`].

**Full crossfade** (p from 0):

| Event | Source |
|---|---|
| every `scSwitch` view change: node, room, zoom, zoom out, zoom back, overlay | [exe DC 0x415966, 0x4159f3, 0x415ad6, 0x415c50, 0x415d6f] |
| `scTextureUpdate` with `visible` ≠ 0 | [exe DC 0x414ed6] |
| the end of `scCutscene`, and the end of a video in the per-frame routine | [exe DC 0x412473, 0x416d13] |
| opening and closing the menu, and menu actions | [exe DC 0x412e9c, 0x413112, 0x413185, 0x4136d7, 0x41390b, 0x413967, 0x4139a4, 0x413dfb] |
| opening and closing the inventory with a right click | [exe DC 0x4174e7, 0x417512] |
| Esc closing the inventory; Esc dismissing an overlay | [exe DC 0x417965, 0x417a3a] |
| a click dismissing an overlay | [exe DC 0x414524] |
| opening the journal (`scJournalShow` or the "journal" item) and closing it | [exe DC 0x4185b8, 0x418600] |
| `scInit`, from the loading screen | [exe DC 0x4104d2] |

**Short crossfade** (p from 0.75):

| Event | Source |
|---|---|
| the camera keys (see [input.md](input.md#camera-keys)) | [exe DC 0x41761a, 0x417888, 0x4178f8, 0x417bd2, 0x417c1a] |
| a click in an edge zone in slideshow mode (see [camera.md](camera.md)) | [exe DC 0x4173f3] |

---

## Frame-time factor

The frame-time factor f (60 ÷ the mean frame rate since start-up) is defined in
[mainloop.md](mainloop.md#frame-timing). In drawing:

- **scaled by f:** the crossfade, and the cloud, fog and rain scrolling [exe DC 0x409638,
  0x40d9b1, 0x40dc9e, 0x40dda4];
- **not scaled:** the colour fades, the cloud-layer crossfade, the gradient slide and the dream
  sweep (see their sections).

---

## Overlays

The overlay frame draws the previous state's frame, then the overlay image on top [exe DC
0x416ec6–0x416f38, 0x40aae0]:

1. slot 0x1c, RGBA, with blend (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`) and opaque white, as
   quad mode 0;
2. gamma;
3. the crossfade.

The view underneath keeps being drawn live. No shipped script calls `scOverlay` (see
[api.md](api.md#scoverlay)).

---

## Video frames (`scCutscene`)

Each video frame is uploaded into slot 0x1a with `glTexSubImage2D` at (0, 0), at the frame's
size, as `GL_BGR`. It is then drawn with blend (`GL_ONE`, `GL_ONE`) and opaque white, through
the lighting pass [exe DC 0x40a710–0x40a7ca]:

| Frame width w | Lighting mode | Quad (see [placement](#zoom-quad-placement)) |
|---|---|---|
| w < 800 | 3 | mode 2: a 640 × 352 frame fills x 0–1025, y 63–704 |
| 800 ≤ w < 1024 | 4 | mode 3: an 800 × 480 frame fills x 0–1025, y 64–704 |
| w ≥ 1024 | 1 | mode 0, sized by the **zoom** slot's width |

[exe DC 0x40a74c–0x40a80f]

- **Wide video, quad size.** If the last zoom image was narrower than 1024, a 1024 × 640 video
  is drawn on the 1312-wide quad. It then covers x 0–1312, y −164–704: enlarged 1.28× and
  cropped (derived). [unverified: whether this happens in play]
- **Wide video in rain.** With `SC_RAIN` on, a ≥ 1024-wide video gets a second lighting pass
  in mode 2 [exe DC 0x40a7cf–0x40a7e3].
- **Effects.** Night, rain flashes, screen tint, candle and gamma all apply to video frames
  (derived from the lighting pass).
- **End.** When `scCutscene` ends, the last video frame is captured and crossfades into the
  next view [exe DC 0x41246e–0x412473].

**Loading screen.** The splash (slot 0x1d, `sc_logo.tga` or `SC_SPLASH`) is drawn after a
clear, as the mode-0 quad x 0–1312, y 704 to −685, with blend (`GL_ONE`, `GL_ONE`) [exe DC
0x407a20–0x407abc]. The shipped scripts set `SC_SPLASH` to `back-loading.tga`, an 800 × 472
8-bit grey TGA [script DC: top level] [data dc/scratches.res: back-loading.tga].

---

## 2006 differences

The 2006 frame code follows the DC layout. The GL import names were checked against the 2006
import table [exe 2006 0x466df0–0x466f34]. The settings block starts at 0x2f0bb34, and the
effect flags sit 4 bytes earlier than in DC: `SC_SKY` 0x2f0bdb4, `SC_NIGHT` 0x2f0bdb8,
`SC_RAIN` 0x2f0bdc0, `SC_FOG` 0x2f0bdc4, `SC_DREAM` 0x2f0bdc8, `SC_CANDLE` 0x2f0bdcc,
`SC_LAMP` 0x2f0bdd0 [exe 2006 0x408da0–0x408e6c, 0x409462, 0x407878, 0x4094c3].

### Projection and field of view

- **Viewport.** The viewport is the same, (0, 64, W, H − 128) [exe 2006 0x40c370–0x40c416].
- **Aspect.** The perspective aspect is **W / H**, not W / (H − 128), with near 0.1 and far
  1000 [exe 2006 0x40c42a–0x40c45b]. What that does to the picture, together with the 2006
  30 × 40 face box, is worked out in [camera.md](camera.md#projection-and-viewport).
- **Field of view.** The default is **53°**, and `SC_DREAM` sweeps it between 43° and 63°
  [exe 2006 0x40d43c–0x40d45f, 0x407878–0x4078fe] (see [camera.md](camera.md)).

### Sky: procedural clouds, no colours

The 2006 sky has no colours, gradient or second layer. The exe contains no `clouds1.tga`,
`clouds2.tga` or `gradient.tga` string [exe 2006: string search], and 2006 has no
`scSetAmbience` (see [api.md](api.md#scsetambience)).

**Node frame.** With `SC_SKY` on, the node frame draws [exe 2006 0x408e44–0x408e67,
0x408de2–0x408dff]:

1. the white set in **white**;
2. **one** cloud layer: the dome, moved (0, −100, 0) and rotated 270° about *x*, with colour
   (1, 1, 1, 1) and blend (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`), textured from slot 0x1e
   [exe 2006 0x40bf00].

The masks and faces are then drawn as in DC.

- **Scrolling.** The cloud *v* offset grows by f × 0.00015 per frame, with no `SC_SKY_SPEED`
  term [exe 2006 0x40bf00; constant 0x46a2a8].
- **Dome.** It is built by the same routine with radius 160, 5° steps, and texture repeats 16
  (*u*) and 8 (*v*) [exe 2006 0x406e92–0x406ead → 0x415490].

**Generating the cloud texture.** Slot 0x1e is filled by code, not from a file. This happens
once, while the textures are set up [exe 2006 0x4072b3–0x4072c2 → 0x40be00]:

1. **Noise maps.** Three 32 × 32 noise maps are filled [exe 2006 0x40be0b–0x40be30 →
   0x415cd0 → 0x415d90], each with its own seed, `rand() % 5000` [exe 2006 0x415da3–0x415db4].
   - Each value is 128 × h(x, y, seed) + 128 over a 34 × 34 grid with wrapped borders
     [exe 2006 0x415dd9–0x415e5c; constant 0x46ab1c = 128].
   - The hash is h = 1 − ((n (n² · 15731 + 789221) + 1376312589) & 0x7fffffff) / 2³⁰, with
     n = x + 57 y + 131 seed, then n = (n ≪ 13) ^ n [exe 2006 0x415d30–0x415d8d; constant
     0x46ab18 = 2⁻³⁰].
   - Each map is then smoothed: 0.25 × centre + 0.125 × the 4 edge neighbours + 0.0625 × the
     4 corner neighbours [exe 2006 0x415ec2–0x415f39; constants 0x46ab20, 0x46ab24, 0x46ab28].
2. **Octaves.** Only the third map is used [exe 2006 0x415d10 → 0x416050 with map +0x10]. A
   256 × 256 float map is the sum of 4 octaves, j = 0…3. Octave j samples the 32-periodic
   map bilinearly at (x, y) / 2^(3 − j) and is divided by 2^j [exe 2006 0x416050–0x41612d,
   0x415f50–0x416044].
3. **Cover and sharpness.** Each value v becomes c = 255 − 255 × 0.996^max(v − 205, 0)
   [exe 2006 0x416140–0x416192; constants 0x434d0000 = 205.0, 0x46ab3c = 0.996, 0x46ab34 =
   255].
4. **Upload.** c is rounded and written to R, G, B and A, then uploaded as RGBA
   [exe 2006 0x40be62–0x40bee7].

The map size is the setting at 0x1c0, default 256 [exe 2006 0x406e87, 0x40d587]. The
generator's loops are fixed at 256 [exe 2006 0x416106, 0x416112].

Results:

- v up to 205 gives a transparent texel;
- the brightest texels reach about c = 170, since v is at most about 480 (derived);
- over the white backdrop, a cloud texel shows as c² + (1 − c) (derived): pale grey on white.

[unverified: whether `rand` is seeded, i.e. whether the clouds differ between launches]

The DC exe has no such generator: the hash constant 15731 and the value 205.0 do not occur
in it [exe DC: listing search]. It only allocates and frees a similar buffer set [exe DC
0x40716e → 0x418e20, 0x407b79 → 0x418e80].

### Fog and rain

- **Fog**, node frame: grey (0.25) white set, then the fog twice [exe 2006 0x408dba–0x408ddd].
  The fog itself differs from DC [exe 2006 0x40bff0; constants 0x46a2ac = 0.0001,
  0x46a2b0 = 0.25]:
  - it is two passes of the camera-centred sphere, rotated 270° about +*y* and −*y*, like the
    DC rain;
  - it uses the procedural cloud texture, colour (1, 1, 1, 1), blend (`GL_ONE`,
    `GL_ONE_MINUS_SRC_ALPHA`), coordinates × 0.25;
  - its ***u*** offset scrolls by ± f × 0.0001: the first pass one way, the second the other.
- **Rain** is as in DC, but scrolls at f × **0.045** per frame [exe 2006 0x40c1b0; constants
  0x46a2b4, 0x46a2b8 = 2.0].

### Zoom frame (2006)

The 2006 zoom frame (0x409fc0) differs from the DC's:

- **White first.** Before the backdrop, it adds a white quad and then the white set (white)
  [exe 2006 0x40a07a–0x40a0b6].
- **Blends.** The mask quad uses (`GL_DST_COLOR`, `GL_ONE`) after sky or fog, and
  (`GL_SRC_COLOR`, `GL_DST_COLOR`) after rain [exe 2006 0x40a0bb–0x40a159].
- **Image.** The zoom image is drawn with colour 0.5 and blend (`GL_DST_COLOR`,
  `GL_SRC_COLOR`): result = image × scene [exe 2006 0x409808 onward, decompiled 0x409450].
- **Placement.** Quads are placed by screen centre, not fixed coordinates [exe 2006
  0x409280–0x409435]:
  - mode 0 draws a T × T quad (T = the setting at 0x1b8, default 1024 [exe 2006 0x40d4f6, 0x40d566]) shifted so that a w × h
    image appears at native size, centred in the window;
  - in the shipped 800 × 600 window, an 800 × 472 zoom fills the 3D band exactly (derived).

### Lighting pass

The constants are identical to DC: night colour, lightning schedule and alphas, candle
schedule and alphas [exe 2006 0x409450; constants 0x46a19c–0x46a270]. The differences:

- There is **no screen-tint pass**. The last branch draws only the image: mode 0 the faces,
  mode 1 the multiplying quad, and nothing for modes ≥ 2 [exe 2006 decompiled 0x409450, first
  `if` after the night branch].
- **Video** is drawn before the lighting pass, with the quad helper [exe 2006
  0x409f0b–0x409f86]:
  - frames narrower than 800 use mode 2, and other frames mode 1 [exe 2006 0x409f0f].

  [unverified: the exact 2006 video quad in each mode. It is built from computed values
  through helper calls (0x409450, 0x407db0), and two earlier readings of that code disagreed]

  The lighting pass then runs with mode + 1 [exe 2006 0x409f8b–0x409f92].

### Unchanged

- **Textures.** All 2006 textures use `GL_REPEAT`. The filter follows `QUALITY` as in DC
  [exe 2006 0x40b46e–0x40b4c6].
- **Gamma, crossfade, frame-time factor.** These are the same:
  - gamma: same alphas and quad [exe 2006 0x407db0; 0x407e9f, 0x407eab];
  - crossfade: the same quad, with progress `SC_MOV_SPEED` × 0.001 × f [exe 2006
    0x408d87–0x408d97, 0x408ea4 onward];
  - the gamma message strings are present [exe 2006: string search].

---

## Open questions

- [unverified: GL colour clamping of the negative lamp alphas; fixed-function GL clamps the
  current colour, so they are expected to draw nothing]

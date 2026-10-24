# Screen layouts

This page describes what the original SCream engine (`scream.exe`) draws on each screen of
*Scratches*: positions, sizes, colours, fades, drawing order, and the click rectangles of
each screen. Keyboard and mouse handling, cursor images and hotspot picking in the scene are
described separately. The functions are in [`api.md`](api.md), the file formats in
[`formats.md`](formats.md).

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables listed in [api.md](api.md#sources),
> the shipped scripts decompiled with unluac, and measurements of the shipped files. A person
> has not yet checked it. A source tag says where a claim can be checked; it is not a
> guarantee. Check a claim against its source before you build on it, and treat
> `[unverified]` claims as open questions.

## Sources

The source tags are those of [api.md](api.md#sources) and [formats.md](formats.md#sources):
`[exe DC 0x…]`, `[exe 2006 0x…]`, `[script DC: name]`, `[script 2006: name]`,
`[data FILE …]` and `[unverified: …]`. The `.res` files measured are the ones listed in
formats.md. Addresses are virtual addresses (image base 0x400000).

The 2006 build is the patched 2006 `scream.exe`. Its routines are named here by address only.

## Conventions

**Coordinates.**
- All positions are in screen pixels of the configured mode, with the origin at the top-left
  and y growing downwards. W and H are the `WIDTH` and `HEIGHT` settings. The shipped modes
  are 1024×768 (DC) and 800×600 (2006) [data dc/scream.cfg, 2006/scream.cfg].
- cx = W/2 and cy = H/2, rounded towards zero [exe DC 0x40855e–0x40857f].
- The 2D layer is an orthographic view of exactly W×H pixels over the whole window
  [exe DC 0x40b620] [exe 2006 0x40ac90].
- Some elements are placed with fixed numbers that do not follow W and H. They are marked
  **fixed**, and numbers for them are given for the shipped mode of that build.
- Rectangles are written [x0, x1] × [y0, y1]. Hit rectangles include both edges
  [exe DC 0x417c90].
- Mouse positions use the same top-down pixels. The cursor is drawn at the mouse position
  converted the same way as text [exe DC 0x40b408–0x40b430].

**Text.**
- The engine has one text routine. It takes a position (x, y), a texture slot and the text
  [exe DC 0x40c040] [exe 2006 0x40b660].
- Each character is a 16×16-pixel cell, and the pen moves 10 pixels per character. The font
  atlas is in [formats.md](formats.md#fonts).
- x is the left edge of the first cell. y is the **bottom** edge of the cells: a line drawn at
  y covers rows y−16 to y [exe DC 0x40c0bf–0x40c0fe; the glyph quad spans 0..16 upwards,
  0x40bf20].
- Text is blended with normal alpha (source alpha, one minus source alpha) in the current
  colour [exe DC 0x40c082–0x40c095].
- Every text below uses texture slot 24, the font loaded by `scFont` (or the journal font while
  the journal is open), unless stated [exe DC 0x40ade5, 0x40b2d1, 0x408a62, 0x40b113].
- The text is used as a `printf` format with no arguments. A `%` in a text is therefore read
  as a conversion [exe DC 0x40c05f–0x40c07d; 0x41ba90 is the `vsprintf` used by `sprintf`
  0x41c290].
- The text is formatted into a 256-byte buffer [exe DC 0x40c044 `SUB ESP,0x108`]
  [unverified: what a longer text does].
- Centred text uses x = cx − (len ÷ 2) × (W ÷ (W ÷ 10)), in integer arithmetic, plus a small
  offset given below. For any W from 100 up the factor is 10 [exe DC 0x40ad99–0x40ade4].

**Colours** are given as (R, G, B, A) [exe DC: the `glColor4f` arguments].

**Frame-rate factor.** Fades advance by a fixed step each frame, multiplied by the engine's
speed factor ([mainloop.md](mainloop.md#frame-timing)) [exe DC 0x40ad4c, 0x4098d9, 0x40941c].
A step is therefore "per 1/60 s", and the steps below are written that way.

**Settings that drive drawing** (offsets into the settings block of
[formats.md](formats.md#3-settings-block)):

| Setting | DC offset | Default | Source |
|---|---|---|---|
| text speed (`SC_TXT_SPEED`) | 0x1a8 | 50 | [exe DC 0x40f3de, 0x40fe58] |
| move speed (`SC_MOV_SPEED`), the rate of every crossfade | 0x1ac | 25 | [exe DC 0x40f366; used at 0x40b0a9, 0x409689, 0x408581] |
| picture slot size N | 0x1bc | 1024 | [exe DC 0x40f384; used at 0x407ae5] |
| inventory icon size S | 0x1c8 | 64 | [exe DC 0x40f3ea; used at 0x409695, 0x417c9c] |
| gamma level (0–3) | 0x024 | 0 | [exe DC 0x40f2ec; menu 0x4090db; key 0x417784] |
| slideshow mode | 0x278 | 0 | [exe DC 0x40f465; menu 0x4091c9] |
| alternate control | 0x2ac | 1 | [exe DC 0x40f451; menu 0x40924f] |

---

## The frame

### Clearing and the bars

- Every frame starts by clearing the window to black [exe DC 0x407c45; clear colour
  (0, 0, 0, 0) at 0x407059–0x407061].
- The 3D scene is drawn into a viewport of W × (H − 128) pixels, starting 64 pixels below the
  top. This leaves a black bar of 64 pixels at the top and at the bottom [exe DC
  0x40df60–0x40e041] [exe 2006 0x40c370–0x40c416].
- The bars are only cleared screen. Nothing draws a bar image [exe DC 0x40df60, 0x407c30].
- The feedback text and choice rows are drawn into the bottom bar (see [Text](#text-on-screen)).

### The picture area

Still pictures (zooms, the menu background, the journal page, the splash screen, overlays and
videos) fill the area between the bars:

- **DC: fixed.** x 0–1024, y 64–704, measured from the bottom of the window. At 1024×768 that
  is [0, 1024] × [64, 704], the same area as the 3D viewport. A picture is either scaled to
  fill it or drawn 1:1, depending on its size
  (see [Pictures](#pictures-zoom-overlay-video-splash)) [exe DC 0x409960, 0x4083c0, 0x407a20].
- **2006.** Each picture is drawn 1:1, centred on (cx, cy). An 800×472 picture at 800×600
  covers [0, 800] × [64, 536] [exe 2006 0x409280, 0x407ed0, 0x4075c0].

### What each state draws

What each engine state draws, back to front, and when the cursor shows are in
[mainloop.md](mainloop.md#what-each-state-draws) [exe DC 0x416a10, jump table 0x4169d0]. Two
points matter for layout:

- Inside the node and zoom frames, gamma is applied first and the crossfade after it; feedback
  text and the cursor are drawn after both, so they never fade or brighten with the picture
  [exe DC 0x409509, 0x409584, 0x416b62–0x416bbe].
- The choice state draws no feedback text [exe DC 0x416de4–0x416e25].

The 2006 build draws the same layers, without the book state [exe 2006 0x414220, jump table
0x4141e4 (8 entries)].

---

## Crossfades and gamma

### Crossfade

Most screen changes crossfade from the old picture to the new one:

1. When the change starts, the engine copies the lower-left N×N pixels of the window
   (N = picture slot size, 1024) into a texture [exe DC 0x407ad0, 0x407b20] [exe 2006 0x407700,
   0x407750].
2. Each frame of the new screen draws that copy back over the picture, in place, at 1:1.
   - Its alpha is 1 − F [exe DC 0x40935c–0x409413; same in 0x409584, 0x40a9d8, 0x40ab89,
     0x40b13e].
   - The copy covers everything except the bottom 64 rows (the bottom bar) [exe DC 0x4093a5:
     quad from y 64 to 960 above the bottom edge, texture rows 1/16–15/16].
3. F rises by move speed × 0.001 per 1/60 s [exe DC 0x408581–0x40858c, 0x409418–0x409428;
   constant 0.001 at 0x483338]. At the default speed 25, a fade from F = 0 takes 40 frames at
   60 fps (2/3 s).
4. The fade ends when F reaches 1 [exe DC 0x40942e–0x409446].

Details:
- There are two kinds: the full crossfade from F = 0, used by every view change, and the
  short one from F = 0.75, used only by the camera keys and the slideshow edge click
  [exe DC 0x407b0e, 0x407b5b]. The list of triggers is in
  [rendering.md](rendering.md#triggers).
- Feedback text and the cursor are drawn after the crossfade, so they never fade with it
  [exe DC 0x416b62–0x416bbe].
- The copy is not scaled. At window sizes over N, the part of the window outside the copy does
  not crossfade [exe DC 0x407aeb–0x407aff].
- The 2006 build is the same, with N = 1024 [exe 2006 0x40a167–0x40a256; N default at
  0x40d566].

### Gamma correction

At gamma levels 1–3 (F12), every screen is brightened after it is drawn, by adding a copy of
it back at alpha 0.33, 0.66 or 1.0; the bottom 64 rows are left out. Details are in
[rendering.md](rendering.md#gamma-f12) [exe DC 0x4082a0].

---

## Text on screen

### Feedback text

The text set by `scFeedback`, `scFeedbackEx`, dialogue lines, subtitles and the engine's own
messages. Line splitting and the duration are in [api.md](api.md#scfeedback).

**Placement** [exe DC 0x40aca0] [exe 2006 0x40a430]:
- Each line is centred: x = cx − 10 × (len ÷ 2) + 1 [exe DC 0x40ad99–0x40ade4].
- The last line of the text is the lowest [exe DC 0x40e150 stores the lines in reverse;
  0x40ad80 draws slot 0 lowest].
- The bottom edges of the lines (top line first) are:

| Lines in the text | Lines shown | Bottom edges | At 1024×768 | At 800×600 |
|---|---|---|---|---|
| 1 | 1 | H−24 | 744 | 576 |
| 2 | 2 | H−32, H−16 | 736, 752 | 568, 584 |
| 3 | 3 | H−40, H−24, H−8 | 728, 744, 760 | 560, 576, 592 |
| 4 | 3 | H−40, H−24, H−8 | 728, 744, 760 | 560, 576, 592 |

Source: [exe DC 0x40ad0d–0x40afbd; jump table 0x40ac88] [exe 2006 0x40a49f–0x40a76e; jump
table 0x40a41c].

**Four-line texts.** Until half the text's duration has passed, the first three lines are
shown. After that, the last three are shown, in the same three positions [exe DC
0x40af6d–0x40afa7; constant 0.5 at 0x48343c]. This agrees with api.md.

**Colour and fade.** The text is white (1, 1, 1) and its alpha is 1 − T, where T is a
transparency value [exe DC 0x40ad5e–0x40ad7b].
- A new text starts fully transparent (T = 1) [exe DC 0x40af1f–0x40af21].
- **Fade-in:** while the time shown is at most the duration and T > 0, T falls by
  text speed × 0.001 per 1/60 s [exe DC 0x40ad36–0x40ad58, 0x40afc2–0x40afe3; 0.001 at
  0x48342c].
- **Fade-out:** after the duration, T rises by text speed × 0.0001 per 1/60 s, ten times slower
  [exe DC 0x40ad48–0x40ad58; 0.0001 at 0x483430].
- When T reaches 1, the text is removed [exe DC 0x40af05, 0x40b008–0x40b01a].
- **T is updated once per line drawn.** A three-line text therefore fades in and out three times
  as fast as a one-line text [exe DC: the update sits inside the line loop 0x40ad36–0x40aeff].
- At the default text speed (50), a one-line text fades in over 20 frames and out over 200
  frames, at 60 fps.
- Time is measured in seconds from when the text was set [exe DC 0x40acc5–0x40acfa; 0.001 at
  0x483434].

**Glow pass (not used).** A second pass exists that redraws each line three more times with a
different blend (source colour, destination colour) and a pulsing alpha [exe DC
0x40aded–0x40aee8]. It runs only when settings offset 0x27c is non-zero. Only the defaults
routine writes that field, and it writes 0, so the pass never runs in the shipped game
[exe DC 0x40f46b; no other writer of 0x4df8de0]. The 2006 build has the same pass
[exe 2006 0x40a580].

### Choice list

The list shown by `scChoiceGo`, and the yes/no prompts of the menu. Both use one drawing
routine [exe DC 0x40b250] [exe 2006 0x40a860].

**Rows.**
- The list is a stack of rows. Row 0 is the lowest.
- Row i has its bottom edge at H − 8 − 16i. When the list has exactly four rows, every row is
  16 pixels lower: H + 8 − 16i [exe DC 0x40b269–0x40b283, 0x40b342–0x40b359].
- In a four-row list, row 0 is therefore half below the bottom of the window.
- No x test is made when clicking on rows (see below).

**In-game choices** (`scChoiceGo`):
- Rows are left-aligned at x = 16 [exe DC 0x40b2b7; the renderer passes 0 at 0x416e0e].
- `scChoiceGo` fills the list from option index *count* down to 0 [exe DC 0x415890–0x4158b5].
  - That is **count + 1 rows**. Row 0 holds the unused text slot just past the last option,
    which is normally empty.
  - Row 1 is the last option, and the top row is the first option.
  - With 16 options, that slot is the start of the handler-name table, so row 0 shows the first
    option's handler name [exe DC 0x403e5a (texts at +0x3c, 96 bytes each), 0x403e85 (handlers
    at +0x63c)].
- All rows are drawn in (0.1, 0.75, 0.75, 1) [exe DC 0x40b2a3–0x40b2bc].
- The row under the mouse is drawn again in (0.1, 1, 1, 1) [exe DC 0x40b2d6–0x40b310; index
  set by 0x40b240].

| Options | Rows (bottom edges, top first) | At 1024×768 |
|---|---|---|
| 1 | option 1: H−24; empty: H−8 | 744, 760 |
| 2 | 1: H−40; 2: H−24; empty: H−8 | 728, 744, 760 |
| 3 | 1: H−40; 2: H−24; 3: H−8; empty: H+8 | 728, 744, 760, (776) |
| 4 | 1: H−72; 2: H−56; 3: H−40; 4: H−24; empty: H−8 | 696 … 760 |

**Click rows, in-game** [exe DC 0x417fd3–0x418074, 0x413e70–0x413f63]:
- Row i (i ≥ 1) is picked by any click with y in [H − 32 − 16i, H − 16 − 16i]. That band is 8
  pixels above the drawn row.
- In a four-row list the band is [H − 16 − 16i, H − 16i].
- The mouse x is not tested.
- Row 0 can be highlighted (except in a four-row list) but never picked [exe DC 0x413f29].

**Menu prompts.** In the choice state over the menu [exe DC 0x412fa0, renderer 0x416e42–0x416ec1]:
- The menu is drawn as usual and three centred rows go on top.
- Each row is at x = cx − 10 × (len ÷ 2) − 4 [exe DC 0x40b35e–0x40b3a0].
- The top row is the question, in white (1, 1, 1, 1). The other two are in
  (0.1, 0.75, 0.75, 1), and the hovered one is drawn again in (0.1, 1, 1, 1) [exe DC
  0x40b3a3–0x40b3da].

| Prompt | Row 2 (H−40, white) | Row 1 (H−24) | Row 0 (H−8) | Source |
|---|---|---|---|---|
| New, no game running (DC) | "Which game do you want to play?" | "Scratches: Director's Cut" | "Scratches: The Last Visit" | [exe DC 0x413031–0x41304e] |
| New, game running | "Start a new game?" | "yes" | "no" | [exe DC 0x413079–0x413092] |
| Load, game running | "Load a saved game?" | "yes" | "no" | [exe DC 0x412fd5–0x412ffb] |
| Save over an existing slot | "Replace saved game?" | "yes" | "no" | [exe DC 0x413097] |
| Quit | "Quit the game?" | "yes" | "no" | [exe DC 0x41305e–0x413072] |

- **Click rows.** y in [H−32, H−16] picks row 1. y in [H−16, H] picks row 0. Both bands are 8
  pixels below the drawn rows. x is not tested [exe DC 0x417fd3–0x418015, 0x413e9f–0x413ec2].
- While a prompt is up, the menu button or slot row that raised it stays highlighted
  [exe DC 0x413668–0x41369a].
- Picking row 1 of the which-game prompt starts the first new-game handler, and row 0 the
  second [exe DC 0x41380d–0x41384f, 0x41377b–0x4137ca].
- With a game running, "yes" to "Start a new game?" ends the current game and then asks which
  game [exe DC 0x4138b3–0x4138e3].
- **2006:** the New button starts a new game at once when no game is running. With a game
  running it asks "Start a new game?". There is no which-game prompt [exe 2006 0x41135b–0x4113c6,
  0x410c50].

### Debug statistics (not shown)

A block of statistics can be drawn in the top-left corner:
- Lines: `screen_size: %d x %d`, `quality: high|low`, `vision_angle: %d`,
  `load_time_peak: %2.1fs`, `current_speed: %2.1f` [exe DC 0x40e5e0; strings 0x4834c7–0x48351f].
- Position: x = 1, bottom edges 84, 100, 116 … [exe DC 0x40b066–0x40b082].
- Font: texture slot 25 (the built-in font).
- Colour: (0.5, 1, 1, 0.75) [exe DC 0x40b042–0x40b056].

Its switch is only ever set to 0, at start-up, so the block is never shown [exe DC 0x40725c →
0x40e0f0; no other writer of 0x4df5118]. The 2006 build has the same block [exe 2006 0x40a7e0].

---

## Inventory

[exe DC 0x409680] [exe 2006 0x408fa0]

**Background.**
- The inventory has no image of its own. When it opens, the crossfade copy of the scene is taken.
- That copy is drawn in place, tinted (0.9, 0.75, 0.5), with alpha 1 − F [exe DC
  0x4096c1–0x409780].
- It covers the window except the bottom bar, as in a crossfade.
- The window behind it is black, so the scene appears darkened and sepia-tinted.

**Fade.**
- When the inventory opens after a finished inventory fade, F restarts at 0 [exe DC
  0x4096ab–0x4096bb, 0x409900–0x409918; finished flag starts at 1, data scream.exe @0x8bb88].
- F rises by move speed × 0.001 per 1/60 s and stops at 0.5 [exe DC 0x4098cc–0x40991d; 0.5 at
  0x483348].
- The background therefore dims from full brightness to half (alpha 1 → 0.5).
- The icons fade in with alpha 2F (0 → 1) [exe DC 0x4097f3–0x409814; 2.0 at 0x483344].
- At the default speed the fade takes 20 frames at 60 fps.

**Icon grid.** S is the icon size (64). Columns c and rows r count from 0. Each icon is drawn
S×S, with the full texture [exe DC 0x40978f–0x4098c6].

| | DC | 2006 |
|---|---|---|
| Columns × rows drawn | 10 × 7 (70 places) [exe DC loops at 0x4098a0, 0x4098c0] | 8 × 8 (64 places) [exe 2006 0x4091c3, 0x4091f5] |
| Icon x | 32 + c × (32 + S): 32, 128, … 896 [exe DC 0x409793–0x409898] | the same: 32, 128, … 704 [exe 2006 0x4090b7–0x4091bf] |
| Icon y | **fixed**: from the bottom of the window, 638 − 96r to 638 − 96r + S. At 768: [66 + 96r, 130 + 96r], rows down to [642, 706] [exe DC 0x40969b, 0x409769–0x409777, 0x4098a6–0x4098af] | **fixed**: from the bottom, 448 − 96r to 448 − 96r + S. At 600: [88 + 96r, 152 + 96r]. Row 5 runs off the bottom (568–632) and rows 6–7 are off-screen [exe 2006 0x408fb5, 0x40907e–0x409092, 0x4091d9–0x4091dc] |
| Inventory position shown | 10r + c + 1 | 8r + c + 1 |

- Places are filled in inventory order. An empty place draws nothing [exe DC 0x4097c0–0x4097c8].
- The icon of the item currently held is left out of the grid [exe DC 0x4097ce–0x4097d7: icon
  slot + 64 compared with the cursor slot 0x48bad4].
- The held item is drawn as the cursor instead: its icon, 64×64, centred on the mouse
  [exe DC 0x40b474–0x40b524; half-size 32 by default at 0x40b602]. Cursor states are in the
  [input spec](input.md).

**Click rectangles** [exe DC 0x417db7–0x417e4e] [exe 2006 0x41519c–0x41523e]:

| | DC | 2006 |
|---|---|---|
| Cell (c 0-based, r 0-based) | [32 + c(32+S), 32 + c(32+S) + S] × [64 + 96r, 64 + 96r + S] | [32 + c(32+S), 32 + c(32+S) + S] × [96 + 96r, 96 + 96r + S] |
| Rows tested | r = 0–10 | r = 0–8 |
| Columns tested | c = 0–9 | c = 0–7 |
| Value returned | 10r + c + 1 | 8r + c + 1 |
| Values acted on | 1–59. Value 60 and up is ignored [exe DC 0x414049] | 1–51 [exe 2006 0x4119d8] |

- The hit rectangles do not follow H. At the shipped sizes they sit 2 px (DC) or 8 px (2006)
  above the drawn icons.
- In the DC, the place showing inventory position 60 (row 5, last column) cannot be clicked,
  nor can row 6.
- Clicking the item named `"journal"` with nothing held opens the journal
  ([api.md](api.md#scjournalset)) [exe DC 0x4140cf–0x4140df].

---

## Journal (DC only)

[exe DC 0x40b0a0, input 0x413f80, open 0x418580, close 0x4185f0]

**Opening.**
- Opening clears the feedback text and loads the journal font into slot 24 and the journal image
  into slot 29 [exe DC 0x418580–0x4185a4].
- Slot 29 also holds the menu background. Opening the menu loads its own image back into
  slot 29 [exe DC 0x412ccf–0x412cda].
- The screen crossfades from the scene [exe DC 0x4185a9–0x4185b8].
- The shipped image `journal.jpg` is 800×472 [data dc/scratches.res: journal.jpg].

**Page.**
- The page image is drawn as the menu background is (see [Menu background](#background)): at
  1024×768 it fills [0, 1025] × [64, 704] [exe DC 0x416d4a → 0x4083c0].
- A JPEG has no transparency, so the cloud layers drawn between its two passes are hidden.

**Text** [exe DC 0x40b0a0–0x40b11b]:
- Black (0, 0, 0, 1), in the journal font.
- Two columns, each of 38 lines: 76 lines per page, the same 76 that a page turn moves
  ([api.md](api.md#scjournaladd)).
- The left column starts at x = 32 and the right column at x = 512. **Fixed.**
- Line j of a column has its bottom edge at y = 100 + 16j, for j = 0–37 (100 … 692). **Fixed:**
  measured from the top of the window.
- The page shows journal lines *start* to *start* + 75, and stops at the last line.

**Click areas** (mouse y measured from the top; **fixed**) [exe DC 0x413f80–0x41401d]:

| Area | Action |
|---|---|
| y ≤ 604, x < 512 | previous page |
| y ≤ 604, x > 512 | next page |
| y ≤ 604, x = 512 | nothing |
| y > 604 | close the journal. With alternate control on, the mouse is then put at (cx, cy) |

- The two areas also select different cursors [exe DC 0x413f8b, 0x413fe5] (see the [input spec](input.md)).
- A page turn plays the turn sound only if the page changed [exe DC 0x40e490].
  - Going back from the first page stays on it.
  - Going forward past the end clamps to the last page, with no sound.
- Esc also closes the journal ([api.md](api.md#scjournalset)).
- Closing crossfades back, then loads the game font into slot 24 again [exe DC 0x4185f0–0x418626].

---

## Main menu

Drawn by [exe DC 0x408550] [exe 2006 0x4080d0]. Clicks are hit-tested by [exe DC 0x417e4f]
[exe 2006 0x41523f] and handled by [exe DC 0x413640] [exe 2006 0x411260].

### Opening and closing

**Opening** [exe DC 0x412cb0]:
1. The menu background image (`SC_IMAGE`) is loaded into slot 29 [exe DC 0x412ccf–0x412cda].
2. The engine checks which save files exist (slots 1–11). Each one found makes its row clickable
   and reads its description [exe DC 0x412d20–0x412e20].
3. The Load button is greyed unless some save exists. The Save button is greyed unless a game is
   running [exe DC 0x412cf5–0x412d0f, 0x412dcd–0x412dd9, 0x412e26–0x412e45, 0x412f8a–0x412f97].
4. The screen crossfades into the built-in `black` zoom, waits 1 second, then shows the menu
   [exe DC 0x412e7f–0x412ec2]. See also [api.md](api.md#scabort).

**Closing** [exe DC 0x4130d0]: a crossfade to the `black` zoom, a 2-second wait, then (with a
game running) a crossfade back to the previous state [exe DC 0x4130fb–0x41318f].

### Background

The `SC_IMAGE` image is drawn only if `scMenuSetEx(SC_IMAGE, …)` was called [exe DC 0x413521,
0x416f54]. Without it, the menu is on black.

The DC draws it in three passes [exe DC 0x4083c0]:
1. The image, added onto the black window (blend one + one) [exe DC 0x4083c7–0x40844d].
2. Two cloud textures (`clouds1.tga`, `clouds2.tga`) drawn on the sky mesh, scrolling and
   alternately fading [exe DC 0x408466 → 0x40d990].
3. The image again, with normal alpha. Its transparent parts let the clouds show through
   [exe DC 0x40846b–0x4084fd].

Placement and image:
- **DC, fixed:** the image is scaled by 1.28 horizontally and 1.356 vertically, so the shipped
  800×472 `back-menu.tga` fills [0, 1025] × [64, 704] at 1024×768 [exe DC 0x4083e7–0x408448:
  quad from (0, 704) to (1312, −685) above the bottom edge, over the whole 1024² texture].
- **2006:** drawn 1:1, centred: [0, 800] × [64, 536] at 800×600 [exe 2006 0x407ed0].
- `back-menu.tga` is a 32-bit RLE TGA, 800×472, the same in both builds [data dc/scratches.res,
  2006/scratches.res: back-menu.tga].
- [unverified: the camera used for the cloud layers in menu state; the sky code is described
  elsewhere]

### Logo

Drawn in every menu mode. Colour (1, 1, 1, 1) [exe DC 0x4085c0–0x4086ad] [exe 2006
0x408140–0x40822c].
- The image (w × h) is centred on x = cx.
- Its top is at cy − 164 − h/2, so its centre is 164 pixels above the screen centre.

| | DC | 2006 |
|---|---|---|
| Image | `menu-logo.tga` 512×256 [data dc/scratches.res] | 480×120 [data 2006/scratches.res] |
| Rectangle | [cx−256, cx+256] × [cy−292, cy−36]; at 1024×768: [256, 768] × [92, 348] | [cx−240, cx+240] × [cy−224, cy−104]; at 800×600: [160, 640] × [76, 196] |

### Buttons (main mode)

The buttons are the `scMenuSet` images back, new, setup, load, save, quit, all 128×64 [data:
menu-*.tga].
- Button k (k = 0 back, 1 new, 2 setup, 3 load, 4 save, 5 quit) is drawn scaled to 112×60
  [exe DC 0x408707–0x40882d] [exe 2006 0x408289–0x4083b1].
  - x range: [cx − 56, cx + 56].
  - Top edge: cy + 72 + 36k (DC) or cy − 4 + 36k (2006).
  - The buttons overlap, because they are 60 tall at a 36-pixel pitch.
- The back button (k = 0) is shown only when a game is running. With no game the menu shows
  k = 1–5 [exe DC 0x416f62–0x416f79, 0x4086fe–0x408908].
- Normal buttons are drawn in (1, 1, 1, 1). Greyed buttons (Load, Save) are drawn in
  (0.5, 0.5, 0.5, 1) [exe DC 0x408736–0x40876c].
- **Hover:** the hovered button, unless greyed, is drawn again with additive blending in
  (0, 1, 1, 0.5) [exe DC 0x408832–0x4088ef].

| k | DC rectangle at 1024×768 | 2006 rectangle at 800×600 |
|---|---|---|
| 0 back | [456, 568] × [456, 516] | [344, 456] × [296, 356] |
| 1 new | [456, 568] × [492, 552] | [344, 456] × [332, 392] |
| 2 setup | [456, 568] × [528, 588] | [344, 456] × [368, 428] |
| 3 load | [456, 568] × [564, 624] | [344, 456] × [404, 464] |
| 4 save | [456, 568] × [600, 660] | [344, 456] × [440, 500] |
| 5 quit | [456, 568] × [636, 696] | [344, 456] × [476, 536] |

**Click rectangles.** Button k: [cx − 64, cx + 64] × [H − 288 + 36k, H − 252 + 36k], for
k = 0–5. These follow H, while the drawing follows H/2 [exe DC 0x417e73–0x417ecc]
[exe 2006 0x415263–0x4152c2].
- DC at 1024×768: [448, 576] × [480 + 36k, 516 + 36k].
- 2006 at 800×600: [336, 464] × [312 + 36k, 348 + 36k].
- The k = 0 rectangle works even when the back button is not drawn. With no game running, a click
  there only starts a crossfade [exe DC 0x4136c0–0x413728].

**Actions** [exe DC 0x413640, table 0x4135c8]:

| k | Action |
|---|---|
| 0 | with a game running, close the menu (resume) |
| 1 | new game (prompts above) |
| 2 | setup mode, with a crossfade |
| 3 | load mode, only if some save exists |
| 4 | save mode, only if a game is running |
| 5 | "Quit the game?" |

### Load and save lists

**Title** [exe DC 0x40890d–0x4089ef] [exe 2006 0x408496–0x40857c]:
- The Load image in load mode, the Save image in save mode.
- Drawn scaled to 96×48 at [cx − 48, cx + 48]. Its top is at cy + 28 (DC) or cy − 48 (2006).
- DC at 1024×768: [464, 560] × [412, 460]. 2006 at 800×600: [352, 448] × [252, 300].

**Rows.**
- Row i is at x = cx − 120, bottom edge H − 276 + 16i (cell [H − 292 + 16i, H − 276 + 16i]),
  in white (1, 1, 1, 1) [exe DC 0x408a02–0x408ac9] [exe 2006 0x40858f–0x40865a].
- The text is the slot number, `". "`, then the description [exe DC 0x40e540; ". " at 0x4834c4].
  - A slot with no save shows just `"n. "` [exe DC 0x4133a0–0x41343d].
  - Row 0 is the quicksave (`scream.011`), whose description is `quicksave`
    ([formats.md](formats.md#appendix-original-save-files-scream0nn)) [exe DC 0x412dfb–0x412e08].
- Load mode shows rows 1–10, plus row 0 when the quicksave exists. Save mode shows rows 1–10
  [exe DC 0x416fc8–0x416fdf, 0x4089f4–0x4089fd, 0x408aeb–0x408af1].
- **Hover:** the hovered row is drawn again with additive blending in (0, 1, 1, 0.5) [exe DC
  0x408a6f–0x408ac0].
- DC at 1024×768: row 1 [392, …] × [492, 508] … row 10 × [636, 652].
- 2006 at 800×600: row 1 [280, …] × [324, 340] … row 10 × [468, 484].

**Click rectangles.** Row i: [cx − 256, cx + 256] × [H − 286 + 16i, H − 270 + 16i], for
i = 0–10. Each is 6 pixels below its drawn row [exe DC 0x417ede–0x417fac] [exe 2006
0x4152d4–0x4153ae].
- In load mode, a row answers only if its save exists. Row 0 answers if the quicksave exists
  [exe DC 0x417f2d–0x417f48, 0x417fa2–0x417faa].
- In save mode every row answers, but a click on row 0 is ignored [exe DC 0x413a9e].

**Actions** [exe DC 0x4139c0–0x413bf5]:
- **Load:** with no game running, loads at once. Otherwise it asks "Load a saved game?". After
  loading, the menu closes and the feedback "Game loaded" appears.
- **Save:** if the slot's file exists, asks "Replace saved game?". Then the menu closes, the save
  is written, the row text is updated and the feedback "Game saved" appears.

### Back button (lists and setup)

The back image is drawn 1:1 (128×64) at [cx − 64, cx + 64] [exe DC 0x408af8–0x408bb3,
0x4092b4–0x409357] [exe 2006 0x40868b–0x408748, 0x408bda–0x408c7d].

| | Top edge | DC at 1024×768 | 2006 at 800×600 |
|---|---|---|---|
| Load/save lists | DC: cy + 262. 2006: cy + 176 | [448, 576] × [646, 710] | [336, 464] × [476, 540] |
| Setup | DC: cy + 252. 2006: cy + 176 | [448, 576] × [636, 700] | [336, 464] × [476, 540] |

- **Hover:** the same cyan additive pass as the main buttons [exe DC 0x408bb8–0x408c49].
- **Click rectangle:** [cx − 64, cx + 64] × [H − 105, H − 70]: [448, 576] × [663, 698] at
  1024×768, [336, 464] × [495, 530] at 800×600 [exe DC 0x417f59–0x417f97] [exe 2006
  0x415359–0x415397].
- The action returns to the main mode, with a crossfade [exe DC 0x4136dc, table 0x41360c].

### Setup

**Title:** the Setup image, placed like the list title [exe DC 0x408c76–0x408d39] [exe 2006
0x40880b–0x4088d2].

**Rows.**
- Row s is at x = cx − 145, bottom edge H − 276 + 16s, in white [exe DC 0x408da4–0x408e67]
  [exe 2006 0x408939–0x408a05].
- Each row is a label padded to 21 characters, then an 8-character right-aligned value. That is
  29 characters, 290 pixels, centred [exe DC strings 0x483243–0x48332f].
- **Hover:** as for slot rows.

| s | Label | Values | Setting | DC | 2006 |
|---|---|---|---|---|---|
| 0 | `hints` | `ON` / `OFF` | hints | ✓ | ✓ |
| 1 | `surround music` | `ON` / `OFF` | `SURROUND` | ✓ | ✓ |
| 2 | `camera speed` | `SLOW` (25) / `NORMAL` (50) / `FAST` (75); other values show no value | camera speed | ✓ | ✓ |
| 3 | `text speed` | as camera speed | text speed | ✓ | ✓ |
| 4 | `gamma correction` | `DISABLED` / `LOW` / `MEDIUM` / `HIGH` (0–3) | gamma level | ✓ | ✓ |
| 5 | `slideshow mode` | `ON` / `OFF` | slideshow mode | ✓ | ✓ |
| 6 | `alternate control` | `ENABLED` / `DISABLED` | alternate control | ✓ | — |

Sources: [exe DC 0x408d4d–0x4092af, table 0x408520, gamma table 0x40853c] [exe 2006
0x4088e2–0x408bd5, table 0x408098; loop ends at s = 5, 0x4089f3].

- The text-speed value is shown only for 25, 50 and 75. During a phone call the script sets
  15, and the row then shows just its label [exe DC 0x408ff0–0x4090b5] [script DC: callBlake].
- **Click rectangles** are the slot-row rectangles (i = s) [exe DC 0x417ede]. A click acts like
  a key: s = 0 → F1, 1 → F2, 2 → F9, 3 → F10, 4 → F12. Rows 5 and 6 toggle their setting
  directly [exe DC 0x413c02–0x413d5d, table 0x413620] [exe 2006 0x411707–0x41177c, table
  0x411240].
- A change shows a feedback message (see [Engine messages](#engine-messages)).
- **DC, hidden row 7.** The rectangle of row 7 ([H − 174, H − 158]; [582, 598] at 768) is
  clickable but nothing is drawn there [exe DC 0x413c0b, table entry 0x413d5e].
  - A click shows the feedback "english" when `LANGUAGE` is `en`.
  - It then calls the Lua global `language` [exe DC 0x413d5e–0x413da5; strings 0x483b25,
    0x483b2d].
  - The shipped script defines no `language` [script DC: no match].
  - [unverified: what calling the undefined global does here]
- Rows 8–10 (DC) and 6–10 (2006) have rectangles but no action [exe DC 0x413c0e].

---

## Pictures: zoom, overlay, video, splash

Zoom images, overlays, video frames and the splash screen are drawn as flat quads over the 3D
band, at fixed window positions with the top edge 704 pixels above the bottom of the window
(y 64 at 768) [exe DC 0x409960, table 0x409948]. Which quad each picture uses, and where an
image of each shipped size lands, is in [rendering.md](rendering.md#zoom-quad-placement)
(DC) and [rendering.md](rendering.md#zoom-frame-2006) (2006, centred at native size). In
short:

- an 800×472 zoom image is stretched to about 1025×640, a 1024×640 one is drawn 1:1;
- overlays are drawn with normal alpha over the previous state's screen, and pick their quad
  by the width of the **zoom** texture, not their own [exe DC 0x40aae0–0x40ab62, 0x409971];
- videos are added onto black; the quad depends on the frame width (see
  [rendering.md](rendering.md#video-frames-sccutscene));
- the splash screen uses the 800×472 quad [exe DC 0x407a20–0x407abc, 0x410641].

[unverified: whether any overlay image differs in width from the zoom shown before it; the
game does not use `scOverlay`]

---

## Engine messages

The engine shows these through the feedback text, in the [feedback position](#feedback-text).
Its message boxes are covered in api.md: "Please insert the game CD" under
[scInit](api.md#scinit) and "Too many reactions!" under
[scInventoryReaction](api.md#scinventoryreaction).

| Message | When | Source |
|---|---|---|
| `Game loaded` | after loading from the menu or by key | [exe DC 0x413a3f, 0x4176ff] |
| `Game saved` | after saving from the menu or the quicksave key | [exe DC 0x413beb, 0x417ac4] |
| `hints ON` / `hints OFF` | F1 or setup row 0 | [exe DC 0x417a77, 0x4177f2] |
| `surround music ON` / `OFF` | F2 or setup row 1 | [exe DC 0x41781e, 0x417a9b] |
| `camera speed SLOW` / `NORMAL` / `FAST` | F9 or setup row 2: under 50 → 50, 50 → 75, 75 → 25, anything else → 50 | [exe DC 0x41771d–0x41773d, 0x417aea–0x417b21] |
| `text speed SLOW` / `NORMAL` / `FAST` | F10 or setup row 3, same cycle | [exe DC 0x41774d–0x41776d, 0x417b2f–0x417b66] |
| `gamma correction LOW` / `NORMAL` / `HIGH` / `DISABLED` | F12 or setup row 4: 0 → 1 → 2 → 3 → 0. The menu calls level 2 `MEDIUM`, the message calls it `NORMAL` | [exe DC 0x417784–0x417bba, table 0x4175a0] |
| `slideshow mode ON` / `OFF` | setup row 5 | [exe DC 0x413c8e–0x413cf5] |
| `alternate control ENABLED` / `DISABLED` | setup row 6 (DC) | [exe DC 0x413cf6–0x413d5d] |
| `english` | DC hidden setup row 7 | [exe DC 0x413db7] |
| hint texts | `scHints` | [api.md](api.md#schints) |

The 2006 exe has the same messages, with three differences [data 2006-patch scream.exe:
string search]:
- it has no alternate-control messages and no `english`;
- its F10 "fast" message is misspelt `test speed FAST`.

---

## Differences in the 2006 build

| Item | DC | 2006 |
|---|---|---|
| Mode | 1024×768 | 800×600 [data 2006/scream.cfg; defaults exe 2006 0x40d3e6–0x40d40f] |
| Pictures | fixed quads scaled to 1024×640 | 1:1, centred on the screen |
| Inventory grid | 10 × 7 drawn; clicks 1–59 | 8 × 8 drawn (5 rows on screen at 600); clicks 1–51 |
| Inventory rows (from the bottom) | 638 − 96r | 448 − 96r |
| Logo | 512×256 | 480×120 |
| Main buttons | top cy + 72 + 36k | top cy − 4 + 36k |
| List back button | cy + 262 | cy + 176 |
| Setup back button | cy + 252 | cy + 176 |
| Setup rows | 7 (plus a hidden 8th) | 6, no alternate control |
| New game | always asks which game (DC); "Start a new game?" first if a game is running | starts at once with no game running; asks "Start a new game?" otherwise |
| Journal | yes | no |
| F10 "fast" message | `text speed FAST` | `test speed FAST` [data 2006-patch scream.exe: string search] |

The 2006 code for the feedback text, choice rows, slot and setup rows, menu hit rectangles,
colours, fades, crossfade and gamma was compared with the DC code and matches it, apart from
the rows above [exe 2006 0x40a430, 0x40a860, 0x4080d0, 0x4150c0, 0x407700, 0x407db0].
[unverified: the 2006 menu-open routine (greying, save scan); it was not compared]. The 2006 menu record keeps its mode at +0x230
and its game-running flag at +0x218, against the DC's +0x260 and +0x248 [exe 2006 0x41524d,
0x41135b].

---

## Open questions

- [unverified: what the text routine does with a text over 255 characters]
- [unverified: the camera used for the menu's cloud layers]
- [unverified: whether any overlay is drawn with the wrong quad because of the zoom-width test]
- [unverified: what calling the undefined `language` global from setup row 7 does]
- [unverified: the 2006 menu-open routine (greying, save scan); it was not compared]

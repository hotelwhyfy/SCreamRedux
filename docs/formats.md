# File formats

This page describes every file the original SCream engine (`scream.exe`) reads or writes. It
covers what the executables, the shipped scripts and the shipped data define, and nothing
about any re-implementation. Where the 2006 engine differs from the Director's Cut (DC), the
difference is noted. The function reference is in [`api.md`](api.md).

> **AI-assisted.** Claude (Anthropic's AI model) wrote this page. The sources were Ghidra
> decompilation and disassembly of the two executables below, the shipped scripts decompiled
> with unluac, and measurements of the shipped files. A person has not yet checked it. A
> source tag says where a claim can be checked; it is not a guarantee. Check a claim against
> its source before you build on it, and treat `[unverified]` claims as open questions.

## Sources

| Tag | Means | File (MD5) |
|---|---|---|
| `[exe DC 0x…]` | code or data at that address in the DC update's `scream.exe` (May 2007, 649728 bytes) | `f696d90b1f1fe5eb050beda06781dcc8` |
| `[exe 2006 0x…]` | the same in the patched 2006 `scream.exe` (April 2006, 543744 bytes) | `5de7958817eecb3815d816d123ab9217` |
| `[script DC: name]`, `[script 2006: name]` | the decompiled shipped scripts (see [api.md](api.md#sources)) | |
| `[data FILE …]` | a measurement of a shipped file, at an offset or over all entries. For `data scream.exe`, a value below 0x400000 is a file offset and a value from 0x400000 up is a virtual address | see below |
| `[unverified: …]` | not confirmed in any of the above | |

Shipped files measured:

| File | MD5 |
|---|---|
| DC `scratches.res` (single-disc release) | `74e6a98a3fede88c26c36d4d2ca77d7f` |
| DC `update.res` (disc 2 update) | `decd946b1410c02b3c011832f489972a` |
| 2006 `scratches.res` | `63bfa7f95b20644ae83787d42dcc7536` |
| `scream.res` (DC) | `0f7030261ca035786985ed87cb12d05a` |
| DC `scratches.sc` (June 2007) | `d697dcb6491e30ce54c38e2f42fdd2c8` |
| 2006 `scratches.sc` | `6b15f443a27f4ea6ea70bf67455d4480` |

Integers in the binary formats are little-endian 32-bit, written by 4-byte `fwrite` calls
[exe DC 0x411370]. String fields have a fixed size and are filled with `strcpy` into a buffer,
so the bytes after the NUL are whatever the buffer held [exe DC 0x411450–0x411489,
0x4115a8–0x4115d1].

## Contents of an install

| File | Format | Role |
|---|---|---|
| `scream.cfg` | [text](#screamcfg) | engine settings, read at start-up [exe DC 0x4061dc] |
| `scream.sc` | [Lua bytecode](#scripts) | engine constants, run first [exe DC 0x406f34] |
| `scratches.sc` | [Lua bytecode](#scripts) | the game script, named by `SCRIPT` [exe DC 0x4061fa] |
| `scream.res` | [archive](#res-archives) | the engine's default textures [exe DC 0x410364–0x41040e] |
| `scratches.res` | [archive](#res-archives) | game resources, named by `SC_RES_FILE` [script DC: top level, `scSet(SC_RES_FILE, "scratches.res")`] |
| `update.res` | [archive](#res-archives) | DC update: replacement and new files [data update.res: 1623 entries]; not opened by the exe (see below) |
| `sdc-intro.srt` | [SubRip-like](#srt-subtitles) | DC only: the intro's subtitles [script DC: `scLoadSubtitle("sdc-intro.srt")`] |
| `scream.000`–`scream.011` | [binary](#appendix-original-save-files-scream0nn) | saved games [exe DC 0x411370] |
| `scream.log` | text | log, opened when `LOG` is non-zero [exe DC 0x404a45, 0x404b19] |

**Loose files and archives.**

- **Loose files.** `scream.cfg`, the scripts and the `.srt` file are opened directly with
  `fopen` [exe DC 0x40f4b0, 0x40624b, 0x418097].
- **Archives.** Images, sounds and videos are looked up by name in the one archive that is
  currently open [exe DC 0x40eb10]. Only one archive is open at a time:
  1. `scInit` opens `scream.res` and loads the default textures from it
     [exe DC 0x410364–0x41040e].
  2. It closes that archive [exe DC 0x410413 → 0x40eac0].
  3. It opens the game archive [exe DC 0x410445–0x410459].

  All later lookups use the game archive only.
- **`update.res`.** The only archive names in the exe are `scream.res` and the default
  `game.res`; the scripts name the game archive `scratches.res` [exe DC: string search]
  [script DC: top level]. So the exe never opens `update.res`, and the update's files must be
  merged into `scratches.res` before the exe can use them. [unverified: which installer tool
  did the merge]

---

## `.res` archives

A flat, uncompressed file store.

| Offset | Size | Field |
|---|---|---|
| 0x000 | 256 bytes | the text `SCream resource file\0`, then non-zero, varying bytes up to 0x0FF [data dc/scratches.res @0x000; dc/update.res @0x000] |
| 0x100 | u32 | entry count N [data dc/scratches.res @0x100: 3350] |
| 0x104 | N × 92 bytes | entry table |

Each entry is 92 bytes [exe DC 0x40ea76 `malloc(0x5c)`, 0x40ea8f]:

| Offset | Size | Field |
|---|---|---|
| +0 | char[80] | name, NUL-terminated. In every shipped entry, the bytes after the NUL are non-zero garbage [data: all entries of the four archives] |
| +80 (0x50) | u32 | type: 1 = `.jpg`, 2 = `.tga`, 3 = `.ogg`. Every shipped type matches its file's extension [data: all entries] |
| +84 (0x54) | u32 | offset of the file's data, from the start of the archive [exe DC 0x40eb7a, 0x40edb1] |
| +88 (0x58) | u32 | size in bytes [exe DC 0x40edc1] |

### Reading

The archive is read as follows [exe DC 0x40e9a0]:

- It builds the path as the path string followed directly by the file name, with no
  separator added [exe DC 0x40e9e4–0x40ea27].
- It opens the file with mode `"rb"` [exe DC 0x40ea28].
- It reads 0x104 bytes (header and count) into one buffer [exe DC 0x40ea42–0x40ea4e].
- It then reads N entries of 92 bytes each [exe DC 0x40ea76–0x40eaae].

It never looks at the header text, and the string `SCream resource file` does not occur in the
exe [exe DC: string search]. The table must start at 0x104 and be contiguous.

### Lookup

Looking up a file [exe DC 0x40eb10]:

- A name is compared with each entry's name in table order, case-sensitively. The first match
  wins [exe DC 0x40eb3e–0x40eb4f; 0x41a780 is a byte-exact compare].
- On a match, it writes the entry's type to the caller, seeks to the offset and returns the
  open file [exe DC 0x40eb5c–0x40eb82].
- A name not in the archive returns NULL. There is no fallback to loose files
  [exe DC 0x40eb9f–0x40ec39].
- When archives are switched off (the archive flag, settings block offset 0x038, is 0), assets are opened as loose files
  from the path instead. The type then comes from the extension: `.jpg` 1, `.tga` 2, `.ogg` 3,
  `.srt` 4 [exe DC 0x40ec3a–0x40ed5b].
- An entry's size is the stored size. For a loose file, it is the file length
  [exe DC 0x40ed60–0x40edfa].

### Shipped archives

| Archive | Entries | Types (1 / 2 / 3) | Longest name | Notes |
|---|---|---|---|---|
| DC `scratches.res` (single-disc) | 3350 | 1930 / 493 / 927 | 32 | data starts right after the table, at 308460 [data] |
| DC `update.res` | 1623 | 1493 / 67 / 63 | 28 | 1574 names are also in DC `scratches.res`; 49 are not [data] |
| 2006 `scratches.res` | 2829 | 1575 / 444 / 810 | 32 | table area reserved for 4096 entries, zero-filled from 260528 to 377092 [data] |
| `scream.res` (DC and 2006) | 6 | 0 / 6 / 0 | 14 | `cursor_def.tga`, `font_def.tga`, `rain_def.tga`, `sc_logo.tga`, `sky_def.tga`, `wait_def.tga` [data] |

- The 2006 archive has one file that the DC single-disc archive lacks: `vid-logo_us.ogg`.
  `update.res` has it among its 49 new names [data].
- `sky_def.tga` is in `scream.res`, but the DC exe never names it [exe DC: string search].
- [unverified: the two-disc DC `scratches.res`; no copy was measured]

---

## Assets

### Images

Images are JPEG or TGA. Every shipped JPEG is baseline (SOF0) with 3 components [data: every
JPEG entry]. Measured from the shipped archives:

| Use | Name pattern (convention) | Measured format |
|---|---|---|
| Node faces | `…_n<N><f>.jpg`, `f` ∈ `f b l r u d` | square JPEG. DC `scratches.res`: 881 at 1024² and 324 (the `_dc` files) at 1280². `update.res`: 876 at 1280² and 2 at 1024². 2006: 881 at 1024² [data] |
| Face variants | e.g. `hs-lobby_n1l1` … `_n1l12`, `chpl-int_n3fs` | JPEG, the same sizes as faces [data]. Scripts swap them in with `scTextureUpdate` [script DC: e.g. `scTextureUpdate(SC_FRONT, "hs-hallway1_dc_n2f.jpg", 1)`] |
| Zooms and stills | free | JPEG, 800×472 (460 in DC base, 459 in 2006) or 1024×640 (9 in DC base, 343 in `update.res`) [data] |
| Masks | `<image>_msk.tga` | TGA, mostly RLE 8-bit grey (type 11). 37 in DC base are RLE 24-bit colour (type 10), and one is 32-bit (`hs-attic_n1b_msk.tga`). Sizes 512², 400×236, and one 512×456 [data] |
| Cursors | `cur-*.tga` | RLE 32-bit TGA, 64² (6 files) or 32² (7 files) [data] |
| Inventory icons | `inv-*.tga` | 32-bit TGA, 64². Two are uncompressed (`inv-gold_key`, `inv-mag_glass`) [data] |
| Fonts | `font-*.tga` | RLE 32-bit TGA, 512², top-left origin [data] |
| Menu | `menu-*.tga` | RLE 32-bit TGA. Buttons are 128×64. `menu-logo.tga` is 512×256 in DC and 480×120 in 2006 [data] |

**Masks.** For every image it is given, the engine also stores a mask name: the image name cut
at the first `.` (by `strtok`), plus `_msk.tga`. For a zoom it is stored at record +0x64
[exe DC 0x414a28–0x414a99]. A node face's mask name is derived the same way
[exe DC 0x414eb0, 0x414fc6–0x4154da]. A missing mask, or a missing face image, is replaced by a white
texture, so the face draws fully opaque [exe DC 0x4078e2, 0x40c20f]. How masks combine with
the sky, fog and rain is in the [rendering spec](rendering.md).

**Fixed names.** The exe loads these files by name:

- From `scream.res`, in `scInit` [exe DC 0x410364–0x41040e]:
  - `sc_logo.tga` (the logo)
  - `font_def.tga` (the default font)
  - `cursor_def.tga` (the default cursors)
  - `wait_def.tga` (the wait cursor)
  - `rain_def.tga` (rain)
- From the game archive: `clouds1.tga`, `clouds2.tga` and `gradient.tga`
  [exe DC 0x4076d7–0x4076f5]. These three are only in DC `scratches.res` [data], and their
  names do not occur in the 2006 exe [exe 2006: string search].
- `sc_logo.tga` is also the default `SPLASH` [exe DC 0x40f398].
- `scInit` creates a zoom named `black` with the image `black.jpg`, before any script zoom
  [exe DC 0x410418–0x410431]. `black.jpg` is in none of the archives [data]. The texture
  loader recognises the name and uploads a zero-filled image, so it renders black
  [exe DC 0x407944–0x4079b1].

### Fonts

The exe builds 272 (0x110) display lists, one per glyph [exe DC 0x40bf20]:

- Each list draws a 16×16-unit quad.
- For glyph i, the quad's texture rectangle is column `i % 16`, row `i / 16` of a 16×16 grid.
  Each cell is 1/16 of the texture in each direction, whatever the texture size [exe DC
  0x40bf20; constants at 0x483450 = 0.0625, 0x483454 = −0.0625].
- Each list then moves 10.0 units to the right, so the advance is fixed and all text is
  monospace [exe DC 0x40bf20, `glTranslated(10.0, …)`].

Text is drawn with `glListBase(base + 16)` and `glCallLists(…, GL_UNSIGNED_BYTE, …)`, so a
byte value selects its glyph directly [exe DC 0x40c10d, 0x40c040]. There is no width table
[exe DC 0x40bf20].

In the shipped 512² fonts, each cell is 32 px. Counting rows from the top, the cells for bytes
0x09, 0x0A, 0x0D, 0x20 and 0xA0 are empty, and the others hold glyphs. That matches "cell =
byte value" [data font_def.tga, font-menu.tga, font-michael.tga: alpha per cell].
[unverified: which code page the glyphs follow, e.g. Windows-1252]

### Audio

Audio files are Ogg Vorbis, each with a single logical stream [data: first-page scan of every
Ogg entry].

- **Sample rates:** 44100 Hz, except 12 `fx-` files and `update.res`'s `trck-alt.ogg`, which
  are 48000 Hz [data].
- **Channels:** mono or stereo [data].
- **Name prefixes:** `fx-`, `fx_stairway_`, `dlg-`, `dlg400…`, `msg400…`, `trck-`, and
  `silence.ogg` [data].
- **Surround music** is three files: `-F` (stereo), `-RL` and `-RR` (mono) [data, e.g.
  `trck-basement-F/RL/RR.ogg`]. `scTrackSet3D` names all three [script DC: 28 calls]. The rear
  files are loaded and played only while the surround setting is on [exe DC 0x41628a,
  0x416297].

### Video

Videos (`vid-*`, and in DC also `intro-*` and `outro-*`) are Ogg Theora, 4:2:0, 24 fps [data].
Measured sizes:

- 640×352;
- an 800×472 picture in an 800×480 frame;
- 1024×640 (DC data only).

[data]

Each video file has exactly one logical stream, so there is **no audio** [data]. Scripts play
the sound with `scFXPlay` next to `scCutscene` [script DC: e.g.
`scFXPlay("fx-door_house_open.ogg")` before `scCutscene("vid-door_lobby_dc.ogg")`].

---

## Scripts

Start-up runs the scripts in this order:

1. It reads the settings [exe DC 0x4061d7–0x4061e1].
2. It looks for the game script: `<SCRIPT>.sc`, then `<SCRIPT>.lua`. If neither opens, it
   logs `Script file not found` [exe DC 0x4061fa–0x406252, 0x406f83–0x406fe8].
3. It loads and runs `scream.sc` [exe DC 0x406f34–0x406f4e].
4. It loads and runs the game script [exe DC 0x406f53–0x406f6a].

Files are opened with `fopen`. The shipped cfg says `SCRATCHES` and the file is
`scratches.sc` [data scream.cfg; file names], so the original relies on Windows' case-blind
file system [exe DC 0x40624b]. The game script builds the world, calls `scInit()`, and ends
with `scRun()` and then `scTerminate()` [script DC: top level].

**Bytecode headers.**

| Engine | Header bytes | Meaning |
|---|---|---|
| DC | `1B 4C 75 61 51 00 01 04 04 04 08 00` | Lua 5.1, format 0, little-endian, int 4, size_t 4, instruction 4, number 8, floating point [data dc/scream.sc @0x00, dc/scratches.sc @0x00] |
| 2006 | `1B 4C 75 61 50 01 04 04 04 06 08 09 09 08` + test number | Lua 5.0, little-endian, int 4, size_t 4, instruction 4, op/A/B/C bits 6/8/9/9, number 8 [data 2006/scream.sc @0x00] |

The embedded Lua is 5.1.1 in DC and 5.0.2 in 2006 [exe DC, exe 2006: version strings].

**Chunk names.** The chunks keep their source names [data @0x10]:

| Chunk | Source name |
|---|---|
| DC `scream.sc` | `@scream.lua` |
| DC `scratches.sc` | `@scratches.lua` |
| 2006 `scratches.sc` | `@scratches-en.lua` |

All four scripts decompile without errors [script DC, 2006: unluac]. [unverified: whether
plain-text Lua loads under the `.sc` name; that depends on 0x43f150 being stock
`luaL_loadfile`]

**`scream.sc`** only assigns `SC_*` and `SC_DEF_*` globals: every line of the decompiled file
is one assignment [script DC: scream.sc, 147 lines]. The 2006 `scream.sc` lacks the following,
which match the settings that 2006 lacks (see the save format) [script 2006: scream.sc]:

- `SC_LANGUAGE`, `SC_IMG_SIZE`, `SC_DEBUG`, `SC_FPS_STYLE`;
- `SC_SKY_COLOR`, `SC_CLOUD_COLOR`, `SC_SCREEN_COLOR`;
- `SC_DAWN`, `SC_MORNING`, `SC_DUSK`, `SC_STORM`;
- `SC_STBOOK`, `SC_FEEDBACK_EX`.

---

## `scream.cfg`

The parser works as follows [exe DC 0x40f4a0]:

- It opens `scream.cfg` with mode `"r"` [exe DC 0x40f4ab].
- It loops on `fscanf(f, "%s", word)` until end of file, so it reads whitespace-separated
  words and ignores line structure [exe DC 0x40f4bf–0x40f4d5].
- Each word is compared case-sensitively with every key in turn [exe DC 0x40f4db onward;
  compare 0x41a780]. On a match, it reads the value with `%s` or `%d` [exe DC 0x40f4f6,
  0x40f5ee].

Consequences of this design:

- Words that aren't keys are skipped, which covers `[SCREAM]` and the `;` comment text
  [exe DC 0x40f4a0; data scream.cfg].
- A `%d` value is stored from a stack variable even if `fscanf` fails. A non-number therefore
  stores the previous number read (or garbage the first time), and the word is read again as
  a key [exe DC 0x40f5e0–0x40f5fd].
- A value like `1.5` gives 1, and `.5` is read as the next word [exe DC 0x40f5ee].
- No value is range-checked, and a key that appears twice keeps its last value
  [exe DC 0x40f4a0].
- `PATH` gets `\` appended to its value [exe DC 0x40f567–0x40f5b5; string 0x4835c2 = `\`].

**Keys.** Every key has a default, set before the file is parsed [exe DC 0x40f260, 0x4061d7].
The offset is the key's position in the settings block that saved games store (see the
[appendix](#3-settings-block)).

| Key | Offset | Type | Default | Notes |
|---|---|---|---|---|
| `SCRIPT` | 0x228 | string | `main` | [exe DC 0x40f4df, 0x40f260] |
| `RESOURCE` | 0x150 | string | `game.res` | the game archive's name (`SC_RES_FILE`). The shipped scripts set it with `scSet` before `scInit` [exe DC 0x40f523, 0x40f32c] [script DC: `scSet(SC_RES_FILE, "scratches.res")`] |
| `PATH` | 0x050 | string | `.\` | `\` is appended to a cfg value [exe DC 0x40f567, 0x40f2d4]. The DC script sets `SC_RES_PATH` to `.\` itself [script DC: top level] |
| `WIDTH` | 0x000 | %d | 1024 | [exe DC 0x40f5d2, 0x40f260] |
| `HEIGHT` | 0x004 | %d | 768 | [exe DC 0x40f606, 0x40f260] |
| `DEPTH` | 0x008 | %d | 32 | [exe DC 0x40f63a, 0x40f260] |
| `QUALITY` | 0x01c | %d | 3 | only 0 differs: textures use `GL_NEAREST` filtering instead of `GL_LINEAR`, and the point, line and polygon smoothing hints are `GL_FASTEST` instead of `GL_NICEST`. Perspective correction is `GL_NICEST` and the fog hint `GL_FASTEST` either way [exe DC 0x4070e8–0x40713b, 0x4072c6–0x4072f8, 0x40bd00]. The setup menu has no quality option; only a debug text that is never shown prints it [exe DC 0x40e648, 0x40e105, 0x40725c] |
| `FULLSCREEN` | 0x034 | %d | 1 | passed to the media-layer start-up [exe DC 0x40f6a2, 0x4102e0] |
| `SURROUND` | 0x1a0 | %d | 1 | the shipped files set 0 [exe DC 0x40f6d6, 0x40f260; data scream.cfg] |
| `LOG` | 0x02c | %d | 0 | the logger writes `scream.log` only when this is non-zero [exe DC 0x40f70a, 0x404a45] |
| `VSYNC` | 0x03c | %d | 1 | when non-zero, the GL set-up sets a swap interval of 1; when 0, it leaves it alone [exe DC 0x419917–0x419929, 0x419526] |
| `RATE` | 0x040 | %d | 0 | [exe DC 0x40f772] [unverified: use] |
| `AUDIO_DEVICE` | 0x044 | %d | 0 | used only inside the `DEBUG` device enumeration, as an index into the device list [exe DC 0x401db0] |
| `BUFFER_SIZE` | 0x048 | %d | 32768 | [exe DC 0x40f7da, 0x40f2c0] [unverified: use] |
| `NUM_BUFFERS` | 0x04c | %d | 4 | [exe DC 0x40f80e, 0x40f2ca] [unverified: use] |
| `DEBUG` | 0x2a4 | %d | 0 | when non-zero, enumerates and logs the OpenAL devices [exe DC 0x40f842, 0x401bd4] |
| `LANGUAGE` | 0x2a8 | string | `en` | [exe DC 0x40f876, 0x40f260]. Returned by `scGet(SC_LANGUAGE)` [exe DC 0x4125de] |

**2006.** The patched 2006 parser has the same keys except `DEBUG`
[exe 2006 0x40d610]. `SCRIPT` sits at 0x224 and `LANGUAGE` at 0x2a0; the other offsets are
unchanged. The retail 2006 exe has no `LANGUAGE` string at all [exe 2006 retail: string
search].

**Shipped files.** Both use CR LF line endings [data dc/scream.cfg, 2006/scream.cfg]. Neither
sets `RESOURCE`, `LANGUAGE` or `DEBUG`.

| Key | DC | 2006 |
|---|---|---|
| `SCRIPT` | `SCRATCHES` | `SCRATCHES` |
| `PATH` | `.\\` | `.` |
| `WIDTH` × `HEIGHT` | 1024×768 | 800×600 |
| `DEPTH` | 0 | 0 |
| `QUALITY` | 1 | 1 |
| `FULLSCREEN` | 1 | 1 |
| `RATE` | 0 | 0 |
| `VSYNC` | 1 | 1 |
| `SURROUND` | 0 | 0 |
| `LOG` | 0 | 0 |
| `AUDIO_DEVICE` | 0 | 0 |
| `BUFFER_SIZE` | 16384 | 32768 |
| `NUM_BUFFERS` | 4 | 4 |

---

## `.srt` subtitles

`scLoadSubtitle(file)` opens the file with `fopen(name, "r")`: a loose file in text
mode, not an archive entry [exe DC 0x418092–0x418097]. It then sets the player's active flag
[exe DC 0x418086].

The shipped file looks like SubRip, with CR LF line endings [data sdc-intro.srt]:

```
1
00:00:05,000 --> 00:00:08,615
In other words, you must head right away
to the Blackwood estate and get me something.

```

### The player

The player runs every frame [exe DC 0x4180b0, called from 0x412010].

**Clock.** A clock goes up by 1.0 each time at least 1000 timer units have
passed since the last tick. The test is `(now − last) × 0.001 × 0.1 ≥ 0.1`
[exe DC 0x4180c7–0x418166]. So cue times have one-second resolution in practice. The clock
is never reset: it starts at 0 when the program starts, and only the player writes it
[exe DC 0x418080; no other writer of 0x48bc58].

**End of a cue.** When the clock reaches the current end time (0 at first), the player clears
the shown text [exe DC 0x41817e, 0x40e150]. It then reads the next cue, unless
the file is at its end [exe DC 0x418192].

**Reading a cue:**

1. `%d` reads the index. **Index 13 stops the player**, so cue 13 is never shown
   [exe DC 0x418192–0x4181bd].
2. `%s` reads the start time, `%s` reads `-->`, and `%s` reads the end time.
3. `fgets` consumes the rest of that line [exe DC 0x418326].
4. Text lines are read with `fgets` until a line equals `"\n"`. Each line is appended with
   `strncat`, followed by a space [exe DC 0x41834a–0x4183ae].

**Line endings.** Each line's length is taken as the difference of two `ftell` calls, minus 2
[exe DC 0x418351–0x41836a]. That is right for CR LF lines. An LF-only line would lose its last
character [unverified: assumes `ftell` returns the raw offset in text mode].

**Showing the text.** The cue text is shown like `scFeedback` text, on the first tick at or
after the start time [exe DC 0x41812e–0x418158, 0x40e150].

**Time parsing.** Times are parsed with `atoi` on three-character fields at offsets 0, 3, 6
and 9. The weights are 12000, 600, 10 and 1/100 (the 1/100 by integer division), and the sum
is multiplied by 0.1 [exe DC 0x4180b0; constant 0x483e2c = 0.1]. As a result:

- minutes and seconds come out right;
- milliseconds are truncated to tenths;
- **an hour counts as 1200 s (20 minutes)**.

**Large clock values.** While an end time is parsed, the field weight steps down only if the
clock is at most 100.0 (its float bits, read as an int, are `< 0x42c80001`)
[exe DC 0x4182dd–0x4182ff]. Past 100 s, every field of an end time is weighted 12000, so that
cue effectively never ends.

**The shipped file.** `sdc-intro.srt` has 12 text cues. The last one ends at 00:01:37,145
(97 s), inside the 100 s limit. It is followed by an empty cue 13 (00:01:37,145 →
00:01:38,000), which the player stops at without showing [data sdc-intro.srt].

---

## Appendix: original save files (`scream.0NN`)

This appendix is needed only to read or write the original game's own save files. An engine
with its own save format can skip it. It gives the on-disk layout; where a field's meaning
is not confirmed, it is marked.

**File names.** The name is `scream.` + `00` + slot for slots under 10, else `scream.` + `0` +
slot. Files are opened with `"wb"` / `"rb"` [exe DC 0x411393–0x411433, 0x4107ed].

| Slot | File | Used for |
|---|---|---|
| 11 | `scream.011` | the F5 quicksave, description `quicksave` [exe DC 0x417ab5–0x417abf] |
| 1–10 | `scream.001`–`scream.010` | the menu's ten save slots; the description comes from the script's `save()` [exe DC 0x413bb4–0x413bd7, 0x413bcf] [script DC: save] [script 2006: save] |
| 0 | `scream.000` | the menu also writes and reads slot 0, with description `new` [exe DC 0x41383e, 0x4138a2, 0x4137bf, 0x413894]. [unverified: its purpose] |

### Overall shape

The file is a dump of the engine's tables [exe DC 0x411370 writer, 0x4107d0 reader]
[exe 2006 0x40f1f0 writer]. Sizes below are given as DC / 2006 (patched
exe). [unverified: the retail 2006 exe's format; that exe contains no `scream.0` string]

- The world tables (rooms, nodes, zooms, objects, items) are written as **last index L**,
  then records 0..L, in creation order. Record 0 is unused.
- When reading, each room, node, zoom and object record goes to the table slot named by its
  own index field [exe DC 0x410bae, 0x410d66, 0x410e92].
- Items are read positionally, for 0 up to the item table's last index in the running game;
  the file's L is read but not used as the bound [exe DC 0x410ec7–0x410fee].
- A save therefore only fits the world built by the same script.

### 1. Description

| Size | Field |
|---|---|
| char[80] | description [exe DC 0x411482–0x411489] [exe 2006 0x40f302–0x40f309] |

### 2. Script globals

| Size | Field |
|---|---|
| i32 | count [exe DC 0x4114df] |
| count × (i32 len, char[len]) | `key=value\0`, where `len` = strlen(key) + strlen(value) + 2 and so includes the NUL [exe DC 0x41193e–0x411a95] |

The file holds every global whose value is a number or a string that converts to one
(`lua_isnumber`) [exe DC 0x4114a6–0x411912, 0x43d460]. Other strings, booleans and tables are
not saved. On load, each line runs as a Lua chunk named `line` [exe DC 0x410957–0x41095c;
string 0x4838fe]. Globals the file doesn't mention keep their current values
[exe DC 0x4107d0].

### 3. Settings block

700 / 676 bytes: a raw copy of the engine's settings [exe DC 0x411524–0x411535]
[exe 2006 0x40f588–0x40f599]. Loading restores the whole block [exe DC 0x410990–0x4109a1].
DC offsets within the block:

| Offset | Type | Field | Source |
|---|---|---|---|
| 0x000 / 0x004 / 0x008 | i32 | WIDTH, HEIGHT, DEPTH | cfg keys [exe DC 0x40f4a0] |
| 0x00c, 0x010 | i32 | RATIOX, RATIOY: the size of the box the cube faces are drawn on, default 1, 1 (see the [camera spec](camera.md)) | [exe DC 0x40f260, 0x40c1b1, 0x40c1bf] |
| 0x014, 0x018 | i32 | `SC_VIEWX`, `SC_VIEWY`, default 16, 9; stored by `scSet`, read by nothing | [exe DC 0x40f289–0x40f293, 0x410011, 0x41001c] |
| 0x01c | i32 | QUALITY | cfg key |
| 0x020 | f32 | speed factor: 60 divided by the mean frame rate since start-up, recomputed once a second; default 1.0 | [exe DC 0x40f2a7, 0x40e890] |
| 0x024 | i32 | gamma level 0–3, cycled by F12; default 0 | [exe DC 0x40f260, 0x417784–0x417bc7] |
| 0x028 | f32 | field of view in degrees, default 45.0; set by `scSet(SC_FOV)` | [exe DC 0x40f2b1, 0x40ffc6, 0x40e07d; debug text "vision_angle"] |
| 0x02c | i32 | LOG | cfg key |
| 0x030 | i32 | media-layer flag, default 1 | `scInit` opens the window, sets up graphics and starts the worker threads only if it is non-zero; sound starts regardless [exe DC 0x410320, 0x4104cd, 0x40f2f8] |
| 0x034 | i32 | FULLSCREEN | cfg key |
| 0x038 | i32 | archive flag, default 1 | 0 makes assets load as loose files (see [Lookup](#lookup)) [exe DC 0x410445] |
| 0x03c–0x04c | i32 ×5 | VSYNC, RATE, AUDIO_DEVICE, BUFFER_SIZE, NUM_BUFFERS | cfg keys |
| 0x050 | char[256] | RES_PATH | cfg `PATH` [exe DC 0x410454] |
| 0x150 | char[80] | RES_FILE | cfg `RESOURCE` [exe DC 0x41044a] |
| 0x1a0 | i32 | SURROUND | cfg key |
| 0x1a4, 0x1a8 | i32 | camera speed (F9) and text speed (F10), default 50 each | [exe DC 0x40f260, 0x417733, 0x417763] |
| 0x1ac | i32 | `SC_MOV_SPEED` (crossfade rate), default 25 | [exe DC 0x40f260, 0x40ff3d] |
| 0x1b0 | i32 | `SC_SKY_SPEED`, default 15 | [exe DC 0x40f260, 0x40ff69] |
| 0x1b4 | i32 | `SC_SHOW_HOTSPOTS`, default 0 | [exe DC 0x40f3e4, 0x40fb17, 0x40b88c] |
| 0x1b8 | i32 | `SC_TEX_SIZE`, default 1280. `scSet(SC_MSK_SIZE)` and `scSet(SC_SKY_SIZE)` also write this field, apparently an original slip | [exe DC 0x40f260, 0x40fe7b, 0x40fea3, 0x41011a–0x41014e, 0x410190, 0x4101aa] |
| 0x1bc | i32 | `SC_IMG_SIZE`, default 1024; also the size of the crossfade capture | [exe DC 0x40f384, 0x40ff1a, 0x407ae5] |
| 0x1c0 | i32 | default 512; read when texture slots are set up | [exe DC 0x40f260, 0x40755f–0x4075c8] [unverified: name; `scSet` never writes it] |
| 0x1c4 | i32 | default 512; read when texture slots are set up | [exe DC 0x40f40d, 0x407173, 0x40740e, 0x4076a3] [unverified: name; `scSet` never writes it] |
| 0x1c8 | i32 | `SC_INV_SIZE`, default 64 | [exe DC 0x40f3ea, 0x40fec6, 0x4101c0] |
| 0x1cc | i32 | `SC_CUR_SIZE`, default 64 | [exe DC 0x40f260, 0x40feee, 0x4101f0] |
| 0x1d0, 0x1d4 | i32 | `SC_SHOWMENU` and `SC_HINTS`, default 0 | [exe DC 0x40f260, 0x41349d, 0x40fbcf, 0x417a23] |
| 0x1d8 | char[80] | SPLASH, default `sc_logo.tga` | [exe DC 0x40f398, 0x410473] |
| 0x228 | char[80] | SCRIPT | cfg key |
| 0x278 | i32 | slideshow mode, default 0 | [exe DC 0x40f260, 0x414448; string 0x483ade] |
| 0x27c | i32 | feedback glow pass, default 0; nothing sets it | [exe DC 0x40f260, 0x40aca0] |
| 0x280–0x2a0 | i32 ×9 | effect flags, in order `SC_WAIT`, `SC_SKY`, `SC_NIGHT`, `SC_GLOW`, `SC_RAIN`, `SC_FOG`, `SC_DREAM`, `SC_CANDLE`, `SC_LAMP`; defaults 1, 1, then 0 | [exe DC 0x40f260; `scEnable` writes 0x40fbda, 0x40fb77, 0x40fb82, 0x40fb8d, 0x40fb98, 0x40fab0, 0x40fba3, 0x40fbae, 0x40fbb9] |
| 0x2a4 | i32 | DEBUG | cfg key |
| 0x2a8 | char[4] | LANGUAGE | cfg key |
| 0x2ac | i32 | alternate control (`SC_FPS_STYLE`), default 1; toggled by the setup menu | [exe DC 0x40f260, 0x413cf6–0x413d3a; strings 0x483af0, 0x483b0b] |
| 0x2b0, 0x2b4, 0x2b8 | i32 ×3 | the sky, cloud and screen ambience presets, `SC_MORNING` (311) at start | [exe DC 0x4102e0, 0x418658–0x41867d]; applied at once on load [exe DC 0x41125b–0x411290] |

**2006.** The block is 24 bytes shorter [exe 2006 0x40f58d]. `SCRIPT` sits 4 bytes earlier and
`LANGUAGE` 8 bytes earlier [exe 2006 0x40d610], and the block ends with `LANGUAGE`. So 2006
lacks one 4-byte field before 0x224, `DEBUG`, and the 16 bytes after `LANGUAGE`.
[unverified: that the missing field before 0x224 is `IMG_SIZE`; 2006 does lack `SC_IMG_SIZE`
(script 2006: scream.sc)]

### 4. Journal (DC only)

The 2006 file has none of these fields [exe 2006 0x40f599 → 0x40f5b1].

| Size | Field | Source |
|---|---|---|
| i32 | journal value A | [exe DC 0x41153a] [unverified: meaning] |
| i32 | journal value B | [exe DC 0x411550] [unverified: meaning] |
| i32[1000] | the used-index flags of `scJournalAdd` | [exe DC 0x411568, 0x418558] |
| char[40] × 1000 | the journal lines | [exe DC 0x411590–0x4115ed] |
| char[48] | the current font file, loaded again on load | [exe DC 0x4115ef, 0x4112eb–0x4112f5] |

### 5. View and location

| Size | Field | Source |
|---|---|---|
| 4 | yaw, in radians | [exe DC 0x411605–0x41162b] (see the [camera spec](camera.md)) |
| 4 | DC: the look vector's y; 2006: the pitch angle in radians | [exe DC 0x411643] [exe 2006 0x407b5c] (see the [camera spec](camera.md)) |
| i32 | engine state; 0xfa2 (4002, `SC_STZOOM`) means the save was made in a zoom | [exe DC 0x411667, 0x4111aa] [script DC: scream.sc] |
| i32 | second state value | [exe DC 0x41167b] [unverified: meaning] |
| i32 | third state value | [exe DC 0x41168f] [unverified: meaning] |
| i32 | current room index | [exe DC 0x4116a6] |

### 6. Global timers

| Size | Field |
|---|---|
| i32 | count C [exe DC 0x4116bf] |
| (C + 1) × 68 bytes | timer records 0..C; record 0 is unused [exe DC 0x4116c4–0x4116f7, 0x412330] |

Each 68-byte record [exe DC 0x40615b, 0x412330]:

| Offset | Field |
|---|---|
| +0x00 | i32 id |
| +0x04 | i32 seconds |
| +0x08 | i32 elapsed, 0 at creation |
| +0x0c | i32 active, 1 at creation |
| +0x10 | char[48] handler name |
| +0x40 | 4 bytes: a code address inside the exe that wrote the file [exe DC 0x40615b → 0x404d80] [exe 2006 0x405cc9, 0x405d29 → 0x404c20] |

### 7. Rooms

| Size | Field |
|---|---|
| i32 | last index L [exe DC 0x41170f] |

Each room record [exe DC 0x4117bb–0x41188b]:

| Size | Field | Source |
|---|---|---|
| i32 | index | [exe DC 0x4117bb, 0x410ba9] |
| i32 | current node index | [exe DC 0x4117d3] |
| i32 | remembered node index (where `scSwitch(SC_ROOM)` enters) | [exe DC 0x4117eb, 0x415bd2, 0x415a4e] |
| i32 | zoom-stack depth D; the open zoom is entry D | [exe DC 0x411803] |
| i32 × 377 / 368 | zoom stack (zoom indices) | [exe DC 0x41181b] [exe 2006 0x40f7b8] |
| timer table | the room's own timers, laid out as in section 6 | [exe DC 0x411835–0x41188b] |

### 8. Nodes

| Size | Field |
|---|---|
| i32 | last index L [exe DC 0x411b39] |

Each record is an i32 index, then six char[48] face names, then six char[48] mask names
[exe DC 0x411b7a–0x411bed] [exe 2006 0x40f92d–0x40f966]. Loading restores both
[exe DC 0x410d71–0x410da6]. The six names are in face order, `SC_FRONT` (1) to `SC_DOWN` (6),
because the writer copies the node record's face array, which `scTexture` fills in that order
[exe DC 0x4149c7–0x414e07, 0x411b7a–0x411bed] (derived).

### 9. Zooms

| Size | Field |
|---|---|
| i32 | last index L [exe DC 0x411c07] |

Each record is an i32 index, then char[48] image, then char[48] mask
[exe DC 0x411c48–0x411cbb, 0x414a0b, 0x414a7d]. The built-in `black` zoom is created before
the script's zooms [exe DC 0x410418]. [unverified: that it is index 1]

### 10. Objects

| Size | Field |
|---|---|
| i32 | last index L [exe DC 0x411cd5] |

Each record is an i32 index, then one i32 [exe DC 0x411cff–0x411d4d]. [unverified: that the
second value is the enabled flag]

### 11. Items

| Size | Field |
|---|---|
| i32 | last index L [exe DC 0x411d67] |

Each 900 / 612-byte record [exe DC 0x411dd5–0x411e8c] [exe 2006 0x40fb79–0x40fbca]:

| Size | Field |
|---|---|
| i32 | index |
| char[48] | icon; loaded again on load [exe DC 0x4112af–0x4112d0] |
| char[400 / 280] | text 1 |
| char[400 / 280] | text 2 |
| char[48] | DC only |

[unverified: that the texts are the label and fail text, and the DC-only field the fail
voice, of `scInventorySet`]

### 12. Inventory

| Size | Field |
|---|---|
| i32 | count C [exe DC 0x411eaa] |
| i32 × (L + 1) | slots, where L is the item table's last index; slots 1..C hold item indices [exe DC 0x411ec7–0x411eeb] |

### 13. Sound channels

Channels 1–17, one 60-byte record each [exe DC 0x411eed–0x411faa, 0x411129–0x41117d]:

| Size | Field | On load |
|---|---|---|
| char[32] | file name | — |
| i32 | loaded flag | if set, the file is loaded again |
| i32 | playing flag | if set, playback restarts at the saved volume |
| 4 bytes | channel value | [unverified: meaning] |
| 4 bytes | channel value | [unverified: meaning] |
| 4 bytes | fade-in rate | if non-zero, a fade-in starts |
| 4 bytes | fade-out rate | if non-zero, a fade-out starts |
| 4 bytes | loop flag | [unverified] |

Loading skips the restart for channels 8 (`SC_STEP`) and 14 (`SC_FX`)
[exe DC 0x41111f–0x411127] [script DC: scream.sc]. Fades are described under
[Sound effects](api.md#sound-effects).

### 14. Playlist

A raw block of 196 / 172 bytes holding the playlist state, restored as is on load
[exe DC 0x411fb0, 0x41118c] [exe 2006 0x40fd3b]. No shipped script uses the playlist
[script DC, 2006: no `scPlaylist*` call sites].

### Loading a save (DC)

After reading the file, the game [exe DC 0x4111aa–0x4112f5]:

- enters the saved room at its remembered node, which runs the room's `scOnEnter` handler
  [exe DC 0x4111d8, 0x415a70];
- if the save was made in a zoom, opens the zoom on top of the saved zoom stack;
- restores the camera angles;
- applies the three ambience presets at once;
- loads the item icons and the font again.

[unverified: the loader also makes one `SC_ZOOMOUT` switch (kind 32); its visible effect was
not traced]

"Game saved" shows after a save whether or not the file could be written; the failure is only
logged (`Could not save game!`) [exe DC 0x4118c0, 0x413beb–0x413bf0, 0x417ac4–0x417ac9].

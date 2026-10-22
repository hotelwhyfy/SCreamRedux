# SCream engine notes

Reverse-engineering notes for the SCream engine (Nucleosys) used by *Scratches* (2006)
and *Scratches – Director's Cut* (2007).

## Source material

| Source | Engine exe | Scripts | Data |
|---|---|---|---|
| 2006 retail disc | `scream.exe` (Jan 2006) | Lua 5.0 bytecode | `scratches.res` (2829 entries) |
| 2006 patch (`scratches-patch.exe`) | `scream.exe` (Apr 2006, same size, ~474 KB differs) | — | — |
| DC disc 1 | `scream.exe` (Apr 2007) | Lua 5.1 bytecode | `scratches.res` (3351 entries, superset of 2006) |
| DC disc 2 update | `scream.exe` (May 2007) | `scratches.sc` (Jun 2007) | `update.res` (1623 entries: 1575 replace, 48 new) |
| DC patch (`scratches-dc-patch.exe`) | — | `scratches_pre.sc` (May 10 2007), `scratches_post.sc` (Jun 2007) | — |

DC patch is script-only. `scratches_post.sc` is byte-identical to the disc 2 update's
`scratches.sc`. `scratches_pre.sc` matches the disc 1 script except it never plays
`vid-logo_us.ogg` (presumably for installs without the update; installer logic not inspected).
The Jun 2007 script adds Bailey dialogue branches, `hs-gallery_n*d` / `chpl-int_n5ds2` images
and `trck-alt.ogg`. **Port target:** DC `scratches.res` + `update.res` overlay + Jun 2007 script.

All installers are NSIS and unpack with `unar`; DC disc 1 data is a split 7z (`scratches.7z.001`…).
`scupdate.exe` merges `update.res` into `scratches.res` by filename (replace or add), so a new
engine can apply it as an overlay archive instead.

Original engine stack (from `scream.exe` strings): Open Watcom C/C++, OpenGL, OpenAL,
Lua 5.1.1 (DC) / 5.0 (2006), IJG libjpeg, libogg/vorbis/theora.

## `.res` archive format

Little-endian. See `tools/scream_res.py`.

```
0x000  char[256]  "SCream resource file\0" + uninitialized padding
0x100  u32        entry count
0x104  entry[count], 92 bytes each:
         char[80] name     NUL-terminated, remainder is garbage
         u32      type     1 = JPEG, 2 = TGA, 3 = Ogg
         u32      offset   absolute
         u32      size
```

Names are flat (no directories). Files are stored uncompressed.

## Assets

- **Nodes are cube maps**: `<area>-<room>_n<N><face>.jpg`, face ∈ `f b l r u d`, 1024×1024 JPEG.
- **Face overlays**: suffixed variants (`_n1l1` … `_n1l12`, `_n3fs`) are alternate/animated
  states for one face, each with an 8-bit 512×512 RLE TGA mask (`*_msk.tga`).
- **Prefixes**: `hs` house, `grdn`/`garden`, `grnhs` greenhouse, `chpl` chapel, `crpt` crypt,
  `inv` inventory icons, `cur` cursors (RGBA TGA), `font` (512×512 RGBA TGA atlases),
  `dlg`/`msg` voice, `fx` sound effects, `trck` music (channel suffixes `-F`, `-RL`, `-RR`
  match the `SURROUND` option), `vid` Theora video.

## Scripts

- `scream.sc` = engine defaults/constants (`SC_DEF_*`, `SC_*`); `scratches.sc` = the game
  (`@scratches-en.lua`). Both are compiled with debug info (line info, local names), so they
  should decompile well (e.g. unluac, which needs a Java runtime — not installed yet).
- DC 5.1 bytecode header: little-endian, int=4, **size_t=4**, instruction=4, number=8 (double).
  A stock 64-bit Lua 5.1 build rejects `size_t=4` chunks, so the loader (`lundump.c`) needs a
  small patch to read 32-bit sizes.
- The game talks to the engine through 87 global C functions named `sc*` (an earlier `strings`
  scan counted 88, but `scH` is just the bytes of a `mov edx, 0x486373` instruction). The DC exe
  registers all of them, and the DC scripts use 64 of them:

```
scAbort scAttach scAudio scBegin scBind scChoiceAdd scChoiceClear scChoiceGo scChoiceRemove
scCreate scCursor scCutscene scDisable scEnable scEnd scFXPlay scFXPlayEx scFXSet scFXStop
scFeedback scFeedbackEx scFont scFootstep scGet scGo scHints scHintsSet scInit
scInventoryAdd scInventoryReaction scInventoryRemove scInventorySet scJournalAdd
scJournalClear scJournalSet scJournalShow scLoadSettings scLoadSubtitle scLock scLookAt
scMenuSet scMenuSetEx scMenuSetSaveFunc scMenuShow scMotion scMusicFadeIn scMusicFadeOut
scMusicPlay scMusicStop scOnClick scOnEnter scOnExit scOnHover scOverlay scPlace
scPlaylistClear scPlaylistFirst scPlaylistLast scPlaylistNext scPlaylistPrevious
scPlaylistQueue scPlaylistStart scPlaylistStop scReaction scRun scScriptAdd scScriptGo
scSet scSetAmbience scSetLastVisited scSleep scStill scSwitch scTerminate scTexture
scTextureUpdate scTimerCreate scTimerCreateGlobal scTrackFadeOut scTrackPlay scTrackSet
scTrackSet3D scTrackSetEx scTrackStop scUnloadSubtitle scUnlock scWait
```

  New in DC vs 2006: `scFeedbackEx scJournal* scLoadSubtitle scSetAmbience scUnloadSubtitle`.

### Decompiled scripts

`tools/vendor/unluac.jar` (unluac 2025-12-23, run with `/opt/homebrew/opt/openjdk/bin/java -jar`)
decompiles all four scripts cleanly into `extracted/decompiled/{dc,2006}/`. DC
`scratches.lua` is ~23.8k lines; 2006 is ~20.6k.

Script structure (DC):
- Lines 1–~1460: all dialogue/message text as globals (`dlg*`, `msg*`, `msgLoc*`).
- `create*()` functions build the whole world up front, declaratively:
  `scCreate(SC_ROOM, name)` / `scBegin` / `scCreate(SC_NODE, "n1")` / `scTexture(face, jpg)` /
  `scCreate(SC_OBJECT, name)` + `scPlace(x, y, w, h, face)` (face-pixel coords on a 1024 face;
  rects can run past an edge and continue on the neighbouring face) + `scOnHover(cursor)` +
  `scOnClick(SC_NODE|SC_ROOM|SC_CUSTOM|SC_FEEDBACK_EX…, target…)` / `scEnd`.
- Handlers (`SC_CUSTOM` targets, timers, menu callbacks) are straight-line code that blocks on
  `scLookAt(…, SC_SMOOTH)`, `scCutscene`, `scSleep`, `scFeedback`, dialogue. The new engine
  should run each handler as a Lua coroutine and yield from those C functions (5.1 allows
  `return lua_yield(L, n)` from a C function).
- Boot (end of file): `scSet` resource path/file → `scInit()` → cursors/fonts/menu/hints →
  all `create*()` → `scRun()` (main loop; returns on quit) → `scTerminate()`.
- Menu starts `newGameSDC` (main game, `font-michael.tga`) or `newGameTLV` (DC bonus
  *The Last Visit*, `font-peter.tga`); `intro`/`outro` play logo/credits videos.
- Game state is Lua globals (`phase`, `jerry_st`, `first_*` flags…). `save()` only returns the
  save-slot description string, so the engine itself must serialize globals + engine state.
- Dialogue: `scCreate(SC_SCRIPT, name)` + `scScriptAdd(ogg, text, flag)` sequences, played by
  `scScriptGo`; branching via `scChoiceAdd`/`scChoiceGo`.
- `scream.lua` is just `SC_DEF_*` defaults and `SC_*` enum values (see `extracted/decompiled/dc/scream.lua`).

### API scope

`tools/api_usage.py` → `docs/api-usage-dc.md`, `docs/api-usage-2006.md` (counts, arities,
constants per argument, examples). DC uses 64 of 87 registered functions; 2006 uses 55, all a
subset of DC's. Never called by either:
`scAttach scChoiceClear scChoiceRemove scJournalShow scLoadSettings scMenuShow scMotion
scMusic* scOnEnter scOnExit scOverlay scPlaylist* scStill`.

### Engine semantics established so far

Verified against the shipped scripts and data while building the engine (`src/`):

- **Hotspot space**: `scPlace(x, y, w, h, face)` is x/y/width/height in a logical 1024×1024
  face, regardless of texture size (faces exist at 1024 and 1280). ~23% of rects overrun the
  face edge; authors split edge-crossing hotspots into two objects by hand, so the engine clips.
- **Zooms**: hotspots use a logical **800×472** space spanning the whole image. The DC update
  replaces 343 zoom images with 1024×640 re-renders of the same shot (16:10), so images are drawn
  at their own aspect (800 wide) and hotspots are mapped proportionally. The face argument of
  `scPlace` is ignored inside zooms (`open_mailbox` uses `SC_BACK` and still worked).
- **The DC update is an HD remaster**: 1,106 cube faces go from 1024² to 1280².
- **`"black"` zoom** is never created by any script; it is an engine built-in blank screen.
- **Zoom stack** (confirmed in `scream.exe`, see below): `SC_ZOOM` always pushes,
  `SC_ZOOMOUT` pops one level, `SC_ZOOMBACK` pops everything back to the node. (An earlier
  inference from the scripts had ZOOMOUT/ZOOMBACK swapped.)
- **Click bindings**: when a click runs a handler, the bound room/view/object are the clicked
  ones (`lookKnob_dc` starts with a bare `scDisable(SC_OBJECT)`).
- **`scBind(SC_OBJECT, name)`** binds every same-named object in the view (names repeat for
  split hotspots, e.g. two `to_n2` in garden n1).
- **`scTextureUpdate(face, file, flag)`**: flag is 1 when the bound node is the one being shown.
- **Blocking calls** (`scSleep`, `scLookAt(…, SC_SMOOTH)`, `scCutscene`, `scScriptGo`,
  `scFXPlayEx(…, wait=1)`) suspend the handler coroutine. The original runs nested render loops
  instead; the script-visible behaviour is the same. Voice waits were checked against clip
  lengths (5.96 s / 8.41 s clips → 5.98 s / 8.46 s measured).
- **`scChoiceGo`** is non-blocking (scripts call `scUnlock()` right after); the chosen option's
  callback runs as a new handler. **`scScriptGo`** plays a dialogue to completion.
- Scripts pass nil for undefined names (`SC_HITNS` typo, `SC_STEREO_PATCH`); the API tolerates it.

### Media

- Audio: Ogg Vorbis, 44.1 kHz (a few 48 kHz), mono/stereo; decoded with stb_vorbis into one
  SDL3 audio stream per voice. Videos (`vid-*`, `intro-*`, `outro-*`) are silent Theora 4:2:0 at
  24 fps (640×352, 800×472 or 1024×640); their sound is separate `scFXPlay` calls.
- Fonts: 512×512 RGBA atlases, 16×16 grid of 32 px cells in Windows-1252 order, no width table
  (widths are measured from alpha).
- `scream --verify-media` decodes every asset: DC base 3357/3357, DC+update 3405/3405 OK.
- Of 2914 files the DC script references, 5 are missing from the archives; only `black.jpg`
  (zoom `kitchen-cellar`) is reachable, and renders black, which is presumably the intent.

### From `scream.exe` (Ghidra)

DC update `scream.exe`, Open Watcom, register calling convention (args in EAX, EDX, EBX,
ECX), so Ghidra's decompiler hides arguments; read the disassembly alongside. Scripts in
`tools/ghidra/` (run headless with `JAVA_HOME=/opt/homebrew/opt/openjdk@21/...`):
`ExportScApi.java` (decompile every registered `sc*` function), `DumpFunctions.java`,
`XRefs.java`, `FindInsn.java`. Output goes to `extracted/ghidra/`.

- **87 functions**, not 88: `scH` was the bytes of `mov edx, 0x486373`.
- Helpers: `0x43d650` lua_tostring, `0x43d5a0` lua_tonumber, `0x43d950` lua_pushcclosure,
  `0x4036c0` GetBind(kind) (bound object per kind 2000–2008), `0x404d80` "call Lua function
  by name" callback (used by scOnClick SC_CUSTOM, timers, choices, reactions, room hooks).
- Engine states in the main loop (`0x416a10`) are the `SC_ST*` constants (4000 node,
  4001 video, 4002 zoom, 4003 inventory, 4004 overlay, 4005 menu, …).
- `scLock`/`scUnlock`: boolean at `0x48bc0c` (also set/restored around internal operations).
- `scWait`/`scGo`: wait mode `0x48bc10` — the cursor (wait cursor) is drawn while locked; only
  if SC_WAIT is enabled (`0x4df8de4`, default on, key 25 in scEnable/scDisable).
- `scMotion`/`scStill`: `0x48bc08` gates camera input. `scSleep` (`0x4124a0`) clears it and
  enters wait mode for the duration, then sets motion back to 1 unconditionally.
- Camera (`0x407e20`, `0x40df60`): yaw in radians; pitch is the look vector's y, clamped to
  ±1.75 (~60.3°). Edge zones are 15% of the width; vertically 15% of (H−192) from the top bar
  and from H−128. Rate per frame = depth_px × 0.005 × CAM_SPEED/1000 × ramp × 60/fps; ramp
  +0.025/frame, reset when the view stops. CAM_SPEED is 25/50/75. Keyboard steps 0.7855 rad.
  `SC_FPS_STYLE` selects a cursor-offset look mode that recentres the cursor (not ported yet).
- 3D view is `glViewport(0, 64, W, H−128)`: 64 px UI bars above and below (800×472 at 800×600).
- `scSwitch` (`0x4158d0`): ROOM switches the node then calls the room's onEnter hook;
  ZOOM pushes (unless re-showing after ZOOMOUT); ZOOMOUT pops one; ZOOMBACK pops all;
  OVERLAY switches to overlay state. The zoom stack lives in the room struct (+0x13c4).
- `scOnEnter`/`scOnExit` store hooks on the bound room (+0x188/+0x1bc); only +0x188 is ever
  called. `scOverlay(a,b,c)` copies three strings into the bound SC_OVERLAY object; the
  overlay draw (`0x40aae0`) puts one texture over the 3D view area.
- `scChoiceClear` sets the option count to 0; `scChoiceRemove` deletes the option at +0x38
  (the last pick) and shifts the rest.
- `scPlaylist*` (`0x4165e0`–`0x4168f0`, driven from `0x40ee00`): up to 38 tracks, 1-based
  index; Next/Previous clamp; when a track ends the index advances and wraps; changes
  crossfade between two music slots. Per-track repeat count/fade time fields (+0xd4/+0xd0)
  are approximated.
- No-ops in the original: `scAttach` (calls an empty function), `scMusicPlay/Stop/FadeIn/FadeOut`.
- `scAbort` (`0x4106d0`): stop playlist, wait for a key/click, return to the main menu.
  `scMenuShow` calls that same menu routine (`0x412cb0`); `scJournalShow` → `0x418580`.
- `scLoadSettings(file)` (`0x40f4a0`): scream.cfg keys SCRIPT, RESOURCE, WIDTH, HEIGHT, DEPTH,
  QUALITY, FULLSCREEN, SURROUND, VSYNC, AUDIO_DEVICE, BUFFER_SIZE, NUM_BUFFERS, DEBUG, LANGUAGE.

## Porting strategy

Embed Lua 5.1 and run the original `scream.sc` + `scratches.sc` unmodified; reimplement the
`sc*` API natively (cube-map renderer, hotspots via masks, audio, video, inventory, menus,
saves). Ghidra on `scream.exe` covers semantics the scripts don't reveal.

## Open questions

- Original save game format (only matters if we want to load original saves).
- The third arg of `scScriptAdd`, and exactly when the two zoom sounds play (`0x416120`,
  `0x4160d0`).
- `scOverlay`: which of the three strings is the image, and what the others do.
- Playlist per-track repeat count and fade-time fields.
- Whether `scTimerCreate` timers are cancelled on leaving the node (currently assumed).
- Loading the 2006 release: its scripts are Lua 5.0 bytecode, which Lua 5.1 cannot load.
- What the 2006 patch changed.

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

### Script checks (`scream --check`, `tools/check_script.py`)

- The world the DC script builds is consistent: every click/reaction target, room handler,
  choice handler and declared file exists, except 4 unreachable files (`hs-guest_pic6.jpg`,
  `hs-lair_skeleton.jpg`, `hs-maid_photo5.jpg`, track `test`'s `trck-test.ogg`).
- Handler literals all resolve, except two bugs in the original script, which raise Lua
  errors at run time: `musicEnterCave_dc()` (undefined; only `musicEnterCave` exists) at the
  end of the cellar-stairs handler, and `scTextureUpdate(SC_LEFT("hs-cellar_dc_n1l.jpg"), 0)`
  in `createLiving_dc` (calls a number, so the handler stops there).
- scream.exe runs handlers with `lua_pcall` (call-by-name `0x404d80`, `0x404e30`); a runtime
  error is written to the log as "Runtime error" (only with logging on) and the game carries
  on — the handler just stops. This port does the same.

### Binding (`scBind` `0x4148a0` = find `0x403fd0` + bind `0x402ce0`)

- `scSwitch` (`0x4158d0`) binds its target the same way before switching (except ZOOMOUT /
  ZOOMBACK): SC_ROOM binds the room and then its entry node, SC_NODE the node, SC_ZOOM the
  zoom, SC_OVERLAY the overlay. Zooming out to a parent zoom re-enters the switch as SC_ZOOM,
  so the parent zoom is bound; zooming out to the node rebinds nothing. Scripts rely on this
  (`scSwitch(SC_ZOOM, x)` then `scBind(SC_OBJECT, …)` / `scTextureUpdate(SC_ZOOM, …)`).
- `scBind(SC_OBJECT, name)` searches the bound node in node state (4000), the bound zoom in
  zoom state (0xfa2), and finds nothing in any other state (inventory, journal, ...).
- One object is bound at a time (an index into the global object table). `scBind(SC_OBJECT,
  name)` takes the first object of that name in the view (creation order); a click binds the
  clicked object itself (`0x414343`, EDX = the hovered index); `scEnable`/`scDisable` and the
  builder calls change only that object. Objects often share a name (one hotspot per cube face),
  and it matters: greenhouse-int/n7 has two `hose` hotspots (`grabHose1`, `grabHose2`), each
  disabling itself; disabling both on the first grab makes `hose_quarry`, the shovel and the
  ending unreachable.
- Nodes are found in the bound room, but zooms by name across all rooms, the first created
  winning — for `scBind(SC_ZOOM)` and `scSwitch(SC_ZOOM)` alike. Duplicate zoom names: `phone`
  / `phone-pick` (house-lobby and house-hall: the hall's version, with a clock hotspot, is
  never shown), `bath1-pic2`, `lair-bucket` (identical copies).

### Crawl (`scream --crawl`)

- 2638 tasks (every object click, scReaction, scInventoryReaction, choice option and dialogue,
  each from the start state, zooms reached through their parents) run clean apart from the two
  script bugs above. This found the binding rules above.

### Explorer (`scream --game tlv|sdc --explore`)

- An automated player limited to player actions (hotspot clicks, item use where a reaction
  exists, dialogue options), retrying everything whenever the game state changes and learning
  scripted doors/stairs as navigation. The Last Visit plays through to `finale_dc` (~250
  actions, 34 states).
- "State" is progress only: script globals, enabled hotspots, inventory, journal entries,
  choices. Location, last-visited nodes, textures, effect flags and which option was picked
  last are left out, or every walk and light switch would count as progress.
- Planning: the nearest action never done anywhere first, else the nearest one not done in this
  state; when nothing is left, backtrack by replaying recorded actions to the nearest state left
  with something untried (a handle that cycles through four positions). Routes follow the zoom
  stack. A stall (no action in 100000 frames) and the action cap end the run with a report.
- Clue puzzles (the studio box combination, Bailey's and Blake's phone numbers) take
  `--explore-hints FILE`, kept outside the repository (it is game content); format at the top
  of `src/explore.cpp`.
- Script quirks the main campaign has: relighting the passage candle sets `jerry_st` back to 17
  (calling Jerry again sets 18); turning the greenhouse faucet back on re-arms the glitter timer,
  which re-enables the small key in the pond (each grab is another hour on the clock).

### Media

- Audio: Ogg Vorbis, 44.1 kHz (a few 48 kHz), mono/stereo; decoded with stb_vorbis into one
  SDL3 audio stream per voice. Videos (`vid-*`, `intro-*`, `outro-*`) are silent Theora 4:2:0 at
  24 fps (640×352, 800×472 or 1024×640); their sound is separate `scFXPlay` calls.
- Fonts: 512×512 RGBA atlases, 16×16 grid of 32 px cells in Windows-1252 order, no width table.
  The game draws all its text monospace (see "Text" below); our proportional variant
  (widths measured from alpha) is only for this port's own menu.
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
- `scLock`/`scUnlock`: boolean at `0x48bc0c` (`0x412550`/`0x412560`). Every capture for a
  transition sets it to 1 and then to 0 (not restored), so switches, journal/menu changes,
  camera keys and overlay dismissal also unlock; the scripts rely on this (many `scLock()`
  branches end with `scSwitch(SC_ZOOMOUT, "")` and no `scUnlock`). The cutscene-end capture
  leaves it alone.
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
  called. `scOverlay(image, enterSound, leaveSound)` (`0x412a00`: +0x34/+0x64/+0x94); the
  overlay draw (`0x40aae0`) puts the image over the view area; a click (state 0xfa4) dismisses
  it with a crossfade and the leave sound.
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

### Inventory, journal, menu (from the scripts and `scream.exe`)

- Controls come from the tutorial hints (`msg01110`–`msg01117`): right-click opens/closes the
  inventory, left-click grabs an item, a held item is used with a left-click on an object
  (`scReaction`) or on another item (the target's `scInventoryReaction`, keyed by the held item).
- `scInventorySet(icon, label, failText, failVoice)`: the last two play when a held item is
  used where it has no reaction ("That doesn't work.", the magnifying glass's "Not much to see
  here...").
- Inventory draw (`0x409680`): the frozen view tinted (0.9, 0.75, 0.5), icons of `SC_INV_SIZE`
  in rows of 10, in a 1024-wide virtual space.
- Hints: with `scHints(SC_HINTS_ACTIVE)` and SC_HINTS on, opening shows `HINTS_OPEN`, grabbing
  the `HINTS_ITEM` item shows `HINTS_GRAB`, closing with it held shows `HINTS_CLOSE`.
- `scJournalSet(image, font, open, turn, close)` attaches the journal to the bound item.
- Journal (`0x418550`, `0x40e380`, draw `0x40b0a0`, input `0x413f80`): `scJournalAdd(index,
  text)` appends the entry once per index (flags at `0x4df7bb8`, saved with the game;
  `scJournalClear` `0x418490` resets only the lines). Entries after the first get a blank line
  before them; a line breaks at the first space once it is over 30 characters; `|` is a
  non-breaking space; at most ~1000 lines. Quirk: the last line is stored only if the entry
  wrapped or it isn't the first entry. The book (800×472 `journal.jpg`) covers the view like a
  zoom; 76 lines per spread in two columns of 38, black, x = 32 / 512, y = 100 + 16·row
  (1024×768 pixels). Clicks with y < 605 turn back (x < 512) or forward (x > 512) by 76 lines
  (`0x40e490` clamps; the turn sound plays only when the page changed); lower clicks close.
  The book opens at the page last shown.
- Inventory grid (hit test `0x417c90`, state 0xfa3): slot (row, col) at x = 32 + col·(32 +
  size), y = 64 + 96·row, size×size, 10 per row (1024×768 pixels). The inventory draw itself
  shows no text; our hover label is an addition.
- `FUN_00408550` is the original menu renderer (modes 0–4; the load/save lists draw 11 rows
  at x = W/2 − 120, y = H − 276 + 16·i). Not ported yet: our menu layout is our own.
- Menu strings in the exe: "Which game do you want to play?", "Scratches: Director's Cut",
  "Scratches: The Last Visit", "Start a new game?", "Load a saved game?", "Replace saved
  game?", "Quit the game?", "Game loaded/saved", "Could not load/save game!"; setup items
  hints, surround music, camera speed (SLOW/NORMAL/FAST), text speed, gamma correction,
  quality (LOW/MEDIUM/HIGH), slideshow mode, alternate control. Menu layout is not recovered.
- Saves are this port's own format (`src/save.cpp`): script globals as a diff against the
  start-of-game baseline, plus world/engine state. A new game restores that baseline.

### Text: feedback, subtitles, dialogue, choices (from `scream.exe`)

- Screen layout is in pixels of the configured mode; the DC's `scream.cfg` default is
  1024×768 (bars 64 px, view 1024×640 = the HD zoom size). This port maps that mode onto its
  800×600 canvas (0.78125 per pixel): bars 50 units, zooms/overlays/book stretched over the
  view, edge-zone depth in 1024-mode pixels.
- Text draw `0x40c040`: one GL display list per character (`0x40bf20`): a 16×16 px quad of the
  whole 32 px atlas cell, then a 10 px advance (monospace). Callers pass x and the cell's
  bottom y (top-down).
- Text setter `0x40e150` (used by scFeedback, dialogue, subtitles, prompts): ignores text of
  400+ chars; length n ≤ 100 → 1 line, ≤ 200 → 2 (share n/2), ≤ 300 → 3, else 4 (n/4); a line
  breaks at the first space once it has share−1 chars; `|` → non-breaking space. Duration
  `100/SC_TXT_SPEED + max(n, 32 if n ≤ 16)/16` s. Quirks: unreached line slots keep stale text,
  overflow lines write below the slot array (both not reproduced).
- Feedback renderer `0x40aca0`: lines stacked up from y = H − offset (offset 24/16/8 for 1/2/3
  lines), 16 px apart, x = W/2 − (len/2)·10 + 1; 4-line text shows lines 2–4 at offset −8 for
  the first half of the duration, then 1–3 at offset 8. Fade per line drawn per frame
  (×60/fps): in at speed·0.001, out at speed·0.0001 after the duration; cleared at full fade.
  A glow pass (flag `0x4df8de0`) is dead code: nothing sets it.
- SC_TXT_SPEED / SC_CAM_SPEED (`0x4df8d0c` / `…d08`, set in `0x40fde0`): SC_LO/MED/HI → 25/50/75,
  other values as given.

### Keyboard (`0x4175b0`, registered as the media layer's key callback; Windows VK codes)

- Camera (each after a short fade, `0x407b20`, progress 0.75; `0x407d30`/`0x407d10` get/set
  the look angle and y): VK_LEFT, 'A', VK_NUMPAD1 → angle −1.57; VK_UP, 'D', VK_NUMPAD4 →
  +1.57; VK_RIGHT, 'W', VK_F8 → y 1.25; VK_DOWN, 'S', VK_F4 → y −1.25; Space, 'E', VK_NUMPAD5
  → y 0. (The letter cases seem written as characters, which aliases numpad/F-keys, and the
  arrows look like an assumed LEFT, RIGHT, UP, DOWN order — ported as compiled.)
- 'L' / VK_SEPARATOR: toggle the log file (`0x4df8b90`, read by the logger `0x404a30`).
- F1 hints ("hints ON/OFF"), F2 surround music (`0x4df8d04`, restarts the track), F5
  quicksave (not in the menu; "scream.011", "Game saved"), F7 quickload ("Game loaded"), F9 /
  F10 camera / text speed 50→75→25→50 ("camera speed NORMAL/FAST/SLOW"), F12 gamma
  (`0x4df8b88` 0→1→2→3→0, "gamma correction LOW/NORMAL/HIGH/DISABLED"). The setup menu
  (`0x413640`) calls this handler with F1/F2/F9/F10/F12 for its items.
- Esc: inventory → close (with a crossfade); journal → close; choice → nothing; overlay →
  dismiss; else the menu if it has been shown (`0x4df8d34`, set by `0x4131c0`), otherwise quit
  (`0x410740`). Not handled here inside the menu.
- Gamma correction (`0x4082a0`, drawn by every screen before the transition): the screen is
  copied and added back over the view area with (SRC_ALPHA, ONE) at 0.33 / 0.66 / 1.0.
- Subtitles (`scLoadSubtitle` `0x418080`, player `0x4180b0` called from the per-frame update
  `0x412010`, so also during cutscenes): a counter of whole seconds since loading
  (`0x48bc58`, +1 when ≥ 1000 ms passed). When counter ≥ cue end: text cleared, next cue read
  (`fscanf %d`, stop at EOF or index 13 — the empty last cue of `sdc-intro.srt`; times
  HH:MM:SS,mmm → tenths with an hour factor of 12000, and an end time's field factor stops
  stepping down once the counter passes 100); its text lines joined with spaces are shown on
  the first tick with counter ≥ start.
- Dialogue (`scScriptGo` `0x4156a0`): music slots 0–7 turned down 0.25 for the whole script;
  per line, unless it was added with flag 1 and SC_HINTS (`0x4df8d38`) is off: voice on channel
  13 + text via `0x40e150`; next line when the voice ends; text cleared at the end. The one
  flagged line in the DC (`dlg-01218`) is a hint ("...stop by the gates...").
- Choices (`scChoiceGo` `0x415850`, draw `0x40b250` with EAX=0, hit test `0x417c90` state
  0xfa7): rows = options + 1 (an empty bottom row), option 0 on top; y = H − offset − 16·row
  (offset 8, or −8 with 4 rows), x = 16, colour (0.1, 0.75, 0.75), hovered row redrawn
  (0.1, 1, 1). Hit rows span the full width, 8 px above the text; with 3 options the hit
  numbering shifts by one. 16 options max (`0x403e30`). The centred variant (EAX=1, top row
  white) is the menu's prompt list (`0x412fa0`).
- View transitions: before every `scSwitch` kind (`0x4158d0`; ZOOMOUT only when a zoom is
  open), journal open/close (`0x418580`/`0x4185f0`), the menu (`0x412cb0`), a click leaving an
  overlay (`0x414501`) and the end of a cutscene (`0x416d13`), the old screen is rendered
  (flag `0x48bc0c`) and copied to a texture (`0x407ad0`, flag `0x48badc`, progress `0x48baf0`
  = 0). Each screen's renderer then draws the capture over the view area (between the bars)
  at alpha 1 − progress and adds SC_MOV_SPEED (`0x4df8d10`, key 1018, default 25, LO/MED/HI →
  25/50/75)·0.001 per frame (×60/fps). `0x407b20` starts at 0.75 (hotkey handler). The intros
  set SC_MOV_SPEED 2 for ~8 s fades. Whether the inventory crossfades is not known (no capture
  found for it).

### Effects (from `scream.exe`, implemented in `src/effects.cpp`)

- `scEnable`/`scDisable` keys → flags (`0x40fa60`): SKY `0x4df8de8`, NIGHT `…dec`, RAIN `…df4`,
  FOG `…df8`, GLOW `…df0` (never read), DREAM `…dfc`, CANDLE `…e00`, LAMP `…e04`, WAIT `…de4`;
  HINTS / SHOW_HOTSPOTS / SURROUND share their setting's variable.
- Each face's `<texture>_msk.tga` (512² grey) is its alpha: black areas show a backdrop. With
  SC_SKY a dome tinted by the SKY_COLOR preset plus a horizon glow through `gradient.tga`, and
  two cloud layers (`clouds1/2.tga`, or `sky_def.tga` in the 2006 data) tinted by the
  CLOUD_COLOR preset, crossfading (`0x40d990`) and scrolling by SC_SKY_SPEED/10. Else SC_FOG:
  dark dome + clouds1 at 50%. Else SC_RAIN: `rain_def.tga` streaks, tiled 2×, fast scroll.
- Presets (sky / horizon / clouds): DAWN (1,.8,.55)/(.8,.05,.05)/(1,.7,.55); MORNING
  (.9,.9,.9)/(.55,.65,.8)/(.75,.75,.75); DUSK (.45,.5,.6)/(1,0,0)/(.95,.55,.45); STORM
  (.7,.7,.9)/(.15,.15,.8)/(.3,.3,.3). Changes fade at 0.0001/frame unless the third
  scSetAmbience argument is 1. SCREEN_COLOR is stored but its use hasn't been found.
- Lighting (`0x409b50`): NIGHT draws the scene again tinted (.1,.1,.5) at 50%. RAIN without a
  candle: storm dimming plus lightning on a 30.5 s schedule (flashes at 10, 10.15, 15, 20, 20.2,
  30 s; strengths 0.25/0.5/0.75 additive + white wash). CANDLE/LAMP: warm (1,.75,.5) additive
  flicker on a 2.5 s schedule; the lamp shifts it 0.25 s and lowers the alpha.
- Field of view is 45° vertical (reset every frame, `0x407c30`); SC_DREAM drifts it between
  35° and 55° at 0.005°/frame. (SC_DEF_FOV 53 in scream.sc is not what the renderer uses.)

## Porting strategy

Embed Lua 5.1 and run the original `scream.sc` + `scratches.sc` unmodified; reimplement the
`sc*` API natively (cube-map renderer, hotspots via masks, audio, video, inventory, menus,
saves). Ghidra on `scream.exe` covers semantics the scripts don't reveal.

## Open questions

- Original save game format (only matters if we want to load original saves).
- Exactly when the two zoom sounds play (`0x416120`, `0x4160d0`).
- Playlist per-track repeat count and fade-time fields.
- Whether `scTimerCreate` timers are cancelled on leaving the node (currently assumed).
- Loading the 2006 release: its scripts are Lua 5.0 bytecode, which Lua 5.1 cannot load.
- What the 2006 patch changed.

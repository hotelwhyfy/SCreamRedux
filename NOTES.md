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
- Each kind has its own slot (`0x49dd0c[kind]`; `0x402b40` only range-checks): binding a room,
  node or zoom, by `scBind` or `scSwitch`, leaves the other slots alone. `grabTooth` relies on
  it (`changeTime`, which binds nodes and the room, runs before its `scDisable(SC_OBJECT)`), and
  so does `putLetter` (`changeTime` ends with `scBind(SC_ROOM, aux)`, then
  `scBind(SC_OBJECT, "inside_mailbox")` must still search the mailbox zoom).
- A failed `scBind` binds index 0 (`0x4148a0` = find + bind of the result); using that slot is
  fatal in scream.exe (`0x4036c0` "GetBind: target not bound" → `0x402340`: dump, message box,
  exit). We clear the slot and log instead. The DC script has one such call: `enterStoreroom` in
  phase 5 binds `dolhom_mask` (node n2) right after entering the room, so it only fails if the
  storeroom is entered at n1.
- Rooms remember the last node: switching to a node stores it in the room (`0x415bd2`,
  `room+0xdd8`), and entering the room returns there. `scSetLastVisited` overrides it.
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
  which re-enables the small key in the pond (each grab is another hour on the clock); digging
  the grave again re-enables the tooth the same way (`dig`, `grabTooth`).

### Media

- Audio: Ogg Vorbis, 44.1 kHz (a few 48 kHz), mono/stereo; decoded with stb_vorbis into one
  SDL3 audio stream per voice. Videos (`vid-*`, `intro-*`, `outro-*`) are silent Theora 4:2:0 at
  24 fps (640×352, 800×472 or 1024×640); their sound is separate `scFXPlay` calls.
- Fonts: 512×512 RGBA atlases, 16×16 grid of 32 px cells in Windows-1252 order, no width table.
  The game draws all its text monospace (see "Text" below); our proportional variant
  (widths measured from alpha) is only for this port's inventory label.
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
  enters wait mode for the duration, then sets motion back to 1 unconditionally. While it
  waits it peeks the message queue (`0x419b80`, PM_REMOVE): Esc ends the sleep and stops every
  playing channel (how the logo sequences are skipped); other input is dropped. The same
  Esc check ends a voice played with wait (`scFXPlayEx`, `0x416120` → `0x416160`; 2006 P
  `0x413960`) and the current dialogue line of `scScriptGo` (`0x415778`, the next line follows);
  the 2006 scSleep has none.
- Camera (`0x407e20`, `0x40df60`): yaw in radians; pitch is the look vector's y, clamped to
  ±1.75 (~60.3°). Edge zones are 15% of the width; vertically 15% of (H−192) from the top bar
  and from H−128. Rate per frame = depth_px × 0.005 × CAM_SPEED/1000 × ramp × 60/fps; ramp
  +0.025/frame, reset when the view stops. CAM_SPEED is 25/50/75. Keyboard steps 0.7855 rad.
  `SC_FPS_STYLE` ("alternate control", `0x4df8e10`; the DC manual's "fixed mouse movement")
  selects a cursor-offset look mode (ported: `Engine::fpsCamera`; on macOS the system pointer
  is captured in relative mode and our own pointer springs back, because moving the system
  pointer back every frame makes quick mouse movements trigger "shake to locate"): zones
  shrink to W/2±1, H/2±1 (`0x40df60`); per frame yaw ∓= (W/2∓1 − x)·0.0003,
  pitch ±= (H/2∓1 − y)·0.0003 (clamped by checking pitch ≤ 1.75 / ≥ −2.0 first), with no
  CAM_SPEED, ramp or frame-time factor (`0x407e20`); every >30 ms the main loop moves the
  pointer 1/10 of the way (≥ 1 px) back to the centre with SetCursorPos (`0x416a8c`) and the
  cursor sprite is drawn at the centre (`0x416b91`); leaving the menu, journal or an overlay
  centres the pointer. The defaults routine (`0x40f260`, called at startup by `0x4061d0`
  before the cfg is read) sets it to 1 in both DC executables, so the DC starts in this mode;
  the 2006 executables don't have it. Only the setup menu changes it and nothing saves it
  (we keep the player's choice in our settings file). The camera and recentring run in node
  state only (`0x416a8c`, states 4000/4006 of the main loop's table at `0x4169d0`). Slideshow mode (`0x4df8ddc`, default 0) makes edge zones
  turn in steps on click (`0x407d50`) instead of the continuous pan.
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
- Music and fades (`0x4161f0`, `0x401310`): a track plays on channels 1–3 (front, plus the
  -RL/-RR files when SURROUND is on; with a stereo device all three just mix). scTrackPlay
  always hard-stops channels 1–3 and restarts (volume SC_VOLUME/10, looping per scTrackSet's
  second argument). Fades are **rates**: every audio-thread tick the gain moves 0.0025/F
  toward its target (F = 0: no fade); the tick is a `Sleep(1)` loop, and how long Sleep(1)
  lasts is Windows' timer resolution at run time: 15.6 ms by default, ~1 ms whenever any
  program asks for it (`timeBeginPeriod`). scream.exe doesn't ask; the shipped `wrap_oal.dll`
  asks for 25 ms when it opens its software device (`alcOpenDevice`, `0x100315a3`), which
  changes nothing. So fade lengths varied between PCs. The design treats F as seconds from
  full volume (the playlist cuts a fading bank after F seconds; the scripts' `scTrackFadeOut(1)
  scSleep(1) scTrackStop()`): a 2.5 ms tick, used here.
  scTrackFadeOut(n) uses F = 1 when fewer than n whole seconds remain, never stops the track;
  a negative n reads the last queued playlist track's SC_FADEOUT (as compiled). scFXSet
  fades are the same rates; a fade-in starts from silence. scTrackPlay/Stop/FadeOut are
  ignored while the playlist plays.
- `scPlaylist*` (`0x4165e0`–`0x4168f0`, driven from `0x40ee00`): up to 38 tracks, 1-based
  index, banks of channels 1–3 / 4–6. A track's loop field is its number of plays (0 = for
  ever); SC_FADEOUT seconds before the end of its last play the next starts on the other bank
  and the old one fades at that rate and is cut at the next one-second tick; SC_FADEIN applies
  only on the first play; the volume drops 0.25 during dialogue. Next/Previous/First/Last
  start the new track without fading the old one (they overlap). scPlaylistStop(n): fade at
  rate n (1 if less than n s left), cut after n s; nil/0 lets the track play out.
- Timers (`0x412220` local, `0x412330` global, ticked by `0x412010`): entries of 0x44 bytes
  (id, seconds, elapsed, active, function name, callback); local ones live in the **bound
  room**, global ones in the world, at most 31 each. Re-creating an id re-arms that timer,
  except the newest one (the search scans 0..count−1 of a 1-based table), which gets a
  duplicate. Once a second: global timers count only in node state (4000), the current room's
  in any state; a timer fires on its Nth tick and stays inactive. Nothing cancels them: a
  room's timers pause while the player is elsewhere. Saved games keep them all.
- No-ops in the original: `scAttach` (calls an empty function), `scMusicPlay/Stop/FadeIn/FadeOut`.
- `scAbort` (`0x4106d0`): stop playlist, wait for a key/click, return to the main menu.
  `scMenuShow` calls that same menu routine (`0x412cb0`); `scJournalShow` → `0x418580`.
- `scLoadSettings(file)` = the scream.cfg parser (`0x40f4a0`, run at startup by `0x4061d0`
  after the defaults `0x40f260`): `fscanf("%s")` over the whole file, so there is no line,
  comment or section syntax — a word equal to a key (case-sensitive) takes the next word; a
  failed `%d` keeps the previous number and leaves the word to be read as a key. Keys
  (default): SCRIPT ("main"; runs SCRIPT.sc, else .lua), RESOURCE ("game.res" = SC_RES_FILE),
  PATH (".\" + "\" = SC_RES_PATH), WIDTH/HEIGHT (1024/768: the mode and every pixel layout),
  DEPTH (32), QUALITY (3; only 0 matters: GL_NEAREST everywhere), FULLSCREEN (1), SURROUND (1;
  the shipped files say 0), LOG (0: scream.log), VSYNC (1: wglSwapIntervalEXT(1); 0 calls
  nothing), RATE (0), AUDIO_DEVICE (0; used only with DEBUG), BUFFER_SIZE (32768),
  NUM_BUFFERS (4), DEBUG (0: audio device listing only), LANGUAGE ("en"; read only by scGet
  and the hidden menu row). The 2006 exes lack DEBUG; the retail one LANGUAGE too.
- scLookAt (`0x412670`): instant from a table (`0x41263c`): front (3.14, 0), back (0, 0), left
  (1.57, 0), right (4.71, 0), up (3.14, y 6.28), down (3.14, y −1.57). Smooth (table
  `0x412654`; back = 0 or 6.28, whichever is nearer; down = front, level, as compiled): with
  the lock flag set and the wait cursor, each frame moves each angle by (1 + remaining)·0.01·
  60/fps until a step covers the rest. Two cases where that loop would never end (a remaining
  angle below −1, and a pitch already exactly on target) are made to finish.

### Inventory, journal, menu (from the scripts and `scream.exe`)

- Controls come from the tutorial hints (`msg01110`–`msg01117`): right-click opens/closes the
  inventory, left-click grabs an item, a held item is used with a left-click on an object
  (`scReaction`) or on another item (the target's `scInventoryReaction`, keyed by the held item).
- `scInventorySet(icon, label, failText, failVoice)`: the last two play when a held item is
  used where it has no reaction ("That doesn't work.", the magnifying glass's "Not much to see
  here...").
- Inventory draw (`0x409680`) replaces the scene: the frame captured on opening, tinted
  (0.9, 0.75, 0.5) at alpha 1 − p, the icons (`SC_INV_SIZE`, rows of 10) at alpha 2p, with p
  rising by SC_MOV_SPEED·0.001 per frame to 0.5 and staying there: a half-dark sepia still.
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
  shows no text and doesn't dim or label the icon under the pointer. Opening and closing crossfade like a view
  switch (capture `0x407ad0` at `0x4174e7` / `0x417512`, then state 0xfa3 / 0xfa6); it doesn't
  open in a dream (SC_DREAM, `0x4df8dfc`) or with nothing in slot 1 (`0x403d20(1)`).
- Cursor (`0x40b400`, set by `0x40b6f0`: 0xc9–0xcb set the mode `0x48bad8`, other values the
  image `0x48bad4`; ≥ 0x41 is an item icon, a 64 px quad). Mode 0xcb draws the image twice
  more added (SRC_ALPHA, ONE), the second a pixel larger all round: the held item brightens.
  That happens with an item held over a hotspot whose click is SC_FEEDBACK, SC_CUSTOM or
  SC_FEEDBACK_EX (`0x414582`, node and zoom alike, whether or not the item has a reaction
  there; not over navigation), and in the inventory over another item (`0x4140fe`).
- Menu (ported in `src/menu.cpp`; renderer `0x408550`, hit test `0x417c90`, click/hover
  `0x413640`, prompts `0x412fa0`/`0x40b250`/`0x413e70`, open `0x412cb0`, close `0x4130d0`).
  Modes at menu+0x260: 0 main, 1 load, 2 save, 4 setup; the renderer also takes −1 (main, no
  game: no "back") and 3 (load with row 0 = scream.011, the quicksave). Images (scMenuSet,
  texture entries 0x26–0x2c) live in square textures (logo 512, the rest 128) and quads stretch
  the whole texture. Logo in every mode, top at H/2 − 420. Main: back/new/set/load/save/quit
  stacked at y = H/2 + 72 + 36·k, 112×120 quads (112×60 images), hit x = W/2 ± 64, y = H − 288
  + 36·k … +36; load greyed (0.5) without saves, save greyed without a game. Hover: the image
  or text again in (0, 1, 1, 0.5) with (SRC_ALPHA, ONE). Other modes: title image (load/save/
  set) 96×48 at y = H/2 + 28, rows of text (current scFont, white) at y = H − 276 + 16·i
  (files 1–10 as "N. description", nothing for empty ones; setup items x = W/2 − 145, label 21
  + value 8 chars), hit rows x = W/2 ± 256, 6 px lower than the text; back below (y = H/2 + 262,
  setup H/2 + 252), hit y = H − 105 … H − 70. Loading only takes rows with a save. Prompts:
  lines added bottom-up ("no", "yes", question), centred, cyan, the question white; rows
  H − 32 … H − 16 (yes / Director's Cut) and H − 16 … H (no / The Last Visit), modal. Starting
  a new game during one asks "Start a new game?", then the game choice with no way back.
  Load/save close the menu first ("Game loaded/saved" as feedback); the quicksave row can't be
  saved to. The setup items call the key handler (F1/F2/F9/F10/F12); slideshow mode and
  alternate control toggle their flags and reset the look to angle 3.14, y 0 (`0x407d10`);
  an undrawn 8th row (hit 13) shows "english" when LANGUAGE is "en" and calls a Lua
  `language()` that no script defines (`0x413d5e`).
  Opening: capture, the "black" zoom for scSleep(1), crossfade to the menu, menu music; closing:
  music fades, black for scSleep(2), crossfade. Background (scMenuSetEx(SC_IMAGE), `0x4083c0`):
  the image added (ONE, ONE) on a fixed quad (x 0..1312, GL y 704..−685: an 800×472 image in
  a 1024 texture, so the view area of the 1024×768 mode), the cloud layers (`0x40d990`), then
  the image with alpha. Feedback text shows in the menu, not under a prompt. Esc does nothing.
- Slideshow mode (`0x4df8ddc`): no continuous camera (`0x416a62`); a click in an edge zone
  (`0x407d50`: a vertical zone wins in a corner and needs |y| ≤ 1.25) captures the screen,
  steps the view once and takes the click (`0x4173be`), even under scStill/scWait or with an
  item held: ±0.7855 rad sideways; up/down set y to ±2.0 (0 only from exactly ∓1.75, which
  doesn't happen). The zone shows its arrow cursor instead of hotspots (`0x414448`). With
  alternate control on as well, scream.exe keeps drawing the cursor in the middle while the
  real pointer moves unseen, and the zones shrink to a pixel round the centre, so a click is
  one frame of the alternate-control turn: an unusable pair (fixed here, see below).
- Cursors (`0x40b400`): always a 64-pixel quad, reaching 8 px up-left of the pointer for the
  menu/forward/look cursors, 24 px for normal/use/zoom-out, centred otherwise (wait, arrows,
  items). The menu shows SC_MENU's cursor (`0x412ed9`), the journal "use" over the pages and
  "zoom out" below y 604 (`0x413f80`), the inventory "use" over an item (`0x414070`).
- Saved games are scream.exe's own files (`src/save.cpp`; writer `0x411370`, reader
  `0x4107d0`): `scream.001`–`.010`, `.011` the quicksave; an 80-byte description (the script's
  `save()`, "quicksave" for F5), every numeric global as a `key=value` Lua line, the 700-byte
  settings block `0x4df8b64`, the journal, the font, the look, state/room, timers, then every
  room (current/entry node, zoom stack, local timers), node (faces, masks), zoom, object
  (enabled), item (icon, label, fail text, voice) and inventory slot by 1-based creation index
  (zoom 1 is the engine's "black"), 17 sound channel records and the playlist block. The
  reader runs the Lua lines, restores the whole settings block (gamma, speeds, flags,
  ambience…), switches to the room (its onEnter runs) and back into the zoom stack. The 2006
  layout (P `0x40f1f0`) drops the journal, font, ambience, DEBUG/alternate control and item
  voice and has smaller rooms/items/playlist; its pitch is an angle. No exe has a name entry:
  the manual's "enter a name" doesn't describe them. F7 shows the "black" zoom for a second
  while everything fades, then loads. "Game saved/loaded" show even when the file failed.
  Loading here first restores the start-of-game baseline (the original leaves non-saved state
  as it was in memory). A new game restores that baseline.

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
  (`0x410740`). In the menu and its prompts Esc does nothing (`0x417934`).
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
  set SC_MOV_SPEED 2 for ~8 s fades. The inventory crossfades too (see Inventory grid).

### Effects (from `scream.exe`, implemented in `src/effects.cpp`)

- `scEnable`/`scDisable` keys → flags (`0x40fa60`): SKY `0x4df8de8`, NIGHT `…dec`, RAIN `…df4`,
  FOG `…df8`, GLOW `…df0` (never read), DREAM `…dfc`, CANDLE `…e00`, LAMP `…e04`, WAIT `…de4`;
  HINTS / SHOW_HOTSPOTS / SURROUND share their setting's variable.
- Composite (`0x409460` nodes, `0x40a830` zooms): the backdrop is drawn from black, then each
  face's `<texture>_msk.tga` is **added** onto it (SRC_COLOR, ONE: + m²; white where a face
  has no mask), then the panorama is **multiplied** in (DST_COLOR, ZERO): node × (backdrop +
  m²). The photo's own sky stays, darkened by the backdrop (why dusk looks dim). No depth
  test; gluPerspective(fov, W/(H−128), 0.1, 1000); everything centred on the eye.
  SC_SKY: the node cube in the sky colour (white texture), `gradient.tga` on its four sides
  (alpha 0 at the top, the horizon colour at the bottom, t from 1/1280 + 0.251 after a one-off
  slide at launch), the bottom face solid horizon colour, then two cloud layers on the dome.
  Else SC_FOG: a 0.25 grey cube and clouds1 at 50% on the dome, twice per frame (the scroll
  advances on each call). Else SC_RAIN: a white cube and `rain_def.tga` ×2 on two side-on
  hemispheres, (DST_COLOR, 1−SRC_ALPHA) then (ONE, 1−SRC_ALPHA); its masks go on with
  (DST_COLOR, ONE). Lighting passes redraw the whole panorama (no mask).
  Zooms: no backdrop under SC_NIGHT; otherwise the backdrop as seen from the node, the zoom's
  `<image>_msk.tga` (white if none) as a quad over the view — fog (DST_COLOR, ONE), rain
  (SRC_COLOR, DST_COLOR), else (SRC_COLOR, ONE) — then the zoom multiplied in. Fog in a zoom
  has no grey cube and one fog pass.
- Dome (`0x418690(150, 5, 5, 12, 4)`, the classic GenerateDome): rings every 5° from the pole,
  four vertices per step as one triangle strip (5184), u = 12·atan2(x, z)/2π + 0.5,
  v = 4·asin(y)/π + 0.5 with the seam fix (0.9 / 0.8). Clouds and fog: translate (0, −100, 0),
  rotate 270° about x (apex 50 above the eye). Rain: ±270° about y, not moved. Cloud layers
  share the dome's uv, scrolled on v by 5e-5·int(SKY_SPEED/10) per frame; SKY_SPEED defaults
  to 15 (`0x40f370`). Layer 2 is the cloud colour −0.05; they crossfade (0.002/frame, pauses
  0.015) between 0.5 and 1. Rain scrolls 0.035/frame.
- SC_SKY and SC_WAIT start enabled (`0x40f3a2`, `0x40f38e`; same in the 2006 exes): the DC
  main game's opening garden has its sky before the script ever calls scEnable(SC_SKY). It
  starts at 9 am, which `changeTime` sets to DAWN at once (sky, clouds and screen tint), the
  orange opening; 10 am fades clouds and tint to MORNING, 11 am the sky.
- Presets (sky / horizon / clouds): DAWN (1,.8,.55)/(.8,.05,.05)/(1,.7,.55); MORNING
  (.9,.9,.9)/(.55,.65,.8)/(.75,.75,.75); DUSK (.45,.5,.6)/(1,0,0)/(.95,.55,.45); STORM
  (.7,.7,.9)/(.15,.15,.8)/(.3,.3,.3). Changes fade at 0.0001/frame (the cloud colour twice, once
  per layer), only on frames that draw the sky, unless the third scSetAmbience argument is 1.
- Lighting (`0x409b50`): NIGHT draws the scene again tinted (.1,.1,.5) at 50%. RAIN without a
  candle: storm dimming plus lightning on a 30.5 s schedule (flashes at 10, 10.15, 15, 20, 20.2,
  30 s; strengths 0.25/0.5/0.75 additive + white wash). CANDLE/LAMP: warm (1,.75,.5) additive
  flicker on a 2.5 s schedule; the lamp shifts it 0.25 s and lowers the alpha.
- Field of view is 45° vertical (reset every frame, `0x407c30`); SC_DREAM drifts it between
  35° and 55° at 0.005°/frame. (SC_DEF_FOV 53 in scream.sc is not what the renderer uses.)

## 2006 release

The 2006 executables (retail and patch; P = `extracted/patch/scream.exe`) are the same code a
year older. Where they behave differently the port follows them when the 2006 game runs
(`Engine::release2006`):
- 800×600 (its cfg and defaults): every pixel layout at that scale.
- Projection gluPerspective(53, W/H) into the W×(H−128) view, faces on a 30×40×30 box
  (RATIOX/RATIOY; hotspots scaled ×1.33 to match), SC_DREAM 43–63° (P `0x40c370`, `0x407840`).
- Pitch is an angle: look = (sin a, sin p, −cos a), clamped ±1.57 (about 45°); keys ±1.25 rad;
  scLookAt up (6.28) therefore looks level and down 45°; smooth scLookAt turns 0.015 rad/frame
  per axis (P `0x4103d0`); slideshow steps 0 ↔ ±1.57.
- Text wraps at 70/140/210 characters and ignores 256+ (P `0x40c500`); "test speed FAST".
- Inventory 8 icons per row, hit rows from y 96, icons drawn 8 px higher (P `0x408fa0`).
- Menu: one game (new game starts it, asking first during a game), 6 setup rows (no alternate
  control), the background at its own size, centred (P `0x407ed0`).
- Sky: a white cube under one cloud layer, untinted, scrolling 0.00015/frame; the clouds are
  generated at startup (P `0x40be00`: Hugo Elias value noise, 32×32 smoothed map seeded by
  Watcom `rand() % 5000` — made three times, the last kept — four octaves over 256², cover
  205, sharpness 0.996; `sky_def.tga` is unused and different). Dome (160, 5, 5, 16, 8). Fog: a
  grey cube and, twice, the clouds tiled ×0.25 on two side-on hemispheres drifting apart
  sideways at 0.0001/call, (ONE, 1−SRC_ALPHA). Rain scrolls 0.045/frame. No ambience presets.
- scSleep keeps the camera free and has no Esc skip; scGet returns nothing for other keys.

- Its scripts are Lua 5.0 bytecode; `src/lua50.cpp` translates each chunk to 5.1 at load
  (layout, instruction fields, RK encoding, opcode numbers; 5.0 TEST = 5.1 TESTSET). The
  shipped scripts use 14 opcodes (no loops, table constructors or varargs) and constants
  up to RK index 106, and call only `string.format` from the libraries, so 5.0/5.1 library
  differences don't arise.
- Crawl: 2352 tasks, clean apart from the unreachable duplicate hall phone zoom (the DC's two
  script bugs aren't in the 2006 script). Explorer: plays to `finale()` (~37k actions) with
  the same hints as the DC (same handlers, combination and phone numbers).

## How the scripts were written (for the editor)

What the shipped scripts say about the developers' own workflow (the basis of `scream-edit`):

- No editor traces in `scream.exe`: its only debug strings are Lua's. The scripts' debug info
  names one source file each, `scratches-en.lua` (2006) and `scratches.lua` (DC): a single
  hand-kept file per language, all text as globals at the top.
- 98.9% of the `scPlace` numbers (9,192 DC, 8,132 2006) are multiples of 5: rectangles read
  off an image editor and rounded, not dragged in a tool. `SC_SHOW_HOTSPOTS` was the check.
- The DC bytecode's line info (`luac -l`) shows a template kept by hand. Within the builders
  (the `create*` functions the boot code runs), there is a blank line before every `scCreate`
  (2,863 of 2,876; the few longer gaps are comments), none after an item's `scInventorySet` /
  `scInventoryReaction` (51 of 55), and `scOnHover(...) scOnClick(...)` share one line (all 2,297).
  The six `scTexture` calls are one block followed by a blank line. `scEnd` follows directly,
  but a blank line separates two `scEnd`s (44 of 57). Functions are 2 or 3 lines apart.
  Handlers have irregular gaps (comments).
- The builders are pure straight-line calls whose arguments are only SC_ constants, numbers,
  strings, globals and a few `globalMusicVolume - n`. So the world is declared in what amounts
  to a data format, and everything else is code. `scream-edit` reads that format back:
  `--roundtrip` regenerates every builder (DC 60, 2006 40). The DC and 2006 worlds built
  from the shipped bytecode, the unluac source and the regenerated source are identical
  (`scream --dump-world`). The regenerated layout matches the shipped line info in 99.70% of
  the 14,461 gaps between calls (29 of 55 builders identical; the rest differ by comments and
  discretionary blank lines).
- Both executables contain Lua's parser (its error strings, "unexpected symbol",
  "chunk has too many syntax levels"), and the DC exe falls back to `SCRIPT.lua`. A new game's plain-source
  `scream.sc` and `game.lua` should therefore load in the original too. Not tried: the exe hasn't
  been run here. The loader call for `scream.sc` itself wasn't traced.

## Porting strategy

Embed Lua 5.1 and run the original `scream.sc` + `scratches.sc` unmodified; reimplement the
`sc*` API natively (cube-map renderer, hotspots via masks, audio, video, inventory, menus,
saves). Ghidra on `scream.exe` covers semantics the scripts don't reveal.

## Open questions

- Exactly when the two zoom sounds play (`0x416120`, `0x4160d0`).
- Fade lengths depended on the player's Windows timer resolution (see Music and fades);
  2.5 ms (F = seconds) is the design's intent, not a measured value.
- The net effect of the reader's ZOOM + ZOOMOUT after loading a game saved in a zoom (the
  zoom stack is restored as saved).
- Saved games are untested against files from the original executables (none available).
- The 2006 cloud texture assumes Watcom's `rand()` is unseeded and uncalled before the
  generator (only caller found: P `0x415da3`); a different seed changes the pattern, not
  its character. `sky_def.tga` can't serve as a reference (it is a different texture).

## Fixes of the original's problems

- Slideshow mode with alternate control: slideshow mode wins (`Engine::fpsStyle`); the cursor
  follows the mouse and edge clicks step 45°. In scream.exe the pair is unusable (see
  Slideshow mode). The setup menu still shows and saves both switches as they are.
- Two loops in smooth scLookAt that never end in scream.exe are made to finish.
- Zoom lookup (`Engine::switchZoom`): the current room's own zoom of a name comes before the
  first one created anywhere (scream.exe `0x403fd0` takes the first). Both releases create a
  zoom "phone" in house-lobby and again in house-hall; the hall's copy adds the "clock"
  hotspot (a description line), which scream.exe could therefore never show. The other
  duplicate names are harmless: `lair-bucket` (house-lair and house-lair-alt) is identical,
  `bath1-pic2` is created twice in one room, `phone-pick` has no hotspots.
- `grabRag_dc` (DC, The Last Visit's cellar): `scTextureUpdate(SC_LEFT("hs-cellar_dc_n1l.jpg"),
  0)` calls a number and stops the handler after the rag is taken and node n3's left face is
  updated, so node n2 still shows the rag (`…_n1lr.jpg`). `Engine::fixScripts` replaces the
  handler with the intended one once the scripts are loaded.
- Left as in the original: `descendFireplace_dc` ends by calling `musicEnterCave_dc`, which no
  script defines (an error after everything else has run; the music it was meant to start
  can't be known).

## Deliberate differences from the original

- The window scales the configured mode (WIDTH × HEIGHT) to fit; the original changed the
  display mode in fullscreen and laid out in real pixels in a window. On a window wider than
  the mode's 4:3, panoramas fill the extra width (the original was always 4:3); zooms, the
  menu and the other 2D screens, with the sky behind them, stay 4:3 with black sides.
- Loading a saved game first restores the start-of-game baseline; scream.exe leaves whatever
  the file doesn't cover (non-numeric globals, choice lists) as it was in memory.
- Setup choices (alternate control, hints, speeds, gamma, surround) persist in
  `settings.cfg`; the original forgets them at startup. Otherwise as the original: every
  saved game carries all the setup choices in its settings block, and loading it brings them
  back (that game's gamma, speeds, hints, surround, alternate control, slideshow); slideshow
  mode itself isn't kept between sessions (off at startup, `0x40f465`).
- Saved games and `scream.log` live in the user data folder, not the game folder.
- The setup launcher (`src/setup.cpp`; `--setup`, Option/Alt held at start, macOS
  Settings… ⌘,) edits the game's scream.cfg in place (backup `scream.cfg.orig`); it adds the
  port's own key WIDESCREEN (default 1), which scream.exe's parser skips like any unknown
  word. The original had no launcher; scream.exe contains an uncalled cfg writer (`0x40f8e0`).
  scream.exe reads the cfg once; the port re-reads it when it changes and applies the mode,
  fullscreen, widescreen, vsync, texture filtering and log at once.
- Additions: middle-drag look, F11 screenshot, the captured pointer for alternate control on
  macOS (the original's SetCursorPos recentring triggers "shake to locate"), the macOS
  fullscreen menu bar suppressed, and the test tools (`--check`, `--crawl`, `--explore`, …).
- The 2006 `newGame` calls `changeTime(9)` before entering a room, so `scGet(SC_ROOM)` is the
  empty room slot 0 and `scBind(SC_ROOM, "")` finds nothing; GetCurr doesn't check the slot
  (P `0x403710`, D `0x4037d0`), so this isn't fatal in either exe.

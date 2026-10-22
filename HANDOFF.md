# SCream Redux — status and handoff

Everything needed to pick this project up cold. Read this first, then
[NOTES.md](NOTES.md) (reverse-engineering findings, with addresses in `scream.exe`)
and [README.md](README.md) (build/run for users).

Last updated: 2026-10-22, at commit `3728598` on `main`
(remote: https://github.com/hotelwhyfy/ScratchesRedux.git, `origin`).

## Goal

A modern reimplementation of the SCream engine (Nucleosys) that runs the
**original, unmodified** *Scratches – Director's Cut* scripts and data, ported
faithfully: behaviour comes from the game scripts and from `scream.exe`
disassembly (Ghidra), not guesswork. The user asked that **all 87 `sc*` API
functions** be ported, including those the shipped scripts never call.
C++20, SDL3, OpenGL 3.3 core, Lua 5.1.5 (patched), stb, libogg/libtheora.

## Where things stand

Working and verified (screenshots and scripted runs):

- Boot: original `scream.sc` + `scratches.sc` run unmodified; the world (14k API
  calls) is built, then `scRun()` enters our main loop.
- Intro handler (logo videos) → main menu (original images/music/wording) →
  new game (choose Director's Cut / The Last Visit), setup, load, save, quit.
- Cube-map nodes, original camera model (edge panning, ramp, ±1.75 pitch,
  45° FOV, letterboxed 3D view with 64-unit bars), hotspots, close-ups (zooms),
  zoom stack, room switches, timers, locks, wait/motion flags.
- Theora cutscenes (skippable), Vorbis effects/music/voices/footsteps/playlist,
  waits synchronised to clip length.
- Text: feedback/subtitles, dialogue scripts, choices, in the game's bitmap fonts.
- Inventory (right-click; grab; use on objects/items; failure lines; tutorial
  hints), journal book.
- Saves (own format; diff of Lua globals vs. start-of-game baseline + world
  and engine state) — round-trip tested through the menu.
- Effects: face masks + sky dome/clouds/horizon, fog, rain + lightning, night,
  candle/lamp flicker, dream FOV, ambience presets.
- `--verify-media`: every asset decodes (DC base 3357/3357, DC+update 3405/3405).
- Full main-game intro (139 s) and The Last Visit intro run to their first scene.

API: 87 functions — 83 done, 4 partial (`./build/scream --api-status`):
`scGet` (location + stored settings only), `scLoadSubtitle` (.srt not parsed),
`scOverlay` (2nd/3rd strings unknown; never used by shipped scripts),
`scSetAmbience` (SC_SCREEN_COLOR use not found).

Not done / known gaps:

- **No full playthrough yet** of either campaign — expect script-specific bugs.
- `.srt` subtitles (`sdc-intro.srt`, used by `newGameTLV`).
- "Alternate control" / `SC_FPS_STYLE` mouse-look (original 0x407e20 free mode,
  cursor recentring 0x416a10) — not ported.
- Menu layout is ours (original positions not recovered); setup screen only has
  hints / camera speed / text speed (original also: surround, gamma, quality,
  slideshow, alternate control).
- 2006 release: scripts are Lua 5.0 bytecode → needs a 5.0 loader or a
  5.0→5.1 path. 2006 patch exe differences not examined.
- Approximations: sky dome geometry/tiling and dusk intensity; playlist
  per-track repeat/fade fields; inventory grid vertical position; original
  save format unknown (ours is incompatible).
- `scTimerCreate` timers are cancelled on node change (assumption).

## Agreed next steps (in order)

1. `.srt` subtitle support for `scLoadSubtitle` (small).
2. Playthrough verification, all automated (user agreed to do a manual feel /
   audio check of e.g. day 1 afterwards):
   a. static checks over the decompiled script: every handler / room / node /
      zoom / item / track / dialogue / choice / asset referenced exists;
   b. a crawler that visits every node/zoom, clicks every hotspot, tries items
      on objects, logging Lua errors / missing assets / dead ends;
   c. scripted walkthroughs of both campaigns derived from the script logic,
      driven through `--exec` + debug hooks, with state checks and screenshots.
3. Alternate control (FPS style) mouse look.
4. 2006 release (Lua 5.0 scripts).

## Layout

```
CMakeLists.txt          build (targets: lua51, ogg, theoradec, scream)
src/                    engine (~5.3k lines)
  main.cpp              CLI options, boot (scream.sc then game script), --verify-media, --api-status
  engine.hpp/.cpp       Engine: options, location, camera, input, main loop (scRun), render, cursor
  api.cpp               all 87 sc* functions + status table + debug Lua helpers
  world.hpp/.cpp        Room/View(node|zoom)/Object/Item/Dialogue/Choice/Track/Overlay, scCreate/scBind/scBegin/scEnd
  script.hpp/.cpp       Lua host; handlers run as coroutines; wait(predicate) for blocking calls
  resources.*           .res archive reader; layered mounts (scream.res, scratches.res, update.res on top)
  renderer.*            GL: cube faces (+masks, blend passes), sky dome, 2D canvas (800x600 letterboxed)
  gl.*                  minimal GL loader via SDL_GL_GetProcAddress
  images.* text.*       stb_image decode / PNG write; bitmap fonts (16x16 grid, widths from alpha)
  audio.* sound.*       stb_vorbis voices on SDL3 streams; game sound model (channels, tracks, playlist)
  video.*               Theora decoder (libtheora) → RGBA
  ui.cpp                inventory + journal screens
  menu.cpp              main menu, setup, load/save slots, prefs
  save.cpp              save/load/new-game state (baseline diff)
  effects.cpp           sky/fog/rain/night/candle/lamp/dream/ambience
  constants.hpp         SC_* values (must match scream.sc)
tools/
  scream_res.py         list/extract .res archives
  api_usage.py          catalogue sc* usage in decompiled scripts → docs/api-usage-*.md
  ghidra/*.java         headless Ghidra scripts (see below); sc_names.txt = the 87 names
  vendor/unluac.jar     Lua decompiler (gitignored)
third_party/            lua-5.1.5 (lundump.c patched, see lundump-size_t.patch), stb,
                        libogg-1.3.6 (hand-written config_types.h), libtheora-1.2.0; each has SOURCE.txt
docs/api-usage-*.md     generated API usage tables
extracted/ (gitignored) unpacked game data, decompiled scripts, Ghidra dumps
```

## Data (gitignored, user's own copy)

ISOs + `scratches-patch.exe`, `scratches-dc-patch.exe`, `scratches-dc-update.exe`
in the repo root. Unpacked (with `unar`, `bsdtar`) into `extracted/`:

- `extracted/dc/install` — DC disc 1: `scream.sc`, `scratches.sc` (May 22), `scratches.res`, `scream.res`, …
- `extracted/dc/update` — DC disc 2 update: `scratches.sc` (Jun 4, latest), `update.res` (HD remaster overlay), `scream.exe` (May 28; the one analysed)
- `extracted/dc/patch` — DC patch scripts (`scratches_post.sc` = update's; `_pre` = disc 1 minus US logo video)
- `extracted/2006/{disc,install}`, `extracted/patch` — 2006 release and its exe patch
- `extracted/decompiled/{dc,2006}/{scratches,scream}.lua` — unluac output (DC script = update's)
- `extracted/ghidra/` — `dc/<name>.c` decompiled sc* functions + `index.txt` (addresses), topic dumps

Run the DC as: `./build/scream --data extracted/dc/install --data extracted/dc/update`
(later `--data` dirs win). Saves/prefs: `~/Library/Application Support/SCreamRedux/Scratches/`.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j8
```
Needs SDL3 (Homebrew 3.4.14 installed). `compile_commands.json` symlinked for clangd
(clangd diagnostics in the editor are often stale — trust the compiler).

## Testing hooks (how everything above was verified)

- `--start ROOM[:NODE]` jump to a location (marks a game running; skips menu/intro)
- `--game sdc|tlv` start a game directly (skips intro+menu)
- `--exec 'LUA'` runs Lua as a handler once startup is idle. Debug globals:
  `debugClick(name)` (object in current view; uses held item if any),
  `debugMouse(x, y)` (800x600 canvas units), `debugPress(1|2)` (left/right click).
  The exec coroutine is named `__exec`: it doesn't count as "busy" for input and
  survives loads. It freezes while the menu is open (game clock paused).
- `--screenshot FILE` (after the scene settles) / `--screenshot-at SECONDS`
- `--hotspots` overlay, `--trace` logs every sc* call, `--verify-media`, `--api-status`
- Useful coordinates: inventory slot i at `x = (32 + (i%10)*96) * 0.78125`, `y = 104 + (i/10)*75`
  (icon 50); menu buttons row y 470–534, 128 wide, centred (4 buttons without a game, 6 with);
  menu text lines from y 250, line height ≈ 26.
- Real-time runs: wrap with `perl -e 'alarm N; exec @ARGV' …` (macOS has no `timeout`).
  Downscale screenshots with `sips -Z 700 in.png --out out.png` before viewing.

## Ghidra workflow

```
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
P=/private/tmp/…/scratchpad/ghidra   # project dir; re-create with -import if missing:
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless $P scream_dc -import extracted/dc/update/scream.exe \
   -overwrite -scriptPath tools/ghidra -postScript ExportScApi.java $(pwd)/extracted/ghidra/dc $(pwd)/tools/ghidra/sc_names.txt
# afterwards, on the existing project:
… analyzeHeadless $P scream_dc -process scream.exe -noanalysis -scriptPath tools/ghidra \
   -postScript DumpFunctions.java OUT DEPTH NAME_OR_HEXADDR…   # decompile + disasm + strings (+callees)
   -postScript XRefs.java OUT HEXADDR…                          # who reads/writes an address
   -postScript FindInsn.java OUT REGEX…                         # instruction text search (addresses print as 0x0048bad4)
```
Notes: Open Watcom register calling convention (args EAX, EDX, EBX, ECX) — the
decompiler hides arguments, read disassembly alongside. Key helpers:
`0x43d650` lua_tostring, `0x43d5a0` lua_tonumber, `0x4036c0` GetBind(kind),
`0x404d80` call-Lua-function-by-name, `0x4158d0` scSwitch, `0x416a10` main loop.
Float constants are read straight from the PE with a small Python snippet
(map VA → file offset via the section table; image base 0x400000).
unluac: `/opt/homebrew/opt/openjdk/bin/java -jar tools/vendor/unluac.jar file.sc`.

## Gotchas learned

- Lua bytecode is 32-bit (size_t=4); our `lundump.c` patch reads either width.
- Scripts pass `nil` for undefined names (`SC_HITNS`, `SC_STEREO_PATCH`) — API is lenient.
- Zoom hotspots are in a logical 800×472 space regardless of image size (update ships 1024×640);
  node hotspots in 1024 face units regardless of 1024/1280 textures.
- `"black"` zoom and `black.jpg` are engine built-ins.
- macOS `grep` treats save files (cp1252 text) as binary and prints nothing — use `awk`/`wc`.
- C++: a nested struct with member initialisers can't be a default argument in its
  enclosing class → define such structs at namespace scope (RenderPass, TextColor).
- `uint8_t` vs `char` signedness when matching byte signatures (`0x80` in "\x80theora").
- SDL audio streams need `SDL_FlushAudioStream` at end of data, or the resampler's
  last frames never drain and "finished" never fires.
- `Resources::read` etc. are relative to `--data` dirs; run from the repo root.

## Conventions

- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`;
  push to `origin main` when the user asks to commit.
- Never commit game data or derived game content (`.gitignore` covers ISOs, exes,
  `extracted/`, root screenshots).
- Record every binary finding in NOTES.md with its address; mark guesses as such
  (in code comments and the `--api-status` notes).

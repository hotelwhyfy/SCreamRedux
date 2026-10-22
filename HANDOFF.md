# SCream Redux — status and handoff

Everything needed to pick this project up cold. Read this first, then
[NOTES.md](NOTES.md) (reverse-engineering findings, with addresses in `scream.exe`)
and [README.md](README.md) (build/run for users).

Last updated: 2026-10-22 (remote: https://github.com/hotelwhyfy/ScratchesRedux.git, `origin`, branch `main`).

## Goal

A modern reimplementation of the SCream engine (Nucleosys) that runs the
**original, unmodified** *Scratches* scripts and data — the Director's Cut (2007)
and the original 2006 release — ported faithfully: behaviour comes from the game
scripts and from `scream.exe` disassembly (Ghidra), not guesswork. The user asked
that **all 87 `sc*` API functions** be ported, including those the shipped scripts
never call. C++20, SDL3, OpenGL 3.3 core, Lua 5.1.5 (patched), stb, libogg/libtheora.

## Where things stand

- **Director's Cut: both campaigns play through to their endings** under the
  automated player (`--explore`): the main game to `finale()` in the lair
  (~38k actions), The Last Visit to its finale (~180 actions), with no engine
  warnings. The crawler (every hotspot, item use, choice, dialogue) is clean apart
  from two bugs in the original script and one zoom that is unreachable by design.
- **2006 release plays through to its ending** too (~37k actions, same hints file);
  its crawl is clean. Its Lua 5.0 scripts are translated to 5.1 bytecode at load
  (`src/lua50.cpp`). It runs on the same engine, which models the DC `scream.exe`;
  differences of the 2006 executable itself have not been examined.
- API: all 87 functions done (`./build/scream --api-status`).
- Everything else that was listed before (boot, menu, nodes/zooms, cutscenes,
  audio, text, subtitles, inventory, journal, saves, effects, keyboard, gamma,
  transitions) works; details per topic are in NOTES.md.

Engine bugs the playthroughs found (all fixed, all in NOTES.md "Binding"): one object
bound at a time; one bind slot per kind (binding a room/node/zoom keeps the object);
a failed `scBind` unbinds; rooms remember the last node; view switches release `scLock`.

## Open items

1. Menu: ported from `0x408550` (layout, prompts, black interludes). Not yet: the
   save-name entry (the DC manual: "you will then be asked to enter a name for the
   saved game"; the save callback `+0x244` set by `0x4135b0` is untraced), the look
   reset when toggling slideshow/alternate control (`0x407d10(3.14, 0)`), the hidden
   language row.
2. Slideshow mode (`0x4df8ddc`): only the switch is ported, not the stepwise camera
   (`0x407d50`, `0x4173be`).
3. 2006 executable: compare its behaviour (and the 2006 patch's) with the DC one
   where it matters (menu without the game choice, no journal/subtitles/ambience
   API in 2006 scripts).
4. Whether the inventory screen crossfades in the original (not checked).
5. Approximations: sky dome geometry/tiling and dusk intensity; playlist per-track
   repeat/fade fields; original save format unknown (ours is incompatible);
   `scTimerCreate` timers are cancelled on node change (assumption).
6. The user's own play-through for feel and audio (automated runs are silent and
   draw almost nothing).

## Layout

```
CMakeLists.txt          build (targets: lua51, ogg, theoradec, scream)
src/
  main.cpp              CLI options, boot (scream.sc then game script), --verify-media, --api-status
  engine.hpp/.cpp       Engine: options, location, camera, input, main loop (scRun), render, cursor, transitions
  api.cpp               all 87 sc* functions + status table + debug Lua helpers
  world.hpp/.cpp        Room/View(node|zoom)/Object/Item/Dialogue/Choice/Track/Overlay, scCreate/scBind/scBegin/scEnd
  script.hpp/.cpp       Lua host; handlers run as coroutines; wait(predicate) for blocking calls; watchdog
  lua50.hpp/.cpp        Lua 5.0 -> 5.1 bytecode translation (2006 scripts)
  resources.*           .res archive reader; layered mounts (scream.res, scratches.res, update.res on top)
  renderer.*            GL: cube faces (+masks, blend passes), sky dome, 2D canvas (800x600 letterboxed)
  gl.* images.* text.*  GL loader; stb_image / PNG write; bitmap fonts (monospace as the game, proportional for our menu)
  audio.* sound.*       stb_vorbis voices on SDL3 streams; game sound model (channels, tracks, playlist)
  video.*               Theora decoder (libtheora) -> RGBA
  ui.cpp menu.cpp       inventory + journal screens; main menu, setup, load/save slots, prefs
  save.cpp              save/load/new-game state (baseline diff)
  effects.cpp           sky/fog/rain/night/candle/lamp/dream/ambience/screen colour
  check.cpp crawl.cpp explore.cpp   test tools (below)
  constants.hpp         SC_* values (must match scream.sc)
tools/
  scream_res.py         list/extract .res archives
  api_usage.py          catalogue sc* usage in decompiled scripts -> docs/api-usage-*.md
  check_script.py       static reference check over a decompiled script
  watch_explore.sh      progress/stall/runaway-clock watcher for an --explore run
  ghidra/*.java         headless Ghidra scripts (below); sc_names.txt = the 87 names
  vendor/unluac.jar     Lua decompiler (gitignored)
third_party/            lua-5.1.5 (lundump.c patched, see lundump-size_t.patch), stb,
                        libogg-1.3.6, libtheora-1.2.0; each has SOURCE.txt
docs/api-usage-*.md     generated API usage tables
extracted/ (gitignored) unpacked game data, decompiled scripts, Ghidra project and dumps
```

## Data (gitignored, user's own copy)

ISOs + `scratches-patch.exe`, `scratches-dc-patch.exe`, `scratches-dc-update.exe`
in the repo root. Unpacked into `extracted/`:

- `extracted/dc/install` — DC disc 1: `scream.sc`, `scratches.sc`, `scratches.res`, `scream.res`, …
- `extracted/dc/update` — DC update: `scratches.sc` (latest), `update.res` (HD overlay), `scream.exe` (the one analysed)
- `extracted/dc/patch` — DC patch scripts
- `extracted/2006/{install,disc}` — 2006 release (`install`: scripts, `scream.res`, exe; `disc`: `scratches.res`); `extracted/patch` — 2006 exe patch
- `extracted/decompiled/{dc,2006}/{scratches,scream}.lua` — unluac output
- `extracted/ghidra-project/` — the Ghidra project (`scream_dc`); `extracted/ghidra/` — dumps
- `extracted/explore-sdc.hints` — clue-puzzle answers for `--explore` (game content: never commit)

Run:
- DC: `./build/scream --data extracted/dc/install --data extracted/dc/update`
- 2006: `./build/scream --data extracted/2006/install --data extracted/2006/disc`

(later `--data` dirs win). Saves/prefs: `~/Library/Application Support/SCreamRedux/Scratches/`.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j8
```
Needs SDL3 (Homebrew 3.4.14). `compile_commands.json` symlinked for clangd
(clangd diagnostics in the editor are often stale — trust the compiler).

## Testing

Always pass `--save-dir DIR` (a temp dir) so tests don't touch the user's saves/prefs.

- `--start ROOM[:NODE]`, `--game sdc|tlv` (2006: `sdc` = its single new game), `--window WxH`
- `--exec 'LUA'` runs Lua as a handler once startup is idle: `debugClick(name)`,
  `debugMouse(x, y)` (800x600 canvas units), `debugPress(1|2)`, `debugKey(vk)`.
- `--screenshot FILE` (after the scene settles) / `--screenshot-at SECONDS`
- `--hotspots`, `--trace`, `--verify-media`, `--api-status`
- `--check`: validates the world the script built (targets, handlers, files).
  `tools/check_script.py SCRIPT.lua DATA_DIR...` does the same over a decompiled script.
- `--turbo`: no audio/video/rendering, game time fast-forwarded.
- `--crawl`: every hotspot click, item reaction, choice and dialogue from the start state;
  reports warnings/Lua errors per task (~2 min).
- `--explore` / `--explore-hints FILE`: an automated player that plays a campaign until
  it ends or gets stuck (explore.cpp header documents the strategy and the hints format).
  Takes 5–15 min for the main game. Watch it:
  `tools/watch_explore.sh LOG 'game sd[c] --explore' 30` (prints progress, stalls,
  a runaway clock and the end; pass a bracketed pattern so pgrep can't match the watcher).
  Expected: DC main game and TLV end with "the game ended (menu shown)".
- Real-time runs: macOS has no `timeout`; use `perl -e 'alarm N; exec @ARGV' …`.
  Kill test processes with `pkill -x scream` (never `pkill -f` with a pattern that
  appears in a watcher's command line).

## Ghidra workflow

```
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
P=$(pwd)/extracted/ghidra-project     # re-create with -import if missing:
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless $P scream_dc -import extracted/dc/update/scream.exe \
   -overwrite -scriptPath tools/ghidra -postScript ExportScApi.java $(pwd)/extracted/ghidra/dc $(pwd)/tools/ghidra/sc_names.txt
# afterwards, on the existing project (one process at a time: the project locks):
… analyzeHeadless $P scream_dc -process scream.exe -noanalysis -scriptPath tools/ghidra \
   -postScript DumpFunctions.java OUT DEPTH NAME_OR_HEXADDR…   # decompile + disasm + strings (+callees)
   -postScript XRefs.java OUT HEXADDR…                          # callers of a function
   -postScript FindInsn.java OUT REGEX…                         # instruction search; addresses print as 0x04df8e1c
```
Open Watcom register calling convention (args EAX, EDX, EBX, ECX) — the decompiler
hides arguments and sometimes garbles stack use; read the disassembly alongside.
Key helpers: `0x43d650` lua_tostring, `0x43d5a0` lua_tonumber, `0x4036c0` GetBind(kind),
`0x402ce0` bind, `0x403fd0` find, `0x404d80` call-Lua-function-by-name, `0x4158d0` scSwitch,
`0x416a10` main loop. Constants are read straight from the PE with a small Python
snippet (VA → file offset via the section table; image base 0x400000).
unluac: `/opt/homebrew/opt/openjdk/bin/java -jar tools/vendor/unluac.jar file.sc`.

## Gotchas learned

- Lua bytecode is 32-bit (size_t=4); our `lundump.c` patch reads either width.
- Scripts pass `nil` for undefined names (`SC_HITNS`, `SC_STEREO_PATCH`) — API is lenient.
- Objects often share a name within a view (one per cube face, or two different
  hotspots): bind/enable act on one object, as the original. Test tools must key
  actions by object, not name.
- Some script quirks are faithful and must stay: the pond key and the grave tooth can be
  taken again (each another hour); relighting the passage candle resets `jerry_st`.
- Zoom hotspots are in a logical 800×472 space regardless of image size; node hotspots
  in 1024 face units regardless of 1024/1280 textures.
- `"black"` zoom and `black.jpg` are engine built-ins.
- The 3D view spans the full window width (wider than the 4:3 canvas on wide windows):
  anything drawn "over the view" must cover that width.
- macOS `grep` treats save files (cp1252 text) as binary — use `awk`/`wc`.
- Monitor commands run under zsh: put watchers in a bash script and test them on an old log.
- SDL audio streams need `SDL_FlushAudioStream` at end of data.
- `Resources::read` etc. are relative to `--data` dirs; run from the repo root.

## Conventions

- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`;
  push to `origin main` after committing.
- Never commit game data or derived game content (`.gitignore` covers ISOs, exes,
  `extracted/`, root screenshots) — including puzzle answers in notes or tests.
- Record every binary finding in NOTES.md with its address; mark guesses as such
  (in code comments and the `--api-status` notes).
- Long test runs: watch them actively and report real progress (compare action counts).

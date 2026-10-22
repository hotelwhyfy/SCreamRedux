# SCream Redux

A reimplementation of the SCream engine used by *Scratches* (2006) and
*Scratches – Director's Cut* (2007). It runs the **original, unmodified game
scripts and data**: the compiled Lua scripts build the world through the
engine's `sc*` API, which this project reimplements in C++20 with SDL3 and
OpenGL.

No game data is included. You need your own copy of the game.

## Status

The Director's Cut is playable from start to finish in principle: intro and
main menu, both campaigns, cube-map scenes with the original camera behaviour
and hotspots, close-ups, Theora cutscenes, Vorbis sound/music/voices,
subtitles, dialogue choices, inventory (with the tutorial hints), journal,
save/load and quicksave, crossfade transitions, the original keyboard shortcuts
and gamma correction, and the sky/fog/rain/night/candle/lightning effects. Text
(feedback, subtitles including the DC intro's .srt, choices, journal) uses the
original's layout and monospaced font. The original 2006 release runs too (its
Lua 5.0 scripts are translated to 5.1 bytecode as they load). Both campaigns of
the Director's Cut and the 2006 game have been played through to their endings
by the automated player (`--explore`). Not yet: the "alternate control"
mouse-look option and the original menu layout.
`scream --api-status` lists the state of all 87 API functions.

See [NOTES.md](NOTES.md) for the reverse-engineering notes.

## Building

Requires CMake 3.24+, a C++20 compiler and SDL3 (`brew install sdl3` on macOS).
Lua 5.1.5, stb, libogg and libtheora are vendored in `third_party/` (see each `SOURCE.txt`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Game data

Unpack the installers (see NOTES.md; `unar` handles the NSIS installers and the
split 7z) so you have directories containing `scream.sc`, `scratches.sc` and
the `.res` archives. For the updated Director's Cut:

```sh
./build/scream --data extracted/dc/install --data extracted/dc/update
```

Later `--data` directories override earlier ones, so the disc 2 update's
`scratches.sc` and `update.res` (HD textures) take precedence. For the 2006
release (scripts and `scream.res` from the installer, `scratches.res` from the disc):

```sh
./build/scream --data extracted/2006/install --data extracted/2006/disc
```

## Running

```
--game sdc|tlv          skip the intro and menu: main game or The Last Visit
--start ROOM[:NODE]     skip the intro and start at a location, e.g. garden:n1
--hotspots              draw clickable areas
--trace                 log every sc* call
--exec LUA              run Lua once startup is idle; debugClick(name) clicks an object
--screenshot FILE       write a PNG once the scene settles, then quit
--verify-media          decode every image, sound and video in the archives
```

Controls (as the original): move the mouse to the screen edges to look around,
left-click to interact, right-click to open/close the inventory (left-click an
item to pick it up, then left-click where to use it). Esc closes the inventory,
journal or overlay, or opens the menu; Esc or Space skips a cutscene. Keys, as
scream.exe maps them: A / Left turn left, D / Up turn right, W / Right look up,
S / Down look down, E / Space look straight ahead; F1 hints, F2 surround music,
F5 quicksave, F7 quickload, F9 camera speed, F10 text speed, F12 gamma.
Extras of this port: middle-drag to look around, F11 for a screenshot.

Saves and setup preferences go to the user data folder
(`~/Library/Application Support/SCreamRedux/Scratches` on macOS) in this
port's own format.

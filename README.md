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
subtitles, dialogue choices, inventory (with the tutorial hints), journal, and
save/load. Not yet: sky/weather effects (fog, rain, night, candle light), the
"alternate control" mouse-look option, and the 2006 release (Lua 5.0 scripts).
`scream --api-status` lists the state of all 87 API functions. A full
play-through hasn't been done yet, so expect script-specific bugs.

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
`scratches.sc` and `update.res` (HD textures) take precedence.

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
item to pick it up, then left-click where to use it). Esc opens the menu or
skips a cutscene. Extras: middle-drag to look around, F12 for a screenshot.

Saves and setup preferences go to the user data folder
(`~/Library/Application Support/SCreamRedux/Scratches` on macOS) in this
port's own format.

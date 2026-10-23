#pragma once

// The setup launcher: a window of its own, before the game starts, that edits
// the game's scream.cfg (the one file the engine reads it from). Not part of
// the original, which had players edit the file by hand; nothing in-game is
// changed by it. Opened with --setup, by holding Option (macOS) / Alt
// (Windows) while the game starts, or from the macOS menu (Settings…), which
// runs it as a separate process next to the game.

#include <filesystem>
#include <string>
#include <vector>

enum class SetupResult { Play, Quit };

// For tests: keys pressed at the start ("down right ... ctrl-s", by SDL key
// name) and a PNG of the screen after them, then the launcher closes.
struct SetupTest {
    std::string keys;
    std::string screenshot;
};

// `standalone`: opened beside a running game (no Play button).
SetupResult runSetup(const std::vector<std::filesystem::path>& dataDirs, bool standalone,
                     const SetupTest* test = nullptr);

// Where scream.cfg is read from: the last data directory that has one (as
// Engine::findData), else the last data directory.
std::filesystem::path configPath(const std::vector<std::filesystem::path>& dataDirs);

#pragma once

// Making a new SCream game: a project folder in the layout the original
// expects (scream.cfg, scream.sc, the game script, a .res archive), with
// placeholder art generated here, since nothing of Nucleosys's may ship.

#include <filesystem>
#include <string>

namespace editor {

// Writes a playable template game into `dir` (which must be empty or new).
bool createProject(const std::filesystem::path& dir, const std::string& title, std::string& error);

// Packs every .jpg, .tga and .ogg in `assets` into a .res archive.
bool packArchive(const std::filesystem::path& assets, const std::filesystem::path& out, std::string& error);

// Cuts an equirectangular panorama into the six faces of a node:
// <base>f.jpg, b, l, r, u, d (the engine's orientation), `size` pixels square.
bool importPanorama(const std::filesystem::path& image, const std::filesystem::path& outDir, const std::string& base,
                    int size, std::string& error);

// scream.sc for a new game: the SC_* constants as the engine defines them.
std::string constantsScript();

}  // namespace editor

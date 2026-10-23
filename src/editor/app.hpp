#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct AppOptions {
    std::vector<std::filesystem::path> dataDirs;
    std::string script;
    // Testing: what to show at start, and a screenshot after a few frames.
    std::string open;        // "room[/view[/object]]"
    std::string center;      // map | code:NAME | asset:NAME
    std::string screenshot;  // write a PNG of the window, then quit
    // Synthetic input, "FRAME ACTION ARGS; ...": move X Y, down [1|3], up [1|3],
    // wheel DY, key NAME (letters, f1-f12, enter, escape; ctrl+ / shift+ prefixes).
    std::string input;
    int quitAt = 20;  // with --screenshot: the frame to take it at
    float yaw = 0;
    int width = 1500, height = 920;
};

// The editor window; returns the process exit status.
int runApp(const AppOptions& opts);

}  // namespace editor

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct AppOptions {
    std::vector<std::filesystem::path> dataDirs;
    std::string script;
};

// The editor window; returns the process exit status.
int runApp(const AppOptions& opts);

}  // namespace editor

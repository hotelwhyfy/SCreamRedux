// Hooks for the setup launcher on platforms other than macOS
// (platform_mac.mm): Alt held at startup on Windows; no application menu.

#include "platform.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#include <climits>
extern char** environ;
#endif

namespace platform {

bool setupKeyHeld() {
#ifdef _WIN32
    return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
#else
    return false;
#endif
}

void installSettingsMenu(std::function<void()>) {}

void spawnSelf(const std::vector<std::string>& args) {
#ifdef _WIN32
    char path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
        return;
    std::string cmd = std::string("\"") + path + "\"";
    for (const auto& a : args) cmd += " \"" + a + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0)
        return;
    path[n] = 0;
    std::vector<char*> argv{path};
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    posix_spawn(&pid, path, nullptr, nullptr, argv.data(), environ);
#endif
}

}  // namespace platform

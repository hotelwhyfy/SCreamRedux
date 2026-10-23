#pragma once

// Small platform hooks for the setup launcher (platform_mac.mm, platform.cpp).

#include <functional>
#include <string>
#include <vector>

namespace platform {

// Option (macOS) or Alt (Windows) is held right now: start with the setup launcher.
bool setupKeyHeld();

// macOS: add "Settings…" (⌘,) to the application menu, calling `open`.
// Elsewhere there is no application menu bar; does nothing.
void installSettingsMenu(std::function<void()> open);

// Start this program again with `args` (the setup launcher beside the game).
void spawnSelf(const std::vector<std::string>& args);

}  // namespace platform

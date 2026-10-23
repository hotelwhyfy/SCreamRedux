// macOS hooks for the setup launcher: the Option key at startup, the
// application menu's "Settings…" item, and starting a second copy.

#include "platform.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <mach-o/dyld.h>
#include <spawn.h>

#include <climits>

extern char** environ;

@interface ScreamSettingsTarget : NSObject
@property(nonatomic, copy) void (^action)(void);
- (void)openSettings:(id)sender;
@end

@implementation ScreamSettingsTarget
- (void)openSettings:(id)sender {
    if (self.action) self.action();
}
@end

namespace platform {

bool setupKeyHeld() {
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlternate) != 0;
}

void installSettingsMenu(std::function<void()> open) {
    static ScreamSettingsTarget* target = nil;  // menu items don't retain their target
    NSMenu* bar = [NSApp mainMenu];
    if (!bar || bar.numberOfItems == 0 || target)
        return;
    NSMenu* appMenu = [[bar itemAtIndex:0] submenu];
    if (!appMenu)
        return;
    target = [ScreamSettingsTarget new];
    auto callback = std::make_shared<std::function<void()>>(std::move(open));
    target.action = ^{ (*callback)(); };
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(openSettings:) keyEquivalent:@","];
    item.target = target;
    // After "About …" and its separator, where macOS apps keep their settings.
    NSInteger at = appMenu.numberOfItems > 0 ? 1 : 0;
    [appMenu insertItem:item atIndex:at];
    [appMenu insertItem:[NSMenuItem separatorItem] atIndex:at + 1];
}

void spawnSelf(const std::vector<std::string>& args) {
    char path[PATH_MAX];
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0)
        return;
    std::vector<char*> argv{path};
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    posix_spawn(&pid, path, nullptr, nullptr, argv.data(), environ);
}

}  // namespace platform

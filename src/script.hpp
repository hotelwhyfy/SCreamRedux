#pragma once

// Lua 5.1 host. Boot scripts run on the main state; every handler (click
// actions, timers, menu callbacks) runs in its own coroutine so that blocking
// API calls like scSleep or scLookAt(..., SC_SMOOTH) can suspend it until the
// engine says the wait is over.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

class ScriptHost {
public:
    using Ready = std::function<bool()>;

    ScriptHost();
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    lua_State* state() const { return L_; }

    // Load and run a chunk (source or bytecode) on the main state.
    bool runFile(const std::filesystem::path& path);

    // Start the global function `name` as a new handler coroutine.
    bool start(const std::string& name);
    // Start a chunk of Lua source as a handler coroutine (debug --exec).
    bool startCode(const std::string& code, const std::string& name);

    // Called from a C API function: suspend the calling coroutine until
    // `ready` returns true. Returns the value the C function must return.
    // On the main state (not yieldable) it returns 0 without waiting.
    int wait(lua_State* L, Ready ready);

    // Resume every coroutine whose wait is over.
    void update();

    bool busy() const { return !threads_.empty(); }
    // Busy with anything other than the named handler (the --exec test driver).
    bool busyExcept(const std::string& name) const;
    bool isMain(lua_State* L) const { return L == L_; }

private:
    struct Thread {
        lua_State* co;
        int ref;
        std::string name;
        Ready ready;
    };

    void resume(Thread& t);

    lua_State* L_;
    std::vector<Thread> threads_;
    Ready pending_;
};

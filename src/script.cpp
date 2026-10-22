#include "script.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <fstream>
#include <iterator>

static int traceback(lua_State* L) {
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

ScriptHost::ScriptHost() : L_(luaL_newstate()) { luaL_openlibs(L_); }

ScriptHost::~ScriptHost() { lua_close(L_); }

bool ScriptHost::runFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot open %s", path.string().c_str());
        return false;
    }
    std::string buf((std::istreambuf_iterator<char>(f)), {});
    std::string chunk = "@" + path.filename().string();

    lua_pushcfunction(L_, traceback);
    if (luaL_loadbuffer(L_, buf.data(), buf.size(), chunk.c_str()) != 0 || lua_pcall(L_, 0, 0, -2) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", lua_tostring(L_, -1));
        lua_pop(L_, 2);
        return false;
    }
    lua_pop(L_, 1);
    return true;
}

bool ScriptHost::start(const std::string& name) {
    lua_getfield(L_, LUA_GLOBALSINDEX, name.c_str());
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "handler %s is not a function", name.c_str());
        return false;
    }
    lua_State* co = lua_newthread(L_);
    lua_insert(L_, -2);
    lua_xmove(L_, co, 1);              // function onto the coroutine
    int ref = luaL_ref(L_, LUA_REGISTRYINDEX);  // keep the thread alive
    threads_.push_back({co, ref, name, nullptr});
    // Resume a copy: a nested start() from inside the handler may grow threads_.
    Thread t = threads_.back();
    resume(t);
    return true;
}

bool ScriptHost::startCode(const std::string& code, const std::string& name) {
    if (luaL_loadbuffer(L_, code.data(), code.size(), ("=" + name).c_str()) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", lua_tostring(L_, -1));
        lua_pop(L_, 1);
        return false;
    }
    lua_setfield(L_, LUA_GLOBALSINDEX, "__exec");
    bool ok = start("__exec");
    lua_pushnil(L_);
    lua_setfield(L_, LUA_GLOBALSINDEX, "__exec");
    return ok;
}

int ScriptHost::wait(lua_State* L, Ready ready) {
    if (isMain(L))
        return 0;
    pending_ = std::move(ready);
    return lua_yield(L, 0);
}

void ScriptHost::resume(Thread& t) {
    pending_ = nullptr;
    int status = lua_resume(t.co, 0);
    auto it = std::find_if(threads_.begin(), threads_.end(), [&](const Thread& x) { return x.co == t.co; });
    if (status == LUA_YIELD) {
        if (it != threads_.end())
            it->ready = std::move(pending_);
        return;
    }
    if (status != 0) {
        lua_State* co = t.co;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "handler %s: %s", t.name.c_str(), lua_tostring(co, -1));
        lua_getfield(co, LUA_GLOBALSINDEX, "debug");
        lua_getfield(co, -1, "traceback");
        lua_pushthread(co);
        lua_pushstring(co, "");
        if (lua_pcall(co, 2, 1, 0) == 0)
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", lua_tostring(co, -1));
    }
    if (it != threads_.end()) {
        luaL_unref(L_, LUA_REGISTRYINDEX, it->ref);
        threads_.erase(it);
    }
}

bool ScriptHost::busyExcept(const std::string& name) const {
    return std::any_of(threads_.begin(), threads_.end(), [&](const Thread& t) { return t.name != name; });
}

void ScriptHost::update() {
    // Snapshot: resuming may start new handlers or finish existing ones.
    std::vector<lua_State*> ready;
    for (auto& t : threads_)
        if (!t.ready || t.ready())
            ready.push_back(t.co);
    for (lua_State* co : ready) {
        auto it = std::find_if(threads_.begin(), threads_.end(), [&](const Thread& x) { return x.co == co; });
        if (it != threads_.end()) {
            Thread t = *it;
            resume(t);
        }
    }
}

#include "gl.hpp"

#include <SDL3/SDL_video.h>

namespace gl {

#define SCREAM_GL_DEFINE(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
SCREAM_GL_FUNCS(SCREAM_GL_DEFINE)
#undef SCREAM_GL_DEFINE

const char* load() {
#define SCREAM_GL_LOAD(ret, name, ...)                                              \
    name = reinterpret_cast<ret (*)(__VA_ARGS__)>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name)                                                                      \
        return "gl" #name;
    SCREAM_GL_FUNCS(SCREAM_GL_LOAD)
#undef SCREAM_GL_LOAD
    return nullptr;
}

}  // namespace gl

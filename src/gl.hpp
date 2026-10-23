#pragma once

// Minimal OpenGL 3.3 core loader: just the entry points the renderer uses,
// fetched through SDL so no platform GL headers are needed.

#include <cstddef>
#include <cstdint>

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum COLOR_BUFFER_BIT = 0x4000, DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum TRIANGLES = 0x0004, TRIANGLE_STRIP = 0x0005;
constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601, LINEAR_MIPMAP_LINEAR = 0x2703, CLAMP_TO_EDGE = 0x812F, REPEAT = 0x2901;
constexpr GLenum RGBA = 0x1908, RGBA8 = 0x8058, UNSIGNED_BYTE = 0x1401, FLOAT = 0x1406;
constexpr GLenum ARRAY_BUFFER = 0x8892, STATIC_DRAW = 0x88E4, DYNAMIC_DRAW = 0x88E8;
constexpr GLenum VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30;
constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr GLenum ZERO = 0, SRC_COLOR = 0x0300;
constexpr GLenum BLEND = 0x0BE2, SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, ONE = 1, DST_COLOR = 0x0306;
constexpr GLenum DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05;

#define SCREAM_GL_FUNCS(X)                                                                         \
    X(void, Clear, GLbitfield)                                                                     \
    X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                        \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                              \
    X(void, Enable, GLenum)                                                                        \
    X(void, Disable, GLenum)                                                                       \
    X(void, BlendFunc, GLenum, GLenum)                                                             \
    X(void, PixelStorei, GLenum, GLint)                                                            \
    X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)                     \
    X(void, GenTextures, GLsizei, GLuint*)                                                         \
    X(void, DeleteTextures, GLsizei, const GLuint*)                                                \
    X(void, BindTexture, GLenum, GLuint)                                                           \
    X(void, ActiveTexture, GLenum)                                                                 \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, TexParameteri, GLenum, GLenum, GLint)                                                  \
    X(void, CopyTexImage2D, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)          \
    X(void, GenerateMipmap, GLenum)                                                                \
    X(GLuint, CreateShader, GLenum)                                                                \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                     \
    X(void, CompileShader, GLuint)                                                                 \
    X(void, GetShaderiv, GLuint, GLenum, GLint*)                                                   \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                  \
    X(void, DeleteShader, GLuint)                                                                  \
    X(GLuint, CreateProgram)                                                                       \
    X(void, AttachShader, GLuint, GLuint)                                                          \
    X(void, LinkProgram, GLuint)                                                                   \
    X(void, GetProgramiv, GLuint, GLenum, GLint*)                                                  \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                 \
    X(void, UseProgram, GLuint)                                                                    \
    X(GLint, GetUniformLocation, GLuint, const GLchar*)                                            \
    X(void, Uniform1i, GLint, GLint)                                                               \
    X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)                                  \
    X(void, Uniform1f, GLint, GLfloat)                                                             \
    X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                           \
    X(void, GenVertexArrays, GLsizei, GLuint*)                                                     \
    X(void, BindVertexArray, GLuint)                                                               \
    X(void, GenBuffers, GLsizei, GLuint*)                                                          \
    X(void, BindBuffer, GLenum, GLuint)                                                            \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                                   \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)           \
    X(void, EnableVertexAttribArray, GLuint)                                                       \
    X(void, DrawArrays, GLenum, GLint, GLsizei)                                                    \
    X(GLenum, GetError)

#define SCREAM_GL_DECLARE(ret, name, ...) extern ret (*name)(__VA_ARGS__);
SCREAM_GL_FUNCS(SCREAM_GL_DECLARE)
#undef SCREAM_GL_DECLARE

// Load all entry points from the current context. Returns the first missing name, or nullptr.
const char* load();

}  // namespace gl

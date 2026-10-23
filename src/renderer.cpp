#include "renderer.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "constants.hpp"
#include "images.hpp"
#include "resources.hpp"
#include "world.hpp"

namespace {

constexpr float kPi = 3.14159265358979f;
float rad(float deg) { return deg * kPi / 180.0f; }

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Column-major 4x4 matrices, as GL expects.
using Mat4 = std::array<float, 16>;

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}

Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    float f = 1.0f / std::tan(rad(fovY) / 2);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
    return m;
}

// Rotation-only view matrix: yaw about +y (turning right), then pitch.
Mat4 viewMatrix(const Camera& cam) {
    float cy = std::cos(rad(cam.yaw)), sy = std::sin(rad(cam.yaw));
    float cp = std::cos(rad(cam.pitch)), sp = std::sin(rad(cam.pitch));
    Mat4 yaw = {cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    Mat4 pitch = {1, 0, 0, 0, 0, cp, -sp, 0, 0, sp, cp, 0, 0, 0, 0, 1};
    return mul(pitch, yaw);
}

Mat4 translate(float x, float y, float z) { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1}; }

// glRotatef about the x / y axis.
Mat4 rotateX(float deg) {
    float c = std::cos(rad(deg)), s = std::sin(rad(deg));
    return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}
Mat4 rotateY(float deg) {
    float c = std::cos(rad(deg)), s = std::sin(rad(deg));
    return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}

Mat4 ortho(float l, float r, float b, float t) {
    Mat4 m{};
    m[0] = 2 / (r - l);
    m[5] = 2 / (t - b);
    m[10] = -1;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1;
    return m;
}

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uMvp;
uniform vec4 uUvXform;  // uv * xy + zw
out vec2 vUv;
out vec2 vMaskUv;
void main() {
    vUv = aUv * uUvXform.xy + uUvXform.zw;
    vMaskUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kFragment = R"(#version 330 core
in vec2 vUv;
in vec2 vMaskUv;
uniform sampler2D uTex;
uniform sampler2D uMask;
uniform int uUseMask;
uniform vec4 uTint;
out vec4 color;
void main() {
    color = texture(uTex, vUv) * uTint;
    if (uUseMask == 1) color.a *= texture(uMask, vMaskUv).r;
}
)";

gl::GLuint compile(gl::GLenum type, const char* src) {
    gl::GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    gl::GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "shader: %s", log);
    }
    return s;
}

bool gNearest = false;  // scream.cfg QUALITY 0: GL_NEAREST everywhere (0x40bdde)

gl::GLuint uploadTexture(int w, int h, const uint8_t* rgba, bool mipmaps) {
    gl::GLuint id;
    gl::GenTextures(1, &id);
    gl::BindTexture(gl::TEXTURE_2D, id);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    if (gNearest) mipmaps = false;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gNearest ? gl::NEAREST : gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER,
                      gNearest ? gl::NEAREST : mipmaps ? gl::LINEAR_MIPMAP_LINEAR : gl::LINEAR);
    if (mipmaps)
        gl::GenerateMipmap(gl::TEXTURE_2D);
    return id;
}

}  // namespace

const FaceBasis& faceBasis(int face) {
    static const FaceBasis faces[7] = {
        {},
        {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}},   // front
        {{0, 0, 1}, {-1, 0, 0}, {0, -1, 0}},   // back
        {{-1, 0, 0}, {0, 0, -1}, {0, -1, 0}},  // left
        {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}},    // right
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},    // up: bottom edge meets the front's top edge
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},    // down: top edge meets the front's bottom edge
    };
    return faces[face >= 1 && face <= 6 ? face : 0];
}

Vec3 viewRay(const Camera& cam, float px, float py, int width, int height) {
    float t = std::tan(rad(cam.fovY) / 2);
    float aspect = float(width) / float(height) * cam.aspectScale;
    Vec3 d{(2 * px / width - 1) * t * aspect, (1 - 2 * py / height) * t, -1};
    // Undo the view rotation (inverse = transpose of a rotation).
    // In box space: picking against the unit cube then finds the point of the stretched face.
    Mat4 v = viewMatrix(cam);
    return {v[0] * d.x + v[1] * d.y + v[2] * d.z,
            (v[4] * d.x + v[5] * d.y + v[6] * d.z) / cam.boxY,
            v[8] * d.x + v[9] * d.y + v[10] * d.z};
}

bool pickFace(Vec3 dir, int& face, float& x, float& y, int texSize) {
    float best = 0;
    face = 0;
    for (int f = sc::FRONT; f <= sc::DOWN; ++f) {
        float d = dot(dir, faceBasis(f).center);
        if (d > best) {
            best = d;
            face = f;
        }
    }
    if (!face)
        return false;
    const FaceBasis& b = faceBasis(face);
    Vec3 p = dir * (1 / best) - b.center;
    x = (dot(p, b.right) + 1) / 2 * texSize;
    y = (dot(p, b.down) + 1) / 2 * texSize;
    return true;
}

Canvas Canvas::fit(int width, int height) {
    Canvas c;
    c.scale = std::min(float(width) / sc::SCREEN_W, float(height) / sc::SCREEN_H);
    c.x = (width - sc::SCREEN_W * c.scale) / 2;
    c.y = (height - sc::SCREEN_H * c.scale) / 2;
    return c;
}

bool Renderer::init() {
    program_ = gl::CreateProgram();
    gl::GLuint vs = compile(gl::VERTEX_SHADER, kVertex), fs = compile(gl::FRAGMENT_SHADER, kFragment);
    gl::AttachShader(program_, vs);
    gl::AttachShader(program_, fs);
    gl::LinkProgram(program_);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    gl::GLint ok = 0;
    gl::GetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetProgramInfoLog(program_, sizeof log, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "link: %s", log);
        return false;
    }
    uMvp_ = gl::GetUniformLocation(program_, "uMvp");
    uTint_ = gl::GetUniformLocation(program_, "uTint");
    uTex_ = gl::GetUniformLocation(program_, "uTex");
    uMask_ = gl::GetUniformLocation(program_, "uMask");
    uUseMask_ = gl::GetUniformLocation(program_, "uUseMask");
    uUvXform_ = gl::GetUniformLocation(program_, "uUvXform");

    gl::GenVertexArrays(1, &vao_);
    gl::BindVertexArray(vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 5 * sizeof(float), nullptr);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);

    const uint8_t white[4] = {255, 255, 255, 255};
    white_ = uploadTexture(1, 1, white, false);
    buildDome(150, 12, 4);
    return true;
}

void Renderer::beginFrame(int width, int height) {
    width_ = width;
    height_ = height;
    canvas_ = Canvas::fit(width, height);
    gl::Viewport(0, 0, width, height);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::UseProgram(program_);
    gl::Uniform1i(uTex_, 0);
    gl::Uniform1i(uMask_, 1);
    gl::Uniform1i(uUseMask_, 0);
    gl::Uniform4f(uUvXform_, 1, 1, 0, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
}

void Renderer::setViewport3D(const Viewport& vp, const Camera& cam) {
    gl::Viewport(vp.x, height_ - vp.y - vp.h, vp.w, vp.h);
    // gluPerspective(fov, W / (H - 128), 0.1, 1000) (0x40df60); no depth test.
    Mat4 box = {1, 0, 0, 0, 0, cam.boxY, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mat4 mvp = mul(mul(perspective(cam.fovY, float(vp.w) / float(vp.h) * cam.aspectScale, 0.1f, 1000.0f), viewMatrix(cam)), box);
    gl::UniformMatrix4fv(uMvp_, 1, 0, mvp.data());
}

void Renderer::applyBlend(Blend b) {
    switch (b) {
    case Blend::Opaque: gl::Disable(gl::BLEND); return;
    case Blend::Alpha: gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA); break;
    case Blend::AddAlpha: gl::BlendFunc(gl::SRC_ALPHA, gl::ONE); break;
    case Blend::AddOne: gl::BlendFunc(gl::ONE, gl::ONE); break;
    case Blend::DstColor: gl::BlendFunc(gl::DST_COLOR, gl::ONE_MINUS_SRC_ALPHA); break;
    case Blend::SrcColorOne: gl::BlendFunc(gl::SRC_COLOR, gl::ONE); break;
    case Blend::DstColorZero: gl::BlendFunc(gl::DST_COLOR, gl::ZERO); break;
    case Blend::DstColorOne: gl::BlendFunc(gl::DST_COLOR, gl::ONE); break;
    case Blend::OneInvSrcAlpha: gl::BlendFunc(gl::ONE, gl::ONE_MINUS_SRC_ALPHA); break;
    case Blend::SrcColorDstColor: gl::BlendFunc(gl::SRC_COLOR, gl::DST_COLOR); break;
    }
    gl::Enable(gl::BLEND);
}

void Renderer::drawNode(const View& node, const Camera& cam, const Viewport& vp, const Pass& pass) {
    setViewport3D(vp, cam);
    applyBlend(pass.blend);
    const float r = pass.r * tint_[0], g = pass.g * tint_[1], bl = pass.b * tint_[2];
    gl::Uniform4f(uTint_, r, g, bl, pass.a);
    // The decoder hands back rows top-first, so v = 0 is the top of the image.
    for (int f = sc::FRONT; f <= sc::DOWN; ++f) {
        const FaceBasis& b = faceBasis(f);
        auto corner = [&](float u, float v) {
            Vec3 p = b.center + b.right * (2 * u - 1) + b.down * (2 * v - 1);
            return std::array<float, 5>{p.x, p.y, p.z, u, v};
        };
        std::array<std::array<float, 5>, 4> q = {corner(0, 0), corner(1, 0), corner(0, 1), corner(1, 1)};
        gl::BufferData(gl::ARRAY_BUFFER, sizeof q, q.data(), gl::DYNAMIC_DRAW);
        const RendererTexture* mask = pass.masks ? (*pass.masks)[size_t(f - 1)] : nullptr;
        if (mask) {
            gl::ActiveTexture(gl::TEXTURE0 + 1);
            gl::BindTexture(gl::TEXTURE_2D, mask->id);
            gl::ActiveTexture(gl::TEXTURE0);
        }
        gl::Uniform1i(uUseMask_, mask ? 1 : 0);
        auto it = textures_.find(node.faces[f - 1]);
        bool have = it != textures_.end() && it->second.id;
        gl::BindTexture(gl::TEXTURE_2D, have ? it->second.id : white_);
        if (!have)
            gl::Uniform4f(uTint_, 0.15f, 0.0f, 0.15f, pass.a);  // missing texture: dark magenta
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
        gl::Uniform4f(uTint_, r, g, bl, pass.a);
    }
    gl::Uniform1i(uUseMask_, 0);
    gl::Viewport(0, 0, width_, height_);
}

// GenerateDome (0x418690), the classic sky dome: rings every dPhi degrees
// from the pole down to the rim, four vertices per step, drawn as a single
// triangle strip (the joins are degenerate). The pole is local +z; u wraps
// around it hTile times and v follows the local y axis.
void Renderer::buildDome(float r, float hTile, float vTile) {
    const float dTheta = 5, dPhi = 5;
    struct V {
        float x, y, z, u, v;
    };
    std::vector<V> v;
    auto vert = [&](float phi, float theta) {
        V p{r * std::sin(rad(phi)) * std::cos(rad(theta)), r * std::sin(rad(phi)) * std::sin(rad(theta)),
            r * std::cos(rad(phi)), 0, 0};
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        float vx = p.x / len, vy = p.y / len, vz = p.z / len;
        p.u = hTile * float(std::atan2(vx, vz) * (1 / (kPi * 2.0))) + 0.5f;
        p.v = vTile * float(std::asin(vy) * (1 / double(kPi))) + 0.5f;
        v.push_back(p);
    };
    for (float phi = 0; phi <= 90 - dPhi; phi += dPhi)
        for (float theta = 0; theta <= 360 - dTheta; theta += dTheta) {
            vert(phi, theta);
            vert(phi + dPhi, theta);
            vert(phi, theta + dTheta);
            vert(phi + dPhi, theta + dTheta);
        }
    // The seam fix: where a coordinate wraps between neighbours, lift the lower one.
    for (size_t i = 0; i + 3 < v.size(); ++i) {
        auto fix = [&](float V::*c, float limit) {
            const size_t pairs[6][2] = {{i, i + 1}, {i + 1, i}, {i, i + 2}, {i + 2, i}, {i + 1, i + 2}, {i + 2, i + 1}};
            for (const auto& [a, b] : pairs)
                if (v[a].*c - v[b].*c > limit) v[b].*c += 1;
        };
        fix(&V::u, 0.9f);
        fix(&V::v, 0.8f);
    }
    domeVertices_ = int(v.size());
    if (!domeVbo_)
        gl::GenBuffers(1, &domeVbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, domeVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(v.size() * sizeof(V)), v.data(), gl::STATIC_DRAW);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
}

// scream.exe's world is ours turned half way round (its camera faces +z at
// the front), so the dome is turned with it.
void Renderer::drawDome(const Camera& cam, const Viewport& vp, const RendererTexture* tex, const Pass& pass,
                        DomeFrame frame, float uvScale, float vOffset, float uOffset) {
    Mat4 model;
    switch (frame) {
    case DomeFrame::Clouds: model = mul(translate(0, -100, 0), rotateX(270)); break;  // 0x40d990
    case DomeFrame::RainA: model = rotateY(270); break;                               // 0x40dda0
    case DomeFrame::RainB: model = rotateY(-270); break;
    }
    model = mul(rotateY(180), model);
    gl::Viewport(vp.x, height_ - vp.y - vp.h, vp.w, vp.h);
    Mat4 mvp = mul(mul(perspective(cam.fovY, float(vp.w) / float(vp.h) * cam.aspectScale, 0.1f, 1000.0f), viewMatrix(cam)), model);
    gl::UniformMatrix4fv(uMvp_, 1, 0, mvp.data());
    applyBlend(pass.blend);
    gl::BindTexture(gl::TEXTURE_2D, tex ? tex->id : white_);
    gl::Uniform4f(uTint_, pass.r * tint_[0], pass.g * tint_[1], pass.b * tint_[2], pass.a);
    gl::Uniform4f(uUvXform_, uvScale, uvScale, uOffset, vOffset);
    gl::BindBuffer(gl::ARRAY_BUFFER, domeVbo_);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 5 * sizeof(float), nullptr);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, domeVertices_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, 5 * sizeof(float), nullptr);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    gl::Uniform4f(uUvXform_, 1, 1, 0, 0);
    gl::Viewport(0, 0, width_, height_);
}

void Renderer::drawCube(const Camera& cam, const Viewport& vp, const FaceTextures& faces, const Pass& pass,
                        const FaceUv* uv, unsigned skip) {
    setViewport3D(vp, cam);
    applyBlend(pass.blend);
    gl::Uniform4f(uTint_, pass.r * tint_[0], pass.g * tint_[1], pass.b * tint_[2], pass.a);
    for (int f = sc::FRONT; f <= sc::DOWN; ++f) {
        if (skip & (1u << (f - 1)))
            continue;
        const std::array<float, 4> r = uv ? (*uv)[size_t(f - 1)] : std::array<float, 4>{0, 0, 1, 1};
        const FaceBasis& b = faceBasis(f);
        auto corner = [&](float u, float v) {
            Vec3 p = b.center + b.right * (2 * u - 1) + b.down * (2 * v - 1);
            return std::array<float, 5>{p.x, p.y, p.z, r[0] + (r[2] - r[0]) * u, r[1] + (r[3] - r[1]) * v};
        };
        std::array<std::array<float, 5>, 4> q = {corner(0, 0), corner(1, 0), corner(0, 1), corner(1, 1)};
        gl::BufferData(gl::ARRAY_BUFFER, sizeof q, q.data(), gl::DYNAMIC_DRAW);
        const RendererTexture* t = faces[size_t(f - 1)];
        gl::BindTexture(gl::TEXTURE_2D, t && t->id ? t->id : white_);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }
    gl::Viewport(0, 0, width_, height_);
}

void Renderer::drawImagePass(const RendererTexture& t, float x, float y, float w, float h, const Pass& pass) {
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::Uniform4f(uTint_, pass.r * tint_[0], pass.g * tint_[1], pass.b * tint_[2], pass.a);
    quad2D(x, y, w, h, 0, 0, 1, 1, pass.blend);
}

void Renderer::drawRectPass(float x, float y, float w, float h, const Pass& pass) {
    drawImagePass(RendererTexture{white_, 1, 1}, x, y, w, h, pass);
}

void Renderer::setRepeat(const RendererTexture& t) {
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::REPEAT);
}

const RendererTexture* Renderer::optionalTexture(Resources& res, const std::string& name) {
    if (auto it = textures_.find(name); it != textures_.end())
        return it->second.id ? &it->second : nullptr;
    if (!res.exists(name)) {
        textures_[name] = {};
        return nullptr;
    }
    return texture(res, name);
}

void Renderer::drawNodeHotspots(const View& node, const Camera& cam, const Viewport& vp) {
    setViewport3D(vp, cam);
    gl::BindTexture(gl::TEXTURE_2D, white_);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    for (const auto& o : node.objects) {
        if (!o->enabled || o->face < sc::FRONT || o->face > sc::DOWN)
            continue;
        const float size = float(sc::DEF_TEX_SIZE);
        float u0 = std::clamp(o->rect.x / size, 0.0f, 1.0f), u1 = std::clamp((o->rect.x + o->rect.w) / size, 0.0f, 1.0f);
        float v0 = std::clamp(o->rect.y / size, 0.0f, 1.0f), v1 = std::clamp((o->rect.y + o->rect.h) / size, 0.0f, 1.0f);
        const FaceBasis& b = faceBasis(o->face);
        auto corner = [&](float u, float v) {
            Vec3 p = (b.center + b.right * (2 * u - 1) + b.down * (2 * v - 1)) * 0.99f;  // just inside the cube
            return std::array<float, 5>{p.x, p.y, p.z, 0, 0};
        };
        std::array<std::array<float, 5>, 4> q = {corner(u0, v0), corner(u1, v0), corner(u0, v1), corner(u1, v1)};
        gl::BufferData(gl::ARRAY_BUFFER, sizeof q, q.data(), gl::DYNAMIC_DRAW);
        // Colour by cursor: forward = green, look = blue, use = orange, other = magenta.
        float r = 1, g = 0, bl = 1;
        if (o->cursor == sc::FORWARD) r = 0, g = 1, bl = 0;
        else if (o->cursor == sc::LOOK) r = 0.2f, g = 0.5f, bl = 1;
        else if (o->cursor == sc::USE) r = 1, g = 0.6f, bl = 0;
        gl::Uniform4f(uTint_, r, g, bl, 0.35f);
        gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
    }
    gl::Uniform4f(uTint_, 1, 1, 1, 1);
    gl::Viewport(0, 0, width_, height_);
}

void Renderer::quad2D(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Blend blend) {
    Mat4 mvp = ortho(0, float(width_), float(height_), 0);
    gl::UniformMatrix4fv(uMvp_, 1, 0, mvp.data());
    float x0 = canvas_.x + x * canvas_.scale, y0 = canvas_.y + y * canvas_.scale;
    float x1 = x0 + w * canvas_.scale, y1 = y0 + h * canvas_.scale;
    float q[4][5] = {{x0, y0, 0, u0, v0}, {x1, y0, 0, u1, v0}, {x0, y1, 0, u0, v1}, {x1, y1, 0, u1, v1}};
    gl::BufferData(gl::ARRAY_BUFFER, sizeof q, q, gl::DYNAMIC_DRAW);
    applyBlend(blend);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
}

void Renderer::drawImage(const Texture& t, float x, float y, float w, float h, float alpha) {
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::Uniform4f(uTint_, tint_[0], tint_[1], tint_[2], alpha);
    quad2D(x, y, w, h);
}

void Renderer::drawImageUV(const Texture& t, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                           float alpha, float r, float g, float b, Blend blend) {
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::Uniform4f(uTint_, r, g, b, alpha);
    quad2D(x, y, w, h, u0, v0, u1, v1, blend);
}

void Renderer::drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    gl::BindTexture(gl::TEXTURE_2D, white_);
    gl::Uniform4f(uTint_, r, g, b, a);
    quad2D(x, y, w, h);
}

Renderer::Texture Renderer::upload(const std::string& name, const Image& img) {
    // No mipmaps: glyphs are sampled at close to 1:1 and mip bleeding blurs neighbours.
    Texture t{uploadTexture(img.width, img.height, img.pixels.data(), false), img.width, img.height};
    if (auto it = textures_.find(name); it != textures_.end() && it->second.id)
        gl::DeleteTextures(1, &it->second.id);
    textures_[name] = t;
    return t;
}

void Renderer::uploadInto(Texture& t, int w, int h, const uint8_t* rgba) {
    if (!t.id) {
        t = {uploadTexture(w, h, rgba, false), w, h};
        return;
    }
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
    t.width = w;
    t.height = h;
}

void Renderer::setNearest(bool on) { gNearest = on; }

void Renderer::refilterAll() {
    for (auto& [name, t] : textures_) {
        if (!t.id) continue;
        gl::BindTexture(gl::TEXTURE_2D, t.id);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gNearest ? gl::NEAREST : gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gNearest ? gl::NEAREST : gl::LINEAR);
    }
}

void Renderer::captureFrame(Texture& t) {
    if (!t.id) {
        gl::GenTextures(1, &t.id);
        gl::BindTexture(gl::TEXTURE_2D, t.id);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    }
    gl::BindTexture(gl::TEXTURE_2D, t.id);
    gl::CopyTexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 0, 0, width_, height_, 0);
    t.width = width_;
    t.height = height_;
}

void Renderer::release(Texture& t) {
    if (t.id)
        gl::DeleteTextures(1, &t.id);
    t = {};
}

const Renderer::Texture* Renderer::texture(Resources& res, const std::string& name) {
    if (name.empty())
        return nullptr;
    if (auto it = textures_.find(name); it != textures_.end())
        return it->second.id ? &it->second : nullptr;
    Texture t;
    if (auto data = res.read(name)) {
        if (auto img = decodeImage(*data)) {
            t = {uploadTexture(img->width, img->height, img->pixels.data(), true), img->width, img->height};
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "cannot decode %s", name.c_str());
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "missing resource %s", name.c_str());
    }
    auto& slot = textures_[name] = t;  // cache failures too, so they are reported once
    return slot.id ? &slot : nullptr;
}

void Renderer::evictAll() {
    for (auto& [name, t] : textures_)
        if (t.id)
            gl::DeleteTextures(1, &t.id);
    textures_.clear();
}

std::string Renderer::capture(const char* path) {
    std::vector<uint8_t> px(size_t(width_) * height_ * 4);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, width_, height_, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
    for (size_t i = 3; i < px.size(); i += 4)
        px[i] = 255;
    return writePng(path, width_, height_, px.data(), true) ? path : "";
}

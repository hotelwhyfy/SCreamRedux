#pragma once

#include <array>
#include <string>
#include <unordered_map>

#include "gl.hpp"

class Resources;
struct Image;
struct View;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Orientation of each cube face image in world space (y up, front = -z).
// A face pixel (u, v) in [0,1] maps to center + (2u-1)*right + (2v-1)*down.
struct FaceBasis {
    Vec3 center, right, down;
};
const FaceBasis& faceBasis(int face);  // face = sc::FRONT..sc::DOWN

// Pixel rectangle in the window, top-left origin.
struct Viewport {
    int x = 0, y = 0, w = 0, h = 0;
};

struct Camera {
    float yaw = 0;    // degrees, 0 = front, +90 = right
    float pitch = 0;  // degrees, + = up
    float fovY = 53;  // degrees
};

// World-space direction through window pixel (px, py).
Vec3 viewRay(const Camera& cam, float px, float py, int width, int height);
// Which face a direction hits and where, in face pixels (0..texSize).
bool pickFace(Vec3 dir, int& face, float& x, float& y, int texSize);

// Placement of the 800x600 virtual canvas inside the window (letterboxed).
struct Canvas {
    float x = 0, y = 0, scale = 1;
    static Canvas fit(int width, int height);
    float toCanvasX(float px) const { return (px - x) / scale; }
    float toCanvasY(float py) const { return (py - y) / scale; }
};

struct RendererTexture {
    gl::GLuint id = 0;
    int width = 0, height = 0;
};

// How an extra pass over the scene combines with what is already drawn
// (the blend modes scream.exe's effects use).
enum class Blend { Opaque, Alpha, AddAlpha, AddOne, DstColor };

struct RenderPass {
    float r = 1, g = 1, b = 1, a = 1;
    Blend blend = Blend::Opaque;
    // Per-face masks (face - 1); a mask's luminance becomes the face's alpha,
    // so a backdrop (sky, fog, rain) shows through the black parts.
    const std::array<const RendererTexture*, 6>* masks = nullptr;
};

class Renderer {
public:
    using Texture = RendererTexture;

    bool init();
    void beginFrame(int width, int height);
    using Pass = RenderPass;
    void drawNode(const View& node, const Camera& cam, const Viewport& vp, const Pass& pass = {});
    // Sphere around the viewer (sky, fog, rain backdrops). v runs from the zenith (0)
    // to the nadir (1); tex = nullptr draws a flat colour.
    void drawDome(const Camera& cam, const Viewport& vp, const RendererTexture* tex, const Pass& pass,
                  float uScale, float vScale, float uOffset, float vOffset);
    // Extra full-texture pass in canvas coordinates (zoom effects).
    void drawImagePass(const RendererTexture& t, float x, float y, float w, float h, const Pass& pass);
    void drawRectPass(float x, float y, float w, float h, const Pass& pass);
    // Wrap a texture instead of clamping (tiling clouds and rain).
    void setRepeat(const RendererTexture& t);
    // Look a texture up only if the resource exists (optional masks), without warnings.
    const RendererTexture* optionalTexture(Resources& res, const std::string& name);
    // Debug overlay (SC_SHOW_HOTSPOTS): enabled objects as translucent rects on their faces.
    void drawNodeHotspots(const View& node, const Camera& cam, const Viewport& vp);
    // Draw a texture in canvas coordinates (800x600 space).
    void drawImage(const Texture& t, float x, float y, float w, float h, float alpha = 1);
    // Draw the (u0,v0)-(u1,v1) part of a texture.
    void drawImageUV(const Texture& t, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     float alpha = 1, float r = 1, float g = 1, float b = 1, Blend blend = Blend::Alpha);
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a);
    // Colour multiplied into drawNode / drawImage (e.g. the inventory's sepia backdrop).
    void setTint(float r, float g, float b) { tint_ = {r, g, b}; }

    const Texture* texture(Resources& res, const std::string& name);
    // Upload an already-decoded image under `name` (e.g. fonts, which need the pixels too).
    Texture upload(const std::string& name, const Image& img);
    // (Re)fill a texture owned by the caller, e.g. a video frame.
    void uploadInto(Texture& t, int w, int h, const uint8_t* rgba);
    void release(Texture& t);
    // Copy the current back buffer (the whole window) into `t` (bottom row first).
    void captureFrame(Texture& t);
    void evictAll();

    // Read back the frame (RGBA, bottom row first).
    std::string capture(const char* path);

private:
    void setViewport3D(const Viewport& vp, const Camera& cam);
    void quad2D(float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
                Blend blend = Blend::Alpha);

    gl::GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    gl::GLint uMvp_ = -1, uTint_ = -1, uTex_ = -1, uMask_ = -1, uUseMask_ = -1, uUvXform_ = -1;
    gl::GLuint domeVbo_ = 0;
    int domeVertices_ = 0;
    void applyBlend(Blend b);
    void buildDome();
    gl::GLuint white_ = 0;
    std::array<float, 3> tint_{1, 1, 1};
    int width_ = 0, height_ = 0;
    Canvas canvas_;
    std::unordered_map<std::string, Texture> textures_;
};

// OpenGL 3.3 core / GLES 3.0 device (CR1:U's, with CR3's vertices: normals and a directional light).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gfx/gl_backup.hpp"

#include "gfx/gl.hpp"
#include "gfx/render_device.hpp"

namespace cr3 {

namespace {

const char* kVertex = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFragment = R"(
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV) * uColor;
}
)";

// The bloom's filters (MVGL's built-in GLSL - filterCopyVP / filterCopyFP / filterBrightFP /
// filterBlurVP / filterBlurFP, libcrx.so .rodata 0xf79ebd ..): the quad's position its UV
const char* kFilterVertex = R"(
layout(location = 0) in vec2 aPos;
uniform vec2 blurOffset;
out vec2 vTexcoord0;
out vec2 vTexcoord1;
out vec2 vTexcoord2;
out vec2 vTexcoord3;
out vec2 vTexcoord4;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vec2 uv = (aPos + 1.0) * 0.5;
    vec2 doubleBlurOffset = blurOffset * 2.0;
    vTexcoord0 = uv - doubleBlurOffset;
    vTexcoord1 = uv - blurOffset;
    vTexcoord2 = uv;
    vTexcoord3 = uv + blurOffset;
    vTexcoord4 = uv + doubleBlurOffset;
}
)";
const char* kFilterFragment[3] = {
    R"(
in vec2 vTexcoord2;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vTexcoord2);
}
)",
    R"(
in vec2 vTexcoord2;
uniform sampler2D uTex;
uniform float bloomLimit;
out vec4 fragColor;
void main() {
    vec4 color = texture(uTex, vTexcoord2) * 2.0;
    float lum = color.x + color.y + color.z;
    fragColor = color * float(lum > bloomLimit);
}
)",
    R"(
in vec2 vTexcoord0;
in vec2 vTexcoord1;
in vec2 vTexcoord2;
in vec2 vTexcoord3;
in vec2 vTexcoord4;
uniform sampler2D uTex;
uniform float bloomScale;
out vec4 fragColor;
void main() {
    vec4 sum = 0.05448868 * texture(uTex, vTexcoord0);
    sum += 0.24420134 * texture(uTex, vTexcoord1);
    sum += 0.40261995 * texture(uTex, vTexcoord2);
    sum += 0.24420134 * texture(uTex, vTexcoord3);
    sum += 0.05448868 * texture(uTex, vTexcoord4);
    fragColor = sum * bloomScale;
}
)"};

// The game's shading (shading.glsl, after shade_flags.h) or the fixed path: GL ES 1.1-style
// texture modes (uTexMode: 0 replace, 1 add, 2 blend, 3 decal, 4 modulate, -1 untextured), an
// alpha test, a directional light
const char* kShadeFlags =
#include "shade_flags.h.inc"
    ;
const char* kShading =
#include "shading.glsl.inc"
    ;

const char* kMeshVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 4) in vec3 aNormal;
layout(location = 5) in vec3 aTangent;
layout(location = 6) in vec3 aBinormal;
layout(location = 7) in vec2 aUV1;
uniform mat4 uMvp;
out vec2 vUV;
out vec2 vUV1;
out vec4 vPoint;
out vec4 vColor;
out vec3 vNormal;
out vec3 vTangent;
out vec3 vBinormal;
out vec3 vView;
out float vFog;
void main() {
    vUV = aUV;
    vUV1 = aUV1;
    vColor = aColor;
    vNormal = aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
    if ((mat.flags.x & SHADE_ON) != 0) {
        vUV = shadeUV(aUV);
        vUV1 = shadeUV1(aUV1);
        vPoint = shadePoint(aPos);
        vTangent = aTangent;
        vBinormal = aBinormal;
        vView = shadeView(aPos);
        vFog = shadeFog(gl_Position.w);
    } else {
        vTangent = vBinormal = vView = vec3(0.0);
        vPoint = vec4(0.0);
        vFog = 0.0;
    }
}
)";

const char* kMeshFragment = R"(
in vec2 vUV;
in vec2 vUV1;
in vec4 vPoint;
in vec4 vColor;
in vec3 vNormal;
in vec3 vTangent;
in vec3 vBinormal;
in vec3 vView;
in float vFog;
uniform vec4 uLight; // xyz towards the light, w > 0: lit
uniform vec4 uColor;
uniform int uTexMode;
uniform int uAlphaTex;  // GL_ALPHA texture: colour always comes from the fragment
uniform float uAlphaRef;
out vec4 fragColor;
void main() {
    if ((mat.flags.x & SHADE_ON) != 0) {
        fragColor = shade(vUV, vUV1, vColor, vNormal, vTangent, vBinormal, vView, vFog, vPoint);
        return;
    }
    vec4 base = uColor * vColor;
    vec4 c = base;
    if (uTexMode >= 0) {
        vec4 t = texture(uTex, vUV);
        if (uAlphaTex != 0) c = vec4(base.rgb, uTexMode == 0 ? t.a : base.a * t.a);
        else if (uTexMode == 0) c = t;
        else if (uTexMode == 1) c = vec4(base.rgb + t.rgb, base.a * t.a);
        else if (uTexMode == 2) c = vec4(base.rgb * (vec3(1.0) - t.rgb), base.a * t.a);
        else if (uTexMode == 3) c = vec4(mix(base.rgb, t.rgb, t.a), base.a);
        else c = base * t;
    }
    if (uAlphaRef >= 0.0 && c.a <= uAlphaRef) discard;
    if (uLight.w > 0.0) c.rgb *= 0.45 + 0.65 * max(dot(normalize(vNormal), normalize(uLight.xyz)), 0.0);
    fragColor = c;
}
)";

unsigned compile(unsigned type, const char* body, const std::string& prefix) {
    const std::string src = std::string(gl::shaderHeader()) + prefix + body;
    const char* p = src.c_str();
    const unsigned s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &p, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "shader compile failed: %s", log);
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

// `shading`: the shared GLSL first (the mesh program)
unsigned link(const char* vsBody, const char* fsBody, bool shading = false) {
    const std::string common = shading ? std::string(kShadeFlags) + kShading : std::string();
    const unsigned vs = compile(GL_VERTEX_SHADER, vsBody, common);
    const unsigned fs = compile(GL_FRAGMENT_SHADER, fsBody, shading ? std::string(kShadeFlags) + "#define SHADING_FRAGMENT\n" + kShading : common);
    if (!vs || !fs) return 0;
    const unsigned p = gl::CreateProgram();
    gl::AttachShader(p, vs);
    gl::AttachShader(p, fs);
    gl::LinkProgram(p);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    GLint ok = 0;
    gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl::GetProgramInfoLog(p, sizeof log, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "program link failed: %s", log);
        gl::DeleteProgram(p);
        return 0;
    }
    return p;
}

// Blend factor codes follow the GL ES 1.1 factor lists (source allows SATURATE, destination
// allows SRC_COLOR) — 9 source and 8 destination values, as validated by setBlendMode_sd.
constexpr GLenum kBlendSrc[9] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA,
                                 GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                                 GL_SRC_ALPHA_SATURATE};
constexpr GLenum kBlendDst[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
                                 GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};

constexpr size_t kStride = kVertexStride;

class GlDevice final : public RenderDevice {
public:
    uint64_t windowFlags() const override { return SDL_WINDOW_OPENGL; }

    bool init(SDL_Window* window) override {
#ifdef CR_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // macOS
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        window_ = window;
        ctx_ = SDL_GL_CreateContext(window);
        if (!ctx_ || !gl::load()) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpenGL setup failed: %s", SDL_GetError());
            return false;
        }
        return initObjects();
    }

    // SDL has already made its replacement context current. Old GL names must not
    // be deleted here: they belong to the lost context, not this new namespace.
    bool restoreAfterContextLoss() override {
        const SDL_GLContext replacement = SDL_GL_GetCurrentContext();
        if (!replacement || SDL_GL_GetCurrentWindow() != window_) return false;
        ctx_ = replacement;
        program_ = meshProgram_ = vao_ = vbo_ = 0;
        filterProgram_[0] = filterProgram_[1] = filterProgram_[2] = 0, filterFbos_.clear(), inFilters_ = false; // (the lost context's)
        ubo_ = 0;
        for (auto& [handle, rec] : textures_) rec.name = 0;
        for (auto& [handle, rec] : meshes_) rec.vao = rec.vbo = rec.ebo = 0;
        anisoLimit_ = -1;
        if (!gl::load() || !initObjects()) return false;
        for (auto& [handle, rec] : textures_) {
            std::vector<uint8_t> pixels;
            if (!rec.pixels.unpack(pixels)) return false;
            createTexture(rec, pixels);
        }
        for (auto& [handle, rec] : meshes_) createMesh(rec);
        const int errors = takeErrors();
        if (errors) SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpenGL recovery: %d errors", errors);
        else SDL_Log("OpenGL context restored: %zu textures, %zu meshes", textures_.size(), meshes_.size());
        return errors == 0;
    }

    void shutdown() override {
        if (!ctx_) return;
        for (auto& [handle, rec] : textures_) gl::DeleteTextures(1, &rec.name);
        for (auto& [handle, rec] : meshes_) deleteMesh(rec);
        textures_.clear();
        meshes_.clear();
        mipmapped_.clear();
        white_ = {};
        if (vbo_) gl::DeleteBuffers(1, &vbo_);
        if (ubo_) gl::DeleteBuffers(1, &ubo_);
        if (vao_) gl::DeleteVertexArrays(1, &vao_);
        if (program_) gl::DeleteProgram(program_);
        if (meshProgram_) gl::DeleteProgram(meshProgram_);
        vbo_ = ubo_ = vao_ = program_ = meshProgram_ = 0;
        SDL_GL_DestroyContext(ctx_);
        ctx_ = nullptr;
    }

    std::string description() const override {
        return std::string("OpenGL ") + reinterpret_cast<const char*>(gl::GetString(GL_VERSION)) + " / " +
               reinterpret_cast<const char*>(gl::GetString(GL_RENDERER));
    }

    void setFiltering(const TextureFiltering& f) override {
        if (f == filtering_) return;
        filtering_ = f;
        for (unsigned id : mipmapped_) filter(textureName(id));
    }
    int maxAnisotropy() const override {
        if (anisoLimit_ < 0) {
            int n = 1;
            if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic") ||
                SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic")) {
                float m = 1;
                gl::GetFloatv(0x84FF, &m); // (GL_MAX_TEXTURE_MAX_ANISOTROPY)
                n = std::max(1, int(m));
            }
            const_cast<GlDevice*>(this)->anisoLimit_ = n;
        }
        return anisoLimit_;
    }

    Texture upload(const Image& img) override {
        Texture t;
        t.width = img.logicalW ? img.logicalW : img.width; // (an HD replacement: the original's)
        t.height = img.logicalH ? img.logicalH : img.height;
        t.alphaOnly = img.alphaOnly;
        t.id = nextTexture_++;
        auto& rec = textures_[t.id];
        rec.width = img.width;
        rec.height = img.height;
        rec.mipmaps = img.mipmaps;
        rec.pixels.store(img.rgba);
        createTexture(rec, img.rgba);
        if (img.mipmaps) mipmapped_.insert(t.id);
        return t;
    }

    void destroy(Texture& tex) override {
        if (const auto fb = filterFbos_.find(tex.id); fb != filterFbos_.end()) {
            gl::DeleteFramebuffers(1, &fb->second);
            filterFbos_.erase(fb);
        }
        auto it = textures_.find(tex.id);
        if (it != textures_.end()) {
            gl::DeleteTextures(1, &it->second.name);
            textures_.erase(it);
        }
        mipmapped_.erase(tex.id);
        tex = {};
    }

    GpuMesh upload(const std::vector<uint8_t>& vertices, const std::vector<uint32_t>& indices, int texture) override {
        GpuMesh g;
        g.texture = texture;
        g.indexCount = int(indices.size());
        g.vao = nextMesh_++;
        auto& rec = meshes_[g.vao];
        rec.vertices = vertices;
        rec.indices = indices;
        createMesh(rec);
        return g;
    }

    void updateVertices(GpuMesh& mesh, const std::vector<uint8_t>& vertices) override {
        auto it = meshes_.find(mesh.vao);
        if (it == meshes_.end()) return;
        auto& rec = it->second;
        const bool sameSize = rec.vertices.size() == vertices.size();
        rec.vertices = vertices; // includes the latest skinned/animated pose
        gl::BindBuffer(GL_ARRAY_BUFFER, rec.vbo);
        if (sameSize) gl::BufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vertices.size()), vertices.data());
        else gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size()), vertices.data(), GL_DYNAMIC_DRAW);
    }

    void destroy(GpuMesh& mesh) override {
        auto it = meshes_.find(mesh.vao);
        if (it != meshes_.end()) {
            deleteMesh(it->second);
            meshes_.erase(it);
        }
        mesh = {};
    }

    void beginFrame(int w, int h) override {
        drawableW_ = w;
        drawableH_ = h;
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0, 0, 0, 1);
        gl::DepthMask(GL_TRUE);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void endFrame(bool present) override {
        if (present) SDL_GL_SwapWindow(window_);
    }

    void setViewport(float fx, float fy, float fw, float fh) override {
        const int x = int(fx), y = int(fy), w = int(fw), h = int(fh);
        gl::Viewport(x, drawableH_ - y - h, w, h);
    }

    void setClip(bool on, float fx, float fy, float fw, float fh) override {
        if (!on) {
            gl::Disable(GL_SCISSOR_TEST);
            return;
        }
        const int x = int(fx), y = int(fy), w = std::max(int(fw), 0), h = std::max(int(fh), 0);
        gl::Enable(GL_SCISSOR_TEST);
        gl::Scissor(x, drawableH_ - y - h, w, h);
    }

    void clearDepth() override {
        gl::DepthMask(GL_TRUE);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
    }

    void setColorWrite(bool on) override {
        const GLboolean v = on ? GL_TRUE : GL_FALSE;
        gl::ColorMask(v, v, v, v);
    }
    // (the default framebuffer, bottom row first, into the texture - its alpha read as 1)
    bool copyFrame(Texture& tex) override {
        if (drawableW_ <= 0 || drawableH_ <= 0) return false;
        if (!tex.id || tex.width != drawableW_ || tex.height != drawableH_ || !textures_.count(tex.id)) {
            destroy(tex);
            Image img;
            img.width = drawableW_, img.height = drawableH_;
            img.rgba.assign(size_t(drawableW_) * size_t(drawableH_) * 4, 0);
            tex = upload(img);
            if (!tex.id) return false;
        }
        const auto it = textures_.find(tex.id);
        if (it == textures_.end()) return false;
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif
        gl::BindTexture(GL_TEXTURE_2D, it->second.name);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, drawableW_, drawableH_);
        tex.bottomUp = true;
        return true;
    }

    void setQuadAdditive(bool on) override { quadAdditive_ = on; }
    bool quadAdditive_ = false;

    void drawQuad(const Texture& tex, const float* v, const float* proj, const float* rgba) override {
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, quadAdditive_ ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        gl::UseProgram(program_);
        gl::UniformMatrix4fv(uProj_, 1, GL_FALSE, proj);
        gl::Uniform1i(uTex_, 0);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, v, GL_DYNAMIC_DRAW);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, textureName(tex.id));
        gl::Uniform4f(uColor_, rgba[0], rgba[1], rgba[2], rgba[3]);
        gl::DrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- the bloom's filters: an FBO on an RGB565 texture each ----
    unsigned filterProgram_[3] = {};
    int uFilterOffset_[3] = {-1, -1, -1}, uFilterLimit_ = -1, uFilterScale_ = -1;
    std::unordered_map<unsigned, unsigned> filterFbos_; // (texture id -> its framebuffer)
    GLint filterPrevFbo_ = 0;
    bool inFilters_ = false;

    bool beginFilters() override {
        if (!filterProgram_[0])
            for (int k = 0; k < 3; ++k) {
                filterProgram_[k] = link(kFilterVertex, kFilterFragment[k]);
                if (!filterProgram_[k]) return false;
                gl::UseProgram(filterProgram_[k]);
                gl::Uniform1i(gl::GetUniformLocation(filterProgram_[k], "uTex"), 0);
                uFilterOffset_[k] = gl::GetUniformLocation(filterProgram_[k], "blurOffset");
            }
        uFilterLimit_ = gl::GetUniformLocation(filterProgram_[1], "bloomLimit");
        uFilterScale_ = gl::GetUniformLocation(filterProgram_[2], "bloomScale");
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
        gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &filterPrevFbo_); // (SDL's default framebuffer isn't 0 everywhere)
        inFilters_ = true;
        return true;
    }

    bool filterPass(Filter f, const Texture& src, Texture& dst, int w, int h, const FilterParams& p) override {
        if (!inFilters_ || w <= 0 || h <= 0 || !src.id) return false;
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
        if (!dst.id || dst.width != w || dst.height != h || !textures_.count(dst.id) || !filterFbos_.count(dst.id)) {
            destroy(dst);
            Image img;
            img.width = w, img.height = h;
            img.rgba.assign(size_t(w) * size_t(h) * 4, 0);
            dst = upload(img);
            const auto it = textures_.find(dst.id);
            if (!dst.id || it == textures_.end()) return false;
            gl::BindTexture(GL_TEXTURE_2D, it->second.name); // (RenderTarget's: GL_RGB / UNSIGNED_SHORT_5_6_5, clamped, linear)
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
            unsigned fbo = 0;
            gl::GenFramebuffers(1, &fbo);
            gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
            gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it->second.name, 0);
            if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                gl::DeleteFramebuffers(1, &fbo);
                gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(filterPrevFbo_));
                destroy(dst);
                return false;
            }
            filterFbos_[dst.id] = fbo;
            dst.bottomUp = true;
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, filterFbos_[dst.id]);
        gl::Viewport(0, 0, w, h);
        gl::Disable(GL_DEPTH_TEST);
        gl::Disable(GL_CULL_FACE);
        gl::Disable(GL_BLEND);
        gl::Disable(GL_SCISSOR_TEST);
        gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const int k = f == Filter::Copy ? 0 : f == Filter::Bright ? 1 : 2;
        gl::UseProgram(filterProgram_[k]);
        gl::Uniform2f(uFilterOffset_[k], f == Filter::Blur ? p.offset[0] : 0.0f, f == Filter::Blur ? p.offset[1] : 0.0f);
        if (f == Filter::Bright) gl::Uniform1f(uFilterLimit_, p.limit);
        if (f == Filter::Blur) gl::Uniform1f(uFilterScale_, p.scale);
        gl::ActiveTexture(GL_TEXTURE0);
        gl::BindTexture(GL_TEXTURE_2D, textureName(src.id));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        static const float kQuad[24] = {-1, -1, 0, 0, 1, -1, 0, 0, 1, 1, 0, 0, -1, -1, 0, 0, 1, 1, 0, 0, -1, 1, 0, 0};
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, sizeof kQuad, kQuad, GL_DYNAMIC_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, 6);
        return true;
    }

    void endFilters(const Texture&) override {
        if (!inFilters_) return;
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(filterPrevFbo_));
        gl::Viewport(0, 0, drawableW_, drawableH_);
        gl::Enable(GL_BLEND);
        inFilters_ = false;
    }

    bool readTexture(const Texture& tex, std::vector<uint8_t>& rgba) override {
        const auto fb = filterFbos_.find(tex.id);
        if (fb == filterFbos_.end()) return false;
        GLint prev = 0;
        gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fb->second);
        rgba.assign(size_t(tex.width) * size_t(tex.height) * 4, 0);
        gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, tex.width, tex.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()); // (ReadColorBufferImage 0xba0038)
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(prev));
        return true;
    }

    void drawMesh(const GpuMesh& mesh, const Texture* tex, const float* mvp, const MeshShading& sh,
                  const DrawState& st) override {
        gl::UseProgram(meshProgram_);
        gl::FrontFace(GL_CCW);
        gl::DepthFunc(GL_LEQUAL);
        gl::UniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp);
        gl::Uniform4f(uMeshColor_, sh.color[0], sh.color[1], sh.color[2], sh.color[3]);
        gl::Uniform1f(uAlphaRef_, sh.alphaRef);
        gl::Uniform1i(uTexMode_, sh.texMode);
        gl::Uniform1i(uAlphaTex_, sh.alphaTex ? 1 : 0);
        gl::Uniform4f(uLight_, sh.light[0], sh.light[1], sh.light[2], sh.lit ? 1.0f : 0.0f);
        const ShadingBlock block = shadingBlock(sh, lighting_);
        gl::BindBuffer(GL_UNIFORM_BUFFER, ubo_);
        gl::BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof block, &block);
        bindTexture(2, sh.extraMap, sh.extraSampling);
        bindTexture(1, sh.normalMap, sh.normalSampling);
        bindTexture(0, tex, st.sampling);
        if (st.blend) {
            gl::Enable(GL_BLEND);
            gl::BlendFunc(kBlendSrc[st.blendSrc < 9 ? st.blendSrc : 4], kBlendDst[st.blendDst < 8 ? st.blendDst : 5]);
            static const GLenum kOps[5] = {0x8006, 0x800a, 0x800b, 0x8007, 0x8008}; // (FUNC_ADD, _SUBTRACT, _REVERSE_SUBTRACT, MIN, MAX)
            if (st.blendOp) gl::BlendEquation(kOps[st.blendOp < 5 ? st.blendOp : 0]);
        } else {
            gl::Disable(GL_BLEND);
        }
        if (st.cull == 0) {
            gl::Disable(GL_CULL_FACE);
        } else {
            gl::Enable(GL_CULL_FACE);
            gl::CullFace(st.cull == 1 ? GL_FRONT : GL_BACK);
        }
        if (st.depthTest) gl::Enable(GL_DEPTH_TEST);
        else gl::Disable(GL_DEPTH_TEST);
        gl::DepthMask(st.depthWrite ? GL_TRUE : GL_FALSE);
        gl::BindVertexArray(meshName(mesh.vao));
        gl::DrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
        gl::DepthMask(GL_TRUE);
        if (st.blend && st.blendOp) gl::BlendEquation(0x8006);
    }

    bool readPixels(std::vector<uint8_t>& rgba, int& w, int& h) override {
        w = drawableW_;
        h = drawableH_;
        std::vector<uint8_t> px(size_t(w) * h * 4);
        gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        rgba.resize(px.size());
        for (int y = 0; y < h; ++y) // GL rows are bottom-up
            std::memcpy(&rgba[size_t(y) * w * 4], &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
        return true;
    }

    int takeErrors() override {
        int n = 0;
        while (gl::GetError() != GL_NO_ERROR) ++n;
        return n;
    }

    void setVSync(bool on) override { vsync_ = on; SDL_GL_SetSwapInterval(on ? 1 : 0); }

private:

    struct TextureRec {
        unsigned name = 0;
        int width = 0, height = 0;
        bool mipmaps = false;
        crgl::Backup pixels;
    };
    struct MeshRec {
        unsigned vao = 0, vbo = 0, ebo = 0;
        std::vector<uint8_t> vertices;
        std::vector<uint32_t> indices;
    };
    std::unordered_map<unsigned, TextureRec> textures_;
    std::unordered_map<unsigned, MeshRec> meshes_;
    unsigned nextTexture_ = 1, nextMesh_ = 1;
    bool vsync_ = true;
    unsigned textureName(unsigned handle) const {
        auto it = textures_.find(handle);
        return it == textures_.end() ? 0 : it->second.name;
    }
    unsigned meshName(unsigned handle) const {
        auto it = meshes_.find(handle);
        return it == meshes_.end() ? 0 : it->second.vao;
    }
    void createTexture(TextureRec& rec, const std::vector<uint8_t>& pixels) {
        gl::GenTextures(1, &rec.name);
        gl::BindTexture(GL_TEXTURE_2D, rec.name);
        gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rec.width, rec.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (rec.mipmaps) {
            gl::GenerateMipmap(GL_TEXTURE_2D);
        }
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, rec.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (rec.mipmaps) filter(rec.name);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    void createMesh(MeshRec& rec) {
        gl::GenVertexArrays(1, &rec.vao);
        gl::GenBuffers(1, &rec.vbo);
        gl::GenBuffers(1, &rec.ebo);
        gl::BindVertexArray(rec.vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, rec.vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(rec.vertices.size()), rec.vertices.data(), GL_STATIC_DRAW);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, rec.ebo);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(rec.indices.size() * sizeof(uint32_t)), rec.indices.data(), GL_STATIC_DRAW);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(12));
        gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, kStride, reinterpret_cast<const void*>(20));
        gl::VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(24));
        gl::VertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(36));
        gl::VertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(48));
        gl::VertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(60));
        for (unsigned a : {0u, 1u, 2u, 4u, 5u, 6u, 7u}) gl::EnableVertexAttribArray(a);
        gl::BindVertexArray(0);
    }
    void deleteMesh(MeshRec& rec) {
        gl::DeleteBuffers(1, &rec.ebo);
        gl::DeleteBuffers(1, &rec.vbo);
        gl::DeleteVertexArrays(1, &rec.vao);
    }
    bool initObjects() {
        program_ = link(kVertex, kFragment);
        meshProgram_ = link(kMeshVertex, kMeshFragment, true);
        if (!program_ || !meshProgram_) return false;
        uMvp_ = gl::GetUniformLocation(meshProgram_, "uMvp");
        gl::UseProgram(meshProgram_);
        gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uTex"), 0);
        gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uNormal"), 1);
        gl::Uniform1i(gl::GetUniformLocation(meshProgram_, "uExtra"), 2);
        gl::UniformBlockBinding(meshProgram_, gl::GetUniformBlockIndex(meshProgram_, "Material"), 0);
        gl::GenBuffers(1, &ubo_);
        gl::BindBuffer(GL_UNIFORM_BUFFER, ubo_);
        gl::BufferData(GL_UNIFORM_BUFFER, sizeof(ShadingBlock), nullptr, GL_DYNAMIC_DRAW);
        gl::BindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);
        uAlphaRef_ = gl::GetUniformLocation(meshProgram_, "uAlphaRef");
        uMeshColor_ = gl::GetUniformLocation(meshProgram_, "uColor");
        uTexMode_ = gl::GetUniformLocation(meshProgram_, "uTexMode");
        uAlphaTex_ = gl::GetUniformLocation(meshProgram_, "uAlphaTex");
        uLight_ = gl::GetUniformLocation(meshProgram_, "uLight");
        uProj_ = gl::GetUniformLocation(program_, "uProj");
        uColor_ = gl::GetUniformLocation(program_, "uColor");
        uTex_ = gl::GetUniformLocation(program_, "uTex");

        gl::GenVertexArrays(1, &vao_);
        gl::GenBuffers(1, &vbo_);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 6, nullptr, GL_DYNAMIC_DRAW);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<const void*>(sizeof(float) * 2));
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
        return true;
    }

    // A texture on unit `unit` with its sampling (texture parameters: per texture, so set per draw)
    void bindTexture(unsigned unit, const Texture* tex, const Sampling& smp) {
        gl::ActiveTexture(GL_TEXTURE0 + unit);
        const bool textured = tex && tex->id;
        gl::BindTexture(GL_TEXTURE_2D, textured ? textureName(tex->id) : whiteId());
        if (!textured) return;
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                          smp.clampS ? GL_CLAMP_TO_EDGE : smp.mirrorS ? GL_MIRRORED_REPEAT : GL_REPEAT);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                          smp.clampT ? GL_CLAMP_TO_EDGE : smp.mirrorT ? GL_MIRRORED_REPEAT : GL_REPEAT);
        // Mipmapped (HD) textures blend between mips whatever the base filter
        const bool mips = mipmapped_.count(tex->id) != 0;
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                          mips ? (smp.minLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR)
                               : smp.minLinear ? GL_LINEAR : GL_NEAREST);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smp.magLinear ? GL_LINEAR : GL_NEAREST);
    }

    // A 1x1 white texture for untextured meshes
    unsigned whiteId() {
        if (!white_.id) {
            Image img;
            img.width = img.height = 1;
            img.rgba = {255, 255, 255, 255};
            white_ = upload(img);
        }
        return textureName(white_.id);
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    int uProj_ = -1, uColor_ = -1, uTex_ = -1;
    unsigned meshProgram_ = 0, ubo_ = 0;
    int uMvp_ = -1, uAlphaRef_ = -1, uMeshColor_ = -1, uTexMode_ = -1, uAlphaTex_ = -1, uLight_ = -1;
    int drawableW_ = 0, drawableH_ = 0;
    std::unordered_set<unsigned> mipmapped_; // textures uploaded with a mip chain
    TextureFiltering filtering_;
    // A mipmapped texture's filtering (bound): GL_TEXTURE_MAX_ANISOTROPY (GL 4.6 /
    // EXT_texture_filter_anisotropic; clamped to the GPU's limit), the nearest texel magnified
    void filter(unsigned id) {
        gl::BindTexture(GL_TEXTURE_2D, id);
        if (maxAnisotropy() > 1) gl::TexParameteri(GL_TEXTURE_2D, 0x84FE, std::clamp(filtering_.anisotropy, 1, maxAnisotropy()));
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering_.sharp ? GL_NEAREST : GL_LINEAR);
    }
    int anisoLimit_ = -1;
    Texture white_;
};

} // namespace

std::unique_ptr<RenderDevice> makeGlDevice() { return std::make_unique<GlDevice>(); }

const char* backendName(Backend b) { return b == Backend::Vulkan ? "Vulkan" : "OpenGL"; }

ShadingBlock shadingBlock(const MeshShading& sh, const SceneLighting& l) {
    ShadingBlock b{};
    auto put = [](float* dst, const float* src, int n, float w = 0) {
        for (int k = 0; k < n; ++k) dst[k] = src[k];
        if (n == 3) dst[3] = w;
    };
    put(b.diffuse, sh.color, 4);
    b.params[0] = sh.bumpiness;
    b.params[1] = sh.specularStrength;
    b.params[2] = sh.specularPower;
    b.params[3] = sh.alphaRef;
    b.velvet[0] = sh.rollOff;
    b.velvet[1] = sh.velvetStrength;
    b.velvet[2] = l.time;
    b.flags[0] = int32_t(sh.shade);
    put(b.surface, sh.surfaceColor, 3);
    put(b.fuzzy, sh.fuzzySpecColor, 3);
    put(b.sub, sh.subColor, 3);
    b.uvAnim[0] = sh.scrollSpeed[0];
    b.uvAnim[1] = sh.scrollSpeed[1];
    b.uvAnim[2] = sh.offset[0];
    b.uvAnim[3] = sh.offset[1];
    put(b.lightDir, l.lightDir, 3);
    put(b.lightColor, l.lightColor, 3, 1);
    put(b.ambient, l.ambient, 3);
    put(b.ground, l.ground, 3);
    put(b.skyDir, l.skyDir, 3);
    put(b.fogParams, l.fogParams, 3);
    put(b.fogColor, l.fogColor, 3);
    put(b.camera, l.camera, 3, 1);
    put(b.uvAnim2, sh.scrollSpeed2, 2);
    put(b.uvAnim2 + 2, sh.offset2, 2);
    b.strengths[0] = sh.overlayStrength;
    b.strengths[1] = sh.distortionStrength;
    put(b.pointPos, l.pointPos, 4);
    put(b.pointColor, l.pointColor, 3, 1);
    return b;
}

uint8_t blendCode(uint32_t f, bool dst) {
    for (uint8_t i = 0; i < (dst ? 8 : 9); ++i)
        if ((dst ? kBlendDst[i] : kBlendSrc[i]) == f) return i;
    return dst ? 5 : 4; // (ONE_MINUS_SRC_ALPHA / SRC_ALPHA)
}

} // namespace cr3

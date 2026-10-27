#pragma once

#include "engine/core/Math3D.h"
#include "engine/gfx/Mesh.h"
#include "engine/gfx/Shader.h"

#include <vector>

namespace eng {

struct Camera {
    Vec3 position{0, 2, 5};
    Vec3 target{0, 1, 0};
    float fovY = 60.0f * kPi / 180.0f;
    float nearZ = 0.1f;
    float farZ = 420.0f;

    Mat4 view() const { return Mat4::lookAt(position, target, {0, 1, 0}); }
    Mat4 projection(float aspect) const { return Mat4::perspective(fovY, aspect, nearZ, farZ); }
    Vec3 forward() const { return (target - position).normalized(); }
};

// Beleuchtung und Atmosphäre eines Frames. Farben sind linear (HDR).
struct Environment {
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f}; // Richtung, in die das Hauptlicht fällt
    Vec3 lightColor{2.5f, 2.3f, 2.0f};
    Vec3 skyAmbient{0.35f, 0.4f, 0.5f};
    Vec3 groundAmbient{0.15f, 0.13f, 0.1f};
    Vec3 skyTop{0.2f, 0.4f, 0.8f};
    Vec3 skyHorizon{0.7f, 0.75f, 0.85f};
    Vec3 skyGround{0.2f, 0.2f, 0.2f};
    Vec3 sunDirection{0.4f, 0.8f, 0.3f};  // Richtung zur Sonnenscheibe
    Vec3 sunColor{6.0f, 5.0f, 3.5f};
    float sunVisible = 1.0f;
    Vec3 moonDirection{-0.3f, 0.6f, -0.7f};
    float moonVisible = 0.0f;
    float stars = 0.0f;
    Vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogDensity = 0.006f;
    float fogHeightFalloff = 0.08f;
    float emissive = 0.0f; // Faktor für Eigenleuchten (z. B. Fenster)
    Vec3 emissiveColor{3.2f, 1.9f, 0.8f};
    float exposure = 1.0f;
    float bloomStrength = 0.6f;
    float vignette = 0.9f;
    float grain = 0.03f;
    float saturation = 1.0f;     // Farbsättigung (Color-Grading)
    Vec3 gradeTint{1, 1, 1};     // Farbstich (Color-Grading)
    float time = 0.0f;
    Vec3 shadowFocus{0, 0, 0};
    float shadowRadius = 45.0f;
    // Tiefenunschärfe: außerhalb des Fokuskreises (Bildkoordinaten 0..1) wird das Bild weich
    float focusBlur = 0.0f;
    Vec2 focusCenter{0.5f, 0.5f};
    float focusRadius = 0.25f;
};

struct PointLight {
    Vec3 position;
    float radius = 8.0f;
    Vec3 color{1.0f, 0.6f, 0.3f};
    float intensity = 2.0f;
};

// Vorwärts-Renderer mit Schattenkarte, Nebel, Himmel, MSAA, Partikeln und
// HDR-Nachbearbeitung (Bloom, Tonemapping, Vignette, Filmkorn).
class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();
    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    bool init();
    void shutdown();
    // Kantenglättung (1 = aus) und Auflösung der Schattenkarte
    void setQuality(int samples, int shadowSize);
    int samples() const { return m_samples; }
    int maxSamples() const { return m_maxSamples; }

    void beginFrame(int framebufferWidth, int framebufferHeight, const Camera& camera, const Environment& env);
    void draw(const Mesh& mesh, const Mat4& model, Color tint = {1, 1, 1, 1}, float glow = 0.0f,
              bool castShadow = true);
    void light(const PointLight& light);
    // Weiches, leuchtendes Partikel (additiv). color ist HDR.
    void particle(Vec3 position, float size, Vec3 color, float alpha = 1.0f);
    void endFrame();

    // Projiziert einen Weltpunkt auf den Bildschirm (Framebuffer-Pixel, oben links = 0,0).
    bool worldToScreen(Vec3 world, Vec2& screen) const;
    const Mat4& viewProjection() const { return m_viewProj; }

    int drawCalls() const { return m_statDrawCalls; }
    int triangles() const { return m_statTriangles; }

private:
    struct DrawItem {
        const Mesh* mesh;
        Mat4 model;
        Color tint;
        float glow;
        bool castShadow;
    };
    struct ParticleVertex {
        Vec3 position;
        float u, v;
        Vec4 color;
    };
    struct Target {
        unsigned fbo = 0;
        unsigned color = 0;      // Textur
        unsigned colorRb = 0;    // Renderbuffer (MSAA)
        unsigned depthRb = 0;
        unsigned depthTex = 0;
        int width = 0;
        int height = 0;
    };

    void ensureTargets(int width, int height);
    void createShadowMap(int size);
    void destroyTarget(Target& t);
    bool createColorTarget(Target& t, int width, int height, bool withDepth);
    bool createMsaaTarget(Target& t, int width, int height, int samples);
    void renderShadows();
    void renderScene();
    void renderParticles();
    void postProcess();
    void fullscreen();

    Shader m_lit;
    Shader m_depth;
    Shader m_sky;
    Shader m_particleShader;
    Shader m_bright;
    Shader m_blur;
    Shader m_composite;

    Target m_shadow;
    Target m_sceneMs;
    Target m_scene;
    Target m_bloomA[2];
    Target m_bloomB[2];
    Target m_dofA[2];
    Target m_dofB[2];
    int m_samples = 4;
    int m_maxSamples = 4;
    int m_shadowSize = 2048;
    unsigned m_emptyVao = 0;
    unsigned m_particleVao = 0;
    unsigned m_particleVbo = 0;

    Camera m_camera;
    Environment m_env;
    Mat4 m_view;
    Mat4 m_proj;
    Mat4 m_viewProj;
    Mat4 m_lightViewProj;
    Vec4 m_frustum[6];
    int m_width = 1;
    int m_height = 1;
    std::vector<DrawItem> m_items;
    std::vector<PointLight> m_lights;
    std::vector<ParticleVertex> m_particles;
    int m_statDrawCalls = 0;
    int m_statTriangles = 0;
};

} // namespace eng

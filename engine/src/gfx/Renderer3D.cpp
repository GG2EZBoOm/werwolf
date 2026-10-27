#include "engine/gfx/Renderer3D.h"

#include "engine/core/Log.h"
#include "engine/gfx/GL.h"

#include <algorithm>
#include <cstddef>

namespace eng {

namespace {

constexpr int kDefaultShadowSize = 2048;
constexpr int kMaxLights = 16;

// --- Shader ------------------------------------------------------------------

const char* kLitVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aGlow;
uniform mat4 uModel;
uniform mat4 uNormalMat;
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vColor;
out float vGlow;
out vec4 vLightPos;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(uNormalMat) * aNormal;
    vColor = aColor;
    vGlow = aGlow;
    vLightPos = uLightViewProj * wp;
    gl_Position = uViewProj * wp;
}
)";

const char* kLitFS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vColor;
in float vGlow;
in vec4 vLightPos;
uniform vec4 uTint;
uniform float uGlow;
uniform float uEmissive;
uniform vec3 uEmissiveColor;
uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform sampler2DShadow uShadowMap;
uniform float uShadowTexel;
uniform int uLightCount;
uniform vec4 uLightPos[16];
uniform vec4 uLightColor2[16];
out vec4 fragColor;

float sampleShadow(float ndl) {
    vec3 p = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float bias = mix(0.0022, 0.0006, ndl);
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += texture(uShadowMap, vec3(p.xy + vec2(x, y) * uShadowTexel, p.z - bias));
    return s / 9.0;
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 albedo = pow(vColor.rgb * uTint.rgb, vec3(2.2));
    float ndl = max(dot(n, -uLightDir), 0.0);
    float shadow = ndl > 0.0 ? sampleShadow(ndl) : 0.0;
    vec3 ambient = mix(uGroundAmbient, uSkyAmbient, n.y * 0.5 + 0.5);
    vec3 lighting = ambient + uLightColor * ndl * shadow;
    for (int i = 0; i < uLightCount; ++i) {
        vec3 d = uLightPos[i].xyz - vWorldPos;
        float dist = length(d);
        float r = uLightPos[i].w;
        if (dist < r) {
            float att = 1.0 - dist / r;
            att *= att;
            float diff = max(dot(n, d / max(dist, 0.001)), 0.0) * 0.8 + 0.2;
            lighting += uLightColor2[i].rgb * uLightColor2[i].w * att * diff;
        }
    }
    // Alpha der Tönung dämpft das Eigenleuchten (z. B. dunkle Fenster einer Ruine)
    vec3 color = albedo * lighting + albedo * uGlow * 3.0 + uEmissiveColor * vGlow * uEmissive * uTint.a;
    float dist = length(vWorldPos - uCameraPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    fog *= clamp(exp(-max(vWorldPos.y, 0.0) * uFogHeight), 0.3, 1.0);
    color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));
    fragColor = vec4(color, 1.0);
}
)";

const char* kDepthVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightMVP;
void main() { gl_Position = uLightMVP * vec4(aPos, 1.0); }
)";

const char* kDepthFS = R"(#version 330 core
void main() {}
)";

const char* kFullscreenVS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kSkyFS = R"(#version 330 core
in vec2 vUV;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunVisible;
uniform vec3 uMoonDir;
uniform float uMoonVisible;
uniform float uStars;
uniform float uTime;
out vec4 fragColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec4 far = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - uCameraPos);
    float h = dir.y;
    vec3 sky = mix(uSkyHorizon, uSkyTop, pow(clamp(h, 0.0, 1.0), 0.45));
    if (h < 0.0) sky = mix(uSkyHorizon, uSkyGround, clamp(-h * 5.0, 0.0, 1.0));

    // Sonne mit Hof
    float sd = max(dot(dir, uSunDir), 0.0);
    sky += uSunColor * uSunVisible * (smoothstep(0.9993, 0.9996, sd) * 8.0 + pow(sd, 12.0) * 0.18 + pow(sd, 2.0) * 0.04);

    // Mond: Scheibe, leichte Krater, Hof
    float md = dot(dir, uMoonDir);
    float disc = smoothstep(0.99935, 0.99955, md);
    float crater = hash(floor(dir * 900.0)) * 0.25;
    sky += vec3(1.6, 1.65, 1.8) * uMoonVisible * disc * (1.0 - crater);
    sky += vec3(0.25, 0.32, 0.5) * uMoonVisible * (pow(max(md, 0.0), 60.0) * 0.6 + pow(max(md, 0.0), 6.0) * 0.08);

    // Sterne
    if (uStars > 0.0 && h > 0.0) {
        vec3 p = dir * 260.0;
        vec3 cell = floor(p);
        float r = hash(cell);
        if (r > 0.985) {
            vec3 f = fract(p) - 0.5;
            float star = smoothstep(0.22, 0.0, length(f));
            float twinkle = 0.6 + 0.4 * sin(uTime * (1.5 + r * 6.0) + r * 100.0);
            sky += vec3(1.0, 0.95, 0.85) * star * twinkle * uStars * clamp(h * 4.0, 0.0, 1.0) * (r - 0.985) * 220.0;
        }
    }
    fragColor = vec4(sky, 1.0);
}
)";

const char* kParticleVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kParticleFS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float d = length(vUV * 2.0 - 1.0);
    float a = clamp(1.0 - d, 0.0, 1.0);
    a *= a;
    fragColor = vec4(vColor.rgb * a * vColor.a, 1.0);
}
)";

const char* kBrightFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uThreshold;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUV + uTexel * vec2(-1, -1)).rgb + texture(uTex, vUV + uTexel * vec2(1, -1)).rgb +
             texture(uTex, vUV + uTexel * vec2(-1, 1)).rgb + texture(uTex, vUV + uTexel * vec2(1, 1)).rgb;
    c *= 0.25;
    float l = max(c.r, max(c.g, c.b));
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
    fragColor = vec4(min(c * k, vec3(40.0)), 1.0);
}
)";

const char* kBlurFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uDir;
out vec4 fragColor;
void main() {
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    fragColor = vec4(c, 1.0);
}
)";

const char* kCompositeFS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom1;
uniform sampler2D uBloom2;
uniform sampler2D uDof1;
uniform sampler2D uDof2;
uniform float uFocusBlur;
uniform vec2 uFocusCenter;
uniform float uFocusRadius;
uniform float uAspect;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
uniform float uSaturation;
uniform vec3 uGradeTint;
out vec4 fragColor;
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    if (uFocusBlur > 0.0) {
        vec2 d = (vUV - uFocusCenter) * vec2(uAspect, 1.0);
        float k = smoothstep(uFocusRadius, uFocusRadius + 0.45, length(d)) * uFocusBlur;
        vec3 soft = mix(texture(uDof1, vUV).rgb, texture(uDof2, vUV).rgb, clamp(k * 1.4 - 0.2, 0.0, 1.0));
        c = mix(c, soft, clamp(k * 1.6, 0.0, 1.0));
    }
    vec3 bloom = texture(uBloom1, vUV).rgb * 0.55 + texture(uBloom2, vUV).rgb * 0.75;
    c += bloom * uBloomStrength;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, uSaturation) * uGradeTint;
    c = aces(c * uExposure);
    vec2 q = vUV - 0.5;
    c *= clamp(1.0 - dot(q, q) * uVignette, 0.0, 1.0);
    c = pow(c, vec3(1.0 / 2.2));
    float n = fract(sin(dot(vUV * 1000.0 + vec2(uTime * 37.0, uTime * 11.0), vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) * uGrain;
    fragColor = vec4(c, 1.0);
}
)";

Mat4 normalMatrix(const Mat4& model) {
    Mat4 m = model;
    m.m[12] = m.m[13] = m.m[14] = 0.0f;
    return m.inverse().transposed();
}

float maxScale(const Mat4& m) {
    const float sx = Vec3{m.m[0], m.m[1], m.m[2]}.length();
    const float sy = Vec3{m.m[4], m.m[5], m.m[6]}.length();
    const float sz = Vec3{m.m[8], m.m[9], m.m[10]}.length();
    return std::max(sx, std::max(sy, sz));
}

} // namespace

Renderer3D::~Renderer3D() { shutdown(); }

bool Renderer3D::init() {
    bool ok = m_lit.load(kLitVS, kLitFS, "lit");
    ok &= m_depth.load(kDepthVS, kDepthFS, "depth");
    ok &= m_sky.load(kFullscreenVS, kSkyFS, "sky");
    ok &= m_particleShader.load(kParticleVS, kParticleFS, "particle");
    ok &= m_bright.load(kFullscreenVS, kBrightFS, "bright");
    ok &= m_blur.load(kFullscreenVS, kBlurFS, "blur");
    ok &= m_composite.load(kFullscreenVS, kCompositeFS, "composite");
    if (!ok) return false;

    glGenVertexArrays(1, &m_emptyVao);

    glGenVertexArrays(1, &m_particleVao);
    glBindVertexArray(m_particleVao);
    glGenBuffers(1, &m_particleVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
    const GLsizei stride = sizeof(ParticleVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(ParticleVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(ParticleVertex, u)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(ParticleVertex, color)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    createShadowMap(kDefaultShadowSize);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_maxSamples = std::max(1, static_cast<int>(maxSamples));
    m_samples = std::min(4, m_maxSamples);
    return true;
}

void Renderer3D::setQuality(int samples, int shadowSize) {
    samples = std::clamp(samples, 1, m_maxSamples);
    if (samples != m_samples) {
        m_samples = samples;
        destroyTarget(m_scene); // erzwingt neue Render-Targets im nächsten Frame
        destroyTarget(m_sceneMs);
    }
    shadowSize = std::clamp(shadowSize, 512, 8192);
    if (shadowSize != m_shadowSize) createShadowMap(shadowSize);
}

void Renderer3D::createShadowMap(int size) {
    destroyTarget(m_shadow);
    m_shadowSize = size;
    // Schattenkarte (Tiefentextur mit Hardware-Vergleich)
    glGenTextures(1, &m_shadow.depthTex);
    glBindTexture(GL_TEXTURE_2D, m_shadow.depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &m_shadow.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadow.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow.depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) log::warn("Schatten-Framebuffer unvollständig");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_shadow.width = m_shadow.height = size;
}

void Renderer3D::shutdown() {
    destroyTarget(m_shadow);
    destroyTarget(m_sceneMs);
    destroyTarget(m_scene);
    for (int i = 0; i < 2; ++i) {
        destroyTarget(m_bloomA[i]);
        destroyTarget(m_bloomB[i]);
        destroyTarget(m_dofA[i]);
        destroyTarget(m_dofB[i]);
    }
    if (m_particleVbo) glDeleteBuffers(1, &m_particleVbo);
    if (m_particleVao) glDeleteVertexArrays(1, &m_particleVao);
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    m_particleVbo = m_particleVao = m_emptyVao = 0;
    m_lit.destroy();
    m_depth.destroy();
    m_sky.destroy();
    m_particleShader.destroy();
    m_bright.destroy();
    m_blur.destroy();
    m_composite.destroy();
}

// --- Render-Targets ----------------------------------------------------------

void Renderer3D::destroyTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.color) glDeleteTextures(1, &t.color);
    if (t.depthTex) glDeleteTextures(1, &t.depthTex);
    if (t.colorRb) glDeleteRenderbuffers(1, &t.colorRb);
    if (t.depthRb) glDeleteRenderbuffers(1, &t.depthRb);
    t = Target{};
}

bool Renderer3D::createColorTarget(Target& t, int width, int height, bool withDepth) {
    destroyTarget(t);
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    if (withDepth) {
        glGenRenderbuffers(1, &t.depthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depthRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthRb);
    }
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

bool Renderer3D::createMsaaTarget(Target& t, int width, int height, int samples) {
    destroyTarget(t);
    t.width = width;
    t.height = height;
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glGenRenderbuffers(1, &t.colorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, t.colorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t.colorRb);
    glGenRenderbuffers(1, &t.depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthRb);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void Renderer3D::ensureTargets(int width, int height) {
    if (m_scene.width == width && m_scene.height == height && m_scene.fbo) return;
    createColorTarget(m_scene, width, height, true);
    if (m_samples > 1 && !createMsaaTarget(m_sceneMs, width, height, m_samples)) {
        log::warn("MSAA nicht verfügbar – Kantenglättung deaktiviert");
        destroyTarget(m_sceneMs);
        m_samples = 1;
    }
    const int w2 = std::max(1, width / 2), h2 = std::max(1, height / 2);
    const int w4 = std::max(1, width / 4), h4 = std::max(1, height / 4);
    createColorTarget(m_bloomA[0], w2, h2, false);
    createColorTarget(m_bloomB[0], w2, h2, false);
    createColorTarget(m_bloomA[1], w4, h4, false);
    createColorTarget(m_bloomB[1], w4, h4, false);
    createColorTarget(m_dofA[0], w2, h2, false);
    createColorTarget(m_dofB[0], w2, h2, false);
    createColorTarget(m_dofA[1], w4, h4, false);
    createColorTarget(m_dofB[1], w4, h4, false);
}

// --- Frame -------------------------------------------------------------------

void Renderer3D::beginFrame(int framebufferWidth, int framebufferHeight, const Camera& camera, const Environment& env) {
    m_width = std::max(1, framebufferWidth);
    m_height = std::max(1, framebufferHeight);
    m_camera = camera;
    m_env = env;
    m_items.clear();
    m_lights.clear();
    m_particles.clear();
    m_view = camera.view();
    m_proj = camera.projection(static_cast<float>(m_width) / static_cast<float>(m_height));
    m_viewProj = m_proj * m_view;

    // Frustum-Ebenen (Gribb/Hartmann)
    auto row = [&](int i) { return Vec4{m_viewProj.m[i], m_viewProj.m[4 + i], m_viewProj.m[8 + i], m_viewProj.m[12 + i]}; };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const Vec4 planes[6] = {{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w},
                            {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},
                            {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w},
                            {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},
                            {r3.x + r2.x, r3.y + r2.y, r3.z + r2.z, r3.w + r2.w},
                            {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w}};
    for (int i = 0; i < 6; ++i) {
        const float len = planes[i].xyz().length();
        m_frustum[i] = {planes[i].x / len, planes[i].y / len, planes[i].z / len, planes[i].w / len};
    }
    ensureTargets(m_width, m_height);
}

void Renderer3D::draw(const Mesh& mesh, const Mat4& model, Color tint, float glow, bool castShadow) {
    if (!mesh.valid()) return;
    m_items.push_back(DrawItem{&mesh, model, tint, glow, castShadow});
}

void Renderer3D::light(const PointLight& l) { m_lights.push_back(l); }

void Renderer3D::particle(Vec3 p, float size, Vec3 color, float alpha) {
    const Vec3 right{m_view.m[0], m_view.m[4], m_view.m[8]};
    const Vec3 up{m_view.m[1], m_view.m[5], m_view.m[9]};
    const float h = size * 0.5f;
    const Vec4 c{color, alpha};
    const ParticleVertex a{p - right * h - up * h, 0, 0, c};
    const ParticleVertex b{p + right * h - up * h, 1, 0, c};
    const ParticleVertex d{p + right * h + up * h, 1, 1, c};
    const ParticleVertex e{p - right * h + up * h, 0, 1, c};
    m_particles.insert(m_particles.end(), {a, b, d, a, d, e});
}

bool Renderer3D::worldToScreen(Vec3 world, Vec2& screen) const {
    const Vec4 clip = m_viewProj * Vec4(world, 1.0f);
    if (clip.w <= 0.05f) return false;
    const float nx = clip.x / clip.w;
    const float ny = clip.y / clip.w;
    screen = {(nx * 0.5f + 0.5f) * static_cast<float>(m_width), (1.0f - (ny * 0.5f + 0.5f)) * static_cast<float>(m_height)};
    return nx > -1.2f && nx < 1.2f && ny > -1.2f && ny < 1.2f;
}

void Renderer3D::fullscreen() {
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer3D::renderShadows() {
    const Vec3 dir = m_env.lightDirection.normalized();
    const Vec3 focus = m_env.shadowFocus;
    const float R = m_env.shadowRadius;
    const Vec3 up = std::fabs(dir.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Mat4 view = Mat4::lookAt(focus - dir * 150.0f, focus, up);
    Mat4 proj = Mat4::orthographic(-R, R, -R, R, 1.0f, 320.0f);
    // An Texel-Raster ausrichten, damit Schatten beim Bewegen nicht flimmern
    const Vec4 origin = (proj * view) * Vec4(0, 0, 0, 1);
    const float half = static_cast<float>(m_shadowSize) * 0.5f;
    const float tx = origin.x * half, ty = origin.y * half;
    proj = Mat4::translation({(std::round(tx) - tx) / half, (std::round(ty) - ty) / half, 0}) * proj;
    m_lightViewProj = proj * view;

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadow.fbo);
    glViewport(0, 0, m_shadowSize, m_shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 3.0f);
    m_depth.bind();
    for (const DrawItem& item : m_items) {
        if (!item.castShadow) continue;
        const Vec3 c = item.model.transformPoint(item.mesh->bounds().center());
        const float r = item.mesh->bounds().radius() * maxScale(item.model);
        if (distanceXZ(c, focus) > R * 1.5f + r) continue;
        m_depth.set("uLightMVP", m_lightViewProj * item.model);
        item.mesh->draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer3D::renderScene() {
    const Target& target = m_samples > 1 ? m_sceneMs : m_scene;
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);

    // Himmel
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    m_sky.bind();
    m_sky.set("uInvViewProj", m_viewProj.inverse());
    m_sky.set("uCameraPos", m_camera.position);
    m_sky.set("uSkyTop", m_env.skyTop);
    m_sky.set("uSkyHorizon", m_env.skyHorizon);
    m_sky.set("uSkyGround", m_env.skyGround);
    m_sky.set("uSunDir", m_env.sunDirection.normalized());
    m_sky.set("uSunColor", m_env.sunColor);
    m_sky.set("uSunVisible", m_env.sunVisible);
    m_sky.set("uMoonDir", m_env.moonDirection.normalized());
    m_sky.set("uMoonVisible", m_env.moonVisible);
    m_sky.set("uStars", m_env.stars);
    m_sky.set("uTime", m_env.time);
    fullscreen();

    // Undurchsichtige Geometrie
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Die 16 nächstgelegenen Punktlichter
    std::sort(m_lights.begin(), m_lights.end(), [&](const PointLight& a, const PointLight& b) {
        return (a.position - m_camera.position).lengthSq() < (b.position - m_camera.position).lengthSq();
    });
    const int lightCount = std::min(kMaxLights, static_cast<int>(m_lights.size()));
    Vec4 lightPos[kMaxLights];
    Vec4 lightColor[kMaxLights];
    for (int i = 0; i < lightCount; ++i) {
        lightPos[i] = {m_lights[static_cast<size_t>(i)].position, m_lights[static_cast<size_t>(i)].radius};
        lightColor[i] = {m_lights[static_cast<size_t>(i)].color, m_lights[static_cast<size_t>(i)].intensity};
    }

    m_lit.bind();
    m_lit.set("uViewProj", m_viewProj);
    m_lit.set("uLightViewProj", m_lightViewProj);
    m_lit.set("uCameraPos", m_camera.position);
    m_lit.set("uLightDir", m_env.lightDirection.normalized());
    m_lit.set("uLightColor", m_env.lightColor);
    m_lit.set("uSkyAmbient", m_env.skyAmbient);
    m_lit.set("uGroundAmbient", m_env.groundAmbient);
    m_lit.set("uFogColor", m_env.fogColor);
    m_lit.set("uFogDensity", m_env.fogDensity);
    m_lit.set("uFogHeight", m_env.fogHeightFalloff);
    m_lit.set("uEmissive", m_env.emissive);
    m_lit.set("uEmissiveColor", m_env.emissiveColor);
    m_lit.set("uShadowTexel", 1.0f / static_cast<float>(m_shadowSize));
    m_lit.set("uShadowMap", 1);
    m_lit.set("uLightCount", lightCount);
    m_lit.setArray("uLightPos", lightPos, lightCount);
    m_lit.setArray("uLightColor2", lightColor, lightCount);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_shadow.depthTex);
    glActiveTexture(GL_TEXTURE0);

    for (const DrawItem& item : m_items) {
        const Vec3 c = item.model.transformPoint(item.mesh->bounds().center());
        const float r = item.mesh->bounds().radius() * maxScale(item.model);
        bool visible = true;
        for (const Vec4& p : m_frustum) {
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) {
                visible = false;
                break;
            }
        }
        if (!visible) continue;
        m_lit.set("uModel", item.model);
        m_lit.set("uNormalMat", normalMatrix(item.model));
        m_lit.set("uTint", Vec4{item.tint.r, item.tint.g, item.tint.b, item.tint.a});
        m_lit.set("uGlow", item.glow);
        item.mesh->draw();
        ++m_statDrawCalls;
        m_statTriangles += item.mesh->triangleCount();
    }
    glDisable(GL_CULL_FACE);
}

void Renderer3D::renderParticles() {
    if (m_particles.empty()) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    m_particleShader.bind();
    m_particleShader.set("uViewProj", m_viewProj);
    glBindVertexArray(m_particleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_particles.size() * sizeof(ParticleVertex)),
                 m_particles.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_particles.size()));
    ++m_statDrawCalls;
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer3D::postProcess() {
    if (m_samples > 1) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneMs.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_scene.fbo);
        glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    // Helle Bereiche extrahieren und weichzeichnen (zwei Stufen)
    auto blurPass = [&](Target& src, Target& tmp) {
        m_blur.bind();
        m_blur.set("uTex", 0);
        glBindFramebuffer(GL_FRAMEBUFFER, tmp.fbo);
        glViewport(0, 0, tmp.width, tmp.height);
        glBindTexture(GL_TEXTURE_2D, src.color);
        m_blur.set("uDir", Vec2{1.0f / static_cast<float>(src.width), 0.0f});
        fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, src.fbo);
        glBindTexture(GL_TEXTURE_2D, tmp.color);
        m_blur.set("uDir", Vec2{0.0f, 1.0f / static_cast<float>(src.height)});
        fullscreen();
    };

    glActiveTexture(GL_TEXTURE0);
    m_bright.bind();
    m_bright.set("uTex", 0);
    m_bright.set("uThreshold", 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomA[0].fbo);
    glViewport(0, 0, m_bloomA[0].width, m_bloomA[0].height);
    glBindTexture(GL_TEXTURE_2D, m_scene.color);
    m_bright.set("uTexel", Vec2{0.5f / static_cast<float>(m_width), 0.5f / static_cast<float>(m_height)});
    fullscreen();
    blurPass(m_bloomA[0], m_bloomB[0]);

    m_bright.bind();
    m_bright.set("uThreshold", 0.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomA[1].fbo);
    glViewport(0, 0, m_bloomA[1].width, m_bloomA[1].height);
    glBindTexture(GL_TEXTURE_2D, m_bloomA[0].color);
    m_bright.set("uTexel", Vec2{0.5f / static_cast<float>(m_bloomA[0].width), 0.5f / static_cast<float>(m_bloomA[0].height)});
    fullscreen();
    blurPass(m_bloomA[1], m_bloomB[1]);
    blurPass(m_bloomA[1], m_bloomB[1]);

    // Unscharfe Fassungen des ganzen Bildes für die Tiefenunschärfe
    if (m_env.focusBlur > 0.0f) {
        m_bright.bind();
        m_bright.set("uThreshold", 0.0f);
        glBindFramebuffer(GL_FRAMEBUFFER, m_dofA[0].fbo);
        glViewport(0, 0, m_dofA[0].width, m_dofA[0].height);
        glBindTexture(GL_TEXTURE_2D, m_scene.color);
        m_bright.set("uTexel", Vec2{0.5f / static_cast<float>(m_width), 0.5f / static_cast<float>(m_height)});
        fullscreen();
        blurPass(m_dofA[0], m_dofB[0]);
        blurPass(m_dofA[0], m_dofB[0]);
        m_bright.bind();
        glBindFramebuffer(GL_FRAMEBUFFER, m_dofA[1].fbo);
        glViewport(0, 0, m_dofA[1].width, m_dofA[1].height);
        glBindTexture(GL_TEXTURE_2D, m_dofA[0].color);
        m_bright.set("uTexel", Vec2{0.5f / static_cast<float>(m_dofA[0].width), 0.5f / static_cast<float>(m_dofA[0].height)});
        fullscreen();
        for (int i = 0; i < 3; ++i) blurPass(m_dofA[1], m_dofB[1]);
    }

    // Zusammensetzen ins Fenster
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    m_composite.bind();
    m_composite.set("uScene", 0);
    m_composite.set("uBloom1", 1);
    m_composite.set("uBloom2", 2);
    m_composite.set("uExposure", m_env.exposure);
    m_composite.set("uBloomStrength", m_env.bloomStrength);
    m_composite.set("uVignette", m_env.vignette);
    m_composite.set("uGrain", m_env.grain);
    m_composite.set("uTime", m_env.time);
    m_composite.set("uSaturation", m_env.saturation);
    m_composite.set("uGradeTint", m_env.gradeTint);
    m_composite.set("uDof1", 3);
    m_composite.set("uDof2", 4);
    m_composite.set("uFocusBlur", m_env.focusBlur);
    m_composite.set("uFocusCenter", m_env.focusCenter);
    m_composite.set("uFocusRadius", m_env.focusRadius);
    m_composite.set("uAspect", static_cast<float>(m_width) / static_cast<float>(std::max(1, m_height)));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_dofA[0].color);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_dofA[1].color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scene.color);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_bloomA[0].color);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_bloomA[1].color);
    fullscreen();
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer3D::endFrame() {
    m_statDrawCalls = 0;
    m_statTriangles = 0;
    renderShadows();
    renderScene();
    renderParticles();
    postProcess();
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

} // namespace eng

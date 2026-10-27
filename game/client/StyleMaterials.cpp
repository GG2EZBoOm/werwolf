// Materialien und Tinten-Bedienelemente: Holztafel, Pergament, Schiefer, Wachs, Eisen.
#include "client/Style.h"

#include "engine/core/Utf8.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::Affine2;
using eng::Color;
using eng::Rect;
using eng::Vec2;
using eng::Vec3;
using namespace palette;

namespace {

uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float rand01(uint32_t seed, uint32_t i) { return static_cast<float>(mix32(seed * 0x9E3779B9u + i * 0x85EBCA6Bu) & 0xFFFFFF) / 16777216.0f; }

// Periodisches Wertrauschen für kachelbare Texturen
float lattice(int x, int y, int px, int py, uint32_t seed) {
    x = ((x % px) + px) % px;
    y = ((y % py) + py) % py;
    return rand01(seed, static_cast<uint32_t>(x * 7919 + y * 104729));
}

float valueNoise(float x, float y, int px, int py, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = lattice(ix, iy, px, py, seed), b = lattice(ix + 1, iy, px, py, seed);
    const float c = lattice(ix, iy + 1, px, py, seed), d = lattice(ix + 1, iy + 1, px, py, seed);
    const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

float fbm(float u, float v, int baseX, int baseY, int octaves, uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        const int px = baseX << o, py = baseY << o;
        sum += valueNoise(u * static_cast<float>(px), v * static_cast<float>(py), px, py, seed + static_cast<uint32_t>(o) * 31u) * amp;
        norm += amp;
        amp *= 0.5f;
    }
    return sum / norm;
}

Color mixC(Color a, Color b, float t) { return eng::lerp(a, b, std::clamp(t, 0.0f, 1.0f)); }

void put(std::vector<uint8_t>& px, int w, int x, int y, Color c, float alpha = 1.0f) {
    uint8_t* p = &px[static_cast<size_t>((y * w + x) * 4)];
    p[0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
    p[1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
    p[2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    p[3] = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

float smooth01(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

// --- Texturen --------------------------------------------------------------------

void UiKit::buildMaterials() {
    // Pergament: fleckig, mit Fasern, Faltkniffen und angesengten Kanten; Alpha = Relief (nicht kachelbar)
    {
        constexpr int N = 1024;
        std::vector<uint8_t> px(N * N * 4);
        struct Stain {
            float x, y, r, s;
        };
        std::vector<Stain> stains;
        for (uint32_t k = 0; k < 7; ++k)
            stains.push_back({rand01(11, k), rand01(12, k), 0.05f + rand01(13, k) * 0.14f, 0.02f + rand01(14, k) * 0.04f});
        const Color light = Color::hex(0xEEDFBA), mid = Color::hex(0xDCC595), dark = Color::hex(0xB89A62), burnt = Color::hex(0x5E3E1E);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const float u = static_cast<float>(x) / N, v = static_cast<float>(y) / N;
                const float n = fbm(u, v, 4, 4, 5, 101u);
                const float fine = fbm(u, v, 64, 64, 2, 202u);
                Color c = mixC(light, mid, n * 1.3f - 0.2f);
                c = mixC(c, dark, (fine - 0.5f) * 0.35f + 0.05f);
                // Fasern: feine, waagrechte Streifen
                const float fiber = valueNoise(u * 6.0f, v * 220.0f, 6, 220, 303u);
                c = mixC(c, dark, smooth01(0.82f, 0.98f, fiber) * 0.25f);
                // Altersflecken: weich und unregelmäßig, ohne scharfe Ränder
                for (const Stain& s : stains) {
                    const float warp = (fbm(u, v, 6, 6, 3, 505u + static_cast<uint32_t>(s.x * 1000.0f)) - 0.5f) * 0.6f;
                    const float d = std::hypot(u - s.x, v - s.y) / s.r + warp;
                    if (d < 1.0f) c = mixC(c, dark, smooth01(1.0f, 0.0f, d) * s.s * 1.6f);
                }
                // angesengte Kanten
                const float e = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
                const float wobble = (fbm(u, v, 8, 8, 3, 404u) - 0.5f) * 0.05f;
                c = mixC(c, dark, smooth01(0.12f, 0.0f, e + wobble) * 0.6f);
                c = mixC(c, burnt, smooth01(0.035f, 0.0f, e + wobble * 0.6f) * 0.85f);
                // Relief: Knitter, Fasern und zwei Faltkniffe (ein Brief, der gefaltet war)
                float height = 0.62f + (fbm(u, v, 5, 5, 4, 606u) - 0.5f) * 0.2f + (fine - 0.5f) * 0.03f + (fiber - 0.5f) * 0.004f;
                const float foldV = std::fabs(u - 0.5f + (fbm(0.3f, v, 1, 8, 2, 707u) - 0.5f) * 0.01f);
                const float foldH = std::fabs(v - 0.47f + (fbm(u, 0.6f, 8, 1, 2, 717u) - 0.5f) * 0.01f);
                height -= std::exp(-foldV * foldV / 0.000018f) * 0.1f; // Tal
                height += std::exp(-foldH * foldH / 0.000025f) * 0.07f; // Grat
                height += (0.5f - foldV) * 0.08f + (0.5f - foldH) * 0.05f; // die Hälften wölben sich leicht
                height -= smooth01(0.05f, 0.0f, e + wobble) * 0.25f;     // eingerollte Ränder
                put(px, N, x, y, c, std::clamp(height, 0.0f, 1.0f));
            }
        }
        m_parchment = eng::Renderer2D::createTexture(N, N, px.data(), false);
    }
    // Holz: waagrechte Bretter mit Maserung und Astlöchern; Alpha = Relief (waagrecht kachelbar)
    {
        constexpr int W = 1024, H = 512, plank = 128;
        std::vector<uint8_t> px(W * H * 4);
        const Color dark = Color::hex(0x24170E), mid = Color::hex(0x3E2918), light = Color::hex(0x5C3E25);
        for (int y = 0; y < H; ++y) {
            const int p = y / plank;
            const float local = static_cast<float>(y % plank) / plank;
            const float plankTone = rand01(55, static_cast<uint32_t>(p)) * 0.35f;
            for (int x = 0; x < W; ++x) {
                const float u = static_cast<float>(x) / W;
                const float warp = fbm(u, local * 0.25f + static_cast<float>(p), 3, 1, 4, 77u + static_cast<uint32_t>(p));
                const float grain = std::sin((local * 9.0f + warp * 6.0f) * eng::kPi);
                Color c = mixC(mid, light, plankTone + grain * 0.12f + (warp - 0.5f) * 0.4f);
                c = mixC(c, dark, smooth01(0.6f, 1.0f, std::fabs(grain)) * 0.35f);
                // Astloch
                const float kx = rand01(66, static_cast<uint32_t>(p)), ky = 0.5f;
                const float dx = std::fabs(u - kx), dxw = std::min(dx, 1.0f - dx) * 7.0f, dy = (local - ky) * 2.2f;
                const float knot = std::hypot(dxw, dy);
                c = mixC(c, dark, smooth01(0.35f, 0.0f, knot) * 0.8f);
                // Fugen
                const float seam = smooth01(0.06f, 0.0f, local) + smooth01(0.95f, 1.0f, local);
                c = mixC(c, dark, seam);
                // Relief: Jahresringe als Rillen, Astloch als Wulst, tiefe Fugen, abgerundete Kanten
                float height = 0.6f + grain * 0.07f - smooth01(0.6f, 1.0f, std::fabs(grain)) * 0.12f + (warp - 0.5f) * 0.1f;
                height += smooth01(0.4f, 0.1f, knot) * 0.12f - smooth01(0.12f, 0.0f, knot) * 0.2f;
                height -= std::pow(std::clamp(seam, 0.0f, 1.0f), 0.7f) * 0.5f;
                height -= smooth01(0.15f, 0.0f, std::min(local, 1.0f - local)) * 0.12f;
                put(px, W, x, y, c, std::clamp(height, 0.0f, 1.0f));
            }
        }
        m_wood = eng::Renderer2D::createTexture(W, H, px.data(), true);
    }
    // Schiefer: dunkel, leicht verwischt (kachelbar)
    {
        constexpr int N = 256;
        std::vector<uint8_t> px(N * N * 4);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = static_cast<float>(x) / N, v = static_cast<float>(y) / N;
                const float n = fbm(u, v, 4, 4, 5, 909u);
                const float smear = fbm(u, v * 0.3f, 2, 1, 3, 919u);
                Color c = mixC(Color::hex(0x23272A), Color::hex(0x31373A), n);
                c = mixC(c, Color::hex(0x464C4E), smooth01(0.55f, 0.85f, smear) * 0.35f); // Kreidereste
                put(px, N, x, y, c);
            }
        m_slateTex = eng::Renderer2D::createTexture(N, N, px.data(), true);
    }
}

void UiKit::destroyMaterials() {
    eng::Renderer2D::destroyTexture(m_parchment);
    eng::Renderer2D::destroyTexture(m_wood);
    eng::Renderer2D::destroyTexture(m_slateTex);
    m_parchment = m_wood = m_slateTex = 0;
}

// --- Materialien ------------------------------------------------------------------

void UiKit::light(Vec2 p, float strength, float seed) {
    const float f = flicker(seed);
    const Vec2 jitter{std::sin(static_cast<float>(m_time) * 5.1f + seed) * 3.0f, std::cos(static_cast<float>(m_time) * 4.3f + seed) * 2.0f};
    m_r->setLight(p + jitter, 260.0f, Vec3{0.92f, 0.76f, 0.56f} * (strength * f), Vec3{0.56f, 0.54f, 0.52f}, 1000.0f);
}

float UiKit::flicker(float seed) const {
    const float t = static_cast<float>(m_time);
    return 0.82f + 0.1f * std::sin(t * 7.3f + seed) * std::sin(t * 3.1f + seed * 2.0f) + 0.08f * std::sin(t * 17.0f + seed * 5.0f);
}

void UiKit::candleLight(Vec2 p, float radius, float strength, float seed) {
    eng::Renderer2D& r = *m_r;
    r.setBlendMode(eng::BlendMode::Additive);
    const float f = flicker(seed);
    r.glow(p, radius, Color::hex(0xFF9B45, 0.16f * strength * f));
    r.glow(p, radius * 0.45f, Color::hex(0xFFC27A, 0.12f * strength * f));
    r.setBlendMode(eng::BlendMode::Alpha);
}

void UiKit::board(const Rect& rc, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.rect(rc.offset(6, 10), Color::hex(0x000000, 0.45f * alpha));
    r.softRect(rc.offset(8, 14), 4.0f, 18.0f, Color::hex(0x000000, 0.55f * alpha));
    const float s = 1.0f / 1024.0f;
    r.litImage(m_wood, rc, {rc.x * s, 0.0f}, {rc.right() * s, rc.h / 512.0f * 0.9f}, Color{1, 1, 1, alpha}, 22.0f);
    // Rahmenleisten und Schatten nach innen
    const float b = 14.0f;
    const Color frameC = Color::hex(0x1A100A, 0.95f * alpha);
    r.gradientV({rc.x, rc.y, rc.w, b}, frameC, Color::hex(0x1A100A, 0.0f));
    r.gradientV({rc.x, rc.bottom() - b, rc.w, b}, Color::hex(0x1A100A, 0.0f), frameC);
    r.gradientH({rc.x, rc.y, b, rc.h}, frameC, Color::hex(0x1A100A, 0.0f));
    r.gradientH({rc.right() - b, rc.y, b, rc.h}, Color::hex(0x1A100A, 0.0f), frameC);
    r.rect({rc.x, rc.y, rc.w, 2.0f}, Color::hex(0x8A6440, 0.35f * alpha)); // Lichtkante
    for (Vec2 p : {Vec2{rc.x + 12, rc.y + 12}, Vec2{rc.right() - 12, rc.y + 12}, Vec2{rc.x + 12, rc.bottom() - 12},
                   Vec2{rc.right() - 12, rc.bottom() - 12}})
        nail(p, alpha);
}

void UiKit::slate(const Rect& rc, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.rect(rc.offset(4, 6), Color::hex(0x000000, 0.4f * alpha));
    const float fw = 9.0f;
    r.litImage(m_wood, rc, {rc.x / 1024.0f, 0.1f}, {rc.right() / 1024.0f, 0.3f}, Color{1, 1, 1, alpha}, 22.0f);
    const Rect inner = rc.shrink(fw);
    const float s = 1.0f / 256.0f;
    r.image(m_slateTex, inner, {inner.x * s, inner.y * s}, {inner.right() * s, inner.bottom() * s}, Color{1, 1, 1, 0.94f * alpha});
    r.roundRectOutline(inner, 0.0f, 1.0f, Color::hex(0x000000, 0.6f * alpha));
}

void UiKit::paperDisc(Vec2 c, float radius, uint32_t seed, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.softRect({c.x - radius + 4.0f, c.y - radius + 7.0f, radius * 2.0f, radius * 2.0f}, radius, 9.0f, Color::hex(0x000000, 0.5f * alpha));
    constexpr int N = 56;
    Vec2 pts[N];
    for (int i = 0; i < N; ++i) {
        const float a = static_cast<float>(i) / N * 2.0f * eng::kPi;
        const float rr = radius * (1.0f - rand01(seed, static_cast<uint32_t>(i)) * 0.02f);
        pts[i] = c + Vec2{std::cos(a) * rr, std::sin(a) * rr};
    }
    auto uv = [&](Vec2 p) { return Vec2{0.15f + (p.x - c.x + radius) / (radius * 2.0f) * 0.7f, 0.15f + (p.y - c.y + radius) / (radius * 2.0f) * 0.7f}; };
    for (int i = 0; i < N; ++i)
        r.litTriangle(m_parchment, c, pts[i], pts[(i + 1) % N], uv(c), uv(pts[i]), uv(pts[(i + 1) % N]), Color{1, 1, 1, alpha}, 9.0f);
}

void UiKit::nail(Vec2 p, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.circle(p + Vec2{1.5f, 2.5f}, 4.6f, Color::hex(0x000000, 0.45f * alpha));
    r.circle(p, 4.2f, iron.withAlpha(alpha));
    r.circle(p + Vec2{-1.2f, -1.2f}, 1.8f, Color::hex(0x8C857A, 0.75f * alpha));
}

void UiKit::paper(const Rect& rc, uint32_t seed, float alpha, Color tint) {
    eng::Renderer2D& r = *m_r;
    // Umriss: an allen Kanten leicht ausgefranst
    std::vector<Vec2> pts;
    auto edge = [&](Vec2 a, Vec2 b, Vec2 inward, uint32_t side) {
        const float len = (b - a).length();
        const int steps = std::max(2, static_cast<int>(len / 18.0f));
        for (int i = 0; i < steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float j = rand01(seed + side * 977u, static_cast<uint32_t>(i)) * 3.2f;
            pts.push_back(a + (b - a) * t + inward * j);
        }
    };
    edge({rc.x, rc.y}, {rc.right(), rc.y}, {0, 1}, 1);
    edge({rc.right(), rc.y}, {rc.right(), rc.bottom()}, {-1, 0}, 2);
    edge({rc.right(), rc.bottom()}, {rc.x, rc.bottom()}, {0, -1}, 3);
    edge({rc.x, rc.bottom()}, {rc.x, rc.y}, {1, 0}, 4);
    const Vec2 c = rc.center();
    // Weicher Schatten plus dunkler Kontaktschatten direkt am Rand
    r.softRect(rc.offset(4.0f + 8.0f * m_lift, 7.0f + 12.0f * m_lift), 3.0f, 9.0f + 10.0f * m_lift, Color::hex(0x000000, (0.5f - 0.12f * m_lift) * alpha));
    r.softRect(rc.offset(1.0f, 2.0f), 2.0f, 2.5f, Color::hex(0x000000, 0.3f * alpha * (1.0f - m_lift)));
    // Ausschnitt der Pergamenttextur: jeder Zettel zeigt eine andere Stelle
    const float ux = rand01(seed, 91) * 0.25f, uy = rand01(seed, 92) * 0.25f;
    auto uv = [&](Vec2 p) { return Vec2{ux + (p.x - rc.x) / rc.w * 0.75f, uy + (p.y - rc.y) / rc.h * 0.75f}; };
    const Color t = Color{tint.r, tint.g, tint.b, tint.a * alpha};
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2& a = pts[i];
        const Vec2& b = pts[(i + 1) % pts.size()];
        r.litTriangle(m_parchment, c, a, b, uv(c), uv(a), uv(b), t, 9.0f);
    }
}

void UiKit::seal(Vec2 c, float radius, Color waxColor, std::string_view glyph, float alpha, uint32_t seed) {
    eng::Renderer2D& r = *m_r;
    constexpr int N = 22;
    Vec2 pts[N];
    for (int i = 0; i < N; ++i) {
        const float a = static_cast<float>(i) / N * 2.0f * eng::kPi;
        const float rr = radius * (1.0f + (rand01(seed, static_cast<uint32_t>(i)) - 0.5f) * 0.22f);
        pts[i] = c + Vec2{std::cos(a) * rr, std::sin(a) * rr};
    }
    for (int i = 0; i < N; ++i) r.triangle(c + Vec2{2, 3}, pts[i] + Vec2{2, 3}, pts[(i + 1) % N] + Vec2{2, 3}, Color::hex(0x000000, 0.35f * alpha));
    const Color lightWax = mixC(waxColor, Color::hex(0xFFFFFF), 0.18f).withAlpha(alpha);
    const Color darkWax = mixC(waxColor, Color::hex(0x000000), 0.35f).withAlpha(alpha);
    for (int i = 0; i < N; ++i) {
        const Vec2 a = pts[i], b = pts[(i + 1) % N];
        const float shadeA = 0.5f + 0.5f * (-(a - c).normalized().x - (a - c).normalized().y) * 0.7f;
        const float shadeB = 0.5f + 0.5f * (-(b - c).normalized().x - (b - c).normalized().y) * 0.7f;
        r.triangleColors(c, a, b, waxColor.withAlpha(alpha), mixC(darkWax, lightWax, shadeA), mixC(darkWax, lightWax, shadeB));
    }
    r.circleOutline(c, radius * 0.68f, 1.5f, darkWax.withAlpha(0.8f * alpha));
    r.circleOutline(c + Vec2{0.6f, 0.8f}, radius * 0.68f, 1.0f, lightWax.withAlpha(0.35f * alpha));
    if (!glyph.empty()) {
        eng::Font& f = radius > 26.0f ? m_fonts->iconLarge : m_fonts->icon;
        const Rect gr{c.x - radius, c.y - radius, radius * 2, radius * 2};
        r.textCentered(f, gr.offset(0.8f, 1.0f), glyph, lightWax.withAlpha(0.45f * alpha));
        r.textCentered(f, gr, glyph, darkWax.withAlpha(0.95f * alpha));
    }
}

void UiKit::claws(Vec2 a, Vec2 b, float spread, float progress, Color c) {
    if (progress <= 0.0f) return;
    eng::Renderer2D& r = *m_r;
    const Vec2 dir = (b - a).normalized();
    const Vec2 nrm{-dir.y, dir.x};
    const float len = (b - a).length();
    for (int k = -1; k <= 1; ++k) {
        const float delay = static_cast<float>(k + 1) * 0.12f;
        const float p = std::clamp((progress - delay) / (1.0f - 0.24f), 0.0f, 1.0f);
        if (p <= 0.0f) continue;
        const Vec2 start = a + nrm * (static_cast<float>(k) * spread) + dir * (std::fabs(static_cast<float>(k)) * len * 0.06f);
        const float l = len * (k == 0 ? 1.0f : 0.86f) * p;
        // Spindelförmiger Riss: in der Mitte breit, an den Enden spitz
        constexpr int S = 10;
        for (int i = 0; i < S; ++i) {
            const float t0 = static_cast<float>(i) / S, t1 = static_cast<float>(i + 1) / S;
            const float w0 = std::sin(t0 * eng::kPi) * spread * 0.22f, w1 = std::sin(t1 * eng::kPi) * spread * 0.22f;
            const Vec2 p0 = start + dir * (l * t0), p1 = start + dir * (l * t1);
            r.triangle(p0 + nrm * w0, p1 + nrm * w1, p1 - nrm * w1, c);
            r.triangle(p0 + nrm * w0, p1 - nrm * w1, p0 - nrm * w0, c);
        }
    }
}

void UiKit::inkLine(Vec2 a, Vec2 b, float thickness, Color c, float progress, uint32_t seed) {
    if (progress <= 0.0f) return;
    eng::Renderer2D& r = *m_r;
    const Vec2 d = b - a;
    const Vec2 nrm = Vec2{-d.y, d.x}.normalized();
    const int steps = std::max(4, static_cast<int>(d.length() / 12.0f));
    Vec2 prev = a;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        if (t > progress + 1e-3f) break;
        const float wobble = (rand01(seed, static_cast<uint32_t>(i)) - 0.5f) * 1.6f;
        const Vec2 p = a + d * t + nrm * wobble;
        const float th = thickness * (0.75f + 0.5f * std::sin(t * eng::kPi));
        r.line(prev, p, th, c);
        prev = p;
    }
}

void UiKit::swash(Vec2 left, float width, Color c, float progress, uint32_t seed) {
    if (progress <= 0.0f) return;
    eng::Renderer2D& r = *m_r;
    constexpr int S = 24;
    Vec2 prev = left;
    for (int i = 1; i <= S; ++i) {
        const float t = static_cast<float>(i) / S;
        if (t > progress) break;
        const float y = std::sin(t * eng::kPi * 1.15f + 0.3f) * 3.0f - t * 2.0f + (rand01(seed, static_cast<uint32_t>(i)) - 0.5f) * 0.6f;
        const Vec2 p{left.x + width * t, left.y + y};
        r.line(prev, p, 2.6f * std::sin(t * eng::kPi) + 0.6f, c);
        prev = p;
    }
}

void UiKit::beginTilt(const Rect& rc, float angle, float scale) { m_r->pushTransform(Affine2::around(rc.center(), angle, scale)); }

void UiKit::endTilt() { m_r->popTransform(); }

void UiKit::writeText(eng::Font& font, Vec2 pos, std::string_view text, Color c, float progress) {
    if (progress <= 0.0f) return;
    // Zeichen für Zeichen sichtbar werden lassen; das jüngste Zeichen blendet weich ein
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        eng::utf8::next(text, i);
        ++count;
    }
    const float shown = progress * static_cast<float>(count);
    float x = pos.x;
    size_t index = 0;
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        eng::utf8::next(text, i);
        const std::string_view ch = text.substr(start, i - start);
        const float a = std::clamp(shown - static_cast<float>(index), 0.0f, 1.0f);
        if (a <= 0.0f) break;
        m_r->text(font, {x, pos.y}, ch, c.withAlpha(c.a * a));
        x += font.measure(ch);
        ++index;
    }
}

// --- Tinte auf Papier ----------------------------------------------------------------

bool UiKit::slip(std::string_view id, const Rect& rc, std::string_view text, float angle, bool selected, uint32_t seed, float appear) {
    eng::Renderer2D& r = *m_r;
    const bool hover = m_ui->hovered(rc) && appear > 0.9f;
    const bool clicked = appear > 0.9f && m_ui->clickArea(id, rc, true);
    const float h = anim(id, hover || selected ? 1.0f : 0.0f, 9.0f);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    if (clicked) burst(m_ui->input().mouse, Color::hex(0xE8D6A8), 16);

    // Zettel fällt beim Erscheinen leicht schwingend an seinen Platz
    const float drop = (1.0f - appear) * -40.0f;
    const float swing = (1.0f - appear) * 0.25f * std::sin(appear * 9.0f);
    const float tilt = angle * (1.0f - 0.85f * h) + swing;
    const float scale = 1.0f + 0.05f * h;
    Rect shown = rc.offset(0, drop - 3.0f * h);
    beginTilt(shown, tilt, scale);
    m_lift = h;
    paper(shown, seed, appear, mixC(Color{0.9f, 0.88f, 0.84f, 1}, Color{1, 1, 1, 1}, 0.55f + 0.45f * h));
    m_lift = 0.0f;
    // Nagel oben, bei Auswahl ein roter Wachstropfen
    const Vec2 pin{shown.x + 22.0f, shown.center().y};
    if (selected) seal(pin, 9.0f, wax, "", appear, seed + 5);
    else nail(pin, appear);
    eng::Font& font = m_fonts->scriptBold;
    const float tx = shown.x + 44.0f + 6.0f * h;
    const float ty = shown.center().y - font.lineHeight() * 0.5f;
    r.text(font, {tx, ty}, text, mixC(inkSoft, inkDark, h).withAlpha(appear));
    // Rote Unterstreichung wird gezogen, wenn man darüberfährt
    swash({tx, ty + font.lineHeight() * 0.86f}, std::min(font.measure(text) + 10.0f, shown.right() - tx - 10.0f), inkRed.withAlpha(0.85f * appear),
          h, seed);
    endTilt();
    return clicked;
}

bool UiKit::inkButton(std::string_view id, const Rect& rc, std::string_view text, bool primary, bool enabled) {
    eng::Renderer2D& r = *m_r;
    const bool hover = enabled && m_ui->hovered(rc);
    const bool clicked = m_ui->clickArea(id, rc, enabled);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    const float h = anim(id, hover ? 1.0f : 0.0f, 12.0f);
    if (clicked) burst(m_ui->input().mouse, primary ? wax : inkSoft, 12);
    // Zu lange Beschriftungen bekommen die kleinere Handschrift
    const float room = primary ? rc.w - rc.h - 14.0f : rc.w - 18.0f;
    eng::Font& font = m_fonts->scriptBold.measure(text) <= room ? m_fonts->scriptBold : m_fonts->script;
    if (primary) {
        // Wachssiegel mit Beschriftung daneben
        const float sr = rc.h * 0.5f + 2.0f * h;
        seal({rc.x + rc.h * 0.5f, rc.center().y}, sr, enabled ? wax : Color::hex(0x6A5A50), "✦", enabled ? 1.0f : 0.5f, 3);
        r.text(font, {rc.x + rc.h + 10.0f + 3.0f * h, rc.center().y - font.lineHeight() * 0.5f}, text,
               (enabled ? mixC(inkDark, inkRed, h) : inkFaded));
    } else {
        // gestempelter Rahmen
        const Color c = enabled ? mixC(inkSoft, inkRed, h) : inkFaded.withAlpha(0.6f);
        inkLine({rc.x, rc.y}, {rc.right(), rc.y + 1.0f}, 1.4f, c, 1.0f, 11);
        inkLine({rc.right(), rc.y + 1.0f}, {rc.right() - 1.0f, rc.bottom()}, 1.4f, c, 1.0f, 12);
        inkLine({rc.right() - 1.0f, rc.bottom()}, {rc.x + 1.0f, rc.bottom() - 1.0f}, 1.4f, c, 1.0f, 13);
        inkLine({rc.x + 1.0f, rc.bottom() - 1.0f}, {rc.x, rc.y}, 1.4f, c, 1.0f, 14);
        if (h > 0.01f) r.rect(rc.shrink(3.0f), inkRed.withAlpha(0.06f * h));
        const float w = font.measure(text);
        r.text(font, {rc.center().x - w * 0.5f, rc.center().y - font.lineHeight() * 0.5f}, text, c);
    }
    return clicked;
}

bool UiKit::inkTab(std::string_view id, const Rect& rc, std::string_view text, bool active, Color ribbon) {
    eng::Renderer2D& r = *m_r;
    const bool hover = m_ui->hovered(rc);
    const bool clicked = m_ui->clickArea(id, rc, true);
    if (hover && !active) m_ui->setCursor(eng::Cursor::Hand);
    const float h = anim(id, active ? 1.0f : (hover ? 0.45f : 0.0f), 10.0f);
    // Lesebändchen, das über die Seite fällt; das aktive hängt tiefer
    const float len = rc.h + 10.0f * h;
    const Rect band{rc.x + 6.0f, rc.y, rc.w - 12.0f, len};
    r.rect(band.offset(2, 3), Color::hex(0x000000, 0.25f));
    r.gradientH(band, mixC(ribbon, Color::hex(0x000000), 0.25f), mixC(ribbon, Color::hex(0xFFFFFF), 0.08f));
    r.triangle({band.x, band.bottom()}, {band.center().x, band.bottom() - 9.0f}, {band.x, band.bottom() + 0.5f}, Color::hex(0x000000, 0.0f));
    // Schwalbenschwanz
    const Color under = mixC(ribbon, Color::hex(0x000000), 0.25f);
    r.triangle({band.x, band.bottom()}, {band.right(), band.bottom()}, {band.center().x, band.bottom() - 8.0f}, under);
    eng::Font& font = m_fonts->script;
    const float w = font.measure(text);
    r.text(font, {band.center().x - w * 0.5f, band.y + 6.0f + 4.0f * h}, text, Color::hex(0xF3E6C6, 0.65f + 0.35f * h));
    return clicked;
}

bool UiKit::inkChoice(std::string_view id, const Rect& rc, const std::vector<std::string>& options, int& index) {
    if (options.empty()) return false;
    eng::Renderer2D& r = *m_r;
    const int n = static_cast<int>(options.size());
    index = std::clamp(index, 0, n - 1);
    const std::string base(id);
    const Rect left{rc.x, rc.y, 26.0f, rc.h}, right{rc.right() - 26.0f, rc.y, 26.0f, rc.h};
    const Rect middle{left.right(), rc.y, rc.w - 52.0f, rc.h};
    bool changed = false;
    if (m_ui->clickArea(base + "/l", left, index > 0)) {
        --index;
        changed = true;
    }
    if (m_ui->clickArea(base + "/r", right, index < n - 1)) {
        ++index;
        changed = true;
    }
    if (m_ui->clickArea(base + "/m", middle, true)) {
        index = (index + 1) % n;
        changed = true;
    }
    const bool hover = m_ui->hovered(rc);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    const float h = anim(base, hover ? 1.0f : 0.0f, 12.0f);
    const float flash = anim(base + "/flash", 0.0f, 4.0f);
    if (changed) anim(base + "/flash", 1.0f, 1000.0f);
    eng::Font& font = m_fonts->paper;
    eng::Font& arrows = m_fonts->scriptBold;
    const Color c = mixC(inkSoft, inkDark, h);
    r.text(arrows, {left.center().x - arrows.measure("‹") * 0.5f, rc.center().y - arrows.lineHeight() * 0.55f}, "‹",
           index > 0 ? mixC(c, inkRed, h) : inkFaded.withAlpha(0.35f));
    r.text(arrows, {right.center().x - arrows.measure("›") * 0.5f, rc.center().y - arrows.lineHeight() * 0.55f}, "›",
           index < n - 1 ? mixC(c, inkRed, h) : inkFaded.withAlpha(0.35f));
    const std::string& value = options[static_cast<size_t>(index)];
    const float w = font.measure(value);
    r.text(font, {middle.center().x - w * 0.5f, rc.center().y - font.lineHeight() * 0.5f - 2.0f * flash}, value, mixC(c, inkRed, flash));
    inkLine({middle.x + 6.0f, rc.bottom() - 3.0f}, {middle.right() - 6.0f, rc.bottom() - 4.0f}, 1.0f, inkFaded.withAlpha(0.5f + 0.4f * h), 1.0f,
            static_cast<uint32_t>(eng::ui::Context::hash(id) & 0xFFFF));
    return changed;
}

bool UiKit::inkSlider(std::string_view id, const Rect& rc, float& value, float lo, float hi, std::string_view valueText) {
    eng::Renderer2D& r = *m_r;
    eng::Input& in = m_ui->input();
    const uint64_t key = eng::ui::Context::hash(id);
    const float textW = 56.0f;
    const Rect track{rc.x + 10.0f, rc.center().y, rc.w - textW - 20.0f, 0.0f};
    const Rect grab{rc.x, rc.y, rc.w - textW, rc.h};
    const bool hover = m_ui->hovered(grab);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    if (hover && in.mousePressed()) m_drag = key;
    bool changed = false;
    if (m_drag == key && in.mouseDown()) {
        const float t = std::clamp((in.mouse.x - track.x) / track.w, 0.0f, 1.0f);
        const float v = lo + (hi - lo) * t;
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    const float h = anim(id, hover || m_drag == key ? 1.0f : 0.0f, 12.0f);
    inkLine({track.x, track.y}, {track.right(), track.y}, 1.6f, inkSoft.withAlpha(0.75f), 1.0f, 21);
    inkLine({track.x, track.y}, {track.x + track.w * t, track.y}, 2.6f, mixC(inkSoft, inkRed, h), 1.0f, 22);
    // Teilstriche
    for (int i = 0; i <= 4; ++i) {
        const float x = track.x + track.w * static_cast<float>(i) / 4.0f;
        r.line({x, track.y - 4.0f}, {x, track.y + 4.0f}, 1.0f, inkFaded.withAlpha(0.6f));
    }
    seal({track.x + track.w * t, track.y}, 8.0f + 1.5f * h, wax, "", 1.0f, 9);
    eng::Font& font = m_fonts->paper;
    r.textRight(font, {rc.right(), rc.center().y - font.lineHeight() * 0.5f}, valueText, inkDark);
    return changed;
}

bool UiKit::inkCheck(std::string_view id, const Rect& rc, std::string_view text, bool& value, bool enabled) {
    eng::Renderer2D& r = *m_r;
    const bool hover = enabled && m_ui->hovered(rc);
    const bool clicked = m_ui->clickArea(id, rc, enabled);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    if (clicked) value = !value;
    const float h = anim(id, hover ? 1.0f : 0.0f, 12.0f);
    const float on = anim(std::string(id) + "/on", value ? 1.0f : 0.0f, 8.0f);
    const float bs = 16.0f;
    const Rect box{rc.x + 2.0f, rc.center().y - bs * 0.5f, bs, bs};
    const Color c = enabled ? mixC(inkSoft, inkDark, h) : inkFaded.withAlpha(0.5f);
    inkLine({box.x, box.y}, {box.right(), box.y + 0.5f}, 1.3f, c, 1.0f, 31);
    inkLine({box.right(), box.y + 0.5f}, {box.right() - 0.5f, box.bottom()}, 1.3f, c, 1.0f, 32);
    inkLine({box.right() - 0.5f, box.bottom()}, {box.x + 0.5f, box.bottom()}, 1.3f, c, 1.0f, 33);
    inkLine({box.x + 0.5f, box.bottom()}, {box.x, box.y}, 1.3f, c, 1.0f, 34);
    // Häkchen wird mit der Feder gesetzt
    if (on > 0.01f) {
        const Vec2 a{box.x + 3.0f, box.center().y}, b{box.x + 7.0f, box.bottom() - 2.0f}, cc{box.right() + 4.0f, box.y - 5.0f};
        const float p1 = std::clamp(on * 2.0f, 0.0f, 1.0f), p2 = std::clamp(on * 2.0f - 1.0f, 0.0f, 1.0f);
        r.line(a, a + (b - a) * p1, 2.4f, inkRed);
        if (p2 > 0.0f) r.line(b, b + (cc - b) * p2, 2.4f, inkRed);
    }
    eng::Font& font = m_fonts->paper;
    r.text(font, {box.right() + 10.0f, rc.center().y - font.lineHeight() * 0.5f}, text, c);
    return clicked;
}

void UiKit::beginPaperTheme() {
    eng::ui::Theme& t = m_ui->theme();
    m_savedTheme = t;
    t.font = &m_fonts->paper;
    t.text = inkDark;
    t.textMuted = inkFaded;
    t.inputBg = Color::hex(0x5E3E1E, 0.08f);
    t.inputBorder = inkFaded.withAlpha(0.55f);
    t.focus = inkRed;
    t.radius = 1.0f;
}

void UiKit::endPaperTheme() { m_ui->theme() = m_savedTheme; }

// --- Zeiger, Übergänge, Funken --------------------------------------------------------

void UiKit::cursor(Vec2 p, eng::Cursor kind) {
    eng::Renderer2D& r = *m_r;
    if (kind == eng::Cursor::IBeam) {
        r.line(p + Vec2{0, -9}, p + Vec2{0, 9}, 2.0f, Color::hex(0x000000, 0.5f));
        r.line(p + Vec2{0, -9}, p + Vec2{0, 9}, 1.2f, parchment);
        return;
    }
    const float h = anim("cursor/hand", kind == eng::Cursor::Hand ? 1.0f : 0.0f, 14.0f);
    // Federspitze aus Messing, beim Überfahren klickbarer Dinge glimmt sie
    const float s = 1.0f + 0.12f * h;
    const Vec2 tip = p;
    const Vec2 l = p + Vec2{2.0f, 17.0f} * s, rr = p + Vec2{12.5f, 12.0f} * s, mid = p + Vec2{6.0f, 10.0f} * s;
    if (h > 0.01f) {
        r.setBlendMode(eng::BlendMode::Additive);
        r.glow(p + Vec2{4, 6}, 20.0f, ember.withAlpha(0.35f * h));
        r.setBlendMode(eng::BlendMode::Alpha);
    }
    r.triangle(tip + Vec2{1.5f, 2.0f}, l + Vec2{1.5f, 2.0f}, rr + Vec2{1.5f, 2.0f}, Color::hex(0x000000, 0.45f));
    r.triangleColors(tip, l, mid, brassLight, mixC(brass, ember, h), brassDark);
    r.triangleColors(tip, mid, rr, brassLight, brassDark, mixC(brass, ember, h));
    r.line(tip, l, 1.0f, inkDark.withAlpha(0.9f));
    r.line(tip, rr, 1.0f, inkDark.withAlpha(0.9f));
    r.line(l, mid, 1.0f, inkDark.withAlpha(0.7f));
    r.line(mid, rr, 1.0f, inkDark.withAlpha(0.7f));
}

void UiKit::clawWipe(float t) {
    if (t < 0.0f || t >= 1.0f) return;
    eng::Renderer2D& r = *m_r;
    const float W = r.width(), H = r.height();
    // Nach einem Wechsel: aus der Dunkelheit reißt eine Pranke das neue Bild auf
    const float dark = 1.0f - smooth01(0.15f, 0.8f, t);
    r.rect({0, 0, W, H}, ink.withAlpha(dark));
    const float slash = smooth01(0.0f, 0.3f, t);
    const float fadeClaws = 1.0f - smooth01(0.45f, 1.0f, t);
    const Vec2 a{W * 0.2f, -H * 0.05f}, b{W * 0.8f, H * 1.05f};
    claws(a, b, W * 0.11f, slash, Color::hex(0x6A1812, 0.75f * fadeClaws));
    claws(a + Vec2{3, 2}, b + Vec2{3, 2}, W * 0.11f, slash, Color::hex(0xC0402A, 0.25f * fadeClaws));
}

void UiKit::burst(Vec2 p, Color c, int count) {
    for (int i = 0; i < count; ++i) {
        const float a = rand01(static_cast<uint32_t>(m_time * 1000.0), static_cast<uint32_t>(i)) * 2.0f * eng::kPi;
        const float speed = 40.0f + rand01(static_cast<uint32_t>(m_time * 977.0), static_cast<uint32_t>(i)) * 120.0f;
        const float life = 0.35f + rand01(static_cast<uint32_t>(m_time * 733.0), static_cast<uint32_t>(i)) * 0.4f;
        m_sparks.push_back({p, Vec2{std::cos(a), std::sin(a) - 0.6f} * speed, life, life, 1.2f + rand01(5, static_cast<uint32_t>(i)) * 1.6f, c});
    }
}

void UiKit::drawBursts() {
    eng::Renderer2D& r = *m_r;
    for (Spark& s : m_sparks) {
        s.life -= m_dt;
        s.vel.y += 180.0f * m_dt;
        s.vel = s.vel * (1.0f - 2.5f * m_dt);
        s.pos = s.pos + s.vel * m_dt;
        const float a = std::clamp(s.life / s.maxLife, 0.0f, 1.0f);
        r.circle(s.pos, s.size, s.color.withAlpha(a));
    }
    std::erase_if(m_sparks, [](const Spark& s) { return s.life <= 0.0f; });
}

} // namespace ww

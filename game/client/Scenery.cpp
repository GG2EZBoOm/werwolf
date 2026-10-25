#include "client/Scenery.h"

#include "engine/core/Random.h"
#include "engine/gfx/Renderer2D.h"

#include <algorithm>
#include <cmath>

namespace ww {

using eng::Color;
using eng::lerp;
using eng::Rect;
using eng::Vec2;

void Scenery::init(uint64_t seed) {
    eng::Rng rng(seed);
    m_stars.clear();
    for (int i = 0; i < 220; ++i) {
        const float y = rng.uniform();
        m_stars.push_back({rng.uniform(), y * y * 0.75f, rng.uniform(0.6f, 1.9f), rng.uniform(0.0f, 6.28f),
                           rng.uniform(0.5f, 2.5f)});
    }
    m_trees.clear();
    for (int layer = 0; layer < 2; ++layer) {
        float x = -0.02f;
        while (x < 1.03f) {
            const float h = layer == 0 ? rng.uniform(0.07f, 0.13f) : rng.uniform(0.10f, 0.19f);
            m_trees.push_back({x, h, h * rng.uniform(0.32f, 0.42f), layer});
            x += layer == 0 ? rng.uniform(0.012f, 0.026f) : rng.uniform(0.025f, 0.05f);
        }
    }
    m_houses.clear();
    const float housePositions[] = {0.06f, 0.15f, 0.27f, 0.70f, 0.81f, 0.92f};
    for (float hx : housePositions) {
        m_houses.push_back(
            {hx + rng.uniform(-0.015f, 0.015f), rng.uniform(0.045f, 0.065f), rng.uniform(0.04f, 0.06f),
             rng.uniform(0.025f, 0.04f), rng.chance(0.75f)});
    }
    m_clouds.clear();
    for (int i = 0; i < 6; ++i)
        m_clouds.push_back({rng.uniform(), rng.uniform(0.08f, 0.35f), rng.uniform(0.6f, 1.3f), rng.uniform(0.004f, 0.01f)});
}

void Scenery::drawSky(eng::Renderer2D& r, float night, double time) const {
    const float w = r.width();
    const float h = r.height();
    const Color top = lerp(Color::hex(0x5B8BC9), Color::hex(0x050813), night);
    const Color bottom = lerp(Color::hex(0xF0BE84), Color::hex(0x1D2547), night);
    r.gradientV({0, 0, w, h}, top, bottom);

    const float t = static_cast<float>(time);

    // Sonne (Tag)
    if (night < 0.98f) {
        const float a = 1.0f - night;
        const Vec2 sun{w * 0.18f, h * (0.2f + 0.25f * night)};
        r.glow(sun, 260.0f, Color::hex(0xFFE2A8, 0.55f * a));
        r.glow(sun, 90.0f, Color::hex(0xFFF3D6, 0.8f * a));
        r.circle(sun, 34.0f, Color::hex(0xFFF6E0, a));
    }

    // Wolken ziehen langsam vorbei
    for (const Cloud& c : m_clouds) {
        float x = std::fmod(c.x + t * c.speed * 0.1f, 1.3f) - 0.15f;
        const Vec2 p{x * w, c.y * h};
        const Color col = lerp(Color::hex(0xFFFFFF, 0.28f), Color::hex(0x8A94C0, 0.06f), night);
        const float s = 70.0f * c.size;
        r.glow(p, s * 1.4f, col);
        r.glow(p + Vec2{s * 0.9f, s * 0.15f}, s * 1.1f, col);
        r.glow(p + Vec2{-s * 0.9f, s * 0.2f}, s, col);
    }

    // Sterne funkeln nur nachts
    if (night > 0.02f) {
        for (const Star& s : m_stars) {
            const float twinkle = 0.55f + 0.45f * std::sin(t * s.speed + s.phase);
            const float a = night * twinkle * (1.0f - s.y);
            r.circle({s.x * w, s.y * h}, s.size * 0.75f, Color::hex(0xFFF8E7, a));
        }
        // Mond mit Hof
        const Vec2 moon{w * 0.82f, h * (0.16f + (1.0f - night) * 0.3f)};
        r.glow(moon, 300.0f, Color::hex(0x9FB4FF, 0.18f * night));
        r.glow(moon, 110.0f, Color::hex(0xE8EEFF, 0.35f * night));
        r.circle(moon, 40.0f, Color::hex(0xF3EEDC, night));
        r.circle(moon + Vec2{-12, -8}, 8.0f, Color::hex(0xD9D2BC, night));
        r.circle(moon + Vec2{10, 10}, 11.0f, Color::hex(0xDDD6C1, night));
        r.circle(moon + Vec2{14, -14}, 5.0f, Color::hex(0xD6CFB8, night));
    }
}

void Scenery::drawHorizon(eng::Renderer2D& r, float night, double time, float horizonY) const {
    const float w = r.width();
    const float h = r.height();
    const float t = static_cast<float>(time);

    // ferne Hügel
    const Color hills = lerp(Color::hex(0x6E8C7A), Color::hex(0x101733), night);
    for (int i = 0; i < 5; ++i) {
        const float cx = w * (0.1f + 0.22f * static_cast<float>(i));
        const float radius = w * (0.22f + 0.04f * static_cast<float>(i % 2));
        r.circle({cx, horizonY + radius * 0.82f}, radius, hills);
    }

    // Wald in zwei Ebenen
    for (const Tree& tree : m_trees) {
        const Color col = tree.layer == 0 ? lerp(Color::hex(0x4D6E58), Color::hex(0x0C1329), night)
                                          : lerp(Color::hex(0x355443), Color::hex(0x080D1E), night);
        const float base = horizonY + (tree.layer == 0 ? 2.0f : 10.0f);
        const float x = tree.x * w;
        const float th = tree.height * h;
        const float tw = tree.width * h;
        for (int k = 0; k < 3; ++k) {
            const float y0 = base - th * (static_cast<float>(k) * 0.28f);
            const float ww = tw * (1.0f - static_cast<float>(k) * 0.22f);
            r.triangle({x - ww * 0.5f, y0}, {x + ww * 0.5f, y0}, {x, y0 - th * 0.55f}, col);
        }
        r.rect({x - tw * 0.05f, base - 2.0f, tw * 0.1f, 6.0f}, col);
    }

    // Boden
    const Color groundTop = lerp(Color::hex(0x56734F), Color::hex(0x0B1022), night);
    const Color groundBottom = lerp(Color::hex(0x34492F), Color::hex(0x05070F), night);
    r.gradientV({0, horizonY + 8.0f, w, h - horizonY}, groundTop, groundBottom);

    // Häuser mit beleuchteten Fenstern
    for (const House& house : m_houses) {
        const float x = house.x * w;
        const float hw = house.width * h;
        const float hh = house.height * h;
        const float base = horizonY + 14.0f;
        const Color wall = lerp(Color::hex(0x7A5E4A), Color::hex(0x0A0E1D), night);
        const Color roof = lerp(Color::hex(0x8C3F33), Color::hex(0x070A16), night);
        r.rect({x - hw * 0.5f, base - hh, hw, hh}, wall);
        r.triangle({x - hw * 0.62f, base - hh + 1.0f}, {x + hw * 0.62f, base - hh + 1.0f},
                   {x, base - hh - house.roof * h}, roof);
        if (house.lit && night > 0.1f) {
            const float flicker = 0.85f + 0.15f * std::sin(t * 3.0f + house.x * 40.0f);
            const Color light = Color::hex(0xFFC66B, night * flicker);
            const Rect win{x - hw * 0.22f, base - hh * 0.62f, hw * 0.18f, hh * 0.24f};
            r.glow(win.center(), hw * 0.6f, Color::hex(0xFFB44D, 0.25f * night * flicker));
            r.rect(win, light);
            r.rect(win.offset(hw * 0.27f, 0), light);
        }
    }
}

void Scenery::drawCampfire(eng::Renderer2D& r, Vec2 pos, float scale, double time, float intensity) const {
    const float t = static_cast<float>(time);
    const float s = scale;

    // Lichtschein auf dem Boden
    r.setBlendMode(eng::BlendMode::Additive);
    const float pulse = 0.9f + 0.1f * std::sin(t * 7.0f) * std::sin(t * 3.3f);
    r.glow(pos + Vec2{0, 6 * s}, 260.0f * s, Color::hex(0xFF8A3D, 0.20f * intensity * pulse));
    r.glow(pos, 120.0f * s, Color::hex(0xFFB054, 0.25f * intensity * pulse));
    r.setBlendMode(eng::BlendMode::Alpha);

    // Steinring und Holzscheite
    for (int i = 0; i < 9; ++i) {
        const float a = static_cast<float>(i) / 9.0f * 2.0f * eng::kPi;
        r.circle(pos + Vec2{std::cos(a) * 30.0f * s, 10.0f * s + std::sin(a) * 9.0f * s}, 6.0f * s,
                 Color::hex(0x3A3F4E));
    }
    r.line(pos + Vec2{-24 * s, 12 * s}, pos + Vec2{20 * s, 2 * s}, 7.0f * s, Color::hex(0x5A3A26));
    r.line(pos + Vec2{-20 * s, 2 * s}, pos + Vec2{24 * s, 12 * s}, 7.0f * s, Color::hex(0x4A2F1F));

    // Flammenzungen
    r.setBlendMode(eng::BlendMode::Additive);
    struct Tongue {
        float offset, height, width, speed, phase;
        uint32_t color;
    };
    const Tongue tongues[] = {
        {-10, 46, 18, 6.0f, 0.0f, 0xFF5A1F}, {9, 42, 17, 7.0f, 1.3f, 0xFF6A26}, {0, 62, 22, 5.0f, 2.1f, 0xFF8A2E},
        {-4, 40, 14, 8.0f, 0.7f, 0xFFB347},  {5, 34, 12, 9.0f, 2.8f, 0xFFD36B},
    };
    for (const Tongue& tg : tongues) {
        const float sway = std::sin(t * tg.speed + tg.phase) * 5.0f * s;
        const float flick = 0.85f + 0.15f * std::sin(t * tg.speed * 1.7f + tg.phase);
        const Vec2 base = pos + Vec2{tg.offset * s, 6.0f * s};
        const Vec2 tip = base + Vec2{sway, -tg.height * s * flick * intensity};
        const Color c = Color::hex(tg.color, 0.75f * intensity);
        r.triangle(base + Vec2{-tg.width * 0.5f * s, 0}, base + Vec2{tg.width * 0.5f * s, 0}, tip, c);
        r.glow(base + Vec2{sway * 0.3f, -tg.height * 0.35f * s}, tg.width * 1.1f * s, c.fade(0.7f));
    }
    r.glow(pos + Vec2{0, -4 * s}, 26.0f * s, Color::hex(0xFFE7A3, 0.8f * intensity));

    // aufsteigende Funken
    for (int i = 0; i < 14; ++i) {
        const float phase = static_cast<float>(i) * 0.731f;
        const float life = std::fmod(t * (0.35f + 0.05f * static_cast<float>(i % 4)) + phase, 1.0f);
        const float x = std::sin(life * 6.0f + phase * 5.0f) * 14.0f * s + (static_cast<float>(i % 5) - 2.0f) * 4 * s;
        const Vec2 p = pos + Vec2{x, -20.0f * s - life * 90.0f * s};
        r.circle(p, (1.8f - life) * s, Color::hex(0xFFB65C, (1.0f - life) * 0.9f * intensity));
    }
    r.setBlendMode(eng::BlendMode::Alpha);
}

void Scenery::drawVignette(eng::Renderer2D& r, float strength) const {
    if (strength <= 0.0f) return;
    const float w = r.width();
    const float h = r.height();
    const Color edge = Color::hex(0x000000, 0.55f * strength);
    const Color clear = Color::hex(0x000000, 0.0f);
    r.gradientV({0, 0, w, h * 0.25f}, edge, clear);
    r.gradientV({0, h * 0.7f, w, h * 0.3f}, clear, edge);
}

} // namespace ww

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eng {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }

    float length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{0.0f, 0.0f};
    }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    constexpr Rect shrink(float m) const { return {x + m, y + m, w - 2 * m, h - 2 * m}; }
    constexpr Rect shrink(float mx, float my) const { return {x + mx, y + my, w - 2 * mx, h - 2 * my}; }
    constexpr Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }

    // Schneidet ein Stück vom Rechteck ab und gibt es zurück (praktisch für Layouts).
    Rect takeTop(float amount) { Rect r{x, y, w, amount}; y += amount; h -= amount; return r; }
    Rect takeBottom(float amount) { Rect r{x, y + h - amount, w, amount}; h -= amount; return r; }
    Rect takeLeft(float amount) { Rect r{x, y, amount, h}; x += amount; w -= amount; return r; }
    Rect takeRight(float amount) { Rect r{x + w - amount, y, amount, h}; w -= amount; return r; }

    static Rect intersect(const Rect& a, const Rect& b) {
        float x0 = std::max(a.x, b.x);
        float y0 = std::max(a.y, b.y);
        float x1 = std::min(a.right(), b.right());
        float y1 = std::min(a.bottom(), b.bottom());
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    // 0xRRGGBB
    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
        return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
    }

    constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr Color fade(float factor) const { return {r, g, b, a * factor}; }
    constexpr Color scaled(float f) const { return {r * f, g * f, b * f, a}; }

    uint32_t packed() const {
        auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline Color lerp(Color a, Color b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}
inline float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float smoothstep(float e0, float e1, float x) {
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
// Bewegt `current` mit begrenzter Geschwindigkeit Richtung `target`.
inline float approach(float current, float target, float maxDelta) {
    if (current < target) return std::min(current + maxDelta, target);
    return std::max(current - maxDelta, target);
}

} // namespace eng

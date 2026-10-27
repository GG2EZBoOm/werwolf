#include "client/Style.h"

#include "engine/core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace ww {

using eng::Color;
using eng::Rect;
using eng::Vec2;
using namespace palette;

namespace {

float hash1(float n) {
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

} // namespace

std::string upper(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        char32_t cp = eng::utf8::next(text, i);
        if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 0x20; // ä ö ü é … (Latin-1)
        else if (cp < 0x10000) cp = static_cast<char32_t>(std::towupper(static_cast<wint_t>(cp)));
        eng::utf8::append(out, cp);
    }
    return out;
}

// --- Rahmen ----------------------------------------------------------------------

void UiKit::init() {
    // Filmkorn: graues Rauschen
    constexpr int kNoise = 128;
    std::vector<uint8_t> noise(kNoise * kNoise * 4);
    uint32_t state = 0x9E3779B9u;
    for (int i = 0; i < kNoise * kNoise; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const uint8_t v = static_cast<uint8_t>(state & 0xFF);
        noise[i * 4 + 0] = noise[i * 4 + 1] = noise[i * 4 + 2] = v;
        noise[i * 4 + 3] = 255;
    }
    m_noise = eng::Renderer2D::createTexture(kNoise, kNoise, noise.data(), true);

    // Pergament/Holz: weiche, fleckige Struktur (nachtileable Wertrauschen)
    constexpr int kPaper = 256;
    std::vector<uint8_t> paper(kPaper * kPaper * 4);
    auto lattice = [](int x, int y, int period) {
        x = ((x % period) + period) % period;
        y = ((y % period) + period) % period;
        return hash1(static_cast<float>(x * 157 + y * 311));
    };
    auto value = [&](float x, float y, int period) {
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
        const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
        const float a = lattice(ix, iy, period), b = lattice(ix + 1, iy, period);
        const float c = lattice(ix, iy + 1, period), d = lattice(ix + 1, iy + 1, period);
        return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
    };
    for (int y = 0; y < kPaper; ++y) {
        for (int x = 0; x < kPaper; ++x) {
            float v = 0.0f, amp = 0.5f;
            for (int o = 0; o < 4; ++o) {
                const int period = 8 << o;
                const float scale = static_cast<float>(period) / static_cast<float>(kPaper);
                v += value(static_cast<float>(x) * scale, static_cast<float>(y) * scale, period) * amp;
                amp *= 0.5f;
            }
            const uint8_t a = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
            uint8_t* p = &paper[static_cast<size_t>((y * kPaper + x) * 4)];
            p[0] = p[1] = p[2] = 255;
            p[3] = a;
        }
    }
    m_paper = eng::Renderer2D::createTexture(kPaper, kPaper, paper.data(), true);
    buildMaterials();
}

void UiKit::shutdown() {
    eng::Renderer2D::destroyTexture(m_noise);
    eng::Renderer2D::destroyTexture(m_paper);
    m_noise = m_paper = 0;
    destroyMaterials();
}

void UiKit::beginFrame(eng::ui::Context& ui, eng::Renderer2D& r, Fonts& fonts, double time, float dt) {
    m_ui = &ui;
    m_r = &r;
    m_fonts = &fonts;
    m_time = time;
    m_dt = dt;
    if (!ui.input().mouseDown()) m_drag = 0;
}

float UiKit::animFrom(std::string_view id, float from, float target, float speed) {
    const uint64_t key = eng::ui::Context::hash(id);
    auto [it, inserted] = m_anim.try_emplace(key, from);
    it->second = eng::approach(it->second, target, m_dt * speed);
    return it->second;
}

float UiKit::anim(std::string_view id, float target, float speed) {
    const uint64_t key = eng::ui::Context::hash(id);
    auto [it, inserted] = m_anim.try_emplace(key, target);
    if (!inserted) it->second = eng::approach(it->second, target, m_dt * speed);
    return it->second;
}

// --- Atmosphäre ------------------------------------------------------------------

void UiKit::vignette(float strength) {
    eng::Renderer2D& r = *m_r;
    const float W = r.width(), H = r.height();
    const Color edge = ink.withAlpha(0.75f * strength), none = ink.withAlpha(0.0f);
    r.gradientV({0, 0, W, H * 0.28f}, edge, none);
    r.gradientV({0, H * 0.72f, W, H * 0.28f}, none, edge);
    r.gradientH({0, 0, W * 0.22f, H}, edge, none);
    r.gradientH({W * 0.78f, 0, W * 0.22f, H}, none, edge);
}

void UiKit::sideShade(float width, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.rect({0, 0, width * 0.35f, r.height()}, ink.withAlpha(alpha));
    r.gradientH({width * 0.35f, 0, width * 0.65f, r.height()}, ink.withAlpha(alpha), ink.withAlpha(0.0f));
}

void UiKit::grain(float strength) {
    if (!m_noise || strength <= 0.0f) return;
    eng::Renderer2D& r = *m_r;
    const float W = r.width(), H = r.height();
    // Jedes Bild ein anderer Ausschnitt – flimmert wie Filmkorn
    const float frame = std::floor(static_cast<float>(m_time) * 24.0f);
    const Vec2 off{hash1(frame), hash1(frame + 7.3f)};
    const float scale = 1.0f / 128.0f * 1.6f;
    r.image(m_noise, {0, 0, W, H}, off, off + Vec2{W * scale, H * scale}, Color{1, 1, 1, strength});
}

void UiKit::embers(const Rect& area, int count, float alpha) {
    eng::Renderer2D& r = *m_r;
    const float t = static_cast<float>(m_time);
    r.setBlendMode(eng::BlendMode::Additive);
    for (int i = 0; i < count; ++i) {
        const float fi = static_cast<float>(i);
        const float speed = 0.025f + hash1(fi * 3.1f) * 0.05f;
        const float life = std::fmod(t * speed + hash1(fi), 1.0f);
        const float x = area.x + hash1(fi * 1.7f) * area.w + std::sin(t * (0.3f + hash1(fi * 5.3f)) + fi) * 28.0f * life;
        const float y = area.bottom() - life * area.h;
        const float flicker = 0.6f + 0.4f * std::sin(t * (3.0f + hash1(fi * 2.9f) * 4.0f) + fi);
        const float a = alpha * flicker * std::sin(life * eng::kPi);
        const float size = 1.2f + hash1(fi * 9.1f) * 2.2f;
        r.glow({x, y}, size * 5.0f, ember.withAlpha(a * 0.35f));
        r.circle({x, y}, size * 0.6f, Color::hex(0xFFD2A0, a));
    }
    r.setBlendMode(eng::BlendMode::Alpha);
}

void UiKit::fade(float alpha, Color color) {
    if (alpha <= 0.0f) return;
    m_r->rect({0, 0, m_r->width(), m_r->height()}, color.withAlpha(std::min(1.0f, alpha)));
}

// --- Flächen und Schmuck -----------------------------------------------------------

void UiKit::diamond(Vec2 c, float s, Color col) {
    eng::Renderer2D& r = *m_r;
    r.triangle({c.x - s, c.y}, {c.x, c.y - s}, {c.x + s, c.y}, col);
    r.triangle({c.x - s, c.y}, {c.x + s, c.y}, {c.x, c.y + s}, col);
}

void UiKit::frame(const Rect& rc, Color c, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.roundRectOutline(rc, 2.0f, 1.0f, c.withAlpha(c.a * 0.38f * alpha));
    r.roundRectOutline(rc.shrink(4.0f), 1.0f, 1.0f, c.withAlpha(c.a * 0.12f * alpha));
    // Eckwinkel und Rauten
    const float L = std::min(16.0f, std::min(rc.w, rc.h) * 0.25f);
    const Color strong = c.withAlpha(c.a * 0.85f * alpha);
    const Vec2 corners[4] = {{rc.x, rc.y}, {rc.right(), rc.y}, {rc.x, rc.bottom()}, {rc.right(), rc.bottom()}};
    for (int i = 0; i < 4; ++i) {
        const Vec2 p = corners[i];
        const float sx = (i % 2 == 0) ? 1.0f : -1.0f;
        const float sy = (i < 2) ? 1.0f : -1.0f;
        r.line(p, p + Vec2{L * sx, 0}, 1.4f, strong);
        r.line(p, p + Vec2{0, L * sy}, 1.4f, strong);
        diamond(p + Vec2{7.0f * sx, 7.0f * sy}, 2.2f, strong);
    }
}

void UiKit::panel(const Rect& rc, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.roundRect(rc.offset(0, 6), 4.0f, Color::hex(0x000000, 0.35f * alpha));
    r.gradientV(rc, inkLight.withAlpha(0.93f * alpha), ink.withAlpha(0.96f * alpha));
    if (m_paper) {
        const float s = 1.0f / 300.0f;
        r.image(m_paper, rc, {rc.x * s, rc.y * s}, {rc.right() * s, rc.bottom() * s}, Color::hex(0x8A6A3A, 0.07f * alpha));
    }
    // leichter Lichtschein oben, als fiele Kerzenlicht auf die Tafel
    r.gradientV({rc.x, rc.y, rc.w, std::min(90.0f, rc.h * 0.4f)}, brass.withAlpha(0.05f * alpha), brass.withAlpha(0.0f));
    frame(rc, brass, alpha);
}

void UiKit::divider(Vec2 c, float width, Color col) {
    eng::Renderer2D& r = *m_r;
    const float h = width * 0.5f;
    r.gradientH({c.x - h, c.y - 0.5f, h - 10.0f, 1.0f}, col.withAlpha(0.0f), col);
    r.gradientH({c.x + 10.0f, c.y - 0.5f, h - 10.0f, 1.0f}, col, col.withAlpha(0.0f));
    diamond(c, 3.2f, col);
}

void UiKit::spaced(eng::Font& font, Vec2 pos, std::string_view text, float spacing, Color c) {
    m_r->textSpaced(font, pos, text, spacing, c);
}

void UiKit::spacedCentered(eng::Font& font, const Rect& rc, std::string_view text, float spacing, Color c) {
    const float w = eng::Renderer2D::measureSpaced(font, text, spacing);
    const float y = rc.y + (rc.h - font.capHeight()) * 0.5f - (font.ascent() - font.capHeight());
    m_r->textSpaced(font, {rc.x + (rc.w - w) * 0.5f, y}, text, spacing, c);
}

void UiKit::label(Vec2 pos, std::string_view text, Color c, float spacing) {
    m_r->textSpaced(m_fonts->label, pos, upper(text), spacing, c);
}

float UiKit::labelWidth(std::string_view text, float spacing) {
    return eng::Renderer2D::measureSpaced(m_fonts->label, upper(text), spacing);
}

float UiKit::keycapWidth(std::string_view key) { return std::max(24.0f, m_fonts->smallBold.measure(key) + 14.0f); }

void UiKit::keycap(const Rect& rc, std::string_view key, Color c, float alpha) {
    eng::Renderer2D& r = *m_r;
    r.roundRect(rc, 3.0f, ink.withAlpha(0.75f * alpha));
    r.roundRect({rc.x, rc.bottom() - 3.0f, rc.w, 3.0f}, 1.5f, c.withAlpha(0.18f * alpha));
    r.roundRectOutline(rc, 3.0f, 1.0f, c.withAlpha(0.7f * alpha));
    r.textCentered(m_fonts->smallBold, rc.offset(0, -1), key, parchment.withAlpha(alpha));
}

float UiKit::hint(Vec2 pos, std::string_view key, std::string_view text, float alpha) {
    const float kw = keycapWidth(key);
    keycap({pos.x, pos.y, kw, 22.0f}, key, brass, alpha);
    m_r->text(m_fonts->small, {pos.x + kw + 8.0f, pos.y + 3.0f}, text, ash.withAlpha(alpha));
    return kw + 8.0f + m_fonts->small.measure(text);
}

// --- Bedienelemente ----------------------------------------------------------------

bool UiKit::menuItem(std::string_view id, Vec2 pos, std::string_view text, bool selected, bool enabled, float width) {
    eng::Renderer2D& r = *m_r;
    eng::Font& font = m_fonts->menu;
    const std::string caps = upper(text);
    const Rect hit{pos.x - 24.0f, pos.y - 8.0f, width, font.lineHeight() + 14.0f};
    const bool hover = enabled && m_ui->hovered(hit);
    const bool clicked = m_ui->clickArea(id, hit, enabled);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    const float h = easeOut(anim(id, (hover || selected) && enabled ? 1.0f : 0.0f, 9.0f));

    if (h > 0.01f) {
        r.gradientH({hit.x, hit.y + 4.0f, hit.w, hit.h - 8.0f}, brass.withAlpha(0.13f * h), brass.withAlpha(0.0f));
        r.rect({hit.x, hit.y + 4.0f, 2.0f, (hit.h - 8.0f) * h}, brassLight.withAlpha(0.9f * h));
        diamond({pos.x - 10.0f + 4.0f * h, hit.center().y}, 3.0f * h, brassLight.withAlpha(h));
    }
    const Color base = enabled ? ash : smoke.withAlpha(0.6f);
    const Color c = eng::lerp(base, brassLight, h);
    r.textSpaced(font, {pos.x + 2.0f + 12.0f * h, pos.y + 1.5f}, caps, 3.0f, Color::hex(0x000000, 0.5f));
    r.textSpaced(font, {pos.x + 12.0f * h, pos.y}, caps, 3.0f, c);
    return clicked;
}

bool UiKit::button(std::string_view id, const Rect& rc, std::string_view text, bool primary, bool enabled) {
    eng::Renderer2D& r = *m_r;
    const bool hover = enabled && m_ui->hovered(rc);
    const bool clicked = m_ui->clickArea(id, rc, enabled);
    if (hover) m_ui->setCursor(eng::Cursor::Hand);
    const float h = anim(id, hover ? 1.0f : 0.0f, 12.0f);
    if (primary) {
        const Color top = eng::lerp(Color::hex(0xB8914A), Color::hex(0xD9B66C), h);
        const Color bottom = eng::lerp(Color::hex(0x8C6C33), Color::hex(0xB08C48), h);
        r.roundRect(rc.offset(0, 3), 3.0f, Color::hex(0x000000, 0.35f));
        r.gradientV(rc.shrink(0.5f), enabled ? top : Color::hex(0x3A3730), enabled ? bottom : Color::hex(0x2C2A25));
        r.roundRectOutline(rc, 3.0f, 1.0f, brassLight.withAlpha(enabled ? 0.55f : 0.15f));
        spacedCentered(m_fonts->smallBold, rc, upper(text), 2.0f, enabled ? Color::hex(0x1A1308) : smoke);
    } else {
        r.roundRect(rc, 3.0f, ink.withAlpha(0.6f + 0.2f * h));
        r.gradientH(rc.shrink(1.0f), brass.withAlpha(0.10f * h), brass.withAlpha(0.0f));
        r.roundRectOutline(rc, 3.0f, 1.0f, brass.withAlpha(enabled ? 0.35f + 0.4f * h : 0.12f));
        spacedCentered(m_fonts->smallBold, rc, upper(text), 2.0f, enabled ? eng::lerp(parchment, brassLight, h) : smoke);
    }
    return clicked;
}

bool UiKit::tab(std::string_view id, const Rect& rc, std::string_view text, bool active) {
    eng::Renderer2D& r = *m_r;
    const bool hover = m_ui->hovered(rc);
    const bool clicked = m_ui->clickArea(id, rc, true);
    if (hover && !active) m_ui->setCursor(eng::Cursor::Hand);
    const float h = anim(id, active ? 1.0f : (hover ? 0.5f : 0.0f), 10.0f);
    spacedCentered(m_fonts->smallBold, rc, upper(text), 2.6f, eng::lerp(smoke, brassLight, h));
    const float w = rc.w * 0.6f * h;
    r.rect({rc.center().x - w * 0.5f, rc.bottom() - 2.0f, w, 2.0f}, brass.withAlpha(h));
    return clicked;
}

bool UiKit::choice(std::string_view id, const Rect& rc, const std::vector<std::string>& options, int& index) {
    if (options.empty()) return false;
    eng::Renderer2D& r = *m_r;
    const int n = static_cast<int>(options.size());
    index = std::clamp(index, 0, n - 1);
    const Rect left{rc.x, rc.y, 28.0f, rc.h};
    const Rect right{rc.right() - 28.0f, rc.y, 28.0f, rc.h};
    const Rect middle{left.right(), rc.y, rc.w - 56.0f, rc.h};
    const std::string base(id);
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
    r.rect({rc.x, rc.bottom() - 1.0f, rc.w, 1.0f}, brass.withAlpha(0.15f + 0.3f * h));
    auto arrow = [&](const Rect& a, bool pointsLeft, bool on) {
        const Vec2 c = a.center();
        const float s = 5.0f;
        const Color col = on ? eng::lerp(ash, brassLight, h) : smoke.withAlpha(0.35f);
        if (pointsLeft) r.triangle({c.x + s * 0.6f, c.y - s}, {c.x - s * 0.8f, c.y}, {c.x + s * 0.6f, c.y + s}, col);
        else r.triangle({c.x - s * 0.6f, c.y - s}, {c.x + s * 0.8f, c.y}, {c.x - s * 0.6f, c.y + s}, col);
    };
    arrow(left, true, index > 0);
    arrow(right, false, index < n - 1);
    r.textCentered(m_fonts->ui, middle, options[static_cast<size_t>(index)], eng::lerp(parchment, brassLight, h));
    // Positionspunkte auf der Grundlinie (nur bei mehr als zwei Möglichkeiten)
    const float dots = static_cast<float>(n) * 8.0f;
    for (int i = 0; i < n && n > 2; ++i)
        r.circle({middle.center().x - dots * 0.5f + 4.0f + static_cast<float>(i) * 8.0f, rc.bottom() - 1.0f}, 1.8f,
                 i == index ? brassLight : smoke.withAlpha(0.6f));
    return changed;
}

bool UiKit::slider(std::string_view id, const Rect& rc, float& value, float lo, float hi, std::string_view valueText) {
    eng::Renderer2D& r = *m_r;
    eng::Input& in = m_ui->input();
    const uint64_t key = eng::ui::Context::hash(id);
    const float textW = 58.0f;
    const Rect track{rc.x + 6.0f, rc.center().y - 1.5f, rc.w - textW - 12.0f, 3.0f};
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
    r.roundRect(track, 1.5f, smoke.withAlpha(0.35f));
    r.roundRect({track.x, track.y, track.w * t, track.h}, 1.5f, eng::lerp(brass, brassLight, h));
    const Vec2 knob{track.x + track.w * t, track.center().y};
    r.glow(knob, 14.0f, brass.withAlpha(0.25f * h));
    diamond(knob, 6.0f + h, brassLight);
    r.textRight(m_fonts->ui, {rc.right(), rc.center().y - m_fonts->ui.lineHeight() * 0.5f}, valueText, parchment);
    return changed;
}

} // namespace ww

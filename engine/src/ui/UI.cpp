#include "engine/ui/UI.h"

#include "engine/core/Utf8.h"
#include "engine/gfx/Font.h"
#include "engine/gfx/Renderer2D.h"

#include <cmath>

namespace eng::ui {

uint64_t Context::hash(std::string_view s) {
    uint64_t h = 1469598103934665603ULL; // FNV-1a
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

void Context::beginFrame(Window& window, Renderer2D& renderer, double time) {
    m_window = &window;
    m_renderer = &renderer;
    m_input = &window.input();
    m_time = time;
    m_cursor = Cursor::Arrow;
    m_focusClaimed = false;
}

void Context::endFrame() {
    // Klick ins Leere entfernt den Tastaturfokus
    if (m_input->mousePressed() && !m_focusClaimed) m_focus = 0;
    if (!m_input->mouseDown()) m_active = 0;
    m_window->setCursor(m_customCursor ? Cursor::None : m_cursor);
}

bool Context::hovered(const Rect& r) const { return r.contains(m_input->mouse); }

void Context::focus(std::string_view id) {
    m_focus = hash(id);
    m_focusClaimed = true;
}

bool Context::interact(uint64_t id, const Rect& r, bool enabled, bool& isHovered, bool& isHeld) {
    isHovered = enabled && hovered(r);
    if (isHovered && m_input->mousePressed()) m_active = id;
    isHeld = enabled && m_active == id && m_input->mouseDown();
    return enabled && m_active == id && m_input->mouseReleased() && isHovered;
}

bool Context::clickArea(std::string_view id, const Rect& r, bool enabled) {
    bool hov = false, held = false;
    const bool clicked = interact(hash(id), r, enabled, hov, held);
    if (hov) m_cursor = Cursor::Hand;
    return clicked;
}

bool Context::button(std::string_view id, const Rect& r, std::string_view label, ButtonKind kind, bool enabled) {
    bool hov = false, held = false;
    const bool clicked = interact(hash(id), r, enabled, hov, held);
    if (hov) m_cursor = Cursor::Hand;

    Color bg, fg;
    switch (kind) {
    case ButtonKind::Primary:
        bg = held ? m_theme.primary.scaled(0.85f) : hov ? m_theme.primaryHover : m_theme.primary;
        fg = m_theme.primaryText;
        break;
    case ButtonKind::Danger:
        bg = held ? m_theme.danger.scaled(0.85f) : hov ? m_theme.dangerHover : m_theme.danger;
        fg = Color::hex(0xFFFFFF);
        break;
    case ButtonKind::Ghost:
        bg = hov ? Color::hex(0xFFFFFF, held ? 0.04f : 0.08f) : Color::hex(0xFFFFFF, 0.0f);
        fg = hov ? m_theme.text : m_theme.textMuted;
        break;
    case ButtonKind::Normal:
    default:
        bg = held ? m_theme.buttonPressed : hov ? m_theme.buttonHover : m_theme.button;
        fg = m_theme.text;
        break;
    }
    if (!enabled) {
        bg = kind == ButtonKind::Ghost ? Color::hex(0xFFFFFF, 0.0f) : m_theme.disabled;
        fg = m_theme.disabledText;
    }

    Renderer2D& rr = *m_renderer;
    rr.roundRect(r, m_theme.radius, bg);
    if (kind == ButtonKind::Normal && enabled) rr.roundRectOutline(r, m_theme.radius, 1.0f, m_theme.panelBorder);
    if (m_theme.font) rr.textCentered(*m_theme.font, r.offset(0, held ? 1.0f : 0.0f), label, fg);
    return clicked;
}

bool Context::checkbox(std::string_view id, const Rect& r, std::string_view label, bool& value, bool enabled) {
    bool hov = false, held = false;
    const bool clicked = interact(hash(id), r, enabled, hov, held);
    if (hov) m_cursor = Cursor::Hand;
    if (clicked) value = !value;

    Renderer2D& rr = *m_renderer;
    const float box = 20.0f;
    const Rect b{r.x, r.y + (r.h - box) * 0.5f, box, box};
    const Color accent = enabled ? m_theme.primary : m_theme.disabledText;
    if (value) {
        rr.roundRect(b, 5.0f, accent);
        // Häkchen aus zwei Linien
        const Color tick = m_theme.primaryText;
        rr.line({b.x + 5.0f, b.y + 10.5f}, {b.x + 8.5f, b.y + 14.0f}, 2.2f, tick);
        rr.line({b.x + 8.5f, b.y + 14.0f}, {b.x + 15.0f, b.y + 6.5f}, 2.2f, tick);
    } else {
        rr.roundRect(b, 5.0f, m_theme.inputBg);
        rr.roundRectOutline(b, 5.0f, 1.5f, hov ? m_theme.focus : m_theme.inputBorder);
    }
    if (m_theme.font) {
        const Color fg = enabled ? m_theme.text : m_theme.textMuted;
        rr.textCentered(*m_theme.font, {b.right() + 10.0f, r.y, m_theme.font->measure(label), r.h}, label, fg);
    }
    return clicked;
}

int Context::stepper(std::string_view id, const Rect& r, std::string_view valueText, bool enabled) {
    const float bw = r.h;
    const Rect minus{r.x, r.y, bw, r.h};
    const Rect plus{r.right() - bw, r.y, bw, r.h};
    const Rect mid{r.x + bw, r.y, r.w - 2 * bw, r.h};
    std::string idMinus(id), idPlus(id);
    idMinus += "/-";
    idPlus += "/+";

    int result = 0;
    if (enabled) {
        if (button(idMinus, minus, "−", ButtonKind::Normal, true)) result = -1;
        if (button(idPlus, plus, "+", ButtonKind::Normal, true)) result = +1;
    }
    Renderer2D& rr = *m_renderer;
    rr.roundRect(mid.shrink(4.0f, 0.0f), 6.0f, m_theme.inputBg);
    if (m_theme.font) rr.textCentered(*m_theme.font, mid, valueText, enabled ? m_theme.text : m_theme.textMuted);
    return result;
}

void Context::editText(uint64_t, std::string& value, size_t maxCodepoints, TextState& st) {
    Input& in = *m_input;
    auto insert = [&](std::string_view raw) {
        // Steuerzeichen entfernen, Zeilenumbrüche/Tabs werden zu Leerzeichen
        std::string text;
        for (size_t i = 0; i < raw.size();) {
            char32_t cp = utf8::next(raw, i);
            if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
            if (cp < 0x20 || cp == 0x7F || cp == utf8::kReplacement) continue;
            utf8::append(text, cp);
        }
        const size_t current = utf8::length(value);
        if (text.empty() || current >= maxCodepoints) return;
        const size_t room = maxCodepoints - current;
        const std::string_view fitted = std::string_view(text).substr(0, utf8::byteOffset(text, room));
        value.insert(st.caret, fitted);
        st.caret += fitted.size();
        st.lastEdit = m_time;
    };

    if (!in.text.empty()) insert(in.text);
    if (in.ctrl && in.pressed(Key::V) && m_window) insert(m_window->clipboardText());

    if (in.pressed(Key::Backspace) && st.caret > 0) {
        size_t from = utf8::prevBoundary(value, st.caret);
        if (in.ctrl) {
            while (from > 0 && value[from - 1] == ' ') from = utf8::prevBoundary(value, from);
            while (from > 0 && value[from - 1] != ' ') from = utf8::prevBoundary(value, from);
        }
        value.erase(from, st.caret - from);
        st.caret = from;
        st.lastEdit = m_time;
    }
    if (in.pressed(Key::Delete) && st.caret < value.size()) {
        const size_t to = utf8::nextBoundary(value, st.caret);
        value.erase(st.caret, to - st.caret);
        st.lastEdit = m_time;
    }
    if (in.pressed(Key::Left)) {
        st.caret = utf8::prevBoundary(value, st.caret);
        st.lastEdit = m_time;
    }
    if (in.pressed(Key::Right)) {
        st.caret = utf8::nextBoundary(value, st.caret);
        st.lastEdit = m_time;
    }
    if (in.pressed(Key::Home)) st.caret = 0;
    if (in.pressed(Key::End)) st.caret = value.size();
}

bool Context::textInput(std::string_view id, const Rect& r, std::string& value, std::string_view placeholder,
                        size_t maxCodepoints, bool enabled) {
    const uint64_t h = hash(id);
    TextState& st = m_textStates[h];
    Font* font = m_theme.font;
    Renderer2D& rr = *m_renderer;
    const Rect inner = r.shrink(12.0f, 0.0f);

    const bool hov = enabled && hovered(r);
    if (hov) m_cursor = Cursor::IBeam;
    if (!enabled && m_focus == h) m_focus = 0;
    if (hov && m_input->mousePressed()) {
        m_focus = h;
        m_focusClaimed = true;
        st.lastEdit = m_time;
        // Cursor an die Klickposition setzen
        if (font) {
            const float target = m_input->mouse.x - inner.x + st.scroll;
            size_t best = value.size();
            float x = 0.0f;
            for (size_t i = 0; i < value.size();) {
                const size_t start = i;
                utf8::next(value, i);
                const float w = font->measure(std::string_view(value).substr(start, i - start));
                if (x + w * 0.5f > target) {
                    best = start;
                    break;
                }
                x += w;
            }
            st.caret = best;
        }
    }

    const bool focused = m_focus == h;
    bool submitted = false;
    st.caret = std::min(st.caret, value.size());
    if (focused) {
        editText(h, value, maxCodepoints, st);
        if (m_input->pressed(Key::Enter)) submitted = true;
        if (m_input->pressed(Key::Escape)) m_focus = 0;
    }

    rr.roundRect(r, m_theme.radius, enabled ? m_theme.inputBg : m_theme.disabled.withAlpha(0.6f));
    rr.roundRectOutline(r, m_theme.radius, focused ? 1.5f : 1.0f, focused ? m_theme.focus : m_theme.inputBorder);
    if (!font) return submitted;

    const float caretX = font->measure(std::string_view(value).substr(0, st.caret));
    if (caretX - st.scroll > inner.w - 2.0f) st.scroll = caretX - inner.w + 2.0f;
    if (caretX - st.scroll < 0.0f) st.scroll = caretX;
    if (font->measure(value) < inner.w) st.scroll = 0.0f;

    const float baseline = r.y + r.h * 0.5f + font->capHeight() * 0.5f;
    rr.pushClip(inner.shrink(-2.0f, 0.0f));
    if (value.empty()) {
        rr.textBaseline(*font, {inner.x, baseline}, placeholder, m_theme.textMuted.fade(enabled ? 0.8f : 0.5f));
    } else {
        rr.textBaseline(*font, {inner.x - st.scroll, baseline}, value, enabled ? m_theme.text : m_theme.textMuted);
    }
    if (focused) {
        const double sinceEdit = m_time - st.lastEdit;
        const bool visible = sinceEdit < 0.5 || std::fmod(sinceEdit, 1.0) < 0.55;
        if (visible) {
            const float ch = font->lineHeight();
            rr.rect({inner.x + caretX - st.scroll, r.y + (r.h - ch) * 0.5f + 1.0f, 1.5f, ch - 2.0f}, m_theme.focus);
        }
    }
    rr.popClip();
    return submitted;
}

} // namespace eng::ui

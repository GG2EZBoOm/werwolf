#pragma once

#include "engine/core/Math.h"
#include "engine/platform/Window.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace eng {
class Font;
class Renderer2D;
} // namespace eng

namespace eng::ui {

struct Theme {
    Font* font = nullptr;
    Font* fontSmall = nullptr;
    Color text = Color::hex(0xECE7DF);
    Color textMuted = Color::hex(0x9AA3B5);
    Color panel = Color::hex(0x141A2B, 0.88f);
    Color panelBorder = Color::hex(0xFFFFFF, 0.08f);
    Color button = Color::hex(0x2A3450);
    Color buttonHover = Color::hex(0x35416A);
    Color buttonPressed = Color::hex(0x222A42);
    Color primary = Color::hex(0xD9A441);
    Color primaryHover = Color::hex(0xE8B85A);
    Color primaryText = Color::hex(0x1B1406);
    Color danger = Color::hex(0xB8433A);
    Color dangerHover = Color::hex(0xCE5348);
    Color inputBg = Color::hex(0x0C1020, 0.9f);
    Color inputBorder = Color::hex(0xFFFFFF, 0.12f);
    Color focus = Color::hex(0xD9A441);
    Color disabled = Color::hex(0x2A2F3D);
    Color disabledText = Color::hex(0x6B7180);
    float radius = 8.0f;
};

enum class ButtonKind { Normal, Primary, Danger, Ghost };

// Immediate-Mode-UI: Widgets werden jeden Frame neu beschrieben, der
// Zustand (Fokus, Cursorposition) wird anhand stabiler IDs gemerkt.
class Context {
public:
    void beginFrame(Window& window, Renderer2D& renderer, double time);
    void endFrame();

    Theme& theme() { return m_theme; }
    Renderer2D& renderer() { return *m_renderer; }
    Input& input() { return *m_input; }
    double time() const { return m_time; }

    bool button(std::string_view id, const Rect& r, std::string_view label, ButtonKind kind = ButtonKind::Normal,
                bool enabled = true);
    // Gibt true zurück, wenn Enter gedrückt wurde.
    bool textInput(std::string_view id, const Rect& r, std::string& value, std::string_view placeholder,
                   size_t maxCodepoints, bool enabled = true);
    // Gibt true zurück, wenn der Wert umgeschaltet wurde.
    bool checkbox(std::string_view id, const Rect& r, std::string_view label, bool& value, bool enabled = true);
    // [-] Wert [+]: liefert -1, 0 oder +1
    int stepper(std::string_view id, const Rect& r, std::string_view valueText, bool enabled = true);
    // Unsichtbare, klickbare Fläche
    bool clickArea(std::string_view id, const Rect& r, bool enabled = true);

    bool hovered(const Rect& r) const;
    void setCursor(Cursor c) { m_cursor = c; }

    void focus(std::string_view id);
    void clearFocus() { m_focus = 0; }
    bool isFocused(std::string_view id) const { return m_focus == hash(id); }
    bool anyFocused() const { return m_focus != 0; }

    static uint64_t hash(std::string_view s);

private:
    struct TextState {
        size_t caret = 0;
        float scroll = 0.0f;
        double lastEdit = 0.0;
    };

    bool interact(uint64_t id, const Rect& r, bool enabled, bool& isHovered, bool& isHeld);
    void editText(uint64_t id, std::string& value, size_t maxCodepoints, TextState& st);

    Window* m_window = nullptr;
    Renderer2D* m_renderer = nullptr;
    Input* m_input = nullptr;
    Theme m_theme;
    double m_time = 0.0;
    uint64_t m_active = 0;
    uint64_t m_focus = 0;
    bool m_focusClaimed = false;
    Cursor m_cursor = Cursor::Arrow;
    std::unordered_map<uint64_t, TextState> m_textStates;
};

} // namespace eng::ui

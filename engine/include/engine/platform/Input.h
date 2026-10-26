#pragma once

#include "engine/core/Math.h"

#include <array>
#include <cstdint>
#include <string>

namespace eng {

enum class Key : uint8_t {
    Unknown,
    Enter,
    Escape,
    Backspace,
    Delete,
    Tab,
    Space,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    M,
    Q,
    R,
    S,
    T,
    V,
    W,
    X,
    Shift,
    Ctrl,
    Num1,
    Num2,
    Num3,
    Num4,
    F1,
    F3,
    F11,
    F12,
    Count
};

enum class MouseButton : uint8_t { Left, Right, Middle, Count };

// Eingabezustand eines Frames. Koordinaten sind logische Pixel.
struct Input {
    Vec2 mouse{-1000.0f, -1000.0f};
    Vec2 mouseDelta{0.0f, 0.0f}; // rohe Mausbewegung dieses Frames (für die Kamera)
    float wheel = 0.0f;
    std::string text; // in diesem Frame getippter Text (UTF-8)
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool down(Key k) const { return m_keyDown[idx(k)]; }
    // Enthält auch automatische Tastenwiederholung.
    bool pressed(Key k) const { return m_keyPressed[idx(k)]; }
    bool mouseDown(MouseButton b = MouseButton::Left) const { return m_btnDown[idx(b)]; }
    bool mousePressed(MouseButton b = MouseButton::Left) const { return m_btnPressed[idx(b)]; }
    bool mouseReleased(MouseButton b = MouseButton::Left) const { return m_btnReleased[idx(b)]; }

    // Wird von der Plattformschicht aufgerufen.
    void onKey(Key k, bool isDown) {
        m_keyDown[idx(k)] = isDown;
        if (isDown) m_keyPressed[idx(k)] = true;
    }
    void onMouseButton(MouseButton b, bool isDown) {
        if (isDown && !m_btnDown[idx(b)]) m_btnPressed[idx(b)] = true;
        if (!isDown && m_btnDown[idx(b)]) m_btnReleased[idx(b)] = true;
        m_btnDown[idx(b)] = isDown;
    }
    void releaseAll() {
        m_keyDown.fill(false);
        for (size_t i = 0; i < m_btnDown.size(); ++i)
            if (m_btnDown[i]) m_btnReleased[i] = true;
        m_btnDown.fill(false);
        ctrl = shift = alt = false;
    }
    // Setzt alles zurück, was nur für einen Frame gilt.
    void endFrame() {
        m_keyPressed.fill(false);
        m_btnPressed.fill(false);
        m_btnReleased.fill(false);
        wheel = 0.0f;
        mouseDelta = {0.0f, 0.0f};
        text.clear();
    }
    // Verhindert, dass ein Klick von mehreren Elementen verarbeitet wird.
    void consumeMouse() {
        m_btnPressed.fill(false);
        m_btnReleased.fill(false);
    }

private:
    template <class E>
    static constexpr size_t idx(E e) {
        return static_cast<size_t>(e);
    }

    std::array<bool, static_cast<size_t>(Key::Count)> m_keyDown{};
    std::array<bool, static_cast<size_t>(Key::Count)> m_keyPressed{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_btnDown{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_btnPressed{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_btnReleased{};
};

} // namespace eng

#pragma once

#include "engine/platform/Input.h"

#include <functional>
#include <memory>
#include <string>

namespace eng {

enum class Cursor { Arrow, Hand, IBeam };

// Ein Fenster mit OpenGL-3.3-Core-Kontext.
// Alle Größen ohne "framebuffer" im Namen sind logische Pixel (physisch / dpiScale).
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const std::string& title, int logicalWidth, int logicalHeight, std::string* error = nullptr);
    void destroy();

    // Verarbeitet alle anstehenden Fensternachrichten. false = Fenster soll schließen.
    bool processEvents();
    void swapBuffers();
    void setVSync(bool enabled);

    int framebufferWidth() const;
    int framebufferHeight() const;
    float dpiScale() const;
    float width() const { return static_cast<float>(framebufferWidth()) / dpiScale(); }
    float height() const { return static_cast<float>(framebufferHeight()) / dpiScale(); }
    bool isMinimized() const;
    bool hasFocus() const;

    Input& input();
    void setCursor(Cursor cursor);
    void toggleFullscreen();
    void setTitle(const std::string& title);
    void requestClose();

    std::string clipboardText() const;
    void setClipboardText(const std::string& text);

    // Wird während des modalen Größenänderns/Verschiebens regelmäßig aufgerufen,
    // damit die Anwendung weiter zeichnen kann.
    std::function<void()> onModalFrame;

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace eng

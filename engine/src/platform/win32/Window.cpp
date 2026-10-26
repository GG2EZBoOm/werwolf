#include "engine/platform/Window.h"

#include "engine/core/Log.h"
#include "engine/core/Utf8.h"
#include "engine/gfx/GL.h"

#include <windows.h>
#include <windowsx.h>

namespace eng {

namespace {

constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_FLAGS_ARB = 0x2094;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

using PFN_wglCreateContextAttribsARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFN_wglSwapIntervalEXT = BOOL(WINAPI*)(int);

constexpr wchar_t kClassName[] = L"EngWindowClass";
constexpr UINT_PTR kModalTimer = 1;
constexpr float kMinLogicalWidth = 960.0f;
constexpr float kMinLogicalHeight = 600.0f;

Key mapKey(WPARAM vk) {
    switch (vk) {
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_BACK: return Key::Backspace;
    case VK_DELETE: return Key::Delete;
    case VK_TAB: return Key::Tab;
    case VK_SPACE: return Key::Space;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case 'A': return Key::A;
    case 'B': return Key::B;
    case 'C': return Key::C;
    case 'D': return Key::D;
    case 'E': return Key::E;
    case 'F': return Key::F;
    case 'G': return Key::G;
    case 'M': return Key::M;
    case 'Q': return Key::Q;
    case 'R': return Key::R;
    case 'S': return Key::S;
    case 'T': return Key::T;
    case 'V': return Key::V;
    case 'W': return Key::W;
    case 'X': return Key::X;
    case '1': return Key::Num1;
    case '2': return Key::Num2;
    case '3': return Key::Num3;
    case '4': return Key::Num4;
    case VK_SHIFT: return Key::Shift;
    case VK_CONTROL: return Key::Ctrl;
    case VK_F1: return Key::F1;
    case VK_F3: return Key::F3;
    case VK_F11: return Key::F11;
    case VK_F12: return Key::F12;
    default: return Key::Unknown;
    }
}

} // namespace

struct Window::Impl {
    Window* owner = nullptr;
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC glrc = nullptr;
    int fbWidth = 0;
    int fbHeight = 0;
    float scale = 1.0f;
    bool closeRequested = false;
    bool minimized = false;
    bool focused = true;
    bool fullscreen = false;
    WINDOWPLACEMENT previousPlacement{sizeof(WINDOWPLACEMENT)};
    Input input;
    Cursor cursor = Cursor::Arrow;
    HCURSOR cursors[3] = {};
    wchar_t pendingHighSurrogate = 0;
    PFN_wglSwapIntervalEXT swapInterval = nullptr;
    bool mouseCaptured = false;

    void applyCapture() {
        if (mouseCaptured && focused) {
            RECT r;
            GetClientRect(hwnd, &r);
            POINT tl{r.left, r.top}, br{r.right, r.bottom};
            ClientToScreen(hwnd, &tl);
            ClientToScreen(hwnd, &br);
            RECT clip{tl.x, tl.y, br.x, br.y};
            ClipCursor(&clip);
            SetCursor(nullptr);
        } else {
            ClipCursor(nullptr);
            SetCursor(cursors[static_cast<int>(cursor)]);
        }
    }

    void updateModifiers() {
        input.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        input.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        input.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    }

    void onChar(wchar_t c) {
        char32_t cp = c;
        if (c >= 0xD800 && c <= 0xDBFF) {
            pendingHighSurrogate = c;
            return;
        }
        if (c >= 0xDC00 && c <= 0xDFFF) {
            if (!pendingHighSurrogate) return;
            cp = 0x10000 + ((static_cast<char32_t>(pendingHighSurrogate) - 0xD800) << 10) + (c - 0xDC00);
            pendingHighSurrogate = 0;
        }
        if (cp < 0x20 || cp == 0x7F) return; // Steuerzeichen laufen über Tasten-Events
        utf8::append(input.text, cp);
    }

    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CLOSE:
            closeRequested = true;
            return 0;
        case WM_SIZE:
            fbWidth = LOWORD(lParam);
            fbHeight = HIWORD(lParam);
            minimized = wParam == SIZE_MINIMIZED;
            if (mouseCaptured) applyCapture();
            return 0;
        case WM_DPICHANGED: {
            scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = static_cast<LONG>(kMinLogicalWidth * scale * 0.85f);
            info->ptMinTrackSize.y = static_cast<LONG>(kMinLogicalHeight * scale * 0.85f);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(mouseCaptured && focused ? nullptr : cursors[static_cast<int>(cursor)]);
                return TRUE;
            }
            break;
        case WM_SETFOCUS:
            focused = true;
            applyCapture();
            return 0;
        case WM_KILLFOCUS:
            focused = false;
            input.releaseAll();
            ClipCursor(nullptr);
            return 0;
        case WM_INPUT: {
            RAWINPUT raw{};
            UINT size = sizeof raw;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) !=
                    static_cast<UINT>(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && focused) {
                input.mouseDelta.x += static_cast<float>(raw.data.mouse.lLastX);
                input.mouseDelta.y += static_cast<float>(raw.data.mouse.lLastY);
            }
            break;
        }
        case WM_MOVE:
            if (mouseCaptured) applyCapture();
            break;
        case WM_MOUSEMOVE:
            input.mouse = {static_cast<float>(GET_X_LPARAM(lParam)) / scale,
                           static_cast<float>(GET_Y_LPARAM(lParam)) / scale};
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            MouseButton b = MouseButton::Left;
            if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) b = MouseButton::Right;
            if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) b = MouseButton::Middle;
            const bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            input.mouse = {static_cast<float>(GET_X_LPARAM(lParam)) / scale,
                           static_cast<float>(GET_Y_LPARAM(lParam)) / scale};
            input.onMouseButton(b, down);
            if (down) SetCapture(hwnd);
            else ReleaseCapture();
            return 0;
        }
        case WM_MOUSEWHEEL:
            input.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            updateModifiers();
            if (wParam == VK_RETURN && input.alt) {
                owner->toggleFullscreen();
                return 0;
            }
            const Key k = mapKey(wParam);
            if (k != Key::Unknown) input.onKey(k, true);
            if (msg == WM_SYSKEYDOWN) break; // Alt+F4 usw. dem System überlassen
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            updateModifiers();
            const Key k = mapKey(wParam);
            if (k != Key::Unknown) input.onKey(k, false);
            if (msg == WM_SYSKEYUP) break;
            return 0;
        }
        case WM_CHAR:
            onChar(static_cast<wchar_t>(wParam));
            return 0;
        case WM_SYSCHAR:
            return 0; // kein Warnton bei Alt+Enter
        case WM_ENTERSIZEMOVE:
            SetTimer(hwnd, kModalTimer, 16, nullptr);
            return 0;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, kModalTimer);
            return 0;
        case WM_TIMER:
            if (wParam == kModalTimer && owner->onModalFrame) owner->onModalFrame();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* self = static_cast<Impl*>(cs->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
        return self->handle(msg, wParam, lParam);
    }
};

Window::Window() : m(std::make_unique<Impl>()) { m->owner = this; }

Window::~Window() { destroy(); }

bool Window::create(const std::string& title, int logicalWidth, int logicalHeight, std::string* error) {
    auto fail = [&](const std::string& msg) {
        log::error("Fenster: {}", msg);
        if (error) *error = msg;
        destroy();
        return false;
    };

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc); // darf fehlschlagen, falls schon registriert

    m->cursors[0] = LoadCursorW(nullptr, IDC_ARROW);
    m->cursors[1] = LoadCursorW(nullptr, IDC_HAND);
    m->cursors[2] = LoadCursorW(nullptr, IDC_IBEAM);

    const UINT systemDpi = GetDpiForSystem();
    m->scale = static_cast<float>(systemDpi) / 96.0f;
    const DWORD style = WS_OVERLAPPEDWINDOW;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rect{0, 0, static_cast<LONG>(static_cast<float>(logicalWidth) * m->scale),
              static_cast<LONG>(static_cast<float>(logicalHeight) * m->scale)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, systemDpi);
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int winW = std::min<int>(rect.right - rect.left, workW);
    const int winH = std::min<int>(rect.bottom - rect.top, workH);
    const int x = work.left + (workW - winW) / 2;
    const int y = work.top + (workH - winH) / 2;

    const std::wstring wtitle = utf8::toWide(title);
    CreateWindowExW(0, kClassName, wtitle.c_str(), style, x, y, winW, winH, nullptr, nullptr, instance, m.get());
    if (!m->hwnd) return fail("CreateWindowEx fehlgeschlagen");
    m->scale = static_cast<float>(GetDpiForWindow(m->hwnd)) / 96.0f;

    m->hdc = GetDC(m->hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ChoosePixelFormat(m->hdc, &pfd);
    if (!format || !SetPixelFormat(m->hdc, format, &pfd)) return fail("Kein passendes Pixelformat");

    HGLRC legacy = wglCreateContext(m->hdc);
    if (!legacy || !wglMakeCurrent(m->hdc, legacy)) return fail("OpenGL-Kontext konnte nicht erstellt werden");

    auto createContextAttribs =
        reinterpret_cast<PFN_wglCreateContextAttribsARB>(wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!createContextAttribs) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(legacy);
        return fail("Der Grafiktreiber unterstützt kein OpenGL 3.3");
    }
    const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                           WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                           WGL_CONTEXT_FLAGS_ARB, 0, 0};
    m->glrc = createContextAttribs(m->hdc, nullptr, attribs);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!m->glrc || !wglMakeCurrent(m->hdc, m->glrc)) return fail("OpenGL-3.3-Kontext konnte nicht erstellt werden");

    if (!gl::loadFunctions()) return fail("OpenGL-Funktionen konnten nicht geladen werden");
    m->swapInterval = reinterpret_cast<PFN_wglSwapIntervalEXT>(wglGetProcAddress("wglSwapIntervalEXT"));
    setVSync(true);

    log::info("OpenGL {} | {} | DPI-Skalierung {:.2f}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)), m->scale);

    // Rohe Mausdaten für die Kamerasteuerung
    RAWINPUTDEVICE mouse{};
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.hwndTarget = m->hwnd;
    RegisterRawInputDevices(&mouse, 1, sizeof mouse);

    ShowWindow(m->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m->hwnd);
    RECT client{};
    GetClientRect(m->hwnd, &client);
    m->fbWidth = client.right - client.left;
    m->fbHeight = client.bottom - client.top;
    return true;
}

void Window::destroy() {
    if (m->mouseCaptured) ClipCursor(nullptr);
    m->mouseCaptured = false;
    if (m->glrc) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m->glrc);
        m->glrc = nullptr;
    }
    if (m->hdc && m->hwnd) ReleaseDC(m->hwnd, m->hdc);
    m->hdc = nullptr;
    if (m->hwnd) {
        DestroyWindow(m->hwnd);
        m->hwnd = nullptr;
    }
}

bool Window::processEvents() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) m->closeRequested = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !m->closeRequested;
}

void Window::swapBuffers() {
    if (m->hdc) SwapBuffers(m->hdc);
}

void Window::setVSync(bool enabled) {
    if (m->swapInterval) m->swapInterval(enabled ? 1 : 0);
}

int Window::framebufferWidth() const { return std::max(1, m->fbWidth); }
int Window::framebufferHeight() const { return std::max(1, m->fbHeight); }
float Window::dpiScale() const { return m->scale; }
bool Window::isMinimized() const { return m->minimized; }
bool Window::hasFocus() const { return m->focused; }
Input& Window::input() { return m->input; }

void Window::setCursor(Cursor cursor) {
    if (m->cursor == cursor) return;
    m->cursor = cursor;
    POINT p;
    GetCursorPos(&p);
    if (WindowFromPoint(p) == m->hwnd) SetCursor(m->cursors[static_cast<int>(cursor)]);
}

void Window::setMouseCaptured(bool captured) {
    if (m->mouseCaptured == captured) return;
    m->mouseCaptured = captured;
    m->applyCapture();
}

bool Window::isMouseCaptured() const { return m->mouseCaptured; }

void Window::toggleFullscreen() {
    // Randloses Vollbild nach dem bewährten Muster aus "The Old New Thing".
    const DWORD style = static_cast<DWORD>(GetWindowLongW(m->hwnd, GWL_STYLE));
    if (!m->fullscreen) {
        MONITORINFO mi{sizeof mi};
        if (GetWindowPlacement(m->hwnd, &m->previousPlacement) &&
            GetMonitorInfoW(MonitorFromWindow(m->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(m->hwnd, GWL_STYLE, static_cast<LONG>(style & ~WS_OVERLAPPEDWINDOW));
            SetWindowPos(m->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m->fullscreen = true;
        }
    } else {
        SetWindowLongW(m->hwnd, GWL_STYLE, static_cast<LONG>(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(m->hwnd, &m->previousPlacement);
        SetWindowPos(m->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m->fullscreen = false;
    }
}

void Window::setTitle(const std::string& title) {
    if (m->hwnd) SetWindowTextW(m->hwnd, utf8::toWide(title).c_str());
}

void Window::requestClose() { m->closeRequested = true; }

std::string Window::clipboardText() const {
    std::string result;
    if (!OpenClipboard(m->hwnd)) return result;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
            result = utf8::fromWide(text);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

void Window::setClipboardText(const std::string& text) {
    if (!OpenClipboard(m->hwnd)) return;
    EmptyClipboard();
    const std::wstring wide = utf8::toWide(text);
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, wide.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

} // namespace eng

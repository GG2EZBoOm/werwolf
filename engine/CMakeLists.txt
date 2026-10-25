# --- Kern: plattformunabhängige Hilfsmittel ------------------------------------
add_library(engine_core STATIC
    src/core/ByteBuffer.cpp
    src/core/Image.cpp
    src/core/Log.cpp
    src/core/Paths.cpp
    src/core/Time.cpp
    src/core/Utf8.cpp
)
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PUBLIC Threads::Threads)

# --- Netzwerk: TCP mit Nachrichten-Framing (Winsock / BSD-Sockets) -------------
add_library(engine_net STATIC
    src/net/Net.cpp
)
target_link_libraries(engine_net PUBLIC engine_core)
if(WIN32)
    target_link_libraries(engine_net PUBLIC ws2_32)
endif()

# --- Client: Fenster, OpenGL-Renderer, Schrift, UI (Win32) ---------------------
if(WERWOLF_BUILD_CLIENT)
    add_library(engine_client STATIC
        src/platform/win32/Window.cpp
        src/gfx/GL.cpp
        src/gfx/Renderer2D.cpp
        src/gfx/Font_win32.cpp
        src/ui/UI.cpp
    )
    target_link_libraries(engine_client PUBLIC engine_core opengl32 gdi32 user32 shell32)
endif()

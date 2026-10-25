# --- Spielregeln + Protokoll (plattformunabhängig) -----------------------------
add_library(werwolf_logic STATIC
    logic/Types.cpp
    logic/Game.cpp
)
target_include_directories(werwolf_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(werwolf_logic PUBLIC engine_core)

# --- Server-Bibliothek (wird vom dedizierten Server und vom Client genutzt) ----
add_library(werwolf_server_lib STATIC
    server/Bot.cpp
    server/GameServer.cpp
    client/NetClient.cpp
)
target_link_libraries(werwolf_server_lib PUBLIC werwolf_logic engine_net)

# --- Dedizierter Server ---------------------------------------------------------
add_executable(werwolf_server server/main.cpp)
target_link_libraries(werwolf_server PRIVATE werwolf_server_lib)

# --- Grafischer Client ----------------------------------------------------------
if(WERWOLF_BUILD_CLIENT)
    add_executable(werwolf WIN32
        client/main.cpp
        client/App.cpp
        client/Config.cpp
        client/Scenery.cpp
        client/MenuScreen.cpp
        client/LobbyScreen.cpp
        client/GameScreen.cpp
        client/ChatPanel.cpp
    )
    target_link_libraries(werwolf PRIVATE werwolf_server_lib engine_client)
endif()

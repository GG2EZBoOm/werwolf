# --- Spielregeln + Protokoll (plattformunabhängig) -----------------------------
add_library(werwolf_logic STATIC
    logic/Types.cpp
    logic/Match.cpp
    world/World.cpp
    world/Movement.cpp
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

# --- Auto-Updater: Kern (plattformunabhängig) und Download/Installation (Windows) -
add_library(werwolf_update STATIC
    update/UpdateCore.cpp
)
target_include_directories(werwolf_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(werwolf_update PUBLIC engine_core)

if(WIN32)
    add_library(werwolf_updater STATIC
        client/Updater.cpp
    )
    target_link_libraries(werwolf_updater PUBLIC werwolf_update engine_http)
endif()

# --- Dedizierter Server ---------------------------------------------------------
add_executable(werwolf_server server/main.cpp)
target_link_libraries(werwolf_server PRIVATE werwolf_server_lib)
target_compile_definitions(werwolf_server PRIVATE WERWOLF_VERSION="${PROJECT_VERSION}")

# --- Grafischer Client ----------------------------------------------------------
if(WERWOLF_BUILD_CLIENT)
    add_executable(werwolf WIN32
        client/main.cpp
        client/App.cpp
        client/Config.cpp
        client/MenuScreen.cpp
        client/LobbyScreen.cpp
        client/GameScreen.cpp
        client/ChatPanel.cpp
        client/WorldRenderer.cpp
        client/Characters.cpp
        client/Controller.cpp
        client/SandboxScreen.cpp
        client/SettingsScreen.cpp
        client/Style.cpp
        client/StyleMaterials.cpp
        client/OverlayScreens.cpp
    )
    target_link_libraries(werwolf PRIVATE werwolf_server_lib werwolf_updater engine_client)
    target_compile_definitions(werwolf PRIVATE
        WERWOLF_VERSION="${PROJECT_VERSION}"
        WERWOLF_UPDATE_URL="${WERWOLF_UPDATE_URL}"
    )
endif()

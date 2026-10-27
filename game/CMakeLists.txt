# --- Spielregeln + Protokoll (plattformunabhängig) -----------------------------
add_library(werwolf_logic STATIC
    i18n/Text.cpp
    logic/Types.cpp
    logic/Match.cpp
    world/World.cpp
    world/Movement.cpp
)
target_include_directories(werwolf_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(werwolf_logic PUBLIC engine_core)

add_library(werwolf_audio STATIC audio/Sounds.cpp audio/SoundEvents.cpp)
target_link_libraries(werwolf_audio PUBLIC werwolf_logic engine_audio)

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
    target_link_libraries(werwolf_updater PUBLIC werwolf_update engine_http werwolf_logic)
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
        client/AudioScene.cpp
        client/HostNetwork.cpp
        client/Discord.cpp
    )
    target_link_libraries(werwolf PRIVATE werwolf_server_lib werwolf_updater engine_client werwolf_audio)
    set(WERWOLF_MUSIC_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio" CACHE PATH "Lokal importierte Musik")
    set(_music_available TRUE)
    foreach(_track day_contemplation_1 day_contemplation_2 night_foreshadow night_eyes_in_the_dark night_creeping_death)
        if(NOT EXISTS "${WERWOLF_MUSIC_ASSET_DIR}/${_track}.wav")
            set(_music_available FALSE)
        endif()
    endforeach()
    if(_music_available)
        enable_language(RC)
        configure_file(client/Music.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Music.rc" @ONLY)
        target_sources(werwolf PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Music.rc")
        target_compile_definitions(werwolf PRIVATE WERWOLF_HAS_MUSIC=1)
        message(STATUS "Musik: 2 Tagestracks und 3 Nacht-Stinger werden eingebettet")
    else()
        message(STATUS "Musikpakete fehlen: nur prozedurale Sounds (Import: tools/import_music.py)")
    endif()
    set(WERWOLF_FOOTSTEP_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio" CACHE PATH "Lokal importierte Schrittaufnahmen")
    set(_footsteps_available TRUE)
    set(_footstep_resources "")
    foreach(_step RANGE 1 20)
        if(_step LESS 10)
            set(_step_name "footstep_0${_step}.wav")
        else()
            set(_step_name "footstep_${_step}.wav")
        endif()
        if(NOT EXISTS "${WERWOLF_FOOTSTEP_ASSET_DIR}/${_step_name}")
            set(_footsteps_available FALSE)
        endif()
        math(EXPR _resource_id "300 + ${_step}")
        string(APPEND _footstep_resources "${_resource_id} RCDATA \"${WERWOLF_FOOTSTEP_ASSET_DIR}/${_step_name}\"\n")
    endforeach()
    if(_footsteps_available)
        enable_language(RC)
        configure_file(client/Footsteps.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Footsteps.rc" @ONLY)
        target_sources(werwolf PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Footsteps.rc")
        target_compile_definitions(werwolf PRIVATE WERWOLF_HAS_FOOTSTEPS=1)
        message(STATUS "Schritte: 20 Aufnahmen werden eingebettet")
    else()
        message(STATUS "Schrittaufnahmen fehlen: prozeduraler Ersatz (Import: tools/import_footsteps.py)")
    endif()
    target_compile_definitions(werwolf PRIVATE
        WERWOLF_VERSION="${PROJECT_VERSION}"
        WERWOLF_UPDATE_URL="${WERWOLF_UPDATE_URL}"
    )
endif()

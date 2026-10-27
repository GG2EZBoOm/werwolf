cmake_minimum_required(VERSION 3.21)
project(Werwolf VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
    set(_client_default ON)
else()
    set(_client_default OFF)
endif()
option(WERWOLF_BUILD_CLIENT "Grafischen Client bauen (aktuell nur Windows)" ${_client_default})
option(WERWOLF_BUILD_TESTS "Tests bauen" ON)
# Woher der Auto-Updater die Datei update.json lädt (leer = Updater aus).
# Mit GitHub Releases zeigt "latest/download" immer auf das neueste veröffentlichte Release.
set(WERWOLF_UPDATE_URL "https://github.com/GG2EZBoOm/werwolf/releases/latest/download/update.json"
    CACHE STRING "Adresse der update.json für den Auto-Updater")

find_package(Threads REQUIRED)

add_subdirectory(engine)
add_subdirectory(game)

if(WERWOLF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

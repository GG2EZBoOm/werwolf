#include "engine/gfx/GL.h"

#include "engine/core/Log.h"

#define ENG_GL_DEFINE(ret, name, ...) PFN_##name name = nullptr;
ENG_GL_FUNCTIONS(ENG_GL_DEFINE)
#undef ENG_GL_DEFINE

namespace eng::gl {
namespace {

void* loadProc(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    const auto v = reinterpret_cast<std::intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
        static HMODULE module = LoadLibraryW(L"opengl32.dll");
        p = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    }
    return p;
}

} // namespace

bool loadFunctions() {
    bool ok = true;
#define ENG_GL_LOAD(ret, name, ...)                                                                                \
    name = reinterpret_cast<PFN_##name>(loadProc(#name));                                                         \
    if (!name) {                                                                                                   \
        log::error("OpenGL-Funktion fehlt: {}", #name);                                                           \
        ok = false;                                                                                                \
    }
    ENG_GL_FUNCTIONS(ENG_GL_LOAD)
#undef ENG_GL_LOAD
    return ok;
}

} // namespace eng::gl

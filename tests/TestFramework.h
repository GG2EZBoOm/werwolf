#pragma once

// Minimales Testgerüst ohne externe Abhängigkeiten.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int runAll() {
    int failedCases = 0;
    for (const Case& c : registry()) {
        const int before = failures();
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (failures() == before) {
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            std::printf("[FEHLER] %s\n", c.name);
            ++failedCases;
        }
    }
    std::printf("\n%zu Tests, %d Prüfungen, %d fehlgeschlagen\n", registry().size(), checks(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

} // namespace test

#define TEST(name)                                                                                                 \
    static void name();                                                                                            \
    static test::Registrar registrar_##name(#name, name);                                                          \
    static void name()

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        ++test::checks();                                                                                          \
        if (!(cond)) {                                                                                             \
            ++test::failures();                                                                                    \
            std::printf("    Prüfung fehlgeschlagen (%s:%d): %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                                             \
    do {                                                                                                           \
        ++test::checks();                                                                                          \
        if (!((a) == (b))) {                                                                                       \
            ++test::failures();                                                                                    \
            std::printf("    Prüfung fehlgeschlagen (%s:%d): %s == %s\n", __FILE__, __LINE__, #a, #b);            \
        }                                                                                                          \
    } while (0)

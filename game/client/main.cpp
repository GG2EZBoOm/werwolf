#include "client/App.h"

#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ww::App app;
    return app.run();
}

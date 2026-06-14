// main.cpp (SDL2 platform) - entry point for cross-platform build.

#include <SDL.h>
#include "sdl_app.h"
#include "app.h"
#include <cstdio>
#include <exception>
#include <string>

int main(int /*argc*/, char* /*argv*/[]) {
    try {
        // settings.ini and roms/ next to the executable.
        std::string settings_path = "settings.ini";
        std::string roms_dir;
        char* base = SDL_GetBasePath();
        if (base) {
            settings_path = std::string(base) + "settings.ini";
            roms_dir      = std::string(base) + "roms";
            SDL_free(base);
        }

        apple1::create_app(settings_path, roms_dir);
        apple1::sdl2::SdlApp sdl_app;
        if (!sdl_app.init()) {
            apple1::destroy_app();
            return 1;
        }
        int rc = sdl_app.run();
        sdl_app.shutdown();
        apple1::destroy_app();
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal: %s\n", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Apple-1 Emulator", e.what(), nullptr);
        return 1;
    }
}

// sdl_app.h - SDL2 platform shim for the Apple-1 emulator.
//
// This is the cross-platform alternative to the Win32/Direct2D code in
// ../windows/.  It owns the SDL_Window, the SDL_Renderer, the glyph atlas
// texture, and runs the GUI event loop.  The Bus/CPU/Display/Settings
// live in the shared core (apple1::App).

#pragma once

#include <SDL.h>
#include "common.h"
#include "settings.h"
#include "display_grid.h"

namespace apple1 {
class App;
} // namespace apple1

namespace apple1::sdl2 {

class SdlApp {
public:
    SdlApp() = default;
    ~SdlApp();

    bool init();             // window, renderer, atlas
    int  run();              // main loop until quit
    void shutdown();

private:
    void build_atlas();      // create texture from 2513 char ROM
    void rebuild_atlas_if_needed();  // checks settings.phosphor() vs cached
    void render_frame();
    void render_text(const DisplayGrid::Snapshot& snap, int ox, int oy,
                     int cell_w, int cell_h);
    void render_dots(int ox, int oy, int cell_w, int cell_h, bool boot_mode);
    void render_scanlines(int ox, int oy, int cell_w, int cell_h);
    void render_vignette();

    void handle_key(SDL_Keysym key);     // F1..F4, F11 etc
    void handle_text(const char* utf8);  // typed characters
    void compute_layout(int& cell_w, int& cell_h, int& ox, int& oy) const;

    void on_resize(int w, int h);

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  atlas_    = nullptr;
    SDL_Texture*  vignette_tex_ = nullptr;

    int           window_w_ = 0;
    int           window_h_ = 0;

    int           cached_phosphor_ = -1;   // -1 means "build first time"
    bool          cached_vignette_dims_ = false;
    int           vignette_w_ = 0;
    int           vignette_h_ = 0;

    bool          running_ = false;
};

} // namespace apple1::sdl2

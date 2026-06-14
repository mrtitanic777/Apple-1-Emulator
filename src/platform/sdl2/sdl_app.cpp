// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// sdl_app.cpp - SDL2 platform shim.

#include "sdl_app.h"
#include "app.h"
#include "bus.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>

namespace apple1::sdl2 {

namespace {

constexpr int kGlyphW = 7;
constexpr int kGlyphH = 8;
constexpr int kGlyphsPerRow = 64;
constexpr int kAtlasW = kGlyphsPerRow * kGlyphW;
constexpr int kAtlasH = kGlyphH;

// Default to 3x scale plus 3-cell bezel on each side - mirrors the
// Windows version's default.
constexpr int kBezelCells   = 3;
constexpr int kDefaultScale = 3;

constexpr int default_window_w() {
    return (kCols + 2 * kBezelCells) * kGlyphW * kDefaultScale;   // 966
}
constexpr int default_window_h() {
    return (kRows + 2 * kBezelCells) * kGlyphH * kDefaultScale;   // 720
}

constexpr u8 ascii_to_glyph(u8 code) {
    return static_cast<u8>((code - 0x40) & 0x3F);
}

struct RGB { u8 r, g, b; };

RGB phosphor_color(Phosphor p) {
    switch (p) {
        case Phosphor::Green: return { 77,  255, 102 };
        case Phosphor::Amber: return { 255, 189,  51 };
        case Phosphor::White:
        default:              return { 255, 255, 255 };
    }
}

} // namespace

SdlApp::~SdlApp() {
    shutdown();
}

bool SdlApp::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Apple-1 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        default_window_w(), default_window_h(),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    // No VSYNC: pace explicitly to 60Hz via steady_clock below so the
    // emulated CPU's frame-tied work (display putc pacing, etc.) doesn't
    // inherit the host monitor's refresh rate (75/120/144 Hz machines
    // were giving non-deterministic disk-load behavior because CPU work
    // got more wall-clock per frame on faster panels).
    renderer_ = SDL_CreateRenderer(
        window_, -1,
        SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    SDL_GetWindowSize(window_, &window_w_, &window_h_);

    build_atlas();
    SDL_StartTextInput();

    return true;
}

void SdlApp::shutdown() {
    if (vignette_tex_) { SDL_DestroyTexture(vignette_tex_); vignette_tex_ = nullptr; }
    if (atlas_)        { SDL_DestroyTexture(atlas_);        atlas_ = nullptr; }
    if (renderer_)     { SDL_DestroyRenderer(renderer_);    renderer_ = nullptr; }
    if (window_)       { SDL_DestroyWindow(window_);        window_ = nullptr; }
    SDL_Quit();
}

void SdlApp::build_atlas() {
    const auto& chargen = app().rom_set().chargen;
    if (chargen.size() != 512) return;

    RGB col = phosphor_color(app().settings().phosphor());
    cached_phosphor_ = static_cast<int>(app().settings().phosphor());

    // RGBA pixel buffer, lit = phosphor color, dark = transparent.
    std::vector<u32> pixels(kAtlasW * kAtlasH, 0);
    for (int g = 0; g < kGlyphsPerRow; ++g) {
        const u8* glyph_bytes = chargen.data() + g * 8;
        for (int row = 0; row < kGlyphH; ++row) {
            u8 b = glyph_bytes[row];
            for (int col_i = 0; col_i < kGlyphW; ++col_i) {
                bool lit = (b & (1 << (6 - col_i))) != 0;
                int x = g * kGlyphW + col_i;
                int y = row;
                if (lit) {
                    // RGBA packed little-endian as ABGR in u32 for
                    // SDL_PIXELFORMAT_RGBA32.
                    pixels[y * kAtlasW + x] =
                        static_cast<u32>(col.r)
                      | (static_cast<u32>(col.g) <<  8)
                      | (static_cast<u32>(col.b) << 16)
                      | (static_cast<u32>(0xFF)  << 24);
                }
            }
        }
    }

    if (atlas_) { SDL_DestroyTexture(atlas_); atlas_ = nullptr; }
    atlas_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC, kAtlasW, kAtlasH);
    if (!atlas_) return;

    SDL_UpdateTexture(atlas_, nullptr, pixels.data(), kAtlasW * 4);
    SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas_, SDL_ScaleModeNearest);
}

void SdlApp::rebuild_atlas_if_needed() {
    int wanted = static_cast<int>(app().settings().phosphor());
    if (wanted != cached_phosphor_) build_atlas();
}

void SdlApp::compute_layout(int& cell_w, int& cell_h,
                            int& ox, int& oy) const {
    // Pick largest integer scale that fits with the bezel padding.
    int avail_w = std::max(window_w_, 1);
    int avail_h = std::max(window_h_, 1);
    int scale_w = avail_w / ((kCols + 2 * kBezelCells) * kGlyphW);
    int scale_h = avail_h / ((kRows + 2 * kBezelCells) * kGlyphH);
    int scale = std::min(scale_w, scale_h);
    if (scale < 1) scale = 1;
    cell_w = kGlyphW * scale;
    cell_h = kGlyphH * scale;
    int total_w = cell_w * kCols;
    int total_h = cell_h * kRows;
    ox = (window_w_ - total_w) / 2;
    oy = (window_h_ - total_h) / 2;
}

void SdlApp::render_text(const DisplayGrid::Snapshot& snap,
                         int ox, int oy, int cell_w, int cell_h) {
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            u8  code = snap.grid[y][x];
            bool is_cursor = snap.cursor_visible
                          && y == snap.cursor_row
                          && x == snap.cursor_col;
            if (snap.boot_mode && !snap.boot_blink_on && code == 0x40)
                continue;
            if (is_cursor) {
                code = 0x40;
            } else {
                if (code == 0x20) continue;
                if (code < 0x20 || code > 0x5F) continue;
            }
            int glyph = ascii_to_glyph(code);
            SDL_Rect src = { glyph * kGlyphW, 0, kGlyphW, kGlyphH };
            SDL_Rect dst = { ox + cell_w * x, oy + cell_h * y, cell_w, cell_h };
            SDL_RenderCopy(renderer_, atlas_, &src, &dst);
        }
    }
    // Cursor at col 40 (one past line end), parked before wrap.
    if (snap.cursor_visible && snap.cursor_col == kCols
        && snap.cursor_row >= 0 && snap.cursor_row < kRows
        && !snap.boot_mode) {
        int glyph = ascii_to_glyph(0x40);
        SDL_Rect src = { glyph * kGlyphW, 0, kGlyphW, kGlyphH };
        SDL_Rect dst = { ox + cell_w * kCols, oy + cell_h * snap.cursor_row,
                         cell_w, cell_h };
        SDL_RenderCopy(renderer_, atlas_, &src, &dst);
    }
}

void SdlApp::render_dots(int ox, int oy, int cell_w, int cell_h,
                         bool boot_mode) {
    if (boot_mode) return;
    if (!app().settings().dot_artifact()) return;

    RGB col = phosphor_color(app().settings().phosphor());
    SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, 30);   // ~12%

    int dot_size = cell_w / kGlyphW;
    if (dot_size < 1) dot_size = 1;

    // Active area: every other cell.
    for (int y = 0; y < kRows; ++y) {
        for (int x = 1; x < kCols; x += 2) {
            int cx = ox + cell_w * x + cell_w / 2 - dot_size / 2;
            int cy = oy + cell_h * y + cell_h / 2 - dot_size / 2;
            SDL_Rect r = { cx, cy, dot_size, dot_size };
            SDL_RenderFillRect(renderer_, &r);
        }
    }
    // Bezel below: 3 rows, dots every 10 cells.
    for (int row_below = 0; row_below < 3; ++row_below) {
        int cy = oy + cell_h * kRows + cell_h * row_below
               + cell_h / 2 - dot_size / 2;
        for (int x = 9; x < kCols; x += 10) {
            int cx = ox + cell_w * x + cell_w / 2 - dot_size / 2;
            SDL_Rect r = { cx, cy, dot_size, dot_size };
            SDL_RenderFillRect(renderer_, &r);
        }
    }
}

void SdlApp::render_scanlines(int ox, int oy, int cell_w, int cell_h) {
    if (!app().settings().scanlines()) return;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 128);  // ~50%
    int pix_h = cell_h / kGlyphH;
    if (pix_h < 1) pix_h = 1;
    for (int py = 0; py < kRows * kGlyphH; ++py) {
        if ((py & 1) == 0) continue;
        SDL_Rect r = { ox, oy + py * pix_h, cell_w * kCols, pix_h };
        SDL_RenderFillRect(renderer_, &r);
    }
}

void SdlApp::render_vignette() {
    if (!app().settings().vignette()) return;

    // Build / rebuild the vignette texture when the window size changes.
    if (!vignette_tex_ || vignette_w_ != window_w_ || vignette_h_ != window_h_) {
        if (vignette_tex_) { SDL_DestroyTexture(vignette_tex_); vignette_tex_ = nullptr; }
        vignette_w_ = window_w_;
        vignette_h_ = window_h_;
        if (vignette_w_ <= 0 || vignette_h_ <= 0) return;

        // Build a radial gradient ARGB image: transparent in center,
        // dark at corners.  Same falloff as the Win32 version.
        std::vector<u32> pixels(vignette_w_ * vignette_h_);
        float cx = vignette_w_ * 0.5f;
        float cy = vignette_h_ * 0.5f;
        float rx = vignette_w_ * 0.55f;
        float ry = vignette_h_ * 0.55f;
        for (int y = 0; y < vignette_h_; ++y) {
            for (int x = 0; x < vignette_w_; ++x) {
                float dx = (x - cx) / rx;
                float dy = (y - cy) / ry;
                float r = std::sqrt(dx * dx + dy * dy);    // 0 center, 1 edge
                // Falloff: 0..0.65 -> 0..0.10, 0.65..1.0 -> 0.10..0.75
                float a;
                if (r <= 0.65f) {
                    a = (r / 0.65f) * 0.10f;
                } else if (r >= 1.0f) {
                    a = 0.75f;
                } else {
                    a = 0.10f + (r - 0.65f) / 0.35f * 0.65f;
                }
                u8 alpha = static_cast<u8>(a * 255.0f);
                pixels[y * vignette_w_ + x] =
                      0    // R
                    | (0 <<  8)
                    | (0 << 16)
                    | (static_cast<u32>(alpha) << 24);
            }
        }
        vignette_tex_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC, vignette_w_, vignette_h_);
        if (!vignette_tex_) return;
        SDL_UpdateTexture(vignette_tex_, nullptr, pixels.data(), vignette_w_ * 4);
        SDL_SetTextureBlendMode(vignette_tex_, SDL_BLENDMODE_BLEND);
    }
    if (vignette_tex_) {
        SDL_Rect dst = { 0, 0, window_w_, window_h_ };
        SDL_RenderCopy(renderer_, vignette_tex_, nullptr, &dst);
    }
}

void SdlApp::render_frame() {
    rebuild_atlas_if_needed();

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    int cell_w, cell_h, ox, oy;
    compute_layout(cell_w, cell_h, ox, oy);

    auto snap = app().display().snapshot();
    render_text(snap, ox, oy, cell_w, cell_h);
    render_dots(ox, oy, cell_w, cell_h, snap.boot_mode);
    render_scanlines(ox, oy, cell_w, cell_h);
    render_vignette();

    SDL_RenderPresent(renderer_);
}

void SdlApp::on_resize(int w, int h) {
    window_w_ = w;
    window_h_ = h;
}

void SdlApp::handle_key(SDL_Keysym key) {
    SDL_Keymod mod = static_cast<SDL_Keymod>(key.mod);
    Settings& s = app().settings();

    switch (key.sym) {
        case SDLK_F1:
            // CLEAR SCREEN
            app().clear_screen();
            return;
        case SDLK_F2:
            // RESET CPU
            app().reset_cpu();
            return;
        case SDLK_F3:
            // Toggle scanlines
            s.set_scanlines(!s.scanlines());
            return;
        case SDLK_F4:
            // Toggle vignette
            s.set_vignette(!s.vignette());
            return;
        case SDLK_F5:
            // Toggle dot artifact
            s.set_dot_artifact(!s.dot_artifact());
            return;
        case SDLK_F6:
            // Cycle phosphor color
            switch (s.phosphor()) {
                case Phosphor::White: s.set_phosphor(Phosphor::Green); break;
                case Phosphor::Green: s.set_phosphor(Phosphor::Amber); break;
                case Phosphor::Amber: s.set_phosphor(Phosphor::White); break;
            }
            return;
        case SDLK_F7:
            // Toggle teletype pacing
            s.set_teletype_pacing(!s.teletype_pacing());
            app().display().set_pacing(s.teletype_pacing());
            return;
        case SDLK_F11: {
            // Toggle fullscreen
            Uint32 flags = SDL_GetWindowFlags(window_);
            if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                SDL_SetWindowFullscreen(window_, 0);
            else
                SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
            return;
        }
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            app().bus().feed_key(0x0D);
            return;
        case SDLK_BACKSPACE:
        case SDLK_LEFT:
            app().bus().feed_key('_');  // Apple-1 uses _ as backspace
            return;
        case SDLK_ESCAPE:
            app().bus().feed_key(0x1B);
            return;
    }

    // Ctrl-letter
    if (mod & KMOD_CTRL) {
        if (key.sym >= SDLK_a && key.sym <= SDLK_z) {
            u8 c = static_cast<u8>(key.sym - SDLK_a + 1);
            app().bus().feed_key(c);
            return;
        }
    }
}

void SdlApp::handle_text(const char* utf8) {
    // Apple-1 input is uppercase ASCII.  Take the first byte and uppercase.
    if (!utf8 || !utf8[0]) return;
    unsigned char c = static_cast<unsigned char>(utf8[0]);
    if (c < 0x20 || c > 0x7E) return;
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 32);
    app().bus().feed_key(c);
}

int SdlApp::run() {
    running_ = true;
    auto last_render = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::microseconds(16667);  // 60.0 Hz

    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running_ = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                     || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                        on_resize(ev.window.data1, ev.window.data2);
                    }
                    break;
                case SDL_KEYDOWN:
                    handle_key(ev.key.keysym);
                    break;
                case SDL_TEXTINPUT:
                    handle_text(ev.text.text);
                    break;
                case SDL_DROPFILE: {
                    // Drag-and-drop a file to load it.
                    try {
                        app().load_file(ev.drop.file);
                    } catch (const std::exception& e) {
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                            "Load failed", e.what(), window_);
                    }
                    SDL_free(ev.drop.file);
                    break;
                }
            }
        }

        // Render at exactly 60 Hz off steady_clock - no VSYNC, no
        // monitor-rate dependency.  sleep_until gives sub-ms precision
        // on Win10+/Linux; the busy-wait check after handles any
        // scheduler over-sleep.
        auto next_render = last_render + frame_interval;
        auto now = std::chrono::steady_clock::now();
        if (now < next_render) {
            std::this_thread::sleep_until(next_render);
        }
        render_frame();
        last_render = std::chrono::steady_clock::now();
    }
    return 0;
}

} // namespace apple1::sdl2

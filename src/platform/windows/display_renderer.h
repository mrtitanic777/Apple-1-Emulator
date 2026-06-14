// display_renderer.h - Direct2D renderer for the 40x24 Apple-1 display.
//
// Renders glyphs using ONLY the 2513 character generator ROM (512 bytes,
// 64 glyphs of 8 rows each, 5x7 active area in 7 cols).  No TrueType
// fallback - the Apple-1 has one font and that font is the 2513.
//
// Pipeline:
//   1. At attach() time, build a bitmap atlas from the char ROM.  The
//      atlas is 64 glyphs laid out horizontally, each glyph rendered at
//      the natural 7px x 8px size into an ID2D1Bitmap.
//   2. On each frame, blit the appropriate atlas subrect into each cell
//      via DrawBitmap with destination scaling.  D2D handles the scaling
//      with nearest-neighbor, so the pixel-art look is preserved at any
//      window size.

#pragma once

#include "common.h"
#include "display_grid.h"
#include "d2d_helpers.h"
#include "settings.h"
#include <windows.h>
#include <d2d1.h>
#include <vector>

namespace apple1::win {

class DisplayRenderer {
public:
    DisplayRenderer() = default;

    // Bind to a window using the given 512-byte 2513 char ROM.  Settings
    // are consulted each frame for phosphor color, scanlines, dot
    // artifact.  Throws if char_rom isn't 512 bytes.
    bool attach(HWND hwnd, const std::vector<u8>& char_rom,
                const Settings* settings);

    void on_resize(int client_w, int client_h);
    void render(const DisplayGrid& grid);

    // Called when a setting changes that affects rendered colors so we
    // know to rebuild the brushes / atlas.
    void invalidate_colors() { colors_dirty_ = true; }

private:
    bool ensure_device_resources();
    void release_device_resources();
    bool ensure_atlas();

    HWND hwnd_ = nullptr;
    int  client_w_ = 0;
    int  client_h_ = 0;

    const Settings* settings_ = nullptr;
    bool colors_dirty_ = true;

    // 2513 ROM: 64 glyphs, each 8 bytes (one byte per row, bits 6-0 used).
    std::vector<u8> char_rom_;

    // The atlas is a 1-bit alpha bitmap, all 64 glyphs side-by-side.  We
    // tint it white when drawing via the brush.  Dimensions are fixed at
    // the natural glyph size; cells are scaled at draw time.
    static constexpr int kGlyphW = 7;        // active columns
    static constexpr int kGlyphH = 8;        // rows including blank top
    static constexpr int kGlyphsPerRow = 64;
    static constexpr int kAtlasW   = kGlyphW * kGlyphsPerRow;  // 448
    static constexpr int kAtlasH   = kGlyphH;                  // 8

    ComPtr<ID2D1Bitmap>            atlas_;
    // Per-cell pixel size in window coordinates.  Calculated on resize so
    // that 40 cells fit horizontally and 24 fit vertically with margin.
    float cell_w_ = 14.0f;
    float cell_h_ = 16.0f;

    ComPtr<ID2D1HwndRenderTarget>  target_;
    ComPtr<ID2D1SolidColorBrush>   brush_text_;
    ComPtr<ID2D1SolidColorBrush>   brush_cursor_;
    ComPtr<ID2D1SolidColorBrush>   brush_dot_;     // faint CRT-artifact dots
};

} // namespace apple1::win

// display_renderer.cpp - 2513 character generator + Direct2D blitting.

#include "display_renderer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace apple1::win {

namespace {

// Apple-1 display code -> glyph index in the 2513.
// The 2513 layout starts at '@' (index 0 = ASCII 0x40), with space at
// index 0x20 (ASCII 0x60 wraps).  Mapping derived from apple1js.
constexpr u8 ascii_to_glyph(u8 code) {
    return static_cast<u8>((code - 0x40) & 0x3F);
}

struct RGB { float r, g, b; };

RGB phosphor_color(Phosphor p) {
    switch (p) {
        case Phosphor::Green: return { 0.30f, 1.00f, 0.40f };
        case Phosphor::Amber: return { 1.00f, 0.74f, 0.20f };
        case Phosphor::White:
        default:              return { 1.00f, 1.00f, 1.00f };
    }
}

} // namespace

bool DisplayRenderer::attach(HWND hwnd, const std::vector<u8>& char_rom,
                             const Settings* settings) {
    if (char_rom.size() != 512) {
        throw std::runtime_error("char ROM must be 512 bytes");
    }
    char_rom_ = char_rom;
    hwnd_ = hwnd;
    settings_ = settings;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    client_w_ = rc.right - rc.left;
    client_h_ = rc.bottom - rc.top;

    on_resize(client_w_, client_h_);
    return ensure_device_resources();
}

void DisplayRenderer::on_resize(int w, int h) {
    client_w_ = w;
    client_h_ = h;
    if (target_) {
        target_->Resize(D2D1::SizeU(static_cast<UINT32>(w),
                                    static_cast<UINT32>(h)));
    }
    // Snap cell size to integer multiples of the natural 7x8 glyph so
    // each lit pixel is square or 1:1 aspect-preserved.  Reserve
    // approximately 3 cells of padding on each side (left/right and
    // top/bottom) so the active raster sits inside a CRT-style black
    // bezel instead of stretching edge-to-edge, and so the bottom-bezel
    // dot artifact has room to display all 3 rows.
    // Parenthesize std::min/std::max to defeat any stray windows.h
    // min/max macros that NOMINMAX should have killed but might not have.
    //
    // We solve for the largest integer scale where:
    //   (kCols + 6) * kGlyphW * scale  <=  w
    //   (kRows + 6) * kGlyphH * scale  <=  h
    int avail_w = (std::max)(w, 1);
    int avail_h = (std::max)(h, 1);
    int scale_w = avail_w / ((kCols + 6) * kGlyphW);
    int scale_h = avail_h / ((kRows + 6) * kGlyphH);
    int scale   = (std::min)(scale_w, scale_h);
    if (scale < 1) scale = 1;
    cell_w_ = static_cast<float>(kGlyphW * scale);
    cell_h_ = static_cast<float>(kGlyphH * scale);
}

bool DisplayRenderer::ensure_device_resources() {
    if (target_) return ensure_atlas();
    if (!hwnd_)  return false;

    HRESULT hr = d2d_factory()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(
            hwnd_,
            D2D1::SizeU(static_cast<UINT32>(client_w_),
                        static_cast<UINT32>(client_h_))),
        target_.addr());
    if (FAILED(hr) || !target_) return false;

    // Nearest-neighbor scaling preserves pixel-art crispness when the
    // atlas is stretched to cell size.
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    RGB col = settings_ ? phosphor_color(settings_->phosphor())
                        : RGB{1.0f, 1.0f, 1.0f};
    target_->CreateSolidColorBrush(
        D2D1::ColorF(col.r, col.g, col.b, 1.0f), brush_text_.addr());
    target_->CreateSolidColorBrush(
        D2D1::ColorF(col.r, col.g, col.b, 1.0f), brush_cursor_.addr());
    // Very faint phosphor-colored dot artifact.
    target_->CreateSolidColorBrush(
        D2D1::ColorF(col.r, col.g, col.b, 0.12f), brush_dot_.addr());
    if (!brush_text_ || !brush_cursor_ || !brush_dot_) return false;

    colors_dirty_ = false;
    return ensure_atlas();
}

bool DisplayRenderer::ensure_atlas() {
    if (atlas_) return true;
    if (!target_) return false;

    RGB col = settings_ ? phosphor_color(settings_->phosphor())
                        : RGB{1.0f, 1.0f, 1.0f};
    // BGRA premultiplied.  Pack the phosphor RGB into the bitmap pixels.
    u32 lit_pixel = 0xFF000000u
                  | (static_cast<u32>(col.b * 255.0f) <<  0)
                  | (static_cast<u32>(col.g * 255.0f) <<  8)
                  | (static_cast<u32>(col.r * 255.0f) << 16);

    std::vector<u32> pixels(kAtlasW * kAtlasH, 0x00000000);

    for (int g = 0; g < kGlyphsPerRow; ++g) {
        const u8* glyph_bytes = char_rom_.data() + g * 8;
        for (int row = 0; row < kGlyphH; ++row) {
            u8 b = glyph_bytes[row];
            for (int col_i = 0; col_i < kGlyphW; ++col_i) {
                bool lit = (b & (1 << (6 - col_i))) != 0;
                int x = g * kGlyphW + col_i;
                int y = row;
                pixels[y * kAtlasW + x] = lit ? lit_pixel : 0u;
            }
        }
    }

    D2D1_SIZE_U sz = D2D1::SizeU(kAtlasW, kAtlasH);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = target_->CreateBitmap(sz, pixels.data(), kAtlasW * 4, props,
                                       atlas_.addr());
    return SUCCEEDED(hr) && atlas_;
}

void DisplayRenderer::release_device_resources() {
    brush_text_.release();
    brush_cursor_.release();
    brush_dot_.release();
    atlas_.release();
    target_.release();
}

void DisplayRenderer::render(const DisplayGrid& grid) {
    if (colors_dirty_) {
        release_device_resources();
        colors_dirty_ = false;
    }
    if (!ensure_device_resources()) return;

    DisplayGrid::Snapshot snap = grid.snapshot();

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));

    // Origin so the grid is centered.  Snap to integer pixels so every
    // glyph rect lands on a pixel boundary - prevents the "some columns
    // are 2px wide, others 3px" effect when client size doesn't divide
    // evenly by the cell size.
    float total_w = cell_w_ * kCols;
    float total_h = cell_h_ * kRows;
    float ox = std::floor((static_cast<float>(client_w_) - total_w) * 0.5f);
    float oy = std::floor((static_cast<float>(client_h_) - total_h) * 0.5f);

    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            u8  code = snap.grid[y][x];
            bool is_cursor = snap.cursor_visible
                          && y == snap.cursor_row
                          && x == snap.cursor_col;

            // In boot mode, ONLY the '@' cells flash; the '_' cells stay
            // lit continuously.  When the blink phase is off, skip @s.
            if (snap.boot_mode && !snap.boot_blink_on && code == 0x40) {
                continue;
            }

            float cx = ox + cell_w_ * x;
            float cy = oy + cell_h_ * y;
            D2D1_RECT_F dst = D2D1::RectF(cx, cy, cx + cell_w_, cy + cell_h_);

            // The cursor is drawn as a '@' glyph (ASCII 0x40), same as the
            // real Apple-1 firmware.  It overrides whatever character is
            // in this cell - the cell content underneath isn't shown
            // while the cursor is on top of it.
            if (is_cursor) {
                code = 0x40;       // '@'
            } else {
                if (code == 0x20) continue;     // blank cell - skip
                if (code < 0x20 || code > 0x5F) continue;
            }

            int glyph = ascii_to_glyph(code);
            float sx = static_cast<float>(glyph * kGlyphW);
            D2D1_RECT_F src = D2D1::RectF(
                sx, 0.0f, sx + kGlyphW, static_cast<float>(kGlyphH));

            // NEAREST_NEIGHBOR scaling gives crisp pixel-art glyphs at
            // any window size.  LINEAR would blur them, which is what
            // you'd want for photos but not for a 7x8 chargen.
            target_->DrawBitmap(
                atlas_.get(), &dst, 1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
        }
    }

    // Special case: cursor parked at column 40 (one past the last cell).
    // Happens for one tick after a line fills up, right before the wrap
    // and CR delay.  The cursor glyph appears just past the rightmost
    // character on the line.
    if (snap.cursor_visible && snap.cursor_col == kCols
        && snap.cursor_row >= 0 && snap.cursor_row < kRows
        && !snap.boot_mode) {
        float cx = ox + cell_w_ * kCols;
        float cy = oy + cell_h_ * snap.cursor_row;
        D2D1_RECT_F dst = D2D1::RectF(cx, cy, cx + cell_w_, cy + cell_h_);
        int glyph = ascii_to_glyph(0x40);
        float sx = static_cast<float>(glyph * kGlyphW);
        D2D1_RECT_F src = D2D1::RectF(
            sx, 0.0f, sx + kGlyphW, static_cast<float>(kGlyphH));
        target_->DrawBitmap(
            atlas_.get(), &dst, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src);
    }

    // CRT/video-circuit dot artifact: faint phosphor-colored dots in a
    // regular grid matching what the real Apple-1 produces.  Active area:
    // a dot in every other character cell.  Bezel below: sparser, every
    // ~10 cells.  Skip during boot mode and when the user has disabled
    // the artifact in Settings.
    bool show_dots = (!settings_ || settings_->dot_artifact()) && !snap.boot_mode;
    if (show_dots && brush_dot_) {
        // Dot size = 1 "scale-pixel" of the glyph atlas (i.e. one logical
        // chargen pixel at whatever the current cell scale is).
        float dot_size = cell_w_ / static_cast<float>(kGlyphW);
        if (dot_size < 1.0f) dot_size = 1.0f;

        // Active area (rows 0..23): one dot per 2 cells horizontally,
        // one per row vertically.  Place dot at the lower-right corner
        // of each chosen cell.
        for (int y = 0; y < kRows; ++y) {
            for (int x = 1; x < kCols; x += 2) {
                float cx = ox + cell_w_ * x + cell_w_ * 0.5f - dot_size * 0.5f;
                float cy = oy + cell_h_ * y + cell_h_ * 0.5f - dot_size * 0.5f;
                D2D1_RECT_F r = D2D1::RectF(
                    cx, cy, cx + dot_size, cy + dot_size);
                target_->FillRectangle(r, brush_dot_.get());
            }
        }

        // Bezel below the active area: dots every ~10 cell-columns, three
        // rows worth, matching the sparse pattern in the real Apple-1
        // CRT photo.
        for (int row_below = 0; row_below < 3; ++row_below) {
            float cy = oy + cell_h_ * kRows + cell_h_ * row_below
                     + cell_h_ * 0.5f - dot_size * 0.5f;
            for (int x = 9; x < kCols; x += 10) {
                float cx = ox + cell_w_ * x + cell_w_ * 0.5f - dot_size * 0.5f;
                D2D1_RECT_F r = D2D1::RectF(
                    cx, cy, cx + dot_size, cy + dot_size);
                target_->FillRectangle(r, brush_dot_.get());
            }
        }
    }

    // Scanlines: horizontal dark stripes overlaid across the active
    // raster area.  One thin stripe per logical-glyph-row at the bottom
    // of each row.  Medium intensity (~50% black opacity).
    if (settings_ && settings_->scanlines() && !snap.boot_mode) {
        float dot_size = cell_w_ / static_cast<float>(kGlyphW);
        if (dot_size < 1.0f) dot_size = 1.0f;

        ComPtr<ID2D1SolidColorBrush> scan_brush;
        target_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.50f), scan_brush.addr());
        if (scan_brush) {
            // One dark stripe per logical pixel row of the active raster.
            // Active raster is kRows * kGlyphH logical rows tall.
            float pix_h = cell_h_ / static_cast<float>(kGlyphH);
            for (int py = 0; py < kRows * kGlyphH; ++py) {
                // Every other logical row gets a dim stripe.
                if ((py & 1) == 0) continue;
                float top = oy + py * pix_h;
                D2D1_RECT_F r = D2D1::RectF(
                    ox, top, ox + cell_w_ * kCols, top + pix_h);
                target_->FillRectangle(r, scan_brush.get());
            }
        }
    }

    // Vignette: radial gradient darkening from transparent at center to
    // ~75% black at the corners.  Subtle but adds a real CRT-tube feel.
    // Drawn last so it tints everything underneath (text, dots, scans).
    if (settings_ && settings_->vignette()) {
        float cw = static_cast<float>(client_w_);
        float ch = static_cast<float>(client_h_);
        float cx = cw * 0.5f;
        float cy = ch * 0.5f;
        // Radius: use the diagonal so the ellipse covers all four corners.
        float rx = cw * 0.55f;
        float ry = ch * 0.55f;

        // Gradient stops: 0.0 transparent, 0.65 still mostly transparent,
        // 1.0 dark.  Soft falloff so it doesn't look like a black ring.
        D2D1_GRADIENT_STOP stops[3];
        stops[0].position = 0.0f;
        stops[0].color    = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
        stops[1].position = 0.65f;
        stops[1].color    = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f);
        stops[2].position = 1.0f;
        stops[2].color    = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f);

        ComPtr<ID2D1GradientStopCollection> stop_coll;
        if (SUCCEEDED(target_->CreateGradientStopCollection(
                stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                stop_coll.addr())) && stop_coll) {
            ComPtr<ID2D1RadialGradientBrush> vbrush;
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props =
                D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(cx, cy),
                    D2D1::Point2F(0.0f, 0.0f),
                    rx, ry);
            if (SUCCEEDED(target_->CreateRadialGradientBrush(
                    props, stop_coll.get(), vbrush.addr())) && vbrush) {
                target_->FillRectangle(
                    D2D1::RectF(0.0f, 0.0f, cw, ch), vbrush.get());
            }
        }
    }

    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        release_device_resources();
    }
}

} // namespace apple1::win

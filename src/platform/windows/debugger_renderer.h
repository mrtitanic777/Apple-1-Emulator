// debugger_renderer.h - Direct2D renderer for the debugger floating window.
//
// Layout sections (top to bottom):
//   1. Status banner    ("PAUSED" / "RUNNING")
//   2. Registers        (PC, A, X, Y, SP, flags, cycles)
//   3. Disassembly      (8 lines from PC, with breakpoint markers)
//   4. Memory inspector (4 rows of 8 bytes from the goto address)
//   5. Tape diagnostics (transitions consumed, flips, reads)
//   6. Breakpoints list
//   7. Help footer

#pragma once

#include "d2d_helpers.h"
#include "cpu6502.h"
#include "bus.h"
#include "debugger.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

namespace apple1::win {

class DebuggerRenderer {
public:
    bool attach(HWND hwnd);
    void on_resize(int w, int h);
    void render(const CPU6502& cpu, const Bus& bus, const Debugger& dbg);

private:
    bool ensure_device_resources();
    void release_device_resources();
    void draw_text(const wchar_t* s, float x, float y);

    HWND hwnd_ = nullptr;
    int  client_w_ = 0;
    int  client_h_ = 0;

    ComPtr<IDWriteTextFormat>      text_format_;
    ComPtr<IDWriteTextFormat>      bold_format_;
    ComPtr<ID2D1HwndRenderTarget>  target_;
    ComPtr<ID2D1SolidColorBrush>   brush_text_;
    ComPtr<ID2D1SolidColorBrush>   brush_dim_;
    ComPtr<ID2D1SolidColorBrush>   brush_accent_;

    float line_h_ = 18.0f;
};

} // namespace apple1::win

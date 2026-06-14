// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// debugger_renderer.h - Direct2D renderer for the debugger floating window.
//
// Layout sections (top to bottom):
//   1. Status banner    ("PAUSED" / "RUNNING")
//   2. Registers        (PC, A, X, Y, SP, flags, cycles)
//   3. Disassembly      (8 lines from PC, with breakpoint markers)
//   4. Memory inspector (4 rows of 8 bytes from the goto address)
//   5. Tape diagnostics (transitions consumed, flips, reads)
//   6. Disk diagnostics (mount, motor, track, mode, data register)
//   7. Breakpoints list
//   8. Help footer

#pragma once

#include "d2d_helpers.h"
#include "cpu6502.h"
#include "bus.h"
#include "debugger.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>

namespace apple1::win {

// Clickable buttons in the debugger window.  The renderer lays them out
// each frame and remembers their rects so the window can hit-test mouse
// clicks back to an action.
enum class DebugButton : int {
    None           = 0,
    ModeFree       = 1,
    ModeStep       = 2,
    ModeRunRTS     = 3,
    StepNow        = 4,
    AddBreakpoint  = 5,    // opens the Breakpoints modal
};

class DebuggerRenderer {
public:
    bool attach(HWND hwnd);
    void on_resize(int w, int h);
    void render(const CPU6502& cpu, const Bus& bus, const Debugger& dbg);

    // Mouse hit test: returns the button under (x, y) from the most
    // recent render() call, or DebugButton::None if no button is there.
    DebugButton hit_test(int x, int y) const;

private:
    bool ensure_device_resources();
    void release_device_resources();
    void draw_text(const wchar_t* s, float x, float y);

    // Lay out one button at (x, y) of the given width, draw it with the
    // appropriate brush (accent if "active"), and remember its rect for
    // later hit-testing.  Returns x + width + gap so the caller can chain
    // buttons in a row.
    float draw_button(DebugButton id, const wchar_t* label,
                      float x, float y, float w, bool active);

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

    // Hit regions captured each render frame.
    struct ButtonRect { DebugButton id; D2D1_RECT_F rect; };
    std::vector<ButtonRect> buttons_;
};

} // namespace apple1::win

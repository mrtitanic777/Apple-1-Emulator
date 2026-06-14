// debugger_window.h - the floating debugger window.  Created on first
// "Show Debugger" command; when closed via the X it hides rather than
// destroys (so re-opening is instant).

#pragma once

#include "debugger_renderer.h"
#include <windows.h>

namespace apple1::win {

class DebuggerWindow {
public:
    bool create(HINSTANCE hInstance, HWND parent);
    void show();
    void hide();
    bool is_visible() const { return visible_; }
    HWND hwnd() const { return hwnd_; }

    // Posted from the main window's render timer.
    void redraw();

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    bool visible_ = false;
    DebuggerRenderer renderer_;
};

} // namespace apple1::win

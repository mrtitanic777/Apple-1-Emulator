// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// debugger_window.cpp

#include "debugger_window.h"
#include "breakpoints_dialog.h"
#include "app.h"
#include "resource.h"
#include <windowsx.h>           // GET_X_LPARAM / GET_Y_LPARAM

namespace apple1::win {

namespace {
const wchar_t* kClassName = L"Apple1DebuggerWindow";
}

bool DebuggerWindow::create(HINSTANCE hInstance, HWND parent) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &DebuggerWindow::proc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;        // D2D handles painting
    wc.lpszClassName = kClassName;
    wc.hIcon   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"Apple-1 Debugger",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 760,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    renderer_.attach(hwnd_);
    return true;
}

void DebuggerWindow::show() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOW);
    visible_ = true;
}

void DebuggerWindow::hide() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void DebuggerWindow::redraw() {
    if (!visible_ || !hwnd_) return;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK DebuggerWindow::proc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam) {
    DebuggerWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<DebuggerWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<DebuggerWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT DebuggerWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            renderer_.on_resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            renderer_.render(app().cpu(), app().bus(), app().debugger());
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            switch (renderer_.hit_test(x, y)) {
                case DebugButton::ModeFree:
                    app().debugger().request_run_free();
                    break;
                case DebugButton::ModeStep:
                    app().debugger().request_pause();
                    break;
                case DebugButton::ModeRunRTS:
                    app().debugger().request_run_to_rts();
                    break;
                case DebugButton::StepNow:
                    app().debugger().request_step();
                    break;
                case DebugButton::AddBreakpoint:
                    BreakpointsDialog::run(hwnd_, app().debugger());
                    break;
                case DebugButton::None:
                default:
                    return 0;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;       // D2D handles background
        case WM_CLOSE:
            hide();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

} // namespace apple1::win

// Author: Phillip Allison (github.com/philtimmes)
// Written for the Apple-1 Emulator. See CONTRIBUTORS.md for details.

// breakpoints_dialog.cpp - see header.
//
// Built programmatically (no resource template) so changes to layout
// don't require a .rc / .h rebuild dance.  Standard Win32 controls;
// modal via EnableWindow(owner, FALSE) + GetMessage pump until the
// dialog window is destroyed.

#include "breakpoints_dialog.h"

#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace apple1::win {

namespace {

const wchar_t* kClassName = L"Apple1BreakpointsDialog";

// Control IDs.  Local to this dialog's HWND.
constexpr int IDC_LIST   = 1001;
constexpr int IDC_EDIT   = 1002;
constexpr int IDC_ADD    = 1003;
constexpr int IDC_TOGGLE = 1004;   // Disable when selected-BP enabled, vice versa
constexpr int IDC_DELETE = 1005;
constexpr int IDC_CLOSE  = 1006;

// Layout (client area).
constexpr int kPad       = 10;
constexpr int kBtnH      = 26;
constexpr int kBtnW      = 96;
constexpr int kEditH     = 22;
constexpr int kClientW   = 360;
constexpr int kClientH   = 360;

// Parse a hex string ("$1A00", "1A00", "0x1A00") to u16.  Returns
// true on success; *out gets the value.  Skips leading whitespace
// and an optional "$" or "0x"/"0X" prefix.  Up to 4 hex digits.
bool parse_hex_u16(const wchar_t* s, u16* out) {
    while (*s && iswspace(*s)) ++s;
    if (*s == L'$') ++s;
    else if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) s += 2;
    if (!*s) return false;
    u32 v = 0;
    int digits = 0;
    while (*s && !iswspace(*s)) {
        wchar_t c = *s++;
        u32 d;
        if      (c >= L'0' && c <= L'9') d = c - L'0';
        else if (c >= L'a' && c <= L'f') d = 10 + (c - L'a');
        else if (c >= L'A' && c <= L'F') d = 10 + (c - L'A');
        else return false;
        v = (v << 4) | d;
        if (++digits > 4) return false;
    }
    if (digits == 0) return false;
    *out = static_cast<u16>(v);
    return true;
}

HFONT make_ui_font() {
    LOGFONTW lf{};
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

HFONT make_mono_font() {
    LOGFONTW lf{};
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Cascadia Mono");
    HFONT f = CreateFontIndirectW(&lf);
    if (f) return f;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    return CreateFontIndirectW(&lf);
}

} // namespace

void BreakpointsDialog::run(HWND owner, Debugger& dbg) {
    HINSTANCE hi = GetModuleHandleW(nullptr);

    // Register the window class lazily (idempotent across calls).
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = &BreakpointsDialog::proc;
        wc.hInstance     = hi;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    // Compute centered placement over the owner window.
    RECT or_rect{};
    if (owner) GetWindowRect(owner, &or_rect);
    int total_w = kClientW + 16;       // approximate border
    int total_h = kClientH + 39;       // approximate caption + border
    int x = (or_rect.left + or_rect.right - total_w) / 2;
    int y = (or_rect.top  + or_rect.bottom - total_h) / 2;
    if (!owner) { x = CW_USEDEFAULT; y = CW_USEDEFAULT; }

    BreakpointsDialog dlg(dbg);
    HWND h = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, L"Breakpoints",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, total_w, total_h,
        owner, nullptr, hi, &dlg);
    if (!h) return;

    // Modal loop: disable the owner while the dialog is up so it
    // really behaves like a modal.
    if (owner) EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

LRESULT CALLBACK BreakpointsDialog::proc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    BreakpointsDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<BreakpointsDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<BreakpointsDialog*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT BreakpointsDialog::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hi = GetModuleHandleW(nullptr);
            HFONT ui_font   = make_ui_font();
            HFONT mono_font = make_mono_font();

            // Top row: "Address (hex):" label + edit + Add button.
            HWND lbl = CreateWindowExW(0, L"STATIC",
                L"Address (hex):",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kPad, kPad + 4, 110, 18,
                hwnd_, nullptr, hi, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)ui_font, TRUE);

            edt_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_LEFT | ES_AUTOHSCROLL,
                kPad + 110, kPad, 110, kEditH,
                hwnd_, (HMENU)(INT_PTR)IDC_EDIT, hi, nullptr);
            SendMessageW(edt_, WM_SETFONT, (WPARAM)mono_font, TRUE);
            SendMessageW(edt_, EM_SETLIMITTEXT, 6, 0);

            btn_add_ = CreateWindowExW(0, L"BUTTON", L"Add",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                kPad + 230, kPad - 2, 100, kBtnH,
                hwnd_, (HMENU)(INT_PTR)IDC_ADD, hi, nullptr);
            SendMessageW(btn_add_, WM_SETFONT, (WPARAM)ui_font, TRUE);

            // Middle: listbox of existing breakpoints.
            const int list_y = kPad + kBtnH + 12;
            const int list_h = kClientH - list_y - kBtnH - kPad - 10;
            lst_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                    LBS_NOTIFY | LBS_HASSTRINGS | LBS_USETABSTOPS,
                kPad, list_y, kClientW - 2*kPad, list_h,
                hwnd_, (HMENU)(INT_PTR)IDC_LIST, hi, nullptr);
            SendMessageW(lst_, WM_SETFONT, (WPARAM)mono_font, TRUE);

            // Bottom row: Disable/Enable, Delete, Close.
            const int btn_y = kClientH - kBtnH - kPad;
            int bx = kPad;
            btn_tog_ = CreateWindowExW(0, L"BUTTON", L"Disable",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                bx, btn_y, kBtnW, kBtnH,
                hwnd_, (HMENU)(INT_PTR)IDC_TOGGLE, hi, nullptr);
            bx += kBtnW + 8;
            btn_del_ = CreateWindowExW(0, L"BUTTON", L"Delete",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                bx, btn_y, kBtnW, kBtnH,
                hwnd_, (HMENU)(INT_PTR)IDC_DELETE, hi, nullptr);
            // Close hugs the right edge.
            btn_close_ = CreateWindowExW(0, L"BUTTON", L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                kClientW - kPad - kBtnW, btn_y, kBtnW, kBtnH,
                hwnd_, (HMENU)(INT_PTR)IDC_CLOSE, hi, nullptr);
            SendMessageW(btn_tog_,   WM_SETFONT, (WPARAM)ui_font, TRUE);
            SendMessageW(btn_del_,   WM_SETFONT, (WPARAM)ui_font, TRUE);
            SendMessageW(btn_close_, WM_SETFONT, (WPARAM)ui_font, TRUE);

            refresh_listbox();
            sync_button_states();
            SetFocus(edt_);
            return 0;
        }
        case WM_COMMAND: {
            const int id   = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_ADD)    { on_add();           return 0; }
            if (id == IDC_DELETE) { on_delete();        return 0; }
            if (id == IDC_TOGGLE) { on_toggle_enable(); return 0; }
            if (id == IDC_CLOSE)  { DestroyWindow(hwnd_); return 0; }
            if (id == IDC_LIST && code == LBN_SELCHANGE) {
                sync_button_states();
                return 0;
            }
            if (id == IDC_LIST && code == LBN_DBLCLK) {
                // Double-click toggles enabled state (handy shortcut).
                on_toggle_enable();
                return 0;
            }
            // Enter in the edit field = Add.
            if (id == IDC_EDIT && code == EN_CHANGE) {
                // Could enable/disable Add live; cheap enough to skip.
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            // Owner-modal loop checks IsWindow; nothing else to do.
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void BreakpointsDialog::refresh_listbox() {
    // Preserve selection (by address) across refresh.
    int sel_idx = (int)SendMessageW(lst_, LB_GETCURSEL, 0, 0);
    u16 sel_addr = 0xFFFF;
    bool had_sel = false;
    if (sel_idx >= 0) {
        LRESULT data = SendMessageW(lst_, LB_GETITEMDATA, sel_idx, 0);
        if (data != LB_ERR) { sel_addr = (u16)data; had_sel = true; }
    }

    SendMessageW(lst_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lst_, LB_RESETCONTENT, 0, 0);

    auto bps = dbg_.breakpoints_snapshot();
    int restore_idx = -1;
    for (size_t i = 0; i < bps.size(); ++i) {
        wchar_t buf[64];
        std::swprintf(buf, 64, L"$%04X\t%s",
                      bps[i].addr,
                      bps[i].enabled ? L"enabled" : L"DISABLED");
        int idx = (int)SendMessageW(lst_, LB_ADDSTRING, 0, (LPARAM)buf);
        if (idx != LB_ERR) {
            SendMessageW(lst_, LB_SETITEMDATA, idx, (LPARAM)bps[i].addr);
            if (had_sel && bps[i].addr == sel_addr) restore_idx = idx;
        }
    }
    // Tab stop ~5 chars wide so the second column lines up.
    const DWORD tabs[1] = { 40 };
    SendMessageW(lst_, LB_SETTABSTOPS, 1, (LPARAM)tabs);
    if (restore_idx >= 0) SendMessageW(lst_, LB_SETCURSEL, restore_idx, 0);

    SendMessageW(lst_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lst_, nullptr, TRUE);
}

void BreakpointsDialog::on_add() {
    wchar_t txt[32]{};
    GetWindowTextW(edt_, txt, 32);
    u16 addr;
    if (!parse_hex_u16(txt, &addr)) {
        MessageBoxW(hwnd_,
            L"Enter a hex address (1-4 digits, e.g. 1A00 or $C100).",
            L"Add Breakpoint", MB_OK | MB_ICONWARNING);
        SetFocus(edt_);
        SendMessageW(edt_, EM_SETSEL, 0, -1);
        return;
    }
    dbg_.add_breakpoint(addr);
    SetWindowTextW(edt_, L"");
    refresh_listbox();
    // Select the just-added entry so subsequent Enable/Delete acts on it.
    int n = (int)SendMessageW(lst_, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        if ((u16)SendMessageW(lst_, LB_GETITEMDATA, i, 0) == addr) {
            SendMessageW(lst_, LB_SETCURSEL, i, 0);
            break;
        }
    }
    sync_button_states();
    SetFocus(edt_);
}

void BreakpointsDialog::on_delete() {
    int sel = (int)SendMessageW(lst_, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    u16 addr = (u16)SendMessageW(lst_, LB_GETITEMDATA, sel, 0);
    dbg_.remove_breakpoint(addr);
    refresh_listbox();
    sync_button_states();
}

void BreakpointsDialog::on_toggle_enable() {
    int sel = (int)SendMessageW(lst_, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    u16 addr = (u16)SendMessageW(lst_, LB_GETITEMDATA, sel, 0);
    bool now_enabled = dbg_.breakpoint_enabled(addr);
    dbg_.set_breakpoint_enabled(addr, !now_enabled);
    refresh_listbox();
    sync_button_states();
}

void BreakpointsDialog::sync_button_states() {
    int sel = (int)SendMessageW(lst_, LB_GETCURSEL, 0, 0);
    BOOL have_sel = (sel >= 0);
    EnableWindow(btn_tog_, have_sel);
    EnableWindow(btn_del_, have_sel);
    if (have_sel) {
        u16 addr = (u16)SendMessageW(lst_, LB_GETITEMDATA, sel, 0);
        bool enabled = dbg_.breakpoint_enabled(addr);
        SetWindowTextW(btn_tog_, enabled ? L"Disable" : L"Enable");
    } else {
        SetWindowTextW(btn_tog_, L"Disable");
    }
}

} // namespace apple1::win

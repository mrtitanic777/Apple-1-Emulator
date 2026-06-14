// Author: Phillip Allison (github.com/philtimmes)
// Written for the Apple-1 Emulator. See CONTRIBUTORS.md for details.

// breakpoints_dialog.h - modal window listing all breakpoints with
// Add (top), Listbox (middle), and Disable/Enable/Delete/Close buttons
// (bottom).  Launched from the debugger window's "ADD BP..." button.

#pragma once

#include "debugger.h"
#include <windows.h>

namespace apple1::win {

class BreakpointsDialog {
public:
    // Pops up the modal, blocking until the user closes it.  Returns
    // when the modal is dismissed - all BP changes have already been
    // committed to dbg.
    static void run(HWND owner, Debugger& dbg);

private:
    explicit BreakpointsDialog(Debugger& dbg) : dbg_(dbg) {}

    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);

    void refresh_listbox();
    void on_add();
    void on_delete();
    void on_toggle_enable();
    void sync_button_states();        // grey out buttons when no selection

    Debugger& dbg_;
    HWND hwnd_      = nullptr;
    HWND lst_       = nullptr;        // listbox of breakpoints
    HWND edt_       = nullptr;        // hex address input
    HWND btn_add_   = nullptr;
    HWND btn_tog_   = nullptr;        // toggle disable/enable (label flips)
    HWND btn_del_   = nullptr;
    HWND btn_close_ = nullptr;
};

} // namespace apple1::win

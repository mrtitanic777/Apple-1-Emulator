// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// main_window.h - the main display window with menu bar, Apple-1 display
// rendering, and keyboard input.

#pragma once

#include "display_renderer.h"
#include "debugger_window.h"
#include <windows.h>
#include <memory>

namespace apple1::win {

class MainWindow {
public:
    bool create(HINSTANCE hInstance, int nCmdShow);
    int  run_message_loop();

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);

    // Menu / accelerator command dispatch.
    void on_command(WORD id);

    // Specific command handlers.
    void cmd_file_open();
    void cmd_cassette_save();
    void cmd_disk_mount();
    void cmd_disk_eject();
    void cmd_debugger_goto_memory();
    void cmd_about();
    void cmd_set_scale(int factor);
    void cmd_prompt_for_tape();   // auto-prompt fired from WM_TIMER
    void cmd_prompt_for_disk();   // auto-prompt fired from WM_TIMER
    void sync_settings_menu();    // mark/unmark menu items per Settings
    void sync_expansions_menu();  // mark/unmark RAM + IO card radio items
    void cmd_set_ram_expansion(RamExpansion r);
    void cmd_set_io_card(IoCard c);

    // WM_CHAR / WM_KEYDOWN -> bus.feed_key.
    void on_char(wchar_t ch);

    HWND        hwnd_     = nullptr;
    HINSTANCE   hInstance_ = nullptr;
    HACCEL      accel_    = nullptr;
    UINT_PTR    timer_id_ = 0;
    bool        prompting_for_tape_ = false;
    bool        prompting_for_disk_ = false;
    DisplayRenderer renderer_;

    std::unique_ptr<DebuggerWindow> debugger_window_;
};

} // namespace apple1::win

// display_grid.h - 40x24 character grid shared between the CPU emulation
// thread (writer) and the Direct2D rendering thread (reader).
//
// This replaces the old terminal-based Display class.  The semantics are
// the same: characters land in a grid, cursor advances, scroll on overflow,
// teletype pacing on each write.  The difference is purely how it gets
// drawn (Direct2D vs ANSI) - all the screen-state logic is here.

#pragma once

#include "common.h"
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

namespace apple1 {

class DisplayGrid {
public:
    DisplayGrid();

    // Receive a character from the PIA write at $D012.  Called from the
    // CPU thread.  Sleeps briefly for teletype pacing.
    void putc(u8 ch);

    // Clear the framebuffer and home the cursor (used by F1 RESET).
    void clear();

    // Power-on garbage screen (the @ / _ checkerboard).
    void fill_garbage();

    // Direct cell write - bypass the cursor.  Used by the boot animation.
    void poke_cell(int row, int col, u8 code);

    // Snapshot the grid into the caller's buffer for rendering.  Returns
    // the cursor (row, col) and whether it should be drawn this frame.
    // Held lock is released before return.
    struct Snapshot {
        std::array<std::array<u8, kCols>, kRows> grid;
        int  cursor_row;
        int  cursor_col;
        bool cursor_visible;
        bool boot_mode;        // true: whole grid blinks (power-on garbage)
        bool boot_blink_on;    // current phase of the boot blink
    };
    Snapshot snapshot() const;

    // Master cursor on/off (off during boot animation).
    void set_cursor_on(bool on);

    // Teletype pacing toggle.  When false, putc() doesn't sleep -
    // instantaneous printing.
    void set_pacing(bool on) { pacing_on_.store(on); }

    // Consume one video-frame's worth of display-busy time without
    // touching the framebuffer.  Used by the bus when the CPU writes a
    // bit-7-clear value to $D012 (the real Apple-1 display chip still
    // takes a frame to acknowledge such writes, which programs like
    // Apple-30th use as a software delay).
    void wait_one_frame();

    // Boot mode: the entire screen is treated as flashing video-RAM
    // garbage.  Disabled by clear() and by explicit set_boot_mode(false).
    void set_boot_mode(bool on);
    bool boot_mode() const;

private:
    void newline_locked();
    void scroll_locked();
    void update_blink_locked();

    mutable std::mutex mutex_;

    std::array<std::array<u8, kCols>, kRows> grid_{};
    int  cursor_row_ = 0;
    int  cursor_col_ = 0;
    bool cursor_on_       = true;
    bool cursor_blink_on_ = true;
    bool boot_mode_       = false;
    u64  last_blink_ms_   = 0;
    std::atomic<bool> pacing_on_{true};

    // Absolute wall-clock deadline for the next paced char.  Lets pacing
    // stay smooth even with millisecond-scale OS scheduler jitter.
    std::chrono::steady_clock::time_point next_char_deadline_{};
};

} // namespace apple1

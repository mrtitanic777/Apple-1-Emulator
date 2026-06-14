// display_grid.cpp

#include "display_grid.h"
#include <chrono>
#include <thread>

namespace apple1 {

namespace {

// Teletype pacing.  The real Apple-1 display can accept one character per
// video frame because its firmware waits for the on-screen scan to clear
// the previous glyph before writing the next.  At 60 Hz that's 16.67 ms
// per char, ~60 chars/sec.  apple1js uses the same 17 ms value.
constexpr auto kCharDelay  = std::chrono::microseconds(16667);

// Carriage return takes noticeably longer on real hardware: the
// horizontal-blank counter has to wrap most of the way around the screen
// before the firmware can position the cursor at column 0 of the new
// line.  Observed on a real Apple-1: ~100-130 ms of "cursor alone" after
// the wrap before the next character lands.  We add ~100 ms on top of
// the normal per-char delay.
constexpr auto kCRExtraDelay = std::chrono::milliseconds(100);

constexpr u64 kBlinkPeriodMs = 400;

u64 now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

} // namespace

DisplayGrid::DisplayGrid() {
    for (auto& row : grid_) row.fill(0x20);
    last_blink_ms_ = now_ms();
}

void DisplayGrid::scroll_locked() {
    for (int y = 0; y < kRows - 1; ++y) grid_[y] = grid_[y + 1];
    grid_[kRows - 1].fill(0x20);
    cursor_row_ = kRows - 1;
}

void DisplayGrid::newline_locked() {
    cursor_col_ = 0;
    if (++cursor_row_ >= kRows) scroll_locked();
}

void DisplayGrid::putc(u8 ch) {
    bool wrapped = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        ch &= 0x7F;
        if (ch == 0x0D) {
            newline_locked();
            wrapped = true;
        } else if (ch == 0x08 || ch == 0x5F) {            // backspace
            if (cursor_col_ > 0) {
                --cursor_col_;
                grid_[cursor_row_][cursor_col_] = 0x20;
            }
        } else if (ch >= 0x20 && ch <= 0x7F) {
            // The 2513 character generator doesn't have lowercase glyphs.
            // When the display sees codes 0x60-0x7F it wraps them down to
            // 0x40-0x5F (same upper-case glyphs as 0x40-0x5F).  Mask off
            // bit 5 to do that fold.
            if (ch >= 0x60) ch &= 0x5F;
            // If the previous write left the cursor parked at column 40
            // (one past the last cell), we did NOT wrap then.  This
            // write is the one that triggers the wrap, but only AFTER
            // we let the cursor sit visibly at column 40 for the CR
            // delay below.
            if (cursor_col_ >= kCols) {
                newline_locked();
                wrapped = true;
            }
            grid_[cursor_row_][cursor_col_] = ch;
            ++cursor_col_;
            // Do NOT wrap here even if cursor_col_ == kCols.  We leave
            // the cursor parked at column 40 so the renderer draws the
            // '@' just past the last char.  The wrap happens on the
            // NEXT write (above).
        } else {
            return;
        }
    }
    // Teletype pacing.  Use a running absolute deadline rather than
    // "sleep 16.67ms from now" so sleep-granularity errors don't drift
    // and accumulate.  Skip entirely if pacing is disabled.
    if (!pacing_on_.load()) return;

    using namespace std::chrono;
    auto delay = kCharDelay + (wrapped ? kCRExtraDelay : nanoseconds(0));

    // Advance the deadline.  If we're way behind (a long pause happened),
    // resync to now so the next char doesn't burst-fire to catch up.
    auto now = steady_clock::now();
    if (next_char_deadline_.time_since_epoch().count() == 0
        || now > next_char_deadline_ + milliseconds(100)) {
        next_char_deadline_ = now;
    }
    next_char_deadline_ += delay;

    // Sleep until ~500us before the deadline, then busy-wait the tail to
    // absorb timer-granularity jitter.  Result: ~sub-millisecond accuracy
    // per char regardless of OS scheduling noise.
    auto coarse = next_char_deadline_ - microseconds(500);
    if (coarse > steady_clock::now()) {
        std::this_thread::sleep_until(coarse);
    }
    while (steady_clock::now() < next_char_deadline_) {
        // tight spin - last 500us at most
    }
}

void DisplayGrid::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& row : grid_) row.fill(0x20);
    cursor_row_ = 0;
    cursor_col_ = 0;
    boot_mode_ = false;       // CLEAR SCREEN exits the power-on garbage state
}

void DisplayGrid::fill_garbage() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            // Apple-1 uninitialized video RAM settles into vertical
            // stripes: even columns hold one character, odd columns hold
            // the other.  No row offset - whole columns are uniform.
            grid_[y][x] = (x % 2 == 0) ? 0x5F : 0x40;
        }
    }
}

void DisplayGrid::poke_cell(int row, int col, u8 code) {
    if (row < 0 || row >= kRows || col < 0 || col >= kCols) return;
    std::lock_guard<std::mutex> lk(mutex_);
    grid_[row][col] = code;
}

void DisplayGrid::set_cursor_on(bool on) {
    std::lock_guard<std::mutex> lk(mutex_);
    cursor_on_ = on;
    // Re-prime the blink phase so it starts visible.
    cursor_blink_on_ = true;
    last_blink_ms_ = now_ms();
}

void DisplayGrid::wait_one_frame() {
    if (!pacing_on_.load()) return;
    using namespace std::chrono;
    auto now = steady_clock::now();
    if (next_char_deadline_.time_since_epoch().count() == 0
        || now > next_char_deadline_ + milliseconds(100)) {
        next_char_deadline_ = now;
    }
    next_char_deadline_ += kCharDelay;
    auto coarse = next_char_deadline_ - microseconds(500);
    if (coarse > steady_clock::now()) {
        std::this_thread::sleep_until(coarse);
    }
    while (steady_clock::now() < next_char_deadline_) {}
}

void DisplayGrid::set_boot_mode(bool on) {
    std::lock_guard<std::mutex> lk(mutex_);
    boot_mode_ = on;
}

bool DisplayGrid::boot_mode() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return boot_mode_;
}

void DisplayGrid::update_blink_locked() {
    u64 t = now_ms();
    if (t - last_blink_ms_ > kBlinkPeriodMs) {
        cursor_blink_on_ = !cursor_blink_on_;
        last_blink_ms_ = t;
    }
}

DisplayGrid::Snapshot DisplayGrid::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    const_cast<DisplayGrid*>(this)->update_blink_locked();
    Snapshot s;
    s.grid = grid_;
    s.cursor_row = cursor_row_;
    s.cursor_col = cursor_col_;
    s.cursor_visible = cursor_on_ && cursor_blink_on_;
    s.boot_mode = boot_mode_;
    s.boot_blink_on = cursor_blink_on_;     // reuse the same phase
    return s;
}

} // namespace apple1

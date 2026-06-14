// debugger.h - debugger state shared between the CPU thread and the UI
// thread.  Holds: paused flag, breakpoints, single-step latch.
//
// The CPU thread checks should_pause_before(pc) at every step.  If true,
// it sleeps until resumed.  The debugger UI manipulates the state from a
// separate thread; the few mutating operations take the mutex.

#pragma once

#include "common.h"
#include <atomic>
#include <mutex>
#include <set>

namespace apple1 {

class Debugger {
public:
    // CPU thread calls this BEFORE executing each instruction.  Returns
    // true if execution should stop here (so the main loop will sleep).
    bool should_pause_before(u16 pc);

    // Toggle pause state.  Used by F5 from the keyboard thread.
    void toggle_pause();
    bool is_paused() const { return paused_.load(); }
    void resume();

    // Single-step: do exactly one instruction, then re-pause.
    void request_step();

    // Step-over: a step-over is just "set a one-shot breakpoint at the
    // address right after the current instruction and resume".  The CPU
    // thread will hit that breakpoint and pause again.  The caller passes
    // the post-instruction address (PC + instruction length).
    void request_step_over(u16 return_addr);

    // Breakpoint management.
    void toggle_breakpoint(u16 addr);
    bool has_breakpoint(u16 addr) const;
    std::set<u16> breakpoints() const;       // snapshot for UI

    // The memory inspector remembers the user's last "goto" address.
    u16  memory_view_addr() const { return memory_view_addr_.load(); }
    void set_memory_view_addr(u16 a) { memory_view_addr_.store(a); }

private:
    std::atomic<bool> paused_{false};
    std::atomic<bool> step_pending_{false};
    std::atomic<u16>  one_shot_bp_{0xFFFF};    // 0xFFFF means "none"
    std::atomic<bool> one_shot_active_{false};

    mutable std::mutex bp_mutex_;
    std::set<u16>     breakpoints_;

    std::atomic<u16>  memory_view_addr_{0x0000};
};

} // namespace apple1

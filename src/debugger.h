// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// debugger.h - debugger state shared between the CPU thread and the UI
// thread.  Holds: paused flag, breakpoints, single-step latch.
//
// The CPU thread checks should_pause_before(pc) at every step.  If true,
// it sleeps until resumed.  The debugger UI manipulates the state from a
// separate thread; the few mutating operations take the mutex.

#pragma once

#include "common.h"
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace apple1 {

// What the CPU thread is doing right now, at the user's request.
//   Free      - run continuously (breakpoints still apply)
//   Step      - paused; user advances one instruction at a time
//   RunToRTS  - run continuously, then pause AFTER the next RTS executes
enum class RunMode : int {
    Free     = 0,
    Step     = 1,
    RunToRTS = 2,
};

class Debugger {
public:
    // CPU thread calls this BEFORE executing each instruction.  Returns
    // true if execution should stop here (so the main loop will sleep).
    // The opcode is passed in so the debugger can detect RTS for the
    // "run to RTS" mode without needing a Bus pointer.
    bool should_pause_before(u16 pc, u8 opcode);

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

    // Run-mode controls.  These are mutually exclusive; setting one
    // automatically clears the other modes' state.  The current mode is
    // derived from the underlying flags via current_mode().
    void    request_run_free();        // resume, no auto-pause
    void    request_run_to_rts();      // resume, pause after next RTS
    void    request_pause();           // force pause (-> Step mode)
    RunMode current_mode() const;

    // Breakpoint management.  Each breakpoint has an enabled flag; a
    // disabled breakpoint stays in the list but doesn't pause the CPU.
    void toggle_breakpoint(u16 addr);                    // F8 / disasm-line click
    void add_breakpoint(u16 addr);                       // no-op if already present
    void remove_breakpoint(u16 addr);                    // no-op if absent
    void set_breakpoint_enabled(u16 addr, bool enabled); // no-op if absent
    void clear_all_breakpoints();
    bool has_breakpoint(u16 addr) const;                 // exists AND enabled
    bool breakpoint_enabled(u16 addr) const;             // exists AND enabled
    // Snapshot for the UI: address ascending; second = enabled flag.
    struct Breakpoint { u16 addr; bool enabled; };
    std::vector<Breakpoint> breakpoints_snapshot() const;

    // The memory inspector remembers the user's last "goto" address.
    u16  memory_view_addr() const { return memory_view_addr_.load(); }
    void set_memory_view_addr(u16 a) { memory_view_addr_.store(a); }

private:
    std::atomic<bool> paused_{false};
    std::atomic<bool> step_pending_{false};
    std::atomic<bool> pause_next_{false};      // set by RunToRTS after the RTS executes
    std::atomic<bool> run_to_rts_{false};      // armed; cleared on first RTS
    std::atomic<u16>  one_shot_bp_{0xFFFF};    // 0xFFFF means "none"
    std::atomic<bool> one_shot_active_{false};

    mutable std::mutex bp_mutex_;
    std::map<u16, bool> breakpoints_;     // addr -> enabled

    std::atomic<u16>  memory_view_addr_{0x0000};
};

} // namespace apple1

// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// debugger.cpp - pause/break/step state.

#include "debugger.h"

namespace apple1 {

bool Debugger::should_pause_before(u16 pc, u8 opcode) {
    // "Pause after the next instruction" latch - the RunToRTS path sets
    // this on the cycle that issues the RTS, and we honor it on the very
    // next call (which is the first instruction at the return address).
    if (pause_next_.exchange(false)) {
        paused_.store(true);
        return true;
    }

    // Single-step latch: allow one instruction past a pause.
    if (step_pending_.exchange(false)) {
        return false;
    }

    // Run-to-RTS: when the next opcode about to execute is an RTS ($60),
    // let it run, then arrange to pause on the call after.  This is what
    // most debuggers' "step out" / "run to return" does.
    if (run_to_rts_.load() && opcode == 0x60) {
        run_to_rts_.store(false);
        pause_next_.store(true);
        return false;
    }

    // One-shot breakpoint (used by step-over).
    if (one_shot_active_.load() && one_shot_bp_.load() == pc) {
        one_shot_active_.store(false);
        paused_.store(true);
        return true;
    }

    // Persistent breakpoints - only enabled ones pause the CPU.
    {
        std::lock_guard<std::mutex> lk(bp_mutex_);
        auto it = breakpoints_.find(pc);
        if (it != breakpoints_.end() && it->second) {
            paused_.store(true);
            return true;
        }
    }

    return paused_.load();
}

void Debugger::toggle_pause() {
    bool was = paused_.exchange(!paused_.load());
    if (was) {
        // Was paused, now resuming - cancel any pending single-step so the
        // main loop runs freely.
        step_pending_.store(false);
    }
}

void Debugger::resume() {
    paused_.store(false);
}

void Debugger::request_step() {
    // Clear other latches so STEP works regardless of the prior mode.
    // Without this, an armed RunToRTS or pause_next_ would short-circuit
    // the step before it ever reached the step_pending_ check.
    run_to_rts_.store(false);
    pause_next_.store(false);
    one_shot_active_.store(false);

    // Combination "step then pause": should_pause_before consumes
    // step_pending_ first (returns false -> one instruction runs), then
    // on the next call sees paused_ and pauses.  That's exactly one
    // instruction per click, ending in the paused state.
    step_pending_.store(true);
    paused_.store(true);
}

void Debugger::request_step_over(u16 return_addr) {
    one_shot_bp_.store(return_addr);
    one_shot_active_.store(true);
    paused_.store(false);
}

void Debugger::request_run_free() {
    run_to_rts_.store(false);
    pause_next_.store(false);
    step_pending_.store(false);
    paused_.store(false);
}

void Debugger::request_run_to_rts() {
    run_to_rts_.store(true);
    pause_next_.store(false);
    step_pending_.store(false);
    paused_.store(false);
}

void Debugger::request_pause() {
    run_to_rts_.store(false);
    pause_next_.store(false);
    step_pending_.store(false);
    paused_.store(true);
}

RunMode Debugger::current_mode() const {
    if (run_to_rts_.load()) return RunMode::RunToRTS;
    if (paused_.load())     return RunMode::Step;
    return RunMode::Free;
}

void Debugger::toggle_breakpoint(u16 addr) {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    auto it = breakpoints_.find(addr);
    if (it == breakpoints_.end()) {
        breakpoints_[addr] = true;            // new BPs default enabled
    } else {
        breakpoints_.erase(it);
    }
}

void Debugger::add_breakpoint(u16 addr) {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    breakpoints_.emplace(addr, true);          // no-op if already present
}

void Debugger::remove_breakpoint(u16 addr) {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    breakpoints_.erase(addr);
}

void Debugger::set_breakpoint_enabled(u16 addr, bool enabled) {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    auto it = breakpoints_.find(addr);
    if (it != breakpoints_.end()) it->second = enabled;
}

void Debugger::clear_all_breakpoints() {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    breakpoints_.clear();
}

bool Debugger::has_breakpoint(u16 addr) const {
    // Used by should_pause_before AND the disasm marker.  Returns true
    // only for ENABLED breakpoints so the marker doesn't lie about
    // which BPs will actually fire.
    std::lock_guard<std::mutex> lk(bp_mutex_);
    auto it = breakpoints_.find(addr);
    return it != breakpoints_.end() && it->second;
}

bool Debugger::breakpoint_enabled(u16 addr) const {
    return has_breakpoint(addr);
}

std::vector<Debugger::Breakpoint> Debugger::breakpoints_snapshot() const {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    std::vector<Breakpoint> out;
    out.reserve(breakpoints_.size());
    for (const auto& kv : breakpoints_) {
        out.push_back({ kv.first, kv.second });
    }
    return out;
}

} // namespace apple1

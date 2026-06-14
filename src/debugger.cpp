// debugger.cpp - pause/break/step state.

#include "debugger.h"

namespace apple1 {

bool Debugger::should_pause_before(u16 pc) {
    // Single-step latch: allow one instruction past a pause.
    if (step_pending_.exchange(false)) {
        return false;
    }

    // One-shot breakpoint (used by step-over).
    if (one_shot_active_.load() && one_shot_bp_.load() == pc) {
        one_shot_active_.store(false);
        paused_.store(true);
        return true;
    }

    // Persistent breakpoints.
    {
        std::lock_guard<std::mutex> lk(bp_mutex_);
        if (breakpoints_.count(pc)) {
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
    step_pending_.store(true);
    paused_.store(false);
    // The CPU thread will execute one instruction, then on the NEXT
    // should_pause_before call will see paused_=true again (because we'll
    // re-pause it from the UI).  Actually simpler: we let it execute one
    // instruction, then immediately set paused_ back to true.
    //
    // Implementation note: should_pause_before consumes step_pending_, so
    // one instruction goes through.  We need the main loop to come back to
    // pause-checking after; the easiest way is to mark paused_ here so on
    // the *next* call it stops again.
    paused_.store(true);
}

void Debugger::request_step_over(u16 return_addr) {
    one_shot_bp_.store(return_addr);
    one_shot_active_.store(true);
    paused_.store(false);
}

void Debugger::toggle_breakpoint(u16 addr) {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    auto it = breakpoints_.find(addr);
    if (it == breakpoints_.end()) {
        breakpoints_.insert(addr);
    } else {
        breakpoints_.erase(it);
    }
}

bool Debugger::has_breakpoint(u16 addr) const {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    return breakpoints_.count(addr) != 0;
}

std::set<u16> Debugger::breakpoints() const {
    std::lock_guard<std::mutex> lk(bp_mutex_);
    return breakpoints_;
}

} // namespace apple1

// bus.h - the memory bus and all I/O.  Single point of truth for which
// address ranges go where.  Owns the RAM, exposes PIA registers for the
// keyboard and display, hosts the ACI tape input mechanism, and holds the
// ROM images that CPU code can read.
//
// Memory map (Apple-1 + emulator extensions):
//   $0000-$1FFF  RAM (8KB on-board)
//   $C000-$C0FF  ACI tape data port (bit-7 flip-flop)
//   $C100-$C1FF  ACI ROM (mini-monitor for tape operations)
//   $D000-$D0FF  PIA (real Apple-1 only decoded low 2 bits, so all 256
//                addresses map to one of four registers - BASIC depends on
//                this since it reads $D0F2 instead of $D012, etc.)
//   $E000-$EFFF  BASIC RAM (4KB; loaded from basic.rom at boot)
//   $FF00-$FFFF  Wozmon ROM

#pragma once

#include "common.h"
#include "disk_ii.h"
#include "roms.h"
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>
#include <functional>

namespace apple1 {

class CPU6502;   // forward decl, defined in cpu6502.h

class Bus {
public:
    // The Bus needs the loaded ROM images.  Wozmon is required; BASIC and
    // ACI ROMs are optional - missing them disables their features but
    // doesn't fail to boot.
    explicit Bus(const roms::Set& rom_set);

    // 6502 read/write.  Single-cycle reads; side-effects (clearing the
    // keyboard "ready" bit on KBD read, advancing the tape flip-flop) happen
    // inside here.
    u8   read(u16 addr);
    void write(u16 addr, u8 val);

    // Convenience: load a flat buffer at a given address.
    void load_bytes(u16 addr, const std::vector<u8>& data);

    // Inject a host keypress.  Thread-safe; called from the keyboard thread.
    void feed_key(char ch);

    // ACI tape input.  Loads a sequence of CPU-cycle deltas representing
    // tape transitions.  Each entry says "after this many CPU cycles, flip
    // bit-7 at $C0xx".  The ACI ROM's read loop measures the cycles between
    // flips to decode 0 vs 1 bits.
    void load_tape(const std::vector<u32>& transitions);

    // Set the CPU pointer so the ACI tape mechanism can read cycle counts.
    // The Bus and CPU have a circular reference because the ACI is timing-
    // driven; this is the cleanest way to break that.  Also propagates to
    // the Disk II controller, which paces nibble latching off the same
    // cycle counter.
    void set_cpu(CPU6502* cpu) { cpu_ = cpu; disk_.set_cpu(cpu); }

    // Disk II access for the app / GUI.  When a .dsk is mounted, the Bus
    // routes $C000-$C00F to the disk controller and disables the ACI tape
    // path entirely (per the "ACI off when disk inserted" rule).
    DiskII&       disk()       { return disk_; }
    const DiskII& disk() const { return disk_; }

    // Display output callback.  Invoked from inside write() when CPU writes
    // to the display port.  Must be thread-safe (called on the CPU thread).
    using DisplayCallback = std::function<void(u8 ch)>;
    void set_display_callback(DisplayCallback cb) { display_cb_ = std::move(cb); }

    // Display-busy callback.  Invoked when the CPU writes a bit-7-clear
    // value to $D012 (which the real display ignores visually but still
    // takes a frame to acknowledge).  Programs use this for timing loops.
    using DisplayWaitCallback = std::function<void()>;
    void set_display_wait_callback(DisplayWaitCallback cb) {
        display_wait_cb_ = std::move(cb);
    }

    // Diagnostics / debugger.  RAM is exposed read-only for the debugger UI;
    // it shouldn't write directly to RAM.
    const std::array<u8, 0x2000>& ram() const { return ram_; }
    const std::vector<u8>& basic_ram() const { return basic_ram_; }
    bool has_basic() const { return !basic_ram_.empty(); }
    bool has_aci()   const { return !aci_rom_.empty(); }

    // Tape diagnostics for the debugger panel.
    u64 tape_reads()        const { return tape_reads_; }
    u64 tape_flips()        const { return tape_flips_; }
    std::size_t tape_index() const { return tape_index_; }
    std::size_t tape_total() const { return tape_transitions_.size(); }
    u64 tape_next_cycle()   const { return tape_next_cycle_; }

    // Auto-prompt state.  When the ACI ROM polls $C0xx and there's no
    // tape loaded, we want the GUI to pop up a file dialog.  The CPU
    // thread can't open a dialog itself, so it sets a "needed" flag here
    // that the GUI polls.  Cancelling the dialog sets the "cancelled"
    // flag to prevent re-prompting forever.
    bool tape_requested() const { return tape_requested_.load(); }
    void clear_tape_request()   { tape_requested_.store(false); }
    void set_tape_cancelled()   { tape_cancelled_.store(true); }
    void reset_tape_state() {
        tape_requested_.store(false);
        tape_cancelled_.store(false);
        tape_transitions_.clear();
        tape_index_ = 0;
        tape_state_ = 0;
        tape_reads_ = 0;
        tape_flips_ = 0;
    }

    // Reset PIA state - matches what a real 6820 PIA does on the /RES
    // line.  Called from App::reset_cpu so Wozmon's pre-config 0x7F poke
    // gets dropped on every reset, not just the first one.
    void reset_pia() { pia_b_configured_ = false; }

    // Cycle-accurate peek without side effects, for the disassembler and
    // memory inspector.  Returns 0 from unmapped regions.
    u8 peek(u16 addr) const;

private:
    void poll_keyboard();
    u8   read_pia(u16 addr);
    void write_pia(u16 addr, u8 val);
    u8   read_tape();

    // 8KB on-board RAM.
    std::array<u8, 0x2000> ram_{};

    // BASIC RAM (4KB at $E000-$EFFF).  We hold it in a vector for
    // flexibility; it's resized to either 0 (no BASIC) or 4096.
    std::vector<u8> basic_ram_;

    // Read-only ROMs.
    std::vector<u8> wozmon_rom_;
    std::vector<u8> aci_rom_;

    // PIA / keyboard state.  The deque is the host-side input buffer;
    // kbd_data_ + kbd_ready_ mirror the PIA's view of "one byte at a time"
    // semantics.  Mutex protects against the keyboard input thread.
    std::deque<char> kbd_buffer_;
    std::mutex       kbd_mutex_;
    bool             kbd_ready_ = false;
    u8               kbd_data_  = 0;

    // ACI tape input state.
    CPU6502*         cpu_ = nullptr;        // for cycle-count access
    std::vector<u32> tape_transitions_;
    std::size_t      tape_index_ = 0;
    u8               tape_state_ = 0;       // 0x00 or 0x80, returned at $C0xx
    u64              tape_next_cycle_ = 0;
    u64              tape_reads_ = 0;       // diagnostic: $C0xx read count
    u64              tape_flips_ = 0;       // diagnostic: bit-7 flips applied

    // Auto-prompt flags - set from CPU thread, read from GUI thread.
    std::atomic<bool> tape_requested_{false};
    std::atomic<bool> tape_cancelled_{false};

    DisplayCallback     display_cb_;
    DisplayWaitCallback display_wait_cb_;

    // Disk II controller.  Inactive until a .dsk is mounted.  When active,
    // it owns $C000-$C00F (soft switches) and the Bus ignores the ACI
    // tape path so the two don't fight over the same address range.
    DiskII              disk_;

    // True once the CPU has written to $D013 (DSP control register).
    // Before this, the PIA's data-direction register on the B-side is
    // still 0 (all inputs), so writes to $D012 don't actually reach the
    // display.  Wozmon writes 0x7F to $D012 before configuring the PIA;
    // this flag prevents that early 0x7F from appearing as '?'.
    bool pia_b_configured_ = false;
};

} // namespace apple1

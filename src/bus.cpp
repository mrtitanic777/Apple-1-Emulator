// bus.cpp - memory map dispatch.

#include "bus.h"
#include "cpu6502.h"

namespace apple1 {

Bus::Bus(const roms::Set& rom_set)
    : wozmon_rom_(rom_set.wozmon)
{
    if (rom_set.basic) {
        basic_ram_ = *rom_set.basic;        // copy 4KB into writable RAM region
    }
    if (rom_set.aci) {
        aci_rom_ = *rom_set.aci;
    }
}

void Bus::poll_keyboard() {
    if (kbd_ready_) return;
    std::lock_guard<std::mutex> lk(kbd_mutex_);
    if (!kbd_buffer_.empty()) {
        char ch = kbd_buffer_.front();
        kbd_buffer_.pop_front();
        kbd_data_  = static_cast<u8>(ch) | 0x80;   // Apple-1 sets bit 7
        kbd_ready_ = true;
    }
}

u8 Bus::read_pia(u16 addr) {
    // Real Apple-1 PIA decode only used the low two bits of the address,
    // so $D0xx all hit one of four registers.  BASIC depends on this
    // (it reads $D0F2 instead of $D012).
    switch (addr & 0x03) {
        case 0x00:  // KBD - clearing 'ready' is a side effect of the read
            kbd_ready_ = false;
            return kbd_data_;
        case 0x01:  // KBDCR - bit 7 set if a key is available
            poll_keyboard();
            return kbd_ready_ ? 0x80 : 0x00;
        case 0x02:  // DSP - always ready (no real wait, this is software)
            return 0x00;
        default:    // DSPCR
            return 0x00;
    }
}

void Bus::write_pia(u16 addr, u8 val) {
    switch (addr & 0x03) {
        case 0x02:  // DSP - write character to display
            // The real Apple-1 PIA has a data-direction register on the
            // B-side at $D013.  Until the CPU configures it, writes to
            // $D012 don't propagate to the display.  Wozmon's reset code
            // writes 0x7F to $D012 BEFORE configuring DDR-B - on real
            // hardware that early write is silently dropped, and we need
            // to do the same so '?' doesn't appear at boot.
            if (!pia_b_configured_) break;
            // Display latches bits 0-5 of whatever is written, regardless
            // of bit 7.  Strip bit 7 so the display callback sees plain
            // 7-bit data.
            if (display_cb_) display_cb_(val & 0x7F);
            break;
        case 0x03:  // DSPCR - configures the display PIA's B-side
            pia_b_configured_ = true;
            break;
        default:
            break;  // KBD/KBDCR writes are no-ops
    }
}

u8 Bus::read_tape() {
    ++tape_reads_;

    // Auto-prompt trigger: ACI ROM is polling $C0xx and no tape has been
    // loaded.  Tell the GUI thread to open a file dialog.  Once cancelled
    // we stop re-prompting until reset.
    if (tape_transitions_.empty() && !tape_cancelled_.load()
        && cpu_ != nullptr) {
        u16 pc = cpu_->pc();
        if (pc >= 0xC100 && pc <= 0xC1FF) {
            tape_requested_.store(true);
        }
    }

    if (cpu_ != nullptr && tape_index_ <= tape_transitions_.size()) {
        const u64 now = cpu_->cycles();
        while (tape_index_ <= tape_transitions_.size() && now >= tape_next_cycle_) {
            tape_state_ ^= 0x80;
            ++tape_flips_;
            if (tape_index_ < tape_transitions_.size()) {
                tape_next_cycle_ += tape_transitions_[tape_index_];
                ++tape_index_;
            } else {
                ++tape_index_;
                break;
            }
        }
    }
    return tape_state_;
}

u8 Bus::read(u16 addr) {
    if (addr < 0x2000)                          return ram_[addr];
    if (addr >= 0xE000 && addr <= 0xEFFF) {
        return basic_ram_.empty() ? 0x00 : basic_ram_[addr - 0xE000];
    }
    if (addr >= 0xC100 && addr <= 0xC1FF) {
        return aci_rom_.empty() ? 0x00 : aci_rom_[addr - 0xC100];
    }
    if (addr >= 0xC000 && addr <= 0xC0FF) {
        // Disk II takes over $C000-$C0FF whenever an image is mounted.
        // The eight 16-byte soft-switch banks all decode to the same
        // switches; we mask the low 4 bits inside DiskII::read.
        if (disk_.mounted())                    return disk_.read(addr);
        return read_tape();
    }
    if (addr >= 0xD000 && addr <= 0xD0FF)       return read_pia(addr);
    if (addr >= 0xFF00)                         return wozmon_rom_[addr - 0xFF00];
    return 0x00;
}

void Bus::write(u16 addr, u8 val) {
    if (addr < 0x2000) { ram_[addr] = val; return; }
    if (addr >= 0xE000 && addr <= 0xEFFF) {
        if (!basic_ram_.empty()) basic_ram_[addr - 0xE000] = val;
        return;
    }
    if (addr >= 0xD000 && addr <= 0xD0FF) { write_pia(addr, val); return; }
    if (addr >= 0xC000 && addr <= 0xC0FF) {
        // Writes to soft switches act as toggles too (real hardware
        // strobes the same line on R/W).  Drop the value; only the
        // address matters.
        if (disk_.mounted()) disk_.write(addr, val);
        return;
    }
    // ROM and unmapped writes are silently dropped.
}

u8 Bus::peek(u16 addr) const {
    // Read with no side effects.  Returns RAM/ROM contents directly; for
    // I/O regions returns 0 (since values there are dynamic / stateful).
    if (addr < 0x2000)                          return ram_[addr];
    if (addr >= 0xE000 && addr <= 0xEFFF) {
        return basic_ram_.empty() ? 0x00 : basic_ram_[addr - 0xE000];
    }
    if (addr >= 0xC100 && addr <= 0xC1FF) {
        return aci_rom_.empty() ? 0x00 : aci_rom_[addr - 0xC100];
    }
    if (addr >= 0xFF00)                         return wozmon_rom_[addr - 0xFF00];
    return 0x00;
}

void Bus::load_bytes(u16 addr, const std::vector<u8>& data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        write(static_cast<u16>((addr + i) & 0xFFFF), data[i]);
    }
}

void Bus::feed_key(char ch) {
    std::lock_guard<std::mutex> lk(kbd_mutex_);
    kbd_buffer_.push_back(ch);
}

void Bus::load_tape(const std::vector<u32>& transitions) {
    tape_transitions_ = transitions;
    tape_index_ = 0;
    tape_state_ = 0;
    if (cpu_ != nullptr && !tape_transitions_.empty()) {
        tape_next_cycle_ = cpu_->cycles() + tape_transitions_[0];
        tape_index_ = 1;
    }
}

} // namespace apple1

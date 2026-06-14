// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// disk_ii.cpp - Disk II controller logic and DOS 3.3 6-and-2 GCR encoder.
//
// References:
//   * Beneath Apple DOS, Worth & Lechner, 1981 - track/sector layout and
//     6-and-2 GCR scheme (the canonical reference).
//   * Apple ][ Disk II Interface Card Schematic and theory of operation.
//
// The encoder produces nibble sequences identical to what a real DOS 3.3
// formatted disk presents to the bus: gap1 / address-field / gap2 /
// data-field / gap3, repeated 16 times per track.

#include "disk_ii.h"
#include "cpu6502.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace apple1 {

namespace {

// Standard DOS 3.3 sector skew: logical sector N -> physical position P.
// DOS interleaves sectors so the next logical sector arrives under the
// head just as RWTS finishes processing the previous one.
constexpr u8 kDos33Skew[16] = {
    0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
    0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};

// Inverse skew: physical sector P -> logical sector L.
// Used for .dsk byte-offset translation: physical sector P's data
// lives at byte offset kDos33SkewInv[P] * 256 in the (logical-order)
// .dsk image.  This is the correct direction - the previous code
// used the forward skew where it needed the inverse, which made
// build.ps1 + emulator mutually consistent but mismatched against
// OneDos's RWTS (which uses true inverse skew at $C240) and against
// real Apple II hardware.
constexpr u8 kDos33SkewInv[16] = {
    0x0, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x1,
    0xE, 0xC, 0xA, 0x8, 0x6, 0x4, 0x2, 0xF
};

// 6-and-2 nibble table.  Maps a 6-bit value (0..63) to the disk-byte
// representation.  These bytes all have bit 7 set and no two consecutive
// 0-bits, the requirement for valid Apple disk nibbles.
constexpr u8 kGcr6and2[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
    0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
    0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
    0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
    0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

// Two-byte 4-and-4 encoding used in the address field for volume/track/
// sector/checksum.  Splits each bit of v across two bytes: odd bits go in
// the first, even bits in the second, with all the other bits set to 1.
inline void encode_44(u8 v, u8 out[2]) {
    out[0] = static_cast<u8>(((v >> 1) & 0x55) | 0xAA);
    out[1] = static_cast<u8>((v & 0x55) | 0xAA);
}

// Disk II timing.
//
// The Disk II's bit cell is 4us (the on-card shift register is clocked
// from a 2 MHz crystal divided down).  Eight bits per byte gives a
// 32us byte time - one new "nibble" appears at the data register every
// 32us of wall-clock time.
//
// We model the 6502 at 1us per clock cycle (the Apple-1 runs at 1.023
// MHz; calling that "1 MHz" loses 2.3% but DOS's timing loops are
// nowhere near that tight, and integer arithmetic stays clean).  Under
// that assumption:
//
//     1 cycle  =  1us
//     1 bit    =  4us  =  4 cycles
//     1 byte   = 32us  = 32 cycles  =  kCyclesPerNibble
//
// update_data_reg() therefore advances nib_index_ by
// (cpu_cycles_elapsed / 32) on every read, which matches the head's
// rotational position to within one byte.
constexpr u64 kMicrosPerCycle  = 1;          // modeling assumption
constexpr u64 kMicrosPerBit    = 4;          // real Disk II bit time
constexpr u64 kCyclesPerBit    = kMicrosPerBit / kMicrosPerCycle;
constexpr u64 kCyclesPerByte   = kCyclesPerBit * 8;        // 32
constexpr u64 kCyclesPerNibble = kCyclesPerByte;           // legacy alias
static_assert(kCyclesPerBit == 4,  "bit-time math broken");
static_assert(kCyclesPerByte == 32, "byte-time math broken");

} // namespace

void DiskII::encode_sector(int track, int sector, const u8* in,
                           std::vector<u8>* out) {
    // ---------- Address field ----------
    // Prologue: D5 AA 96
    out->push_back(0xD5); out->push_back(0xAA); out->push_back(0x96);

    // Volume (default 254), track, sector, checksum - each as 4-and-4.
    const u8 volume = 254;
    u8 enc[2];
    encode_44(volume,                                   enc); out->push_back(enc[0]); out->push_back(enc[1]);
    encode_44(static_cast<u8>(track),                   enc); out->push_back(enc[0]); out->push_back(enc[1]);
    encode_44(static_cast<u8>(sector),                  enc); out->push_back(enc[0]); out->push_back(enc[1]);
    const u8 addr_chk = volume ^ static_cast<u8>(track) ^ static_cast<u8>(sector);
    encode_44(addr_chk,                                 enc); out->push_back(enc[0]); out->push_back(enc[1]);

    // Epilogue: DE AA EB
    out->push_back(0xDE); out->push_back(0xAA); out->push_back(0xEB);

    // Gap 2: a handful of self-sync $FF bytes.
    for (int i = 0; i < 6; ++i) out->push_back(0xFF);

    // ---------- Data field ----------
    // Prologue: D5 AA AD
    out->push_back(0xD5); out->push_back(0xAA); out->push_back(0xAD);

    // 6-and-2 GCR: 256 bytes of sector data become 342 6-bit values, then
    // one trailing checksum byte = 343 disk bytes.
    //   * "twos" array (86 bytes): the low 2 bits of every input byte,
    //     packed in groups of 3 into 6-bit slots.
    //   * "sixes" array (256 bytes): the high 6 bits of every input byte.
    // Then XOR-chain each 6-bit value with the previous to produce the
    // values that get table-mapped into disk bytes.
    //
    // Index mapping (matches the canonical Apple-1/II P6 boot decoder):
    //   The boot reads twos[85] FIRST off disk and stores at AUXBUF[85];
    //   it then walks AUX_IDX from 85 down to 0 paired with output Y =
    //   0..85.  So AUXBUF[85] -> output 0, AUXBUF[0] -> output 85.  In
    //   encoder terms: twos[85-k] holds bit-pairs for inputs k, k+86,
    //   k+172.  Equivalently for loop variable i: twos[i] holds pairs
    //   for inputs (85-i), (85-i)+86, (85-i)+172.
    //
    // For i=0,1 the third input (85-i)+172 = 257 or 256 doesn't exist
    // (the 2-bit residue space has 86*3 = 258 slots but only 256 inputs);
    // those two slots are zero-filled, NOT read past the sector buffer.
    u8 twos[86]  = {0};
    u8 sixes[256];

    for (int i = 0; i < 256; ++i) {
        sixes[i] = static_cast<u8>((in[i] >> 2) & 0x3F);
    }
    auto rev2 = [](u8 v) -> u8 { return static_cast<u8>(((v & 1) << 1) | ((v & 2) >> 1)); };
    for (int i = 0; i < 86; ++i) {
        const int k  = 85 - i;                      // input base index 85..0
        const u8  b0 = rev2(static_cast<u8>(in[k]        & 0x03));
        const u8  b1 = rev2(static_cast<u8>(in[k +  86]  & 0x03));   // 86..171: always in range
        const u8  b2 = (k + 172 < 256)
                       ? rev2(static_cast<u8>(in[k + 172] & 0x03))   // 172..255 only when k <= 83
                       : 0;
        twos[i] = static_cast<u8>(b0 | (b1 << 2) | (b2 << 4));
    }

    // XOR-chain twos[], then sixes[], emit 6-bit codes, then trailing checksum.
    u8 last = 0;
    for (int i = 85; i >= 0; --i) {
        u8 v = static_cast<u8>(twos[i] ^ last);
        out->push_back(kGcr6and2[v & 0x3F]);
        last = twos[i];
    }
    for (int i = 0; i < 256; ++i) {
        u8 v = static_cast<u8>(sixes[i] ^ last);
        out->push_back(kGcr6and2[v & 0x3F]);
        last = sixes[i];
    }
    // Trailing checksum: just the table-encoded "last".
    out->push_back(kGcr6and2[last & 0x3F]);

    // Epilogue: DE AA EB
    out->push_back(0xDE); out->push_back(0xAA); out->push_back(0xEB);

    // Gap 3: a few more sync bytes between this sector and the next.
    for (int i = 0; i < 27; ++i) out->push_back(0xFF);
}

void DiskII::encode_track(int t) {
    // Each track gets a leading gap of self-sync $FF nibbles, then 16
    // sectors back-to-back, then padding $FF's to fill out kNibblesPerTrack.
    if (t < 0 || t >= 35) return;
    std::vector<u8>& buf = tracks_nib_[t];
    buf.clear();
    buf.reserve(kNibblesPerTrack);

    // Gap 1: ~48 self-sync bytes at the top of the track.
    for (int i = 0; i < 48; ++i) buf.push_back(0xFF);

    // The .dsk file is laid out in standard DOS 3.3 LOGICAL order:
    // byte offset (T*16 + L)*256 holds the data of logical sector L.
    // When we lay down physical sector P here, we present the bytes
    // that logical sector kDos33SkewInv[P] contains - i.e. P=1 -> L=13,
    // P=2 -> L=11, etc.  RWTS on the guest reads logical L by
    // translating to physical SKEW_INV[L] and spinning until that
    // physical addr field passes, then decoding the data.  Round-trip
    // is clean: write logical L -> stored at byte L*256.
    for (int phys = 0; phys < 16; ++phys) {
        int logical = kDos33SkewInv[phys];
        const u8* sector_data = &raw_image_[(t * 16 + logical) * 256];
        encode_sector(t, phys, sector_data, &buf);
    }

    // Pad to kNibblesPerTrack with self-sync $FF.
    while (buf.size() < kNibblesPerTrack) buf.push_back(0xFF);
    // If encode overshot (shouldn't with these gap sizes), trim.
    if (buf.size() > kNibblesPerTrack) buf.resize(kNibblesPerTrack);
}

void DiskII::encode_all_tracks() {
    for (int t = 0; t < 35; ++t) encode_track(t);
}

bool DiskII::mount_dsk(const std::string& path, std::string* err_out) {
    // If we already have an image with pending writes, flush them back
    // to the OLD file before swapping.  Discarding would silently lose
    // whatever the guest just wrote.
    if (mounted_) flush_write_session();

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err_out) *err_out = "can't open: " + path;
        return false;
    }
    raw_image_.assign((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
    if (raw_image_.size() != 143360) {
        if (err_out) {
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                          "expected 143360 bytes for .dsk, got %zu",
                          raw_image_.size());
            *err_out = buf;
        }
        raw_image_.clear();
        return false;
    }
    image_path_ = path;
    encode_all_tracks();

    // Reset DISK state on mount, but leave the DRIVE state alone -
    // in particular motor_on_.  The motor is part of the mechanism,
    // not the disk; real hardware doesn't stop spinning when you
    // swap a disk.  Critically, if the boot ROM is mid-poll waiting
    // for the auto-prompt to deliver a disk, it has already issued
    // its one-time LDA MOTON and would never re-issue it - so
    // resetting motor_on_ would hang the CPU at BPL even though the
    // disk IS now mounted.
    mounted_       = true;
    phase_pos_     = 0;
    for (volatile bool& p : phase_state_) p = false;
    active_drive_  = 1;
    q6_high_       = false;
    q7_high_       = false;
    nib_index_       = 0;
    byte_bit_index_  = 0;
    shift_reg_       = 0;
    next_bit_cycle_  = 0;       // primed lazily on first read
    write_nibbles_.clear();
    return true;
}

void DiskII::eject() {
    flush_write_session();
    mounted_ = false;
    image_path_.clear();
    raw_image_.clear();
    for (auto& t : tracks_nib_) t.clear();
    motor_on_ = false;
    write_nibbles_.clear();
}

void DiskII::touch_phase(int phase, bool on) {
    // Apple stepper algorithm: when a phase turns ON, if it's adjacent to
    // the current phase_pos_ % 8 mod-pattern, step toward it.  We use a
    // simplified model that's good enough for DOS RWTS:
    //   * Track position is encoded as half-track (phase_pos_ in 0..69).
    //   * Current "phase under head" = (phase_pos_ / 2) % 4.
    //   * If new phase = current + 1 mod 4 -> step up.
    //   * If new phase = current - 1 mod 4 -> step down.
    //   * If new phase = current -> no movement.
    //   * Opposite phase -> ignored (real drive does nothing useful).
    const bool was_on = phase_state_[phase];
    phase_state_[phase] = on;
    if (!on || was_on) return;        // only rising edges trigger a step

    const int cur_phase = (phase_pos_ / 2) % 4;
    const int diff_up   = (phase - cur_phase + 4) % 4;
    if (diff_up == 1 && phase_pos_ < 68) {
        phase_pos_ += 2;              // up one whole track
    } else if (diff_up == 3 && phase_pos_ > 0) {
        phase_pos_ -= 2;              // down one whole track
    }
    // Snap nibble index proportionally so we don't sit on the same byte
    // every seek (otherwise DOS sees a stuck stream and times out).
    if (mounted_ && phase_pos_ / 2 < 35) {
        std::size_t track_size = tracks_nib_[phase_pos_ / 2].size();
        if (track_size > 0) nib_index_ %= track_size;
    }
}

void DiskII::touch_motor(bool on) {
    if (on == motor_on_) return;
    if (!on) {
        // Motor stopping closes any open write session - DOS turns the
        // motor off after RWTS finishes, which is our cue to commit.
        flush_write_session();
    }
    motor_on_ = on;
    if (on) {
        // Motor just turned on.  Start the bit clock NOW - one bit
        // every 4 cycles from this point, forever (until motor off).
        // No primer, no lazy init: if the motor is on, cycles count
        // and bits shift, full stop.
        shift_reg_       = 0x00;
        byte_bit_index_  = 0;
        next_bit_cycle_  = cpu_ ? (cpu_->cycles() + kCyclesPerBit) : 0;
    }
}

void DiskII::touch_drive(int drive) {
    // Single-drive build: silently ignore the request to switch to drive 2.
    if (drive == 1) active_drive_ = 1;
}

void DiskII::touch_q6(bool high) { q6_high_ = high; }

void DiskII::touch_q7(bool high) {
    if (q7_high_ && !high) {
        // Falling edge (write -> read): write session ends here.
        flush_write_session();
    } else if (!q7_high_ && high) {
        // Rising edge (read -> write): start a fresh capture buffer.
        // Snapshot which encoded-track byte position the head is at
        // so flush_write_session() can identify the sector by walking
        // backward to the previous addr field (RWTS only writes the
        // data field; the addr field is unchanged).
        write_nibbles_.clear();
        write_start_track_     = phase_pos_ / 2;
        write_start_nib_index_ = nib_index_;
    }
    q7_high_ = high;
}

void DiskII::update_shift_reg() {
    // Bit-level cycle-driven shift: clock one disk bit into the Wozniak
    // LS166 shift register every 4 CPU cycles.  New bit lands in LSB
    // (bit 0), the previous register contents shift left by 1, and
    // the old MSB falls off the top.  Multiple CPU reads of Q6L
    // within one 4-cycle window see the same register value - real
    // hardware doesn't consume on read, the shift register just keeps
    // sampling the head's bit stream.
    //
    // Byte alignment relies on the gap1 self-sync $FF run (48+ bytes
    // = 384+ bits) at the top of each track settling the register
    // before any data byte arrives.  Once a 1-bit walks into bit 7,
    // the polling CPU's BPL falls through; over the next 4 cycles the
    // register holds a complete byte for the read to capture.
    //
    // The byte stream stored in tracks_nib_[t] is flat 8-bit bytes
    // in MSB-first order.  Sync $FF bytes are just regular $FF in
    // the stream - no special encoding.  The "clear on bit-7-read"
    // behavior in read() resets the register after every byte
    // delivered, so byte boundaries stay clean even through long
    // runs of sync FFs.
    if (!mounted_ || !motor_on_ || !cpu_) return;
    const int t = phase_pos_ / 2;
    if (t < 0 || t >= 35) return;
    const std::vector<u8>& buf = tracks_nib_[t];
    if (buf.empty()) return;

    const u64 now = cpu_->cycles();

    if (byte_latch_.load(std::memory_order_relaxed)) {
        // ---- BYTE-LEVEL latch (deterministic across hosts) ----
        // Deliver one whole byte every 32 emulated cycles.  No
        // partial shift register; no per-bit state to race on.
        // next_bit_cycle_ is reused as "cycle the NEXT byte arrives".
        if (next_bit_cycle_ == 0) next_bit_cycle_ = now + kCyclesPerByte;
        while (now >= next_bit_cycle_) {
            shift_reg_ = buf[nib_index_];
            nib_index_ = (nib_index_ + 1) % buf.size();
            next_bit_cycle_ += kCyclesPerByte;
            // Valid GCR bytes always have bit 7 set, so the first
            // delivery satisfies the CPU's BPL Q6L poll.  Stop here
            // so the CPU sees this byte before the next is laid in.
            if (shift_reg_ & 0x80) break;
        }
        return;
    }

    // ---- BIT-LEVEL latch (cycle-accurate LS166) ----
    // Motor on = bit clock runs.  Catch up however many 4-cycle
    // ticks have elapsed since last call.  No priming, no idle
    // detection - just count cycles and shift.  Initial seed
    // happens at motor-on in touch_motor().
    if (next_bit_cycle_ == 0) next_bit_cycle_ = now + kCyclesPerBit;
    while (now >= next_bit_cycle_) {
        const u8 cur_byte = buf[nib_index_];
        const u8 bit = static_cast<u8>((cur_byte >> (7 - byte_bit_index_)) & 1);
        shift_reg_ = static_cast<u8>((shift_reg_ << 1) | bit);
        ++byte_bit_index_;
        next_bit_cycle_ += kCyclesPerBit;
        if (byte_bit_index_ >= 8) {
            byte_bit_index_ = 0;
            nib_index_ = (nib_index_ + 1) % buf.size();
            if (shift_reg_ & 0x80) break;
        }
    }
}

u64 DiskII::head_micros() const {
    if (!cpu_) return 0;
    return cpu_->cycles() * kMicrosPerCycle;
}

DiskII::HeadSnapshot DiskII::snapshot_head() const {
    // Pull every field into local variables in one tight burst.  Without
    // a mutex this isn't strictly atomic, but the window during which
    // the CPU thread can mutate state mid-read is now microseconds wide
    // instead of the multi-millisecond round-trip a multi-call display
    // takes; in practice the snapshot is consistent within a single
    // disk-byte time.  (A proper fix would be a mutex around the data
    // path, but the cost on the CPU-side hot loop is non-trivial.)
    HeadSnapshot s{};
    s.mounted = mounted_;
    if (!mounted_) return s;

    const int t = phase_pos_ / 2;
    s.track = t;
    if (t < 0 || t >= 35) return s;
    const std::vector<u8>& buf = tracks_nib_[t];
    if (buf.empty()) return s;

    // Snapshot mutable head fields in source order to minimise inter-
    // field race (read shift_reg LAST so any bit-tick that flips it
    // also moves byte_bit_index and nib_index, keeping the trio
    // consistent: a fresh shift_reg matches the freshest indices).
    const std::size_t idx = nib_index_;
    const int         bbi = byte_bit_index_;
    const u8          sr  = shift_reg_;
    s.nib_index      = idx;
    s.byte_bit_index = bbi;
    s.shift_reg      = sr;

    const std::ptrdiff_t sz = static_cast<std::ptrdiff_t>(buf.size());
    for (int d = -3; d <= 3; ++d) {
        std::ptrdiff_t i = static_cast<std::ptrdiff_t>(idx) + d;
        i %= sz;
        if (i < 0) i += sz;
        s.stream[d + 3] = buf[static_cast<std::size_t>(i)];
    }
    s.next_bit = static_cast<u8>((s.stream[3] >> (7 - bbi)) & 1);
    return s;
}

void DiskII::soft_switch_dispatch(u16 addr) {
    switch (addr & 0x0F) {
        case 0x0: touch_phase(0, false); break;
        case 0x1: touch_phase(0, true);  break;
        case 0x2: touch_phase(1, false); break;
        case 0x3: touch_phase(1, true);  break;
        case 0x4: touch_phase(2, false); break;
        case 0x5: touch_phase(2, true);  break;
        case 0x6: touch_phase(3, false); break;
        case 0x7: touch_phase(3, true);  break;
        case 0x8: touch_motor(false);    break;
        case 0x9: touch_motor(true);     break;
        case 0xA: touch_drive(1);        break;
        case 0xB: touch_drive(2);        break;   // single-drive: no-op
        case 0xC: touch_q6(false);       break;
        case 0xD: touch_q6(true);        break;
        case 0xE: touch_q7(false);       break;
        case 0xF: touch_q7(true);        break;
    }
}

u8 DiskII::read(u16 addr) {
    // Every $C00x access is a soft-switch first.  Reads of $C00C (Q6L)
    // in read mode return the current Wozniak shift register and, if
    // bit 7 was set (= byte was complete), CLEAR the register so the
    // next byte starts assembling from bit 0.  This is the real LS166
    // latch behavior - delivering a complete byte resets the latch
    // and bit-collection starts fresh, giving the polling loop clean
    // byte boundaries.
    //
    // Other soft-switch reads (PHASE0..3, MOTOR, DRIVE, Q6H, Q7L,
    // Q7H) just toggle their flip-flops and return the current shift
    // register as the floating-bus value (no shift, no clear).
    soft_switch_dispatch(addr);
    if ((addr & 0x0F) == 0x0C && !q6_high_ && !q7_high_) {
        update_shift_reg();
        const u8 v = shift_reg_;
        if (v & 0x80) shift_reg_ = 0;
        return v;
    }
    return shift_reg_;
}

void DiskII::write(u16 addr, u8 val) {
    // STA $C00D in write mode (Q7H high) loads the write latch; we treat
    // each such store as one nibble committed to the disk and stash it
    // for later denibblization by flush_write_session().  Capture happens
    // BEFORE the soft-switch dispatch so we use the q7_high_ state as it
    // was at the time of the store.
    if (q7_high_ && (addr & 0x0F) == 0x0D) {
        write_nibbles_.push_back(val);
    }
    soft_switch_dispatch(addr);
}

u8 DiskII::decode_44(u8 a, u8 b) {
    // Inverse of encode_44 in this same file:
    //   out[0] = ((v >> 1) & 0x55) | 0xAA   -> odd  bits of v at even pos
    //   out[1] = (v & 0x55) | 0xAA          -> even bits of v in place
    return static_cast<u8>(((a << 1) & 0xAA) | (b & 0x55));
}

bool DiskII::decode_data_field(const u8* gcr343, u8 out[256]) {
    // Inverse of kGcr6and2.  Built lazily on first use.  Valid disk
    // bytes live in [0x96..0xFF]; sentinel 0xFF marks invalid codes.
    static const auto inv = []() {
        std::array<u8, 0x100 - 0x96> t;
        t.fill(0xFF);
        for (int i = 0; i < 64; ++i) t[kGcr6and2[i] - 0x96] = static_cast<u8>(i);
        return t;
    }();
    auto lookup = [](u8 b) -> u8 {
        if (b < 0x96) return 0xFF;
        return inv[b - 0x96];
    };

    u8 twos_dec[86];
    u8 sixes_dec[256];

    // Encoder XOR-chains successive 6-bit values; decoder inverts that
    // by walking the same direction.  twos[] are emitted in reverse, so
    // disk byte k (for k=0..85) decodes to twos[85-k].
    u8 last = 0;
    for (int k = 0; k < 86; ++k) {
        u8 v = lookup(gcr343[k]);
        if (v == 0xFF) return false;
        u8 d = static_cast<u8>(v ^ last);
        twos_dec[85 - k] = d;
        last = d;
    }
    for (int k = 0; k < 256; ++k) {
        u8 v = lookup(gcr343[86 + k]);
        if (v == 0xFF) return false;
        u8 d = static_cast<u8>(v ^ last);
        sixes_dec[k] = d;
        last = d;
    }
    // Trailing checksum: a valid sector ends with chk ^ last == 0.
    u8 chk_v = lookup(gcr343[342]);
    if (chk_v == 0xFF) return false;
    if (static_cast<u8>(chk_v ^ last) != 0) return false;

    // Reconstruct each output byte from its high-6 (sixes_dec) and the
    // 2 low bits packed into twos_dec at the right pair position.
    //
    // Index mapping (must match encoder above): twos[i] holds pairs for
    // inputs (85-i), (85-i)+86, (85-i)+172.  Equivalently for output i:
    // the aux index is (85 - (i % 86)).
    auto rev2 = [](u8 v) -> u8 {
        return static_cast<u8>(((v & 1) << 1) | ((v & 2) >> 1));
    };
    for (int i = 0; i < 256; ++i) {
        int idx      = 85 - (i % 86);
        int pair_pos = (i < 86) ? 0 : (i < 172) ? 2 : 4;
        u8 raw_pair  = static_cast<u8>((twos_dec[idx] >> pair_pos) & 0x3);
        u8 low2      = rev2(raw_pair);
        out[i]       = static_cast<u8>((sixes_dec[i] << 2) | low2);
    }
    return true;
}

void DiskII::flush_write_session() {
    if (write_nibbles_.empty() || !mounted_) {
        write_nibbles_.clear();
        return;
    }

    bool any_written = false;
    const std::size_t N = write_nibbles_.size();

    // Helper: write decoded sector data into raw_image_ and re-encode.
    // sec is the PHYSICAL sector number from the address field; the
    // .dsk image lives in DOS 3.3 LOGICAL order, so translate via the
    // INVERSE skew (P -> L) before splicing the bytes in.  Round-trip:
    // guest writes logical L -> RWTS picks physical P=SKEW_INV[L] ->
    // emulator stores at byte (T*16 + SKEW_INV[P])*256 = (T*16+L)*256.
    auto commit_sector = [&](u8 trk, u8 sec, const u8* data) {
        if (trk >= 35 || sec >= 16) return;
        int log_sec = kDos33SkewInv[sec];
        std::size_t off = (static_cast<std::size_t>(trk) * 16 +
                           static_cast<std::size_t>(log_sec)) * 256;
        std::memcpy(&raw_image_[off], data, 256);
        encode_track(trk);
        any_written = true;
    };

    // ---- Pass 1: full-sector writes that include the addr field ----
    // (Format/INIT-style code re-emits D5 AA 96 then D5 AA AD.)
    std::size_t i = 0;
    while (i + 11 < N) {
        if (write_nibbles_[i]   == 0xD5 &&
            write_nibbles_[i+1] == 0xAA &&
            write_nibbles_[i+2] == 0x96) {

            u8 trk = decode_44(write_nibbles_[i+5], write_nibbles_[i+6]);
            u8 sec = decode_44(write_nibbles_[i+7], write_nibbles_[i+8]);
            if (trk >= 35 || sec >= 16) { ++i; continue; }

            std::size_t j = i + 11;
            bool found_data = false;
            while (j + 2 < N) {
                if (write_nibbles_[j]   == 0xD5 &&
                    write_nibbles_[j+1] == 0xAA &&
                    write_nibbles_[j+2] == 0xAD) {
                    found_data = true;
                    break;
                }
                ++j;
            }
            if (!found_data || j + 3 + 343 > N) break;

            u8 sector_data[256];
            if (decode_data_field(&write_nibbles_[j + 3], sector_data)) {
                commit_sector(trk, sec, sector_data);
            }
            i = j + 3 + 343;
            continue;
        }
        ++i;
    }

    // ---- Pass 2: data-field-only write (RWTS sector rewrite) ----
    // Real Disk II RWTS spins the head until the target address field
    // passes, then flips Q7H and writes ONLY the data field.  The addr
    // field stays put on the medium.  Identify the target sector by
    // walking backward in the encoded track from where the head was
    // when the write started, looking for the most recent D5 AA 96.
    if (!any_written && write_start_track_ >= 0
        && write_start_track_ < 35) {
        const std::vector<u8>& trk_buf = tracks_nib_[write_start_track_];
        const std::size_t TN = trk_buf.size();
        if (TN >= 11) {
            std::size_t pos  = write_start_nib_index_ % TN;
            std::size_t best = TN;          // sentinel = "not found"
            for (std::size_t step = 0; step < TN; ++step) {
                std::size_t k  = (pos + TN - step) % TN;
                std::size_t k1 = (k + 1) % TN;
                std::size_t k2 = (k + 2) % TN;
                if (trk_buf[k]  == 0xD5 &&
                    trk_buf[k1] == 0xAA &&
                    trk_buf[k2] == 0x96) {
                    best = k;
                    break;
                }
            }
            if (best != TN) {
                auto rd = [&](std::size_t off) {
                    return trk_buf[(best + off) % TN];
                };
                u8 trk = decode_44(rd(5), rd(6));
                u8 sec = decode_44(rd(7), rd(8));

                // Find D5 AA AD in the captured nibbles.
                std::size_t j = 0;
                bool found_data = false;
                while (j + 2 < N) {
                    if (write_nibbles_[j]   == 0xD5 &&
                        write_nibbles_[j+1] == 0xAA &&
                        write_nibbles_[j+2] == 0xAD) {
                        found_data = true;
                        break;
                    }
                    ++j;
                }
                if (found_data && j + 3 + 343 <= N) {
                    u8 sector_data[256];
                    if (decode_data_field(&write_nibbles_[j + 3], sector_data)) {
                        commit_sector(trk, sec, sector_data);
                    }
                }
            }
        }
    }

    write_nibbles_.clear();
    write_start_track_ = -1;

    if (any_written) save_image_to_file();
}

void DiskII::save_image_to_file() {
    if (image_path_.empty() || raw_image_.size() != 143360) return;
    std::ofstream f(image_path_, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(raw_image_.data()),
            static_cast<std::streamsize>(raw_image_.size()));
}

} // namespace apple1

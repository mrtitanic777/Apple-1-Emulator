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

// One nibble of disk-byte time at 1 MHz on a real Disk II is ~32us = 32
// cycles.  We use 32 here so DOS's timing loops see sensible spacing
// between successive byte reads, even though our model isn't truly
// rotational.
constexpr u64 kCyclesPerNibble = 32;

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
    //     packed in groups of 3 (input[i], input[i+86], input[i+172])
    //     into a single 6-bit value.
    //   * "sixes" array (256 bytes): the high 6 bits of every input byte.
    // Then XOR-chain each 6-bit value with the previous to produce the
    // values that get table-mapped into disk bytes.
    u8 twos[86]  = {0};
    u8 sixes[256];

    for (int i = 0; i < 256; ++i) {
        sixes[i] = static_cast<u8>((in[i] >> 2) & 0x3F);
    }
    // The 2-bit groups overlap as DOS expects: byte i of "twos" contains
    // bit-pairs from inputs i, i+86, i+172, with i+172 in the lowest
    // position (and the bits are also bit-swapped within each pair).
    auto rev2 = [](u8 v) -> u8 { return static_cast<u8>(((v & 1) << 1) | ((v & 2) >> 1)); };
    for (int i = 0; i < 86; ++i) {
        u8 b0 = rev2(static_cast<u8>(in[i]        & 0x03));
        u8 b1 = rev2(static_cast<u8>(in[i +  86]  & 0x03));
        u8 b2 = rev2(static_cast<u8>(in[i + 172]  & 0x03));
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

void DiskII::encode_all_tracks() {
    // Each track gets a leading gap of self-sync $FF nibbles, then 16
    // sectors back-to-back, then padding $FF's to fill out kNibblesPerTrack.
    for (int t = 0; t < 35; ++t) {
        std::vector<u8>& buf = tracks_nib_[t];
        buf.clear();
        buf.reserve(kNibblesPerTrack);

        // Gap 1: ~48 self-sync bytes at the top of the track.
        for (int i = 0; i < 48; ++i) buf.push_back(0xFF);

        // 16 sectors in skewed (physical) order.  The raw image is in
        // DOS-order, so when we lay down physical sector P we pull from
        // logical sector slot = skew^-1(P) in the image.  The kDos33Skew
        // table is already physical->logical, so:
        for (int phys = 0; phys < 16; ++phys) {
            int logical = kDos33Skew[phys];
            const u8* sector_data = &raw_image_[(t * 16 + logical) * 256];
            encode_sector(t, phys, sector_data, &buf);
        }

        // Pad to kNibblesPerTrack with self-sync $FF.
        while (buf.size() < kNibblesPerTrack) buf.push_back(0xFF);
        // If encode overshot (shouldn't with these gap sizes), trim.
        if (buf.size() > kNibblesPerTrack) buf.resize(kNibblesPerTrack);
    }
}

bool DiskII::mount_dsk(const std::string& path, std::string* err_out) {
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

    // Reset controller state - fresh mount = head at track 0, motor off.
    mounted_       = true;
    phase_pos_     = 0;
    for (bool& p : phase_state_) p = false;
    motor_on_      = false;
    active_drive_  = 1;
    q6_high_       = false;
    q7_high_       = false;
    nib_index_     = 0;
    data_reg_      = 0;
    last_seen_cyc_ = cpu_ ? cpu_->cycles() : 0;
    return true;
}

void DiskII::eject() {
    mounted_ = false;
    image_path_.clear();
    raw_image_.clear();
    for (auto& t : tracks_nib_) t.clear();
    motor_on_ = false;
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
    motor_on_ = on;
    // Reset the cycle baseline whenever the motor turns on so the elapsed
    // count starts from "now" rather than however many cycles passed while
    // the motor was off.
    if (on && cpu_) last_seen_cyc_ = cpu_->cycles();
}

void DiskII::touch_drive(int drive) {
    // Single-drive build: silently ignore the request to switch to drive 2.
    if (drive == 1) active_drive_ = 1;
}

void DiskII::touch_q6(bool high) { q6_high_ = high; }
void DiskII::touch_q7(bool high) { q7_high_ = high; }

void DiskII::update_data_reg() {
    if (!mounted_ || !motor_on_ || !cpu_) return;
    const int t = phase_pos_ / 2;
    if (t < 0 || t >= 35) return;
    const std::vector<u8>& buf = tracks_nib_[t];
    if (buf.empty()) return;

    const u64 now     = cpu_->cycles();
    const u64 elapsed = now - last_seen_cyc_;
    if (elapsed < kCyclesPerNibble) return;

    const u64 nibs = elapsed / kCyclesPerNibble;
    nib_index_     = (nib_index_ + static_cast<std::size_t>(nibs)) % buf.size();
    data_reg_      = buf[nib_index_];
    last_seen_cyc_ += nibs * kCyclesPerNibble;
}

u8 DiskII::read(u16 addr) {
    // Every $C00x access is a soft-switch first.  After dispatching the
    // switch, only the read-mode $C00C returns useful data; the rest read
    // back as the current data register (which is what real hardware does -
    // the floating bus reflects the latest latched value).
    write(addr, 0);   // shared toggle path

    if (!q6_high_ && !q7_high_) {     // read mode
        update_data_reg();
        return data_reg_;
    }
    // Other modes return the floating data register on real hardware too.
    return data_reg_;
}

void DiskII::write(u16 addr, u8 /*val*/) {
    // Read-only build: every soft-switch toggles; Q7H (write-mode) is
    // accepted as a mode change but no data is committed back to the image.
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

} // namespace apple1

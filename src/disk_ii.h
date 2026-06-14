// disk_ii.h - Disk II controller emulation (hardware only, no DOS/boot ROM).
//
// Soft switches at $C000-$C00F (as requested - this is the slot-0 mirror,
// not the standard slot-6 $C0E0-$C0EF placement).  When a .dsk image is
// mounted, this class takes over the $C000-$C0FF range from the ACI tape.
// When nothing is mounted, the Bus continues to route reads to the ACI tape
// flip-flop as before.
//
// Soft-switch map (each switch is one byte; reads and writes both toggle):
//   $C000 PHASE0 OFF    $C001 PHASE0 ON
//   $C002 PHASE1 OFF    $C003 PHASE1 ON
//   $C004 PHASE2 OFF    $C005 PHASE2 ON
//   $C006 PHASE3 OFF    $C007 PHASE3 ON
//   $C008 MOTOR OFF     $C009 MOTOR ON
//   $C00A DRIVE 1       $C00B DRIVE 2  (single-drive build: $C00B is no-op)
//   $C00C Q6L (shift)   $C00D Q6H (load) - mode select bit 0
//   $C00E Q7L (read)    $C00F Q7H (write)- mode select bit 1
//
// Read-mode (Q6L+Q7L): reads return the data register (shift register
// latching nibbles off the simulated rotating disk).  All other modes are
// no-ops in this read-only build.
//
// Track layout: a .dsk file is 143,360 bytes = 35 tracks * 16 sectors * 256.
// On mount we GCR-encode (DOS 3.3 6-and-2) each track into a 6656-byte
// nibble buffer.  Read mode advances through this buffer cyclically; one
// new nibble is latched every 32 CPU cycles (matches real Disk II byte
// time of ~32us at 1 MHz with 8-bit nibbles).
//
// Stepper: the four PHASE switches advance/retract the head one half-track
// at a time when the adjacent phase is energised next.  We collapse this
// into a half-track position 0..69 (track 0..34.5).  Quarter-track
// fractional positions land on the next whole track's nibble stream which
// is good enough for DOS RWTS - it always seeks to integer tracks.

#pragma once

#include "common.h"
#include <array>
#include <string>
#include <vector>

namespace apple1 {

class CPU6502;   // for cycle access

class DiskII {
public:
    DiskII() = default;

    // Wire up the CPU so the data register can pace nibble latching off the
    // running cycle counter.  Must be called once at construction time.
    void set_cpu(CPU6502* cpu) { cpu_ = cpu; }

    // Mount a .dsk image (DOS 3.3 sector order, exactly 143,360 bytes).
    // Returns true on success; on failure mounted_ stays false and the
    // bus will continue to route $C00x to the ACI tape.
    bool mount_dsk(const std::string& path, std::string* err_out);

    // Eject the current image.  After this, mounted() returns false and
    // $C00x reverts to ACI tape behaviour on the bus.
    void eject();

    bool mounted() const { return mounted_; }

    // Bus interface.  Both reads and writes act as soft-switch accesses;
    // only reads of $C00C in read-mode return meaningful data.
    u8   read(u16 addr);
    void write(u16 addr, u8 val);

    // Diagnostics for the debugger panel.
    int  track()      const { return phase_pos_ / 2; }       // 0..34
    int  half_track() const { return phase_pos_; }            // 0..69
    bool motor_on()   const { return motor_on_; }
    int  drive()      const { return active_drive_; }         // 1 (only)
    u8   data_reg()   const { return data_reg_; }
    const std::string& image_path() const { return image_path_; }

private:
    // The four soft-switch handlers.  All take addr (for the low bit) and
    // return nothing; the data register is read via data_reg_.
    void touch_phase(int phase, bool on);
    void touch_motor(bool on);
    void touch_drive(int drive);
    void touch_q6(bool high);
    void touch_q7(bool high);

    // Advance the data register by however many nibbles' worth of CPU
    // cycles have elapsed since last read.  Wraps in the current track's
    // nibble buffer.
    void update_data_reg();

    // GCR-encode all 35 tracks from raw_image_ into tracks_nib_.
    void encode_all_tracks();

    // Encode one 256-byte sector at sector slot s on track t into the
    // 343-byte GCR field at out (with prologue/epilogue framing).
    void encode_sector(int track, int sector, const u8* in,
                       std::vector<u8>* out);

    CPU6502* cpu_ = nullptr;

    bool        mounted_      = false;
    std::string image_path_;
    std::vector<u8> raw_image_;          // 143,360 bytes (35 * 16 * 256)

    // 35 tracks * 6656 nibbles per track.  Real Disk II tracks are ~6400-
    // 6656 nibbles at the standard rotation rate.
    static constexpr std::size_t kNibblesPerTrack = 6656;
    std::array<std::vector<u8>, 35> tracks_nib_;

    // Stepper / drive state.
    int   phase_pos_      = 0;       // half-tracks, 0..69
    bool  phase_state_[4] = { false, false, false, false };
    bool  motor_on_       = false;
    int   active_drive_   = 1;
    bool  q6_high_        = false;   // false = shift (read), true = load
    bool  q7_high_        = false;   // false = read, true = write

    // Data register state.  nib_index_ is the position within the current
    // track's nibble buffer; data_reg_ is the latest latched value.
    std::size_t nib_index_     = 0;
    u8          data_reg_      = 0;
    u64         last_seen_cyc_ = 0;
};

} // namespace apple1

// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// disk_ii.h - Disk II controller emulation (hardware only, no DOS/boot ROM).
//
// Soft switches at $C000-$C00F (as requested - this is the slot-0 mirror,
// not the standard slot-6 $C0E0-$C0EF placement).  Routed by the Bus when
// the IO Card is set to "Disk 1".
//
// Read path: on mount we GCR-encode the .dsk image into per-track nibble
// buffers; reads of $C00C in read-mode (Q6L+Q7L) return one nibble at a
// time, paced off the CPU cycle counter (~32 cyc/byte).
//
// Write path: while Q7H (write mode) is active, every value stored to
// $C00D is captured into a write-session buffer.  On mode-exit / motor-
// off / eject we scan that buffer for sector framing (D5 AA 96 address
// mark + 4-and-4 trk/sec, D5 AA AD data mark + 343 GCR bytes), denibble
// the data field back to raw bytes, splice it into raw_image_ at the
// right (track, sector) offset, re-encode that track, and write the
// updated .dsk back to disk.  This keeps the file in sync with what the
// guest OS thinks is on the platter.
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
// new nibble is latched every 32 CPU cycles.
//
// Timing: we model 1 CPU cycle = 1us (Apple-1 6502 clock).  The Disk II
// shifts a bit cell every 4us (= 4 cycles) and a full byte every 32us
// (= 32 cycles), which is what kCyclesPerNibble in the .cpp encodes.
//
// Stepper: the four PHASE switches advance/retract the head one half-track
// at a time when the adjacent phase is energised next.  We collapse this
// into a half-track position 0..69 (track 0..34.5).  Quarter-track
// fractional positions land on the next whole track's nibble stream which
// is good enough for DOS RWTS - it always seeks to integer tracks.

#pragma once

#include "common.h"
#include <array>
#include <atomic>
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

    // Switch latch model at runtime.  false = bit-level LS166 (default,
    // cycle-accurate); true = byte-level (deterministic across hosts -
    // delivers one whole nibble per 32 emulated cycles, no partial
    // shift register state).
    void set_byte_latch(bool on) { byte_latch_.store(on); }

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
    u8   data_reg()   const { return shift_reg_; }
    bool q6_high()    const { return q6_high_; }
    bool q7_high()    const { return q7_high_; }
    std::size_t nib_index() const { return nib_index_; }
    static constexpr std::size_t nibbles_per_track() { return kNibblesPerTrack; }

    // Atomic snapshot of the head's data-path state, taken at one
    // instant so the debugger's "7-byte stream + latch + bit index +
    // next-bit" view is self-consistent.  Reading the same fields via
    // separate accessors races with the CPU thread and can stitch
    // together impossible state (e.g. byte_bit_index=7 with shift_reg
    // = $00 even though 7 mid-byte shifts would necessarily leave a
    // nonzero register).
    struct HeadSnapshot {
        bool        mounted;
        int         track;
        std::size_t nib_index;
        u8          stream[7];      // bytes at delta -3..+3 around nib_index
        u8          shift_reg;      // live LS166 contents
        int         byte_bit_index; // 0..7, bits already shifted from stream[3]
        u8          next_bit;       // 0 or 1, what next tick will OR into LSB
    };
    HeadSnapshot snapshot_head() const;
    const std::string& image_path() const { return image_path_; }

    // Read-only access to the GCR-encoded buffer for one track.  Used by
    // the --debug nibble dump to inspect what the boot ROM will see.
    const std::vector<u8>& track_buffer(int t) const {
        static const std::vector<u8> empty;
        return (t >= 0 && t < 35) ? tracks_nib_[t] : empty;
    }

    // Where the head is right now (= the next byte that will be latched
    // on the upcoming $C00C read).  Same as nib_index_ in the
    // advance-per-read model.
    std::size_t head_position() const { return nib_index_; }

    // Total head time in microseconds (= CPU cycles, since 1 cycle = 1us).
    u64 head_micros() const;

private:
    // The four soft-switch handlers.  All take addr (for the low bit) and
    // return nothing; the data register is read via data_reg_.
    void touch_phase(int phase, bool on);
    void touch_motor(bool on);
    void touch_drive(int drive);
    void touch_q6(bool high);
    void touch_q7(bool high);

    // Address-only soft-switch dispatch.  Used by both read() (which
    // additionally returns the data register in read mode) and write()
    // (which additionally latches write data on $C00D in write mode).
    void soft_switch_dispatch(u16 addr);

    // Advance the shift register by however many BITS have arrived since
    // last call.  One disk bit clocks in every 4 CPU cycles; shift left
    // by 1, OR in the new bit at LSB (the Wozniak shift register: bit 7
    // is the OLDEST bit, bit 0 is the freshest).  A valid GCR data byte
    // (all encoded values $96..$FF have bit 7 set) becomes "ready" when
    // its leading 1 walks into bit 7 of the shift register.  Multiple
    // CPU reads of Q6L inside one 4-cycle window see the SAME register
    // value - there's no consume-on-read, the register just keeps
    // shifting at the disk rate.
    void update_shift_reg();

    // GCR-encode all 35 tracks from raw_image_ into tracks_nib_.
    void encode_all_tracks();

    // GCR-encode just one track from raw_image_ - used after a sector
    // writeback so subsequent reads see the new contents.
    void encode_track(int track);

    // Encode one 256-byte sector at sector slot s on track t into the
    // 343-byte GCR field at out (with prologue/epilogue framing).
    void encode_sector(int track, int sector, const u8* in,
                       std::vector<u8>* out);

    // Walk the captured write-session nibbles, denibble any complete
    // sectors found, splice them into raw_image_, re-encode the affected
    // tracks, and persist raw_image_ back to image_path_.
    void flush_write_session();

    // Decode one 343-byte GCR data field (86 twos + 256 sixes + chk) into
    // 256 raw bytes.  Returns false on bad nibble or checksum mismatch.
    static bool decode_data_field(const u8* gcr343, u8 out[256]);

    // Inverse of encode_44 - recover a byte from its two 4-and-4 disk
    // bytes (used for vol/trk/sec/chk in the address field).
    static u8   decode_44(u8 a, u8 b);

    // Persist raw_image_ to image_path_.  No-op if path is empty or
    // raw_image_ isn't the right size.
    void save_image_to_file();

    CPU6502* cpu_ = nullptr;

    volatile bool mounted_    = false;
    std::string image_path_;
    std::vector<u8> raw_image_;          // 143,360 bytes (35 * 16 * 256)

    // 35 tracks * 6656 nibbles per track.  Real Disk II tracks are ~6400-
    // 6656 nibbles at the standard rotation rate.
    static constexpr std::size_t kNibblesPerTrack = 6656;
    std::array<std::vector<u8>, 35> tracks_nib_;

    // Stepper / drive state.  Same volatile rationale as the data-path
    // fields below: CPU thread writes, UI/debugger thread reads unlocked.
    volatile int   phase_pos_      = 0;       // half-tracks, 0..69
    volatile bool  phase_state_[4] = { false, false, false, false };
    volatile bool  motor_on_       = false;
    volatile int   active_drive_   = 1;
    volatile bool  q6_high_        = false;
    volatile bool  q7_high_        = false;

    // Disk II data path - bit-level cycle-driven shift register
    // (Wozniak's LS166 + MC3470 model).
    //
    // The disk spins at ~300 RPM regardless of CPU activity: one new
    // bit clocks into the shift register every 4 CPU cycles (4 us at
    // 1 MHz).  Each tick: shift_reg_ = (shift_reg_ << 1) | next_bit.
    // The new bit lands at LSB (bit 0), the old MSB drops off the top.
    // A valid GCR data byte (encoded values $96..$FF) always has bit 7
    // set, so after 8 ticks shifting in such a byte, the shift register's
    // bit 7 = 1 and the BPL-polling CPU sees the byte as "ready".
    //
    // The CPU's standard polling loop
    //
    //     LOOP: LDA $C0EC
    //           BPL  LOOP
    //
    // sees bit 7 clear during assembly and bit 7 set when the byte
    // is complete.  Reads of Q6L return the current register; if
    // bit 7 was set, the register is then CLEARED so the next byte
    // starts assembling from bit 0.  That clear-on-bit-7-read keeps
    // byte boundaries clean across the whole track, including the
    // sync $FF runs at the top (each $FF is just a regular 8-bit
    // byte in the stream - no special multi-bit encoding).
    // Volatile: CPU thread mutates these inside update_shift_reg().  The
    // UI thread reads them through snapshot_head() unlocked.  Without
    // volatile, MSVC's whole-program optimizer can hoist loads of these
    // fields out of the catch-up loop on AMD targets (Intel happens to
    // keep them in memory because of different scheduling heuristics),
    // causing the loop to compute next_bit_cycle_/nib_index_ from stale
    // register copies and the disk read returns wrong data.
    volatile std::size_t nib_index_           = 0;
    volatile int         byte_bit_index_      = 0;
    volatile u8          shift_reg_           = 0;
    volatile u64         next_bit_cycle_      = 0;

    // Byte-level latch mode.  When true, update_shift_reg() ignores
    // bit-level state and just delivers buf[nib_index_] every 32
    // emulated cycles.  Set from settings via set_byte_latch().
    std::atomic<bool>    byte_latch_{false};

    // Write capture: STA $C00D in Q7H mode appends here.  Cleared on
    // session start (Q7L->Q7H transition) and consumed by flush.
    std::vector<u8> write_nibbles_;

    // Head position (nib_index_ on the currently-mounted track) at the
    // moment write capture started.  Real Disk II RWTS only writes the
    // DATA field of a sector - the address field stays put on disk and
    // RWTS spins the head until that addr field passes by, then flips
    // to write mode for the data field portion.  flush_write_session()
    // uses this snapshot to walk the encoded track backward from here
    // and find the most-recently-passed D5 AA 96 addr field, which
    // tells us which sector is being rewritten.
    int         write_start_track_     = -1;
    std::size_t write_start_nib_index_ = 0;
};

} // namespace apple1

// common.h - shared types, constants, and small utilities used across the
// emulator.  Single point of definition so we don't drift between modules.
//
// Apple-1 hardware constants:
//   CPU clock:   1.023 MHz
//   Display:     40 columns x 24 rows, character cells (single-char glyphs)
//   I/O via PIA: $D010-$D013 (KBD, KBDCR, DSP, DSPCR)
//   RAM:         $0000-$1FFF (8KB on-board)
//   Wozmon:      $FF00-$FFFF
//   ACI ROM:     $C100-$C1FF (with tape data port at $C000-$C0FF)
//   BASIC:       $E000-$EFFF (RAM, loaded with Integer BASIC at boot)

#pragma once

#include <cstdint>
#include <cstddef>

namespace apple1 {

// Type aliases - shorter and clearer than std::uintN_t for our use.
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

// Apple-1 hardware constants.
constexpr u32 kCpuHz       = 1023000;    // 1.023 MHz original clock
constexpr int kCols        = 40;
constexpr int kRows        = 24;

// Cassette format constants (frequencies derived from apple1js, validated
// against the real 1976 ACI ROM in the Python prototype).
constexpr u32 kCassetteSampleRate     = 44100;
constexpr u32 kCassetteLeaderCycles   = 592;    // CPU cycles per leader half-cycle
constexpr u32 kCassetteBit0Cycles     = 238;    // '0' bit half-cycle (HIGH freq)
constexpr u32 kCassetteBit1Cycles     = 473;    // '1' bit half-cycle (LOW freq)
constexpr u32 kCassetteSync1Cycles    = 180;
constexpr u32 kCassetteSync2Cycles    = 238;
constexpr u32 kCassetteLeaderCount    = 8192;   // leader transitions, matches apple1js
constexpr double kCassetteAmplitude   = 0.8;
constexpr double kCassetteTrailerSecs = 0.5;

} // namespace apple1

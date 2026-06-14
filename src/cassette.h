// cassette.h - Apple-1 ACI cassette tape format.
//
// Three operations:
//   1. Encode a byte buffer as an audio WAV file (real, playable audio that
//      sounds like 1976 cassette saves).
//   2. Decode that WAV back into bytes (the simple, fast path).
//   3. Convert a WAV into a stream of CPU-cycle deltas that the actual ACI
//      ROM at $C100 can read through bus port $C081 - the "real hardware"
//      validation path.
//
// Frequencies (verified against apple1js, which is verified against real
// Apple-1 hardware):
//   leader  ~864 Hz   (592 CPU cycles per half-cycle)
//   '0' bit ~2150 Hz  (238 CPU cycles per half-cycle, HIGH freq)
//   '1' bit ~1080 Hz  (473 CPU cycles per half-cycle, LOW freq)

#pragma once

#include "common.h"
#include "bus.h"
#include <string>
#include <vector>

namespace apple1::cassette {

// Encode bytes as a WAV file.  Returns duration in seconds.
double save_wav(const std::vector<u8>& data, const std::string& filepath);

// Decode a WAV (encoded by save_wav or apple1js) back into bytes.
std::vector<u8> load_wav(const std::string& filepath);

// Read a WAV and produce the list of CPU-cycle deltas describing when each
// audio zero-crossing should flip the tape input bit.  Feeding this to a
// Bus via load_tape() lets the real ACI ROM decode the WAV bit-by-bit.
std::vector<u32> wav_to_tape_transitions(const std::string& filepath);

// Combined helper: read a memory range from the bus and write it as WAV.
struct SaveResult { u32 byte_count; double duration_secs; };
SaveResult save_cassette(Bus& bus, const std::string& filepath,
                         u16 start_addr, u16 end_addr);

// Combined helper: load a WAV and stage it as tape input for the ACI.
// Returns the number of transitions queued.
std::size_t stage_cassette_for_aci(Bus& bus, const std::string& filepath);

} // namespace apple1::cassette

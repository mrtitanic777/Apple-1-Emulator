// fileio.h - the load_program() dispatcher.
// Auto-detects file format by extension and content:
//   .wav             -> decode as ACI cassette tape audio
//   .bin             -> raw bytes, load at $0300 by default,
//                       or "name_XXXX.bin" loads at $XXXX
//   text with ':'    -> Wozmon deposit format, e.g. "1000: A2 0 BD 13"

#pragma once

#include "bus.h"
#include <string>

namespace apple1::fileio {

// One-shot load.  Returns a human-readable status string for the user.
std::string load_program(Bus& bus, const std::string& filepath);

} // namespace apple1::fileio

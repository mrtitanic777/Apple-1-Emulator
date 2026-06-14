// disasm.h - 6502 disassembler for the debugger.
// Given a Bus and an address, returns the formatted instruction text and
// the number of bytes consumed.  All documented MOS 6502 opcodes are
// covered; unknown opcodes display as "??? $XX".

#pragma once

#include "common.h"
#include "bus.h"
#include <string>

namespace apple1::disasm {

struct Decoded {
    std::string text;   // e.g. "LDA #$42" or "JSR $FFEF"
    u8 length;          // 1, 2, or 3 bytes
};

// Decode the instruction at addr.  Uses bus.peek() so no I/O side effects.
Decoded decode(const Bus& bus, u16 addr);

} // namespace apple1::disasm

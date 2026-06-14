// disasm.cpp - 6502 disassembler.
//
// Per-opcode lookup table: mnemonic + addressing mode.  Addressing mode
// determines both how to format the operand and the instruction length.

#include "disasm.h"
#include <cstdio>

namespace apple1::disasm {

namespace {

enum class Mode : u8 {
    IMP,    // implied (1 byte)
    ACC,    // accumulator (1 byte)
    IMM,    // immediate #$nn (2 bytes)
    ZP,     // zero page $nn (2 bytes)
    ZPX,    // zero page,X (2 bytes)
    ZPY,    // zero page,Y (2 bytes)
    REL,    // relative branch (2 bytes)
    ABS,    // absolute $nnnn (3 bytes)
    ABSX,   // absolute,X (3 bytes)
    ABSY,   // absolute,Y (3 bytes)
    IND,    // indirect (3 bytes)
    INDX,   // (zp,X) (2 bytes)
    INDY,   // (zp),Y (2 bytes)
    UNK,    // unknown opcode
};

struct Entry { const char* mnemonic; Mode mode; };

constexpr Entry table[256] = {
    /* 0x00 */ {"BRK", Mode::IMP},  {"ORA", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ORA", Mode::ZP},   {"ASL", Mode::ZP},   {"???", Mode::UNK},
               {"PHP", Mode::IMP},  {"ORA", Mode::IMM},  {"ASL", Mode::ACC},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ORA", Mode::ABS},  {"ASL", Mode::ABS},  {"???", Mode::UNK},
    /* 0x10 */ {"BPL", Mode::REL},  {"ORA", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ORA", Mode::ZPX},  {"ASL", Mode::ZPX},  {"???", Mode::UNK},
               {"CLC", Mode::IMP},  {"ORA", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ORA", Mode::ABSX}, {"ASL", Mode::ABSX}, {"???", Mode::UNK},
    /* 0x20 */ {"JSR", Mode::ABS},  {"AND", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"BIT", Mode::ZP},   {"AND", Mode::ZP},   {"ROL", Mode::ZP},   {"???", Mode::UNK},
               {"PLP", Mode::IMP},  {"AND", Mode::IMM},  {"ROL", Mode::ACC},  {"???", Mode::UNK},
               {"BIT", Mode::ABS},  {"AND", Mode::ABS},  {"ROL", Mode::ABS},  {"???", Mode::UNK},
    /* 0x30 */ {"BMI", Mode::REL},  {"AND", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"AND", Mode::ZPX},  {"ROL", Mode::ZPX},  {"???", Mode::UNK},
               {"SEC", Mode::IMP},  {"AND", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"AND", Mode::ABSX}, {"ROL", Mode::ABSX}, {"???", Mode::UNK},
    /* 0x40 */ {"RTI", Mode::IMP},  {"EOR", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"EOR", Mode::ZP},   {"LSR", Mode::ZP},   {"???", Mode::UNK},
               {"PHA", Mode::IMP},  {"EOR", Mode::IMM},  {"LSR", Mode::ACC},  {"???", Mode::UNK},
               {"JMP", Mode::ABS},  {"EOR", Mode::ABS},  {"LSR", Mode::ABS},  {"???", Mode::UNK},
    /* 0x50 */ {"BVC", Mode::REL},  {"EOR", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"EOR", Mode::ZPX},  {"LSR", Mode::ZPX},  {"???", Mode::UNK},
               {"CLI", Mode::IMP},  {"EOR", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"EOR", Mode::ABSX}, {"LSR", Mode::ABSX}, {"???", Mode::UNK},
    /* 0x60 */ {"RTS", Mode::IMP},  {"ADC", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ADC", Mode::ZP},   {"ROR", Mode::ZP},   {"???", Mode::UNK},
               {"PLA", Mode::IMP},  {"ADC", Mode::IMM},  {"ROR", Mode::ACC},  {"???", Mode::UNK},
               {"JMP", Mode::IND},  {"ADC", Mode::ABS},  {"ROR", Mode::ABS},  {"???", Mode::UNK},
    /* 0x70 */ {"BVS", Mode::REL},  {"ADC", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ADC", Mode::ZPX},  {"ROR", Mode::ZPX},  {"???", Mode::UNK},
               {"SEI", Mode::IMP},  {"ADC", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"ADC", Mode::ABSX}, {"ROR", Mode::ABSX}, {"???", Mode::UNK},
    /* 0x80 */ {"???", Mode::UNK},  {"STA", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"STY", Mode::ZP},   {"STA", Mode::ZP},   {"STX", Mode::ZP},   {"???", Mode::UNK},
               {"DEY", Mode::IMP},  {"???", Mode::UNK},  {"TXA", Mode::IMP},  {"???", Mode::UNK},
               {"STY", Mode::ABS},  {"STA", Mode::ABS},  {"STX", Mode::ABS},  {"???", Mode::UNK},
    /* 0x90 */ {"BCC", Mode::REL},  {"STA", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"STY", Mode::ZPX},  {"STA", Mode::ZPX},  {"STX", Mode::ZPY},  {"???", Mode::UNK},
               {"TYA", Mode::IMP},  {"STA", Mode::ABSY}, {"TXS", Mode::IMP},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"STA", Mode::ABSX}, {"???", Mode::UNK},  {"???", Mode::UNK},
    /* 0xA0 */ {"LDY", Mode::IMM},  {"LDA", Mode::INDX}, {"LDX", Mode::IMM},  {"???", Mode::UNK},
               {"LDY", Mode::ZP},   {"LDA", Mode::ZP},   {"LDX", Mode::ZP},   {"???", Mode::UNK},
               {"TAY", Mode::IMP},  {"LDA", Mode::IMM},  {"TAX", Mode::IMP},  {"???", Mode::UNK},
               {"LDY", Mode::ABS},  {"LDA", Mode::ABS},  {"LDX", Mode::ABS},  {"???", Mode::UNK},
    /* 0xB0 */ {"BCS", Mode::REL},  {"LDA", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"LDY", Mode::ZPX},  {"LDA", Mode::ZPX},  {"LDX", Mode::ZPY},  {"???", Mode::UNK},
               {"CLV", Mode::IMP},  {"LDA", Mode::ABSY}, {"TSX", Mode::IMP},  {"???", Mode::UNK},
               {"LDY", Mode::ABSX}, {"LDA", Mode::ABSX}, {"LDX", Mode::ABSY}, {"???", Mode::UNK},
    /* 0xC0 */ {"CPY", Mode::IMM},  {"CMP", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"CPY", Mode::ZP},   {"CMP", Mode::ZP},   {"DEC", Mode::ZP},   {"???", Mode::UNK},
               {"INY", Mode::IMP},  {"CMP", Mode::IMM},  {"DEX", Mode::IMP},  {"???", Mode::UNK},
               {"CPY", Mode::ABS},  {"CMP", Mode::ABS},  {"DEC", Mode::ABS},  {"???", Mode::UNK},
    /* 0xD0 */ {"BNE", Mode::REL},  {"CMP", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"CMP", Mode::ZPX},  {"DEC", Mode::ZPX},  {"???", Mode::UNK},
               {"CLD", Mode::IMP},  {"CMP", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"CMP", Mode::ABSX}, {"DEC", Mode::ABSX}, {"???", Mode::UNK},
    /* 0xE0 */ {"CPX", Mode::IMM},  {"SBC", Mode::INDX}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"CPX", Mode::ZP},   {"SBC", Mode::ZP},   {"INC", Mode::ZP},   {"???", Mode::UNK},
               {"INX", Mode::IMP},  {"SBC", Mode::IMM},  {"NOP", Mode::IMP},  {"???", Mode::UNK},
               {"CPX", Mode::ABS},  {"SBC", Mode::ABS},  {"INC", Mode::ABS},  {"???", Mode::UNK},
    /* 0xF0 */ {"BEQ", Mode::REL},  {"SBC", Mode::INDY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"SBC", Mode::ZPX},  {"INC", Mode::ZPX},  {"???", Mode::UNK},
               {"SED", Mode::IMP},  {"SBC", Mode::ABSY}, {"???", Mode::UNK},  {"???", Mode::UNK},
               {"???", Mode::UNK},  {"SBC", Mode::ABSX}, {"INC", Mode::ABSX}, {"???", Mode::UNK},
};

// Length per mode (bytes including the opcode).
constexpr u8 mode_length(Mode m) {
    switch (m) {
        case Mode::IMP: case Mode::ACC: case Mode::UNK:   return 1;
        case Mode::IMM: case Mode::ZP:  case Mode::ZPX:
        case Mode::ZPY: case Mode::REL: case Mode::INDX:
        case Mode::INDY:                                  return 2;
        case Mode::ABS: case Mode::ABSX:
        case Mode::ABSY: case Mode::IND:                  return 3;
    }
    return 1;
}

std::string fmt(const char* mnem, Mode mode, u16 pc, u8 b1, u8 b2) {
    char buf[64];
    u16 abs_target = static_cast<u16>(b1 | (b2 << 8));
    switch (mode) {
        case Mode::IMP:
        case Mode::UNK:
            std::snprintf(buf, sizeof(buf), "%s", mnem);
            break;
        case Mode::ACC:
            std::snprintf(buf, sizeof(buf), "%s A", mnem);
            break;
        case Mode::IMM:
            std::snprintf(buf, sizeof(buf), "%s #$%02X", mnem, b1);
            break;
        case Mode::ZP:
            std::snprintf(buf, sizeof(buf), "%s $%02X", mnem, b1);
            break;
        case Mode::ZPX:
            std::snprintf(buf, sizeof(buf), "%s $%02X,X", mnem, b1);
            break;
        case Mode::ZPY:
            std::snprintf(buf, sizeof(buf), "%s $%02X,Y", mnem, b1);
            break;
        case Mode::REL: {
            // Show the absolute target address, not the relative offset.
            int8_t off = static_cast<int8_t>(b1);
            u16 target = static_cast<u16>(pc + 2 + off);
            std::snprintf(buf, sizeof(buf), "%s $%04X", mnem, target);
        } break;
        case Mode::ABS:
            std::snprintf(buf, sizeof(buf), "%s $%04X", mnem, abs_target);
            break;
        case Mode::ABSX:
            std::snprintf(buf, sizeof(buf), "%s $%04X,X", mnem, abs_target);
            break;
        case Mode::ABSY:
            std::snprintf(buf, sizeof(buf), "%s $%04X,Y", mnem, abs_target);
            break;
        case Mode::IND:
            std::snprintf(buf, sizeof(buf), "%s ($%04X)", mnem, abs_target);
            break;
        case Mode::INDX:
            std::snprintf(buf, sizeof(buf), "%s ($%02X,X)", mnem, b1);
            break;
        case Mode::INDY:
            std::snprintf(buf, sizeof(buf), "%s ($%02X),Y", mnem, b1);
            break;
    }
    return buf;
}

} // namespace

Decoded decode(const Bus& bus, u16 addr) {
    u8 op = bus.peek(addr);
    const Entry& e = table[op];
    u8 len = mode_length(e.mode);
    u8 b1 = (len > 1) ? bus.peek(static_cast<u16>(addr + 1)) : 0;
    u8 b2 = (len > 2) ? bus.peek(static_cast<u16>(addr + 2)) : 0;
    if (e.mode == Mode::UNK) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "??? $%02X", op);
        return { buf, 1 };
    }
    return { fmt(e.mnemonic, e.mode, addr, b1, b2), len };
}

} // namespace apple1::disasm

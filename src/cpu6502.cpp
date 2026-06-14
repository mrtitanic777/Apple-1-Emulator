// cpu6502.cpp - MOS 6502 CPU core implementation.
//
// The opcode dispatch is a giant switch statement; the compiler turns this
// into a jump table that's significantly faster than the Python equivalent's
// chained if/elif.  Behavior matches the Python prototype exactly so that
// the same ROMs and test programs work bit-for-bit.

#include "cpu6502.h"

namespace apple1 {

// Cycle cost table.  Default 2 cycles for unmapped slots (matches NOP).
const u8 CPU6502::kCycleTable[256] = {
    /* 0x00 */ 7,6,0,0,0,3,5,0, 3,2,2,0,0,4,6,0,
    /* 0x10 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
    /* 0x20 */ 6,6,0,0,3,3,5,0, 4,2,2,0,4,4,6,0,
    /* 0x30 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
    /* 0x40 */ 6,6,0,0,0,3,5,0, 3,2,2,0,3,4,6,0,
    /* 0x50 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
    /* 0x60 */ 6,6,0,0,0,3,5,0, 4,2,2,0,5,4,6,0,
    /* 0x70 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
    /* 0x80 */ 0,6,0,0,3,3,3,0, 2,0,2,0,4,4,4,0,
    /* 0x90 */ 2,6,0,0,4,4,4,0, 2,5,2,0,0,5,0,0,
    /* 0xA0 */ 2,6,2,0,3,3,3,0, 2,2,2,0,4,4,4,0,
    /* 0xB0 */ 2,5,0,0,4,4,4,0, 2,4,2,0,4,4,4,0,
    /* 0xC0 */ 2,6,0,0,3,3,5,0, 2,2,2,0,4,4,6,0,
    /* 0xD0 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
    /* 0xE0 */ 2,6,0,0,3,3,5,0, 2,2,2,0,4,4,6,0,
    /* 0xF0 */ 2,5,0,0,0,4,6,0, 2,4,0,0,0,4,7,0,
};

CPU6502::CPU6502(Bus& bus) : bus_(bus) {}

void CPU6502::reset() {
    a_ = x_ = y_ = 0;
    sp_ = 0xFD;
    status_ = U | I;
    pc_ = read16(0xFFFC);
}

u16 CPU6502::read16(u16 addr) {
    u8 lo = read(addr);
    u8 hi = read(addr + 1);
    return static_cast<u16>(hi) << 8 | lo;
}

void CPU6502::set_flag(u8 flag, bool on) {
    if (on) status_ |=  flag;
    else    status_ &= ~flag;
}

void CPU6502::set_zn(u8 v) {
    set_flag(Z, v == 0);
    set_flag(N, (v & 0x80) != 0);
}

void CPU6502::push(u8 v) {
    write(0x100 + sp_, v);
    sp_ = (sp_ - 1) & 0xFF;
}

u8 CPU6502::pop() {
    sp_ = (sp_ + 1) & 0xFF;
    return read(0x100 + sp_);
}

void CPU6502::push16(u16 v) {
    push((v >> 8) & 0xFF);
    push(v & 0xFF);
}

u16 CPU6502::pop16() {
    u8 lo = pop();
    u8 hi = pop();
    return static_cast<u16>(hi) << 8 | lo;
}

u16 CPU6502::addr_imm()  { u16 a = pc_; pc_ = (pc_ + 1) & 0xFFFF; return a; }
u16 CPU6502::addr_zp()   { u16 a = read(pc_); pc_ = (pc_ + 1) & 0xFFFF; return a; }
u16 CPU6502::addr_zpx()  { u16 a = (read(pc_) + x_) & 0xFF; pc_ = (pc_ + 1) & 0xFFFF; return a; }
u16 CPU6502::addr_zpy()  { u16 a = (read(pc_) + y_) & 0xFF; pc_ = (pc_ + 1) & 0xFFFF; return a; }

u16 CPU6502::addr_abs() {
    u16 a = read16(pc_);
    pc_ = (pc_ + 2) & 0xFFFF;
    return a;
}

u16 CPU6502::addr_absx() {
    u16 a = (read16(pc_) + x_) & 0xFFFF;
    pc_ = (pc_ + 2) & 0xFFFF;
    return a;
}

u16 CPU6502::addr_absy() {
    u16 a = (read16(pc_) + y_) & 0xFFFF;
    pc_ = (pc_ + 2) & 0xFFFF;
    return a;
}

u16 CPU6502::addr_ind() {
    // 6502 JMP indirect has a famous page-wrap bug: when the pointer
    // address ends in $FF, the high byte is read from $XX00 (same page)
    // instead of the next page.  We reproduce it exactly for fidelity.
    u16 ptr = read16(pc_);
    pc_ = (pc_ + 2) & 0xFFFF;
    u8  lo = read(ptr);
    u8  hi = read((ptr & 0xFF00) | ((ptr + 1) & 0xFF));
    return static_cast<u16>(hi) << 8 | lo;
}

u16 CPU6502::addr_indx() {
    u8 zp = (read(pc_) + x_) & 0xFF;
    pc_ = (pc_ + 1) & 0xFFFF;
    u8 lo = read(zp);
    u8 hi = read((zp + 1) & 0xFF);
    return static_cast<u16>(hi) << 8 | lo;
}

u16 CPU6502::addr_indy() {
    u8 zp = read(pc_);
    pc_ = (pc_ + 1) & 0xFFFF;
    u8 lo = read(zp);
    u8 hi = read((zp + 1) & 0xFF);
    return (static_cast<u16>(hi) << 8 | lo) + y_;
}

u16 CPU6502::addr_rel() {
    i64 off = read(pc_);
    pc_ = (pc_ + 1) & 0xFFFF;
    if (off & 0x80) off -= 0x100;
    return static_cast<u16>((pc_ + off) & 0xFFFF);
}

void CPU6502::branch(bool cond) {
    u16 target = addr_rel();
    if (cond) pc_ = target;
}

void CPU6502::op_adc(u8 m) {
    u16 a = a_;
    u16 c = get_flag(C) ? 1 : 0;
    u16 r = a + m + c;
    set_flag(C, r > 0xFF);
    // Two's-complement overflow: set when sign of result differs from sign
    // of both inputs (which agreed beforehand).
    set_flag(V, ((~(a ^ m) & (a ^ r)) & 0x80) != 0);
    a_ = static_cast<u8>(r & 0xFF);
    set_zn(a_);
}

void CPU6502::op_sbc(u8 m) {
    // 6502 SBC is just ADC with inverted operand (because of carry semantics).
    op_adc(m ^ 0xFF);
}

void CPU6502::op_cmp(u8 reg, u8 m) {
    u16 r = static_cast<u16>(reg) - m;
    set_flag(C, reg >= m);
    set_zn(static_cast<u8>(r & 0xFF));
}

void CPU6502::step() {
    u8 op = read(pc_);
    pc_ = (pc_ + 1) & 0xFFFF;

    switch (op) {
        // LDA
        case 0xA9: a_ = read(addr_imm());  set_zn(a_); break;
        case 0xA5: a_ = read(addr_zp());   set_zn(a_); break;
        case 0xB5: a_ = read(addr_zpx());  set_zn(a_); break;
        case 0xAD: a_ = read(addr_abs());  set_zn(a_); break;
        case 0xBD: a_ = read(addr_absx()); set_zn(a_); break;
        case 0xB9: a_ = read(addr_absy()); set_zn(a_); break;
        case 0xA1: a_ = read(addr_indx()); set_zn(a_); break;
        case 0xB1: a_ = read(addr_indy()); set_zn(a_); break;

        // LDX
        case 0xA2: x_ = read(addr_imm());  set_zn(x_); break;
        case 0xA6: x_ = read(addr_zp());   set_zn(x_); break;
        case 0xB6: x_ = read(addr_zpy());  set_zn(x_); break;
        case 0xAE: x_ = read(addr_abs());  set_zn(x_); break;
        case 0xBE: x_ = read(addr_absy()); set_zn(x_); break;

        // LDY
        case 0xA0: y_ = read(addr_imm());  set_zn(y_); break;
        case 0xA4: y_ = read(addr_zp());   set_zn(y_); break;
        case 0xB4: y_ = read(addr_zpx());  set_zn(y_); break;
        case 0xAC: y_ = read(addr_abs());  set_zn(y_); break;
        case 0xBC: y_ = read(addr_absx()); set_zn(y_); break;

        // STA
        case 0x85: write(addr_zp(),   a_); break;
        case 0x95: write(addr_zpx(),  a_); break;
        case 0x8D: write(addr_abs(),  a_); break;
        case 0x9D: write(addr_absx(), a_); break;
        case 0x99: write(addr_absy(), a_); break;
        case 0x81: write(addr_indx(), a_); break;
        case 0x91: write(addr_indy(), a_); break;

        // STX
        case 0x86: write(addr_zp(),  x_); break;
        case 0x96: write(addr_zpy(), x_); break;
        case 0x8E: write(addr_abs(), x_); break;

        // STY
        case 0x84: write(addr_zp(),  y_); break;
        case 0x94: write(addr_zpx(), y_); break;
        case 0x8C: write(addr_abs(), y_); break;

        // Register transfers
        case 0xAA: x_  = a_;  set_zn(x_); break;       // TAX
        case 0xA8: y_  = a_;  set_zn(y_); break;       // TAY
        case 0xBA: x_  = sp_; set_zn(x_); break;       // TSX
        case 0x8A: a_  = x_;  set_zn(a_); break;       // TXA
        case 0x9A: sp_ = x_;              break;       // TXS
        case 0x98: a_  = y_;  set_zn(a_); break;       // TYA

        // Stack
        case 0x48: push(a_); break;                                   // PHA
        case 0x68: a_ = pop(); set_zn(a_); break;                     // PLA
        case 0x08: push(status_ | B | U); break;                      // PHP
        case 0x28: status_ = (pop() & ~B) | U; break;                 // PLP

        // ADC
        case 0x69: op_adc(read(addr_imm()));  break;
        case 0x65: op_adc(read(addr_zp()));   break;
        case 0x75: op_adc(read(addr_zpx()));  break;
        case 0x6D: op_adc(read(addr_abs()));  break;
        case 0x7D: op_adc(read(addr_absx())); break;
        case 0x79: op_adc(read(addr_absy())); break;
        case 0x61: op_adc(read(addr_indx())); break;
        case 0x71: op_adc(read(addr_indy())); break;

        // SBC
        case 0xE9: op_sbc(read(addr_imm()));  break;
        case 0xE5: op_sbc(read(addr_zp()));   break;
        case 0xF5: op_sbc(read(addr_zpx()));  break;
        case 0xED: op_sbc(read(addr_abs()));  break;
        case 0xFD: op_sbc(read(addr_absx())); break;
        case 0xF9: op_sbc(read(addr_absy())); break;
        case 0xE1: op_sbc(read(addr_indx())); break;
        case 0xF1: op_sbc(read(addr_indy())); break;

        // AND
        case 0x29: a_ &= read(addr_imm());  set_zn(a_); break;
        case 0x25: a_ &= read(addr_zp());   set_zn(a_); break;
        case 0x35: a_ &= read(addr_zpx());  set_zn(a_); break;
        case 0x2D: a_ &= read(addr_abs());  set_zn(a_); break;
        case 0x3D: a_ &= read(addr_absx()); set_zn(a_); break;
        case 0x39: a_ &= read(addr_absy()); set_zn(a_); break;
        case 0x21: a_ &= read(addr_indx()); set_zn(a_); break;
        case 0x31: a_ &= read(addr_indy()); set_zn(a_); break;

        // ORA
        case 0x09: a_ |= read(addr_imm());  set_zn(a_); break;
        case 0x05: a_ |= read(addr_zp());   set_zn(a_); break;
        case 0x15: a_ |= read(addr_zpx());  set_zn(a_); break;
        case 0x0D: a_ |= read(addr_abs());  set_zn(a_); break;
        case 0x1D: a_ |= read(addr_absx()); set_zn(a_); break;
        case 0x19: a_ |= read(addr_absy()); set_zn(a_); break;
        case 0x01: a_ |= read(addr_indx()); set_zn(a_); break;
        case 0x11: a_ |= read(addr_indy()); set_zn(a_); break;

        // EOR
        case 0x49: a_ ^= read(addr_imm());  set_zn(a_); break;
        case 0x45: a_ ^= read(addr_zp());   set_zn(a_); break;
        case 0x55: a_ ^= read(addr_zpx());  set_zn(a_); break;
        case 0x4D: a_ ^= read(addr_abs());  set_zn(a_); break;
        case 0x5D: a_ ^= read(addr_absx()); set_zn(a_); break;
        case 0x59: a_ ^= read(addr_absy()); set_zn(a_); break;
        case 0x41: a_ ^= read(addr_indx()); set_zn(a_); break;
        case 0x51: a_ ^= read(addr_indy()); set_zn(a_); break;

        // CMP
        case 0xC9: op_cmp(a_, read(addr_imm()));  break;
        case 0xC5: op_cmp(a_, read(addr_zp()));   break;
        case 0xD5: op_cmp(a_, read(addr_zpx()));  break;
        case 0xCD: op_cmp(a_, read(addr_abs()));  break;
        case 0xDD: op_cmp(a_, read(addr_absx())); break;
        case 0xD9: op_cmp(a_, read(addr_absy())); break;
        case 0xC1: op_cmp(a_, read(addr_indx())); break;
        case 0xD1: op_cmp(a_, read(addr_indy())); break;

        // CPX
        case 0xE0: op_cmp(x_, read(addr_imm())); break;
        case 0xE4: op_cmp(x_, read(addr_zp()));  break;
        case 0xEC: op_cmp(x_, read(addr_abs())); break;

        // CPY
        case 0xC0: op_cmp(y_, read(addr_imm())); break;
        case 0xC4: op_cmp(y_, read(addr_zp()));  break;
        case 0xCC: op_cmp(y_, read(addr_abs())); break;

        // INC / DEC memory
        case 0xE6: { u16 a = addr_zp();   u8 v = read(a) + 1; write(a, v); set_zn(v); } break;
        case 0xF6: { u16 a = addr_zpx();  u8 v = read(a) + 1; write(a, v); set_zn(v); } break;
        case 0xEE: { u16 a = addr_abs();  u8 v = read(a) + 1; write(a, v); set_zn(v); } break;
        case 0xFE: { u16 a = addr_absx(); u8 v = read(a) + 1; write(a, v); set_zn(v); } break;

        case 0xC6: { u16 a = addr_zp();   u8 v = read(a) - 1; write(a, v); set_zn(v); } break;
        case 0xD6: { u16 a = addr_zpx();  u8 v = read(a) - 1; write(a, v); set_zn(v); } break;
        case 0xCE: { u16 a = addr_abs();  u8 v = read(a) - 1; write(a, v); set_zn(v); } break;
        case 0xDE: { u16 a = addr_absx(); u8 v = read(a) - 1; write(a, v); set_zn(v); } break;

        case 0xE8: x_ = (x_ + 1) & 0xFF; set_zn(x_); break;   // INX
        case 0xC8: y_ = (y_ + 1) & 0xFF; set_zn(y_); break;   // INY
        case 0xCA: x_ = (x_ - 1) & 0xFF; set_zn(x_); break;   // DEX
        case 0x88: y_ = (y_ - 1) & 0xFF; set_zn(y_); break;   // DEY

        // ASL accumulator + memory
        case 0x0A:
            set_flag(C, (a_ & 0x80) != 0);
            a_ = (a_ << 1) & 0xFF; set_zn(a_);
            break;
        case 0x06: case 0x16: case 0x0E: case 0x1E: {
            u16 a = (op == 0x06) ? addr_zp()
                  : (op == 0x16) ? addr_zpx()
                  : (op == 0x0E) ? addr_abs()
                  :                addr_absx();
            u8 v = read(a);
            set_flag(C, (v & 0x80) != 0);
            v = (v << 1) & 0xFF;
            write(a, v); set_zn(v);
        } break;

        // LSR accumulator + memory
        case 0x4A:
            set_flag(C, (a_ & 0x01) != 0);
            a_ >>= 1; set_zn(a_);
            break;
        case 0x46: case 0x56: case 0x4E: case 0x5E: {
            u16 a = (op == 0x46) ? addr_zp()
                  : (op == 0x56) ? addr_zpx()
                  : (op == 0x4E) ? addr_abs()
                  :                addr_absx();
            u8 v = read(a);
            set_flag(C, (v & 0x01) != 0);
            v >>= 1;
            write(a, v); set_zn(v);
        } break;

        // ROL accumulator + memory
        case 0x2A: {
            u8 c = get_flag(C) ? 1 : 0;
            set_flag(C, (a_ & 0x80) != 0);
            a_ = static_cast<u8>(((a_ << 1) | c) & 0xFF);
            set_zn(a_);
        } break;
        case 0x26: case 0x36: case 0x2E: case 0x3E: {
            u16 a = (op == 0x26) ? addr_zp()
                  : (op == 0x36) ? addr_zpx()
                  : (op == 0x2E) ? addr_abs()
                  :                addr_absx();
            u8 v = read(a);
            u8 c = get_flag(C) ? 1 : 0;
            set_flag(C, (v & 0x80) != 0);
            v = static_cast<u8>(((v << 1) | c) & 0xFF);
            write(a, v); set_zn(v);
        } break;

        // ROR accumulator + memory
        case 0x6A: {
            u8 c = get_flag(C) ? 1 : 0;
            set_flag(C, (a_ & 0x01) != 0);
            a_ = static_cast<u8>((a_ >> 1) | (c << 7));
            set_zn(a_);
        } break;
        case 0x66: case 0x76: case 0x6E: case 0x7E: {
            u16 a = (op == 0x66) ? addr_zp()
                  : (op == 0x76) ? addr_zpx()
                  : (op == 0x6E) ? addr_abs()
                  :                addr_absx();
            u8 v = read(a);
            u8 c = get_flag(C) ? 1 : 0;
            set_flag(C, (v & 0x01) != 0);
            v = static_cast<u8>((v >> 1) | (c << 7));
            write(a, v); set_zn(v);
        } break;

        // BIT - tests bits without modifying A; copies bits 6,7 of M to V,N
        case 0x24: {
            u8 v = read(addr_zp());
            set_flag(Z, (a_ & v) == 0);
            set_flag(V, (v & 0x40) != 0);
            set_flag(N, (v & 0x80) != 0);
        } break;
        case 0x2C: {
            u8 v = read(addr_abs());
            set_flag(Z, (a_ & v) == 0);
            set_flag(V, (v & 0x40) != 0);
            set_flag(N, (v & 0x80) != 0);
        } break;

        // Branches
        case 0x10: branch(!get_flag(N)); break;   // BPL
        case 0x30: branch( get_flag(N)); break;   // BMI
        case 0x50: branch(!get_flag(V)); break;   // BVC
        case 0x70: branch( get_flag(V)); break;   // BVS
        case 0x90: branch(!get_flag(C)); break;   // BCC
        case 0xB0: branch( get_flag(C)); break;   // BCS
        case 0xD0: branch(!get_flag(Z)); break;   // BNE
        case 0xF0: branch( get_flag(Z)); break;   // BEQ

        // Jumps & subroutines
        case 0x4C: pc_ = addr_abs(); break;        // JMP abs
        case 0x6C: pc_ = addr_ind(); break;        // JMP ind (page-wrap bug)
        case 0x20: {                               // JSR abs
            u16 target = addr_abs();
            // 6502 pushes (return address - 1), so RTS adds 1 back.
            push16((pc_ - 1) & 0xFFFF);
            pc_ = target;
        } break;
        case 0x60: pc_ = (pop16() + 1) & 0xFFFF; break;  // RTS
        case 0x40: {                               // RTI
            status_ = (pop() & ~B) | U;
            pc_ = pop16();
        } break;

        // Flag ops
        case 0x18: set_flag(C, false); break;     // CLC
        case 0x38: set_flag(C, true);  break;     // SEC
        case 0x58: set_flag(I, false); break;     // CLI
        case 0x78: set_flag(I, true);  break;     // SEI
        case 0xB8: set_flag(V, false); break;     // CLV
        case 0xD8: set_flag(D, false); break;     // CLD
        case 0xF8: set_flag(D, true);  break;     // SED

        // BRK
        case 0x00: {
            pc_ = (pc_ + 1) & 0xFFFF;
            push16(pc_);
            push(status_ | B | U);
            set_flag(I, true);
            pc_ = read16(0xFFFE);
        } break;

        // NOP - and the silent default for anything we missed.
        case 0xEA:
        default:
            break;
    }

    // Advance cycle counter using the table.  Slots we left at 0 (some
    // unofficial opcodes the Apple-1 ROMs never emit) get the NOP cost.
    u8 cyc = kCycleTable[op];
    cycles_ += cyc ? cyc : 2;
}

} // namespace apple1

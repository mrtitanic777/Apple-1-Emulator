// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// cpu6502.h - MOS 6502 CPU core.  Cycle-accurate for the timing-sensitive
// instructions that the ACI ROM uses to decode tape; documented opcodes
// only (no unofficial/illegal instructions; the Apple-1 firmware never uses
// them).  No BCD math support since the Apple-1 ROMs don't need it.

#pragma once

#include "common.h"
#include "bus.h"

namespace apple1 {

class CPU6502 {
public:
    // Status flag bits.
    static constexpr u8 C = 0x01;  // carry
    static constexpr u8 Z = 0x02;  // zero
    static constexpr u8 I = 0x04;  // interrupt disable
    static constexpr u8 D = 0x08;  // decimal (unused on Apple-1)
    static constexpr u8 B = 0x10;  // break
    static constexpr u8 U = 0x20;  // unused (always 1 when pushed)
    static constexpr u8 V = 0x40;  // overflow
    static constexpr u8 N = 0x80;  // negative

    explicit CPU6502(Bus& bus);

    // Execute one instruction; advances PC and cycle count.
    void step();

    // Hardware reset - re-vector from $FFFC, clear stack to $FD, set I flag.
    void reset();

    // Direct register access (for the debugger, savestates, F1 RESET).
    u8  a()      const { return a_; }
    u8  x()      const { return x_; }
    u8  y()      const { return y_; }
    u8  sp()     const { return sp_; }
    u16 pc()     const { return pc_; }
    u8  status() const { return status_; }
    u64 cycles() const { return cycles_; }

    void set_pc(u16 v)     { pc_ = v; }
    void set_sp(u8 v)      { sp_ = v; }
    void set_a(u8 v)       { a_ = v; }
    void set_x(u8 v)       { x_ = v; }
    void set_y(u8 v)       { y_ = v; }
    void set_status(u8 v)  { status_ = v; }

private:
    // Memory helpers (delegate to bus).
    u8   read(u16 addr)            { return bus_.read(addr); }
    void write(u16 addr, u8 val)   { bus_.write(addr, val); }
    u16  read16(u16 addr);

    // Flag helpers.
    void set_flag(u8 flag, bool on);
    bool get_flag(u8 flag) const   { return (status_ & flag) != 0; }
    void set_zn(u8 v);

    // Stack helpers.
    void push(u8 v);
    u8   pop();
    void push16(u16 v);
    u16  pop16();

    // Addressing modes - return effective address, advance PC past operand.
    u16 addr_imm();
    u16 addr_zp();
    u16 addr_zpx();
    u16 addr_zpy();
    u16 addr_abs();
    u16 addr_absx();
    u16 addr_absy();
    u16 addr_ind();         // 6502 JMP indirect with page-wrap bug
    u16 addr_indx();        // (zp,X)
    u16 addr_indy();        // (zp),Y
    u16 addr_rel();         // relative branch target

    // Common branch dispatch.
    void branch(bool cond);

    // ALU helpers.
    void op_adc(u8 m);
    void op_sbc(u8 m);
    void op_cmp(u8 reg, u8 m);

    // 6502 cycle cost per opcode.  Page-crossing and branch-taken penalties
    // are ignored (we use base cycle count).  Index by opcode.
    static const u8 kCycleTable[256];

    Bus& bus_;
    u8   a_      = 0;
    u8   x_      = 0;
    u8   y_      = 0;
    u8   sp_     = 0xFD;
    u16  pc_     = 0;
    u8   status_ = U | I;
    volatile u64 cycles_ = 0;
};

} // namespace apple1

// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// debugger_renderer.cpp

#include "debugger_renderer.h"
#include "disasm.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace apple1::win {

namespace {
const wchar_t* kFont  = L"Cascadia Mono";
const wchar_t* kFontFallback = L"Consolas";
constexpr float kFontSize = 14.0f;
constexpr float kPadX     = 12.0f;
constexpr float kPadY     = 10.0f;
}

bool DebuggerRenderer::attach(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc;
    GetClientRect(hwnd, &rc);
    client_w_ = rc.right - rc.left;
    client_h_ = rc.bottom - rc.top;

    auto make_format = [&](IDWriteTextFormat** out, DWRITE_FONT_WEIGHT w) {
        HRESULT hr = dwrite_factory()->CreateTextFormat(
            kFont, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"en-us", out);
        if (FAILED(hr)) {
            hr = dwrite_factory()->CreateTextFormat(
                kFontFallback, nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"en-us", out);
        }
        return SUCCEEDED(hr);
    };

    IDWriteTextFormat* tf = nullptr;
    if (!make_format(&tf, DWRITE_FONT_WEIGHT_REGULAR)) return false;
    text_format_.release();  text_format_.p = tf;
    text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    if (!make_format(&tf, DWRITE_FONT_WEIGHT_BOLD)) return false;
    bold_format_.release();  bold_format_.p = tf;
    bold_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    bold_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    line_h_ = kFontSize * 1.35f;

    return ensure_device_resources();
}

void DebuggerRenderer::on_resize(int w, int h) {
    client_w_ = w;
    client_h_ = h;
    if (target_) {
        target_->Resize(D2D1::SizeU(static_cast<UINT32>(w),
                                    static_cast<UINT32>(h)));
    }
}

bool DebuggerRenderer::ensure_device_resources() {
    if (target_) return true;
    if (!hwnd_)  return false;

    HRESULT hr = d2d_factory()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(static_cast<UINT32>(client_w_),
                               static_cast<UINT32>(client_h_))),
        target_.addr());
    if (FAILED(hr)) return false;

    // White on black like the main window.
    target_->CreateSolidColorBrush(
        D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f), brush_text_.addr());
    target_->CreateSolidColorBrush(
        D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f), brush_dim_.addr());
    target_->CreateSolidColorBrush(
        D2D1::ColorF(0.95f, 0.65f, 0.20f, 1.0f), brush_accent_.addr());
    return brush_text_ && brush_dim_ && brush_accent_;
}

void DebuggerRenderer::release_device_resources() {
    brush_text_.release();
    brush_dim_.release();
    brush_accent_.release();
    target_.release();
}

void DebuggerRenderer::draw_text(const wchar_t* s, float x, float y) {
    if (!s || !*s) return;
    D2D1_RECT_F rect = D2D1::RectF(x, y, x + client_w_, y + line_h_);
    target_->DrawTextW(s, static_cast<UINT32>(wcslen(s)),
                       text_format_.get(), rect, brush_text_.get(),
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
}

float DebuggerRenderer::draw_button(DebugButton id, const wchar_t* label,
                                    float x, float y, float w, bool active) {
    constexpr float h   = 22.0f;
    constexpr float gap = 6.0f;
    D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);

    auto* outline = active ? brush_accent_.get() : brush_dim_.get();
    target_->DrawRectangle(r, outline, active ? 1.6f : 1.0f);

    // Centred label.  We size a one-line rect at the button's vertical
    // midline; D2D handles the horizontal centering via DWRITE alignment.
    auto* fmt = active ? bold_format_.get() : text_format_.get();
    DWRITE_TEXT_ALIGNMENT old_align = fmt->GetTextAlignment();
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    D2D1_RECT_F text_rect = D2D1::RectF(x, y + (h - line_h_) * 0.5f,
                                        x + w, y + h);
    target_->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmt,
                       text_rect,
                       active ? brush_accent_.get() : brush_text_.get(),
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
    fmt->SetTextAlignment(old_align);

    buttons_.push_back({ id, r });
    return x + w + gap;
}

DebugButton DebuggerRenderer::hit_test(int x, int y) const {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    for (const auto& b : buttons_) {
        if (fx >= b.rect.left && fx <= b.rect.right &&
            fy >= b.rect.top  && fy <= b.rect.bottom) {
            return b.id;
        }
    }
    return DebugButton::None;
}

// Convert UTF-8 ASCII string to wide.  We only ever pass ASCII so this is
// a straight widening.
static std::wstring widen(const char* s) {
    std::wstring out;
    while (*s) out.push_back(static_cast<wchar_t>(*s++));
    return out;
}

void DebuggerRenderer::render(const CPU6502& cpu, const Bus& bus,
                              const Debugger& dbg) {
    if (!ensure_device_resources()) return;

    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0.04f, 0.04f, 0.04f, 1.0f));

    // Reset hit regions; draw_button() repopulates as it lays out.
    buttons_.clear();

    char line[128];
    float y = kPadY;
    auto next_line = [&]() { y += line_h_; };
    auto draw_line = [&](const char* s) {
        std::wstring w = widen(s);
        draw_text(w.c_str(), kPadX, y);
        next_line();
    };
    auto draw_accent = [&](const char* s) {
        std::wstring w = widen(s);
        D2D1_RECT_F rect = D2D1::RectF(kPadX, y, kPadX + client_w_, y + line_h_);
        target_->DrawTextW(w.c_str(), static_cast<UINT32>(w.size()),
                           bold_format_.get(), rect,
                           brush_accent_.get(),
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
        next_line();
    };

    // Status banner
    std::snprintf(line, sizeof(line), "  STATUS:  %s",
                  dbg.is_paused() ? "PAUSED" : "RUNNING");
    draw_accent(line);

    // Run-mode buttons.  The active mode highlights in the accent colour.
    // STEP NOW is always available - in Step mode it advances one
    // instruction; outside Step mode it still single-steps (then leaves
    // the prior mode's intent in place).
    RunMode mode = dbg.current_mode();
    float bx = kPadX;
    bx = draw_button(DebugButton::ModeFree,   L"FREE",   bx, y,  72.0f,
                     mode == RunMode::Free);
    bx = draw_button(DebugButton::ModeStep,   L"PAUSE",  bx, y,  72.0f,
                     mode == RunMode::Step);
    bx = draw_button(DebugButton::ModeRunRTS, L"TO RTS", bx, y,  72.0f,
                     mode == RunMode::RunToRTS);
    bx += 12.0f;       // visual gap between mode group and action button
    // STEP advances exactly one CPU instruction every click, regardless
    // of the current mode (it pauses on entry if not already paused).
    bx = draw_button(DebugButton::StepNow, L"STEP", bx, y, 88.0f, false);
    bx += 12.0f;
    // ADD BP opens the Breakpoints modal (Add/Disable/Enable/Delete).
    draw_button(DebugButton::AddBreakpoint, L"ADD BP...", bx, y, 96.0f, false);
    y += 22.0f + 4.0f;
    next_line();

    // Registers
    std::snprintf(line, sizeof(line),
                  "  PC: $%04X    A: $%02X   X: $%02X   Y: $%02X",
                  cpu.pc(), cpu.a(), cpu.x(), cpu.y());
    draw_line(line);
    std::snprintf(line, sizeof(line),
                  "  SP: $%02X      CYC: %llu",
                  cpu.sp(),
                  static_cast<unsigned long long>(cpu.cycles()));
    draw_line(line);

    u8 s = cpu.status();
    auto fl = [&](u8 mask, char on) { return (s & mask) ? on : '.'; };
    std::snprintf(line, sizeof(line),
                  "  FLAGS:  %c%c-%c%c%c%c%c",
                  fl(CPU6502::N, 'N'), fl(CPU6502::V, 'V'),
                  fl(CPU6502::B, 'B'), fl(CPU6502::D, 'D'),
                  fl(CPU6502::I, 'I'), fl(CPU6502::Z, 'Z'),
                  fl(CPU6502::C, 'C'));
    draw_line(line);
    next_line();

    // Disassembly
    draw_accent("  DISASSEMBLY");
    u16 addr = cpu.pc();
    for (int i = 0; i < 8; ++i) {
        auto d = disasm::decode(bus, addr);
        const char* marker = (i == 0) ? ">" : " ";
        const char* bp     = dbg.has_breakpoint(addr) ? "*" : " ";
        std::snprintf(line, sizeof(line), "  %s%s $%04X  %s",
                      marker, bp, addr, d.text.c_str());
        draw_line(line);
        addr = static_cast<u16>(addr + d.length);
    }
    next_line();

    // Memory inspector
    u16 base = dbg.memory_view_addr();
    std::snprintf(line, sizeof(line), "  MEMORY $%04X", base);
    draw_accent(line);
    for (int i = 0; i < 4; ++i) {
        u16 a = static_cast<u16>(base + i * 8);
        char buf[80];
        int n = std::snprintf(buf, sizeof(buf), "  $%04X:", a);
        for (int j = 0; j < 8; ++j) {
            u8 v = bus.peek(static_cast<u16>(a + j));
            n += std::snprintf(buf + n, sizeof(buf) - n, " %02X", v);
        }
        draw_line(buf);
    }
    next_line();

    // Tape diagnostics (critical for the ACI bug)
    draw_accent("  TAPE");
    std::snprintf(line, sizeof(line),
                  "  TRANSITIONS:  %llu / %llu",
                  static_cast<unsigned long long>(bus.tape_index()),
                  static_cast<unsigned long long>(bus.tape_total()));
    draw_line(line);
    std::snprintf(line, sizeof(line),
                  "  FLIPS:        %llu",
                  static_cast<unsigned long long>(bus.tape_flips()));
    draw_line(line);
    std::snprintf(line, sizeof(line),
                  "  $C0xx READS:  %llu",
                  static_cast<unsigned long long>(bus.tape_reads()));
    draw_line(line);
    next_line();

    // Disk II diagnostics - shows the latched nibble at $C00C (Q6L) the
    // CPU saw at its most recent read.  Refreshes ~30Hz so a running
    // boot loop visibly cycles through the GCR stream.
    draw_accent("  DISK");
    const auto& disk = bus.disk();
    if (disk.mounted()) {
        // Single atomic snapshot - the CPU thread is racing alongside,
        // and reading the fields one at a time produces stitched-up
        // impossible state (e.g. byte_bit_index=7 with shift_reg=$00).
        auto s = disk.snapshot_head();
        std::snprintf(line, sizeof(line),
                      "  STREAM:  %02X %02X %02X [%02X] %02X %02X %02X",
                      s.stream[0], s.stream[1], s.stream[2], s.stream[3],
                      s.stream[4], s.stream[5], s.stream[6]);
        draw_line(line);
        std::snprintf(line, sizeof(line),
                      "  LATCH:   $%02X   bit %d/8 of source   next-bit:[%u]",
                      s.shift_reg, s.byte_bit_index,
                      static_cast<unsigned>(s.next_bit));
        draw_line(line);
    }
    if (!disk.mounted()) {
        draw_line("  STATUS:       (no image mounted)");
    } else {
        std::snprintf(line, sizeof(line),
                      "  STATUS:       MOUNTED   MOTOR: %s   DRIVE: %d",
                      disk.motor_on() ? "ON " : "OFF", disk.drive());
        draw_line(line);
        std::snprintf(line, sizeof(line),
                      "  TRACK:        %2d        HALF: %2d",
                      disk.track(), disk.half_track());
        draw_line(line);
        const char* disk_mode = !disk.q7_high()
                                ? (disk.q6_high() ? "READ-LOAD"  : "READ-SHIFT")
                                : (disk.q6_high() ? "WRITE-LOAD" : "WRITE-SHIFT");
        std::snprintf(line, sizeof(line),
                      "  MODE:         %s",
                      disk_mode);
        draw_line(line);
        // Advance-per-read model: data_reg_ holds the byte the CPU
        // most-recently consumed from $C00C, and the head is sitting on
        // the byte that the NEXT read will return.
        u8   nib = disk.data_reg();
        char ch  = (nib >= 0x20 && nib < 0x7F) ? static_cast<char>(nib) : '.';
        std::snprintf(line, sizeof(line),
                      "  LATCH:        $%02X  '%c'  (bin %c%c%c%c%c%c%c%c)",
                      nib, ch,
                      (nib & 0x80) ? '1':'0', (nib & 0x40) ? '1':'0',
                      (nib & 0x20) ? '1':'0', (nib & 0x10) ? '1':'0',
                      (nib & 0x08) ? '1':'0', (nib & 0x04) ? '1':'0',
                      (nib & 0x02) ? '1':'0', (nib & 0x01) ? '1':'0');
        draw_line(line);
        std::snprintf(line, sizeof(line),
                      "  HEAD:         %zu / %zu  (next byte to be read)",
                      disk.head_position(),
                      apple1::DiskII::nibbles_per_track());
        draw_line(line);
        std::snprintf(line, sizeof(line),
                      "  DISK TIME:    %llu us",
                      static_cast<unsigned long long>(disk.head_micros()));
        draw_line(line);
    }

    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) release_device_resources();
}

} // namespace apple1::win

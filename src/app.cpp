// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// app.cpp - app state + CPU thread.

#include "app.h"
#include "disasm.h"
#include "fileio.h"
#include "cassette.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace apple1 {

namespace {
std::unique_ptr<App> g_app;
}

App& app()        { return *g_app; }
void create_app(const std::string& settings_path,
                const std::string& roms_dir_override,
                const std::string& debug_log_path) {
    g_app = std::make_unique<App>(settings_path, roms_dir_override,
                                  debug_log_path);
}
void destroy_app(){ g_app.reset(); }

App::App(const std::string& settings_path,
         const std::string& roms_dir_override,
         const std::string& debug_log_path) {
    debug_log_path_ = debug_log_path;
    if (!debug_log_path.empty()) {
        debug_log_.open(debug_log_path, std::ios::out | std::ios::trunc);
        if (debug_log_) {
            debug_log_ << "; Apple-1 emulator debug trace.\n"
                          "; Logged: every instruction whose PC is in "
                          "$C100..$C1FF.\n"
                          "; Columns:\n"
                          ";   PC      bytes     disassembly       "
                          "A  X  Y  SP F=NV-BDIZC  CYC\n";
            debug_log_.flush();
        }
    }

    if (!settings_path.empty()) settings_.set_path(settings_path);
    settings_.load();

    // Load ROMs.
    std::string dir = roms_dir_override.empty()
                    ? roms::locate_rom_directory()
                    : roms_dir_override;
    rom_set_ = roms::load_from_directory(dir);

    bus_ = std::make_unique<Bus>(rom_set_);
    bus_->set_ram_expansion(settings_.ram_expansion());
    bus_->set_io_card(settings_.io_card());
    bus_->disk().set_byte_latch(settings_.disk_latch() == DiskLatch::Byte);
    cpu_ = std::make_unique<CPU6502>(*bus_);
    bus_->set_cpu(cpu_.get());
    bus_->set_display_callback([this](u8 ch) { display_.putc(ch); });
    bus_->set_display_wait_callback([this]() { display_.wait_one_frame(); });

    display_.set_pacing(settings_.teletype_pacing());

    reset_cpu();

    // Real-Apple-1 power-on behavior: the 1K x 7-bit dynamic shift
    // register that holds the video data wakes up full of garbage and
    // STAYS that way until the CLEAR SCREEN button is pressed.  The CPU
    // is paused too because nothing useful can run while the operator
    // hasn't asked for it yet (in our case, until they press F2 RESET).
    debugger_.toggle_pause();   // -> paused
    display_.set_cursor_on(false);
    display_.fill_garbage();
    display_.set_boot_mode(true);

    // Auto-mount the last .dsk the user used, but only if the active
    // card is willing to use it (the cassette card has no disk port).
    // If the file is gone we silently skip - no error popup since this
    // is happening before the user has seen anything.
    if (settings_.io_card() != IoCard::Cassette) {
        std::string last = settings_.last_disk();
        if (!last.empty()) {
            std::string err;
            if (!bus_->disk().mount_dsk(last, &err)) {
                // Forget the path so we don't keep failing on every launch.
                settings_.set_last_disk("");
            }
        }
    }

    start();
}

App::~App() { stop(); }

void App::start() {
    shutdown_.store(false);
    cpu_thread_ = std::thread(&App::cpu_loop, this);
}

void App::stop() {
    shutdown_.store(true);
    if (cpu_thread_.joinable()) cpu_thread_.join();
}

void App::reset_cpu() {
    bool was_paused = debugger_.is_paused();
    if (!was_paused) debugger_.toggle_pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    cpu_->set_pc(0xFF00);
    cpu_->set_sp(0xFF);
    cpu_->set_status(CPU6502::U);
    bus_->reset_tape_state();
    bus_->reset_disk_request_state();
    bus_->reset_pia();

    debugger_.toggle_pause();
}

void App::clear_screen() {
    // The real Apple-1 CLEAR SCREEN button gated the video shift register's
    // data-in line, so a full screen of '@' characters got shifted in.
    // Our DisplayGrid clear() blanks to space which is visually equivalent
    // for a user: clean slate.  Also turn the cursor on so the user can
    // see where output will go.
    display_.clear();
    display_.set_cursor_on(true);
}

void App::load_file(const std::string& path) {
    // load_file calls into the Bus directly, but the CPU thread is
    // writing/reading memory concurrently.  The Bus has internal locks
    // for the keyboard buffer but RAM itself is unprotected.  To keep this
    // simple AND safe we pause the CPU via the debugger flag for the
    // duration of the load.  The user can resume from the CPU menu.
    bool was_paused = debugger_.is_paused();
    if (!was_paused) debugger_.toggle_pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));  // let CPU notice
    fileio::load_program(*bus_, path);

    // --debug: dump the nibblized track buffers right after a successful
    // .dsk mount, so we can inspect what the boot ROM is going to read.
    if (!debug_log_path_.empty() && bus_->disk().mounted()) {
        dump_disk_nibbles(debug_log_path_ + ".nib");
    }

    // Remember the last successfully-mounted disk so it auto-mounts on
    // the next launch (unless the active card has been switched to the
    // cassette - then we don't replay it).
    if (bus_->disk().mounted()) {
        settings_.set_last_disk(bus_->disk().image_path());
    }

    if (!was_paused) debugger_.toggle_pause();
}

void App::dump_disk_nibbles(const std::string& path) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f) return;
    f << "; Apple-1 emulator nibble dump.\n"
         "; Source .dsk: " << bus_->disk().image_path() << "\n"
         "; Per-track GCR-encoded buffer (6656 bytes/track, 35 tracks).\n"
         "; Layout per track: 48 self-sync $FF + 16 sectors {address field +\n"
         ";                   gap2 + data field + gap3} padded with $FF.\n\n";

    for (int t = 0; t < 35; ++t) {
        const auto& buf = bus_->disk().track_buffer(t);
        f << "==== Track " << t << " ("
          << buf.size() << " bytes) ====\n";
        char line[80];
        for (std::size_t i = 0; i < buf.size(); i += 16) {
            int n = std::snprintf(line, sizeof(line), "%05zX:", i);
            for (std::size_t j = 0; j < 16 && i + j < buf.size(); ++j) {
                n += std::snprintf(line + n, sizeof(line) - n, " %02X",
                                   buf[i + j]);
            }
            f << line << "\n";
        }
        f << "\n";
    }
    f.flush();
}

void App::save_cassette(u16 start, u16 end, const std::string& path,
                        std::string* err_out) {
    try {
        cassette::save_cassette(*bus_, path, start, end);
    } catch (const std::exception& e) {
        if (err_out) *err_out = e.what();
    }
}

void App::stage_cassette(const std::string& path, std::size_t* count_out,
                         std::string* err_out) {
    try {
        std::size_t n = cassette::stage_cassette_for_aci(*bus_, path);
        if (count_out) *count_out = n;
    } catch (const std::exception& e) {
        if (err_out) *err_out = e.what();
    }
}

void App::cpu_loop() {
    using namespace std::chrono;

    // Throttle the emulated CPU to ~1.023 MHz (real Apple-1 clock).  We
    // run a fixed batch of cycles, then sleep for the wall-clock time
    // those cycles should have taken minus what we've actually used.
    // Without this, software-only delay loops (e.g. Apple-30th's image-
    // display pause) execute ~100x faster than on real hardware.
    constexpr u64 kBatchCycles = 1024;       // ~1ms worth
    constexpr auto kBatchNs   = nanoseconds(1'000'000'000ull * kBatchCycles
                                            / kCpuHz);   // ~1000us

    // Trace helper: log one line per instruction whose PC is in
    // $C100..$C1FF.  Captures registers BEFORE the instruction executes
    // plus a disassembly of what's about to run.
    //
    // Per-line format:
    //   $PCPC  XX XX XX  MNEMONIC OPERAND          A=XX X=XX Y=XX SP=XX F=NV-BDIZC  CYC=N
    //
    // The opcode-byte field is fixed-width (8 chars: "XX XX XX" max, padded
    // with spaces for 1- or 2-byte instructions).  The disassembly column
    // is padded to 16 chars so registers always line up.
    auto trace = [this]() {
        if (!debug_log_.is_open()) return;
        const u16 pc = cpu_->pc();
        // Trace boot ROM ($C100-$C1FF) AND BASIC's DO_FOR / DO_NEXT /
        // EXPR / DO_BAD region ($E5xx-$E6xx and $E40C error path) so
        // we can see the FOR-loop ?SN divergence.
        const bool in_boot   = (pc >= 0xC100 && pc <= 0xC1FF);
        const bool in_for    = (pc >= 0xE57C && pc <= 0xE6FF);
        const bool in_err    = (pc >= 0xE40C && pc <= 0xE415);
        if (!in_boot && !in_for && !in_err) return;

        const auto d = disasm::decode(*bus_, pc);

        // Raw opcode bytes (1..3 of them), padded to 8 chars.
        char bytes[16];
        switch (d.length) {
            case 1:
                std::snprintf(bytes, sizeof(bytes), "%02X      ",
                              bus_->peek(pc));
                break;
            case 2:
                std::snprintf(bytes, sizeof(bytes), "%02X %02X   ",
                              bus_->peek(pc),
                              bus_->peek(static_cast<u16>(pc + 1)));
                break;
            case 3:
            default:
                std::snprintf(bytes, sizeof(bytes), "%02X %02X %02X",
                              bus_->peek(pc),
                              bus_->peek(static_cast<u16>(pc + 1)),
                              bus_->peek(static_cast<u16>(pc + 2)));
                break;
        }

        const u8 s = cpu_->status();
        auto fl = [&](u8 mask, char on) { return (s & mask) ? on : '.'; };
        char flags[9];
        std::snprintf(flags, sizeof(flags), "%c%c-%c%c%c%c%c",
                      fl(CPU6502::N, 'N'), fl(CPU6502::V, 'V'),
                      fl(CPU6502::B, 'B'), fl(CPU6502::D, 'D'),
                      fl(CPU6502::I, 'I'), fl(CPU6502::Z, 'Z'),
                      fl(CPU6502::C, 'C'));

        // Pad disasm to 16 chars so the register column lines up.
        char dis[24];
        std::snprintf(dis, sizeof(dis), "%-16s", d.text.c_str());

        // BASIC ZP slots: IP $00-01, PE $02-03, T0 $06-07, T1 $08-09,
        // T2 $0A-0B, CURLN $0C-0D, RUNF $0F, FSTK $18.
        const u8 ip_l = bus_->peek(0x0000);
        const u8 ip_h = bus_->peek(0x0001);
        const u8 t0_l = bus_->peek(0x0006);
        const u8 t0_h = bus_->peek(0x0007);
        const u8 t2_l = bus_->peek(0x000A);
        const u8 t2_h = bus_->peek(0x000B);
        const u8 cl_l = bus_->peek(0x000C);
        const u8 cl_h = bus_->peek(0x000D);
        const u8 fstk = bus_->peek(0x0018);

        char line[256];
        std::snprintf(line, sizeof(line),
                      "$%04X  %s  %s  A=%02X X=%02X Y=%02X SP=%02X F=%s  "
                      "IP=%02X%02X T0=%02X%02X T2=%02X%02X CURLN=%02X%02X FSTK=%02X  "
                      "CYC=%llu\n",
                      pc, bytes, dis,
                      cpu_->a(), cpu_->x(), cpu_->y(), cpu_->sp(),
                      flags,
                      ip_h, ip_l, t0_h, t0_l, t2_h, t2_l, cl_h, cl_l, fstk,
                      static_cast<unsigned long long>(cpu_->cycles()));
        debug_log_ << line;
    };

    auto next_tick = steady_clock::now();
    while (!shutdown_.load()) {
        if (debugger_.should_pause_before(cpu_->pc(),
                                          bus_->peek(cpu_->pc()))) {
            if (debug_log_.is_open()) debug_log_.flush();
            std::this_thread::sleep_for(milliseconds(20));
            next_tick = steady_clock::now();   // resync after pause
            continue;
        }

        // The outer should_pause_before just gave us the green light -
        // and may have consumed a single-step / run-to-RTS latch in the
        // process.  We MUST run at least one instruction here before
        // re-checking, otherwise the inner loop's first call would see
        // paused_=true (with step_pending_ already exhausted) and break
        // out before cpu_->step() ever runs - that's the bug that made
        // the STEP button do nothing.
        u64 target = cpu_->cycles() + kBatchCycles;
        trace();
        cpu_->step();

        // Continue the batch.
        while (cpu_->cycles() < target && !shutdown_.load()) {
            if (debugger_.should_pause_before(cpu_->pc(),
                                              bus_->peek(cpu_->pc()))) break;
            trace();
            cpu_->step();
        }

        // Sleep until the wall-clock time when this batch should have
        // completed.  If we're behind (host slower than 1MHz, or putc
        // sleeps eating time), don't accumulate slack - just resync.
        next_tick += kBatchNs;
        auto now = steady_clock::now();
        if (next_tick > now) {
            std::this_thread::sleep_until(next_tick);
        } else if (now - next_tick > milliseconds(50)) {
            next_tick = now;   // resync if we fell behind by >50ms
        }
    }
}

} // namespace apple1

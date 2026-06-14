// app.cpp - app state + CPU thread.

#include "app.h"
#include "fileio.h"
#include "cassette.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace apple1 {

namespace {
std::unique_ptr<App> g_app;
}

App& app()        { return *g_app; }
void create_app(const std::string& settings_path,
                const std::string& roms_dir_override) {
    g_app = std::make_unique<App>(settings_path, roms_dir_override);
}
void destroy_app(){ g_app.reset(); }

App::App(const std::string& settings_path,
         const std::string& roms_dir_override) {
    if (!settings_path.empty()) settings_.set_path(settings_path);
    settings_.load();

    // Load ROMs.
    std::string dir = roms_dir_override.empty()
                    ? roms::locate_rom_directory()
                    : roms_dir_override;
    rom_set_ = roms::load_from_directory(dir);

    bus_ = std::make_unique<Bus>(rom_set_);
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
    if (!was_paused) debugger_.toggle_pause();
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

    auto next_tick = steady_clock::now();
    while (!shutdown_.load()) {
        if (debugger_.should_pause_before(cpu_->pc())) {
            std::this_thread::sleep_for(milliseconds(20));
            next_tick = steady_clock::now();   // resync after pause
            continue;
        }

        // Run instructions until we've consumed kBatchCycles.
        u64 target = cpu_->cycles() + kBatchCycles;
        while (cpu_->cycles() < target && !shutdown_.load()) {
            if (debugger_.should_pause_before(cpu_->pc())) break;
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

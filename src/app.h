// app.h - process-wide application state.  Owns all the emulator
// components (Bus, CPU, ROMs, DisplayGrid, Debugger) plus the CPU thread
// itself.  Lifetime spans WinMain.
//
// Why this exists: with two windows (main + debugger) both manipulating
// the same emulator, threading it through every WM_COMMAND would be ugly.
// Instead the windows call into App via a singleton-ish reference.

#pragma once

#include "bus.h"
#include "cpu6502.h"
#include "debugger.h"
#include "display_grid.h"
#include "roms.h"
#include "settings.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace apple1 {

class App {
public:
    // Construct everything (load ROMs, build the Bus, hook up callbacks,
    // start the CPU thread).  Throws on fatal init errors.
    App(const std::string& settings_path = "",
        const std::string& roms_dir_override = "");

    // Stop the CPU thread and join it.
    ~App();

    // Accessors used by the windows.
    Bus&         bus()         { return *bus_; }
    CPU6502&     cpu()         { return *cpu_; }
    Debugger&    debugger()    { return debugger_; }
    DisplayGrid& display()     { return display_; }
    Settings&    settings()    { return settings_; }
    const roms::Set& rom_set() const { return rom_set_; }

    // Commands (called from menu handlers).
    void reset_cpu();
    void clear_screen();
    void load_file(const std::string& path);
    void save_cassette(u16 start, u16 end, const std::string& path,
                       std::string* err_out);
    void stage_cassette(const std::string& path, std::size_t* count_out,
                        std::string* err_out);

    // The CPU thread is unconditionally on (paused via debugger).  These
    // start/stop the *whole* thread, used only by ctor/dtor.
    void start();
    void stop();

private:
    void cpu_loop();

    roms::Set            rom_set_;
    std::unique_ptr<Bus>     bus_;
    std::unique_ptr<CPU6502> cpu_;
    Debugger             debugger_;
    DisplayGrid          display_;
    Settings             settings_;

    std::thread          cpu_thread_;
    std::atomic<bool>    shutdown_{false};
};

// Lazy global accessor.  Created in WinMain.
App& app();
void create_app(const std::string& settings_path = "",
                const std::string& roms_dir_override = "");
void destroy_app();

} // namespace apple1

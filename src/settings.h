// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// settings.h - persistent user-toggleable settings.
// Backed by a simple INI-style file (one key=value per line).  The file
// location is provided by the platform layer at construction time so
// this module stays free of any OS-specific code.

#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace apple1 {

enum class Phosphor : int {
    White  = 0,
    Green  = 1,
    Amber  = 2,
};

// Apple-1 RAM expansion. The on-board RAM is always 8KB ($0000-$1FFF); the
// values below denote *additional* RAM stacked on top of that.  Mapping:
//   None  ->  $0000-$1FFF  (8KB total)
//   K8    ->  $0000-$3FFF  (16KB total)
//   K16   ->  $0000-$5FFF  (24KB total)
//   K24   ->  $0000-$7FFF  (32KB total)
enum class RamExpansion : int {
    None = 0,
    K8   = 1,
    K16  = 2,
    K24  = 3,
};

// What's plugged into the expansion slot.
//   None      -> $C000-$C1FF unmapped
//   Cassette  -> ACI registers at $C000-$C0FF, ACI ROM at $C100-$C1FF
//   Disk1     -> Disk II soft switches at $C000-$C00F,
//                OneDos boot ROM at $C100-$C1FF
enum class IoCard : int {
    None     = 0,
    Cassette = 1,
    Disk1    = 2,
};

// Disk II read latch model.
//   Bit  -> cycle-accurate LS166 shift register; one bit per 4 cycles.
//           Matches real hardware exactly but is sensitive to host OS
//           thread interactions on some machines (AMD chiplet schedulers
//           in particular).
//   Byte -> deliver one whole nibble byte per 32 cycles; no partial
//           shift register state.  Deterministic across hosts at the
//           cost of strict accuracy (real Disk II latches per bit).
enum class DiskLatch : int {
    Bit  = 0,
    Byte = 1,
};

class Settings {
public:
    void set_path(std::string p) { path_ = std::move(p); }
    const std::string& path() const { return path_; }

    void load();
    void save() const;

    bool         scanlines()       const { return scanlines_.load(); }
    bool         dot_artifact()    const { return dot_artifact_.load(); }
    bool         teletype_pacing() const { return teletype_pacing_.load(); }
    bool         vignette()        const { return vignette_.load(); }
    Phosphor     phosphor()        const { return static_cast<Phosphor>(phosphor_.load()); }
    RamExpansion ram_expansion()   const { return static_cast<RamExpansion>(ram_expansion_.load()); }
    IoCard       io_card()         const { return static_cast<IoCard>(io_card_.load()); }
    DiskLatch    disk_latch()      const { return static_cast<DiskLatch>(disk_latch_.load()); }
    std::string  last_disk()       const { std::lock_guard g(last_disk_mu_); return last_disk_; }

    void set_scanlines(bool v)             { scanlines_.store(v); save(); }
    void set_dot_artifact(bool v)          { dot_artifact_.store(v); save(); }
    void set_teletype_pacing(bool v)       { teletype_pacing_.store(v); save(); }
    void set_vignette(bool v)              { vignette_.store(v); save(); }
    void set_phosphor(Phosphor p)          { phosphor_.store(static_cast<int>(p)); save(); }
    void set_ram_expansion(RamExpansion r) { ram_expansion_.store(static_cast<int>(r)); save(); }
    void set_io_card(IoCard c)             { io_card_.store(static_cast<int>(c)); save(); }
    void set_disk_latch(DiskLatch d)       { disk_latch_.store(static_cast<int>(d)); save(); }
    void set_last_disk(const std::string& p) {
        { std::lock_guard g(last_disk_mu_); last_disk_ = p; }
        save();
    }

private:
    std::string       path_ = "settings.ini";
    std::atomic<bool> scanlines_{false};
    std::atomic<bool> dot_artifact_{true};
    std::atomic<bool> teletype_pacing_{true};
    std::atomic<bool> vignette_{true};
    std::atomic<int>  phosphor_{static_cast<int>(Phosphor::White)};
    std::atomic<int>  ram_expansion_{static_cast<int>(RamExpansion::None)};
    std::atomic<int>  io_card_{static_cast<int>(IoCard::Cassette)};
    std::atomic<int>  disk_latch_{static_cast<int>(DiskLatch::Bit)};

    // Last .dsk path the user mounted - replayed on startup so the
    // disk persists across runs (unless Cassette is the active card).
    mutable std::mutex last_disk_mu_;
    std::string       last_disk_;
};

} // namespace apple1

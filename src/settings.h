// settings.h - persistent user-toggleable settings.
// Backed by a simple INI-style file (one key=value per line).  The file
// location is provided by the platform layer at construction time so
// this module stays free of any OS-specific code.

#pragma once

#include <atomic>
#include <string>

namespace apple1 {

enum class Phosphor : int {
    White  = 0,
    Green  = 1,
    Amber  = 2,
};

class Settings {
public:
    void set_path(std::string p) { path_ = std::move(p); }
    const std::string& path() const { return path_; }

    void load();
    void save() const;

    bool     scanlines()       const { return scanlines_.load(); }
    bool     dot_artifact()    const { return dot_artifact_.load(); }
    bool     teletype_pacing() const { return teletype_pacing_.load(); }
    bool     vignette()        const { return vignette_.load(); }
    Phosphor phosphor()        const { return static_cast<Phosphor>(phosphor_.load()); }

    void set_scanlines(bool v)       { scanlines_.store(v); save(); }
    void set_dot_artifact(bool v)    { dot_artifact_.store(v); save(); }
    void set_teletype_pacing(bool v) { teletype_pacing_.store(v); save(); }
    void set_vignette(bool v)        { vignette_.store(v); save(); }
    void set_phosphor(Phosphor p)    { phosphor_.store(static_cast<int>(p)); save(); }

private:
    std::string       path_ = "settings.ini";
    std::atomic<bool> scanlines_{false};
    std::atomic<bool> dot_artifact_{true};
    std::atomic<bool> teletype_pacing_{true};
    std::atomic<bool> vignette_{true};
    std::atomic<int>  phosphor_{static_cast<int>(Phosphor::White)};
};

} // namespace apple1

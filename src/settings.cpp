// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// settings.cpp - no OS-specific code.  Reads/writes a small INI file at
// the path set by the platform layer.

#include "settings.h"
#include <fstream>
#include <string>

namespace apple1 {

void Settings::load() {
    std::ifstream f(path_);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if      (k == "scanlines")       scanlines_.store(v == "1");
        else if (k == "dot_artifact")    dot_artifact_.store(v == "1");
        else if (k == "teletype_pacing") teletype_pacing_.store(v == "1");
        else if (k == "vignette")        vignette_.store(v == "1");
        else if (k == "phosphor") {
            int p = 0;
            try { p = std::stoi(v); } catch (...) {}
            if (p >= 0 && p <= 2) phosphor_.store(p);
        }
        else if (k == "ram_expansion") {
            int r = 0;
            try { r = std::stoi(v); } catch (...) {}
            if (r >= 0 && r <= 3) ram_expansion_.store(r);
        }
        else if (k == "io_card") {
            int c = 0;
            try { c = std::stoi(v); } catch (...) {}
            if (c >= 0 && c <= 2) io_card_.store(c);
        }
        else if (k == "disk_latch") {
            int d = 0;
            try { d = std::stoi(v); } catch (...) {}
            if (d >= 0 && d <= 1) disk_latch_.store(d);
        }
        else if (k == "last_disk") {
            std::lock_guard g(last_disk_mu_);
            last_disk_ = v;
        }
    }
}

void Settings::save() const {
    std::ofstream f(path_, std::ios::trunc);
    if (!f) return;
    f << "scanlines="       << (scanlines_.load() ? 1 : 0) << "\n";
    f << "dot_artifact="    << (dot_artifact_.load() ? 1 : 0) << "\n";
    f << "teletype_pacing=" << (teletype_pacing_.load() ? 1 : 0) << "\n";
    f << "vignette="        << (vignette_.load() ? 1 : 0) << "\n";
    f << "phosphor="        << phosphor_.load() << "\n";
    f << "ram_expansion="   << ram_expansion_.load() << "\n";
    f << "io_card="         << io_card_.load() << "\n";
    f << "disk_latch="      << disk_latch_.load() << "\n";
    {
        std::lock_guard g(last_disk_mu_);
        if (!last_disk_.empty()) {
            f << "last_disk=" << last_disk_ << "\n";
        }
    }
}

} // namespace apple1

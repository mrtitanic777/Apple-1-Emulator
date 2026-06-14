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
}

} // namespace apple1

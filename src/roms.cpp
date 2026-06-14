// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// roms.cpp - read .rom files from disk.

#include "roms.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>

namespace apple1::roms {

namespace fs = std::filesystem;

// Read entire file into a byte vector.  Returns empty optional if missing.
static std::optional<std::vector<u8>> read_file_optional(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return std::nullopt;
    std::vector<u8> out((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    return out;
}

Set load_from_directory(const std::string& dir) {
    Set set;
    fs::path d(dir);

    // Required: Wozmon.
    auto woz = read_file_optional(d / "wozmon.rom");
    if (!woz) {
        std::ostringstream os;
        os << "wozmon.rom not found in " << d.string()
           << " - the system can't boot without it.";
        throw std::runtime_error(os.str());
    }
    if (woz->size() != 256) {
        std::ostringstream os;
        os << "wozmon.rom has size " << woz->size() << " (expected 256).";
        throw std::runtime_error(os.str());
    }
    set.wozmon = std::move(*woz);

    // Optional: BASIC.
    auto basic = read_file_optional(d / "basic.rom");
    if (basic) {
        if (basic->size() != 0x1000) {
            std::ostringstream os;
            os << "basic.rom has size " << basic->size() << " (expected 4096).";
            throw std::runtime_error(os.str());
        }
        set.basic = std::move(basic);
    }

    // Optional: ACI.
    auto aci = read_file_optional(d / "aci.rom");
    if (aci) {
        if (aci->size() != 256) {
            std::ostringstream os;
            os << "aci.rom has size " << aci->size() << " (expected 256).";
            throw std::runtime_error(os.str());
        }
        set.aci = std::move(aci);
    }

    // Optional: OneDos ROM (Disk 1 IO card).  Two layouts supported:
    //   256 bytes -> $C100-$C1FF, just the boot loader.
    //   512 bytes -> $C100-$C1FF boot + $C200-$C2FF NIBTAB.
    // Falls back to boot.rom for users who keep the file under its OneDos
    // build name.
    auto onedos = read_file_optional(d / "onedos.rom");
    if (!onedos) onedos = read_file_optional(d / "boot.rom");
    if (onedos) {
        if (onedos->size() != 256 && onedos->size() != 512) {
            std::ostringstream os;
            os << "onedos.rom has size " << onedos->size()
               << " (expected 256 or 512).";
            throw std::runtime_error(os.str());
        }
        set.onedos = std::move(onedos);
    }

    // Required: character generator (2513-style 5x7 glyphs, 64 chars * 8 rows).
    auto chargen = read_file_optional(d / "char.rom");
    if (!chargen) {
        std::ostringstream os;
        os << "char.rom not found in " << d.string()
           << " - the 2513 character generator is required for display.";
        throw std::runtime_error(os.str());
    }
    if (chargen->size() != 512) {
        std::ostringstream os;
        os << "char.rom has size " << chargen->size() << " (expected 512).";
        throw std::runtime_error(os.str());
    }
    set.chargen = std::move(*chargen);

    return set;
}

std::string locate_rom_directory() {
    // Prefer a 'roms' subdirectory next to the executable if it exists;
    // fall back to the current working directory.  Users running the exe
    // from the dev tree expect ./roms/, but a deployed install might just
    // dump everything in one place.
    fs::path candidates[] = { "roms", "./roms", "." };
    for (const auto& c : candidates) {
        if (fs::exists(c / "wozmon.rom")) {
            return c.string();
        }
    }
    return "roms";   // default; load will fail with a clear message
}

} // namespace apple1::roms

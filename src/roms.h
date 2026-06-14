// roms.h - load ROM image files at startup.
//
// All three Apple-1 ROMs ship as separate .rom files alongside the
// executable.  This module reads them at boot.  Missing optional ROMs
// (BASIC, ACI) just disable the related features; missing Wozmon is fatal
// because there's no system without it.

#pragma once

#include "common.h"
#include <string>
#include <vector>
#include <optional>

namespace apple1::roms {

struct Set {
    std::vector<u8> wozmon;             // required, 256 bytes
    std::vector<u8> chargen;            // required, 512 bytes (2513-style)
    std::optional<std::vector<u8>> basic; // optional, 4096 bytes
    std::optional<std::vector<u8>> aci;   // optional, 256 bytes
};

// Read the three ROM images from a directory.  Returns the loaded set.
// Throws std::runtime_error if Wozmon is missing or any ROM has wrong size.
Set load_from_directory(const std::string& dir);

// Find the ROM directory: first try ./roms/ relative to the executable,
// then ./ relative to the current working directory.
std::string locate_rom_directory();

} // namespace apple1::roms

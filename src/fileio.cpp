// fileio.cpp - load_program() dispatcher.

#include "fileio.h"
#include "cassette.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>

namespace apple1::fileio {

namespace {

// Extract a 4-hex-digit address from "name_XXXX.ext" if present.  Returns
// nullopt if filename doesn't match.
std::optional<u16> address_from_filename(const std::string& path,
                                         const std::string& ext_suffix) {
    // Match e.g. "myprog_E000.bin" - underscore, 4 hex digits, then the
    // extension at end of filename.
    std::string pattern = "_([0-9A-Fa-f]{4})\\" + ext_suffix + "$";
    std::regex re(pattern);
    std::smatch m;
    if (std::regex_search(path, m, re)) {
        return static_cast<u16>(std::stoul(m[1].str(), nullptr, 16));
    }
    return std::nullopt;
}

bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
        char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

// Read file fully into a byte buffer.
std::vector<u8> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("can't open: " + path);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
}

// Returns true iff the buffer is plausibly ASCII text.
bool looks_like_text(const std::vector<u8>& data) {
    if (data.empty()) return false;
    for (u8 b : data) {
        if (b == '\r' || b == '\n' || b == '\t') continue;
        if (b < 0x20 || b > 0x7E) return false;
    }
    return true;
}

// Parse Wozmon deposit lines.  Returns the number of bytes deposited.
u32 parse_wozmon_text(Bus& bus, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    u32 count = 0;
    while (std::getline(in, line)) {
        // Trim and skip blanks.
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line = line.substr(first);

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        u16 addr = static_cast<u16>(std::stoul(line.substr(0, colon),
                                                nullptr, 16));
        std::istringstream toks(line.substr(colon + 1));
        std::string tok;
        while (toks >> tok) {
            u8 v = static_cast<u8>(std::stoul(tok, nullptr, 16));
            bus.write(addr++, v);
            ++count;
        }
    }
    return count;
}

} // namespace

std::string load_program(Bus& bus, const std::string& filepath) {
    std::vector<u8> data;
    try {
        data = slurp(filepath);
    } catch (const std::exception& e) {
        return std::string("LOAD ERROR: ") + e.what();
    }

    // WAV cassette file?  Detected by extension; decoder will throw if
    // malformed.
    if (ends_with_ci(filepath, ".wav")) {
        try {
            auto decoded = cassette::load_wav(filepath);
            u16 addr = 0x0300;
            if (auto a = address_from_filename(filepath, ".wav")) addr = *a;
            bus.load_bytes(addr, decoded);
            std::ostringstream os;
            os << "LOADED CASSETTE: " << decoded.size()
               << " BYTES AT $" << std::hex << std::uppercase << addr;
            return os.str();
        } catch (const std::exception& e) {
            return std::string("WAV DECODE ERROR: ") + e.what();
        }
    }

    // .dsk disk image?  Hand off to the Disk II controller.  The raw_image
    // we already slurped contains the 143,360 bytes; mount_dsk reads from
    // disk itself, so just pass the path through.
    if (ends_with_ci(filepath, ".dsk")) {
        std::string err;
        if (bus.disk().mount_dsk(filepath, &err)) {
            std::ostringstream os;
            os << "MOUNTED DISK: " << filepath
               << " (" << data.size() << " BYTES)";
            return os.str();
        }
        return std::string("DSK MOUNT ERROR: ") + err;
    }

    // Wozmon deposit text?
    if (looks_like_text(data)) {
        std::string text(data.begin(), data.end());
        if (text.find(':') != std::string::npos) {
            try {
                u32 n = parse_wozmon_text(bus, text);
                std::ostringstream os;
                os << "LOADED " << n << " BYTES";
                return os.str();
            } catch (const std::exception& e) {
                return std::string("PARSE ERROR: ") + e.what();
            }
        }
    }

    // Raw binary.
    u16 addr = 0x0300;
    if (auto a = address_from_filename(filepath, ".bin")) addr = *a;
    bus.load_bytes(addr, data);
    std::ostringstream os;
    os << "LOADED " << data.size()
       << " BYTES AT $" << std::hex << std::uppercase << addr;
    return os.str();
}

} // namespace apple1::fileio

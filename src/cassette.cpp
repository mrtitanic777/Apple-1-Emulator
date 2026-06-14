// cassette.cpp - WAV encode/decode + tape-cycle conversion.

#include "cassette.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace apple1::cassette {

namespace {

// Map CPU cycles to samples at our chosen audio sample rate.  Minimum 2
// samples to keep a real square wave even at very high frequencies.
std::size_t cycles_to_samples(u32 cycles) {
    double s = static_cast<double>(cycles) * kCassetteSampleRate / kCpuHz;
    auto n = static_cast<std::size_t>(std::round(s));
    return n < 2 ? 2 : n;
}

// --- WAV file I/O.  Minimal format: 16-bit mono PCM with a 44-byte header.

void write_wav(const std::string& path, const std::vector<int16_t>& samples,
               u32 sample_rate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open WAV for writing: " + path);
    }

    auto u32le = [&](uint32_t v) {
        char b[4] = { char(v), char(v >> 8), char(v >> 16), char(v >> 24) };
        f.write(b, 4);
    };
    auto u16le = [&](uint16_t v) {
        char b[2] = { char(v), char(v >> 8) };
        f.write(b, 2);
    };

    const u32 byte_rate   = sample_rate * 2;       // mono * 16-bit
    const u32 data_bytes  = static_cast<u32>(samples.size() * sizeof(int16_t));
    const u32 file_size   = 36 + data_bytes;

    f.write("RIFF", 4);  u32le(file_size);  f.write("WAVE", 4);
    f.write("fmt ", 4);  u32le(16);         u16le(1);   u16le(1);
    u32le(sample_rate);  u32le(byte_rate);  u16le(2);   u16le(16);
    f.write("data", 4);  u32le(data_bytes);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

struct WavData {
    u32 sample_rate;
    std::vector<int16_t> samples;   // mono (multi-channel input averaged)
};

WavData read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open WAV: " + path);

    char id[4];
    f.read(id, 4);
    if (std::strncmp(id, "RIFF", 4) != 0) throw std::runtime_error("not a RIFF file");
    f.ignore(4);                                   // file size
    f.read(id, 4);
    if (std::strncmp(id, "WAVE", 4) != 0) throw std::runtime_error("not a WAVE file");

    auto read_u32 = [&]() -> u32 {
        unsigned char b[4]; f.read(reinterpret_cast<char*>(b), 4);
        return u32(b[0]) | (u32(b[1]) << 8) | (u32(b[2]) << 16) | (u32(b[3]) << 24);
    };
    auto read_u16 = [&]() -> u16 {
        unsigned char b[2]; f.read(reinterpret_cast<char*>(b), 2);
        return u16(b[0]) | u16(b[1] << 8);
    };

    u16 channels = 1, bits = 16;
    u32 sample_rate = 44100;
    std::vector<int16_t> samples;
    bool got_fmt = false, got_data = false;

    while (!got_data && f.good()) {
        f.read(id, 4);
        if (!f.good()) break;
        u32 chunk_size = read_u32();
        if (std::strncmp(id, "fmt ", 4) == 0) {
            u16 fmt = read_u16();    (void)fmt;        // 1 = PCM
            channels      = read_u16();
            sample_rate   = read_u32();
            (void)read_u32();        // byte rate
            (void)read_u16();        // block align
            bits          = read_u16();
            if (chunk_size > 16) f.ignore(chunk_size - 16);
            got_fmt = true;
        } else if (std::strncmp(id, "data", 4) == 0) {
            if (!got_fmt) throw std::runtime_error("data chunk before fmt chunk");
            if (bits != 16) throw std::runtime_error("only 16-bit WAV supported");
            const std::size_t total_samples = chunk_size / 2;
            std::vector<int16_t> raw(total_samples);
            f.read(reinterpret_cast<char*>(raw.data()), chunk_size);
            if (channels == 1) {
                samples = std::move(raw);
            } else {
                samples.reserve(total_samples / channels);
                for (std::size_t i = 0; i < total_samples; i += channels) {
                    int sum = 0;
                    for (u16 c = 0; c < channels; ++c) sum += raw[i + c];
                    samples.push_back(static_cast<int16_t>(sum / channels));
                }
            }
            got_data = true;
        } else {
            f.ignore(chunk_size);
        }
    }
    if (!got_data) throw std::runtime_error("no data chunk found");
    return { sample_rate, std::move(samples) };
}

// Append `count` samples of value `v` to the buffer.
void append_run(std::vector<int16_t>& buf, std::size_t count, int16_t v) {
    buf.insert(buf.end(), count, v);
}

} // namespace

double save_wav(const std::vector<u8>& data, const std::string& filepath) {
    const int16_t amp = static_cast<int16_t>(kCassetteAmplitude * 32767.0);
    std::vector<int16_t> samples;
    int16_t state = amp;

    // One half-cycle = a run of samples at the current state, then state flips.
    auto emit = [&](u32 cpu_cycles) {
        append_run(samples, cycles_to_samples(cpu_cycles), state);
        state = static_cast<int16_t>(-state);
    };

    // Leader: many transitions of ~864 Hz tone.
    for (u32 i = 0; i < kCassetteLeaderCount; ++i) {
        emit(kCassetteLeaderCycles);
        emit(kCassetteLeaderCycles);
    }

    // Two sync transitions of distinct length to mark "data starts now".
    emit(kCassetteSync1Cycles);
    emit(kCassetteSync2Cycles);

    // Data: each bit is one full cycle (two transitions).  MSB first.
    for (u8 byte : data) {
        for (int bit_idx = 7; bit_idx >= 0; --bit_idx) {
            u32 half = ((byte >> bit_idx) & 1)
                       ? kCassetteBit1Cycles
                       : kCassetteBit0Cycles;
            emit(half);
            emit(half);
        }
    }

    // Trailing sync cycle so the decoder can measure the last bit's period.
    emit(kCassetteLeaderCycles);
    emit(kCassetteLeaderCycles);

    // Trailer silence.
    append_run(samples,
               static_cast<std::size_t>(kCassetteSampleRate * kCassetteTrailerSecs),
               0);

    write_wav(filepath, samples, kCassetteSampleRate);
    return static_cast<double>(samples.size()) / kCassetteSampleRate;
}

std::vector<u8> load_wav(const std::string& filepath) {
    WavData wav = read_wav(filepath);
    if (wav.samples.size() < 4) {
        throw std::runtime_error("WAV too short to contain tape data");
    }

    // Find all zero crossings (both directions).
    std::vector<std::size_t> crossings;
    int16_t prev = wav.samples[0];
    for (std::size_t i = 1; i < wav.samples.size(); ++i) {
        int16_t cur = wav.samples[i];
        if ((prev <= 0 && cur > 0) || (prev >= 0 && cur < 0)) {
            crossings.push_back(i);
        }
        prev = cur;
    }
    if (crossings.size() < 4) throw std::runtime_error("not enough zero crossings");

    // Periods (sample counts between crossings).
    std::vector<std::size_t> periods;
    periods.reserve(crossings.size() - 1);
    for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
        periods.push_back(crossings[i + 1] - crossings[i]);
    }

    // Compute thresholds based on expected cycle counts at our sample rate.
    const double cps  = static_cast<double>(kCpuHz) / wav.sample_rate;
    const double s_b0 = kCassetteBit0Cycles  / cps;     // shortest, ~10
    const double s_b1 = kCassetteBit1Cycles  / cps;     // medium,   ~20
    const double s_ld = kCassetteLeaderCycles / cps;    // longest,  ~25
    const double t_bit    = (s_b0 + s_b1) / 2.0;        // 0 vs 1 boundary
    const double t_leader = (s_b1 + s_ld) / 2.0;        // 1 vs leader boundary

    // Skip leader: find first period below the leader threshold.
    std::size_t data_start = SIZE_MAX;
    for (std::size_t i = 0; i < periods.size(); ++i) {
        if (periods[i] < t_leader) { data_start = i; break; }
    }
    if (data_start == SIZE_MAX) throw std::runtime_error("no data after leader");

    // Skip the two sync transitions.
    std::size_t cursor = data_start + 2;

    std::vector<u8> out;
    u8  bits = 0;
    int bit_count = 0;
    while (cursor + 1 < periods.size()) {
        std::size_t p1 = periods[cursor];
        std::size_t p2 = periods[cursor + 1];
        if (p1 >= t_leader) break;                       // back to leader/end
        double avg = (p1 + p2) / 2.0;
        int bit = (avg > t_bit) ? 1 : 0;
        bits = static_cast<u8>((bits << 1) | bit);
        if (++bit_count == 8) {
            out.push_back(bits);
            bits = 0;
            bit_count = 0;
        }
        cursor += 2;
    }
    return out;
}

std::vector<u32> wav_to_tape_transitions(const std::string& filepath) {
    WavData wav = read_wav(filepath);
    if (wav.samples.size() < 2) {
        throw std::runtime_error("no tape transitions detected");
    }

    // All zero crossings -> each one is a bit-7 flip on $C081.
    std::vector<std::size_t> crossings;
    int16_t prev = wav.samples[0];
    for (std::size_t i = 1; i < wav.samples.size(); ++i) {
        int16_t cur = wav.samples[i];
        if ((prev <= 0 && cur > 0) || (prev >= 0 && cur < 0)) {
            crossings.push_back(i);
        }
        prev = cur;
    }
    if (crossings.empty()) throw std::runtime_error("no zero crossings in WAV");

    // Convert sample-position deltas to CPU-cycle deltas.  Truncate
    // (not round) to match the Python prototype, which was validated
    // against the real ACI ROM.
    const double cps = static_cast<double>(kCpuHz) / wav.sample_rate;
    std::vector<u32> transitions;
    transitions.reserve(crossings.size());
    std::size_t last = 0;
    for (std::size_t c : crossings) {
        std::size_t dsamp = c - last;
        u32 dcyc = static_cast<u32>(dsamp * cps);   // truncate
        transitions.push_back(dcyc < 1 ? 1 : dcyc);
        last = c;
    }
    return transitions;
}

SaveResult save_cassette(Bus& bus, const std::string& filepath,
                         u16 start_addr, u16 end_addr) {
    if (end_addr < start_addr) std::swap(start_addr, end_addr);
    std::vector<u8> data;
    data.reserve(end_addr - start_addr + 1);
    for (u32 a = start_addr; a <= end_addr; ++a) {
        data.push_back(bus.peek(static_cast<u16>(a)));
    }
    double duration = save_wav(data, filepath);
    return { static_cast<u32>(data.size()), duration };
}

std::size_t stage_cassette_for_aci(Bus& bus, const std::string& filepath) {
    auto transitions = wav_to_tape_transitions(filepath);
    bus.load_tape(transitions);
    return transitions.size();
}

} // namespace apple1::cassette

// =====================================================================
// deluge-sim / wav_io.hpp — RIFF WAV read/write, dependency-free
// Supports: PCM16/24/32, float32, WAVE_FORMAT_EXTENSIBLE (reads subformat).
// =====================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace deluge_sim {

struct WavData {
    uint32_t sample_rate = 48000;
    uint16_t channels = 2;                 // 1..8 accepted on read; sim uses <=2
    std::vector<float> samples;            // interleaved: L R L R ...
    size_t frames() const {
        return (channels == 0) ? 0 : samples.size() / channels;
    }
};

// Reads a WAV. On error returns false and fills 'err' with an actionable message.
bool read_wav(const std::string& path, WavData& out, std::string& err);

// Writes WAV PCM16 (clamped to [-1,1]). 'data' comes interleaved.
bool write_wav16(const std::string& path, uint32_t sample_rate,
                 uint16_t channels, const std::vector<float>& data,
                 std::string& err);

} // namespace deluge_sim

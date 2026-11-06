// =====================================================================
// deluge-sim / wav_io.hpp — Lectura/escritura WAV RIFF sin dependencias
// Soporta: PCM16/24/32, float32, WAVE_FORMAT_EXTENSIBLE (lee subformat).
// =====================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace deluge_sim {

struct WavData {
    uint32_t sample_rate = 48000;
    uint16_t channels = 2;                 // 1..8 aceptado en lectura; sim usa <=2
    std::vector<float> samples;            // intercalado: L R L R ...
    size_t frames() const {
        return (channels == 0) ? 0 : samples.size() / channels;
    }
};

// Lee un WAV. En error devuelve false y llena 'err' con mensaje accionable.
bool read_wav(const std::string& path, WavData& out, std::string& err);

// Escribe WAV PCM16 (clamp a [-1,1]). 'data' viene intercalado.
bool write_wav16(const std::string& path, uint32_t sample_rate,
                 uint16_t channels, const std::vector<float>& data,
                 std::string& err);

} // namespace deluge_sim

// =====================================================================
// deluge-sim / board_clock.hpp — Stubs de reloj del board (fase 2)
// En el firmware real estos números salen del reloj de audio del M7
// (I2S/SAI + timers DWT). En el sim son constantes deterministas para
// que el lazo del board se comporte como audio de hardware fijo.
// Capa app: SDL-free, testeable headless.
// =====================================================================
#pragma once
#include <cstddef>

namespace deluge_sim {
namespace board {

constexpr double kBoardSampleRate = 48000.0;
constexpr size_t  kHardwareBlockFrames = 128;   // marcos por "interrupción" simulada

struct BoardClock {
    double sample_rate = kBoardSampleRate;
    size_t hw_block = kHardwareBlockFrames;

    // Segundos de audio contenidos en 'frames' marcos.
    double audio_seconds(size_t frames) const { return double(frames) / sample_rate; }

    // Duración de UN bloque hardware (la cadencia fija del board).
    double block_ms() const { return audio_seconds(hw_block) * 1000.0; }

    // Marcos necesarios para 'seconds' (redondeo al marco más próximo).
    size_t frames_for(double seconds) const {
        return (seconds <= 0.0) ? 0 : size_t(seconds * sample_rate + 0.5);
    }
};

} // namespace board
} // namespace deluge_sim

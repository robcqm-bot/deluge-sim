// =====================================================================
// deluge-sim / board_clock.hpp — Board clock stubs (phase 2)
// In the real firmware these numbers come from the M7 audio clock
// (I2S/SAI + DWT timers). In the sim they are deterministic constants so
// the board loop behaves like fixed-hardware audio.
// App layer: SDL-free, testable headless.
// =====================================================================
#pragma once
#include <cstddef>

namespace deluge_sim {
namespace board {

constexpr double kBoardSampleRate = 48000.0;
constexpr size_t  kHardwareBlockFrames = 128;   // frames per simulated "interrupt"

struct BoardClock {
    double sample_rate = kBoardSampleRate;
    size_t hw_block = kHardwareBlockFrames;

    // Seconds of audio contained in 'frames' frames.
    double audio_seconds(size_t frames) const { return double(frames) / sample_rate; }

    // Duration of ONE hardware block (the board's fixed cadence).
    double block_ms() const { return audio_seconds(hw_block) * 1000.0; }

    // Frames needed for 'seconds' (rounded to the nearest frame).
    size_t frames_for(double seconds) const {
        return (seconds <= 0.0) ? 0 : size_t(seconds * sample_rate + 0.5);
    }
};

} // namespace board
} // namespace deluge_sim

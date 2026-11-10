// =====================================================================
// deluge-sim / board_core.hpp — SDL-free board logic (phase 2)
// Three SDL-free pieces, testable headless:
//   SdCard    : "SD card" = host folder (songs = readable *.wav)
//   PadGrid   : 8x8 pad grid (state, toggles)
//   BoardPump : FIXED-CADENCE playback loop (hardware blocks)
//
// Architecture rule: like the rest of the app layer, this module
// may know deluge_sim and suite_core but NEVER includes SDL — the
// graphical shell (board_main.cpp) is the only piece that touches SDL2.
// =====================================================================
#pragma once
#include "board_clock.hpp"
#include "deluge_sim/core.hpp"
#include "deluge_sim/engine.hpp"
#include "deluge_sim/wav_io.hpp"

#include <string>
#include <vector>

namespace deluge_sim {
namespace board {

// ------------------------------ SdCard -------------------------------
// v0.7: the SD card is a host folder; songs are the *.wav files and
// chain presets are the *.json files (ChainPreset format) directly
// readable from it.
class SdCard {
public:
    // Scans *.wav and *.json (case-insensitive extension) in 'folder',
    // each list sorted alphabetically by path. Does not open the files
    // (that happens on selection). false with an actionable 'err' if
    // the folder does not exist.
    bool mount(const std::string& folder, std::string& err);
    void unmount() { mounted_ = false; songs_.clear(); presets_.clear(); folder_.clear(); }

    bool mounted() const { return mounted_; }
    const std::string& folder() const { return folder_; }
    size_t song_count() const { return songs_.size(); }
    size_t preset_count() const { return presets_.size(); }
    const std::vector<std::string>& songs() const { return songs_; }      // .wav paths
    const std::vector<std::string>& presets() const { return presets_; }  // .json paths

private:
    bool mounted_ = false;
    std::string folder_;
    std::vector<std::string> songs_;
    std::vector<std::string> presets_;
};

// ------------------------------ PadGrid ------------------------------
// 8x8 grid like the panel's RGB grid. Fixed v0.2 semantics:
//   pad row 0, col 0 = PLAY/STOP    (lit = playing)
//   pad row 0, col 1 = chain BYPASS (lit = chain bypassed)
//   rest             = free pads (persistent visual state only)
constexpr int kPadCols = 8;
constexpr int kPadRows = 8;

struct PadGrid {
    bool lit[kPadRows][kPadCols] = {};

    bool is_lit(int row, int col) const;   // false if out of range
    bool toggle(int row, int col);         // toggles and returns the new state; false if out of range
    void all_off();

    bool is_playing() const { return lit[0][0]; }
    bool is_bypassed() const { return lit[0][1]; }
    void set_playing(bool on) { lit[0][0] = on; }
    void set_bypassed(bool on) { lit[0][1] = on; }
};

// ------------------------------ BoardPump ----------------------------
// INTERACTIVE counterpart of Engine::run (which is offline batch): same
// FileAudioSource, same IProcessor contract, same non-finite guard —
// but ALWAYS delivering fixed hardware-size blocks (clock.hw_block),
// like the board's audio interrupt. The UI pumps on demand to fill the
// device queue; no threads and no allocations during render here.
//
// load_song contract: the WavData must stay ALIVE while the song is
// loaded (same contract as FileAudioSource; the shell guarantees the
// song is kept in memory).
class BoardPump {
public:
    explicit BoardPump(const BoardClock& clock = BoardClock());

    // The chain is processed in insertion order. Bypass skips it without
    // tearing it down (set_bypass). clear_processors allows rebuilding the
    // chain (e.g. on preset change) without throwing away the pump.
    void add_processor(ProcessorPtr p);
    size_t processor_count() const { return chain_.size(); }
    void clear_processors() { chain_.clear(); }
    void set_bypass(bool on) { bypass_ = on; }
    bool bypassed() const { return bypass_; }

    // Loads the song to play (1-2 channels; the sample rate must match
    // the clock's: resampling is future work). Leaves the pump stopped
    // and clears telemetry. false with an actionable 'err'.
    bool load_song(const WavData& song, std::string& err);

    void start() { playing_ = true; }
    void stop() { playing_ = false; }
    bool is_playing() const { return playing_; }

    // Fixed cadence: each call delivers EXACTLY clock.hw_block frames
    // except for the final stretch of the song (delivers the remainder
    // and auto-stops). Returns 0 if not playing or the song has ended.
    // 'out' receives INTERLEAVED float (2 channels); required minimum
    // capacity: clock.hw_block * 2 (out_frames_cap is honored).
    size_t pump(float* out, size_t out_frames_cap);

    // Telemetry accumulated since load_song (frames, blocks, guard).
    const RunReport& stats() const { return stats_; }

private:
    BoardClock clock_;
    std::vector<ProcessorPtr> chain_;
    std::unique_ptr<FileAudioSource> src_;   // recreated by load_song
    const WavData* song_ = nullptr;
    std::vector<float> bufL_, bufR_;         // block memory (allocated once)
    Block blk_;
    bool playing_ = false;
    bool bypass_ = false;
    RunReport stats_;
};

} // namespace board
} // namespace deluge_sim

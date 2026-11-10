// =====================================================================
// deluge-sim / core.hpp — Core types and contracts
// v0.1 (offline harness)
//
// Patterns: Strategy (IProcessor), Pipeline (Engine), Adapter (in main).
// Architecture rule: nothing in this header knows about suite_core and
// vice versa; the only meeting point is the adapter in main.cpp.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deluge_sim {

// --- Simulator constants (v0.1: offline 48k stereo harness) ---
constexpr double kDefaultSampleRate = 48000.0;
constexpr size_t kDefaultMaxBlock = 512;   // frames per render block
constexpr size_t kMaxChannels = 2;         // v0.1: mono or stereo

// --- Audio block (planar: L and R channels kept separate) ---
// Memory is owned by the Engine; processors must NOT reallocate it.
struct Block {
    float* L = nullptr;        // left channel samples
    float* R = nullptr;        // right channel samples (== L in mono)
    size_t frames = 0;         // valid frames in this block
    size_t channels = 2;       // 1 = mono (R aliases L), 2 = stereo
};

// --- Preparation context (fixed after prepare(); immutable during render) ---
struct ProcessContext {
    double sample_rate = kDefaultSampleRate;
    size_t max_block = kDefaultMaxBlock;
    size_t channels = kMaxChannels;
};

// --- Processor contract (Strategy) ---
// Contract: process() must not allocate memory, must not throw, and must
// handle non-finite inputs internally (see suite_core/fet76_ref).
class IProcessor {
public:
    virtual ~IProcessor() = default;

    // Stable short name (reports, presets, logs).
    virtual const char* name() const = 0;

    // Called once before rendering. ctx stays fixed for the whole run.
    virtual void prepare(const ProcessContext& ctx) = 0;

    // Processes the block in-place. frames <= ctx.max_block guaranteed.
    virtual void process(Block& b) = 0;

    // Returns to the startup state (parameters untouched).
    virtual void reset() = 0;
};

using ProcessorPtr = std::unique_ptr<IProcessor>;

// --- Report for one render run ---
struct RunReport {
    size_t frames_rendered = 0;
    size_t blocks_rendered = 0;
    size_t nonfinite_input_fixed = 0;   // non-finite samples replaced with 0
    double wall_seconds = 0.0;          // wall-clock render time
    double realtime_factor = 0.0;       // audio_seconds / wall_seconds
    std::string warning;                // empty = no warnings
};

} // namespace deluge_sim

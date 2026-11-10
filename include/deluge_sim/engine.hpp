// =====================================================================
// deluge-sim / engine.hpp — Render pipeline and sources/sinks
// Data flow: IAudioSource -> [IProcessor x N] -> WavAudioSink
// Engine accepts ANY IAudioSource: in RAM (FileAudioSource) or streamed
// from disk (StreamWavSource, see deluge_sim/stream_source.hpp).
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "deluge_sim/wav_io.hpp"
#include <chrono>
#include <functional>

namespace deluge_sim {

// --- Audio source interface (Strategy) ---
// Contract: read() fills 'b' (planar L/R, mono folded to stereo) with up
// to max_frames frames and returns false when the stream is exhausted. Any
// implementation works for Engine::run: in RAM (FileAudioSource) or
// streamed from disk (StreamWavSource, for hour-long renders without
// loading the whole file into memory).
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool read(Block& b, size_t max_frames) = 0;   // false = end
};

// --- Source: serves blocks from a WavData already loaded in RAM ---
// v0.1: whole file in RAM (small test renders). For large files use
// StreamWavSource (stream_source.hpp), which reads block by block from
// disk; both implement IAudioSource.
class FileAudioSource : public IAudioSource {
public:
    explicit FileAudioSource(const WavData& data);

    // Accepted on load: 1 channel (folded to stereo) or 2 channels.
    // Other counts must be rejected BEFORE constructing (see WavData).
    bool read(Block& b, size_t max_frames) override;   // false = end of stream

private:
    const WavData& data_;
    size_t pos_ = 0;                          // next frame to deliver
};

// --- Sink: accumulates and writes PCM16 at the end ---
class WavAudioSink {
public:
    WavAudioSink(uint32_t sample_rate, uint16_t channels);
    void write(const Block& b);
    // Writes the file. Returns false with 'err' filled on disk failure.
    bool finalize(const std::string& path, std::string& err);

private:
    uint32_t sample_rate_;
    uint16_t channels_;
    std::vector<float> acc_;                  // interleaved
};

// --- Engine: orchestrates the full run ---
class Engine {
public:
    Engine(double sample_rate = kDefaultSampleRate,
           size_t max_block = kDefaultMaxBlock);

    // The chain is processed in insertion order (insert → master).
    void add_processor(ProcessorPtr p);
    size_t processor_count() const { return chain_.size(); }

    // Prepares all processors and runs source -> chain -> sink.
    // Accepts any IAudioSource (RAM or streamed from disk).
    // Does not throw: failures come back in 'err' (and RunReport is partial).
    bool run(IAudioSource& src, WavAudioSink& sink,
             RunReport& report, std::string& err);

private:
    double sample_rate_;
    size_t max_block_;
    std::vector<ProcessorPtr> chain_;
    std::vector<float> bufL_, bufR_;          // block buffers (allocated once)
};

} // namespace deluge_sim

// =====================================================================
// deluge-sim / stream_source.hpp — Streaming WAV source from disk
// Reads the WAV block by block (without loading the whole file into RAM)
// to support hour-long renders. Same read semantics as
// FileAudioSource (planar L/R, mono->stereo, partial final block) and
// same header validation rules as read_wav.
// =====================================================================
#pragma once
#include "deluge_sim/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace deluge_sim {

// Audio source that decodes the WAV incrementally from disk.
// Handles PCM16/24/32 and float32 formats (and WAVE_FORMAT_EXTENSIBLE
// via its subformat), just like read_wav.
class StreamWavSource : public IAudioSource {
public:
    // Opens 'path' and validates the RIFF header with the same rules as
    // read_wav: 'fmt ' chunk required, truncated 'data' chunk is clipped
    // to the real file size, sample rate 1..768000 (0 or absurd =>
    // rejection), WAVE_FORMAT_EXTENSIBLE resolved via its subformat.
    // Accepts 1 or 2 channels: 1 channel is folded to stereo on read, 2
    // pass through; more than 2 => the constructor fails with an
    // actionable 'err'. On failure the object is left unusable (read()
    // always returns false) and 'err' explains the reason.
    StreamWavSource(const std::string& path, std::string& err);

    // Same as FileAudioSource::read: fills b (planar L/R, mono->stereo)
    // with up to max_frames frames; false = end of stream. Reads from disk
    // ONLY the necessary bytes (internal ~64 KiB buffer reserved once,
    // no per-sample allocations). If the 'data' chunk declared more bytes
    // than the file has, delivers the existing frames and the first time
    // it is exhausted returns false leaving in error() a message that
    // mentions "truncado" (documented behavior).
    bool read(Block& b, size_t max_frames) override;

    uint32_t sample_rate() const { return sample_rate_; }
    uint16_t channels() const { return channels_; }   // channels in the file
    size_t frames_total() const { return frames_total_; }

    // Last read error (non-empty only after detecting truncation or an
    // I/O failure mid-stream).
    const std::string& error() const { return err_; }

private:
    bool fill_buffer();          // brings the next chunk of frames into buf_

    std::ifstream file_;
    std::vector<uint8_t> buf_;        // read buffer (single allocation)
    size_t buf_frames_cap_ = 0;       // frames that fit in buf_
    size_t buf_frames_ = 0;           // valid frames in buf_
    size_t buf_pos_ = 0;              // next frame to consume from buf_
    uint64_t data_remaining_ = 0;     // bytes of 'data' still on disk
    uint64_t data_declared_ = 0;      // bytes declared by the 'data' chunk
    uint64_t data_size_ = 0;          // bytes actually available (clamped)
    size_t frame_bytes_ = 0;          // channels * bytes_per_sample
    size_t frames_total_ = 0;         // deliverable frames (as read_wav)
    size_t frames_done_ = 0;          // frames already delivered
    uint16_t bits_ = 0;               // bits per sample (16/24/32)
    uint16_t bytes_per_ = 0;          // bits/8
    uint16_t format_ = 0;             // 1 = PCM, 3 = float (after subformat)
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    bool usable_ = false;             // false if the constructor failed
    bool truncated_ = false;          // 'data' declared more than exists
    bool io_failed_ = false;          // short I/O mid-stream
    bool trunc_reported_ = false;     // err_ already set (first time only)
    std::string err_;
};

} // namespace deluge_sim

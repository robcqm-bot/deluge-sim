// =====================================================================
// deluge-sim / engine.cpp — Pipeline: fuente -> cadena -> sumidero
// =====================================================================
#include "deluge_sim/engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace deluge_sim {

// ----------------------------- FileAudioSource ----------------------
FileAudioSource::FileAudioSource(const WavData& data) : data_(data) {}

bool FileAudioSource::read(Block& b, size_t max_frames) {
    const size_t total = data_.frames();
    if (pos_ >= total || data_.channels > kMaxChannels) return false;  // fin / no soportado
    const size_t n = std::min(max_frames, total - pos_);
    const uint16_t ch = data_.channels;
    const float* s = data_.samples.data() + size_t(pos_) * ch;
    if (ch == 1) {
        for (size_t i = 0; i < n; ++i) { b.L[i] = s[i]; b.R[i] = s[i]; }  // mono->estéreo
        b.channels = 2;
    } else {
        for (size_t i = 0; i < n; ++i) { b.L[i] = s[2 * i]; b.R[i] = s[2 * i + 1]; }
        b.channels = 2;
    }
    b.frames = n;
    pos_ += n;
    return true;
}

// ----------------------------- WavAudioSink --------------------------
WavAudioSink::WavAudioSink(uint32_t sample_rate, uint16_t channels)
    : sample_rate_(sample_rate), channels_(channels) {
    acc_.reserve(size_t(sample_rate) * channels);   // 1 s de reserva inicial
}

void WavAudioSink::write(const Block& b) {
    const size_t n = b.frames;
    if (channels_ == 1 || b.channels == 1) {
        for (size_t i = 0; i < n; ++i) acc_.push_back(b.L[i]);       // colapso mono
    } else {
        for (size_t i = 0; i < n; ++i) {
            acc_.push_back(b.L[i]);
            acc_.push_back(b.R[i]);
        }
    }
}

bool WavAudioSink::finalize(const std::string& path, std::string& err) {
    return write_wav16(path, sample_rate_, channels_, acc_, err);
}

// -------------------------------- Engine -----------------------------
Engine::Engine(double sample_rate, size_t max_block)
    : sample_rate_(sample_rate > 0 ? sample_rate : kDefaultSampleRate),
      max_block_(max_block == 0 ? kDefaultMaxBlock : max_block),
      bufL_(max_block_), bufR_(max_block_) {}

void Engine::add_processor(ProcessorPtr p) {
    if (p) chain_.push_back(std::move(p));
}

bool Engine::run(IAudioSource& src, WavAudioSink& sink,
                 RunReport& report, std::string& err) {
    ProcessContext ctx{sample_rate_, max_block_, kMaxChannels};
    for (auto& p : chain_) {
        try {
            p->prepare(ctx);
        } catch (const std::exception& e) {
            err = std::string("prepare falló en '") + p->name() + "': " + e.what();
            return false;
        }
    }

    Block b;
    b.L = bufL_.data();
    b.R = bufR_.data();
    const auto t0 = std::chrono::steady_clock::now();

    while (src.read(b, max_block_)) {
        // Guarda de entrada: sustituye no-finitos por 0 (defensa en profundidad;
        // los procesadores además tienen su propio guardia interno).
        for (size_t i = 0; i < b.frames; ++i) {
            if (!std::isfinite(b.L[i])) { b.L[i] = 0.0f; ++report.nonfinite_input_fixed; }
            if (!std::isfinite(b.R[i])) { b.R[i] = 0.0f; ++report.nonfinite_input_fixed; }
        }
        for (auto& p : chain_) {
            try {
                p->process(b);
            } catch (const std::exception& e) {
                err = std::string("process falló en '") + p->name() +
                      "' en el bloque " + std::to_string(report.blocks_rendered) +
                      ": " + e.what();
                return false;
            }
        }
        sink.write(b);
        ++report.blocks_rendered;
        report.frames_rendered += b.frames;
    }

    const auto t1 = std::chrono::steady_clock::now();
    report.wall_seconds = std::chrono::duration<double>(t1 - t0).count();
    const double audio_seconds = double(report.frames_rendered) / sample_rate_;
    report.realtime_factor =
        (report.wall_seconds > 1e-9) ? audio_seconds / report.wall_seconds : 0.0;
    if (report.frames_rendered == 0) {
        report.warning = "entrada vacía (0 marcos): se produjo un WAV solo-cabecera";
    }
    return true;
}

} // namespace deluge_sim

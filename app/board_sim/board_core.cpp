// =====================================================================
// deluge-sim / board_core.cpp — Implementación SDL-free del board
// =====================================================================
#include "board_core.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace deluge_sim {
namespace board {

// ------------------------------ SdCard -------------------------------
bool SdCard::mount(const std::string& folder, std::string& err) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        err = "carpeta SD no encontrada: '" + folder + "'";
        return false;
    }
    songs_.clear();
    presets_.clear();
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".wav") songs_.push_back(e.path().string());
        else if (ext == ".json") presets_.push_back(e.path().string());
    }
    std::sort(songs_.begin(), songs_.end());
    std::sort(presets_.begin(), presets_.end());
    if (songs_.empty()) {
        err = "la carpeta SD no contiene ningún .wav: '" + folder + "'";
        return false;
    }
    folder_ = folder;
    mounted_ = true;
    return true;
}

// ------------------------------ PadGrid ------------------------------
bool PadGrid::is_lit(int row, int col) const {
    if (row < 0 || row >= kPadRows || col < 0 || col >= kPadCols) return false;
    return lit[row][col];
}

bool PadGrid::toggle(int row, int col) {
    if (row < 0 || row >= kPadRows || col < 0 || col >= kPadCols) return false;
    lit[row][col] = !lit[row][col];
    return lit[row][col];
}

void PadGrid::all_off() {
    for (int r = 0; r < kPadRows; ++r)
        for (int c = 0; c < kPadCols; ++c) lit[r][c] = false;
}

// ------------------------------ BoardPump ----------------------------
BoardPump::BoardPump(const BoardClock& clock) : clock_(clock) {
    if (clock_.hw_block == 0) clock_.hw_block = kHardwareBlockFrames;
    if (clock_.sample_rate <= 0.0) clock_.sample_rate = kBoardSampleRate;
    bufL_.assign(clock_.hw_block, 0.0f);
    bufR_.assign(clock_.hw_block, 0.0f);
}

void BoardPump::add_processor(ProcessorPtr p) {
    if (p) chain_.push_back(std::move(p));
}

bool BoardPump::load_song(const WavData& song, std::string& err) {
    stop();
    if (song.channels == 0 || song.channels > kMaxChannels) {
        err = "el board v0.2 acepta 1 o 2 canales (canción tiene " +
              std::to_string(song.channels) + ")";
        return false;
    }
    if (std::fabs(double(song.sample_rate) - clock_.sample_rate) > 0.5) {
        err = "canción a " + std::to_string(song.sample_rate) +
              " Hz; el board corre a " + std::to_string(unsigned(clock_.sample_rate)) +
              " Hz (resample: fase futura)";
        return false;
    }

    // Prepara la cadena con el contexto FIJO del board (como el engine).
    ProcessContext ctx{clock_.sample_rate, clock_.hw_block, kMaxChannels};
    for (auto& p : chain_) {
        try {
            p->prepare(ctx);
        } catch (const std::exception& e) {
            err = std::string("prepare falló en '") + p->name() + "': " + e.what();
            return false;
        }
    }

    song_ = &song;
    src_ = std::make_unique<FileAudioSource>(song);
    blk_.L = bufL_.data();
    blk_.R = bufR_.data();
    stats_ = RunReport{};
    return true;
}

size_t BoardPump::pump(float* out, size_t out_frames_cap) {
    if (!playing_ || !src_ || out == nullptr) return 0;

    const size_t n = std::min(clock_.hw_block, out_frames_cap);
    blk_.frames = 0;
    blk_.channels = 2;
    if (!src_->read(blk_, n)) {          // fin de la canción: auto-stop
        stop();
        return 0;
    }

    // Guarda de entrada (misma defensa en profundidad que el engine).
    for (size_t i = 0; i < blk_.frames; ++i) {
        if (!std::isfinite(blk_.L[i])) { blk_.L[i] = 0.0f; ++stats_.nonfinite_input_fixed; }
        if (!std::isfinite(blk_.R[i])) { blk_.R[i] = 0.0f; ++stats_.nonfinite_input_fixed; }
    }

    if (!bypass_) {
        for (auto& p : chain_) {
            // El contrato IProcessor no lanza; por defensa se captura
            // igual que en Engine::run (corrida aborta limpia).
            try {
                p->process(blk_);
            } catch (const std::exception& e) {
                stop();
                stats_.warning = std::string("process falló en '") + p->name() +
                                 "': " + e.what();
                return 0;
            }
        }
    }

    for (size_t i = 0; i < blk_.frames; ++i) {     // planar -> intercalado
        out[2 * i] = blk_.L[i];
        out[2 * i + 1] = blk_.R[i];
    }
    ++stats_.blocks_rendered;
    stats_.frames_rendered += blk_.frames;
    return blk_.frames;
}

} // namespace board
} // namespace deluge_sim

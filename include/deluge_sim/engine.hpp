// =====================================================================
// deluge-sim / engine.hpp — Pipeline de render y fuentes/sumideros
// Data flow: FileAudioSource -> [IProcessor x N] -> WavAudioSink
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "deluge_sim/wav_io.hpp"
#include <chrono>
#include <functional>

namespace deluge_sim {

// --- Source: serves blocks from an in-memory WavData ---
// v0.1: whole file in RAM (test renders are small). Streaming from disk
// for large files is on the roadmap (see TODO.md).
class FileAudioSource {
public:
    explicit FileAudioSource(const WavData& data);

    // Aceptado en carga: 1 canal (se pliega a estéreo) o 2 canales.
    // Otros recuentos deben rechazarse ANTES de construir (ver WavData).
    bool read(Block& b, size_t max_frames);   // false = fin de stream

private:
    const WavData& data_;
    size_t pos_ = 0;                          // marco siguiente a entregar
};

// --- Sumidero: acumula y escribe PCM16 al finalizar ---
class WavAudioSink {
public:
    WavAudioSink(uint32_t sample_rate, uint16_t channels);
    void write(const Block& b);
    // Escribe el archivo. Devuelve false con 'err' lleno en fallo de disco.
    bool finalize(const std::string& path, std::string& err);

private:
    uint32_t sample_rate_;
    uint16_t channels_;
    std::vector<float> acc_;                  // intercalado
};

// --- Motor: orquesta la corrida completa ---
class Engine {
public:
    Engine(double sample_rate = kDefaultSampleRate,
           size_t max_block = kDefaultMaxBlock);

    // La cadena se procesa en orden de inserción (insert → master).
    void add_processor(ProcessorPtr p);
    size_t processor_count() const { return chain_.size(); }

    // Prepara todos los procesadores y ejecuta source -> chain -> sink.
    // No lanza: los fallos vuelven en 'err' (y RunReport queda parcial).
    bool run(FileAudioSource& src, WavAudioSink& sink,
             RunReport& report, std::string& err);

private:
    double sample_rate_;
    size_t max_block_;
    std::vector<ProcessorPtr> chain_;
    std::vector<float> bufL_, bufR_;          // memoria de bloques (una vez)
};

} // namespace deluge_sim

// =====================================================================
// deluge-sim / engine.hpp — Pipeline de render y fuentes/sumideros
// Data flow: IAudioSource -> [IProcessor x N] -> WavAudioSink
// Engine acepta CUALQUIER IAudioSource: en RAM (FileAudioSource) o en
// streaming desde disco (StreamWavSource, ver deluge_sim/stream_source.hpp).
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "deluge_sim/wav_io.hpp"
#include <chrono>
#include <functional>

namespace deluge_sim {

// --- Interfaz de fuente de audio (Strategy) ---
// Contrato: read() llena 'b' (planar L/R, mono se pliega a estéreo) con
// hasta max_frames marcos y devuelve false al agotar el stream. Cualquier
// implementación sirve para Engine::run: en RAM (FileAudioSource) o en
// streaming desde disco (StreamWavSource, para renders de horas sin
// cargar el archivo completo a memoria).
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool read(Block& b, size_t max_frames) = 0;   // false = fin
};

// --- Source: sirve bloques desde un WavData ya cargado en RAM ---
// v0.1: fichero completo en RAM (renders de prueba pequeños). Para
// ficheros grandes usa StreamWavSource (stream_source.hpp), que lee por
// bloques desde disco; ambas implementan IAudioSource.
class FileAudioSource : public IAudioSource {
public:
    explicit FileAudioSource(const WavData& data);

    // Aceptado en carga: 1 canal (se pliega a estéreo) o 2 canales.
    // Otros recuentos deben rechazarse ANTES de construir (ver WavData).
    bool read(Block& b, size_t max_frames) override;   // false = fin de stream

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
    // Acepta cualquier IAudioSource (RAM o streaming desde disco).
    // No lanza: los fallos vuelven en 'err' (y RunReport queda parcial).
    bool run(IAudioSource& src, WavAudioSink& sink,
             RunReport& report, std::string& err);

private:
    double sample_rate_;
    size_t max_block_;
    std::vector<ProcessorPtr> chain_;
    std::vector<float> bufL_, bufR_;          // memoria de bloques (una vez)
};

} // namespace deluge_sim

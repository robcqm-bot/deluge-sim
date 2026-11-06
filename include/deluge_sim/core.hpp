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

// --- Constantes del simulador (v0.1: harness offline 48k estéreo) ---
constexpr double kDefaultSampleRate = 48000.0;
constexpr size_t kDefaultMaxBlock = 512;   // marcos por bloque de render
constexpr size_t kMaxChannels = 2;         // v0.1: mono o estéreo

// --- Bloque de audio (planar: canal L y canal R separados) ---
// La memoria la posee el Engine; los procesadores NO la realocan.
struct Block {
    float* L = nullptr;        // muestras canal izquierdo
    float* R = nullptr;        // muestras canal derecho (== L en mono)
    size_t frames = 0;         // marcos válidos en este bloque
    size_t channels = 2;       // 1 = mono (R alias de L), 2 = estéreo
};

// --- Contexto de preparación (fijo tras prepare(); inmutable en render) ---
struct ProcessContext {
    double sample_rate = kDefaultSampleRate;
    size_t max_block = kDefaultMaxBlock;
    size_t channels = kMaxChannels;
};

// --- Contrato de procesador (Strategy) ---
// Contrato: process() no debe reservar memoria, no debe lanzar, y debe
// tratar entradas no finitas internamente (ver suite_core/fet76_ref).
class IProcessor {
public:
    virtual ~IProcessor() = default;

    // Nombre corto estable (reportes, presets, logs).
    virtual const char* name() const = 0;

    // Llamado una vez antes del render. ctx queda fijo para toda la run.
    virtual void prepare(const ProcessContext& ctx) = 0;

    // Procesa el bloque in-place. frames <= ctx.max_block garantizado.
    virtual void process(Block& b) = 0;

    // Vuelve al estado de arranque (sin tocar parámetros).
    virtual void reset() = 0;
};

using ProcessorPtr = std::unique_ptr<IProcessor>;

// --- Reporte de una corrida de render ---
struct RunReport {
    size_t frames_rendered = 0;
    size_t blocks_rendered = 0;
    size_t nonfinite_input_fixed = 0;   // muestras no finitas sustituidas por 0
    double wall_seconds = 0.0;          // tiempo de reloj del render
    double realtime_factor = 0.0;       // audio_seconds / wall_seconds
    std::string warning;                // vacío = sin avisos
};

} // namespace deluge_sim

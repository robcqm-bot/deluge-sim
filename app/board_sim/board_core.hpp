// =====================================================================
// deluge-sim / board_core.hpp — Lógica del board sin SDL (fase 2)
// Tres piezas SDL-free, testeables headless:
//   SdCard    : "tarjeta SD" = carpeta del host (songs = *.wav legibles)
//   PadGrid   : rejilla de pads 8x8 (estado, toggles)
//   BoardPump : lazo de reproducción de CADENCIA FIJA (bloques hardware)
//
// Regla de arquitectura: igual que el resto de la capa app, este módulo
// puede conocer deluge_sim y suite_core pero NUNCA incluye SDL — el
// shell gráfico (board_main.cpp) es la única pieza que toca SDL2.
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
// v0.7: la tarjeta SD es una carpeta del host; las canciones son los
// *.wav y los presets de cadena son los *.json (formato ChainPreset)
// directamente legibles en ella.
class SdCard {
public:
    // Escanea *.wav y *.json (extensión insensible a mayúsculas) en
    // 'folder', cada lista ordenada alfabéticamente por ruta. No abre
    // los archivos (eso ocurre al seleccionar). false con 'err'
    // accionable si la carpeta no existe.
    bool mount(const std::string& folder, std::string& err);
    void unmount() { mounted_ = false; songs_.clear(); presets_.clear(); folder_.clear(); }

    bool mounted() const { return mounted_; }
    const std::string& folder() const { return folder_; }
    size_t song_count() const { return songs_.size(); }
    size_t preset_count() const { return presets_.size(); }
    const std::vector<std::string>& songs() const { return songs_; }      // rutas .wav
    const std::vector<std::string>& presets() const { return presets_; }  // rutas .json

private:
    bool mounted_ = false;
    std::string folder_;
    std::vector<std::string> songs_;
    std::vector<std::string> presets_;
};

// ------------------------------ PadGrid ------------------------------
// Rejilla 8x8 como el grid RGB del panel. Semántica v0.2 fija:
//   pad fila 0, col 0 = PLAY/STOP     (encendido = reproduciendo)
//   pad fila 0, col 1 = BYPASS cadena (encendido = cadena en bypass)
//   resto             = pads libres (solo estado visual persistente)
constexpr int kPadCols = 8;
constexpr int kPadRows = 8;

struct PadGrid {
    bool lit[kPadRows][kPadCols] = {};

    bool is_lit(int row, int col) const;   // false si fuera de rango
    bool toggle(int row, int col);         // alterna y devuelve el nuevo estado; false si fuera de rango
    void all_off();

    bool is_playing() const { return lit[0][0]; }
    bool is_bypassed() const { return lit[0][1]; }
    void set_playing(bool on) { lit[0][0] = on; }
    void set_bypassed(bool on) { lit[0][1] = on; }
};

// ------------------------------ BoardPump ----------------------------
// Contraparte INTERACTIVA de Engine::run (que es batch offline): misma
// FileAudioSource, mismo contrato IProcessor, misma guardia no-finitos —
// pero entregando SIEMPRE bloques de tamaño hardware fijo (clock.hw_block),
// como la interrupción de audio de la placa. La UI bombea a demanda para
// llenar la cola del dispositivo; aquí no hay hilos ni allocs en render.
//
// Contrato de load_song: la WavData debe seguir VIVA mientras la canción
// esté cargada (mismo contrato que FileAudioSource; el shell garantiza
// tener la canción en memoria).
class BoardPump {
public:
    explicit BoardPump(const BoardClock& clock = BoardClock());

    // La cadena se procesa en orden de inserción. El bypass la salta sin
    // desmontarla (set_bypass). clear_processors permite reconstruir la
    // cadena (p. ej. al cambiar de preset) sin tirar el pump.
    void add_processor(ProcessorPtr p);
    size_t processor_count() const { return chain_.size(); }
    void clear_processors() { chain_.clear(); }
    void set_bypass(bool on) { bypass_ = on; }
    bool bypassed() const { return bypass_; }

    // Carga la canción a reproducir (1-2 canales; sample rate debe
    // coincidir con el del reloj: el resample es fase futura). Deja el
    // pump parado y limpia telemetría. false con 'err' accionable.
    bool load_song(const WavData& song, std::string& err);

    void start() { playing_ = true; }
    void stop() { playing_ = false; }
    bool is_playing() const { return playing_; }

    // Cadencia fija: cada llamada entrega EXACTAMENTE clock.hw_block
    // marcos salvo el tramo final de la canción (entrega el resto y
    // auto-stop). Devuelve 0 si no está reproduciendo o la canción
    // terminó. 'out' recibe float INTERCALADO (2 canales); capacidad
    // mínima exigida: clock.hw_block * 2 (se respeta out_frames_cap).
    size_t pump(float* out, size_t out_frames_cap);

    // Telemetría acumulada desde load_song (marcos, bloques, guardia).
    const RunReport& stats() const { return stats_; }

private:
    BoardClock clock_;
    std::vector<ProcessorPtr> chain_;
    std::unique_ptr<FileAudioSource> src_;   // recreado por load_song
    const WavData* song_ = nullptr;
    std::vector<float> bufL_, bufR_;         // memoria de bloque (una vez)
    Block blk_;
    bool playing_ = false;
    bool bypass_ = false;
    RunReport stats_;
};

} // namespace board
} // namespace deluge_sim

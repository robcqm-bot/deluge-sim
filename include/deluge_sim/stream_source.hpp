// =====================================================================
// deluge-sim / stream_source.hpp — Fuente WAV en streaming desde disco
// Lee el WAV por bloques (sin cargar el archivo completo a RAM) para
// soportar renders de horas. Misma semántica de lectura que
// FileAudioSource (planar L/R, mono->estéreo, bloque parcial final) y
// mismas reglas de validación de cabecera que read_wav.
// =====================================================================
#pragma once
#include "deluge_sim/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace deluge_sim {

// Fuente de audio que decodifica el WAV incrementalmente desde disco.
// Comporta los formatos PCM16/24/32 y float32 (y WAVE_FORMAT_EXTENSIBLE
// leyendo el subformato), igual que read_wav.
class StreamWavSource : public IAudioSource {
public:
    // Abre 'path' y valida la cabecera RIFF con las mismas reglas que
    // read_wav: chunk 'fmt ' obligatorio, chunk 'data' truncado se recorta
    // al tamaño real del archivo, sample rate 1..768000 (0 o absurdo =>
    // rechazo), WAVE_FORMAT_EXTENSIBLE resuelto por su subformato.
    // Acepta 1 o 2 canales: 1 canal se pliega a estéreo al leer, 2 van
    // directos; más de 2 => el constructor falla con 'err' accionable.
    // En fallo el objeto queda inutilizable (read() devuelve siempre false)
    // y 'err' explica el motivo.
    StreamWavSource(const std::string& path, std::string& err);

    // Igual que FileAudioSource::read: llena b (planar L/R, mono->estéreo)
    // con hasta max_frames marcos; false = fin de stream. Lee del disco
    // SOLO los bytes necesarios (buffer interno de ~64 KiB reservado una
    // vez, sin allocaciones por muestra). Si el chunk 'data' declaraba más
    // bytes de los que el archivo tiene, entrega los marcos existentes y
    // la primera vez que se agota devuelve false dejando en error() un
    // mensaje que menciona "truncado" (comportamiento documentado).
    bool read(Block& b, size_t max_frames) override;

    uint32_t sample_rate() const { return sample_rate_; }
    uint16_t channels() const { return channels_; }   // canales del archivo
    size_t frames_total() const { return frames_total_; }

    // Último error de lectura (no vacío solo tras detectar truncado o un
    // fallo de E/S a mitad de stream).
    const std::string& error() const { return err_; }

private:
    bool fill_buffer();          // trae el siguiente chunk de marcos a buf_

    std::ifstream file_;
    std::vector<uint8_t> buf_;        // buffer de lectura (una sola reserva)
    size_t buf_frames_cap_ = 0;       // marcos que caben en buf_
    size_t buf_frames_ = 0;           // marcos válidos en buf_
    size_t buf_pos_ = 0;              // marco siguiente a consumir de buf_
    uint64_t data_remaining_ = 0;     // bytes de 'data' pendientes en disco
    uint64_t data_declared_ = 0;      // bytes declarados por el chunk 'data'
    uint64_t data_size_ = 0;          // bytes realmente disponibles (clamp)
    size_t frame_bytes_ = 0;          // canales * bytes_por_muestra
    size_t frames_total_ = 0;         // marcos entregables (como read_wav)
    size_t frames_done_ = 0;          // marcos ya entregados
    uint16_t bits_ = 0;               // bits por muestra (16/24/32)
    uint16_t bytes_per_ = 0;          // bits/8
    uint16_t format_ = 0;             // 1 = PCM, 3 = float (tras subformato)
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    bool usable_ = false;             // false si el constructor falló
    bool truncated_ = false;          // 'data' declaraba más de lo que hay
    bool io_failed_ = false;          // E/S corta a mitad de stream
    bool trunc_reported_ = false;     // err_ ya rellenado (solo la 1ª vez)
    std::string err_;
};

} // namespace deluge_sim

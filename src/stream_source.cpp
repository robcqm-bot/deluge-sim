// =====================================================================
// deluge-sim / stream_source.cpp — WAV en streaming: cabecera + decode
// incremental por bloques (sin cargar el archivo a RAM)
// =====================================================================
#include "deluge_sim/stream_source.hpp"

#include <algorithm>
#include <cstring>

namespace deluge_sim {
namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;
constexpr size_t kStreamChunkBytes = 65536;      // ~64 KiB por lectura

// Espejo EXACTO de los helpers de wav_io.cpp: mismas reglas de lectura
// little-endian y de decodificación para que la ruta streaming y la RAM
// (read_wav) produzcan valores bit-idénticos. Mantener en sincronía.
uint16_t rd_u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t rd_u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

float decode_sample(const uint8_t* p, uint16_t bits, uint16_t format) {
    if (format == kFormatFloat && bits == 32) {
        float v;
        std::memcpy(&v, p, 4);
        return v;
    }
    if (bits == 16) {
        int16_t v = int16_t(rd_u16(p));
        return float(v) / 32768.0f;
    }
    if (bits == 24) {
        int32_t v = int32_t(p[0]) | int32_t(p[1]) << 8 | int32_t(p[2]) << 16;
        if (v & 0x800000) v -= 1 << 24;              // signo: extiende a 32 bits
        return float(v) / 8388608.0f;
    }
    if (bits == 32) {
        int32_t v = int32_t(rd_u32(p));
        return float(double(v) / 2147483648.0);
    }
    return 0.0f;
}

} // namespace

// ----------------------------- construcción --------------------------
StreamWavSource::StreamWavSource(const std::string& path, std::string& err) {
    file_.open(path, std::ios::binary);
    if (!file_) { err = "no se pudo abrir: " + path; return; }

    file_.seekg(0, std::ios::end);
    const std::streamoff fsz = file_.tellg();
    if (fsz < 44) {
        err = "archivo demasiado pequeño para ser WAV: " + path;
        return;
    }
    const uint64_t end = uint64_t(fsz);

    file_.seekg(0);
    uint8_t magic[12];
    file_.read(reinterpret_cast<char*>(magic), 12);
    if (!file_ || std::memcmp(magic, "RIFF", 4) != 0 ||
        std::memcmp(magic + 8, "WAVE", 4) != 0) {
        err = "no es un RIFF/WAVE: " + path;
        return;
    }

    // Escaneo tolerante de chunks CON SEEK (solo se leen las cabeceras de
    // 8 bytes y el cuerpo del fmt; el data NO se toca aquí): mismas reglas
    // que read_wav — clamp del tamaño al fin real del archivo y avance con
    // padding a byte par.
    uint64_t pos = 12;
    bool fmt_found = false, data_found = false;
    uint64_t fmt_size = 0, data_off = 0, data_size = 0;   // data_size: clamp
    uint64_t data_declared = 0;                            // sin clamp
    std::vector<uint8_t> fmtbuf;
    uint8_t hdr[8];
    while (pos + 8 <= end) {
        file_.seekg(std::streamoff(pos));
        file_.read(reinterpret_cast<char*>(hdr), 8);
        if (!file_) break;
        const uint32_t size = rd_u32(hdr + 4);
        const uint64_t body = pos + 8;
        const uint64_t avail = (body <= end) ? (end - body) : 0;
        const uint64_t csize = (size > avail) ? avail : size;  // clamp
        if (std::memcmp(hdr, "fmt ", 4) == 0 && csize >= 16) {
            const size_t take = size_t(std::min<uint64_t>(csize, 64));
            fmtbuf.resize(take);
            file_.read(reinterpret_cast<char*>(fmtbuf.data()),
                       std::streamsize(take));
            if (file_) { fmt_found = true; fmt_size = csize; }
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            data_found = true;
            data_off = body;
            data_size = csize;            // clamp: truncado => lo que haya
            data_declared = size;
        }
        pos = body + csize + (csize & 1);  // padding a byte par
    }
    if (!fmt_found) { err = "chunk 'fmt ' no encontrado: " + path; return; }
    if (!data_found || data_size == 0) {
        err = "chunk 'data' vacío o ausente: " + path;
        return;
    }

    // Cabecera fmt (mismas comprobaciones y mensajes que read_wav).
    uint16_t audio_format = rd_u16(fmtbuf.data());
    const uint16_t channels = rd_u16(fmtbuf.data() + 2);
    const uint32_t rate = rd_u32(fmtbuf.data() + 4);
    const uint16_t bits = rd_u16(fmtbuf.data() + 14);
    if (audio_format == kFormatExtensible && fmt_size >= 40 && fmtbuf.size() >= 40) {
        audio_format = rd_u16(fmtbuf.data() + 24);       // SubFormat GUID
    }
    if (rate == 0 || rate > 768000) {
        err = "sample rate inválido: " + std::to_string(rate);
        return;
    }
    if (channels == 0 || channels > 8) {
        err = "recuento de canales inválido: " + std::to_string(channels);
        return;
    }
    const uint16_t bytes_per = uint16_t(bits / 8);
    const bool ok_pcm = (audio_format == kFormatPcm) &&
                        (bits == 16 || bits == 24 || bits == 32);
    const bool ok_flt = (audio_format == kFormatFloat) && (bits == 32);
    if (!ok_pcm && !ok_flt) {
        err = "formato no soportado (format=" + std::to_string(audio_format) +
              ", bits=" + std::to_string(bits) + "). Soportado: PCM16/24/32, float32.";
        return;
    }
    // El sim entrega mono/estéreo (como FileAudioSource); más canales se
    // rechazan AQUÍ, con mensaje accionable.
    if (channels > kMaxChannels) {
        err = "el sim acepta 1 o 2 canales (archivo tiene " +
              std::to_string(channels) +
              "): convierte la pista a mono o estéreo antes de renderizar";
        return;
    }

    // Estado de streaming: solo marcos completos (mismo floor que read_wav).
    frame_bytes_ = size_t(channels) * bytes_per;
    frames_total_ = size_t(data_size / frame_bytes_);
    sample_rate_ = rate;
    channels_ = channels;
    bits_ = bits;
    bytes_per_ = bytes_per;
    format_ = audio_format;
    data_declared_ = data_declared;
    data_size_ = data_size;
    data_remaining_ = uint64_t(frames_total_) * frame_bytes_;
    truncated_ = data_declared > data_size;    // declaraba más de lo que hay

    // Buffer de lectura: una sola reserva, alineada a marcos completos.
    buf_frames_cap_ = std::max<size_t>(1, kStreamChunkBytes / frame_bytes_);
    buf_.resize(buf_frames_cap_ * frame_bytes_);

    file_.clear();
    file_.seekg(std::streamoff(data_off));
    usable_ = true;
}

// ------------------------------ lectura ------------------------------
bool StreamWavSource::fill_buffer() {
    if (io_failed_ || data_remaining_ == 0) return false;
    const size_t want = size_t(std::min<uint64_t>(buf_.size(), data_remaining_));
    file_.clear();
    file_.read(reinterpret_cast<char*>(buf_.data()), std::streamsize(want));
    const size_t got = size_t(file_.gcount());
    data_remaining_ -= got;
    buf_pos_ = 0;
    buf_frames_ = got / frame_bytes_;          // floor: descarta marco cortado
    if (got < want) io_failed_ = true;         // el disco dio menos de lo pedido
    return buf_frames_ > 0;
}

bool StreamWavSource::read(Block& b, size_t max_frames) {
    if (!usable_) return false;
    if (frames_done_ >= frames_total_) {
        // Fin de stream: la única vez que se informa de truncado es aquí
        // (y solo si el chunk 'data' declaraba más bytes de los reales).
        if (truncated_ && !trunc_reported_) {
            trunc_reported_ = true;
            err_ = "WAV truncado: el chunk 'data' declara " +
                   std::to_string(data_declared_) +
                   " bytes pero el archivo solo contiene " +
                   std::to_string(data_size_) + "; se entregaron " +
                   std::to_string(frames_total_) + " marcos completos";
        }
        return false;
    }

    const size_t n = std::min(max_frames, frames_total_ - frames_done_);
    b.channels = 2;                            // mono ya plegado a estéreo
    size_t done = 0;
    while (done < n) {
        if (buf_pos_ >= buf_frames_ && !fill_buffer()) break;
        const size_t avail = buf_frames_ - buf_pos_;
        const size_t take = std::min(avail, n - done);
        const uint8_t* p = buf_.data() + buf_pos_ * frame_bytes_;
        if (channels_ == 1) {
            for (size_t i = 0; i < take; ++i) {
                b.L[done + i] = decode_sample(p + i * frame_bytes_, bits_, format_);
                b.R[done + i] = b.L[done + i];             // mono->estéreo
            }
        } else {
            for (size_t i = 0; i < take; ++i) {
                const uint8_t* fr = p + i * frame_bytes_;
                b.L[done + i] = decode_sample(fr, bits_, format_);
                b.R[done + i] = decode_sample(fr + bytes_per_, bits_, format_);
            }
        }
        done += take;
        buf_pos_ += take;
        frames_done_ += take;
    }
    b.frames = done;

    if (io_failed_ && !trunc_reported_) {
        trunc_reported_ = true;
        err_ = "WAV truncado: la lectura se interrumpió antes de completar "
               "el chunk 'data' (el disco dio menos bytes de los pedidos)";
    }
    if (done < n) return done > 0;   // parcial entregado, o false si no avanzó
    return true;
}

} // namespace deluge_sim

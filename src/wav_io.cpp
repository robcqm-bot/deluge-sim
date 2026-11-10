// =====================================================================
// deluge-sim / wav_io.cpp — RIFF WAV read/write, dependency-free
// =====================================================================
#include "deluge_sim/wav_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace deluge_sim {
namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

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
        if (v & 0x800000) v -= 1 << 24;              // sign: sign-extend to 32 bits
        return float(v) / 8388608.0f;
    }
    if (bits == 32) {
        int32_t v = int32_t(rd_u32(p));
        return float(double(v) / 2147483648.0);
    }
    return 0.0f;
}

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
void put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
}

} // namespace

bool read_wav(const std::string& path, WavData& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "no se pudo abrir: " + path; return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    if (b.size() < 44) { err = "archivo demasiado pequeño para ser WAV: " + path; return false; }
    if (std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        err = "no es un RIFF/WAVE: " + path; return false;
    }

    // Tolerant chunk scan (fmt may come after data in theory).
    size_t pos = 12, end = b.size();
    const uint8_t* fmt_ptr = nullptr; size_t fmt_size = 0;
    size_t data_off = 0, data_size = 0; bool data_found = false;
    while (pos + 8 <= end) {
        const uint8_t* id = b.data() + pos;
        uint32_t size = rd_u32(b.data() + pos + 4);
        size_t body = pos + 8;
        if (body + size > end) size = end - body;       // truncated data -> clamp
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            fmt_ptr = b.data() + body; fmt_size = size;
        } else if (std::memcmp(id, "data", 4) == 0) {
            data_off = body; data_size = size; data_found = true;
        }
        pos = body + size + (size & 1);                 // pad to even byte
    }
    if (!fmt_ptr) { err = "chunk 'fmt ' no encontrado: " + path; return false; }
    if (!data_found || data_size == 0) { err = "chunk 'data' vacío o ausente: " + path; return false; }

    uint16_t audio_format = rd_u16(fmt_ptr);
    uint16_t channels    = rd_u16(fmt_ptr + 2);
    uint32_t rate        = rd_u32(fmt_ptr + 4);
    uint16_t bits        = rd_u16(fmt_ptr + 14);
    if (audio_format == kFormatExtensible && fmt_size >= 40) {
        audio_format = rd_u16(fmt_ptr + 24);            // first field of the SubFormat GUID
    }
    if (rate == 0 || rate > 768000) { err = "sample rate inválido: " + std::to_string(rate); return false; }
    if (channels == 0 || channels > 8) { err = "recuento de canales inválido: " + std::to_string(channels); return false; }

    uint16_t bytes_per = uint16_t(bits / 8);
    bool ok_pcm = (audio_format == kFormatPcm) && (bits == 16 || bits == 24 || bits == 32);
    bool ok_flt = (audio_format == kFormatFloat) && (bits == 32);
    if (!ok_pcm && !ok_flt) {
        err = "formato no soportado (format=" + std::to_string(audio_format) +
              ", bits=" + std::to_string(bits) + "). Soportado: PCM16/24/32, float32.";
        return false;
    }

    size_t frame_bytes = size_t(channels) * bytes_per;
    size_t frames = data_size / frame_bytes;
    out.sample_rate = rate;
    out.channels = channels;
    out.samples.resize(size_t(frames) * channels);
    for (size_t i = 0; i < frames * channels; ++i) {
        out.samples[i] = decode_sample(b.data() + data_off + i * bytes_per, bits, audio_format);
    }
    return true;
}

bool write_wav16(const std::string& path, uint32_t sample_rate,
                 uint16_t channels, const std::vector<float>& data,
                 std::string& err) {
    if (channels == 0 || channels > 8) { err = "canal inválido al escribir"; return false; }
    size_t frames = data.size() / channels;
    size_t data_bytes = frames * size_t(channels) * 2;

    std::vector<uint8_t> hdr;
    hdr.reserve(44);
    hdr.insert(hdr.end(), {'R','I','F','F'});
    put_u32(hdr, uint32_t(36 + data_bytes));
    hdr.insert(hdr.end(), {'W','A','V','E','f','m','t',' '});
    put_u32(hdr, 16);
    put_u16(hdr, kFormatPcm);
    put_u16(hdr, channels);
    put_u32(hdr, sample_rate);
    put_u32(hdr, sample_rate * channels * 2);   // byte rate
    put_u16(hdr, uint16_t(channels * 2));       // block align
    put_u16(hdr, 16);                           // bits
    hdr.insert(hdr.end(), {'d','a','t','a'});
    put_u32(hdr, uint32_t(data_bytes));

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { err = "no se pudo crear: " + path; return false; }
    f.write(reinterpret_cast<const char*>(hdr.data()), std::streamsize(hdr.size()));
    for (size_t i = 0; i < frames * channels; ++i) {
        float s = std::isfinite(data[i]) ? data[i] : 0.0f;   // final defense
        long v = std::lrint(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        uint16_t u = uint16_t(int16_t(v));
        uint8_t two[2] = { uint8_t(u), uint8_t(u >> 8) };
        f.write(reinterpret_cast<const char*>(two), 2);
    }
    if (!f) { err = "escritura fallida (¿disco lleno?): " + path; return false; }
    return true;
}

} // namespace deluge_sim

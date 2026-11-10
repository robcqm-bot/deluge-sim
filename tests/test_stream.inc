// =====================================================================
// test_stream.inc — pruebas de StreamWavSource (fuente WAV en streaming)
// CONTRATO: se incluye VERBATIM dentro del namespace anónimo de
// tests/test_all.cpp DESPUÉS de check() y make_sine(). Por tanto: sin
// #include, sin main(), sin check propio; solo funciones test_stream_*
// y helpers internos (prefijo ts_ para evitar colisiones).
// Include que el integrador debe añadir a test_all.cpp:
//     #include "deluge_sim/stream_source.hpp"
// (los <std> usados ya están: cmath cstdint filesystem fstream iostream
//  limits string system_error vector; make_unique llega vía core.hpp).
// =====================================================================

// --------------------------- helpers ts_ -----------------------------

// Escrituras little-endian para fabricar WAVs a mano (solo en tests).
void ts_put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void ts_put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 24));
}

// Punta de float<->bits para serializar float32 sin <cstring> (C++17):
// la unión preserva el patrón de bits exacto de cada muestra.
union TsFloatBits {
    float f;
    uint32_t u;
};

// Buffers planares reutilizables (Block no posee memoria; los vectores
// se reservan una vez y los punteros de b quedan fijos).
struct TsPlanar {
    std::vector<float> L, R;
    deluge_sim::Block b;
    explicit TsPlanar(size_t cap) : L(cap), R(cap) {
        b.L = L.data();
        b.R = R.data();
    }
};

// Consume 'src' en bloques de 'cap' y devuelve el tamaño de cada bloque.
std::vector<size_t> ts_drain(deluge_sim::IAudioSource& src, TsPlanar& bufs,
                             size_t cap, std::vector<float>* outL = nullptr,
                             std::vector<float>* outR = nullptr) {
    std::vector<size_t> sizes;
    for (;;) {
        if (!src.read(bufs.b, cap)) break;
        sizes.push_back(bufs.b.frames);
        if (outL) {
            for (size_t i = 0; i < bufs.b.frames; ++i) {
                outL->push_back(bufs.b.L[i]);
                outR->push_back(bufs.b.R[i]);
            }
        }
    }
    return sizes;
}

// ------------------------------ pruebas ------------------------------

// a) RAM (read_wav) vs streaming (bloques de 512): bit-iguales.
void test_stream_equals_ram() {
    std::cout << "[test] stream: igualdad bit-exacta RAM vs streaming (PCM16 3 s)\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_ram.wav").string();
    const size_t frames = 48000 * 3;                        // 3 s @ 48 kHz
    WavData w = make_sine(48000, frames, 0.5);
    std::string err;
    check(deluge_sim::write_wav16(tmp, 48000, 2, w.samples, err),
          "escritura WAV estéreo PCM16 de 3 s");
    WavData ram;
    check(deluge_sim::read_wav(tmp, ram, err) && ram.frames() == frames,
          "lectura RAM (read_wav) completa");
    deluge_sim::StreamWavSource src(tmp, err);
    check(src.sample_rate() == 48000 && src.channels() == 2,
          "cabecera vía streaming: 48000 Hz, 2 canales");
    check(src.frames_total() == frames,
          "frames_total == 144000 (medido " + std::to_string(src.frames_total()) + ")");
    TsPlanar bufs(512);
    std::vector<float> L, R;
    L.reserve(frames);
    R.reserve(frames);
    const std::vector<size_t> sizes = ts_drain(src, bufs, 512, &L, &R);
    size_t got = 0;
    for (size_t s : sizes) got += s;
    check(got == frames && L.size() == frames && R.size() == frames,
          "streaming entrega los 144000 marcos");
    bool exact = true;
    for (size_t i = 0; i < frames && exact; ++i) {
        exact = (L[i] == ram.samples[2 * i]) && (R[i] == ram.samples[2 * i + 1]);
    }
    check(exact, "TODAS las muestras L/R bit-iguales RAM vs streaming (==)");
}

// b) Bloque parcial final y fin de stream: misma secuencia que
//    FileAudioSource. Con max 512: 1500 marcos => 512, 512, 476, false
//    (secuencia del enunciado); 1000 marcos => 512, 488, false.
void test_stream_partial_tail() {
    std::cout << "[test] stream: cola parcial 512+512+476 y fin de stream\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_tail.wav").string();
    WavData w = make_sine(48000, 1500, 0.5);
    std::string err;
    check(deluge_sim::write_wav16(tmp, 48000, 2, w.samples, err),
          "escritura WAV de 1500 marcos");
    deluge_sim::StreamWavSource src(tmp, err);
    TsPlanar bufs(512);
    const std::vector<size_t> sizes = ts_drain(src, bufs, 512);
    check(sizes.size() == 3 && sizes[0] == 512 && sizes[1] == 512 &&
              sizes[2] == 476,
          "secuencia streaming exacta: 512, 512, 476");
    check(!src.read(bufs.b, 512), "lectura tras agotar -> false");
    // Misma secuencia que FileAudioSource sobre los mismos datos.
    deluge_sim::FileAudioSource fsrc(w);
    const std::vector<size_t> fsizes = ts_drain(fsrc, bufs, 512);
    check(fsizes == sizes, "FileAudioSource da la MISMA secuencia de bloques");
    // Caso de 1000 marcos (el enunciado mezclaba 1000 con 512/512/476;
    // 512+512+476 = 1500, así que se cubren ambos): 512, 488, false.
    const std::string tmp2 = (std::filesystem::temp_directory_path() /
                              "deluge_sim_test_stream_tail2.wav").string();
    WavData w2 = make_sine(48000, 1000, 0.5);
    check(deluge_sim::write_wav16(tmp2, 48000, 2, w2.samples, err),
          "escritura WAV de 1000 marcos");
    deluge_sim::StreamWavSource src2(tmp2, err);
    const std::vector<size_t> s2 = ts_drain(src2, bufs, 512);
    check(s2.size() == 2 && s2[0] == 512 && s2[1] == 488,
          "1000 marcos: 512, 488, luego false");
}

// c) Mono: L == R == canal original, bit-exacto contra read_wav.
void test_stream_mono_fold() {
    std::cout << "[test] stream: mono se pliega a estéreo bit-exacto (2 s)\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_mono.wav").string();
    const size_t frames = 48000 * 2;                        // 2 s @ 48 kHz
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                       double(i) / 48000.0));
    }
    std::string err;
    check(deluge_sim::write_wav16(tmp, 48000, 1, mono, err),
          "escritura WAV mono PCM16 de 2 s");
    WavData ram;
    check(deluge_sim::read_wav(tmp, ram, err) && ram.channels == 1 &&
              ram.frames() == frames,
          "lectura RAM mono (1 canal)");
    deluge_sim::StreamWavSource src(tmp, err);
    check(src.channels() == 1, "channels() reporta el canal del archivo (1)");
    check(src.frames_total() == frames, "frames_total == 96000");
    TsPlanar bufs(512);
    std::vector<float> L, R;
    const std::vector<size_t> sizes = ts_drain(src, bufs, 512, &L, &R);
    size_t got = 0;
    for (size_t s : sizes) got += s;
    bool exact = (got == frames) && (L.size() == frames) && (R.size() == frames);
    for (size_t i = 0; i < frames && exact; ++i) {
        exact = (L[i] == ram.samples[i]) && (R[i] == ram.samples[i]) &&
                (L[i] == R[i]);
    }
    check(exact, "L == R == canal original bit-exacto contra read_wav (==)");
}

// d) WAV float32 fabricado a mano (RIFF de 44 bytes, format tag 3):
//    decodificación bit-exacta de los floats generados.
void test_stream_float32() {
    std::cout << "[test] stream: WAV float32 fabricado a mano, bit-exacto\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_f32.wav").string();
    const uint32_t sr = 48000;
    const size_t frames = 4800;
    std::string err;
    std::vector<float> gen(frames * 2);                     // estéreo intercalado
    for (size_t i = 0; i < frames; ++i) {
        const float v = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                             1000.0 * double(i) / double(sr)));
        gen[2 * i] = v;
        gen[2 * i + 1] = v;
    }
    // Cabecera estándar de 44 bytes: format tag 3 (IEEE float), 32 bits.
    const uint32_t data_bytes = uint32_t(frames) * 2 * 4;
    std::vector<uint8_t> f;
    f.reserve(44 + size_t(data_bytes));
    f.insert(f.end(), {'R', 'I', 'F', 'F'});
    ts_put_u32(f, 36 + data_bytes);
    f.insert(f.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    ts_put_u32(f, 16);
    ts_put_u16(f, 3);                                       // format tag float
    ts_put_u16(f, 2);                                       // canales
    ts_put_u32(f, sr);
    ts_put_u32(f, sr * 2 * 4);                              // byte rate
    ts_put_u16(f, 2 * 4);                                   // block align
    ts_put_u16(f, 32);                                      // bits
    f.insert(f.end(), {'d', 'a', 't', 'a'});
    ts_put_u32(f, data_bytes);
    for (size_t i = 0; i < gen.size(); ++i) {
        TsFloatBits fb;
        fb.f = gen[i];
        ts_put_u32(f, fb.u);                                // bits LE exactos
    }
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(f.data()),
                  std::streamsize(f.size()));
        check(bool(out), "escritura del WAV float32 fabricado");
    }
    deluge_sim::StreamWavSource src(tmp, err);
    check(src.sample_rate() == 48000 && src.channels() == 2 &&
              src.frames_total() == frames,
          "cabecera float32 vía streaming correcta");
    TsPlanar bufs(512);
    std::vector<float> L, R;
    ts_drain(src, bufs, 512, &L, &R);
    bool exact = (L.size() == frames) && (R.size() == frames);
    for (size_t i = 0; i < frames && exact; ++i) {
        exact = (L[i] == gen[2 * i]) && (R[i] == gen[2 * i + 1]);
    }
    check(exact, "valores float32 bit-exactos contra los generados (==)");
    // Consistencia cruzada: read_wav decodifica lo mismo (cabecera válida).
    WavData ram;
    check(deluge_sim::read_wav(tmp, ram, err) && ram.frames() == frames &&
              ram.samples[0] == gen[0] && ram.samples[1] == gen[1],
          "read_wav del WAV fabricado coincide (cabecera estándar válida)");
}

// e) WAV truncado a mitad del data: entrega los marcos existentes y luego
//    false con err que menciona "truncado" (sin colgarse ni lanzar).
void test_stream_truncated() {
    std::cout << "[test] stream: WAV truncado a mitad del data -> entrega y false\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_trunc.wav").string();
    WavData w = make_sine(48000, 1000, 0.5);
    std::string err;
    check(deluge_sim::write_wav16(tmp, 48000, 2, w.samples, err),
          "escritura WAV PCM16 válido (1000 marcos)");
    {   // trunca el archivo a la mitad del data: 44 + 2000 bytes => 500 marcos
        std::ifstream in(tmp, std::ios::binary);
        in.seekg(0, std::ios::end);
        const std::streamoff sz = in.tellg();
        in.seekg(0);
        const size_t total = size_t(sz);
        std::vector<char> all(total);                       // variable con nombre: sin vexing parse
        in.read(all.data(), sz);
        in.close();
        const size_t keep = 44 + (all.size() - 44) / 2;
        all.resize(keep);
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(all.data(), std::streamsize(all.size()));
        check(bool(out), "archivo truncado a la mitad del data");
    }
    try {
        deluge_sim::StreamWavSource src(tmp, err);
        check(src.frames_total() == 500,
              "marcos entregables == 500 (clamp como read_wav, medido " +
                  std::to_string(src.frames_total()) + ")");
        WavData back;
        check(deluge_sim::read_wav(tmp, back, err) && back.frames() == 500,
              "read_wav sobre el truncado coincide (500 marcos)");
        TsPlanar bufs(512);
        const std::vector<size_t> sizes = ts_drain(src, bufs, 512);
        check(sizes.size() == 1 && sizes[0] == 500,
              "entrega un bloque de 500 marcos y luego false");
        check(!src.read(bufs.b, 512),
              "lectura extra sigue en false (sin colgarse)");
        const std::string terr = src.error();
        check(terr.find("truncado") != std::string::npos,
              "err no vacío menciona 'truncado': " + terr);
    } catch (...) {
        check(false, "no debía lanzar excepción en ningún punto");
    }
}

// f) Engine::run con FileAudioSource vs StreamWavSource sobre el MISMO
//    WAV PCM16, misma cadena (ganancia x0.5) y mismo bloque: salidas
//    bit-idénticas. Valida la refactor IAudioSource.
class StreamGainHalf : public deluge_sim::IProcessor {
public:
    const char* name() const override { return "stream_gain_half_test"; }
    void prepare(const deluge_sim::ProcessContext&) override {}
    void process(deluge_sim::Block& b) override {
        for (size_t i = 0; i < b.frames; ++i) {
            b.L[i] *= 0.5f;                     // x0.5 exacto en IEEE-754
            if (b.channels == 2) b.R[i] *= 0.5f;
        }
    }
    void reset() override {}
};

void test_stream_engine_bitexact() {
    std::cout << "[test] stream: Engine con FileAudioSource vs StreamWavSource "
                 "bit-exacto\n";
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_stream_engine.wav").string();
    WavData w = make_sine(48000, 5000, 0.5);
    std::string err;
    check(deluge_sim::write_wav16(tmp, 48000, 2, w.samples, err),
          "escritura WAV PCM16 de entrada (5000 marcos)");

    // Ruta A: fuente en RAM.
    WavData ram;
    check(deluge_sim::read_wav(tmp, ram, err), "lectura RAM del mismo WAV");
    deluge_sim::Engine eng_a(48000.0, 512);
    eng_a.add_processor(std::make_unique<StreamGainHalf>());
    deluge_sim::FileAudioSource src_a(ram);
    deluge_sim::WavAudioSink sink_a(48000, 2);
    deluge_sim::RunReport rep_a;
    check(eng_a.run(src_a, sink_a, rep_a, err), "corrida con FileAudioSource");
    check(rep_a.frames_rendered == 5000, "RAM: 5000 marcos renderizados");
    const std::string out_a = (std::filesystem::temp_directory_path() /
                               "deluge_sim_test_stream_out_a.wav").string();
    check(sink_a.finalize(out_a, err), "escritura de la salida A");

    // Ruta B: fuente en streaming desde el MISMO archivo (mismo bloque y
    // misma cadena; el engine acepta cualquier IAudioSource).
    deluge_sim::StreamWavSource src_b(tmp, err);
    check(src_b.frames_total() == 5000, "streaming: 5000 marcos totales");
    deluge_sim::Engine eng_b(48000.0, 512);
    eng_b.add_processor(std::make_unique<StreamGainHalf>());
    deluge_sim::WavAudioSink sink_b(48000, 2);
    deluge_sim::RunReport rep_b;
    check(eng_b.run(src_b, sink_b, rep_b, err), "corrida con StreamWavSource");
    check(rep_b.frames_rendered == 5000, "streaming: 5000 marcos renderizados");
    const std::string out_b = (std::filesystem::temp_directory_path() /
                               "deluge_sim_test_stream_out_b.wav").string();
    check(sink_b.finalize(out_b, err), "escritura de la salida B");

    // Comparación bit a bit de ambas salidas relesídas.
    WavData a, b;
    check(deluge_sim::read_wav(out_a, a, err) &&
              deluge_sim::read_wav(out_b, b, err),
          "relectura de ambas salidas");
    bool exact = (a.frames() == b.frames()) && (a.samples.size() == b.samples.size());
    for (size_t i = 0; i < a.samples.size() && exact; ++i) {
        exact = (a.samples[i] == b.samples[i]);
    }
    check(exact, "salidas bit-idénticas muestra a muestra (refactor IAudioSource)");
}

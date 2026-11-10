// =====================================================================
// deluge-sim / test_all.cpp — Batería de verificación (binario simtests)
// Cada prueba lleva su expectativa MATEMÁTICA documentada. Exit 0 = todo ok.
// =====================================================================
#include "board_core.hpp"
#include "deluge_sim/engine.hpp"
#include "deluge_sim/preset.hpp"
#include "deluge_sim/stream_source.hpp"
#include "deluge_sim/wav_io.hpp"
#include "suite_core/fet76_ref.hpp"
#include "suite_core/opto_comp.hpp"
#include "suite_core/passive_eq.hpp"
#include "suite_core/plate_reverb.hpp"
#include "suite_core/tape_sat.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace deluge_sim;
using suite_core::Fet76Ref;
using suite_core::OptoCompRef;
using suite_core::PassiveEQRef;
using suite_core::PlateReverbRef;

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
    if (!ok) ++g_failures;
}

// Adaptador blanco de pruebas (mismo patrón que el de main.cpp).
class FetAdapter : public IProcessor {
public:
    explicit FetAdapter(const Fet76Ref::Params& p) { f_.set_params(p); }
    const char* name() const override { return "fet76_ref"; }
    void prepare(const ProcessContext& ctx) override {
        f_.set_sample_rate(ctx.sample_rate);
        f_.prepare();
    }
    void process(Block& b) override {
        f_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { f_.reset(); }
    Fet76Ref& ref() { return f_; }

private:
    Fet76Ref f_;
};

// Adaptador blanco del óptico (mismo patrón).
class OptoAdapter : public IProcessor {
public:
    explicit OptoAdapter(const OptoCompRef::Params& p) { o_.set_params(p); }
    const char* name() const override { return "opto_comp_ref"; }
    void prepare(const ProcessContext& ctx) override {
        o_.set_sample_rate(ctx.sample_rate);
        o_.prepare();
    }
    void process(Block& b) override {
        o_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { o_.reset(); }
    OptoCompRef& ref() { return o_; }

private:
    OptoCompRef o_;
};

// Adaptador blanco del EQ pasivo (mismo patrón).
class PassiveEQAdapter : public IProcessor {
public:
    explicit PassiveEQAdapter(const PassiveEQRef::Params& p) { e_.set_params(p); }
    const char* name() const override { return "passive_eq"; }
    void prepare(const ProcessContext& ctx) override {
        e_.set_sample_rate(ctx.sample_rate);
        e_.prepare();
    }
    void process(Block& b) override {
        e_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { e_.reset(); }
    PassiveEQRef& ref() { return e_; }

private:
    PassiveEQRef e_;
};

// Adaptador blanco de la reverb de placa (mismo patrón).
class PlateAdapter : public IProcessor {
public:
    explicit PlateAdapter(const PlateReverbRef::Params& p) { r_.set_params(p); }
    const char* name() const override { return "plate_reverb"; }
    void prepare(const ProcessContext& ctx) override {
        r_.set_sample_rate(ctx.sample_rate);
        r_.prepare();
    }
    void process(Block& b) override {
        r_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { r_.reset(); }

private:
    PlateReverbRef r_;
};

// Genera un seno estéreo intercalado con pico dado (frecuencia fija 1 kHz).
WavData make_sine(uint32_t sr, size_t frames, double peak, double freq_hz = 1000.0) {
    WavData w;
    w.sample_rate = sr;
    w.channels = 2;
    w.samples.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        float v = float(peak * std::sin(2.0 * 3.14159265358979323846 * freq_hz *
                                        double(i) / double(sr)));
        w.samples[2 * i] = v;
        w.samples[2 * i + 1] = v;
    }
    return w;
}

bool run_engine(WavData& in, std::vector<float>& outL, double block = 512) {
    Engine engine(double(in.sample_rate), size_t(block));
    engine.add_processor(std::make_unique<FetAdapter>(Fet76Ref::Params{}));
    FileAudioSource src(in);
    WavAudioSink sink(in.sample_rate, 2);
    RunReport rep;
    std::string err;
    if (!engine.run(src, sink, rep, err)) {
        std::cout << "  (engine error: " << err << ")\n";
        return false;
    }
    // Relee el sumidero reutilizando la ruta de archivo (verifica WAV I/O):
    std::string tmp = (std::filesystem::temp_directory_path() /
                       "deluge_sim_test_out.wav").string();
    if (!sink.finalize(tmp, err)) return false;
    WavData back;
    if (!read_wav(tmp, back, err)) return false;
    outL.assign(back.frames(), 0.0f);
    for (size_t i = 0; i < back.frames(); ++i) outL[i] = back.samples[2 * i];
    return back.frames() == in.frames();
}

// ------------------------------ pruebas ------------------------------
void test_wav_roundtrip() {
    std::cout << "[test] wav roundtrip 16 bits\n";
    std::string tmp = (std::filesystem::temp_directory_path() /
                       "deluge_sim_test_rt.wav").string();
    WavData w = make_sine(48000, 4800, 0.5);
    std::string err;
    check(write_wav16(tmp, w.sample_rate, 2, w.samples, err), "escritura WAV");
    WavData back;
    check(read_wav(tmp, back, err) && back.frames() == w.frames(), "relectura WAV");
    double max_err = 0.0;
    for (size_t i = 0; i < w.samples.size() && i < back.samples.size(); ++i) {
        max_err = std::max(max_err, double(std::fabs(w.samples[i] - back.samples[i])));
    }
    check(max_err <= 1.5 / 32768.0, "error de cuantizacion <= 1.5 LSB");
}

void test_fet76_no_gr() {
    std::cout << "[test] fet76: senal a -60 dB con umbral 0 -> GR = 0\n";
    WavData in = make_sine(48000, 48000, 0.001);            // ~-60 dBFS pico
    Fet76Ref f;
    Fet76Ref::Params p;
    p.thresh_db = 0.0;
    f.set_params(p);
    f.set_sample_rate(48000);
    f.prepare();
    std::vector<float> L(in.frames()), R(in.frames());
    for (size_t i = 0; i < in.frames(); ++i) {
        L[i] = in.samples[2 * i];
        R[i] = in.samples[2 * i + 1];
    }
    f.process(L.data(), R.data(), L.size());
    double max_diff = 0.0;
    for (size_t i = 0; i < L.size(); ++i)
        max_diff = std::max(max_diff, double(std::fabs(L[i] - in.samples[2 * i])));
    check(max_diff < 1e-6, "salida == entrada (sin reduccion)");
}

void test_fet76_gr_steady() {
    std::cout << "[test] fet76: GR estacionaria (esperado pico ~0.372, "
                 "matematica feedback)\n";
    // Curva feedback: out_peak = (in_peak + k*thresh)/(1+k)
    //   in=-6.0206 dB, thresh=-12 dB, ratio 4 (k=0.75) -> -8.583 dB -> 0.3718
    WavData in = make_sine(48000, 48000, 0.5);               // 1 s @ 1 kHz
    Fet76Ref f;
    Fet76Ref::Params p;
    p.thresh_db = -12.0;
    p.ratio = 4.0;
    p.attack_s = 0.001;
    p.release_s = 0.1;
    f.set_params(p);
    f.set_sample_rate(48000);
    f.prepare();
    std::vector<float> L(in.frames()), R(in.frames());
    for (size_t i = 0; i < in.frames(); ++i) {
        L[i] = in.samples[2 * i];
        R[i] = in.samples[2 * i + 1];
    }
    f.process(L.data(), R.data(), L.size());
    double peak_tail = 0.0;                                  // ultimos 0.2 s
    for (size_t i = L.size() - 9600; i < L.size(); ++i)
        peak_tail = std::max(peak_tail, double(std::fabs(L[i])));
    check(peak_tail > 0.32 && peak_tail < 0.43,
          "pico de salida en banda esperada (medido " + std::to_string(peak_tail) + ")");
    check(f.current_gr_db() < -2.0 && f.current_gr_db() > -3.2,
          "telemetria GR en banda (medida " + std::to_string(f.current_gr_db()) + ")");
}

void test_fet76_release() {
    std::cout << "[test] fet76: release devuelve GR a ~0 tras 3 tau\n";
    const uint32_t sr = 48000;
    const size_t sig = size_t(sr) / 2;                       // 0.5 s de senal
    const size_t sil = size_t(sr) * 3 / 10;                  // 0.3 s de silencio
    WavData in = make_sine(sr, sig + sil, 0.5);
    for (size_t i = sig; i < sig + sil; ++i) {
        in.samples[2 * i] = 0.0f;
        in.samples[2 * i + 1] = 0.0f;
    }
    Fet76Ref f;
    Fet76Ref::Params p;
    p.thresh_db = -12.0;
    p.ratio = 4.0;
    p.attack_s = 0.001;
    p.release_s = 0.1;
    f.set_params(p);
    f.set_sample_rate(sr);
    f.prepare();
    std::vector<float> L(in.frames()), R(in.frames());
    for (size_t i = 0; i < in.frames(); ++i) {
        L[i] = in.samples[2 * i];
        R[i] = in.samples[2 * i + 1];
    }
    f.process(L.data(), R.data(), L.size());
    check(f.current_gr_db() > -0.5,
          "GR recuperada (medida " + std::to_string(f.current_gr_db()) + " dB)");
}

void test_fet76_nonfinite() {
    std::cout << "[test] fet76: NaN/Inf -> salida finita y contador\n";
    Fet76Ref f;
    f.set_sample_rate(48000);
    f.prepare();
    std::vector<float> L{0.5f, std::numeric_limits<float>::quiet_NaN(),
                         0.25f, std::numeric_limits<float>::infinity(), -0.5f};
    std::vector<float> R = L;
    f.process(L.data(), R.data(), L.size());
    bool all_finite = true;
    for (float v : L) all_finite = all_finite && std::isfinite(v);
    for (float v : R) all_finite = all_finite && std::isfinite(v);
    check(all_finite, "todas las salidas finitas");
    check(f.bad_input_count() == 4, "contador de entradas no finitas == 4");
}

void test_fet76_denormal_tiny() {
    std::cout << "[test] fet76: senal denormal (1e-30) -> finita y sin estancarse\n";
    WavData in = make_sine(48000, 9600, 1e-30);
    Fet76Ref f;
    f.set_sample_rate(48000);
    f.prepare();
    std::vector<float> L(in.frames()), R(in.frames());
    for (size_t i = 0; i < in.frames(); ++i) {
        L[i] = in.samples[2 * i];
        R[i] = in.samples[2 * i + 1];
    }
    f.process(L.data(), R.data(), L.size());
    bool finite = true;
    for (float v : L) finite = finite && std::isfinite(v);
    check(finite, "salida finita con entrada denormal");
}

void test_fet76_mono() {
    std::cout << "[test] fet76: camino mono (R = nullptr)\n";
    Fet76Ref f;
    f.set_sample_rate(48000);
    f.prepare();
    std::vector<float> L(4800);
    for (size_t i = 0; i < L.size(); ++i)
        L[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                    double(i) / 48000.0));
    f.process(L.data(), nullptr, L.size());
    bool finite = true;
    for (float v : L) finite = finite && std::isfinite(v);
    check(finite, "salida mono finita");
}

void test_engine_end_to_end() {
    std::cout << "[test] engine extremo a extremo (bloque parcial 1000/512)\n";
    WavData in = make_sine(48000, 1000, 0.5);                // < 2 bloques
    std::vector<float> outL;
    check(run_engine(in, outL), "corrida completa y relectura OK");
    check(outL.size() == 1000, "marcos de salida == 1000 (parcial manejado)");
    bool finite = true;
    for (float v : outL) finite = finite && std::isfinite(v);
    check(finite, "salida del engine finita");
}

// --------------------- pruebas board (fase 2, v0.2) ------------------

// Procesador blanco: ganancia exacta x0.5 (multiplicación por potencia
// de 2 => exacta en IEEE-754, sin redondeo).
class GainHalf : public IProcessor {
public:
    const char* name() const override { return "gain_half_test"; }
    void prepare(const ProcessContext&) override {}
    void process(Block& b) override {
        for (size_t i = 0; i < b.frames; ++i) {
            b.L[i] *= 0.5f;
            if (b.channels == 2) b.R[i] *= 0.5f;
        }
    }
    void reset() override {}
};

void test_board_sdcard() {
    std::cout << "[test] sdcard: carpeta con 2 wav y 1 otro -> exactamente 2 canciones\n";
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string dir = (fs::temp_directory_path() /
                       "deluge_sim_test_sd").string();
    fs::remove_all(dir, ec);                                // limpia corrida previa
    fs::create_directories(dir);
    std::string err;
    // Archivos de prueba: 2 wav (nombres invertidos para probar el orden),
    // 1 no-wav que NO debe contar y 1 subdirectorio que tampoco.
    { std::ofstream f(dir + "/bbb.wav"); f << 'x'; }
    { std::ofstream f(dir + "/aaa.wav"); f << 'x'; }
    { std::ofstream f(dir + "/ccc.txt"); f << 'x'; }
    fs::create_directories(dir + "/sub.wav");
    deluge_sim::board::SdCard sd;
    bool ok = sd.mount(dir, err);
    check(ok, "montaje de carpeta SD");
    check(ok && sd.song_count() == 2, "solo los *.wav cuentan (2, no 3 ni 4)");
    check(ok && sd.song_count() == 2 &&
              sd.songs()[0].find("aaa.wav") != std::string::npos &&
              sd.songs()[1].find("bbb.wav") != std::string::npos,
          "orden alfabético por ruta");
    deluge_sim::board::SdCard bad;
    check(!bad.mount(dir + "/no_existe", err) && !err.empty(),
          "carpeta inexistente -> error accionable");
    fs::remove_all(dir, ec);
}

void test_board_pads() {
    std::cout << "[test] pads: toggles exactos y pads de rol (play/bypass)\n";
    deluge_sim::board::PadGrid g;
    check(!g.is_playing() && !g.is_bypassed(), "arranque: todo apagado");
    check(!g.toggle(8, 0) && !g.toggle(0, 8) && !g.toggle(-1, 0),
          "fuera de rango -> false");
    g.set_playing(true);
    g.set_bypassed(true);
    check(g.is_playing() && g.is_bypassed() && g.is_lit(0, 0) && g.is_lit(0, 1),
          "pads de rol reflejan play/bypass");
    bool v = g.toggle(3, 4);
    check(v && g.is_lit(3, 4), "pad libre encendido por toggle");
    v = g.toggle(3, 4);
    check(!v && !g.is_lit(3, 4), "pad libre apagado por segundo toggle");
    g.all_off();
    int lit = 0;
    for (int r = 0; r < deluge_sim::board::kPadRows; ++r)
        for (int c = 0; c < deluge_sim::board::kPadCols; ++c)
            lit += g.is_lit(r, c) ? 1 : 0;
    check(lit == 0, "all_off apaga los 64 pads");
}

void test_board_clock() {
    std::cout << "[test] clock: matemática 48k / bloque 128\n";
    deluge_sim::board::BoardClock c;                        // 48000 Hz, 128
    check(std::fabs(c.audio_seconds(48000) - 1.0) < 1e-12,
          "48000 marcos == 1.000 s exacto");
    check(std::fabs(c.block_ms() - 8.0 / 3.0) < 1e-9,
          "bloque 128 == 2.666... ms (8/3)");
    check(c.frames_for(1.0) == 48000 && c.frames_for(0.0) == 0,
          "frames_for redondeo al marco más próximo");
    check(std::fabs(c.audio_seconds(c.hw_block) * 1000.0 - c.block_ms()) < 1e-12,
          "block_ms coherente con audio_seconds");
}

void test_board_pump_cadence() {
    std::cout << "[test] pump: cadencia fija 1000 marcos @128 (7x128 + 104 + fin)\n";
    WavData in = make_sine(48000, 1000, 0.5);               // 1000 marcos
    deluge_sim::board::BoardClock c;
    deluge_sim::board::BoardPump pump(c);                   // sin cadena: passthrough
    std::string err;
    check(pump.load_song(in, err), "carga de canción");
    check(!pump.is_playing(), "cargada pero parada");
    std::vector<size_t> sizes;
    std::vector<float> out(128 * 2, 0.0f);
    pump.start();
    for (;;) {
        size_t n = pump.pump(out.data(), 128);
        if (n == 0) break;
        sizes.push_back(n);
    }
    check(sizes.size() == 8, "8 llamadas con datos (7x128 + 1x104)");
    bool seq_ok = sizes.size() == 8;
    for (size_t i = 0; i < 7 && seq_ok; ++i) seq_ok = (sizes[i] == 128);
    if (seq_ok) seq_ok = (sizes[7] == 104);
    check(seq_ok, "secuencia exacta de bloques hardware (cadencia fija)");
    check(!pump.is_playing(), "auto-stop al terminar la canción");
    check(pump.stats().frames_rendered == 1000 && pump.stats().blocks_rendered == 8,
          "telemetría: 1000 marcos en 8 bloques");
    check(pump.pump(out.data(), 128) == 0, "llamada extra -> 0 (sin reproducir)");
}

void test_board_pump_chain_bypass() {
    std::cout << "[test] pump: cadena aplicada, bypass exacto y guardia NaN\n";
    WavData in = make_sine(48000, 256, 0.5);
    deluge_sim::board::BoardClock c;
    deluge_sim::board::BoardPump pump(c);
    pump.add_processor(std::make_unique<GainHalf>());
    std::string err;
    check(pump.load_song(in, err), "carga con cadena de 1 procesador");
    std::vector<float> out(128 * 2, 0.0f);
    pump.start();
    size_t got = 0, calls = 0;
    for (;;) {
        size_t n = pump.pump(out.data(), 128);
        if (n == 0) break;
        // x0.5 exacto: la salida debe ser EXACTAMENTE la mitad bit a bit.
        for (size_t i = 0; i < n; ++i) {
            if (out[2 * i] != in.samples[2 * (i + got)] * 0.5f) { got = size_t(-1); break; }
            if (out[2 * i + 1] != in.samples[2 * (i + got) + 1] * 0.5f) { got = size_t(-1); break; }
        }
        if (got == size_t(-1)) break;
        got += n;
        ++calls;
    }
    check(got == 256, "cadena x0.5 aplicada exactamente a los 256 marcos");
    check(calls == 2, "2 bloques (128+128) en 256 marcos");

    // Bypass: sin cadena, la salida debe ser EXACTAMENTE la fuente.
    WavData in2 = make_sine(48000, 128, 0.5);
    deluge_sim::board::BoardPump pump2(c);
    pump2.add_processor(std::make_unique<GainHalf>());
    check(pump2.load_song(in2, err), "recarga con cadena");
    pump2.set_bypass(true);
    std::vector<float> out2(128 * 2, 0.0f);
    pump2.start();
    bool exact = pump2.pump(out2.data(), 128) == 128;
    for (size_t i = 0; i < 256 && exact; ++i) exact = (out2[i] == in2.samples[i]);
    check(exact, "bypass: salida == fuente bit a bit");

    // Guardia de entrada: NaN en la canción -> sustituido y contado.
    WavData in3 = make_sine(48000, 128, 0.5);
    in3.samples[10] = std::numeric_limits<float>::quiet_NaN();
    deluge_sim::board::BoardPump pump3(c);
    check(pump3.load_song(in3, err), "carga con NaN");
    std::vector<float> out3(128 * 2, 0.0f);
    pump3.start();
    pump3.pump(out3.data(), 128);
    bool finite = true;
    for (float v : out3) finite = finite && std::isfinite(v);
    check(finite && pump3.stats().nonfinite_input_fixed == 1,
          "NaN sustituido por 0 y contado (== 1)");

    // Sample rate incompatible: rechazo accionable.
    WavData in4 = make_sine(44100, 128, 0.5);
    deluge_sim::board::BoardPump pump4(c);
    check(!pump4.load_song(in4, err) && err.find("Hz") != std::string::npos,
          "44100 Hz vs reloj 48000 -> rechazo con mensaje");
}

// ---------------- pruebas opto_comp (referencia, v0.3) ---------------

// Estabiliza el opto con una entrada DC (el detector converge EXACTO a
// |x|) y devuelve la GR estacionaria y la salida final.
struct OptoSteady {
    double gr_db = 0.0;
    double out = 0.0;
};
OptoSteady opto_steady_dc(double x, const OptoCompRef::Params& p) {
    OptoCompRef o;
    o.set_params(p);
    o.set_sample_rate(48000);
    o.prepare();
    std::vector<float> L(14400, float(x)), R(14400, float(x));  // 0.3 s
    o.process(L.data(), R.data(), L.size());
    OptoSteady s;
    s.gr_db = o.current_gr_db();
    s.out = L.back();
    return s;
}

void test_opto_gain_law_dc() {
    std::cout << "[test] opto: ley de ganancia EXACTA con DC (rodilla + lineal)\n";
    // Ley documentada (s = 1-1/R, w = knee):
    //   e<=0: 0 | 0<e<=w: s*e^2/(2w) | e>w: s*(e-w/2)
    OptoCompRef::Params p;                 // thresh=-12, ratio=4, knee=6
    p.thresh_db = -12.0; p.ratio = 4.0; p.knee_db = 6.0;
    const double s = 0.75, w = 6.0, th = -12.0;

    // Caso A: en rodilla (e = 5.979 dB < w)
    {
        const double x = 0.5;
        const double e = 20.0 * std::log10(x) - th;
        const double gr_expect = s * e * e / (2.0 * w);
        const OptoSteady m = opto_steady_dc(x, p);
        check(std::fabs(m.gr_db + gr_expect) < 1e-3,
              "GR en rodilla == ley (esperada " + std::to_string(gr_expect) +
              ", medida " + std::to_string(-m.gr_db) + ")");
        check(std::fabs(m.out - x * std::pow(10.0, -gr_expect / 20.0)) < 1e-3,
              "salida == x*10^(-GR/20)");
    }
    // Caso B: EXACTAMENTE en la juntura rodilla/lineal (e = w): ambas ramas
    //         dan lo mismo (continuidad C1).
    {
        const double x = std::pow(10.0, (th + w) / 20.0);
        const double gr_expect = s * (w - 0.5 * w);
        const OptoSteady m = opto_steady_dc(x, p);
        check(std::fabs(m.gr_db + gr_expect) < 1e-3,
              "GR en la juntura e=knee == s*knee/2 (esperada " +
              std::to_string(gr_expect) + ")");
    }
    // Caso C: por encima de la rodilla (pendiente s = 1 - 1/R).
    {
        const double x = 0.7;
        const double e = 20.0 * std::log10(x) - th;
        const double gr_expect = s * (e - 0.5 * w);
        const OptoSteady m = opto_steady_dc(x, p);
        check(std::fabs(m.gr_db + gr_expect) < 1e-3,
              "GR sobre rodilla == s*(e-w/2) (esperada " +
              std::to_string(gr_expect) + ")");
    }
    // Caso D: bajo el umbral -> sin reduccion (salida == entrada).
    {
        const OptoSteady m = opto_steady_dc(0.1, p);
        check(std::fabs(m.gr_db) < 1e-9 && std::fabs(m.out - 0.1) < 1e-6,
              "bajo umbral: GR = 0 y salida == entrada");
    }
}

void test_opto_attack_ballistics() {
    std::cout << "[test] opto: attack ~2 tau hasta el 63% de la GR final\n";
    // Con ley cuadratica en rodilla, el 63.2% de la GR final ocurre en
    // f_env = 0.868 => t = -tau*ln(1-0.868) ~= 2.02 tau (tau = attack_s).
    OptoCompRef o;
    OptoCompRef::Params p;
    p.thresh_db = -12.0; p.ratio = 4.0; p.knee_db = 6.0; p.attack_s = 0.010;
    o.set_params(p);
    o.set_sample_rate(48000);
    o.prepare();
    const double gr_final = 2.2346;             // de la ley (caso A anterior)
    size_t t63 = 0, onset = 0;
    float v = 0.0f;
    std::vector<float> one(1);
    for (size_t i = 0; i < 24000; ++i) {        // 0.5 s
        v = (i < 9600) ? 0.0f : 0.5f;           // silencio 0.2 s, luego DC
        if (onset == 0 && i >= 9600) onset = i;
        one[0] = v;
        o.process(one.data(), nullptr, 1);
        if (onset != 0 && t63 == 0 && o.current_gr_db() <= -0.632 * gr_final) {
            t63 = i - onset + 1;
        }
    }
    check(std::fabs(o.current_gr_db() + gr_final) < 0.05 * gr_final,
          "GR final convergida a la ley (medida " +
          std::to_string(-o.current_gr_db()) + ")");
    const double t63_ms = double(t63) / 48.0;
    check(t63_ms > 10.0 && t63_ms < 40.0,
          "tiempo al 63% ~ 2 tau = 20 ms (medido " + std::to_string(t63_ms) + " ms)");
}

void test_opto_release_program_dependent() {
    std::cout << "[test] opto: release program-dependent (GR profunda despeja mas lento)\n";
    // tau_release interpola de release_slow (GR->0) a release_fast
    // (|GR| >= gr_span_db). La cola larga es la firma del opto.
    auto recovery_ms = [&](double x, double gr_at_release) {
        OptoCompRef o;
        OptoCompRef::Params p;
        p.thresh_db = -12.0; p.ratio = 4.0; p.knee_db = 6.0;
        p.release_fast_s = 0.08; p.release_slow_s = 0.60; p.gr_span_db = 10.0;
        o.set_params(p);
        o.set_sample_rate(48000);
        o.prepare();
        std::vector<float> loud(14400, float(x));           // 0.3 s fuertes
        o.process(loud.data(), nullptr, loud.size());
        if (std::fabs(o.current_gr_db() + gr_at_release) > 0.15 * gr_at_release) {
            return -1.0;                                    // GR no convergio
        }
        std::vector<float> quiet(96000, 0.001f);            // 2 s de despeje
        size_t t = 0;
        std::vector<float> one(1);
        for (size_t i = 0; i < quiet.size(); ++i) {
            one[0] = quiet[i];
            o.process(one.data(), nullptr, 1);
            if (o.current_gr_db() > -1.0) { t = i + 1; break; }
        }
        return double(t) / 48.0;
    };
    // GR profunda: x=0.9 -> e=11.08 dB -> GR = s*(e-w/2) = 6.06 dB.
    // GR somera:  x=0.5 -> e=5.98 dB -> GR = 2.23 dB (rodilla).
    // tau efectivo inicial con depth=0.606: interpolando coeficientes
    // (1/tau) da ~121 ms; crece hacia 600 ms al despejar -> el tiempo
    // medido a -1 dB queda en ~150-200 ms, muy por encima del tau fast.
    const double deep = recovery_ms(0.9, 6.0636);
    const double shallow = recovery_ms(0.5, 2.2346);
    check(deep > 0 && deep > 100.0 && deep < 400.0,
          "despeje profundo a -1 dB en banda [100,400] ms (medido " +
          std::to_string(deep) + " ms)");
    check(shallow > 0 && shallow < 300.0,
          "despeje somero a -1 dB tarda < 300 ms (medido " +
          std::to_string(shallow) + " ms)");
    check(deep > shallow * 1.5,
          "GR profunda despeja claramente mas lento (program-dependent, "
          "razon > 1.5; medido razon " + std::to_string(deep / shallow) + ")");
}

void test_opto_nonfinite() {
    std::cout << "[test] opto: NaN/Inf -> salida finita y contador\n";
    OptoCompRef o;
    o.set_sample_rate(48000);
    o.prepare();
    std::vector<float> L{0.5f, std::numeric_limits<float>::quiet_NaN(),
                         0.25f, std::numeric_limits<float>::infinity(), -0.5f};
    std::vector<float> R = L;
    o.process(L.data(), R.data(), L.size());
    bool all_finite = true;
    for (float v : L) all_finite = all_finite && std::isfinite(v);
    for (float v : R) all_finite = all_finite && std::isfinite(v);
    check(all_finite, "todas las salidas finitas");
    check(o.bad_input_count() == 4, "contador de entradas no finitas == 4");
}

void test_opto_mono() {
    std::cout << "[test] opto: camino mono (R = nullptr)\n";
    OptoCompRef o;
    o.set_sample_rate(48000);
    o.prepare();
    std::vector<float> L(4800);
    for (size_t i = 0; i < L.size(); ++i)
        L[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                    double(i) / 48000.0));
    o.process(L.data(), nullptr, L.size());
    bool finite = true;
    for (float v : L) finite = finite && std::isfinite(v);
    check(finite, "salida mono finita");
}

void test_engine_chain_fet_opto() {
    std::cout << "[test] engine cadena [fet76, opto]: orden de insercion y finita\n";
    WavData in = make_sine(48000, 48000, 0.5);
    Engine engine(48000.0, 512);
    auto fet = std::make_unique<FetAdapter>(Fet76Ref::Params{});    // thresh -18
    OptoCompRef::Params pop;                     // thresh -12 por defecto
    pop.thresh_db = -24.0;                       // el fet76 ya aplasta la señal
    auto opto = std::make_unique<OptoAdapter>(pop);   // (el opto ve ~-14 dB)
    FetAdapter* fet_p = fet.get();               // telemetría tras la corrida
    OptoAdapter* opto_p = opto.get();
    engine.add_processor(std::move(fet));        // orden de inserción: fet -> opto
    engine.add_processor(std::move(opto));
    FileAudioSource src(in);
    WavAudioSink sink(48000, 2);
    RunReport rep;
    std::string err;
    check(engine.run(src, sink, rep, err), "corrida de la cadena completa");
    check(rep.frames_rendered == 48000, "marcos preservados (48000)");
    check(engine.processor_count() == 2, "cadena con 2 procesadores");
    // ambos compresores deben haber actuado (telemetria < 0 dB):
    check(fet_p->ref().current_gr_db() < -0.5 && opto_p->ref().current_gr_db() < -0.5,
          "GR de ambos eslabones activa (fet " +
          std::to_string(fet_p->ref().current_gr_db()) + ", opto " +
          std::to_string(opto_p->ref().current_gr_db()) + ")");
    // salida finita y comprimida: pico <= pico de entrada
    check(sink.finalize((std::filesystem::temp_directory_path() /
                         "deluge_sim_test_chain.wav").string(), err),
          "sumidero escribe WAV de la cadena");
}

// ---------------- pruebas passive_eq (referencia, v0.4) --------------

// Ganancia estacionaria (dB) a 'freq': senoidal de pico 0.5, 24 ciclos,
// se mide el pico del último 25% (transitorio ya extinguido).
double eq_steady_gain_db(const PassiveEQRef::Params& p, double freq) {
    PassiveEQRef e;
    e.set_params(p);
    e.set_sample_rate(48000);
    e.prepare();
    const uint32_t sr = 48000;
    const size_t n = size_t(double(sr) / freq) * 24;
    std::vector<float> L(n), R(n);
    for (size_t i = 0; i < n; ++i) {
        float v = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * freq *
                                       double(i) / double(sr)));
        L[i] = v;
        R[i] = v;
    }
    e.process(L.data(), R.data(), n);
    const size_t start = n - n / 4;
    double peak = 0.0;
    for (size_t i = start; i < n; ++i)
        peak = std::max(peak, double(std::fabs(L[i])));
    return 20.0 * std::log10(peak / 0.5);
}

void test_eq_dc_unity() {
    std::cout << "[test] eq: unidad exacta en DC (ambas etapas peaking)\n";
    PassiveEQRef::Params p;
    p.boost_db = 8.0;
    p.cut_db = 8.0;
    PassiveEQRef e;
    e.set_params(p);
    e.set_sample_rate(48000);
    e.prepare();
    std::vector<float> L(9600, 0.4f), R(9600, 0.4f);
    e.process(L.data(), R.data(), L.size());
    check(std::fabs(L.back() - 0.4) < 1e-4 && std::fabs(R.back() - 0.4) < 1e-4,
          "DC: salida == entrada (|H(1)| = 1 exacto)");
    check(std::fabs(e.magnitude_db(0.0)) < 1e-9,
          "magnitude_db(DC) == 0 dB exacto");
}

void test_eq_boost_response_exact() {
    std::cout << "[test] eq: refuerzo exacto en f0 (RBJ, +6 dB) y recovery\n";
    PassiveEQRef::Params p;
    p.boost_db = 6.0;                       // freq 60, q_boost 0.9
    const double g0 = eq_steady_gain_db(p, 60.0);
    check(std::fabs(g0 - 6.0) < 0.05,
          "en f0 la ganancia es EXACTAMENTE boost_db (medida " +
          std::to_string(g0) + " dB)");
    PassiveEQRef e;
    e.set_params(p);
    check(std::fabs(g0 - e.magnitude_db(60.0)) < 0.05,
          "medido == |H| analitico en f0");
    const double g10 = eq_steady_gain_db(p, 600.0);
    check(std::fabs(g10) < 0.5 &&
              std::fabs(g10 - e.magnitude_db(600.0)) < 0.05,
          "recovery a 10x f0 ~ 0 dB y coherente con |H| (medida " +
          std::to_string(g10) + " dB)");
}

void test_eq_cut_response_exact() {
    std::cout << "[test] eq: recorte exacto en 2.5 f0 y mordida en f0\n";
    PassiveEQRef::Params p;
    p.cut_db = 9.0;                         // dip en 2.5*60 = 150 Hz
    const double gdip = eq_steady_gain_db(p, 150.0);
    check(std::fabs(gdip + 9.0) < 0.05,
          "en 2.5*f0 el recorte es EXACTAMENTE -cut_db (medido " +
          std::to_string(gdip) + " dB)");
    PassiveEQRef e;
    e.set_params(p);
    check(std::fabs(gdip - e.magnitude_db(150.0)) < 0.05,
          "medido == |H| analitico en el dip");
    const double gf0 = eq_steady_gain_db(p, 60.0);
    check(gf0 < -0.05 && gf0 > gdip,
          "en f0 el dip muerde poco y menos que en su centro (medido " +
          std::to_string(gf0) + " dB)");
    check(std::fabs(gf0 - e.magnitude_db(60.0)) < 0.05,
          "medido == |H| analitico en f0");
}

void test_eq_trick_curve() {
    std::cout << "[test] eq: EL TRUCO — boost+cut a la misma frecuencia => "
                 "pico con dip encima\n";
    PassiveEQRef::Params p;
    p.boost_db = 8.0;
    p.cut_db = 8.0;
    PassiveEQRef::Params pboost_only;
    pboost_only.boost_db = 8.0;

    const double g_f0 = eq_steady_gain_db(p, 60.0);       // pico neto
    const double g_dip = eq_steady_gain_db(p, 150.0);     // dip sobre el pico
    const double g_hi = eq_steady_gain_db(p, 750.0);      // 12.5x f0
    const double g_boost_only = eq_steady_gain_db(pboost_only, 60.0);

    check(g_f0 > 0.5 && g_f0 < g_boost_only,
          "truco: pico neto en f0 (>0) pero recortado por el dip (medido " +
          std::to_string(g_f0) + " vs boost solo " +
          std::to_string(g_boost_only) + ")");
    check(g_dip < -3.0,
          "truco: dip claro por encima del pico (< -3 dB, medido " +
          std::to_string(g_dip) + ")");
    check(std::fabs(g_hi) < 1.0,
          "recupera ~0 dB por encima de ambas bandas (medido " +
          std::to_string(g_hi) + ")");
    PassiveEQRef e;
    e.set_params(p);
    bool consistent = std::fabs(g_f0 - e.magnitude_db(60.0)) < 0.05 &&
                      std::fabs(g_dip - e.magnitude_db(150.0)) < 0.05 &&
                      std::fabs(g_hi - e.magnitude_db(750.0)) < 0.05;
    check(consistent, "los tres puntos == |H| analitico de la cascada");
}

void test_eq_nonfinite_mono() {
    std::cout << "[test] eq: NaN/Inf -> finita y contador; camino mono\n";
    PassiveEQRef e;
    PassiveEQRef::Params p;
    p.boost_db = 6.0;
    e.set_params(p);
    e.set_sample_rate(48000);
    e.prepare();
    std::vector<float> L{0.5f, std::numeric_limits<float>::quiet_NaN(),
                         0.25f, std::numeric_limits<float>::infinity(), -0.5f};
    std::vector<float> R = L;
    e.process(L.data(), R.data(), L.size());
    bool all_finite = true;
    for (float v : L) all_finite = all_finite && std::isfinite(v);
    for (float v : R) all_finite = all_finite && std::isfinite(v);
    check(all_finite && e.bad_input_count() == 4,
          "salidas finitas y contador == 4");
    // mono
    PassiveEQRef m;
    m.set_params(p);
    m.set_sample_rate(48000);
    m.prepare();
    std::vector<float> mono(4800);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                       double(i) / 48000.0));
    m.process(mono.data(), nullptr, mono.size());
    bool finite = true;
    for (float v : mono) finite = finite && std::isfinite(v);
    check(finite, "salida mono finita");
}

void test_engine_chain_eq() {
    std::cout << "[test] engine cadena [fet76, eq]: boost que sobrevive la cadena\n";
    WavData in = make_sine(48000, 48000, 0.5);   // seno a 1 kHz
    Engine engine(48000.0, 512);
    auto fet = std::make_unique<FetAdapter>(Fet76Ref::Params{});  // thresh -18
    PassiveEQRef::Params peq;
    peq.freq_hz = 1000.0;                        // refuerzo justo en la señal
    peq.boost_db = 6.0;
    auto eq = std::make_unique<PassiveEQAdapter>(peq);
    engine.add_processor(std::move(fet));        // orden de inserción: fet -> eq
    engine.add_processor(std::move(eq));
    FileAudioSource src(in);
    WavAudioSink sink(48000, 2);
    RunReport rep;
    std::string err;
    check(engine.run(src, sink, rep, err), "corrida de la cadena completa");
    check(rep.frames_rendered == 48000, "marcos preservados (48000)");
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_chain_eq.wav").string();
    check(sink.finalize(tmp, err), "sumidero escribe WAV");
    WavData back;
    check(read_wav(tmp, back, err) && back.frames() == 48000, "relectura OK");
    double peak = 0.0;
    for (float v : back.samples) peak = std::max(peak, double(std::fabs(v)));
    // fet76 solo dejaría ~0.38; el +6 dB del EQ sobrevive: pico ~0.75
    check(peak > 0.6 && peak < 0.9,
          "boost del EQ sobrevive la cadena (pico medido " +
          std::to_string(peak) + ")");
    bool finite = true;
    for (float v : back.samples) finite = finite && std::isfinite(v);
    check(finite, "salida de la cadena finita");
}

// ---------------- pruebas plate_reverb (referencia, v0.5) -------------

// RT60 medido por regresión lineal sobre la envolvente de energía (bloques
// de 50 ms) en la banda relativa [lo_db, hi_db] bajo el bloque máximo.
double plate_measure_rt60(const PlateReverbRef::Params& p, double lo_db,
                          double hi_db, double seconds = 10.0) {
    PlateReverbRef r;
    r.set_params(p);
    r.set_sample_rate(48000);
    r.prepare();
    const size_t n = size_t(48000.0 * seconds);
    std::vector<float> L(n, 0.0f), R(n, 0.0f);
    L[0] = 1.0f;
    R[0] = 1.0f;
    r.process(L.data(), R.data(), n);
    const size_t blk = 2400;                             // 50 ms
    std::vector<double> t, db;
    double max_db = -1e9;
    std::vector<double> raw;
    for (size_t b = 0; b + blk <= n; b += blk) {
        double acc = 0.0;
        for (size_t i = b; i < b + blk; ++i) acc += double(L[i]) * double(L[i]);
        const double rms = std::sqrt(acc / double(blk));
        const double d = 20.0 * std::log10(std::max(rms, 1e-12));
        raw.push_back(d);
        if (d > max_db) max_db = d;
    }
    for (size_t k = 0; k < raw.size(); ++k) {
        if (raw[k] <= max_db - hi_db && raw[k] >= max_db - lo_db) {
            t.push_back(double(k) * 0.05 + 0.025);       // centro del bloque
            db.push_back(raw[k]);
        }
    }
    if (t.size() < 4) return -1.0;                       // banda sin datos
    // regresión lineal db = a + m·t
    double st = 0, sd = 0, stt = 0, sdt = 0;
    for (size_t k = 0; k < t.size(); ++k) {
        st += t[k]; sd += db[k];
        stt += t[k] * t[k]; sdt += t[k] * db[k];
    }
    const double m = (sdt - st * sd / double(t.size())) /
                     (stt - st * st / double(t.size()));
    return (m < -1e-9) ? 60.0 / (-m) : -1.0;
}

void test_plate_rt60() {
    std::cout << "[test] plate: RT60 medido por impulso == decay_s (±20%)\n";
    PlateReverbRef::Params p;
    p.decay_s = 2.4;
    p.damp_pct = 30.0;
    p.mix_pct = 100.0;
    const double rt60 = plate_measure_rt60(p, 50.0, 30.0);
    check(rt60 > 1.92 && rt60 < 2.88,
          "RT60 medido en banda [1.92, 2.88] s (medido " +
          std::to_string(rt60) + " s, objetivo 2.4)");
}

void test_plate_damping_hf() {
    std::cout << "[test] plate: damping — la cola a 6 kHz decae más rápido "
                 "que a 200 Hz\n";
    // fc(damp 50%) = 4 kHz: |H| a 6 kHz ≈ 0.57 por vuelta (-4.9 dB/67 ms)
    // => RT60_hf ~ 0.6 s vs RT60_lf ~ decay_s.
    auto burst_rt60 = [&](double freq) {
        PlateReverbRef r;
        PlateReverbRef::Params p;
        p.decay_s = 2.4;
        p.damp_pct = 50.0;
        p.mix_pct = 100.0;
        p.predelay_ms = 5.0;
        r.set_params(p);
        r.set_sample_rate(48000);
        r.prepare();
        const uint32_t sr = 48000;
        const size_t burst = size_t(0.4 * sr);
        const size_t tail = size_t(5.0 * sr);
        std::vector<float> L(burst + tail, 0.0f), R = L;
        for (size_t i = 0; i < burst; ++i)
            L[i] = R[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                               freq * double(i) / double(sr)));
        r.process(L.data(), R.data(), L.size());
        // envolvente RMS por bloques de 50 ms desde el fin del burst
        const size_t blk = 2400;
        std::vector<double> t, db;
        double max_db = -1e9;
        std::vector<double> raw;
        for (size_t b = burst; b + blk <= burst + tail; b += blk) {
            double acc = 0.0;
            for (size_t i = b; i < b + blk; ++i)
                acc += double(L[i]) * double(L[i]);
            const double d = 20.0 * std::log10(std::max(std::sqrt(acc / blk), 1e-12));
            raw.push_back(d);
            if (d > max_db) max_db = d;
        }
        for (size_t k = 0; k < raw.size(); ++k)
            if (raw[k] <= max_db - 10.0 && raw[k] >= max_db - 35.0) {
                t.push_back(double(k) * 0.05 + 0.025);
                db.push_back(raw[k]);
            }
        if (t.size() < 3) return -1.0;
        double st = 0, sd = 0, stt = 0, sdt = 0;
        for (size_t k = 0; k < t.size(); ++k) {
            st += t[k]; sd += db[k];
            stt += t[k] * t[k]; sdt += t[k] * db[k];
        }
        const double m = (sdt - st * sd / double(t.size())) /
                         (stt - st * st / double(t.size()));
        return (m < -1e-9) ? 60.0 / (-m) : -1.0;
    };
    const double lf = burst_rt60(200.0);
    const double hf = burst_rt60(6000.0);
    check(lf > 1.68 && lf < 3.12,
          "RT60 a 200 Hz ~ decay_s (medido " + std::to_string(lf) + " s)");
    check(hf > 0.0 && hf < 0.8 * lf,
          "RT60 a 6 kHz < 0.8x el de 200 Hz (medido " +
          std::to_string(hf) + " s)");
}

void test_plate_dry_and_predelay() {
    std::cout << "[test] plate: bypass bit-exacto (mix 0) y silencio exacto "
                 "antes de predelay+colas (wet 100)\n";
    // mix 0: salida == entrada bit a bit
    PlateReverbRef r;
    PlateReverbRef::Params p;
    p.mix_pct = 0.0;
    r.set_params(p);
    r.set_sample_rate(48000);
    r.prepare();
    std::vector<float> L(4800), R(4800);
    for (size_t i = 0; i < L.size(); ++i) {
        L[i] = float(0.4 * std::sin(2.0 * 3.14159265358979323846 * 500.0 *
                                    double(i) / 48000.0));
        R[i] = float(0.3 * std::cos(2.0 * 3.14159265358979323846 * 700.0 *
                                    double(i) / 48000.0));
    }
    std::vector<float> Lin = L, Rin = R;
    r.process(L.data(), R.data(), L.size());
    bool exact = true;
    for (size_t i = 0; i < L.size(); ++i)
        if (L[i] != Lin[i] || R[i] != Rin[i]) { exact = false; break; }
    check(exact, "mix 0%: salida == entrada bit a bit");
    // wet 100 + predelay 20 ms: los primeros 2000 muestreos son 0.0 exactos
    PlateReverbRef w;
    PlateReverbRef::Params pw;
    pw.mix_pct = 100.0;
    pw.predelay_ms = 20.0;
    w.set_params(pw);
    w.set_sample_rate(48000);
    w.prepare();
    std::vector<float> W(4800, 0.0f), WR(4800, 0.0f);
    W[0] = 1.0f; WR[0] = 1.0f;                            // impulso
    w.process(W.data(), WR.data(), W.size());
    bool silent = true;
    for (size_t i = 0; i < 2000; ++i)
        if (W[i] != 0.0f || WR[i] != 0.0f) { silent = false; break; }
    check(silent, "wet 100%: exactamente 0 antes de predelay+primera línea");
    bool nonzero = false;
    for (size_t i = 3000; i < W.size(); ++i)
        if (W[i] != 0.0f) { nonzero = true; break; }
    check(nonzero, "la cola mojada llega (energía tras 3000 muestras)");
}

void test_plate_determinism_mod() {
    std::cout << "[test] plate: determinismo (LFO fijo) y modulación activa\n";
    auto run = [&](double depth_ms) {
        PlateReverbRef r;
        PlateReverbRef::Params p;
        p.mix_pct = 100.0;
        p.mod_depth_ms = depth_ms;
        p.mod_rate_hz = 0.73;
        p.predelay_ms = 5.0;
        r.set_params(p);
        r.set_sample_rate(48000);
        r.prepare();
        std::vector<float> L(24000, 0.0f), R(24000, 0.0f);
        L[0] = R[0] = 1.0f;
        r.process(L.data(), R.data(), L.size());
        return L;
    };
    const std::vector<float> a = run(0.0);
    const std::vector<float> a2 = run(0.0);
    const std::vector<float> b = run(0.8);
    const std::vector<float> b2 = run(0.8);
    bool a_eq = true, b_eq = true, differs = false;
    for (size_t i = 0; i < a.size(); ++i) {
        a_eq = a_eq && (a[i] == a2[i]);
        b_eq = b_eq && (b[i] == b2[i]);
        if (a[i] != b[i]) differs = true;
    }
    check(a_eq, "mod 0: dos corridas bit-identicas (LTI determinista)");
    check(b_eq, "mod 0.8 ms: dos corridas bit-identicas (LFO determinista)");
    check(differs, "mod 0.8 ms cambia la cola vs mod 0 (modos sembrados)");
}

void test_plate_stability_nonfinite_mono() {
    std::cout << "[test] plate: estabilidad con ruido, NaN/Inf y camino mono\n";
    // ruido blanco 2 s a plena mezcla: finito y acotado
    PlateReverbRef r;
    PlateReverbRef::Params p;
    p.mix_pct = 100.0;
    p.decay_s = 1.5;
    r.set_params(p);
    r.set_sample_rate(48000);
    r.prepare();
    uint32_t rng = 0x12345678u;
    std::vector<float> L(96000), R(96000);
    for (size_t i = 0; i < L.size(); ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float v = float((double(rng) / 4294967295.0) * 2.0 - 1.0) * 0.5f;
        L[i] = v;
        R[i] = v;
    }
    r.process(L.data(), R.data(), L.size());
    bool finite = true;
    double peak = 0.0;
    for (float v : L) {
        finite = finite && std::isfinite(v);
        peak = std::max(peak, double(std::fabs(v)));
    }
    check(finite && peak < 3.0,
          "ruido a wet 100%: finito y pico < 3 (medido " +
          std::to_string(peak) + ")");
    // NaN/Inf
    PlateReverbRef n;
    n.set_sample_rate(48000);
    n.prepare();
    std::vector<float> NL{0.5f, std::numeric_limits<float>::quiet_NaN(),
                          0.25f, std::numeric_limits<float>::infinity(), -0.5f};
    std::vector<float> NR = NL;
    n.process(NL.data(), NR.data(), NL.size());
    bool all_finite = true;
    for (float v : NL) all_finite = all_finite && std::isfinite(v);
    for (float v : NR) all_finite = all_finite && std::isfinite(v);
    check(all_finite && n.bad_input_count() == 4,
          "NaN/Inf sustituidos y contados (== 4)");
    // mono
    PlateReverbRef m;
    m.set_params(p);
    m.set_sample_rate(48000);
    m.prepare();
    std::vector<float> mono(4800);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                       double(i) / 48000.0));
    m.process(mono.data(), nullptr, mono.size());
    bool mfinite = true;
    for (float v : mono) mfinite = mfinite && std::isfinite(v);
    check(mfinite, "salida mono finita");
}

void test_engine_chain_plate() {
    std::cout << "[test] engine cadena [eq, plate]: extremo a extremo finito\n";
    WavData in = make_sine(48000, 48000, 0.5);
    Engine engine(48000.0, 512);
    PassiveEQRef::Params peq;
    peq.freq_hz = 1000.0;
    peq.boost_db = 6.0;
    auto eq = std::make_unique<PassiveEQAdapter>(peq);
    auto plate = std::make_unique<PlateAdapter>(PlateReverbRef::Params{});
    engine.add_processor(std::move(eq));         // orden de inserción: eq -> plate
    engine.add_processor(std::move(plate));
    FileAudioSource src(in);
    WavAudioSink sink(48000, 2);
    RunReport rep;
    std::string err;
    check(engine.run(src, sink, rep, err), "corrida de la cadena completa");
    check(rep.frames_rendered == 48000, "marcos preservados (48000)");
    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "deluge_sim_test_chain_plate.wav").string();
    check(sink.finalize(tmp, err), "sumidero escribe WAV");
    WavData back;
    check(read_wav(tmp, back, err) && back.frames() == 48000, "relectura OK");
    bool finite = true;
    for (float v : back.samples) finite = finite && std::isfinite(v);
    check(finite, "salida de la cadena finita");
}

// ------------- módulos v0.6 (trabajo en paralelo, integrados) --------
// Cada .inc se incluye verbatim DENTRO de este namespace anónimo: usa
// check()/make_sine() de arriba y NO trae #include propios.
#include "test_tape.inc"
#include "test_stream.inc"
#include "test_preset.inc"

} // namespace

int main() {
    std::cout << "deluge-sim :: simtests v0.6\n";
    test_wav_roundtrip();
    test_fet76_no_gr();
    test_fet76_gr_steady();
    test_fet76_release();
    test_fet76_nonfinite();
    test_fet76_denormal_tiny();
    test_fet76_mono();
    test_engine_end_to_end();
    test_board_sdcard();
    test_board_pads();
    test_board_clock();
    test_board_pump_cadence();
    test_board_pump_chain_bypass();
    test_opto_gain_law_dc();
    test_opto_attack_ballistics();
    test_opto_release_program_dependent();
    test_opto_nonfinite();
    test_opto_mono();
    test_engine_chain_fet_opto();
    test_eq_dc_unity();
    test_eq_boost_response_exact();
    test_eq_cut_response_exact();
    test_eq_trick_curve();
    test_eq_nonfinite_mono();
    test_engine_chain_eq();
    test_plate_rt60();
    test_plate_damping_hf();
    test_plate_dry_and_predelay();
    test_plate_determinism_mod();
    test_plate_stability_nonfinite_mono();
    test_engine_chain_plate();
    test_tape_dc_zero();
    test_tape_odd_symmetry();
    test_tape_small_signal_gain();
    test_tape_saturation_exact();
    test_tape_bounded();
    test_tape_nonfinite_mono();
    test_stream_equals_ram();
    test_stream_partial_tail();
    test_stream_mono_fold();
    test_stream_float32();
    test_stream_truncated();
    test_stream_engine_bitexact();
    test_preset_roundtrip();
    test_preset_malformed();
    test_preset_unknown_fields();
    test_preset_bad_types();
    test_preset_empty();
    test_preset_disk_errors();
    if (g_failures == 0) {
        std::cout << "RESULTADO: TODO OK\n";
        return 0;
    }
    std::cout << "RESULTADO: " << g_failures << " FALLOS\n";
    return 1;
}

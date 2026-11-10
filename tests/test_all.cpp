// =====================================================================
// deluge-sim / test_all.cpp — Batería de verificación (binario simtests)
// Cada prueba lleva su expectativa MATEMÁTICA documentada. Exit 0 = todo ok.
// =====================================================================
#include "board_core.hpp"
#include "deluge_sim/engine.hpp"
#include "deluge_sim/wav_io.hpp"
#include "suite_core/fet76_ref.hpp"
#include "suite_core/opto_comp.hpp"

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

} // namespace

int main() {
    std::cout << "deluge-sim :: simtests v0.3\n";
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
    if (g_failures == 0) {
        std::cout << "RESULTADO: TODO OK\n";
        return 0;
    }
    std::cout << "RESULTADO: " << g_failures << " FALLOS\n";
    return 1;
}

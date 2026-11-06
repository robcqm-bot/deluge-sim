// =====================================================================
// deluge-sim / test_all.cpp — Batería de verificación (binario simtests)
// Cada prueba lleva su expectativa MATEMÁTICA documentada. Exit 0 = todo ok.
// =====================================================================
#include "deluge_sim/engine.hpp"
#include "deluge_sim/wav_io.hpp"
#include "suite_core/fet76_ref.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace deluge_sim;
using suite_core::Fet76Ref;

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

} // namespace

int main() {
    std::cout << "deluge-sim :: simtests v0.1\n";
    test_wav_roundtrip();
    test_fet76_no_gr();
    test_fet76_gr_steady();
    test_fet76_release();
    test_fet76_nonfinite();
    test_fet76_denormal_tiny();
    test_fet76_mono();
    test_engine_end_to_end();
    if (g_failures == 0) {
        std::cout << "RESULTADO: TODO OK\n";
        return 0;
    }
    std::cout << "RESULTADO: " << g_failures << " FALLOS\n";
    return 1;
}

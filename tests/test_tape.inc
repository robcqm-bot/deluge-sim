// =====================================================================
// deluge-sim / test_tape.inc — TapeSatRef tests (tape saturation)
// CONTRACT: this file is included VERBATIM inside the anonymous
// namespace of test_all.cpp, AFTER check() and make_sine(). Therefore
// it has NO #include, no main(), and no check() definition of its own.
// The integrator must add to test_all.cpp:
//     #include "suite_core/tape_sat.hpp"
// MATHEMATICAL expectations of the model (see the tape_sat.hpp banner):
//     g = 10^(drive_db/20); b = (bias_pct/100)*0.5; norm = 1-tanh(b)^2
//     x>=0: y_raw = tanh(g*x+b) - tanh(b)
//     x<0 : y_raw = tanh(g*x-b) + tanh(b)        (global curve is ODD)
//     y = y_raw / norm * trim_lin
// =====================================================================

// ---- helper: zero-phase stereo sine written straight to L/R ----------
// (same math as make_sine; fills the buffers directly so
// WavData is not dragged into these tests)
void tape_fill_sine(std::vector<float>& L, std::vector<float>& R,
                    size_t frames, uint32_t sr, double peak,
                    double freq_hz = 1000.0) {
    L.resize(frames);
    R.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const float v = float(peak * std::sin(2.0 * 3.14159265358979323846 *
                                              freq_hz * double(i) / double(sr)));
        L[i] = v;
        R[i] = v;
    }
}

// a) DC 0.0 -> bit-exact 0.0 output (bias 0 and bias 100)
void test_tape_dc_zero() {
    std::cout << "[test] tape: DC 0.0 -> salida 0.0 bit-exacto (bias 0 y bias 100)\n";
    // y(0) = tanh(b) - tanh(b) = 0 EXACT for any bias (the same
    // double subtraction) and 0/norm*trim = 0 in IEEE => bit-exact.
    for (double bias : {0.0, 100.0}) {
        suite_core::TapeSatRef t;
        suite_core::TapeSatRef::Params p;        // drive 6, trim 0
        p.bias_pct = bias;
        t.set_params(p);
        t.set_sample_rate(48000.0);
        t.prepare();
        std::vector<float> L(4800, 0.0f), R(4800, 0.0f);
        t.process(L.data(), R.data(), L.size());
        size_t nonzero = 0;
        for (float v : L) nonzero += (v != 0.0f) ? 1 : 0;
        for (float v : R) nonzero += (v != 0.0f) ? 1 : 0;
        check(nonzero == 0, "DC 0.0 con bias " + std::to_string(bias) +
                            " -> salida 0.0 bit-exacto (muestras != 0: " +
                            std::to_string(nonzero) + ")");
    }
}

// b) EXACT odd symmetry: y(-x) == -y(x) (bias 0 and bias 60)
void test_tape_odd_symmetry() {
    std::cout << "[test] tape: simetria impar EXACTA y(-x) == -y(x) (bias 0 y 60)\n";
    // It is memoryless (no dynamic state that would break the comparison)
    // and the global curve is odd: processing x and -x from reset state
    // must give outputs exactly negated from each other (float equality).
    const uint32_t sr = 48000;
    const size_t frames = 4800;                  // 0.1 s @ 1 kHz
    std::vector<float> X, XR;
    tape_fill_sine(X, XR, frames, sr, 0.3);      // phase 0, amp 0.3, 1 kHz
    std::vector<float> NX(frames), NR(frames);
    for (size_t i = 0; i < frames; ++i) { NX[i] = -X[i]; NR[i] = -XR[i]; }
    for (double bias : {0.0, 60.0}) {
        suite_core::TapeSatRef::Params p;        // drive 6, trim 0
        p.bias_pct = bias;
        suite_core::TapeSatRef a;                // processes +x from reset
        a.set_params(p);
        a.set_sample_rate(double(sr));
        a.prepare();
        suite_core::TapeSatRef n;                // processes -x from reset
        n.set_params(p);
        n.set_sample_rate(double(sr));
        n.prepare();
        std::vector<float> L = X, R = XR;
        std::vector<float> NL = NX, NRn = NR;
        a.process(L.data(), R.data(), frames);
        n.process(NL.data(), NRn.data(), frames);
        size_t bad = 0;
        for (size_t i = 0; i < frames; ++i)
            bad += (NL[i] != -L[i] || NRn[i] != -R[i]) ? 1 : 0;
        check(bad == 0, "y(-x) == -y(x) bit-exacto con bias " +
                        std::to_string(bias) + " (muestras divergentes: " +
                        std::to_string(bad) + ")");
    }
}

// c) small-signal gain == exact 10^(drive_db/20)
void test_tape_small_signal_gain() {
    std::cout << "[test] tape: ganancia small-signal == 10^(drive_db/20) exacta\n";
    // The slope at 0 of BOTH branches is g*(1-tanh(b)^2)/norm = g exact.
    // In a real small-signal regime (very small amplitude) the output
    // peak / input amplitude falls within 1e-3 relative: the curve
    // deviation is O(g^2*tanh(b)*A) with A = amplitude.
    const double g = std::pow(10.0, 6.0 / 20.0);          // drive_db = 6
    const uint32_t sr = 48000;
    const size_t frames = 4800;
    suite_core::TapeSatRef::Params p;
    p.drive_db = 6.0;
    p.bias_pct = 40.0;
    {
        suite_core::TapeSatRef t;
        t.set_params(p);
        t.set_sample_rate(double(sr));
        t.prepare();
        const double amp = 5e-4;                          // real small-signal
        std::vector<float> L, R;
        tape_fill_sine(L, R, frames, sr, amp);
        t.process(L.data(), R.data(), L.size());
        double peak = 0.0;
        for (float v : L) peak = std::max(peak, double(std::fabs(v)));
        const double measured = peak / amp;
        check(std::fabs(measured - g) <= 1e-3 * g,
              "ganancia small-signal == 10^(6/20) (esperada " +
              std::to_string(g) + ", medida " + std::to_string(measured) + ")");
    }
    {
        // At the insert amplitude (0.05) the peak ALREADY COMPRESSES due
        // to the curve (quadratic term -g^2*tanh(b)*x^2 introduced by the
        // bias), so there it is verified against the EXACT model value at
        // the real input peak.
        suite_core::TapeSatRef t;
        t.set_params(p);
        t.set_sample_rate(double(sr));
        t.prepare();
        std::vector<float> L, R;
        tape_fill_sine(L, R, frames, sr, 0.05);
        double in_peak = 0.0;
        for (float v : L) in_peak = std::max(in_peak, double(std::fabs(v)));
        t.process(L.data(), R.data(), L.size());
        double out_peak = 0.0;
        for (float v : L) out_peak = std::max(out_peak, double(std::fabs(v)));
        const double b = 0.40 * 0.5;
        const double tb = std::tanh(b);
        const double nrm = 1.0 - tb * tb;
        const double expected = (std::tanh(g * in_peak + b) - tb) / nrm;
        check(std::fabs(out_peak - expected) <= 1e-3,
              "pico a 0.05 == curva exacta del modelo (esperado " +
              std::to_string(expected) + ", medido " +
              std::to_string(out_peak) + ")");
    }
}

// d) saturated peak == exact curve (drive 12, bias 30)
void test_tape_saturation_exact() {
    std::cout << "[test] tape: pico saturado == (tanh(g*A+b)-tanh(b))/norm (drive 12, bias 30)\n";
    // The sine peak (phase 0) is at x = +0.5, sampled exactly in IEEE;
    // the curve is monotonically increasing, so the output peak is the
    // curve evaluated at the real input peak. Computed here with
    // std::tanh: out_peak == (tanh(g*0.5+b) - tanh(b)) / norm.
    const double g = std::pow(10.0, 12.0 / 20.0);         // drive_db = 12
    const double b = 0.30 * 0.5;                          // bias_pct = 30
    const double tb = std::tanh(b);
    const double nrm = 1.0 - tb * tb;
    const uint32_t sr = 48000;
    const size_t frames = 4800;
    std::vector<float> L, R;
    tape_fill_sine(L, R, frames, sr, 0.5);
    suite_core::TapeSatRef::Params p;
    p.drive_db = 12.0;
    p.bias_pct = 30.0;
    suite_core::TapeSatRef t;
    t.set_params(p);
    t.set_sample_rate(double(sr));
    t.prepare();
    double in_peak = 0.0;
    for (float v : L) in_peak = std::max(in_peak, double(std::fabs(v)));
    t.process(L.data(), R.data(), L.size());
    double out_peak = 0.0;
    for (float v : L) out_peak = std::max(out_peak, double(std::fabs(v)));
    const double expected = (std::tanh(g * in_peak + b) - tb) / nrm;
    check(std::fabs(out_peak - expected) <= 1e-3,
          "pico de salida == curva exacta del modelo (esperado " +
          std::to_string(expected) + ", medido " +
          std::to_string(out_peak) + ")");
}

// e) bounded: drive 24 with a full-scale sine -> |y| < 2.0 and finite
void test_tape_bounded() {
    std::cout << "[test] tape: acotada — drive 24 y seno a plena escala -> |y| < 2.0\n";
    // |y| <= (1 - tanh(b))/norm * trim_lin ~ 1 with bias 0 and trim 0:
    // tanh bounds it no matter what the input does.
    const uint32_t sr = 48000;
    const size_t frames = 48000;
    std::vector<float> L, R;
    tape_fill_sine(L, R, frames, sr, 1.0);
    suite_core::TapeSatRef::Params p;
    p.drive_db = 24.0;
    suite_core::TapeSatRef t;
    t.set_params(p);
    t.set_sample_rate(double(sr));
    t.prepare();
    t.process(L.data(), R.data(), L.size());
    bool ok = true;
    double peak = 0.0;
    for (float v : L) { ok = ok && std::isfinite(v); peak = std::max(peak, double(std::fabs(v))); }
    for (float v : R) ok = ok && std::isfinite(v);
    check(ok && peak < 2.0,
          "todas las muestras finitas y |y| < 2.0 (pico medido " +
          std::to_string(peak) + ")");
}

// f) NaN/Inf -> finite and counter == 4; mono path (R = nullptr)
void test_tape_nonfinite_mono() {
    std::cout << "[test] tape: NaN/Inf -> finitas y contador == 4; camino mono\n";
    suite_core::TapeSatRef t;
    t.set_sample_rate(48000.0);
    t.prepare();
    std::vector<float> L{0.5f, std::numeric_limits<float>::quiet_NaN(),
                         0.25f, std::numeric_limits<float>::infinity(), -0.5f};
    std::vector<float> R = L;
    t.process(L.data(), R.data(), L.size());
    bool all_finite = true;
    for (float v : L) all_finite = all_finite && std::isfinite(v);
    for (float v : R) all_finite = all_finite && std::isfinite(v);
    check(all_finite && t.bad_input_count() == 4,
          "salidas finitas y contador de no finitas == 4");
    // mono path (R = nullptr)
    suite_core::TapeSatRef m;
    m.set_sample_rate(48000.0);
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

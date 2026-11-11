// =====================================================================
// deluge-sim / test_golden_parity.inc — firmware-adapter parity tests
// CONTRACT: this file is included VERBATIM inside the anonymous
// namespace of test_all.cpp, AFTER check() and make_sine(). Therefore
// it has NO #include, no main(), and no check() definition of its own.
// The integrator must already have in test_all.cpp:
//     #include "deluge_sim/q31_bridge.hpp"
//     #include "suite_core/tape_sat.hpp"
// PURPOSE: the golden vectors (deluge-sim-golden-v1, "q31_scale":
// 2147483648) are produced by the DIRECT sim path. The real firmware
// will run the ADAPTER path (float -> q31 at the StereoSample
// boundary, q31 -> float into the engine, float -> q31 out). This
// file reconstructs BOTH routes over the golden tape signal and
// bounds the q31 deviation between them.
// =====================================================================

// a) Parity: ADAPTER route (firmware) vs GOLDEN route (cmd_golden) on
//    the golden tape signal: stereo 1 kHz sine, peak 0.5, phase 0,
//    48000 Hz, 480 frames — the EXACT signal cmd_golden renders
//    (app/main.cpp), replicated inline with the same formula and the
//    same float narrowing.
//
//    ADAPTER route: float -> q31 (bridge) -> q31 -> float (bridge)
//                   -> TapeSatRef (defaults, 48 kHz, prepare)
//                   -> float -> q31.
//    GOLDEN route:  float -> TapeSatRef (same params, 48 kHz, prepare)
//                   -> float -> q31.
//
//    Budget <= 4 LSB: the adapter route adds ONE input quantization
//    round trip (<= 0.5 LSB of 2^31 per sample through the bridge) and
//    the final float->q31 rounding of both routes (<= 0.5 LSB each);
//    the engine itself is deterministic float and memoryless, so with
//    equal input floats it contributes NO deviation. On THIS signal
//    the input round trip is bit-exact (every |v| is either 0 or
//    >= 0.065, far above the half-float-ulp absorption zone), so the
//    measured deviation is expected to be 0; 4 LSB is the documented
//    contract bound for arbitrary signals.
void test_golden_tape_parity() {
    std::cout << "[test] golden parity: ruta adaptador vs ruta dorada "
                 "(tape, 480 marcos)\n";
    const uint32_t sr = 48000;
    const size_t frames = 480;
    std::vector<float> sig(frames);
    for (size_t i = 0; i < frames; ++i) {
        sig[i] = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                      1000.0 * double(i) / double(sr)));
    }
    suite_core::TapeSatRef::Params p{};      // defaults: drive 6, bias 0, trim 0

    // GOLDEN route: direct float render, q31 only at the JSON boundary.
    std::vector<float> gold_L = sig, gold_R = sig;
    suite_core::TapeSatRef gold;
    gold.set_params(p);
    gold.set_sample_rate(double(sr));
    gold.prepare();
    gold.process(gold_L.data(), gold_R.data(), frames);
    std::vector<int32_t> gold_q31(frames * 2);
    deluge_sim::interleave_q31_from_planar(gold_L.data(), gold_R.data(),
                                           gold_q31.data(), frames);

    // ADAPTER route: the boundary the firmware will cross.
    std::vector<int32_t> in_q31(frames * 2);
    deluge_sim::interleave_q31_from_planar(sig.data(), sig.data(),
                                           in_q31.data(), frames);
    std::vector<float> adap_L(frames), adap_R(frames);
    deluge_sim::planar_from_interleave_q31(in_q31.data(), adap_L.data(),
                                           adap_R.data(), frames);
    suite_core::TapeSatRef adap;
    adap.set_params(p);
    adap.set_sample_rate(double(sr));
    adap.prepare();
    adap.process(adap_L.data(), adap_R.data(), frames);
    std::vector<int32_t> adap_q31(frames * 2);
    deluge_sim::interleave_q31_from_planar(adap_L.data(), adap_R.data(),
                                           adap_q31.data(), frames);

    long long max_dev = 0;
    for (size_t i = 0; i < adap_q31.size(); ++i) {
        const long long a = adap_q31[i], b = gold_q31[i];
        const long long d = (a >= b) ? a - b : b - a;
        if (d > max_dev) max_dev = d;
    }
    check(max_dev <= 4,
          "desviacion maxima adaptador vs dorado <= 4 LSB (medida " +
              std::to_string(max_dev) + " LSB sobre " +
              std::to_string(adap_q31.size()) + " muestras q31)");
}

// b) Documented scale values of the golden format (Q2.31, unity 2^31):
//    the key conversions the firmware integrator reads off the JSON,
//    plus the raw bridge round trip of the full golden signal.
void test_golden_scale_documented() {
    std::cout << "[test] golden scale: valores documentados de la escala "
                 "Q2.31 (unity 2^31)\n";
    // Key documented conversions (q31_bridge.hpp banner).
    check(deluge_sim::float_to_q31(0.5) == 1073741824,
          "0.5 -> 0.5*2^31 = 1073741824 exacto (escala del golden)");
    check(deluge_sim::float_to_q31(1.0) == 2147483647 &&
              deluge_sim::float_to_q31(-1.0) == -2147483648,
          "extremos: +1.0 -> 2147483647 (0.99999999953 de vuelta), "
              "-1.0 -> -2147483648 exacto (asimetria int32)");
    // First NONZERO golden sample (frame 1): float(0.5*sin(2*pi/48))
    // scaled to 2^31 is EXACTLY 4x the old 2^29 golden value
    // (35037856 * 4 = 140151424) — the scale change multiplies every
    // q31 vector by exactly 4.
    const float v1 = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                          1000.0 * double(1) / double(48000)));
    check(deluge_sim::float_to_q31(double(v1)) == 140151424,
          "primera muestra no nula del golden == 140151424 "
              "(= 4 x 35037856 del formato anterior 2^29)");
    // Raw bridge round trip of the FULL golden signal: error <= 1 LSB
    // of 2^31 (the adapter-route parity is covered in (a); this pins
    // the float -> q31 -> float conversion alone on the same signal).
    const uint32_t sr = 48000;
    const size_t frames = 480;
    double max_err_lsb = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        const float v = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                             1000.0 * double(i) / double(sr)));
        const int32_t q = deluge_sim::float_to_q31(double(v));
        const double back = deluge_sim::q31_to_float(q);
        max_err_lsb = std::max(max_err_lsb,
                               std::fabs(back - double(v)) /
                                   deluge_sim::kQ31Lsb);
    }
    check(max_err_lsb <= 1.0,
          "round-trip float->q31->float de la senal dorada <= 1 LSB de 2^31 "
              "(medido " + std::to_string(max_err_lsb) + " LSB)");
}

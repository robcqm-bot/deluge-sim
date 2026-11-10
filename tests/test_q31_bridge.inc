// =====================================================================
// deluge-sim / test_q31_bridge.inc — float <-> q31 bridge tests
// CONTRACT: this file is included VERBATIM inside the anonymous
// namespace of test_all.cpp, AFTER check() and make_sine(). Therefore
// it has NO #include, no main(), and no check() definition of its own.
// The integrator must add to test_all.cpp:
//     #include "deluge_sim/q31_bridge.hpp"
// Documented convention (see q31_bridge.hpp): Q2.29, 2^29 == 1.0;
// float->q31 = NaN->0, clamp to +-1.0, lround(x * 2^29);
// q31->float = y / 2^29 (exact power-of-two division).
// =====================================================================

// a) EXACT round trip on the Q2.29 grid: k/8 for k=-8..8
void test_q31_roundtrip_grid() {
    std::cout << "[test] q31: round-trip EXACTO en la rejilla k/8 (k=-8..8)\n";
    // k/8 is exactly representable in float, and its q31 image k*2^26 is
    // an exact integer, so both conversions are exact in IEEE-754 and
    // the round trip must return the ORIGINAL float bit for bit.
    size_t bad = 0;
    for (int k = -8; k <= 8; ++k) {
        const float x = float(k) / 8.0f;
        const int32_t q = deluge_sim::float_to_q31(double(x));
        const float y = float(deluge_sim::q31_to_float(q));
        if (y != x) ++bad;
    }
    check(bad == 0, "k/8 -> q31 -> float bit-exacto para los 17 valores");
    // Grid endpoints map to the exact +-2^29 q31 values (symmetric range).
    check(deluge_sim::float_to_q31(1.0) == 536870912 &&
              deluge_sim::float_to_q31(-1.0) == -536870912,
          "+-1.0 -> +-2^29 exactos en q31");
    check(deluge_sim::float_to_q31(0.5) == 268435456,
          "0.5 -> 2^28 exacto en q31");
}

// b) Clamp: anything outside [-1, 1] saturates at +-2^29 -> back to +-1.0
void test_q31_clamp() {
    std::cout << "[test] q31: clamp a +-1.0 (fuera de rango satura en +-2^29)\n";
    check(deluge_sim::float_to_q31(1.5) == 536870912,
          "1.5 -> +2^29 (saturacion documentada)");
    check(deluge_sim::float_to_q31(-1.5) == -536870912,
          "-1.5 -> -2^29 (saturacion documentada)");
    check(deluge_sim::float_to_q31(1e30) == 536870912 &&
              deluge_sim::float_to_q31(-1e30) == -536870912,
          "valores gigantes finitos -> +-2^29");
    check(deluge_sim::float_to_q31(
              std::numeric_limits<float>::infinity()) == 536870912 &&
              deluge_sim::float_to_q31(
                  -std::numeric_limits<float>::infinity()) == -536870912,
          "+-Inf -> clamp a +-1.0 -> +-2^29");
    check(deluge_sim::float_to_q31(
              std::numeric_limits<float>::quiet_NaN()) == 0,
          "NaN -> 0 (convencion documentada del harness)");
    // Round trip of the saturated values: exactly +-1.0 (documented).
    check(float(deluge_sim::q31_to_float(deluge_sim::float_to_q31(1.5))) == 1.0f &&
              float(deluge_sim::q31_to_float(deluge_sim::float_to_q31(-1.5))) == -1.0f,
          "round-trip de +-1.5 -> exactamente +-1.0");
}

// c) Exact symmetry: q31(-x) == -q31(x), including +-1.0
void test_q31_symmetry() {
    std::cout << "[test] q31: simetria exacta q31(-x) == -q31(x) (incluye +-1.0)\n";
    // The bridge range is [-2^29, +2^29]: FULLY symmetric. Unlike raw
    // int32 q31 (minimum with no positive counterpart), here -1.0 ->
    // -2^29 and +1.0 -> +2^29 are both representable, so NO asymmetric
    // special case exists (documented in q31_bridge.hpp).
    size_t bad = 0;
    for (int k = -16; k <= 16; ++k) {
        const double x = double(k) / 16.0;
        if (deluge_sim::float_to_q31(-x) != -deluge_sim::float_to_q31(x)) ++bad;
    }
    for (double x : {0.5, -0.5, 0.123456, -0.123456, 0.9999999, 1.0, -1.0}) {
        if (deluge_sim::float_to_q31(-x) != -deluge_sim::float_to_q31(x)) ++bad;
    }
    check(bad == 0, "q31(-x) == -q31(x) en toda la sonda (sin caso especial)");
}

// d) 0.5-peak sine through the round trip: max error <= 1 LSB of Q2.29
void test_q31_sine_1lsb() {
    std::cout << "[test] q31: seno 0.5 ida-y-vuelta, error <= 1 LSB de Q2.29\n";
    // Quantization to nearest with lround contributes at most 0.5 LSB;
    // the 1 LSB bound leaves margin for the float narrowing on the way
    // back. 1 LSB = 1/2^29 ~ 1.8626e-9.
    WavData w = make_sine(48000, 4800, 0.5);
    double max_err = 0.0;
    for (size_t i = 0; i < w.samples.size(); ++i) {
        const int32_t q = deluge_sim::float_to_q31(double(w.samples[i]));
        const double back = deluge_sim::q31_to_float(q);
        max_err = std::max(max_err, std::fabs(back - double(w.samples[i])));
    }
    const double lsb = max_err / deluge_sim::kQ31Lsb;
    check(max_err <= deluge_sim::kQ31Lsb,
          "error maximo <= 1 LSB (medido " + std::to_string(lsb) +
              " LSB, limite 1.0)");
}

// e) Frame converters: StereoSample layout {l, r} and round trip
void test_q31_interleave_planar() {
    std::cout << "[test] q31: intercalado <-> planar (layout StereoSample y round-trip)\n";
    WavData w = make_sine(48000, 256, 0.5);
    std::vector<float> L(256), R(256);
    for (size_t i = 0; i < 256; ++i) {
        L[i] = w.samples[2 * i];
        R[i] = w.samples[2 * i + 1];
    }
    std::vector<int32_t> pairs(512);
    deluge_sim::interleave_q31_from_planar(L.data(), R.data(), pairs.data(), 256);
    // Layout: pairs[2i] == q31(L[i]), pairs[2i+1] == q31(R[i]).
    size_t bad = 0;
    for (size_t i = 0; i < 256; ++i) {
        if (pairs[2 * i] != deluge_sim::float_to_q31(double(L[i])) ||
            pairs[2 * i + 1] != deluge_sim::float_to_q31(double(R[i]))) ++bad;
    }
    check(bad == 0, "layout intercalado {l,r} exacto (256 marcos)");
    check(pairs[0] == 0 && pairs[1] == 0,
          "primera muestra del seno (fase 0) -> par {0,0}");
    // Round trip through the frame converters: <= 1 LSB per sample.
    std::vector<float> L2(256), R2(256);
    deluge_sim::planar_from_interleave_q31(pairs.data(), L2.data(), R2.data(), 256);
    double max_err = 0.0;
    for (size_t i = 0; i < 256; ++i) {
        max_err = std::max(max_err, std::fabs(double(L2[i]) - double(L[i])));
        max_err = std::max(max_err, std::fabs(double(R2[i]) - double(R[i])));
    }
    check(max_err <= deluge_sim::kQ31Lsb,
          "round-trip planar -> q31 -> planar <= 1 LSB (medido " +
              std::to_string(max_err / deluge_sim::kQ31Lsb) + " LSB)");
}

// =====================================================================
// SUITE-CORE / passive_eq.hpp — OWN ZONE (GPL firewall)
// =====================================================================
// FIREWALL RULE: this module does NOT include any header from
// deluge_sim or the Deluge firmware. Standard C++ only + raw buffers.
//
// PassiveEQRef: reference passive program EQ (textbook EQP-1A-ish
// character). Generic design — NOT a port of any product.
//
// Documented model (verified by tests with exact expectation):
//   Two RBJ biquads in cascade (exact formulas from RBJ's "cookbook"):
//     1. BOOST:   peaking at freq_hz, Q=q_boost, gain +boost_db.
//     2. CUT:     peaking at kCutFreqRatio * freq_hz, Q=q_cut, gain
//        -cut_db. In a passive program EQ the attenuation band sits
//        ABOVE the boost (~2.5x in this generic design) — the classic
//        TRICK stems from that separation: boost + cut at the SAME
//        frequency => peak at f0 with a dip above it.
//   The total response is the product |H_boost| * |H_cut| (+ trim), and
//   magnitude_db() evaluates it ANALYTICALLY — it is the expectation
//   against which the tests measure steady sine waves.
//
//   Both stages are peaking => |H(DC)| = 1 and |H(Nyquist)| = 1 exact.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace suite_core {

class PassiveEQRef {
public:
    struct Params {
        double freq_hz  = 60.0;    // selector: BOOST frequency (20..1000)
        double boost_db = 0.0;     // 0..12  (resonant boost at freq_hz)
        double cut_db   = 0.0;     // 0..12  (cut at kCutFreqRatio*freq_hz)
        double q_boost  = 0.9;     // 0.3..2 (boost width)
        double q_cut    = 0.6;     // 0.3..2 (cut width, more program)
        double trim_db  = 0.0;     // -24..24 (level match)
    };

    static constexpr double kCutFreqRatio = 2.5;   // generic design constant

    void set_sample_rate(double sr);               // recomputes coefficients
    void prepare();                                // resets dynamic state
    void set_params(const Params& p);              // clamps to valid ranges
    const Params& params() const { return params_; }

    // EXACT |H(e^jw)| of the cascade (boost * cut, + trim), in dB.
    // Public on purpose: it is the mathematical expectation the tests
    // verify by measuring steady sines, and the curve the UI draws.
    double magnitude_db(double freq) const;

    // Processes in-place. R == nullptr => mono path (uses L only).
    // Guarantees: no allocations, no exceptions, finite output.
    void process(float* L, float* R, size_t frames);

    void reset();                                  // dynamic state back to startup

    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    // TDF2 biquad (transposed direct form II) with coefficients already
    // normalized by a0. Per-channel state (unlinked stereo: an EQ is
    // LTI, each channel filters independently).
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double s1 = 0.0, s2 = 0.0;
        double process(double x) {
            const double y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            // Denormal guard: during long silence the states decay
            // exponentially; they are anchored to zero (no audible cost).
            if (std::fabs(s1) < 1e-15) s1 = 0.0;
            if (std::fabs(s2) < 1e-15) s2 = 0.0;
            return y;
        }
    };

    void recompute_coefficients();                 // RBJ peaking -> both stages

    Params params_{};
    double sample_rate_ = 48000.0;
    Biquad boost_[2];                              // [0]=L, [1]=R
    Biquad cut_[2];
    double trim_lin_ = 1.0;
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

// =====================================================================
// SUITE-CORE / opto_comp.hpp — OWN ZONE (GPL firewall)
// =====================================================================
// FIREWALL RULE: this module does NOT include any header from
// deluge_sim or the Deluge firmware. Standard C++ only + raw buffers.
// Interface: planar float pointers + minimal context. This is the only
// thing that travels to another target (M7, Teensy, VST) uncontaminated.
//
// OptoCompRef: reference "opto" compressor — electroluminescent cell +
// photoresistor (textbook LA-2A-ish character). Generic design — NOT
// yet a port of any product.
//
// Documented model (verified by tests with exact expectation):
//   1. Detector: one-pole envelope of max(|L|,|R|) with ASYMMETRIC
//      coefficients (attack on the way up, release on the way down).
//      The photoresistor is slow: default attack 10 ms.
//   2. STATIC gain law in dB over the excess
//      e = detector_level - threshold, with a smooth quadratic knee of
//      width knee_db and slope s = 1 - 1/R above it (R = ratio):
//        e <= 0          : GR = 0
//        0 < e <= knee   : GR = s * e^2 / (2*knee)
//        e >  knee       : GR = s * (e - knee/2)
//      (C1-continuous at e=knee; slope 0 when entering the knee.)
//   3. PROGRAM-DEPENDENT release (the opto signature): the release
//      linearly interpolates its one-pole COEFFICIENTS between
//      release_slow_s (GR -> 0) and release_fast_s (|GR| >= gr_span_db).
//      Since coef ~ 1/(fs*tau), this equals interpolating 1/tau: the
//      initial fall (deep GR) comes out faster than interpolating tau
//      directly, and the tail stretches as it clears — the character
//      of a real photoresistor. The tests pin the measured band.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace suite_core {

class OptoCompRef {
public:
    struct Params {
        double thresh_db      = -12.0;   // detector threshold (dBFS)
        double ratio          = 3.0;     // 1..20 (slope above the knee)
        double knee_db        = 6.0;     // knee width (dB) 0..12
        double attack_s       = 0.010;   // 0.002..0.2 (photocell: slow)
        double release_fast_s = 0.080;   // 0.01..0.5 (tau with deep GR)
        double release_slow_s = 0.600;   // 0.05..3.0 (tau as it clears)
        double gr_span_db     = 10.0;    // |GR| at which release is fully fast 1..24
        double out_gain_db    = 0.0;     // make-up
    };

    void set_sample_rate(double sr);          // recomputes coefficients
    void prepare();                           // resets dynamic state
    void set_params(const Params& p);         // clamps to valid ranges
    const Params& params() const { return params_; }

    // Static GR law (reduction in dB, >= 0) for an excess in dB.
    // Public on purpose: it is THE mathematical expectation the tests
    // verify and the UI can draw.
    double gain_law_db(double excess_db) const;

    // Processes in-place. R == nullptr => mono path (uses L only).
    // Guarantees: no allocations, no exceptions, finite output.
    void process(float* L, float* R, size_t frames);

    void reset();                             // dynamic state back to startup

    // Telemetry (for tests and UI): current applied GR in dB (<= 0).
    double current_gr_db() const { return gr_db_; }
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_coefficients();

    Params params_{};
    double sample_rate_ = 48000.0;
    double attack_coef_ = 0.0;
    double release_fast_coef_ = 0.0;
    double release_slow_coef_ = 0.0;
    double out_lin_ = 1.0;
    double env_ = 0.0;                        // detector level (linear)
    double gr_db_ = 0.0;                      // current applied GR (<= 0)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

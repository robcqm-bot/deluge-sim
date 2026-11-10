// =====================================================================
// SUITE-CORE / tape_sat.hpp — OWN ZONE (GPL firewall)
// =====================================================================
// FIREWALL RULE: this module does NOT include any header from
// deluge_sim or the Deluge firmware. Standard C++ only + raw buffers.
// Interface: planar float pointers + minimal context. This is the only
// thing that travels to another target (M7, Teensy, VST) uncontaminated.
//
// TapeSatRef: reference tape saturation, channel-insert style (tanh
// curve with tape bias, operating point shifted by sign). Generic
// design — NOT yet a port of any product.
//
// Documented model (verified by tests with exact expectation):
//   g    = 10^(drive_db/20)
//   b    = (bias_pct/100) * 0.5
//   norm = 1 - tanh(b)^2          (normalizes the small-signal gain)
//   For x >= 0: y_raw = tanh(g*x + b) - tanh(b)
//   For x <  0: y_raw = tanh(g*x - b) + tanh(b)
//        (bias by sign: asymmetric knees above/below, but since tanh is
//         odd the overall function is exactly ODD.)
//   y = y_raw / norm * trim_lin   with trim_lin = 10^(trim_db/20)
//
//   Key properties:
//     - y(0) = 0 exact (no DC).
//     - y(-x) == -y(x) exact => no even harmonics.
//     - Small-signal gain == g exactly: the slope at 0 of both branches
//       is g*(1 - tanh(b)^2)/norm = g.
//     - Bounded: |y| <= (1 - tanh(b))/norm * trim_lin < trim_lin/norm.
//
//   The model is MEMORYLESS (static curve applied sample by sample):
//   no dynamic state, no fs dependence. set_sample_rate is stored only
//   to keep the interface uniform with the rest of the suite (the curve
//   does not change with the sampling rate).
//
//   Implementation: the x < 0 branch is applied as -branch(|x|) using
//   the oddness of tanh, so the symmetry y(-x) == -y(x) is bit-exact
//   regardless of the platform libm.
//
//   Non-finite guard: NaN/Inf -> 0 and counter (opto_comp pattern).
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace suite_core {

class TapeSatRef {
public:
    struct Params {
        double drive_db = 6.0;    // 0..24 (saturation into tanh)
        double bias_pct = 0.0;    // 0..100 (tape bias: asymmetric knees)
        double trim_db  = 0.0;    // -24..24 (output level)
    };

    void set_sample_rate(double sr);          // stored; the curve does not depend on fs
    void prepare();                           // reset (no dynamic state)
    void set_params(const Params& p);         // clamps to valid ranges
    const Params& params() const { return params_; }

    // EXACT model curve for one sample (THE mathematical expectation
    // the tests verify and the UI can draw).
    double curve(double x) const;

    // Processes in-place. R == nullptr => mono path (uses L only).
    // Guarantees: no allocations, no exceptions, finite output.
    void process(float* L, float* R, size_t frames);

    void reset();                             // state back to startup (counter to 0)

    // Telemetry (for tests and UI): non-finite inputs replaced by 0.
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_constants();

    Params params_{};
    double sample_rate_ = 48000.0;            // uniform interface only (static curve)
    double g_lin_ = 1.0;                      // 10^(drive_db/20)
    double b_ = 0.0;                          // linear bias (0..0.5)
    double tanh_b_ = 0.0;                     // tanh(b)
    double norm_ = 1.0;                       // 1 - tanh(b)^2
    double trim_lin_ = 1.0;                   // 10^(trim_db/20)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

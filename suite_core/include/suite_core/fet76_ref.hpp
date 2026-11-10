// =====================================================================
// SUITE-CORE / fet76_ref.hpp — OWN ZONE (GPL firewall)
// =====================================================================
// FIREWALL RULE: this module does NOT include any header from
// deluge_sim or the Deluge firmware. Standard C++ only + raw buffers.
// Interface: planar float pointers + minimal context. This is the only
// thing that travels to another target (M7, Teensy, VST) uncontaminated.
//
// Fet76Ref v0.1: FET-style FEEDBACK compressor, generic textbook design
// — NOT yet a port of any product. Calibration against references
// (RE + NKS presets) plugs in later BEHIND this same interface.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace suite_core {

struct SuiteContext {
    double sample_rate = 48000.0;
};

class Fet76Ref {
public:
    struct Params {
        double in_gain_db  = 0.0;     // drive into the cell
        double thresh_db   = -18.0;   // detector threshold (dBFS)
        double ratio       = 4.0;     // 1..20
        double attack_s    = 0.0008;  // 20e-6 .. 0.8
        double release_s   = 0.25;    // 0.01 .. 2.0
        double out_gain_db = 0.0;     // make-up
    };

    void set_sample_rate(double sr);          // recomputes coefficients
    void prepare();                           // resets dynamic state
    void set_params(const Params& p);         // clamps to valid ranges
    const Params& params() const { return params_; }

    // Processes in-place. R == nullptr => mono path (uses L only).
    // Guarantees: no allocations, no exceptions, finite output.
    void process(float* L, float* R, size_t frames);

    void reset();                             // dynamic state back to startup

    // Telemetry (for tests and UI): current gain reduction in dB.
    double current_gr_db() const { return gr_db_; }
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_coefficients();

    Params params_{};
    double sample_rate_ = 48000.0;
    double attack_coef_ = 0.0;
    double release_coef_ = 0.0;
    double in_lin_ = 1.0, out_lin_ = 1.0;
    double gr_db_ = 0.0;                      // detector state (<=0)
    double y_prev_l_ = 0.0, y_prev_r_ = 0.0;  // feedback (previous sample)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

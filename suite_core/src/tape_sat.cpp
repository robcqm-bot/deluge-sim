// =====================================================================
// SUITE-CORE / tape_sat.cpp — Reference tape saturation
// Firewall own zone: standard C++ only. No allocations in process();
// no exceptions; output always finite.
// =====================================================================
#include "suite_core/tape_sat.hpp"

#include <algorithm>
#include <cmath>

namespace suite_core {

void TapeSatRef::set_sample_rate(double sr) {
    // Stored for interface uniformity with the rest of the suite.
    // The curve is MEMORYLESS and static: there are no coefficients to
    // recompute; fs does not alter the sample-by-sample transfer.
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
}

void TapeSatRef::recompute_constants() {
    // Model constants precomputed: process() does no pow and no tanh of
    // parameters, just one tanh per sample (the curve itself).
    g_lin_    = std::pow(10.0, params_.drive_db / 20.0);
    b_        = (params_.bias_pct / 100.0) * 0.5;
    tanh_b_   = std::tanh(b_);
    norm_     = 1.0 - tanh_b_ * tanh_b_;      // in [1-tanh(0.5)^2, 1] > 0
    trim_lin_ = std::pow(10.0, params_.trim_db / 20.0);
}

void TapeSatRef::prepare() {
    reset();
}

void TapeSatRef::set_params(const Params& p) {
    params_.drive_db = std::clamp(p.drive_db, 0.0, 24.0);
    params_.bias_pct = std::clamp(p.bias_pct, 0.0, 100.0);
    params_.trim_db  = std::clamp(p.trim_db, -24.0, 24.0);
    recompute_constants();
}

double TapeSatRef::curve(double x) const {
    // Positive branch on |x| with the sign applied separately: the
    // oddness of tanh makes this identical to the documented per-branch
    // formula (tanh(g*x - b) + tanh(b) == -(tanh(g*|x| + b) - tanh(b)) for x<0),
    // but guarantees y(-x) == -y(x) BIT-EXACT on any libm.
    // At x = 0: tanh(b) - tanh(b) = 0 exact => y(0) = 0, no DC.
    const double ax = g_lin_ * std::fabs(x);
    const double yr = std::tanh(ax + b_) - tanh_b_;
    const double y  = (x < 0.0) ? -yr : yr;
    return y / norm_ * trim_lin_;
}

void TapeSatRef::reset() {
    bad_inputs_ = 0;                          // there is no other dynamic state
}

void TapeSatRef::process(float* L, float* R, size_t frames) {
    if (!L) return;
    for (size_t i = 0; i < frames; ++i) {
        float xl = L[i];
        if (!std::isfinite(xl)) { xl = 0.0f; ++bad_inputs_; }
        float xr = xl;
        if (R) {
            xr = R[i];
            if (!std::isfinite(xr)) { xr = 0.0f; ++bad_inputs_; }
        }
        L[i] = float(curve(double(xl)));
        if (R) R[i] = float(curve(double(xr)));
    }
}

} // namespace suite_core

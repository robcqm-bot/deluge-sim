// =====================================================================
// SUITE-CORE / opto_comp.cpp — Reference optical compressor
// Firewall own zone: standard C++ only. No allocations in process();
// no exceptions; output always finite.
// =====================================================================
#include "suite_core/opto_comp.hpp"

#include <algorithm>
#include <cmath>

namespace suite_core {
namespace {
constexpr double kLnToDb = 8.685889638065035;      // 20/ln(10)
constexpr double kDbToLn = 0.11512925464970229;    // ln(10)/20
constexpr double kFloorLin = 1e-12;                // -240 dBFS (detector floor)
constexpr double kFloorDb = -240.0;
} // namespace

void OptoCompRef::set_sample_rate(double sr) {
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
    recompute_coefficients();
}

void OptoCompRef::recompute_coefficients() {
    // Per-sample one-pole: coef = 1 - exp(-1/(sr * tau)).
    attack_coef_ = 1.0 - std::exp(-1.0 / (sample_rate_ * std::max(params_.attack_s, 1e-4)));
    release_fast_coef_ =
        1.0 - std::exp(-1.0 / (sample_rate_ * std::max(params_.release_fast_s, 1e-3)));
    release_slow_coef_ =
        1.0 - std::exp(-1.0 / (sample_rate_ * std::max(params_.release_slow_s, 1e-3)));
    out_lin_ = std::exp(params_.out_gain_db * kDbToLn);
}

void OptoCompRef::prepare() {
    reset();
}

void OptoCompRef::set_params(const Params& p) {
    params_.thresh_db      = std::clamp(p.thresh_db, -60.0, 0.0);
    params_.ratio          = std::clamp(p.ratio, 1.0, 20.0);
    params_.knee_db        = std::clamp(p.knee_db, 0.0, 12.0);
    params_.attack_s       = std::clamp(p.attack_s, 0.002, 0.2);
    params_.release_fast_s = std::clamp(p.release_fast_s, 0.01, 0.5);
    params_.release_slow_s = std::clamp(p.release_slow_s, 0.05, 3.0);
    // The slow tail cannot be faster than the initial fall.
    params_.release_slow_s = std::max(params_.release_slow_s, params_.release_fast_s);
    params_.gr_span_db     = std::clamp(p.gr_span_db, 1.0, 24.0);
    params_.out_gain_db    = std::clamp(p.out_gain_db, -24.0, 24.0);
    recompute_coefficients();
}

double OptoCompRef::gain_law_db(double e) const {
    const double s = 1.0 - 1.0 / std::max(params_.ratio, 1.000001);
    const double w = std::max(params_.knee_db, 1e-9);
    if (e <= 0.0) return 0.0;
    if (e <= w) return s * e * e / (2.0 * w);         // C1 quadratic knee
    return s * (e - 0.5 * w);                          // linear segment (s = 1 - 1/R)
}

void OptoCompRef::reset() {
    env_ = 0.0;
    gr_db_ = 0.0;
    bad_inputs_ = 0;
}

void OptoCompRef::process(float* L, float* R, size_t frames) {
    if (!L) return;
    const double thresh = params_.thresh_db;

    for (size_t i = 0; i < frames; ++i) {
        float xl = L[i];
        if (!std::isfinite(xl)) { xl = 0.0f; ++bad_inputs_; }
        float xr = xl;
        if (R) {
            xr = R[i];
            if (!std::isfinite(xr)) { xr = 0.0f; ++bad_inputs_; }
        }

        // ---- Detector: one-pole envelope of max(|L|,|R|), ASYMMETRIC.
        //      Rises with the photocell attack; falls with the release,
        //      program-dependent (fast with deep GR, slow at the end).
        const double det = std::fmax(std::fabs(double(xl)),
                                     R ? std::fabs(double(xr)) : 0.0);
        double coef;
        if (det > env_) {
            coef = attack_coef_;
        } else {
            const double depth = std::min(1.0, std::fabs(gr_db_) / params_.gr_span_db);
            coef = release_slow_coef_ + (release_fast_coef_ - release_slow_coef_) * depth;
        }
        env_ += (det - env_) * coef;
        if (env_ < kFloorLin) env_ = 0.0;   // avoids denormals in long silence

        // ---- Static gain law over the detector level.
        const double lvl_db = (env_ > kFloorLin) ? std::log(env_) * kLnToDb : kFloorDb;
        gr_db_ = -gain_law_db(lvl_db - thresh);

        const double g = std::exp(gr_db_ * kDbToLn) * out_lin_;
        L[i] = float(double(xl) * g);
        if (R) R[i] = float(double(xr) * g);
    }
}

} // namespace suite_core

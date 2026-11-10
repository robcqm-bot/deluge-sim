// =====================================================================
// SUITE-CORE / passive_eq.cpp — Passive program EQ (reference)
// Firewall own zone: standard C++ only. No allocations in process();
// no exceptions; output always finite.
// =====================================================================
#include "suite_core/passive_eq.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace suite_core {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDbToLn = 0.11512925464970229;    // ln(10)/20
} // namespace

void PassiveEQRef::set_sample_rate(double sr) {
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
    recompute_coefficients();
}

void PassiveEQRef::recompute_coefficients() {
    // --- Stage 1: BOOST peaking at freq_hz (RBJ, exact) ---
    {
        const double f0 = params_.freq_hz;
        const double Q = params_.q_boost;
        const double A = std::pow(10.0, params_.boost_db / 40.0);   // G/2 linear
        const double w0 = 2.0 * kPi * f0 / sample_rate_;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha / A;
        for (int ch = 0; ch < 2; ++ch) {
            boost_[ch].b0 = (1.0 + alpha * A) / a0;
            boost_[ch].b1 = (-2.0 * cw) / a0;
            boost_[ch].b2 = (1.0 - alpha * A) / a0;
            boost_[ch].a1 = (-2.0 * cw) / a0;
            boost_[ch].a2 = (1.0 - alpha / A) / a0;
        }
    }
    // --- Stage 2: CUT peaking at kCutFreqRatio * freq_hz ---
    {
        const double f0 = params_.freq_hz * kCutFreqRatio;
        const double Q = params_.q_cut;
        const double A = std::pow(10.0, -params_.cut_db / 40.0);   // NEGATIVE gain
        const double w0 = 2.0 * kPi * f0 / sample_rate_;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * Q);
        const double a0 = 1.0 + alpha / A;
        for (int ch = 0; ch < 2; ++ch) {
            cut_[ch].b0 = (1.0 + alpha * A) / a0;
            cut_[ch].b1 = (-2.0 * cw) / a0;
            cut_[ch].b2 = (1.0 - alpha * A) / a0;
            cut_[ch].a1 = (-2.0 * cw) / a0;
            cut_[ch].a2 = (1.0 - alpha / A) / a0;
        }
    }
    trim_lin_ = std::exp(params_.trim_db * kDbToLn);
}

void PassiveEQRef::prepare() {
    reset();
}

void PassiveEQRef::set_params(const Params& p) {
    params_.freq_hz  = std::clamp(p.freq_hz, 20.0, 1000.0);
    params_.boost_db = std::clamp(p.boost_db, 0.0, 12.0);
    params_.cut_db   = std::clamp(p.cut_db, 0.0, 12.0);
    params_.q_boost  = std::clamp(p.q_boost, 0.3, 2.0);
    params_.q_cut    = std::clamp(p.q_cut, 0.3, 2.0);
    params_.trim_db  = std::clamp(p.trim_db, -24.0, 24.0);
    recompute_coefficients();
}

double PassiveEQRef::magnitude_db(double freq) const {
    // |H(e^jw)| of a biquad: |(b0 + b1 z + b2 z^2) / (1 + a1 z + a2 z^2)|
    // with z = e^(-jw). Product of the two stages + trim (all exact).
    const double w = 2.0 * kPi * freq / sample_rate_;
    const std::complex<double> z(std::cos(w), -std::sin(w));
    const std::complex<double> z2 = z * z;
    auto mag = [&](const Biquad& bq) {
        const std::complex<double> num = bq.b0 + bq.b1 * z + bq.b2 * z2;
        const std::complex<double> den = 1.0 + bq.a1 * z + bq.a2 * z2;
        return std::abs(num) / std::abs(den);
    };
    const double lin = mag(boost_[0]) * mag(cut_[0]) * trim_lin_;
    return 20.0 * std::log10(std::max(lin, 1e-30));
}

void PassiveEQRef::reset() {
    for (int ch = 0; ch < 2; ++ch) {
        boost_[ch].s1 = boost_[ch].s2 = 0.0;
        cut_[ch].s1 = cut_[ch].s2 = 0.0;
    }
    bad_inputs_ = 0;
}

void PassiveEQRef::process(float* L, float* R, size_t frames) {
    if (!L) return;

    for (size_t i = 0; i < frames; ++i) {
        float xl = L[i];
        if (!std::isfinite(xl)) { xl = 0.0f; ++bad_inputs_; }
        double yl = boost_[0].process(double(xl));
        yl = cut_[0].process(yl) * trim_lin_;
        L[i] = float(yl);
        if (R) {
            float xr = R[i];
            if (!std::isfinite(xr)) { xr = 0.0f; ++bad_inputs_; }
            double yr = boost_[1].process(double(xr));
            yr = cut_[1].process(yr) * trim_lin_;
            R[i] = float(yr);
        }
    }
}

} // namespace suite_core

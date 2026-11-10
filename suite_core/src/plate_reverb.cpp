// =====================================================================
// SUITE-CORE / plate_reverb.cpp — FDN plate reverb (reference)
// Firewall own zone: standard C++ only. No allocations in process();
// no exceptions; output always finite.
// =====================================================================
#include "suite_core/plate_reverb.hpp"

#include <algorithm>
#include <cmath>

namespace suite_core {

// Base lengths @48 kHz (samples): odd and pairwise coprime to maximize
// modal density. Re-scaled with fs.
const size_t PlateReverbRef::kBaseSamples[kLines] = {
    1543, 2093, 2543, 2957, 3497, 3967, 4487, 4897};

constexpr int kLines = PlateReverbRef::kLines;

// ------------------------------ ModDelay -----------------------------
void PlateReverbRef::ModDelay::resize(size_t samples, size_t extra) {
    base = std::max<size_t>(samples, 1);
    buf.assign(base + extra + 4, 0.0f);
    wpos = 0;
}

double PlateReverbRef::ModDelay::read(double offset) const {
    // Fractional read backwards from wpos (offset >= 1 guaranteed by
    // the caller: base + mod never drops below 1).
    double rpos = double(wpos) - offset;
    const size_t n = buf.size();
    while (rpos < 0.0) rpos += double(n);
    const size_t i0 = size_t(rpos) % n;
    const double fr = rpos - std::floor(rpos);
    const size_t i1 = (i0 + 1) % n;
    return double(buf[i0]) + (double(buf[i1]) - double(buf[i0])) * fr;
}

void PlateReverbRef::ModDelay::write(float v) {
    buf[wpos] = v;
    wpos = (wpos + 1) % buf.size();
}

void PlateReverbRef::ModDelay::clear() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    wpos = 0;
}

// ------------------------------ Allpass ------------------------------
void PlateReverbRef::Allpass::resize(size_t delay) {
    buf.assign(std::max<size_t>(delay, 1), 0.0f);
    wpos = 0;
}

float PlateReverbRef::Allpass::process(float x) {
    const float d = buf[wpos];
    const float y = d - float(g) * x;
    buf[wpos] = float(x) + float(g) * d;
    wpos = (wpos + 1) % buf.size();
    return y;
}

void PlateReverbRef::Allpass::clear() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    wpos = 0;
}

// ------------------------------- setup -------------------------------
void PlateReverbRef::set_sample_rate(double sr) {
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
    const double scale = sample_rate_ / 48000.0;
    const double max_extra = 2.0 * 2.0 * sample_rate_ / 1000.0;  // 2·mod_depth_max
    for (int i = 0; i < kLines; ++i) {
        line_[i].resize(size_t(std::lround(double(kBaseSamples[i]) * scale)),
                        size_t(max_extra));
        lfo_phase_[i] = (2.0 * 3.14159265358979323846 * double(i)) / kLines;
    }
    predelay_.resize(size_t(std::lround(200.0 * sample_rate_ / 1000.0)), 8);
    const double dscale = sample_rate_ / 48000.0;
    diff_[0].resize(size_t(std::lround(341.0 * dscale)));
    diff_[1].resize(size_t(std::lround(427.0 * dscale)));
    diff_[2].resize(size_t(std::lround(127.0 * dscale)));
    recompute_coefficients();
}

void PlateReverbRef::recompute_coefficients() {
    // g_i = 10^(-3·t_i/RT60): each line decays 10^(-3t/RT60) per unit
    // of time => the whole tail decays at exactly decay_s (at low end).
    const double rt60 = params_.decay_s;
    for (int i = 0; i < kLines; ++i) {
        const double t_i = double(line_[i].base) / sample_rate_;
        line_gain_[i] = std::pow(10.0, -3.0 * t_i / rt60);
    }
    // Damping: fc on a log scale 20 kHz (0%) -> 800 Hz (100%).
    const double pct = params_.damp_pct / 100.0;
    const double fc = std::exp(std::log(20000.0) +
                               pct * std::log(800.0 / 20000.0));
    damp_coef_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / sample_rate_);
    // LFO and modulation.
    lfo_inc_ = 2.0 * 3.14159265358979323846 * params_.mod_rate_hz / sample_rate_;
    mod_depth_smp_ = params_.mod_depth_ms * sample_rate_ / 1000.0;
    // Effective predelay (the buffer was sized for the 200 ms maximum).
    predelay_.tap = std::max<size_t>(
        1, size_t(std::lround(params_.predelay_ms * sample_rate_ / 1000.0)));
    // Mix and trim.
    mix_ = params_.mix_pct / 100.0;
    dry_ = 1.0 - mix_;
    out_lin_ = std::exp(params_.out_gain_db * 0.11512925464970229);  // ln10/20
}

void PlateReverbRef::prepare() {
    reset();
}

void PlateReverbRef::set_params(const Params& p) {
    params_.decay_s      = std::clamp(p.decay_s, 0.2, 20.0);
    params_.damp_pct     = std::clamp(p.damp_pct, 0.0, 100.0);
    params_.predelay_ms  = std::clamp(p.predelay_ms, 0.0, 200.0);
    params_.mod_rate_hz  = std::clamp(p.mod_rate_hz, 0.0, 8.0);
    params_.mod_depth_ms = std::clamp(p.mod_depth_ms, 0.0, 2.0);
    params_.mix_pct      = std::clamp(p.mix_pct, 0.0, 100.0);
    params_.out_gain_db  = std::clamp(p.out_gain_db, -24.0, 24.0);
    recompute_coefficients();
}

void PlateReverbRef::reset() {
    for (int i = 0; i < kLines; ++i) {
        line_[i].clear();
        lp_[i] = 0.0;
    }
    predelay_.clear();
    for (auto& a : diff_) a.clear();
    for (int i = 0; i < kLines; ++i) {
        lfo_phase_[i] = (2.0 * 3.14159265358979323846 * double(i)) / kLines;
    }
    bad_inputs_ = 0;
}

// ------------------------------ render -------------------------------
void PlateReverbRef::process(float* L, float* R, size_t frames) {
    if (!L) return;
    const double kSqrt8 = 2.8284271247461903;             // sqrt(8)

    for (size_t n = 0; n < frames; ++n) {
        float xl = L[n];
        if (!std::isfinite(xl)) { xl = 0.0f; ++bad_inputs_; }
        float xr = xl;
        if (R) {
            xr = R[n];
            if (!std::isfinite(xr)) { xr = 0.0f; ++bad_inputs_; }
        }

        // ---- input: mono-sum -> predelay -> diffusion ----
        float xin = 0.5f * (xl + (R ? xr : xl));
        xin = predelay_.process(xin);
        float dif = diff_[0].process(diff_[1].process(diff_[2].process(xin)));
        const double fin = double(dif);

        // ---- modulated reads + per-line damping ----
        double lp[kLines];
        double tapL = 0.0, tapR = 0.0;
        for (int i = 0; i < kLines; ++i) {
            const double off = double(line_[i].base) +
                               mod_depth_smp_ * std::sin(lfo_phase_[i]);
            const double r = line_[i].read(off);
            lp_[i] += damp_coef_ * (r - lp_[i]);          // loop one-pole
            lp[i] = lp_[i];
            if (i < 4) tapL += lp_[i]; else tapR += lp_[i];
            lfo_phase_[i] += lfo_inc_;
            if (lfo_phase_[i] >= 2.0 * 3.14159265358979323846)
                lfo_phase_[i] -= 2.0 * 3.14159265358979323846;
        }
        const double wetL = 0.5 * tapL;                   // 4 normalized taps
        const double wetR = 0.5 * tapR;

        // ---- 8x8 Hadamard matrix (3 butterfly stages) over g_i·lp_i ----
        double v[kLines];
        for (int i = 0; i < kLines; ++i) v[i] = line_gain_[i] * lp[i];
        for (int stage = 0; stage < 3; ++stage) {
            const int half = 1 << stage;                  // 1, 2, 4
            for (int blk = 0; blk < kLines; blk += half * 2) {
                for (int i = 0; i < half; ++i) {
                    const double a = v[blk + i];
                    const double b = v[blk + i + half];
                    v[blk + i] = a + b;
                    v[blk + i + half] = a - b;
                }
            }
        }

        // ---- loop writes (diffuse input + feedback) ----
        for (int i = 0; i < kLines; ++i) {
            line_[i].write(float(fin + v[i] / kSqrt8));
        }

        // ---- output: dry/wet mix + trim (finite by construction) ----
        L[n] = float(dry_ * double(xl) * out_lin_ +
                     mix_ * wetL * out_lin_);
        if (R) {
            R[n] = float(dry_ * double(xr) * out_lin_ +
                         mix_ * wetR * out_lin_);
        }
    }
}

} // namespace suite_core

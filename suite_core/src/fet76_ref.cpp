// =====================================================================
// SUITE-CORE / fet76_ref.cpp — Compresor FET feedback (referencia)
// Zona propia del cortafuegos: solo C++ estándar. Sin allocaciones en
// process(); sin excepciones; salida siempre finita.
// =====================================================================
#include "suite_core/fet76_ref.hpp"

#include <algorithm>
#include <cmath>

namespace suite_core {
namespace {
constexpr double kLnToDb = 8.685889638065035;      // 20/ln(10)
constexpr double kDbToLn = 0.11512925464970229;    // ln(10)/20
constexpr double kFloorLin = 1e-12;                // -240 dBFS
} // namespace

void Fet76Ref::set_sample_rate(double sr) {
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
    recompute_coefficients();
}

void Fet76Ref::recompute_coefficients() {
    // One-pole por muestra: coef = 1 - exp(-1/(sr * tau)).
    attack_coef_  = 1.0 - std::exp(-1.0 / (sample_rate_ * std::max(params_.attack_s, 1e-6)));
    release_coef_ = 1.0 - std::exp(-1.0 / (sample_rate_ * std::max(params_.release_s, 1e-4)));
    in_lin_  = std::exp(params_.in_gain_db * kDbToLn);
    out_lin_ = std::exp(params_.out_gain_db * kDbToLn);
}

void Fet76Ref::prepare() {
    reset();
}

void Fet76Ref::set_params(const Params& p) {
    params_.in_gain_db  = std::clamp(p.in_gain_db, -24.0, 24.0);
    params_.thresh_db   = std::clamp(p.thresh_db, -60.0, 0.0);
    params_.ratio       = std::clamp(p.ratio, 1.0, 20.0);
    params_.attack_s    = std::clamp(p.attack_s, 20e-6, 0.8);
    params_.release_s   = std::clamp(p.release_s, 0.01, 2.0);
    params_.out_gain_db = std::clamp(p.out_gain_db, -24.0, 24.0);
    recompute_coefficients();
}

void Fet76Ref::reset() {
    gr_db_ = 0.0;
    y_prev_l_ = 0.0;
    y_prev_r_ = 0.0;
    bad_inputs_ = 0;
}

void Fet76Ref::process(float* L, float* R, size_t frames) {
    if (!L) return;
    const double thresh = params_.thresh_db;
    const double gr_per_over = 1.0 - 1.0 / params_.ratio;   // pendiente de la curva

    for (size_t i = 0; i < frames; ++i) {
        float xl = L[i];
        if (!std::isfinite(xl)) { xl = 0.0f; ++bad_inputs_; }
        float xr = xl;
        if (R) {
            xr = R[i];
            if (!std::isfinite(xr)) { xr = 0.0f; ++bad_inputs_; }
        }

        // ---- Detector de REALIMENTACIÓN: mide la salida de la celda
        //      (muestra anterior, antes del make-up). Enlaza canales por máx.
        const double lvl = std::fmax(std::fabs(y_prev_l_),
                                     R ? std::fabs(y_prev_r_) : 0.0);
        const double lvl_db = (lvl > kFloorLin) ? std::log(lvl) * kLnToDb : -240.0;
        const double over = lvl_db - thresh;
        const double target = (over > 0.0) ? -over * gr_per_over : 0.0;

        // ---- Balística one-pole: attack al cerrar, release al abrir.
        const double coef = (target < gr_db_) ? attack_coef_ : release_coef_;
        gr_db_ += (target - gr_db_) * coef;

        const double g = std::exp(gr_db_ * kDbToLn);   // 10^(gr/20)

        const double yl = (double(xl) * in_lin_) * g;
        L[i] = float(yl * out_lin_);
        y_prev_l_ = yl;
        if (R) {
            const double yr = (double(xr) * in_lin_) * g;
            R[i] = float(yr * out_lin_);
            y_prev_r_ = yr;
        }
    }
}

} // namespace suite_core

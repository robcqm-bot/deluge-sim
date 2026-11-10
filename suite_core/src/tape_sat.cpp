// =====================================================================
// SUITE-CORE / tape_sat.cpp — Saturación de cinta de referencia
// Zona propia del cortafuegos: solo C++ estándar. Sin allocaciones en
// process(); sin excepciones; salida siempre finita.
// =====================================================================
#include "suite_core/tape_sat.hpp"

#include <algorithm>
#include <cmath>

namespace suite_core {

void TapeSatRef::set_sample_rate(double sr) {
    // Se guarda por uniformidad de interfaz con el resto de la suite.
    // La curva es MEMORYLESS y estática: no hay coeficientes que
    // recalcular, fs no altera la transferencia muestra a muestra.
    sample_rate_ = (sr > 0.0) ? sr : 48000.0;
}

void TapeSatRef::recompute_constants() {
    // Constantes del modelo precalculadas: process() no hace pow ni tanh
    // de parámetros, solo un tanh por muestra (la curva en sí).
    g_lin_    = std::pow(10.0, params_.drive_db / 20.0);
    b_        = (params_.bias_pct / 100.0) * 0.5;
    tanh_b_   = std::tanh(b_);
    norm_     = 1.0 - tanh_b_ * tanh_b_;      // en [1-tanh(0.5)^2, 1] > 0
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
    // Rama positiva en |x| y signo aplicado aparte: la imparidad de tanh
    // hace esto idéntico a la fórmula documentada por ramas
    // (tanh(g*x - b) + tanh(b) == -(tanh(g*|x| + b) - tanh(b)) para x<0),
    // pero garantiza y(-x) == -y(x) BIT-EXACTO en cualquier libm.
    // En x = 0: tanh(b) - tanh(b) = 0 exacto => y(0) = 0 sin DC.
    const double ax = g_lin_ * std::fabs(x);
    const double yr = std::tanh(ax + b_) - tanh_b_;
    const double y  = (x < 0.0) ? -yr : yr;
    return y / norm_ * trim_lin_;
}

void TapeSatRef::reset() {
    bad_inputs_ = 0;                          // no hay más estado dinámico
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

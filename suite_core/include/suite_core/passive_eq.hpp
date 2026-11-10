// =====================================================================
// SUITE-CORE / passive_eq.hpp — ZONA PROPIA (cortafuegos GPL)
// =====================================================================
// REGLA DEL CORTAFUEGOS: este módulo NO incluye ningún header de
// deluge_sim ni del firmware Deluge. Solo C++ estándar + buffers crudos.
//
// PassiveEQRef: EQ pasivo de programa de referencia (carácter EQP-1A-ish
// de manual). Diseño genérico — NO es un port de producto alguno.
//
// Modelo documentado (verificado por tests con expectativa exacta):
//   Dos biquad RBJ en cascada (formulas del "cookbook" de RBJ, exactas):
//     1. REFUERZO: peaking en freq_hz, Q=q_boost, ganancia +boost_db.
//     2. RECorte:  peaking en kCutFreqRatio * freq_hz, Q=q_cut, ganancia
//        -cut_db. En un EQ pasivo de programa la banda de atenuación
//        cae POR ENCIMA del refuerzo (~2.5x en este diseño genérico) —
//        de esa separación nace el TRUCO clásico: refuerzo + recorte a
//        la MISMA frecuencia => pico en f0 con un dip encima.
//   La respuesta total es el producto |H_boost| * |H_cut| (+ trim), y
//   magnitude_db() la evalúa ANALÍTICAMENTE — es la expectativa contra
//   la que los tests miden senos estacionarios.
//
//   Ambas etapas son peaking => |H(DC)| = 1 y |H(Nyquist)| = 1 exactos.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace suite_core {

class PassiveEQRef {
public:
    struct Params {
        double freq_hz  = 60.0;    // selector: frecuencia del REFUERZO (20..1000)
        double boost_db = 0.0;     // 0..12  (refuerzo resonante en freq_hz)
        double cut_db   = 0.0;     // 0..12  (recorte en kCutFreqRatio*freq_hz)
        double q_boost  = 0.9;     // 0.3..2 (ancho del refuerzo)
        double q_cut    = 0.6;     // 0.3..2 (ancho del recorte, más program)
        double trim_db  = 0.0;     // -24..24 (match de nivel)
    };

    static constexpr double kCutFreqRatio = 2.5;   // constante del diseño genérico

    void set_sample_rate(double sr);               // recalcula coeficientes
    void prepare();                                // reset de estado dinámico
    void set_params(const Params& p);              // sujeta a rangos (clamp)
    const Params& params() const { return params_; }

    // |H(e^jw)| EXACTA de la cascada (boost * cut, + trim), en dB.
    // Pública a propósito: es la expectativa matemática que los tests
    // verifican midiendo senos estacionarios, y la curva que dibuja la UI.
    double magnitude_db(double freq) const;

    // Procesa in-place. R == nullptr => camino mono (usa solo L).
    // Garantías: sin allocaciones, sin excepciones, salida finita.
    void process(float* L, float* R, size_t frames);

    void reset();                                  // estado dinámico a arranque

    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    // Biquad TDF2 (transposed direct form II) con coeficientes ya
    // normalizados por a0. Estados por canal (estéreo no enlazado: un EQ
    // es LTI, cada canal filtra independiente).
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double s1 = 0.0, s2 = 0.0;
        double process(double x) {
            const double y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            // Guardia denormal: en silencio largo los estados decaen
            // exponencialmente; se anclan a cero (sin coste audible).
            if (std::fabs(s1) < 1e-15) s1 = 0.0;
            if (std::fabs(s2) < 1e-15) s2 = 0.0;
            return y;
        }
    };

    void recompute_coefficients();                 // RBJ peaking -> ambas etapas

    Params params_{};
    double sample_rate_ = 48000.0;
    Biquad boost_[2];                              // [0]=L, [1]=R
    Biquad cut_[2];
    double trim_lin_ = 1.0;
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

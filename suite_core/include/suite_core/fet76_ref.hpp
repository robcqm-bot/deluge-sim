// =====================================================================
// SUITE-CORE / fet76_ref.hpp — ZONA PROPIA (cortafuegos GPL)
// =====================================================================
// REGLA DEL CORTAFUEGOS: este módulo NO incluye ningún header de
// deluge_sim ni del firmware Deluge. Solo C++ estándar + buffers crudos.
// Interfaz: punteros float planar + contexto mínimo. Es lo único que
// viaja a otro destino (M7, Teensy, VST) sin contaminación.
//
// Fet76Ref v0.1: compresor estilo FET de REALIMENTACIÓN (feedback),
// diseño genérico de manual — NO es todavía un port de producto alguno.
// La calibración contra referencias (RE + presets NKS) se conecta
// después DETRÁS de esta misma interfaz.
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
        double in_gain_db  = 0.0;     // drive hacia la celda
        double thresh_db   = -18.0;   // umbral del detector (dBFS)
        double ratio       = 4.0;     // 1..20
        double attack_s    = 0.0008;  // 20e-6 .. 0.8
        double release_s   = 0.25;    // 0.01 .. 2.0
        double out_gain_db = 0.0;     // make-up
    };

    void set_sample_rate(double sr);          // recalcula coeficientes
    void prepare();                           // reset de estado dinámico
    void set_params(const Params& p);         // sujeta a rangos (clamp)
    const Params& params() const { return params_; }

    // Procesa in-place. R == nullptr => camino mono (usa solo L).
    // Garantías: sin allocaciones, sin excepciones, salida finita.
    void process(float* L, float* R, size_t frames);

    void reset();                             // estado dinámico a arranque

    // Telemetría (para tests y UI): reducción de ganancia actual en dB.
    double current_gr_db() const { return gr_db_; }
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_coefficients();

    Params params_{};
    double sample_rate_ = 48000.0;
    double attack_coef_ = 0.0;
    double release_coef_ = 0.0;
    double in_lin_ = 1.0, out_lin_ = 1.0;
    double gr_db_ = 0.0;                      // estado del detector (<=0)
    double y_prev_l_ = 0.0, y_prev_r_ = 0.0;  // realimentación (muestra previa)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

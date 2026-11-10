// =====================================================================
// SUITE-CORE / opto_comp.hpp — ZONA PROPIA (cortafuegos GPL)
// =====================================================================
// REGLA DEL CORTAFUEGOS: este módulo NO incluye ningún header de
// deluge_sim ni del firmware Deluge. Solo C++ estándar + buffers crudos.
// Interfaz: punteros float planar + contexto mínimo. Es lo único que
// viaja a otro destino (M7, Teensy, VST) sin contaminación.
//
// OptoCompRef: compresor "óptico" de referencia — celda electrolumi-
// niscente + fotorresistencia (carácter LA-2A-ish de manual). Diseño
// genérico — NO es todavía un port de producto alguno.
//
// Modelo documentado (verificado por tests con expectativa exacta):
//   1. Detector: envolvente one-pole de max(|L|,|R|) con coeficientes
//      ASIMÉTRICOS (attack al subir, release al bajar). La fotorresi-
//      stencia es lenta: attack por defecto 10 ms.
//   2. Ley de ganancia ESTÁTICA en dB sobre el exceso
//      e = nivel_detector - umbral, con rodilla suave cuadrática de
//      ancho knee_db y pendiente s = 1 - 1/R por encima (R = ratio):
//        e <= 0          : GR = 0
//        0 < e <= knee   : GR = s * e^2 / (2*knee)
//        e >  knee       : GR = s * (e - knee/2)
//      (C1 continua en e=knee; pendiente 0 al entrar en la rodilla.)
//   3. Release PROGRAM-DEPENDENT (la firma del opto): el release
//      interpola linealmente sus COEFICIENTES one-pole entre
//      release_slow_s (GR -> 0) y release_fast_s (|GR| >= gr_span_db).
//      Como coef ~ 1/(fs*tau), eso equivale a interpolar 1/tau: la
//      caída inicial (GR profunda) sale más rápida que interpolando
//      tau directamente, y la cola se alarga al despejar — el carácter
//      de la fotorresistencia real. Los tests fijan la banda medida.
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace suite_core {

class OptoCompRef {
public:
    struct Params {
        double thresh_db      = -12.0;   // umbral del detector (dBFS)
        double ratio          = 3.0;     // 1..20 (pendiente sobre la rodilla)
        double knee_db        = 6.0;     // ancho de rodilla (dB) 0..12
        double attack_s       = 0.010;   // 0.002..0.2 (fotocélula: lenta)
        double release_fast_s = 0.080;   // 0.01..0.5 (tau con GR profunda)
        double release_slow_s = 0.600;   // 0.05..3.0 (tau al despejar)
        double gr_span_db     = 10.0;    // |GR| que satura el release en fast 1..24
        double out_gain_db    = 0.0;     // make-up
    };

    void set_sample_rate(double sr);          // recalcula coeficientes
    void prepare();                           // reset de estado dinámico
    void set_params(const Params& p);         // sujeta a rangos (clamp)
    const Params& params() const { return params_; }

    // Ley estática de GR (dB de reducción, >= 0) para un exceso en dB.
    // Pública a propósito: es LA expectativa matemática que los tests
    // verifican y la UI puede dibujar.
    double gain_law_db(double excess_db) const;

    // Procesa in-place. R == nullptr => camino mono (usa solo L).
    // Garantías: sin allocaciones, sin excepciones, salida finita.
    void process(float* L, float* R, size_t frames);

    void reset();                             // estado dinámico a arranque

    // Telemetría (para tests y UI): GR aplicada actual en dB (<= 0).
    double current_gr_db() const { return gr_db_; }
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_coefficients();

    Params params_{};
    double sample_rate_ = 48000.0;
    double attack_coef_ = 0.0;
    double release_fast_coef_ = 0.0;
    double release_slow_coef_ = 0.0;
    double out_lin_ = 1.0;
    double env_ = 0.0;                        // nivel del detector (lineal)
    double gr_db_ = 0.0;                      // GR aplicada actual (<= 0)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

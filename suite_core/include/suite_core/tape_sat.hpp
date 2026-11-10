// =====================================================================
// SUITE-CORE / tape_sat.hpp — ZONA PROPIA (cortafuegos GPL)
// =====================================================================
// REGLA DEL CORTAFUEGOS: este módulo NO incluye ningún header de
// deluge_sim ni del firmware Deluge. Solo C++ estándar + buffers crudos.
// Interfaz: punteros float planar + contexto mínimo. Es lo único que
// viaja a otro destino (M7, Teensy, VST) sin contaminación.
//
// TapeSatRef: saturación de cinta estilo inserto de canal de referencia
// (curva tanh con bias de cinta, punto de operación desplazado por
// signo). Diseño genérico — NO es todavía un port de producto alguno.
//
// Modelo documentado (verificado por tests con expectativa exacta):
//   g    = 10^(drive_db/20)
//   b    = (bias_pct/100) * 0.5
//   norm = 1 - tanh(b)^2          (normaliza la ganancia small-signal)
//   Para x >= 0: y_raw = tanh(g*x + b) - tanh(b)
//   Para x <  0: y_raw = tanh(g*x - b) + tanh(b)
//        (bias por signo: rodillas asimétricas arriba/abajo, pero como
//         tanh es impar la función global es IMPAR exacta.)
//   y = y_raw / norm * trim_lin   con trim_lin = 10^(trim_db/20)
//
//   Propiedades clave:
//     - y(0) = 0 exacto (sin DC).
//     - y(-x) == -y(x) exacto => sin armónicos pares.
//     - Ganancia small-signal == g exacta: la pendiente en 0 de ambas
//       ramas es g*(1 - tanh(b)^2)/norm = g.
//     - Acotada: |y| <= (1 - tanh(b))/norm * trim_lin < trim_lin/norm.
//
//   El modelo es MEMORYLESS (curva estática aplicada muestra a muestra):
//   no hay estado dinámico ni dependencia de fs. set_sample_rate se
//   guarda únicamente para mantener la interfaz uniforme con el resto
//   de la suite (la curva no cambia con la frecuencia de muestreo).
//
//   Implementación: la rama x < 0 se aplica como -rama(|x|) usando la
//   imparidad de tanh, de modo que la simetría y(-x) == -y(x) sea
//   bit-exacto con independencia del libm de la plataforma.
//
//   Guardia no finita: NaN/Inf -> 0 y contador (patrón de opto_comp).
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace suite_core {

class TapeSatRef {
public:
    struct Params {
        double drive_db = 6.0;    // 0..24 (saturación hacia tanh)
        double bias_pct = 0.0;    // 0..100 (bias de cinta: rodillas asimétricas)
        double trim_db  = 0.0;    // -24..24 (nivel de salida)
    };

    void set_sample_rate(double sr);          // se guarda; la curva no depende de fs
    void prepare();                           // reset (no hay estado dinámico)
    void set_params(const Params& p);         // sujeta a rangos (clamp)
    const Params& params() const { return params_; }

    // Curva EXACTA del modelo para una muestra (LA expectativa matemática
    // que los tests verifican y la UI puede dibujar).
    double curve(double x) const;

    // Procesa in-place. R == nullptr => camino mono (usa solo L).
    // Garantías: sin allocaciones, sin excepciones, salida finita.
    void process(float* L, float* R, size_t frames);

    void reset();                             // estado a arranque (contador a 0)

    // Telemetría (para tests y UI): entradas no finitas sustituidas por 0.
    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    void recompute_constants();

    Params params_{};
    double sample_rate_ = 48000.0;            // solo interfaz uniforme (curva estática)
    double g_lin_ = 1.0;                      // 10^(drive_db/20)
    double b_ = 0.0;                          // bias lineal (0..0.5)
    double tanh_b_ = 0.0;                     // tanh(b)
    double norm_ = 1.0;                       // 1 - tanh(b)^2
    double trim_lin_ = 1.0;                   // 10^(trim_db/20)
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

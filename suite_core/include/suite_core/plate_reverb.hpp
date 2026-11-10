// =====================================================================
// SUITE-CORE / plate_reverb.hpp — ZONA PROPIA (cortafuegos GPL)
// =====================================================================
// REGLA DEL CORTAFUEGOS: este módulo NO incluye ningún header de
// deluge_sim ni del firmware Deluge. Solo C++ estándar + buffers crudos.
//
// PlateReverbRef: reverb de placa de referencia (carácter de placa de
// manual: cola densa, decaimiento suave, decorrelación estéreo).
// Diseño genérico FDN — NO es un port de producto alguno.
//
// Modelo documentado (verificado por tests con expectativa matemática):
//   FDN de 8 líneas de retardo → damping one-pole por línea → matriz de
//   Hadamard 8x8 normalizada (ortogonal: conserva la energía del lazo)
//   → ganancia de realimentación POR LÍNEA g_i = 10^(-3·t_i/RT60).
//   Con esa fórmula, CADA línea decae 10^(-3t/RT60) por unidad de tiempo
//   => la cola entera decae exactamente a RT60 = decay_s (a graves, donde
//   el damping es ~unitario). La matriz ortogonal evita que el lazo
//   explote: energía × g² por vuelta.
//
//   Cadena de entrada: mono-sum 0.5(L+R) -> predelay -> 3 allpass de
//   Schroeder (difusión). Salida mojada: taps 0-3 = L, taps 4-7 = R
//   (decorrelación estéreo por grupos de líneas).
//
//   Modulación: cada línea lee con offset sinusoidal (profundidad
//   mod_depth_ms, tasas mod_rate_hz, fases repartidas 2πi/8) con
//   interpolación lineal — siembra los modos de la placa.
//
//   Damping: fc recorre log-escala de 20 kHz (damp 0%) a 800 Hz (damp
//   100%); coeficiente c = 1 - exp(-2π·fc/fs). En el lajo multiplica
//   una vez por vuelta (t_i por vuelta) => el decaimiento HF es más
//   rápido que el LF cuando damp > 0.
//
//   Bypass exacto: mix 0% => salida == entrada bit a bit. Wet 100%:
//   la salida es exactamente 0 hasta predelay + primera línea (el
//   contenido del FDN arranca en cero).
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace suite_core {

class PlateReverbRef {
public:
    static constexpr int kLines = 8;           // líneas del FDN (público p/ UI)

    struct Params {
        double decay_s     = 2.4;    // RT60 objetivo, 0.2..20 s
        double damp_pct    = 30.0;   // 0..100 (0 = sin damping HF)
        double predelay_ms = 20.0;   // 0..200
        double mod_rate_hz = 0.6;    // 0..8 (LFO común de las 8 líneas)
        double mod_depth_ms = 0.6;   // 0..2 (profundidad del offset)
        double mix_pct     = 30.0;   // 0..100 (0 = seco, 100 = mojado)
        double out_gain_db = 0.0;    // -24..24
    };

    void set_sample_rate(double sr);           // recalcula longitudes/ganancias
    void prepare();                            // reset de estado dinámico
    void set_params(const Params& p);          // sujeta a rangos (clamp)
    const Params& params() const { return params_; }

    // Procesa in-place. R == nullptr => camino mono (usa solo L).
    // Garantías: sin allocaciones, sin excepciones, salida finita.
    void process(float* L, float* R, size_t frames);

    void reset();

    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    // Línea de retardo con lectura fraccional (interpolación lineal)
    // para la modulación. El buffer es potencia-de-2 libre: circular.
    struct ModDelay {
        std::vector<float> buf;
        size_t wpos = 0;                       // índice de escritura siguiente
        size_t base = 1;                       // longitud base en muestras
        size_t tap = 1;                        // offset de lectura para process()
        void resize(size_t samples, size_t extra);
        double read(double offset) const;      // offset >= 1
        void write(float v);
        void clear();
        // Retardo simple (predelay): lee a 'tap' y escribe x.
        double process(double x) {
            const double r = read(double(tap));
            write(float(x));
            return r;
        }
    };

    // Allpass de Schroeder de una etapa: y = -g·x + x[n-D] + g·y[n-D].
    struct Allpass {
        std::vector<float> buf;
        size_t wpos = 0;
        double g = 0.5;
        void resize(size_t delay);
        float process(float x);
        void clear();
    };

    void recompute_coefficients();

    Params params_{};
    double sample_rate_ = 48000.0;

    // 8 líneas + estados (longitudes base @48k en muestras, coprimas entre sí)
    static const size_t kBaseSamples[kLines];
    ModDelay line_[kLines];
    double lp_[kLines] = {};                   // estado del damping por línea
    double damp_coef_ = 1.0;                   // 1 = sin damping
    double line_gain_[kLines] = {};            // g_i = 10^(-3 t_i / RT60)

    double lfo_phase_[kLines] = {};            // fase actual de cada LFO
    double lfo_inc_ = 0.0;                     // 2π·rate/fs por muestra
    double mod_depth_smp_ = 0.0;               // profundidad en muestras

    ModDelay predelay_;
    Allpass diff_[3];

    double mix_ = 0.3, dry_ = 0.7, out_lin_ = 1.0;
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

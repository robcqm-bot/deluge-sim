// =====================================================================
// SUITE-CORE / plate_reverb.hpp — OWN ZONE (GPL firewall)
// =====================================================================
// FIREWALL RULE: this module does NOT include any header from
// deluge_sim or the Deluge firmware. Standard C++ only + raw buffers.
//
// PlateReverbRef: reference plate reverb (textbook plate character:
// dense tail, smooth decay, stereo decorrelation).
// Generic FDN design — NOT a port of any product.
//
// Documented model (verified by tests with mathematical expectation):
//   FDN of 8 delay lines → one-pole damping per line → normalized 8x8
//   Hadamard matrix (orthogonal: preserves loop energy) → per-LINE
//   feedback gain g_i = 10^(-3·t_i/RT60).
//   With that formula, EACH line decays 10^(-3t/RT60) per unit of time
//   => the whole tail decays at exactly RT60 = decay_s (at low
//   frequencies, where damping is ~unity). The orthogonal matrix keeps
//   the loop from blowing up: energy × g² per round trip.
//
//   Input chain: mono-sum 0.5(L+R) -> predelay -> 3 Schroeder allpass
//   stages (diffusion). Wet output: taps 0-3 = L, taps 4-7 = R (stereo
//   decorrelation by line groups).
//
//   Modulation: each line reads with a sinusoidal offset (depth
//   mod_depth_ms, rates mod_rate_hz, phases spread 2πi/8) with linear
//   interpolation — seeds the plate modes.
//
//   Damping: fc sweeps the log scale from 20 kHz (damp 0%) to 800 Hz
//   (damp 100%); coefficient c = 1 - exp(-2π·fc/fs). In the loop it
//   multiplies once per round (t_i per round) => HF decay is faster
//   than LF when damp > 0.
//
//   Exact bypass: mix 0% => output == input bit for bit. Wet 100%:
//   the output is exactly 0 until predelay + first line (the FDN
//   content starts from zero).
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace suite_core {

class PlateReverbRef {
public:
    static constexpr int kLines = 8;           // FDN lines (public for UI)

    struct Params {
        double decay_s     = 2.4;    // target RT60, 0.2..20 s
        double damp_pct    = 30.0;   // 0..100 (0 = no HF damping)
        double predelay_ms = 20.0;   // 0..200
        double mod_rate_hz = 0.6;    // 0..8 (LFO shared by the 8 lines)
        double mod_depth_ms = 0.6;   // 0..2 (offset depth)
        double mix_pct     = 30.0;   // 0..100 (0 = dry, 100 = wet)
        double out_gain_db = 0.0;    // -24..24
    };

    void set_sample_rate(double sr);           // recomputes lengths/gains
    void prepare();                            // resets dynamic state
    void set_params(const Params& p);          // clamps to valid ranges
    const Params& params() const { return params_; }

    // Processes in-place. R == nullptr => mono path (uses L only).
    // Guarantees: no allocations, no exceptions, finite output.
    void process(float* L, float* R, size_t frames);

    void reset();

    uint64_t bad_input_count() const { return bad_inputs_; }

private:
    // Delay line with fractional read (linear interpolation) for the
    // modulation. The buffer needs no power-of-2 size: circular.
    struct ModDelay {
        std::vector<float> buf;
        size_t wpos = 0;                       // next write index
        size_t base = 1;                       // base length in samples
        size_t tap = 1;                        // read offset for process()
        void resize(size_t samples, size_t extra);
        double read(double offset) const;      // offset >= 1
        void write(float v);
        void clear();
        // Simple delay (predelay): reads at 'tap' and writes x.
        double process(double x) {
            const double r = read(double(tap));
            write(float(x));
            return r;
        }
    };

    // Single-stage Schroeder allpass: y = -g·x + x[n-D] + g·y[n-D].
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

    // 8 lines + states (base lengths @48k in samples, pairwise coprime)
    static const size_t kBaseSamples[kLines];
    ModDelay line_[kLines];
    double lp_[kLines] = {};                   // per-line damping state
    double damp_coef_ = 1.0;                   // 1 = no damping
    double line_gain_[kLines] = {};            // g_i = 10^(-3 t_i / RT60)

    double lfo_phase_[kLines] = {};            // current phase of each LFO
    double lfo_inc_ = 0.0;                     // 2π·rate/fs per sample
    double mod_depth_smp_ = 0.0;               // depth in samples

    ModDelay predelay_;
    Allpass diff_[3];

    double mix_ = 0.3, dry_ = 0.7, out_lin_ = 1.0;
    uint64_t bad_inputs_ = 0;
};

} // namespace suite_core

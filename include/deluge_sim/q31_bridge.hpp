// =====================================================================
// deluge-sim / q31_bridge.hpp — float <-> q31 bridge (firmware boundary)
// v0.2 (scale fix: audio unity 2^31, verified against Deluge firmware
//       v1.3.0 source)
//
// THE BRIDGE between the two audio worlds:
//   - This sim: planar float L/R, amplitude +-1.0.
//   - Real Deluge firmware: integer audio carried in
//         struct StereoSample { int32_t l; int32_t r; }
//     with unity 2^31 == 2147483648 (Q2.31). Evidence (firmware
//     v1.3.0): util/fixedpoint.h defines ONE_Q31 = 2147483647; the
//     metronome adds q31 sample values to sample.l/r directly; the DAC
//     applies sample * masterVolume >> 32 (full q31 multiply).
//     Interleaved layout: {l, r} per frame. NOTE: pan/param SCALARS
//     elsewhere in the firmware are Q2.29, but the AUDIO SAMPLES are
//     Q2.31 — do not confuse the two.
//
// Exact conversion convention used EVERYWHERE in this repo (the golden
// vectors depend on it; do not change without bumping the format):
//   float -> q31:  NaN -> 0 (harness convention, suite_core pattern);
//                  clamp the rest to [-1.0, +1.0] (+-Inf clamp to
//                  +-1.0 like any out-of-range value), then
//                  llround(x * 2147483648.0) with the rounded result
//                  saturated to the int32 range [-2147483648, 2147483647].
//                  (llround, not lround: on Win64 `long` is 32-bit and
//                  the +2^31 product would be an out-of-domain call.)
//   q31 -> float:  y / 2147483648.0   (division by a power of two:
//                  exact in IEEE-754, no rounding).
//
//   ASYMMETRIC EXTREMES (raw int32 q31): +1.0 scales to +2147483648,
//   which is NOT representable in int32, so it saturates to
//   INT32_MAX = 2147483647 — that is 0.99999999953 when converted back
//   (exactly one LSB below unity; a float32 round-trip still returns
//   1.0f because 1 - 2^-31 rounds to 1.0 in float precision). -1.0
//   scales to INT32_MIN = -2147483648 exactly. So
//   q31(-1.0) == -q31(1.0) - 1: unlike the old +-2^29 convention, the
//   range is NOT fully symmetric at the extremes. For every |x| < 1.0
//   the exact symmetry q31(-x) == -q31(x) still holds (round-half-
//   away-from-zero is an odd function).
//
// Verified against the real firmware paths (the former TODO(M1) audit):
// no hidden shifts — StereoSample audio is consumed as plain Q2.31.
//
// Allocation rule: the conversion functions NEVER allocate; every
// buffer is caller-provided (same discipline as suite_core).
// =====================================================================
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace deluge_sim {

// Q2.31 unity: 1.0 == 2^31 == 2147483648 in q31 units. This is the
// SCALE (reported by the golden JSON "q31_scale" field); it does NOT
// fit in int32 — the largest representable sample is INT32_MAX
// (2147483647), one LSB below unity (see the asymmetric-extremes note
// in the banner). int64 so the value survives intact.
constexpr int64_t kQ31Unity = 2147483648LL;

// One least-significant bit of Q2.31 expressed in float units.
constexpr double kQ31Lsb = 1.0 / 2147483648.0;

// Scalar conversions (single sample). Inline: shared by the tests, the
// golden CLI command and the frame converters below.
inline int32_t float_to_q31(double x) {
    if (std::isnan(x)) return 0;             // documented: NaN -> 0
    if (x > 1.0) x = 1.0;                    // clamp (also +Inf)
    if (x < -1.0) x = -1.0;                  // clamp (also -Inf)
    // Scale to Q2.31 and saturate the ROUNDED result to int32: the
    // product of +1.0 is +2^31, one past INT32_MAX, so everything
    // within half an LSB of unity rounds to 2147483648 and saturates
    // down to 2147483647 (documented asymmetric extreme); -1.0 gives
    // -2147483648 exactly. llround keeps the intermediate in 64 bits
    // even on Win64, where `long` (lround's return type) is 32-bit and
    // +2^31 would not fit.
    const long long r = std::llround(x * 2147483648.0);
    if (r > 2147483647LL) return 2147483647;   // +1.0 -> INT32_MAX
    if (r < -2147483648LL) return -2147483648; // defensive; exact -1.0
    return static_cast<int32_t>(r);
}

inline double q31_to_float(int32_t v) {
    return double(v) / 2147483648.0;
}

// Planar float L/R -> interleaved q31 pairs (firmware StereoSample
// layout): out_pairs[2*i] = q31(L[i]), out_pairs[2*i+1] = q31(R[i]).
// Caller owns out_pairs (2*frames int32). No allocation.
void interleave_q31_from_planar(const float* L, const float* R,
                                int32_t* out_pairs, size_t frames);

// Inverse: interleaved q31 pairs -> planar float L/R. Caller owns L and
// R (frames floats each). No allocation.
void planar_from_interleave_q31(const int32_t* pairs,
                                float* L, float* R, size_t frames);

} // namespace deluge_sim

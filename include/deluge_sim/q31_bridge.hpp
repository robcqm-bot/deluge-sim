// =====================================================================
// deluge-sim / q31_bridge.hpp — float <-> q31 bridge (firmware boundary)
// v0.1
//
// THE BRIDGE between the two audio worlds:
//   - This sim: planar float L/R, amplitude +-1.0.
//   - Real Deluge firmware: integer audio carried in
//         struct StereoSample { int32_t l; int32_t r; }
//     with amplitude ~Q2.29: the value 2^29 == 536870912 represents
//     1.0 (sim full scale). Interleaved layout: {l, r} per frame.
//
// Exact conversion convention used EVERYWHERE in this repo (the golden
// vectors depend on it; do not change without bumping the format):
//   float -> q31:  NaN -> 0 (harness convention, suite_core pattern);
//                  clamp the rest to [-1.0, +1.0] (+-Inf clamp to
//                  +-1.0 like any out-of-range value), then
//                  lround(x * 536870912.0).
//   q31 -> float:  y / 536870912.0   (division by a power of two:
//                  exact in IEEE-754, no rounding).
//
//   Resulting q31 range: [-536870912, +536870912] — FULLY SYMMETRIC.
//   Unlike raw int32 q31 (where the minimum -2^31 has no positive
//   counterpart), -1.0 -> -2^29 and +1.0 -> +2^29 are both
//   representable, so there is NO asymmetric-minimum special case.
//
// TODO(M1): cross-check this convention against the REAL firmware
// paths. Some firmware code shifts q31 by <<2 on the way in/out; until
// that audit is done, this bridge deliberately implements ONLY the
// documented Q2.29 convention above — no hidden shifts.
//
// Allocation rule: the conversion functions NEVER allocate; every
// buffer is caller-provided (same discipline as suite_core).
// =====================================================================
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace deluge_sim {

// 1.0 in Q2.29 (2^29). Also the saturation bound in q31 units.
constexpr int32_t kQ31Unity = 536870912;

// One least-significant bit of Q2.29 expressed in float units.
constexpr double kQ31Lsb = 1.0 / 536870912.0;

// Scalar conversions (single sample). Inline: shared by the tests, the
// golden CLI command and the frame converters below.
inline int32_t float_to_q31(double x) {
    if (std::isnan(x)) return 0;             // documented: NaN -> 0
    if (x > 1.0) x = 1.0;                    // clamp (also +Inf)
    if (x < -1.0) x = -1.0;                  // clamp (also -Inf)
    return static_cast<int32_t>(std::lround(x * 536870912.0));
}

inline double q31_to_float(int32_t v) {
    return double(v) / 536870912.0;
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

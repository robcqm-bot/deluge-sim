// =====================================================================
// deluge-sim / q31_bridge.cpp — float <-> q31 bridge implementation
// See q31_bridge.hpp for the documented Q2.31 convention (unity 2^31 ==
// 2147483648, clamp to +-1.0, llround saturated to int32 with the
// asymmetric-extremes note). Every buffer here is caller-provided: no
// allocation, no exceptions.
// =====================================================================
#include "deluge_sim/q31_bridge.hpp"

namespace deluge_sim {

void interleave_q31_from_planar(const float* L, const float* R,
                                int32_t* out_pairs, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        out_pairs[2 * i]     = float_to_q31(double(L[i]));
        out_pairs[2 * i + 1] = float_to_q31(double(R[i]));
    }
}

void planar_from_interleave_q31(const int32_t* pairs,
                                float* L, float* R, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        L[i] = static_cast<float>(q31_to_float(pairs[2 * i]));
        R[i] = static_cast<float>(q31_to_float(pairs[2 * i + 1]));
    }
}

} // namespace deluge_sim

// =====================================================================
// deluge-sim / adapter_plate.hpp — Adaptador de la reverb de placa
// Mismo patrón de cortafuegos que adapter_fet76.hpp: único puente
// delimitado entre deluge_sim y suite_core para PlateReverbRef.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/plate_reverb.hpp"

namespace deluge_sim {

class AdapterPlateReverb : public IProcessor {
public:
    explicit AdapterPlateReverb(const suite_core::PlateReverbRef::Params& p) {
        r_.set_params(p);
    }
    const char* name() const override { return "plate_reverb"; }
    void prepare(const ProcessContext& ctx) override {
        r_.set_sample_rate(ctx.sample_rate);
        r_.prepare();
    }
    void process(Block& b) override {
        r_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { r_.reset(); }
    uint64_t bad_inputs() const { return r_.bad_input_count(); }

private:
    suite_core::PlateReverbRef r_;
};

} // namespace deluge_sim

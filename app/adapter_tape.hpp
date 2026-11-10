// =====================================================================
// deluge-sim / adapter_tape.hpp — Adaptador del procesador de cinta
// Mismo patrón de cortafuegos que adapter_opto.hpp: único puente
// delimitado entre deluge_sim y suite_core para TapeSatRef.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/tape_sat.hpp"

namespace deluge_sim {

class AdapterTapeSat : public IProcessor {
public:
    explicit AdapterTapeSat(const suite_core::TapeSatRef::Params& p) { t_.set_params(p); }
    const char* name() const override { return "tape_sat"; }
    void prepare(const ProcessContext& ctx) override {
        t_.set_sample_rate(ctx.sample_rate);
        t_.prepare();
    }
    void process(Block& b) override {
        t_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { t_.reset(); }
    uint64_t bad_inputs() const { return t_.bad_input_count(); }

private:
    suite_core::TapeSatRef t_;
};

} // namespace deluge_sim

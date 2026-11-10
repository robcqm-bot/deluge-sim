// =====================================================================
// deluge-sim / adapter_eq.hpp — Adaptador del EQ pasivo de programa
// Mismo patrón de cortafuegos que adapter_fet76.hpp: único puente
// delimitado entre deluge_sim y suite_core para PassiveEQRef.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/passive_eq.hpp"

namespace deluge_sim {

class AdapterPassiveEQ : public IProcessor {
public:
    explicit AdapterPassiveEQ(const suite_core::PassiveEQRef::Params& p) {
        e_.set_params(p);
    }
    const char* name() const override { return "passive_eq"; }
    void prepare(const ProcessContext& ctx) override {
        e_.set_sample_rate(ctx.sample_rate);
        e_.prepare();
    }
    void process(Block& b) override {
        e_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { e_.reset(); }
    uint64_t bad_inputs() const { return e_.bad_input_count(); }

private:
    suite_core::PassiveEQRef e_;
};

} // namespace deluge_sim

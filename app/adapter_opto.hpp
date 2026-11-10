// =====================================================================
// deluge-sim / adapter_opto.hpp — Opto processor adapter
// Same firewall pattern as adapter_fet76.hpp: only bounded
// bridge between deluge_sim and suite_core for OptoCompRef.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/opto_comp.hpp"

namespace deluge_sim {

class AdapterOpto : public IProcessor {
public:
    explicit AdapterOpto(const suite_core::OptoCompRef::Params& p) { o_.set_params(p); }
    const char* name() const override { return "opto_comp_ref"; }
    void prepare(const ProcessContext& ctx) override {
        o_.set_sample_rate(ctx.sample_rate);
        o_.prepare();
    }
    void process(Block& b) override {
        o_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { o_.reset(); }
    double gr_db() const { return o_.current_gr_db(); }
    uint64_t bad_inputs() const { return o_.bad_input_count(); }

private:
    suite_core::OptoCompRef o_;
};

} // namespace deluge_sim

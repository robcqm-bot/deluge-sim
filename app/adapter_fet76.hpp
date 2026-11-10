// =====================================================================
// deluge-sim / adapter_fet76.hpp — THE ADAPTER (firewall)
// Only bounded bridge between deluge_sim and suite_core: translates
// deluge_sim::Block (planar) to raw float* pointers (suite-core style).
// Shared by simcli (main.cpp) and boardsim (board_main.cpp).
// In the real firmware integration this file lives in
// src/deluge/dsp/suite/fet76_adapter.cpp.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/fet76_ref.hpp"

namespace deluge_sim {

// App layer (outside the firewall): knows both sides and does not expose
// suite_core types except through the constructor Params.
class AdapterFet76 : public IProcessor {
public:
    explicit AdapterFet76(const suite_core::Fet76Ref::Params& p) { f_.set_params(p); }
    const char* name() const override { return "fet76_ref"; }
    void prepare(const ProcessContext& ctx) override {
        f_.set_sample_rate(ctx.sample_rate);
        f_.prepare();
    }
    void process(Block& b) override {
        f_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { f_.reset(); }
    double gr_db() const { return f_.current_gr_db(); }
    uint64_t bad_inputs() const { return f_.bad_input_count(); }

private:
    suite_core::Fet76Ref f_;
};

} // namespace deluge_sim

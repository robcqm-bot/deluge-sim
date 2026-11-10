// =====================================================================
// deluge-sim / adapter_fet76.hpp — EL ADAPTADOR (cortafuegos)
// Único puente delimitado entre deluge_sim y suite_core: traduce
// deluge_sim::Block (planar) a punteros crudos float* (tipo suite-core).
// Compartido por simcli (main.cpp) y boardsim (board_main.cpp).
// En la integración real al firmware este archivo vive en
// src/deluge/dsp/suite/fet76_adapter.cpp.
// =====================================================================
#pragma once
#include "deluge_sim/core.hpp"
#include "suite_core/fet76_ref.hpp"

namespace deluge_sim {

// Capa app (fuera del cortafuegos): conoce ambos lados y no expone
// tipos de suite_core salvo por los Params del constructor.
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

// =====================================================================
// deluge-sim / fx_factory.hpp — Shared FX registry (app layer)
// SINGLE source of truth for: valid keys per fx, effective defaults,
// and building processors from a parameter map.
// Consumed by simcli (main.cpp) and boardsim (board_main.cpp) — so the
// CLI and the board cannot diverge in defaults or keys.
// App layer: knows deluge_sim and the adapters; never crosses into
// suite_core directly (the adapters are the bridge).
// =====================================================================
#pragma once
#include "adapter_eq.hpp"
#include "adapter_fet76.hpp"
#include "adapter_opto.hpp"
#include "adapter_plate.hpp"
#include "adapter_tape.hpp"
#include "deluge_sim/core.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace deluge_sim {

// k=v keys known by each fx (unprefixed). The "fx." prefix targets
// a single link of the chain.
inline const std::map<std::string, std::vector<const char*>>& fx_keys() {
    static const std::map<std::string, std::vector<const char*>> m = {
        {"fet76", {"in_gain_db", "thresh_db", "ratio", "attack_s",
                   "release_s", "out_gain_db"}},
        {"opto",  {"thresh_db", "ratio", "knee_db", "attack_s",
                   "release_fast_s", "release_slow_s", "gr_span_db",
                   "out_gain_db"}},
        {"eq",    {"freq_hz", "boost_db", "cut_db", "q_boost", "q_cut",
                   "trim_db"}},
        {"plate", {"decay_s", "damp_pct", "predelay_ms", "mod_rate_hz",
                   "mod_depth_ms", "mix_pct", "out_gain_db"}},
        {"tape",  {"drive_db", "bias_pct", "trim_db"}},
    };
    return m;
}

// A key is valid if: unprefixed and some fx in the chain has it,
// or with the "fx." prefix and that fx has it.
inline bool key_known(const std::string& k, const std::vector<std::string>& chain) {
    const size_t dot = k.find('.');
    if (dot != std::string::npos) {
        const std::string fx = k.substr(0, dot), rest = k.substr(dot + 1);
        if (rest.find('.') != std::string::npos) return false;
        const auto it = fx_keys().find(fx);
        if (it == fx_keys().end()) return false;
        for (const char* vk : it->second)
            if (rest == vk) return true;
        return false;
    }
    for (const std::string& fx : chain) {
        for (const char* vk : fx_keys().at(fx))
            if (k == vk) return true;
    }
    return false;
}

// Value of 'key' for fx 'name': first the prefixed "name.key",
// then the unprefixed one (applies to all), and otherwise the default.
inline double kv_val(const std::map<std::string, double>& kv,
                     const std::string& name, const char* key, double def) {
    auto it = kv.find(name + "." + key);
    if (it != kv.end()) return it->second;
    it = kv.find(key);
    return (it != kv.end()) ? it->second : def;
}

// EFFECTIVE values per fx: struct defaults + k=v (prefixed or not).
inline std::map<std::string, double> effective_fx_params(
    const std::map<std::string, double>& kv, const std::string& fx) {
    std::map<std::string, double> m;
    const auto put = [&](const char* k, double def) { m[k] = kv_val(kv, fx, k, def); };
    if (fx == "fet76") {
        suite_core::Fet76Ref::Params d;
        put("in_gain_db", d.in_gain_db);   put("thresh_db", d.thresh_db);
        put("ratio", d.ratio);             put("attack_s", d.attack_s);
        put("release_s", d.release_s);     put("out_gain_db", d.out_gain_db);
    } else if (fx == "opto") {
        suite_core::OptoCompRef::Params d;
        put("thresh_db", d.thresh_db);     put("ratio", d.ratio);
        put("knee_db", d.knee_db);         put("attack_s", d.attack_s);
        put("release_fast_s", d.release_fast_s);
        put("release_slow_s", d.release_slow_s);
        put("gr_span_db", d.gr_span_db);   put("out_gain_db", d.out_gain_db);
    } else if (fx == "eq") {
        suite_core::PassiveEQRef::Params d;
        put("freq_hz", d.freq_hz);         put("boost_db", d.boost_db);
        put("cut_db", d.cut_db);           put("q_boost", d.q_boost);
        put("q_cut", d.q_cut);             put("trim_db", d.trim_db);
    } else if (fx == "plate") {
        suite_core::PlateReverbRef::Params d;
        put("decay_s", d.decay_s);         put("damp_pct", d.damp_pct);
        put("predelay_ms", d.predelay_ms); put("mod_rate_hz", d.mod_rate_hz);
        put("mod_depth_ms", d.mod_depth_ms);
        put("mix_pct", d.mix_pct);         put("out_gain_db", d.out_gain_db);
    } else if (fx == "tape") {
        suite_core::TapeSatRef::Params d;
        put("drive_db", d.drive_db);       put("bias_pct", d.bias_pct);
        put("trim_db", d.trim_db);
    }
    return m;
}

namespace detail {
inline double getd(const std::map<std::string, double>& m, const char* k, double def) {
    const auto it = m.find(k);
    return (it != m.end()) ? it->second : def;
}
} // namespace detail

// Builds the processor for fx 'name' from a parameter map
// (tolerates partial maps: missing entries take the default). nullptr if
// the fx is unknown. set_params centralizes clamping.
inline std::unique_ptr<IProcessor> make_fx(
    const std::string& name, const std::map<std::string, double>& p) {
    using detail::getd;
    if (name == "fet76") {
        suite_core::Fet76Ref::Params d;
        d.in_gain_db  = getd(p, "in_gain_db", d.in_gain_db);
        d.thresh_db   = getd(p, "thresh_db", d.thresh_db);
        d.ratio       = getd(p, "ratio", d.ratio);
        d.attack_s    = getd(p, "attack_s", d.attack_s);
        d.release_s   = getd(p, "release_s", d.release_s);
        d.out_gain_db = getd(p, "out_gain_db", d.out_gain_db);
        return std::make_unique<AdapterFet76>(d);
    }
    if (name == "opto") {
        suite_core::OptoCompRef::Params d;
        d.thresh_db      = getd(p, "thresh_db", d.thresh_db);
        d.ratio          = getd(p, "ratio", d.ratio);
        d.knee_db        = getd(p, "knee_db", d.knee_db);
        d.attack_s       = getd(p, "attack_s", d.attack_s);
        d.release_fast_s = getd(p, "release_fast_s", d.release_fast_s);
        d.release_slow_s = getd(p, "release_slow_s", d.release_slow_s);
        d.gr_span_db     = getd(p, "gr_span_db", d.gr_span_db);
        d.out_gain_db    = getd(p, "out_gain_db", d.out_gain_db);
        return std::make_unique<AdapterOpto>(d);
    }
    if (name == "eq") {
        suite_core::PassiveEQRef::Params d;
        d.freq_hz  = getd(p, "freq_hz", d.freq_hz);
        d.boost_db = getd(p, "boost_db", d.boost_db);
        d.cut_db   = getd(p, "cut_db", d.cut_db);
        d.q_boost  = getd(p, "q_boost", d.q_boost);
        d.q_cut    = getd(p, "q_cut", d.q_cut);
        d.trim_db  = getd(p, "trim_db", d.trim_db);
        return std::make_unique<AdapterPassiveEQ>(d);
    }
    if (name == "plate") {
        suite_core::PlateReverbRef::Params d;
        d.decay_s      = getd(p, "decay_s", d.decay_s);
        d.damp_pct     = getd(p, "damp_pct", d.damp_pct);
        d.predelay_ms  = getd(p, "predelay_ms", d.predelay_ms);
        d.mod_rate_hz  = getd(p, "mod_rate_hz", d.mod_rate_hz);
        d.mod_depth_ms = getd(p, "mod_depth_ms", d.mod_depth_ms);
        d.mix_pct      = getd(p, "mix_pct", d.mix_pct);
        d.out_gain_db  = getd(p, "out_gain_db", d.out_gain_db);
        return std::make_unique<AdapterPlateReverb>(d);
    }
    if (name == "tape") {
        suite_core::TapeSatRef::Params d;
        d.drive_db = getd(p, "drive_db", d.drive_db);
        d.bias_pct = getd(p, "bias_pct", d.bias_pct);
        d.trim_db  = getd(p, "trim_db", d.trim_db);
        return std::make_unique<AdapterTapeSat>(d);
    }
    return nullptr;
}

} // namespace deluge_sim

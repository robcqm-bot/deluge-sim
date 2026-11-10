// =====================================================================
// deluge-sim / preset.hpp — JSON import/export of chain presets
// Own minimal JSON: no external dependencies (no nlohmann or the like).
// File format (UTF-8, indented with 2 spaces when saving):
//   {
//     "version": 1,
//     "chain": ["eq", "plate"],
//     "params": {
//       "eq":    { "freq_hz": 150, "boost_db": 3.5 },
//       "plate": { "decay_s": 3.2, "damp_pct": 45 }
//     }
//   }
// - "chain": fx in insertion order. "params": value per fx/parameter.
// - Numbers written with %.17g: save+load is bit-exact for
//   any finite double (including -0.0 and denormals).
// - Tolerant reading (documented forward-compat): unknown keys are
//   silently skipped and true/false/null as the value of
//   "version"/"chain"/"params" is accepted and ignored. Inside
//   "chain" (strings only) and "params" (numbers only) types are
//   STRICT: anything else => false + a clear err.
// - Errors: bool + actionable 'err'; syntax errors carry a line number
//   ("línea 3: se esperaba ':'"). No exceptions thrown outward.
// =====================================================================
#pragma once
#include <map>
#include <string>
#include <vector>

namespace deluge_sim {

struct ChainPreset {
    int version = 1;
    std::vector<std::string> chain;                       // fx in insertion order
    // params per fx: fx name -> (param key -> value)
    std::map<std::string, std::map<std::string, double>> params;
};

// Saves the preset as JSON to 'path'. false + err on disk failure or
// if any value is not finite (JSON cannot represent NaN/Inf).
bool save_preset(const std::string& path, const ChainPreset& p, std::string& err);

// Loads a preset from 'path'. false + err (with "línea N" for JSON
// syntax errors). 'out' is only modified if the load succeeded.
bool load_preset(const std::string& path, ChainPreset& out, std::string& err);

} // namespace deluge_sim

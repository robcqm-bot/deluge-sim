// =====================================================================
// deluge-sim / preset.hpp — Import/export de presets de cadena en JSON
// JSON mínimo propio: sin dependencias externas (ni nlohmann ni similar).
// Formato de archivo (UTF-8, con sangría de 2 espacios al guardar):
//   {
//     "version": 1,
//     "chain": ["eq", "plate"],
//     "params": {
//       "eq":    { "freq_hz": 150, "boost_db": 3.5 },
//       "plate": { "decay_s": 3.2, "damp_pct": 45 }
//     }
//   }
// - "chain": fx en orden de inserción. "params": valor por fx/parámetro.
// - Números escritos con %.17g: guardar+cargar es bit-exacto para
//   cualquier double finito (incluidos -0.0 y denormales).
// - Lectura tolerante (forward-compat documentado): las claves
//   desconocidas se saltan silenciosamente y true/false/null como valor
//   de "version"/"chain"/"params" se aceptan y se ignoran. Dentro de
//   "chain" (solo strings) y "params" (solo números) los tipos son
//   ESTRICTOS: cualquier otra cosa => false + err claro.
// - Errores: bool + 'err' accionable; los de sintaxis llevan número de
//   línea ("línea 3: se esperaba ':'"). Sin excepciones hacia fuera.
// =====================================================================
#pragma once
#include <map>
#include <string>
#include <vector>

namespace deluge_sim {

struct ChainPreset {
    int version = 1;
    std::vector<std::string> chain;                       // fx en orden de inserción
    // params por fx: nombre fx -> (clave param -> valor)
    std::map<std::string, std::map<std::string, double>> params;
};

// Guarda el preset como JSON en 'path'. false + err si falla el disco o
// si algún valor no es finito (el JSON no representa NaN/Inf).
bool save_preset(const std::string& path, const ChainPreset& p, std::string& err);

// Carga un preset desde 'path'. false + err (con "línea N" para errores
// de sintaxis JSON). 'out' solo se modifica si la carga fue exitosa.
bool load_preset(const std::string& path, ChainPreset& out, std::string& err);

} // namespace deluge_sim

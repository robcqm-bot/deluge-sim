// =====================================================================
// deluge-sim / test_preset.inc — Preset import/export tests
// CONTRACT: included VERBATIM inside the anonymous namespace of
// tests/test_all.cpp, after check() and make_sine(...). No #include,
// no main() and no check() of its own: only test_preset_xxx() and
// file-static helpers. Only header the integrator must add to
// test_all.cpp:   #include "deluge_sim/preset.hpp"
// =====================================================================

// ------------------------------ helpers ------------------------------

// Equality by BITS of doubles (the values in these tests are finite:
// NaN never arrives because save_preset rejects it). ±0 distinguished
// with signbit.
static bool preset_bits_equal(double a, double b) {
    if (a != b) return false;                           // NaN also lands here
    if (a == 0.0) return std::signbit(a) == std::signbit(b);
    return true;
}

// Deep comparison of 'params' with exact bit semantics.
static bool preset_params_bits_equal(const ChainPreset& a, const ChainPreset& b) {
    if (a.params.size() != b.params.size()) return false;
    auto ia = a.params.begin();
    auto ib = b.params.begin();
    for (; ia != a.params.end(); ++ia, ++ib) {
        if (ia->first != ib->first) return false;
        if (ia->second.size() != ib->second.size()) return false;
        auto ja = ia->second.begin();
        auto jb = ib->second.begin();
        for (; ja != ia->second.end(); ++ja, ++jb) {
            if (ja->first != jb->first) return false;
            if (!preset_bits_equal(ja->second, jb->second)) return false;
        }
    }
    return true;
}

// Writes plain text (for hand-written broken JSON cases).
static bool preset_write_text(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return bool(f);
}

// Reads a whole file into a string (no istreambuf_iterator to avoid
// depending on <iterator>, which is not in the contract's list).
static std::string preset_read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (!s.empty()) f.read(&s[0], std::streamsize(s.size()));
    return s;
}

// Broken JSON case: text and label for the messages.
struct PresetBrokenCase {
    const char* text;
    const char* label;
};

// ------------------------------ tests --------------------------------

// a) Bit-exact roundtrip: "ugly" values (pi, 1e-9, -45.5, 1e21, 0.0,
//    plus -0.0 and the minimum denormal) survive save+load identically.
void test_preset_roundtrip() {
    std::cout << "[test] preset: roundtrip JSON bit-exacto con valores feos\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset.json").string();
    ChainPreset p;                                      // version 1 by default
    p.chain = {"eq", "plate", "fet76"};                 // insertion order
    p.params["eq"]["freq_hz"]      = 3.141592653589793;
    p.params["eq"]["boost_db"]     = 1e-9;
    p.params["plate"]["decay_s"]   = -45.5;
    p.params["plate"]["damp_pct"]  = 1e21;
    p.params["fet76"]["thresh_db"] = 0.0;
    p.params["fet76"]["negzero"]   = -0.0;              // the sign must survive
    p.params["fet76"]["subnormal"] = 5e-324;            // minimum denormal
    std::string err;
    check(save_preset(path, p, err), "guardado del preset");
    ChainPreset back;
    check(load_preset(path, back, err), "carga del preset");
    check(back.version == p.version, "version == 1");
    check(back.chain == p.chain && back.chain.size() == 3,
          "chain identica y en orden (eq, plate, fet76)");
    check(preset_params_bits_equal(p, back),
          "params bit-exactos tras el roundtrip (bits de doubles)");
    check(preset_bits_equal(back.params["eq"]["freq_hz"], 3.141592653589793),
          "3.141592653589793 bit-exacto");
    check(back.params["eq"]["boost_db"] == 1e-9, "1e-9 bit-exacto");
    check(back.params["plate"]["decay_s"] == -45.5, "-45.5 bit-exacto");
    check(back.params["plate"]["damp_pct"] == 1e21, "1e21 bit-exacto");
    check(back.params["fet76"]["thresh_db"] == 0.0, "0.0 exacto");
    check(preset_bits_equal(back.params["fet76"]["negzero"], -0.0),
          "-0.0 conserva el signo (bits)");
    check(back.params["fet76"]["subnormal"] == 5e-324,
          "5e-324 (denormal) bit-exacto");

    // The header banner example loads as-is (documented format).
    const std::string ejemplo =
        "{\n"
        "  \"version\": 1,\n"
        "  \"chain\": [\"eq\", \"plate\"],\n"
        "  \"params\": {\n"
        "    \"eq\":    { \"freq_hz\": 150, \"boost_db\": 3.5 },\n"
        "    \"plate\": { \"decay_s\": 3.2, \"damp_pct\": 45 }\n"
        "  }\n"
        "}\n";
    check(preset_write_text(path, ejemplo), "escritura del ejemplo del banner");
    ChainPreset ej;
    check(load_preset(path, ej, err), "carga del ejemplo del banner");
    check(ej.chain.size() == 2 && ej.chain[0] == "eq" && ej.chain[1] == "plate",
          "ejemplo: chain eq -> plate");
    check(ej.params.at("eq").at("freq_hz") == 150.0 &&
              ej.params.at("eq").at("boost_db") == 3.5 &&
              ej.params.at("plate").at("decay_s") == 3.2 &&
              ej.params.at("plate").at("damp_pct") == 45.0,
          "ejemplo: los cuatro parámetros exactos");
}

// b) Broken JSON (10 variants): always false and err with "línea N".
void test_preset_malformed() {
    std::cout << "[test] preset: JSON roto -> false y err con 'línea N'\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset_bad.json").string();
    const PresetBrokenCase casos[] = {
        {"",                                              "archivo vacío"},
        {"{\"version\": 1, \"chain\": [\"eq\"",           "llaves sin cerrar"},
        {"{\n  \"version\": 1,\n}",                       "coma colgante"},
        {"{\"version\": 1.2.3}",                          "número 1.2.3"},
        {"{\"version\": tru}",                            "token inesperado"},
        {"{\"version\" 1}",                               "dos puntos ausentes"},
        {"{\"version\": 1, \"chain\": [\"eq",             "string sin cerrar"},
        {"{\"params\": {\"eq\": {\"x\": NaN}}}",          "literal NaN (no existe en JSON)"},
        {"{\"chain\": [\"eq\",]}",                         "coma colgante en array"},
        {"{\"version\": 1} basura",                       "datos sobrantes tras la raíz"},
    };
    for (const PresetBrokenCase& c : casos) {
        check(preset_write_text(path, c.text),
              std::string("escritura del caso roto: ") + c.label);
        ChainPreset out;
        std::string err;
        const bool ok = load_preset(path, out, err);
        check(!ok, std::string("JSON roto rechazado: ") + c.label);
        check(ok || err.find("línea") != std::string::npos,
              std::string("err menciona 'línea' (") + c.label + "): " + err);
    }
}

// c) Unknown fields ("author", "tags", "meta") load without error
//    and are ignored (documented forward-compat).
void test_preset_unknown_fields() {
    std::cout << "[test] preset: campos desconocidos se ignoran (forward-compat)\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset_extra.json").string();
    const std::string json =
        "{\n"
        "  \"author\": \"alguien\",\n"
        "  \"version\": 1,\n"
        "  \"tags\": [\"vocal\", 7, null],\n"
        "  \"chain\": [\"eq\"],\n"
        "  \"params\": {\"eq\": {\"boost_db\": 3.5}},\n"
        "  \"meta\": {\"deep\": {\"x\": [1, {\"y\": true}]}, \"flag\": false, \"nada\": null}\n"
        "}\n";
    check(preset_write_text(path, json), "escritura del preset con extras");
    ChainPreset out;
    std::string err;
    check(load_preset(path, out, err), "carga con campos desconocidos");
    check(out.version == 1, "version leída == 1");
    check(out.chain.size() == 1 && out.chain[0] == "eq", "chain leída intacta");
    check(out.params.size() == 1 && out.params.count("eq") == 1 &&
              out.params.at("eq").size() == 1 &&
              out.params.at("eq").at("boost_db") == 3.5,
          "params leídos intactos (extras ignorados)");
}

// d) Wrong types: chain with a number, parameter with a string, etc.
//    Each case => false and err mentioning the culprit element and 'línea'.
void test_preset_bad_types() {
    std::cout << "[test] preset: tipos incorrectos -> false y err claro\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset_types.json").string();
    struct BadTypeCase {
        const char* text;
        const char* label;
        const char* must_mention;
    };
    const BadTypeCase casos[] = {
        {"{\"version\": 1, \"chain\": [\"eq\", 42]}",
         "chain con número", "chain"},
        {"{\"version\": 1, \"chain\": [null]}",
         "elemento null en chain", "chain"},
        {"{\"version\": 1, \"params\": {\"eq\": {\"drive\": \"mucho\"}}}",
         "parámetro con string", "drive"},
        {"{\"version\": 1, \"params\": {\"eq\": 5}}",
         "fx que no es objeto", "eq"},
        {"{\"version\": \"uno\"}",
         "version con string", "version"},
    };
    for (const BadTypeCase& c : casos) {
        check(preset_write_text(path, c.text),
              std::string("escritura del caso de tipos: ") + c.label);
        ChainPreset out;
        std::string err;
        const bool ok = load_preset(path, out, err);
        check(!ok, std::string("tipo incorrecto rechazado: ") + c.label);
        check(ok || (err.find(c.must_mention) != std::string::npos &&
                     err.find("línea") != std::string::npos),
              std::string("err menciona '") + c.must_mention + "' y 'línea' (" +
              c.label + "): " + err);
    }
    // NaN on save: JSON cannot represent NaN/Inf => false + actionable err.
    ChainPreset p;
    p.params["eq"]["drive"] = std::numeric_limits<double>::quiet_NaN();
    std::string err;
    const bool guardado = save_preset(path, p, err);   // separate: err readable in the check
    check(!guardado && err.find("no es finito") != std::string::npos,
          "NaN al guardar => rechazado con err (err: " + err + ")");
}

// e) Empty chain and params: full roundtrip and correct emitted JSON.
void test_preset_empty() {
    std::cout << "[test] preset: cadena y params vacíos -> roundtrip OK\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset_empty.json").string();
    ChainPreset p;                                      // version 1, all empty
    std::string err;
    check(save_preset(path, p, err), "guardado del preset vacío");
    ChainPreset back;
    check(load_preset(path, back, err), "carga del preset vacío");
    check(back.version == 1 && back.chain.empty() && back.params.empty(),
          "roundtrip vacío: version 1, chain [], params {}");
    const std::string text = preset_read_text(path);
    check(text.find("\"chain\": []") != std::string::npos &&
              text.find("\"params\": {}") != std::string::npos,
          "el JSON emitido usa 'chain': [] y 'params': {}");
}

// Extra (disk errors): impossible paths => false + actionable err.
void test_preset_disk_errors() {
    std::cout << "[test] preset: errores de disco accionables\n";
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path() /
                             "deluge_sim_preset_dir_inexistente").string();
    std::error_code ec;
    fs::remove_all(dir, ec);                            // ensures it does not exist
    const std::string missing = dir + "/preset.json";
    ChainPreset p;
    std::string err;
    const bool saved = save_preset(missing, p, err);   // separate: err readable in the check
    check(!saved && err.find("no se pudo crear") != std::string::npos,
          "save en carpeta inexistente => err accionable (err: " + err + ")");
    const bool loaded = load_preset(missing, p, err);  // separate: err readable in the check
    check(!loaded && err.find("no se pudo abrir") != std::string::npos,
          "load de archivo inexistente => err accionable (err: " + err + ")");
    fs::remove_all(dir, ec);
}

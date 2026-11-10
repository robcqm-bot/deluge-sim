// =====================================================================
// deluge-sim / test_preset.inc — Pruebas de import/export de presets
// CONTRATO: se incluye VERBATIM dentro del namespace anónimo de
// tests/test_all.cpp, después de check() y make_sine(...). Sin #include,
// sin main() y sin check() propio: solo test_preset_xxx() y helpers
// file-static. Única cabecera que el integrador debe añadir a
// test_all.cpp:   #include "deluge_sim/preset.hpp"
// =====================================================================

// ------------------------------ helpers ------------------------------

// Igualdad por BITS de doubles (los valores de estos tests son finitos:
// NaN no llega porque save_preset lo rechaza). ±0 se distingue con signbit.
static bool preset_bits_equal(double a, double b) {
    if (a != b) return false;                           // NaN cae aquí también
    if (a == 0.0) return std::signbit(a) == std::signbit(b);
    return true;
}

// Comparación profunda de 'params' con semántica de bits exacta.
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

// Escribe texto plano (para casos JSON rotos escritos a mano).
static bool preset_write_text(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return bool(f);
}

// Lee un archivo completo a string (sin istreambuf_iterator para no
// depender de <iterator>, que no está en la lista del contrato).
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

// Caso de JSON roto: texto y etiqueta para los mensajes.
struct PresetBrokenCase {
    const char* text;
    const char* label;
};

// ------------------------------ pruebas ------------------------------

// a) Roundtrip bit-exacto: valores "feos" (pi, 1e-9, -45.5, 1e21, 0.0,
//    más -0.0 y la denormal mínima) sobreviven guardar+cargar idénticos.
void test_preset_roundtrip() {
    std::cout << "[test] preset: roundtrip JSON bit-exacto con valores feos\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset.json").string();
    ChainPreset p;                                      // version 1 por defecto
    p.chain = {"eq", "plate", "fet76"};                 // orden de inserción
    p.params["eq"]["freq_hz"]      = 3.141592653589793;
    p.params["eq"]["boost_db"]     = 1e-9;
    p.params["plate"]["decay_s"]   = -45.5;
    p.params["plate"]["damp_pct"]  = 1e21;
    p.params["fet76"]["thresh_db"] = 0.0;
    p.params["fet76"]["negzero"]   = -0.0;              // el signo debe sobrevivir
    p.params["fet76"]["subnormal"] = 5e-324;            // denormal mínima
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

    // El ejemplo del banner del header carga tal cual (formato documentado).
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

// b) JSON roto (10 variantes): siempre false y err con "línea N".
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

// c) Campos desconocidos ("author", "tags", "meta") se cargan sin error
//    y se ignoran (forward-compat documentado).
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

// d) Tipos incorrectos: chain con número, parámetro con string, etc.
//    Cada caso => false y err que menciona el elemento culpable y 'línea'.
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
    // NaN al guardar: el JSON no representa NaN/Inf => false + err accionable.
    ChainPreset p;
    p.params["eq"]["drive"] = std::numeric_limits<double>::quiet_NaN();
    std::string err;
    const bool guardado = save_preset(path, p, err);   // aparte: err legible en el check
    check(!guardado && err.find("no es finito") != std::string::npos,
          "NaN al guardar => rechazado con err (err: " + err + ")");
}

// e) Cadena y params vacíos: roundtrip completo y JSON emitido correcto.
void test_preset_empty() {
    std::cout << "[test] preset: cadena y params vacíos -> roundtrip OK\n";
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() /
                              "deluge_sim_test_preset_empty.json").string();
    ChainPreset p;                                      // version 1, todo vacío
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

// Extra (errores de disco): rutas imposibles => false + err accionable.
void test_preset_disk_errors() {
    std::cout << "[test] preset: errores de disco accionables\n";
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path() /
                             "deluge_sim_preset_dir_inexistente").string();
    std::error_code ec;
    fs::remove_all(dir, ec);                            // asegura inexistencia
    const std::string missing = dir + "/preset.json";
    ChainPreset p;
    std::string err;
    const bool saved = save_preset(missing, p, err);   // aparte: err legible en el check
    check(!saved && err.find("no se pudo crear") != std::string::npos,
          "save en carpeta inexistente => err accionable (err: " + err + ")");
    const bool loaded = load_preset(missing, p, err);  // aparte: err legible en el check
    check(!loaded && err.find("no se pudo abrir") != std::string::npos,
          "load de archivo inexistente => err accionable (err: " + err + ")");
    fs::remove_all(dir, ec);
}

// =====================================================================
// deluge-sim / preset.cpp — Parser/escritor JSON mínimo para presets
// Descendente recursivo, sin dependencias externas y sin excepciones
// hacia fuera: toda la comunicación de errores es bool + err.
// =====================================================================
#include "deluge_sim/preset.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace deluge_sim {
namespace {

// ------------------------------ escritura ------------------------------

// Escapa un string para JSON: comillas, barra invertida y caracteres de
// control básicos. Los bytes UTF-8 (>= 0x80) pasan intactos.
void json_escape(const std::string& s, std::string& out) {
    for (char ch : s) {
        const unsigned char c = (unsigned char)ch;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
}

// %.17g garantiza el roundtrip bit-exacto de cualquier double finito.
std::string fmt_double(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    std::string s(buf);
    // Guardia de locale: si el entorno activó un locale con coma decimal,
    // el separador debe seguir siendo '.' para que el JSON sea válido.
    for (char& c : s) if (c == ',') c = '.';
    return s;
}

// Serializa el preset con sangría de 2 espacios (arrays de 'chain' en
// línea, objetos de 'params' expandidos).
void build_json(const ChainPreset& p, std::string& out) {
    out.clear();
    out += "{\n";
    out += "  \"version\": " + std::to_string(p.version) + ",\n";

    out += "  \"chain\": [";
    for (size_t i = 0; i < p.chain.size(); ++i) {
        if (i != 0) out += ", ";
        out += '"';
        json_escape(p.chain[i], out);
        out += '"';
    }
    out += "],\n";

    out += "  \"params\": {";
    if (p.params.empty()) {
        out += "}\n";
    } else {
        out += "\n";
        size_t fx_vistos = 0;
        for (const auto& fx : p.params) {
            out += "    \"";
            json_escape(fx.first, out);
            out += "\": {";
            if (fx.second.empty()) {
                out += "}";
            } else {
                out += "\n";
                size_t par_vistos = 0;
                for (const auto& kv : fx.second) {
                    out += "      \"";
                    json_escape(kv.first, out);
                    out += "\": ";
                    out += fmt_double(kv.second);
                    out += (++par_vistos < fx.second.size()) ? ",\n" : "\n";
                }
                out += "    }";
            }
            out += (++fx_vistos < p.params.size()) ? ",\n" : "\n";
        }
        out += "  }\n";
    }
    out += "}\n";
}

// ------------------------------ parser ------------------------------

// Cota de anidado para que un archivo hostil no agote la pila.
constexpr int kMaxDepth = 200;

// Cursor de lectura: posición, línea actual y primer error registrado.
struct JsonCursor {
    const std::string& text;
    size_t pos = 0;
    int line = 1;
    std::string err;

    explicit JsonCursor(const std::string& t) : text(t) {}

    // Registra el primer error con número de línea accionable.
    bool fail(const std::string& msg) {
        if (err.empty()) err = "línea " + std::to_string(line) + ": " + msg;
        return false;
    }
    bool eof() const { return pos >= text.size(); }
    char peek() const { return eof() ? '\0' : text[pos]; }
    // Avanza un carácter contando saltos de línea.
    void bump() {
        if (text[pos] == '\n') ++line;
        ++pos;
    }
    // Whitespace JSON: espacio, tabulador, CR y LF.
    void skip_ws() {
        while (!eof()) {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') bump();
            else break;
        }
    }
    // Describe el token de la posición actual, para errores accionables.
    std::string describe_at() const {
        if (eof()) return "fin del archivo";
        const char c = peek();
        switch (c) {
            case '{': return "objeto";
            case '[': return "array";
            case '"': return "string";
            case 't': case 'f': return "bool";
            case 'n': return "null";
            default: break;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return "número";
        if ((unsigned char)c < 0x20) return "carácter de control";
        return std::string("'") + c + "'";
    }
    bool expect(char c) {
        skip_ws();
        if (peek() == c) { bump(); return true; }
        return fail(std::string("se esperaba '") + c + "' (encontrado " +
                    describe_at() + ")");
    }
    // Consume un literal exacto ('true', 'false', 'null') ya despachado.
    bool parse_literal(const char* lit) {
        for (const char* p = lit; *p != '\0'; ++p) {
            if (eof() || text[pos] != *p)
                return fail(std::string("literal inválido (se esperaba '") +
                            lit + "')");
            bump();
        }
        return true;
    }
    // true/false/null donde se espera un campo: aceptados e ignorados.
    bool skip_literal_any() {
        const char c = peek();
        if (c == 't') return parse_literal("true");
        if (c == 'f') return parse_literal("false");
        return parse_literal("null");
    }
    // Cadena JSON con escapes (incluido \uXXXX con pares surrogate).
    bool parse_string(std::string& out, const char* context) {
        skip_ws();
        if (peek() != '"')
            return fail(std::string("se esperaba '\"' (") + context +
                        "; encontrado " + describe_at() + ")");
        bump();
        out.clear();
        for (;;) {
            if (eof()) return fail("cadena sin cerrar");
            const char c = text[pos];
            if (c == '"') { bump(); return true; }
            if ((unsigned char)c < 0x20) {
                if (c == '\n')
                    return fail("cadena sin cerrar (salto de línea sin escape)");
                return fail("carácter de control sin escapar dentro de cadena");
            }
            if (c != '\\') { out += c; bump(); continue; }
            bump();                                     // consume '\'
            if (eof()) return fail("cadena sin cerrar (escape al final)");
            const char e = text[pos];
            switch (e) {
                case '"':  out += '"';  bump(); break;
                case '\\': out += '\\'; bump(); break;
                case '/':  out += '/';  bump(); break;
                case 'b':  out += '\b'; bump(); break;
                case 'f':  out += '\f'; bump(); break;
                case 'n':  out += '\n'; bump(); break;
                case 'r':  out += '\r'; bump(); break;
                case 't':  out += '\t'; bump(); break;
                case 'u':
                    bump();
                    if (!parse_unicode_escape(out)) return false;
                    break;
                default:
                    return fail(std::string("escape inválido '\\") + e +
                                "' dentro de cadena");
            }
        }
    }
    // Lee 4 dígitos hexadecimales de un escape \u.
    bool hex4(unsigned& cp) {
        if (pos + 4 > text.size())
            return fail("escape \\u incompleto (se esperaban 4 dígitos hexadecimales)");
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text[pos + size_t(i)];
            unsigned d;
            if (h >= '0' && h <= '9') d = unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') d = unsigned(h - 'a') + 10u;
            else if (h >= 'A' && h <= 'F') d = unsigned(h - 'A') + 10u;
            else return fail(std::string("escape \\u inválido (se esperaba un "
                                         "dígito hexadecimal, encontrado '") + h + "')");
            cp = cp * 16u + d;
        }
        pos += 4;                                       // dígitos hex: nunca '\n'
        return true;
    }
    bool parse_unicode_escape(std::string& out) {
        unsigned cp;
        if (!hex4(cp)) return false;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {           // surrogate alto: pareja
            if (pos + 2 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
                pos += 2;                               // '\\u': nunca '\n'
                unsigned lo;
                if (!hex4(lo)) return false;
                if (lo < 0xDC00u || lo > 0xDFFFu)
                    return fail("surrogate bajo inválido tras un surrogate alto");
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            } else {
                return fail("surrogate alto sin su pareja \\uDC00-\\uDFFF");
            }
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            return fail("surrogate bajo aislado");
        }
        // Codifica el punto de código como UTF-8.
        if (cp < 0x80u) {
            out += char(cp);
        } else if (cp < 0x800u) {
            out += char(0xC0u | (cp >> 6));
            out += char(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            out += char(0xE0u | (cp >> 12));
            out += char(0x80u | ((cp >> 6) & 0x3Fu));
            out += char(0x80u | (cp & 0x3Fu));
        } else {
            out += char(0xF0u | (cp >> 18));
            out += char(0x80u | ((cp >> 12) & 0x3Fu));
            out += char(0x80u | ((cp >> 6) & 0x3Fu));
            out += char(0x80u | (cp & 0x3Fu));
        }
        return true;
    }
    // Número JSON estricto: [signo] int [frac] [exp]. Sin NaN/Inf ni
    // ceros a la izquierda; la conversión es strtod del token validado.
    bool parse_number(double& out) {
        skip_ws();
        const size_t inicio = pos;
        while (!eof()) {
            const char c = text[pos];
            if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ||
                (c >= '0' && c <= '9')) bump();
            else break;
        }
        const std::string tok = text.substr(inicio, pos - inicio);
        if (tok.empty())
            return fail("se esperaba un número (encontrado " + describe_at() + ")");
        size_t i = 0;
        if (tok[i] == '-') ++i;
        if (i >= tok.size() || tok[i] < '0' || tok[i] > '9')
            return fail("número inválido ('" + tok + "')");
        if (tok[i] == '0') {                            // '0' no seguido de dígitos
            ++i;
            if (i < tok.size() && tok[i] >= '0' && tok[i] <= '9')
                return fail("número inválido (cero a la izquierda: '" + tok + "')");
        } else {
            while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') ++i;
        }
        if (i < tok.size() && tok[i] == '.') {
            ++i;
            if (i >= tok.size() || tok[i] < '0' || tok[i] > '9')
                return fail("número inválido ('" + tok +
                            "': sin dígitos tras el punto)");
            while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') ++i;
        }
        if (i < tok.size() && (tok[i] == 'e' || tok[i] == 'E')) {
            ++i;
            if (i < tok.size() && (tok[i] == '+' || tok[i] == '-')) ++i;
            if (i >= tok.size() || tok[i] < '0' || tok[i] > '9')
                return fail("número inválido ('" + tok +
                            "': sin dígitos en el exponente)");
            while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') ++i;
        }
        if (i != tok.size())
            return fail("número inválido ('" + tok + "')");
        const double v = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(v))
            return fail("número fuera de rango ('" + tok + "')");
        out = v;
        return true;
    }
    // Salta cualquier valor JSON (claves desconocidas: forward-compat).
    bool skip_value(int depth) {
        if (depth > kMaxDepth)
            return fail("JSON anidado demasiado profundo (>" +
                        std::to_string(kMaxDepth) + " niveles)");
        skip_ws();
        if (eof()) return fail("se esperaba un valor (fin del archivo)");
        const char c = peek();
        if (c == '{') {
            bump(); skip_ws();
            if (peek() == '}') { bump(); return true; }
            for (;;) {
                std::string key;
                if (!parse_string(key, "clave de objeto")) return false;
                if (!expect(':')) return false;
                if (!skip_value(depth + 1)) return false;
                skip_ws();
                if (peek() == ',') {
                    bump(); skip_ws();
                    if (peek() == '}') return fail("coma colgante antes de '}'");
                    continue;
                }
                if (peek() == '}') { bump(); return true; }
                return fail("se esperaba ',' o '}' (encontrado " +
                            describe_at() + ")");
            }
        }
        if (c == '[') {
            bump(); skip_ws();
            if (peek() == ']') { bump(); return true; }
            for (;;) {
                if (!skip_value(depth + 1)) return false;
                skip_ws();
                if (peek() == ',') {
                    bump(); skip_ws();
                    if (peek() == ']') return fail("coma colgante antes de ']'");
                    continue;
                }
                if (peek() == ']') { bump(); return true; }
                return fail("se esperaba ',' o ']' (encontrado " +
                            describe_at() + ")");
            }
        }
        if (c == '"') { std::string tmp; return parse_string(tmp, "valor"); }
        if (c == 't') return parse_literal("true");
        if (c == 'f') return parse_literal("false");
        if (c == 'n') return parse_literal("null");
        if (c == '-' || (c >= '0' && c <= '9')) { double d; return parse_number(d); }
        return fail("valor inesperado (encontrado " + describe_at() + ")");
    }
};

// "version": número entero; true/false/null => ignorado (queda el 1).
bool parse_version_field(JsonCursor& j, int& version) {
    j.skip_ws();
    const char c = j.peek();
    if (c == 't' || c == 'f' || c == 'n') return j.skip_literal_any();
    if (c == '"' || c == '{' || c == '[')
        return j.fail("'version' debe ser un número (encontrado " +
                      j.describe_at() + ")");
    double v;
    if (!j.parse_number(v)) return false;
    if (v != std::floor(v) || v < -2147483648.0 || v > 2147483647.0)
        return j.fail("'version' debe ser un entero (encontrado " +
                      fmt_double(v) + ")");
    version = int(v);
    return true;
}

// "chain": array de strings; true/false/null => ignorado. Los elementos
// son ESTRICTOS: cualquier cosa que no sea string => error claro.
bool parse_chain_field(JsonCursor& j, std::vector<std::string>& chain) {
    j.skip_ws();
    const char c = j.peek();
    if (c == 't' || c == 'f' || c == 'n') return j.skip_literal_any();
    if (c != '[')
        return j.fail("'chain' debe ser un array de strings (encontrado " +
                      j.describe_at() + ")");
    j.bump();
    j.skip_ws();
    if (j.peek() == ']') { j.bump(); return true; }
    size_t idx = 0;
    for (;;) {
        j.skip_ws();
        if (j.peek() != '"')
            return j.fail("'chain': el elemento " + std::to_string(idx) +
                          " debe ser string (encontrado " + j.describe_at() + ")");
        std::string fx;
        if (!j.parse_string(fx, "elemento de 'chain'")) return false;
        chain.push_back(fx);
        ++idx;
        j.skip_ws();
        if (j.peek() == ',') {
            j.bump(); j.skip_ws();
            if (j.peek() == ']')
                return j.fail("coma colgante antes de ']' en 'chain'");
            continue;
        }
        if (j.peek() == ']') { j.bump(); return true; }
        return j.fail("'chain': se esperaba ',' o ']' (encontrado " +
                      j.describe_at() + ")");
    }
}

// "params": { fx: { parámetro: número } }. true/false/null como valor
// del campo => ignorado. Los valores hoja son ESTRICTOS: solo números.
bool parse_params_field(JsonCursor& j,
                        std::map<std::string, std::map<std::string, double>>& params) {
    j.skip_ws();
    const char c = j.peek();
    if (c == 't' || c == 'f' || c == 'n') return j.skip_literal_any();
    if (c != '{')
        return j.fail("'params' debe ser un objeto {fx: {param: valor}} "
                      "(encontrado " + j.describe_at() + ")");
    j.bump();
    j.skip_ws();
    if (j.peek() == '}') { j.bump(); return true; }
    for (;;) {
        std::string fx;
        if (!j.parse_string(fx, "nombre del fx en 'params'")) return false;
        j.skip_ws();
        if (j.peek() != ':')
            return j.fail("'params': se esperaba ':' tras el fx '" + fx +
                          "' (encontrado " + j.describe_at() + ")");
        j.bump();
        j.skip_ws();
        if (j.peek() != '{')
            return j.fail("'params': el fx '" + fx +
                          "' debe ser un objeto de parámetros (encontrado " +
                          j.describe_at() + ")");
        j.bump();
        j.skip_ws();
        if (j.peek() == '}') {
            j.bump();
            params[fx] = {};                            // fx sin parámetros
        } else {
            for (;;) {
                std::string key;
                if (!j.parse_string(key, "nombre de parámetro")) return false;
                j.skip_ws();
                if (j.peek() != ':')
                    return j.fail("'params': se esperaba ':' tras el parámetro '" +
                                  key + "' de '" + fx + "' (encontrado " +
                                  j.describe_at() + ")");
                j.bump();
                j.skip_ws();
                const char v = j.peek();
                if (v != '-' && (v < '0' || v > '9'))
                    return j.fail("el parámetro '" + key + "' de '" + fx +
                                  "' debe ser un número (encontrado " +
                                  j.describe_at() + ")");
                double d;
                if (!j.parse_number(d)) return false;
                params[fx][key] = d;
                j.skip_ws();
                if (j.peek() == ',') {
                    j.bump(); j.skip_ws();
                    if (j.peek() == '}')
                        return j.fail("coma colgante antes de '}' en los "
                                      "parámetros de '" + fx + "'");
                    continue;
                }
                if (j.peek() == '}') { j.bump(); break; }
                return j.fail("'params': se esperaba ',' o '}' en '" + fx +
                              "' (encontrado " + j.describe_at() + ")");
            }
        }
        // Siguiente entrada de fx.
        j.skip_ws();
        if (j.peek() == ',') {
            j.bump(); j.skip_ws();
            if (j.peek() == '}')
                return j.fail("coma colgante antes de '}' en 'params'");
            continue;
        }
        if (j.peek() == '}') { j.bump(); return true; }
        return j.fail("'params': se esperaba ',' o '}' (encontrado " +
                      j.describe_at() + ")");
    }
}

// Raíz: objeto con "version", "chain", "params" y claves desconocidas,
// que se saltan silenciosamente (forward-compat documentado).
bool parse_root(JsonCursor& j, ChainPreset& out) {
    // BOM UTF-8 opcional (los editores de Windows lo añaden): se salta.
    if (j.text.size() >= 3 && (unsigned char)j.text[0] == 0xEF &&
        (unsigned char)j.text[1] == 0xBB && (unsigned char)j.text[2] == 0xBF) {
        j.pos = 3;
    }
    j.skip_ws();
    if (j.eof()) return j.fail("archivo vacío (se esperaba '{' con el preset)");
    if (j.peek() != '{')
        return j.fail("se esperaba '{' al inicio del preset (encontrado " +
                      j.describe_at() + ")");
    j.bump();
    j.skip_ws();
    if (j.peek() == '}') {
        j.bump();                                       // preset vacío {}
    } else {
        for (;;) {
            std::string key;
            if (!j.parse_string(key, "clave del objeto raíz")) return false;
            if (!j.expect(':')) return false;
            if (key == "version") {
                if (!parse_version_field(j, out.version)) return false;
            } else if (key == "chain") {
                if (!parse_chain_field(j, out.chain)) return false;
            } else if (key == "params") {
                if (!parse_params_field(j, out.params)) return false;
            } else {
                if (!j.skip_value(1)) return false;     // desconocida: se salta
            }
            j.skip_ws();
            if (j.peek() == ',') {
                j.bump(); j.skip_ws();
                if (j.peek() == '}')
                    return j.fail("coma colgante antes de '}'");
                continue;
            }
            if (j.peek() == '}') { j.bump(); break; }
            return j.fail("se esperaba ',' o '}' tras '" + key +
                          "' (encontrado " + j.describe_at() + ")");
        }
    }
    j.skip_ws();
    if (!j.eof())
        return j.fail("datos sobrantes tras el objeto raíz (encontrado " +
                      j.describe_at() + ")");
    return true;
}

} // namespace

bool save_preset(const std::string& path, const ChainPreset& p, std::string& err) {
    try {
        // El JSON no representa NaN/Inf: se rechazan antes de escribir nada.
        for (const auto& fx : p.params) {
            for (const auto& kv : fx.second) {
                if (!std::isfinite(kv.second)) {
                    err = "no se puede guardar: el parámetro '" + kv.first +
                          "' de '" + fx.first + "' no es finito (NaN/Inf)";
                    return false;
                }
            }
        }
        std::string json;
        build_json(p, json);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { err = "no se pudo crear: " + path; return false; }
        f.write(json.data(), std::streamsize(json.size()));
        if (!f) { err = "escritura fallida (¿disco lleno?): " + path; return false; }
        return true;
    } catch (const std::exception& e) {
        err = std::string("error interno guardando el preset: ") + e.what();
        return false;
    } catch (...) {
        err = "error interno desconocido guardando el preset";
        return false;
    }
}

bool load_preset(const std::string& path, ChainPreset& out, std::string& err) {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) { err = "no se pudo abrir: " + path; return false; }
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        if (f.bad()) { err = "error de lectura: " + path; return false; }
        ChainPreset tmp;                    // 'out' solo cambia si todo va bien
        JsonCursor j(text);
        if (!parse_root(j, tmp)) { err = j.err; return false; }
        out = std::move(tmp);
        return true;
    } catch (const std::exception& e) {
        err = std::string("error interno leyendo el preset: ") + e.what();
        return false;
    } catch (...) {
        err = "error interno desconocido leyendo el preset";
        return false;
    }
}

} // namespace deluge_sim

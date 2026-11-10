// =====================================================================
// deluge-sim / preset.cpp — Minimal JSON parser/writer for presets
// Recursive descent, no external dependencies, no exceptions thrown
// outward: all error communication is bool + err.
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

// ------------------------------- writing -------------------------------

// Escapes a string for JSON: quotes, backslash and basic control
// characters. UTF-8 bytes (>= 0x80) pass through intact.
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

// %.17g guarantees a bit-exact roundtrip of any finite double.
std::string fmt_double(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    std::string s(buf);
    // Locale guard: if the environment enabled a locale with a decimal
    // comma, the separator must remain '.' so the JSON stays valid.
    for (char& c : s) if (c == ',') c = '.';
    return s;
}

// Serializes the preset with 2-space indentation ('chain' arrays on
// one line, 'params' objects expanded).
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

// Nesting cap so a hostile file cannot exhaust the stack.
constexpr int kMaxDepth = 200;

// Read cursor: position, current line and first recorded error.
struct JsonCursor {
    const std::string& text;
    size_t pos = 0;
    int line = 1;
    std::string err;

    explicit JsonCursor(const std::string& t) : text(t) {}

    // Records the first error with an actionable line number.
    bool fail(const std::string& msg) {
        if (err.empty()) err = "línea " + std::to_string(line) + ": " + msg;
        return false;
    }
    bool eof() const { return pos >= text.size(); }
    char peek() const { return eof() ? '\0' : text[pos]; }
    // Advances one character, counting newlines.
    void bump() {
        if (text[pos] == '\n') ++line;
        ++pos;
    }
    // JSON whitespace: space, tab, CR and LF.
    void skip_ws() {
        while (!eof()) {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') bump();
            else break;
        }
    }
    // Describes the token at the current position, for actionable errors.
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
    // Consumes an exact literal ('true', 'false', 'null') already dispatched.
    bool parse_literal(const char* lit) {
        for (const char* p = lit; *p != '\0'; ++p) {
            if (eof() || text[pos] != *p)
                return fail(std::string("literal inválido (se esperaba '") +
                            lit + "')");
            bump();
        }
        return true;
    }
    // true/false/null where a field is expected: accepted and ignored.
    bool skip_literal_any() {
        const char c = peek();
        if (c == 't') return parse_literal("true");
        if (c == 'f') return parse_literal("false");
        return parse_literal("null");
    }
    // JSON string with escapes (including \uXXXX with surrogate pairs).
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
            bump();                                     // consumes '\'
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
    // Reads 4 hexadecimal digits of a \u escape.
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
        pos += 4;                                       // hex digits: never '\n'
        return true;
    }
    bool parse_unicode_escape(std::string& out) {
        unsigned cp;
        if (!hex4(cp)) return false;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {           // high surrogate: pair
            if (pos + 2 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
                pos += 2;                               // '\\u': never '\n'
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
        // Encodes the code point as UTF-8.
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
    // Strict JSON number: [sign] int [frac] [exp]. No NaN/Inf and no
    // leading zeros; conversion is strtod on the validated token.
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
        if (tok[i] == '0') {                            // '0' not followed by digits
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
    // Skips any JSON value (unknown keys: forward-compat).
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

// "version": integer number; true/false/null => ignored (stays 1).
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

// "chain": array of strings; true/false/null => ignored. Elements
// are STRICT: anything that is not a string => clear error.
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

// "params": { fx: { parameter: number } }. true/false/null as the
// field's value => ignored. Leaf values are STRICT: numbers only.
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
            params[fx] = {};                            // fx with no parameters
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
        // Next fx entry.
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

// Root: object with "version", "chain", "params" and unknown keys,
// which are silently skipped (documented forward-compat).
bool parse_root(JsonCursor& j, ChainPreset& out) {
    // Optional UTF-8 BOM (added by Windows editors): skipped.
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
        j.bump();                                       // empty preset {}
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
                if (!j.skip_value(1)) return false;     // unknown: skipped
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
        // JSON cannot represent NaN/Inf: they are rejected before writing anything.
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
        ChainPreset tmp;                    // 'out' only changes if all goes well
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

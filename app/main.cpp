// =====================================================================
// deluge-sim / main.cpp — simcli: CLI del simulador (harness offline)
//
//   simcli render --in entrada.wav --out salida.wav [--chain a,b] [k=v ...]
//       --chain : fx en orden de inserción (default fet76); válidos:
//                 fet76, opto
//       k=v     : sin prefijo se aplica a CADA fx de la cadena que tenga
//                 ese parámetro; con prefijo "fx.k=v" solo a ese fx.
//       --block : marcos por bloque de render
//   simcli help
//
// Aquí viven LOS ADAPTADORES (el único puente delimitado entre
// deluge_sim y suite_core; las clases están en adapter_*.hpp). En la
// integración real al firmware se trasladan a src/deluge/dsp/suite/.
// =====================================================================
#include "adapter_eq.hpp"
#include "adapter_fet76.hpp"
#include "adapter_opto.hpp"
#include "adapter_plate.hpp"
#include "deluge_sim/engine.hpp"
#include "deluge_sim/wav_io.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace deluge_sim;

// Claves k=v conocidas por cada fx (sin prefijo). El prefijo "fx." las
// dirige a un solo eslabón de la cadena.
const std::map<std::string, std::vector<const char*>>& fx_keys() {
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
    };
    return m;
}

void print_usage() {
    std::cout <<
        "deluge-sim v0.5 (offline DSP harness + board simulator)\n"
        "Uso:\n"
        "  simcli render --in ENTRADA.wav --out SALIDA.wav [--chain fet76,opto,eq,plate]"
        " [k=v ...] [--block N]\n"
        "    --chain: fx en orden de inserción (default fet76)\n"
        "    k fet76: in_gain_db thresh_db ratio attack_s release_s out_gain_db\n"
        "    k opto:  thresh_db ratio knee_db attack_s release_fast_s"
        " release_slow_s gr_span_db out_gain_db\n"
        "    k eq:    freq_hz boost_db cut_db q_boost q_cut trim_db\n"
        "    k plate: decay_s damp_pct predelay_ms mod_rate_hz mod_depth_ms"
        " mix_pct out_gain_db\n"
        "    prefijo por fx: fet76.ratio=8 opto.knee_db=3\n"
        "  simcli help\n";
}

bool parse_double(const std::string& s, double& out) {
    try {
        size_t used = 0;
        double v = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// La clave es válida si: sin prefijo y la tiene algún fx de la cadena,
// o con prefijo "fx." y la tiene ese fx.
bool key_known(const std::string& k, const std::vector<std::string>& chain) {
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

// Valor de 'key' para el fx 'name': primero el prefijado "name.key",
// luego el sin prefijo (se aplica a todos), y si no existe el default.
double kv_val(const std::map<std::string, double>& kv, const std::string& name,
              const char* key, double def) {
    auto it = kv.find(name + "." + key);
    if (it != kv.end()) return it->second;
    it = kv.find(key);
    return (it != kv.end()) ? it->second : def;
}

int cmd_render(int argc, char** argv) {
    std::string in, out;
    double block = double(kDefaultMaxBlock);
    std::vector<std::string> chain{"fet76"};
    std::map<std::string, double> kv;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--in" && i + 1 < argc) { in = argv[++i]; }
        else if (a == "--out" && i + 1 < argc) { out = argv[++i]; }
        else if (a == "--block" && i + 1 < argc) {
            if (!parse_double(argv[++i], block) || block < 8 || block > 65536) {
                std::cerr << "ERROR: --block fuera de rango [8,65536]\n";
                return 2;
            }
        } else if (a == "--chain" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string item;
            chain.clear();
            while (std::getline(ss, item, ',')) {
                // recorta espacios del ítem
                const size_t b = item.find_first_not_of(" \t");
                const size_t e = item.find_last_not_of(" \t");
                if (b == std::string::npos) continue;
                item = item.substr(b, e - b + 1);
                if (fx_keys().count(item) == 0) {
                    std::cerr << "ERROR: fx desconocido en --chain: '" << item
                              << "' (válidos: fet76 opto eq plate)\n";
                    return 2;
                }
                chain.push_back(item);
            }
            if (chain.empty()) {
                std::cerr << "ERROR: --chain vacío\n";
                return 2;
            }
        } else if (a.find('=') != std::string::npos) {
            std::string k = a.substr(0, a.find('=')), v = a.substr(a.find('=') + 1);
            double d = 0.0;
            if (!parse_double(v, d)) {
                std::cerr << "ERROR: valor numérico inválido en '" << a << "'\n";
                return 2;
            }
            kv[k] = d;                         // la validez de la clave se
        } else {                               // comprueba tras conocer --chain
            std::cerr << "ERROR: argumento no reconocido: '" << a << "'\n";
            print_usage();
            return 2;
        }
    }
    if (in.empty() || out.empty()) {
        std::cerr << "ERROR: --in y --out son obligatorios\n";
        print_usage();
        return 2;
    }
    for (const auto& [k, v] : kv) {
        if (!key_known(k, chain)) {
            std::cerr << "ERROR: parámetro desconocido para la cadena actual: '"
                      << k << "'\nVálidos: ";
            bool first = true;
            for (const std::string& fx : chain)
                for (const char* vk : fx_keys().at(fx)) {
                    if (!first) std::cerr << ' ';
                    std::cerr << vk;
                    first = false;
                }
            std::cerr << " (o prefijados fx.k=v)\n";
            return 2;
        }
    }

    WavData wav;
    std::string err;
    if (!read_wav(in, wav, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 3;
    }
    if (wav.channels > kMaxChannels) {
        std::cerr << "ERROR: el sim acepta 1 o 2 canales (archivo tiene "
                  << wav.channels << ")\n";
        return 3;
    }

    // Parámetros por fx: defaults + k=v (sin prefijo = todos los fx que
    // tengan la clave; con prefijo = solo ese fx).
    suite_core::Fet76Ref::Params p76;
    p76.in_gain_db  = kv_val(kv, "fet76", "in_gain_db",  p76.in_gain_db);
    p76.thresh_db   = kv_val(kv, "fet76", "thresh_db",   p76.thresh_db);
    p76.ratio       = kv_val(kv, "fet76", "ratio",       p76.ratio);
    p76.attack_s    = kv_val(kv, "fet76", "attack_s",    p76.attack_s);
    p76.release_s   = kv_val(kv, "fet76", "release_s",   p76.release_s);
    p76.out_gain_db = kv_val(kv, "fet76", "out_gain_db", p76.out_gain_db);

    suite_core::OptoCompRef::Params pop;
    pop.thresh_db      = kv_val(kv, "opto", "thresh_db",      pop.thresh_db);
    pop.ratio          = kv_val(kv, "opto", "ratio",          pop.ratio);
    pop.knee_db        = kv_val(kv, "opto", "knee_db",        pop.knee_db);
    pop.attack_s       = kv_val(kv, "opto", "attack_s",       pop.attack_s);
    pop.release_fast_s = kv_val(kv, "opto", "release_fast_s", pop.release_fast_s);
    pop.release_slow_s = kv_val(kv, "opto", "release_slow_s", pop.release_slow_s);
    pop.gr_span_db     = kv_val(kv, "opto", "gr_span_db",     pop.gr_span_db);
    pop.out_gain_db    = kv_val(kv, "opto", "out_gain_db",    pop.out_gain_db);

    suite_core::PassiveEQRef::Params peq;
    peq.freq_hz  = kv_val(kv, "eq", "freq_hz",  peq.freq_hz);
    peq.boost_db = kv_val(kv, "eq", "boost_db", peq.boost_db);
    peq.cut_db   = kv_val(kv, "eq", "cut_db",   peq.cut_db);
    peq.q_boost  = kv_val(kv, "eq", "q_boost",  peq.q_boost);
    peq.q_cut    = kv_val(kv, "eq", "q_cut",    peq.q_cut);
    peq.trim_db  = kv_val(kv, "eq", "trim_db",  peq.trim_db);

    suite_core::PlateReverbRef::Params pplate;
    pplate.decay_s      = kv_val(kv, "plate", "decay_s",      pplate.decay_s);
    pplate.damp_pct     = kv_val(kv, "plate", "damp_pct",     pplate.damp_pct);
    pplate.predelay_ms  = kv_val(kv, "plate", "predelay_ms",  pplate.predelay_ms);
    pplate.mod_rate_hz  = kv_val(kv, "plate", "mod_rate_hz",  pplate.mod_rate_hz);
    pplate.mod_depth_ms = kv_val(kv, "plate", "mod_depth_ms", pplate.mod_depth_ms);
    pplate.mix_pct      = kv_val(kv, "plate", "mix_pct",      pplate.mix_pct);
    pplate.out_gain_db  = kv_val(kv, "plate", "out_gain_db",  pplate.out_gain_db);

    Engine engine(double(wav.sample_rate), size_t(block));
    for (const std::string& fx : chain) {
        if (fx == "fet76")      engine.add_processor(std::make_unique<AdapterFet76>(p76));
        else if (fx == "opto")  engine.add_processor(std::make_unique<AdapterOpto>(pop));
        else if (fx == "eq")    engine.add_processor(std::make_unique<AdapterPassiveEQ>(peq));
        else if (fx == "plate") engine.add_processor(std::make_unique<AdapterPlateReverb>(pplate));
    }

    FileAudioSource src(wav);
    WavAudioSink sink(wav.sample_rate, 2);
    RunReport report;
    if (!engine.run(src, sink, report, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 4;
    }
    if (!sink.finalize(out, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 4;
    }

    std::cout << "OK  " << report.frames_rendered << " marcos en "
              << report.blocks_rendered << " bloques | tiempo=" << report.wall_seconds
              << "s | x" << report.realtime_factor << " realtime | cadena=";
    for (size_t i = 0; i < chain.size(); ++i)
        std::cout << (i ? "," : "") << chain[i];
    std::cout << "\n";
    if (!report.warning.empty()) std::cout << "AVISO: " << report.warning << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 2; }
    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { print_usage(); return 0; }
    if (cmd == "render") return cmd_render(argc - 2, argv + 2);
    std::cerr << "ERROR: comando desconocido: '" << cmd << "'\n";
    print_usage();
    return 2;
}

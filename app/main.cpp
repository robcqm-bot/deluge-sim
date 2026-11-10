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
#include "deluge_sim/engine.hpp"
#include "deluge_sim/preset.hpp"
#include "deluge_sim/wav_io.hpp"
#include "fx_factory.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace deluge_sim;

// Las claves por fx, los defaults efectivos y la fábrica de procesadores
// viven en fx_factory.hpp (compartidos con boardsim).

void print_usage() {
    std::cout <<
        "deluge-sim v0.7 (offline DSP harness + board simulator)\n"
        "Uso:\n"
        "  simcli render --in ENTRADA.wav --out SALIDA.wav [--chain a,b,...]"
        " [--preset FILE] [k=v ...] [--block N]\n"
        "    --chain: fx en orden de inserción (default fet76)\n"
        "    --preset: cadena y parámetros base desde un JSON; los k=v del"
        " CLI sobreescriben por encima\n"
        "    k fet76: in_gain_db thresh_db ratio attack_s release_s out_gain_db\n"
        "    k opto:  thresh_db ratio knee_db attack_s release_fast_s"
        " release_slow_s gr_span_db out_gain_db\n"
        "    k eq:    freq_hz boost_db cut_db q_boost q_cut trim_db\n"
        "    k plate: decay_s damp_pct predelay_ms mod_rate_hz mod_depth_ms"
        " mix_pct out_gain_db\n"
        "    k tape:  drive_db bias_pct trim_db\n"
        "    prefijo por fx: fet76.ratio=8 opto.knee_db=3\n"
        "  simcli preset --save FILE [--chain a,b] [k=v ...] | --load FILE\n"
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

int cmd_render(int argc, char** argv) {
    std::string in, out, preset_path;
    double block = double(kDefaultMaxBlock);
    std::vector<std::string> chain{"fet76"};
    bool chain_from_cli = false;
    std::map<std::string, double> kv;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--in" && i + 1 < argc) { in = argv[++i]; }
        else if (a == "--out" && i + 1 < argc) { out = argv[++i]; }
        else if (a == "--preset" && i + 1 < argc) { preset_path = argv[++i]; }
        else if (a == "--block" && i + 1 < argc) {
            if (!parse_double(argv[++i], block) || block < 8 || block > 65536) {
                std::cerr << "ERROR: --block fuera de rango [8,65536]\n";
                return 2;
            }
        } else if (a == "--chain" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string item;
            chain.clear();
            chain_from_cli = true;
            while (std::getline(ss, item, ',')) {
                // recorta espacios del ítem
                const size_t b = item.find_first_not_of(" \t");
                const size_t e = item.find_last_not_of(" \t");
                if (b == std::string::npos) continue;
                item = item.substr(b, e - b + 1);
                if (fx_keys().count(item) == 0) {
                    std::cerr << "ERROR: fx desconocido en --chain: '" << item
                              << "' (válidos: fet76 opto eq plate tape)\n";
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

    // Preset base (opcional): cadena y parámetros desde JSON. Los k=v del
    // CLI (ya parseados) sobreescriben por encima del preset.
    std::string err;
    if (!preset_path.empty()) {
        ChainPreset pr;
        if (!load_preset(preset_path, pr, err)) {
            std::cerr << "ERROR: " << err << "\n";
            return 3;
        }
        if (!chain_from_cli) chain = pr.chain;
        for (const auto& [fx, kvv] : pr.params)
            for (const auto& [k, v] : kvv)
                kv.emplace(fx + "." + k, v);        // emplace: el CLI gana
        std::cout << "Preset cargado: " << preset_path << "\n";
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

    // Parámetros por fx: defaults + preset + k=v (sin prefijo = todos los
    // fx que tengan la clave; con prefijo = solo ese fx).
    const auto m76 = effective_fx_params(kv, "fet76");
    const auto mop = effective_fx_params(kv, "opto");
    const auto meq = effective_fx_params(kv, "eq");
    const auto mpl = effective_fx_params(kv, "plate");
    const auto mta = effective_fx_params(kv, "tape");

    suite_core::Fet76Ref::Params p76;
    p76.in_gain_db  = m76.at("in_gain_db");
    p76.thresh_db   = m76.at("thresh_db");
    p76.ratio       = m76.at("ratio");
    p76.attack_s    = m76.at("attack_s");
    p76.release_s   = m76.at("release_s");
    p76.out_gain_db = m76.at("out_gain_db");

    suite_core::OptoCompRef::Params pop;
    pop.thresh_db      = mop.at("thresh_db");
    pop.ratio          = mop.at("ratio");
    pop.knee_db        = mop.at("knee_db");
    pop.attack_s       = mop.at("attack_s");
    pop.release_fast_s = mop.at("release_fast_s");
    pop.release_slow_s = mop.at("release_slow_s");
    pop.gr_span_db     = mop.at("gr_span_db");
    pop.out_gain_db    = mop.at("out_gain_db");

    suite_core::PassiveEQRef::Params peq;
    peq.freq_hz  = meq.at("freq_hz");
    peq.boost_db = meq.at("boost_db");
    peq.cut_db   = meq.at("cut_db");
    peq.q_boost  = meq.at("q_boost");
    peq.q_cut    = meq.at("q_cut");
    peq.trim_db  = meq.at("trim_db");

    suite_core::PlateReverbRef::Params pplate;
    pplate.decay_s      = mpl.at("decay_s");
    pplate.damp_pct     = mpl.at("damp_pct");
    pplate.predelay_ms  = mpl.at("predelay_ms");
    pplate.mod_rate_hz  = mpl.at("mod_rate_hz");
    pplate.mod_depth_ms = mpl.at("mod_depth_ms");
    pplate.mix_pct      = mpl.at("mix_pct");
    pplate.out_gain_db  = mpl.at("out_gain_db");

    suite_core::TapeSatRef::Params pta;
    pta.drive_db = mta.at("drive_db");
    pta.bias_pct = mta.at("bias_pct");
    pta.trim_db  = mta.at("trim_db");

    WavData wav;
    if (!read_wav(in, wav, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 3;
    }
    if (wav.channels > kMaxChannels) {
        std::cerr << "ERROR: el sim acepta 1 o 2 canales (archivo tiene "
                  << wav.channels << ")\n";
        return 3;
    }

    Engine engine(double(wav.sample_rate), size_t(block));
    for (const std::string& fx : chain) {
        if (fx == "fet76")      engine.add_processor(std::make_unique<AdapterFet76>(p76));
        else if (fx == "opto")  engine.add_processor(std::make_unique<AdapterOpto>(pop));
        else if (fx == "eq")    engine.add_processor(std::make_unique<AdapterPassiveEQ>(peq));
        else if (fx == "plate") engine.add_processor(std::make_unique<AdapterPlateReverb>(pplate));
        else if (fx == "tape")  engine.add_processor(std::make_unique<AdapterTapeSat>(pta));
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

// ------------------------- comando preset ----------------------------
//   simcli preset --save FILE [--chain a,b] [k=v ...]
//   simcli preset --load FILE
int cmd_preset(int argc, char** argv) {
    std::string save_path, load_path;
    std::vector<std::string> chain{"fet76"};
    std::map<std::string, double> kv;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--save" && i + 1 < argc) { save_path = argv[++i]; }
        else if (a == "--load" && i + 1 < argc) { load_path = argv[++i]; }
        else if (a == "--chain" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string item;
            chain.clear();
            while (std::getline(ss, item, ',')) {
                const size_t b = item.find_first_not_of(" \t");
                const size_t e = item.find_last_not_of(" \t");
                if (b == std::string::npos) continue;
                item = item.substr(b, e - b + 1);
                if (fx_keys().count(item) == 0) {
                    std::cerr << "ERROR: fx desconocido en --chain: '" << item
                              << "' (válidos: fet76 opto eq plate tape)\n";
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
            kv[k] = d;
        } else {
            std::cerr << "ERROR: argumento no reconocido: '" << a << "'\n";
            print_usage();
            return 2;
        }
    }

    if (!load_path.empty()) {
        ChainPreset pr;
        std::string err;
        if (!load_preset(load_path, pr, err)) {
            std::cerr << "ERROR: " << err << "\n";
            return 3;
        }
        std::cout << "version: " << pr.version << "\nchain:";
        for (const auto& fx : pr.chain) std::cout << " " << fx;
        std::cout << "\n";
        for (const auto& [fx, kvv] : pr.params) {
            std::cout << fx << ":";
            for (const auto& [k, v] : kvv)
                std::cout << " " << k << "=" << v;
            std::cout << "\n";
        }
        return 0;
    }

    if (save_path.empty()) {
        std::cerr << "ERROR: preset requiere --save FILE o --load FILE\n";
        print_usage();
        return 2;
    }

    for (const auto& [k, v] : kv) {
        if (!key_known(k, chain)) {
            std::cerr << "ERROR: parámetro desconocido para la cadena actual: '"
                      << k << "'\n";
            return 2;
        }
    }

    ChainPreset pr;
    pr.version = 1;
    pr.chain = chain;
    for (const std::string& fx : chain)
        pr.params[fx] = effective_fx_params(kv, fx);
    std::string err;
    if (!save_preset(save_path, pr, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 4;
    }
    std::cout << "OK  preset guardado: " << save_path << " (cadena:";
    for (const auto& fx : pr.chain) std::cout << " " << fx;
    std::cout << ")\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 2; }
    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { print_usage(); return 0; }
    if (cmd == "render") return cmd_render(argc - 2, argv + 2);
    if (cmd == "preset") return cmd_preset(argc - 2, argv + 2);
    std::cerr << "ERROR: comando desconocido: '" << cmd << "'\n";
    print_usage();
    return 2;
}

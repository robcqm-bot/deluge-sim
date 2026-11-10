// =====================================================================
// deluge-sim / main.cpp — simcli: simulator CLI (offline harness)
//
//   simcli render --in input.wav --out output.wav [--chain a,b] [k=v ...]
//       --chain : fx in insertion order (default fet76); valid:
//                 fet76, opto
//       k=v     : unprefixed applies to EVERY fx in the chain that has
//                 that parameter; prefixed "fx.k=v" only to that fx.
//       --block : frames per render block
//   simcli golden --engine tape|fet76|opto|eq|plate --frames N --out FILE
//                 [k=v ...]
//       renders a deterministic test signal (stereo 1 kHz sine, peak
//       0.5) through ONE engine instance (default params + k=v) and
//       writes golden vectors as JSON: input/output interleaved Q2.29
//       int32 + sample rate + engine + effective params. The firmware
//       must reproduce output_q31 EXACTLY from input_q31 (see M1).
//   simcli help
//
// THE ADAPTERS live here (the only bounded bridge between
// deluge_sim and suite_core; the classes are in adapter_*.hpp). In the
// real firmware integration they move to src/deluge/dsp/suite/.
// =====================================================================
#include "deluge_sim/engine.hpp"
#include "deluge_sim/preset.hpp"
#include "deluge_sim/q31_bridge.hpp"
#include "deluge_sim/wav_io.hpp"
#include "fx_factory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace deluge_sim;

// The per-fx keys, the effective defaults and the processor factory
// live in fx_factory.hpp (shared with boardsim).

void print_usage() {
    std::cout <<
        "deluge-sim v0.8 (offline DSP harness + board simulator)\n"
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
        "  simcli golden --engine tape|fet76|opto|eq|plate --frames N"
        " --out FILE [k=v ...]\n"
        "    vectores dorados Q2.29 (JSON): seno 1 kHz 0.5 estéreo,"
        " una instancia del motor,\n"
        "    params por defecto (+k=v, solo claves del motor); N se"
        " recorta a [128,48000]\n"
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
                // trims spaces from the item
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
            kv[k] = d;                         // key validity is
        } else {                               // checked once --chain is known
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

    // Base preset (optional): chain and parameters from JSON. The CLI
    // k=v (already parsed) override on top of the preset.
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
                kv.emplace(fx + "." + k, v);        // emplace: CLI wins
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

    // Per-fx parameters: defaults + preset + k=v (unprefixed = every fx
    // that has the key; prefixed = only that fx).
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

// ------------------------- preset command ----------------------------
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

// ------------------------- golden command ----------------------------
//   simcli golden --engine tape|fet76|opto|eq|plate --frames N --out FILE
//                 [k=v ...]
//
// Writes the M1 golden-vectors file: the DETERMINISTIC contract the
// firmware must reproduce sample-exactly.
//
// JSON format "deluge-sim-golden-v1" (free-form but stable):
//   {
//     "format": "deluge-sim-golden-v1",
//     "engine": "tape",                   // one engine, default params
//     "sample_rate": 48000,               // fixed harness rate
//     "frames": 480,
//     "channels": 2,
//     "block": 512,                       // render cadence (kDefaultMaxBlock)
//     "q31_scale": 536870912,             // 2^29 == 1.0 (Q2.29, see
//                                         // deluge_sim/q31_bridge.hpp)
//     "test_signal": "stereo sine 1000 Hz peak 0.5 phase 0",
//     "params": { "drive_db": 6, ... },   // EFFECTIVE params used
//                                         // (defaults + k=v, %.17g)
//     "input_q31":  [l0, r0, l1, r1, ...],  // 2*frames int32, interleaved
//     "output_q31": [l0, r0, l1, r1, ...]   // same layout, after engine
//   }
// The float render path never touches WAV (no extra quantization): the
// only quantization step is the documented float->q31 bridge at the
// JSON boundary. The firmware check is int32 equality per sample.
int cmd_golden(int argc, char** argv) {
    std::string engine, out;
    double frames_in = 0.0;
    bool frames_seen = false;
    std::map<std::string, double> kv;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--engine" && i + 1 < argc) { engine = argv[++i]; }
        else if (a == "--out" && i + 1 < argc) { out = argv[++i]; }
        else if (a == "--frames" && i + 1 < argc) {
            if (!parse_double(argv[++i], frames_in)) {
                std::cerr << "ERROR: --frames debe ser un numero: '" << argv[i]
                          << "'\n";
                return 2;
            }
            frames_seen = true;
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

    if (fx_keys().count(engine) == 0) {
        std::cerr << "ERROR: motor desconocido: '" << engine
                  << "' (válidos: fet76 opto eq plate tape)\n";
        return 2;
    }
    if (out.empty()) {
        std::cerr << "ERROR: --out FILE es obligatorio\n";
        print_usage();
        return 2;
    }
    if (!frames_seen || frames_in < 1.0) {
        std::cerr << "ERROR: --frames N es obligatorio (rango [128,48000],"
                     " se recorta)\n";
        return 2;
    }
    const double nf = std::min(48000.0, std::max(128.0, frames_in));
    const size_t frames = size_t(std::llround(nf));
    if (nf != frames_in)
        std::cout << "AVISO: --frames ajustado a " << frames
                  << " (rango [128,48000])\n";

    // Only keys of the chosen engine (simplified cmd_render parsing).
    const std::vector<const char*>& valid = fx_keys().at(engine);
    for (const auto& entry : kv) {
        const std::string& k = entry.first;
        if (std::find(valid.begin(), valid.end(), k) == valid.end()) {
            std::cerr << "ERROR: parámetro desconocido para el motor "
                      << engine << ": '" << k << "'\nVálidos:";
            for (const char* vk : valid) std::cerr << ' ' << vk;
            std::cerr << "\n";
            return 2;
        }
    }

    // One engine instance: defaults + k=v (fx_factory is the single
    // source of truth for keys and defaults; it builds the adapters).
    const std::map<std::string, double> pm = effective_fx_params(kv, engine);
    std::unique_ptr<IProcessor> proc = make_fx(engine, pm);
    if (!proc) {                             // unreachable: engine validated
        std::cerr << "ERROR: motor desconocido: '" << engine << "'\n";
        return 2;
    }

    // Deterministic test signal: 1 kHz stereo sine, peak 0.5, phase 0.
    const uint32_t sr = 48000;
    std::vector<float> L(frames), R(frames);
    for (size_t i = 0; i < frames; ++i) {
        const float v = float(0.5 * std::sin(2.0 * 3.14159265358979323846 *
                                             1000.0 * double(i) / double(sr)));
        L[i] = v;
        R[i] = v;
    }

    // Input vectors FIRST (process() is in-place), then render through
    // the single instance in blocks of kDefaultMaxBlock (the Engine's
    // default cadence). No WAV round-trip anywhere.
    std::vector<int32_t> in_q31(frames * 2), out_q31(frames * 2);
    interleave_q31_from_planar(L.data(), R.data(), in_q31.data(), frames);

    ProcessContext ctx;                      // default: stereo, 48 kHz
    ctx.sample_rate = double(sr);
    ctx.max_block = kDefaultMaxBlock;
    ctx.channels = 2;
    proc->prepare(ctx);
    for (size_t off = 0; off < frames; off += kDefaultMaxBlock) {
        Block b;
        b.L = L.data() + off;
        b.R = R.data() + off;
        b.frames = std::min(kDefaultMaxBlock, frames - off);
        b.channels = 2;
        proc->process(b);
    }
    interleave_q31_from_planar(L.data(), R.data(), out_q31.data(), frames);

    std::ofstream f(out);
    if (!f) {
        std::cerr << "ERROR: no se puede escribir: '" << out << "'\n";
        return 4;
    }
    auto write_array = [&](const char* key, const std::vector<int32_t>& v) {
        f << "  \"" << key << "\": [";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) f << ',';
            if (i && i % 8 == 0) f << "\n    ";
            else if (i) f << ' ';
            f << static_cast<long long>(v[i]);
        }
        f << "\n  ]";
    };
    f << "{\n"
      << "  \"format\": \"deluge-sim-golden-v1\",\n"
      << "  \"engine\": \"" << engine << "\",\n"
      << "  \"sample_rate\": " << sr << ",\n"
      << "  \"frames\": " << frames << ",\n"
      << "  \"channels\": 2,\n"
      << "  \"block\": " << kDefaultMaxBlock << ",\n"
      << "  \"q31_scale\": " << kQ31Unity << ",\n"
      << "  \"test_signal\": \"stereo sine 1000 Hz peak 0.5 phase 0\",\n"
      << "  \"params\": {";
    bool first_p = true;
    for (const auto& [k, v] : pm) {
        char num[40];
        std::snprintf(num, sizeof(num), "%.17g", v);
        f << (first_p ? "\n" : ",\n") << "    \"" << k << "\": " << num;
        first_p = false;
    }
    f << (first_p ? "},\n" : "\n  },\n");
    write_array("input_q31", in_q31);
    f << ",\n";
    write_array("output_q31", out_q31);
    f << "\n}\n";
    f.close();
    if (!f) {
        std::cerr << "ERROR: falló la escritura de: '" << out << "'\n";
        return 4;
    }
    std::cout << "OK  golden: " << out << " (engine=" << engine
              << ", frames=" << frames
              << ", entrada/salida Q2.29 intercaladas)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 2; }
    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { print_usage(); return 0; }
    if (cmd == "render") return cmd_render(argc - 2, argv + 2);
    if (cmd == "preset") return cmd_preset(argc - 2, argv + 2);
    if (cmd == "golden") return cmd_golden(argc - 2, argv + 2);
    std::cerr << "ERROR: comando desconocido: '" << cmd << "'\n";
    print_usage();
    return 2;
}

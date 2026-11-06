// =====================================================================
// deluge-sim / main.cpp — simcli: CLI del simulador (harness offline v0.1)
//
//   simcli render --in entrada.wav --out salida.wav [k=v ...]
//       k válidos: thresh_db ratio attack_s release_s in_gain_db out_gain_db
//                  block=<marcos por bloque>
//   simcli help
//
// Aquí vive EL ADAPTADOR (el único puente delimitado entre deluge_sim y
// suite_core). En la integración real al firmware, este adaptador se
// traslada a src/deluge/dsp/suite/fet76_adapter.cpp.
// =====================================================================
#include "deluge_sim/engine.hpp"
#include "deluge_sim/wav_io.hpp"
#include "suite_core/fet76_ref.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace {

using namespace deluge_sim;
using suite_core::Fet76Ref;

// -------- EL ADAPTADOR (cortafuegos: deluge_sim <-> suite_core) --------
class AdapterFet76 : public IProcessor {
public:
    explicit AdapterFet76(const Fet76Ref::Params& p) { f_.set_params(p); }
    const char* name() const override { return "fet76_ref"; }
    void prepare(const ProcessContext& ctx) override {
        f_.set_sample_rate(ctx.sample_rate);
        f_.prepare();
    }
    void process(Block& b) override {
        f_.process(b.L, b.channels == 1 ? nullptr : b.R, b.frames);
    }
    void reset() override { f_.reset(); }
    double gr_db() const { return f_.current_gr_db(); }
    uint64_t bad_inputs() const { return f_.bad_input_count(); }

private:
    Fet76Ref f_;
};

void print_usage() {
    std::cout <<
        "deluge-sim v0.1 (offline DSP harness)\n"
        "Uso:\n"
        "  simcli render --in ENTRADA.wav --out SALIDA.wav [k=v ...] [--block N]\n"
        "    k: thresh_db ratio attack_s release_s in_gain_db out_gain_db\n"
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
    std::string in, out;
    double block = double(kDefaultMaxBlock);
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
        } else if (a.find('=') != std::string::npos) {
            std::string k = a.substr(0, a.find('=')), v = a.substr(a.find('=') + 1);
            static const char* kValid[] = {"thresh_db", "ratio", "attack_s",
                                           "release_s", "in_gain_db", "out_gain_db"};
            bool known = false;
            for (const char* vk : kValid) known = known || (k == vk);
            double d = 0.0;
            if (!known || !parse_double(v, d)) {
                std::cerr << "ERROR: parámetro desconocido o inválido: '" << a << "'\n"
                          << "Válidos: thresh_db ratio attack_s release_s "
                             "in_gain_db out_gain_db\n";
                return 2;
            }
            kv[k] = d;
        } else {
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

    WavData wav;
    std::string err;
    if (!read_wav(in, wav, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 3;
    }
    if (wav.channels > kMaxChannels) {
        std::cerr << "ERROR: el sim v0.1 acepta 1 o 2 canales (archivo tiene "
                  << wav.channels << ")\n";
        return 3;
    }

    Fet76Ref::Params p;   // defaults; se sobreescriben con los k=v presentes
    if (kv.count("thresh_db"))   p.thresh_db   = kv["thresh_db"];
    if (kv.count("ratio"))       p.ratio       = kv["ratio"];
    if (kv.count("attack_s"))    p.attack_s    = kv["attack_s"];
    if (kv.count("release_s"))   p.release_s   = kv["release_s"];
    if (kv.count("in_gain_db"))  p.in_gain_db  = kv["in_gain_db"];
    if (kv.count("out_gain_db")) p.out_gain_db = kv["out_gain_db"];

    Engine engine(double(wav.sample_rate), size_t(block));
    engine.add_processor(std::make_unique<AdapterFet76>(p));

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
              << "s | x" << report.realtime_factor << " realtime\n";
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

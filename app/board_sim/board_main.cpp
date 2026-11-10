// =====================================================================
// deluge-sim / board_main.cpp — boardsim: simulador de placa (fase 2)
//
//   boardsim --sd CARPETA [--song N] [--hw-block N] [--list] [k=v ...]
//       --sd        carpeta del host que hace de "tarjeta SD" (*.wav)
//       --song N    índice de canción inicial (default 0)
//       --hw-block  marcos por bloque hardware (default 128, rango [8,4096])
//       --list      lista las canciones y sale (sin ventana)
//       k válidos (fet76): thresh_db ratio attack_s release_s
//                          in_gain_db out_gain_db
//
//   Controles: ESPACIO play/stop | B bypass | ARRIBA/ABAJO canción |
//              click en pads | ESC o cerrar ventana = salir
//   Pads: fila 0 col 0 = PLAY/STOP, fila 0 col 1 = BYPASS, resto libres.
//
// La ÚNICA pieza que toca SDL2 es este archivo: board_core es SDL-free
// (testeable headless) y el cortafuegos suite_core queda intacto.
// =====================================================================
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "adapter_fet76.hpp"
#include "board_core.hpp"
#include "deluge_sim/wav_io.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace deluge_sim;
using deluge_sim::board::BoardClock;
using deluge_sim::board::BoardPump;
using deluge_sim::board::PadGrid;
using deluge_sim::board::SdCard;
using deluge_sim::board::kPadCols;
using deluge_sim::board::kPadRows;

constexpr int kWindowW = 720;
constexpr int kWindowH = 560;
constexpr int kCellPx = 56;
constexpr int kGapPx = 6;
constexpr int kQueueBlocks = 8;          // bloques hardware de cola (~21 ms)

void print_usage() {
    std::cout <<
        "deluge-sim boardsim v0.3 (simulador de placa)\n"
        "Uso:\n"
        "  boardsim --sd CARPETA [--song N] [--hw-block N] [--list] [k=v ...]\n"
        "    k: thresh_db ratio attack_s release_s in_gain_db out_gain_db\n"
        "Controles: ESPACIO play/stop | B bypass | ARRIBA/ABAJO canción | "
        "click en pads | ESC salir\n";
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

bool parse_long(const std::string& s, long& out) {
    try {
        size_t used = 0;
        long v = std::stol(s, &used);
        if (used != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// Dibuja un rectángulo relleno con color RGB.
void fill_rect(SDL_Renderer* r, int x, int y, int w, int h,
               Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

int run_board(int argc, char** argv) {
    std::string sd_folder;
    long song_index = 0;
    double hw_block_d = double(board::kHardwareBlockFrames);
    bool list_only = false;
    std::map<std::string, double> kv;

    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sd" && i + 1 < argc) { sd_folder = argv[++i]; }
        else if (a == "--song" && i + 1 < argc) {
            if (!parse_long(argv[++i], song_index) || song_index < 0) {
                std::cerr << "ERROR: --song debe ser un índice >= 0\n";
                return 2;
            }
        }
        else if (a == "--hw-block" && i + 1 < argc) {
            if (!parse_double(argv[++i], hw_block_d) ||
                hw_block_d < 8 || hw_block_d > 4096) {
                std::cerr << "ERROR: --hw-block fuera de rango [8,4096]\n";
                return 2;
            }
        }
        else if (a == "--list") { list_only = true; }
        else if (a.find('=') != std::string::npos) {
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
    if (sd_folder.empty()) {
        std::cerr << "ERROR: --sd CARPETA es obligatorio (la 'tarjeta SD' es una "
                     "carpeta del host con *.wav)\n";
        print_usage();
        return 2;
    }

    // ---- tarjeta SD (carpeta del host) ----
    SdCard sd;
    std::string err;
    if (!sd.mount(sd_folder, err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 3;
    }
    std::cout << "SD montada: " << sd.folder() << " (" << sd.song_count()
              << " canciones)\n";
    for (size_t i = 0; i < sd.songs().size(); ++i)
        std::cout << "  [" << i << "] " << sd.songs()[i] << "\n";
    if (list_only) return 0;
    if (size_t(song_index) >= sd.song_count()) song_index = 0;

    // ---- reloj del board + cadena (mismo adaptador que simcli) ----
    BoardClock clock;
    clock.hw_block = size_t(hw_block_d);
    BoardPump pump(clock);

    suite_core::Fet76Ref::Params p;   // defaults; sobrescritos por los k=v
    if (kv.count("thresh_db"))   p.thresh_db   = kv["thresh_db"];
    if (kv.count("ratio"))       p.ratio       = kv["ratio"];
    if (kv.count("attack_s"))    p.attack_s    = kv["attack_s"];
    if (kv.count("release_s"))   p.release_s   = kv["release_s"];
    if (kv.count("in_gain_db"))  p.in_gain_db  = kv["in_gain_db"];
    if (kv.count("out_gain_db")) p.out_gain_db = kv["out_gain_db"];
    auto adapter = std::make_unique<AdapterFet76>(p);
    AdapterFet76* adapter_raw = adapter.get();
    pump.add_processor(std::move(adapter));

    // ---- carga de la canción seleccionada (debe seguir viva: es local) ----
    WavData song;
    auto load_song_index = [&](long idx) -> bool {
        if (!read_wav(sd.songs()[size_t(idx)], song, err)) {
            std::cerr << "ERROR: " << err << "\n";
            return false;
        }
        if (song.channels > kMaxChannels) {
            std::cerr << "ERROR: el board v0.2 acepta 1 o 2 canales (archivo tiene "
                      << song.channels << ")\n";
            return false;
        }
        if (!pump.load_song(song, err)) {
            std::cerr << "ERROR: " << err << "\n";
            return false;
        }
        std::cout << "Canción [" << idx << "]: " << sd.songs()[size_t(idx)]
                  << " (" << song.frames() << " marcos @ " << song.sample_rate
                  << " Hz)\n";
        return true;
    };
    if (!load_song_index(song_index)) return 3;

    // ---- SDL: vídeo + audio ----
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "ERROR: SDL_Init: " << SDL_GetError() << "\n";
        return 3;
    }

    SDL_AudioSpec want{};
    want.freq = int(clock.sample_rate);
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = Uint16(clock.hw_block * 2);      // ventana del dispositivo
    SDL_AudioSpec have{};
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "ERROR: no se pudo abrir el dispositivo de audio: "
                  << SDL_GetError() << "\n";
        SDL_Quit();
        return 3;
    }

    SDL_Window* win = SDL_CreateWindow("deluge-sim board v0.3",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       kWindowW, kWindowH, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!win || !ren) {
        std::cerr << "ERROR: no se pudo crear la ventana: " << SDL_GetError() << "\n";
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_CloseAudioDevice(dev);
        SDL_Quit();
        return 3;
    }
    SDL_PauseAudioDevice(dev, 0);                   // el dispositivo corre ya

    const int grid_w = kPadCols * kCellPx + (kPadCols - 1) * kGapPx;
    const int x0 = (kWindowW - grid_w) / 2;
    const int y0 = 48;

    PadGrid pads;
    std::vector<float> outbuf(clock.hw_block * 2, 0.0f);
    const Uint32 target_bytes = Uint32(clock.hw_block * 2 * sizeof(float) * kQueueBlocks);
    bool starved = false;
    uint64_t underruns = 0;
    const auto t0 = std::chrono::steady_clock::now();

    bool running = true;
    while (running) {
        // ---- eventos ----
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; }
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:
                        if (pump.is_playing()) {
                            pump.stop();
                            SDL_ClearQueuedAudio(dev);
                            pads.set_playing(false);
                            std::cout << "STOP\n";
                        } else {
                            pump.start();
                            pads.set_playing(true);
                            std::cout << "PLAY\n";
                        }
                        break;
                    case SDLK_b:
                        pump.set_bypass(!pump.bypassed());
                        pads.set_bypassed(pump.bypassed());
                        std::cout << (pump.bypassed() ? "BYPASS ON\n" : "BYPASS OFF\n");
                        break;
                    case SDLK_UP:
                    case SDLK_DOWN: {
                        long dir = (e.key.keysym.sym == SDLK_UP) ? -1 : 1;
                        long n = long(sd.song_count());
                        song_index = ((song_index + dir) % n + n) % n;
                        pump.stop();
                        SDL_ClearQueuedAudio(dev);
                        pads.set_playing(false);
                        if (load_song_index(song_index)) {
                            std::cout << "SELECT " << song_index << "\n";
                        } else {
                            running = false;        // error accionable ya impreso
                        }
                        break;
                    }
                    default: break;
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int col = (e.button.x - x0) / (kCellPx + kGapPx);
                int row = (e.button.y - y0) / (kCellPx + kGapPx);
                bool inside = (e.button.x - x0) >= 0 && (e.button.y - y0) >= 0 &&
                              col >= 0 && col < kPadCols && row >= 0 && row < kPadRows &&
                              (e.button.x - x0) % (kCellPx + kGapPx) < kCellPx &&
                              (e.button.y - y0) % (kCellPx + kGapPx) < kCellPx;
                if (inside) {
                    if (row == 0 && col == 0) {          // pad PLAY/STOP
                        if (pump.is_playing()) {
                            pump.stop();
                            SDL_ClearQueuedAudio(dev);
                            pads.set_playing(false);
                            std::cout << "STOP\n";
                        } else {
                            pump.start();
                            pads.set_playing(true);
                            std::cout << "PLAY\n";
                        }
                    } else if (row == 0 && col == 1) {   // pad BYPASS
                        pump.set_bypass(!pump.bypassed());
                        pads.set_bypassed(pump.bypassed());
                        std::cout << (pump.bypassed() ? "BYPASS ON\n" : "BYPASS OFF\n");
                    } else {
                        pads.toggle(row, col);           // pads libres
                    }
                }
            }
        }

        // ---- lazo de cadencia fija: llena la cola del dispositivo ----
        pump.set_bypass(pads.is_bypassed());
        if (pads.is_playing()) {
            const size_t queued = SDL_GetQueuedAudioSize(dev);
            if (queued == 0) {
                if (!starved) { ++underruns; starved = true; }  // el dispositivo se quedó sin datos
            } else {
                starved = false;
            }
            while (pads.is_playing() && SDL_GetQueuedAudioSize(dev) < target_bytes) {
                size_t n = pump.pump(outbuf.data(), clock.hw_block);
                if (n == 0) {                            // fin de canción
                    pads.set_playing(false);
                    SDL_ClearQueuedAudio(dev);
                    std::cout << "FIN DE CANCIÓN (auto-stop)\n";
                } else {
                    SDL_QueueAudio(dev, outbuf.data(), Uint32(n * 2 * sizeof(float)));
                }
            }
        }

        // ---- dibujo ----
        fill_rect(ren, 0, 0, kWindowW, kWindowH, 24, 26, 30);
        for (int r = 0; r < kPadRows; ++r) {
            for (int c = 0; c < kPadCols; ++c) {
                const int x = x0 + c * (kCellPx + kGapPx);
                const int y = y0 + r * (kCellPx + kGapPx);
                Uint8 cr = 44, cg = 48, cb = 56;         // apagado
                if (r == 0 && c == 0) {                  // PLAY
                    if (pads.is_lit(r, c)) { cr = 60; cg = 220; cb = 120; }
                    else { cr = 34; cg = 70; cb = 50; }
                } else if (r == 0 && c == 1) {           // BYPASS
                    if (pads.is_lit(r, c)) { cr = 240; cg = 180; cb = 60; }
                    else { cr = 80; cg = 62; cb = 30; }
                } else if (pads.is_lit(r, c)) {
                    cr = 0; cg = 200; cb = 255;
                }
                fill_rect(ren, x, y, kCellPx, kCellPx, cr, cg, cb);
            }
        }
        // Barra de progreso (playhead) bajo el grid.
        const double frac = song.frames() > 0
            ? double(pump.stats().frames_rendered) / double(song.frames()) : 0.0;
        fill_rect(ren, x0, kWindowH - 56, grid_w, 10, 60, 64, 72);
        fill_rect(ren, x0, kWindowH - 56,
                  int(grid_w * (frac > 1.0 ? 1.0 : frac)), 10, 120, 200, 255);
        // Medidor de GR del fet76 (0 dB arriba, -24 dB abajo) a la derecha.
        const double gr = adapter_raw->gr_db();          // [0, -24]
        const double grf = (0.0 - gr) / 24.0;            // 0=sin GR, 1=GR máx
        fill_rect(ren, x0 + grid_w + 12, y0, 14, kPadRows * kCellPx + 7 * kGapPx,
                  60, 64, 72);
        fill_rect(ren, x0 + grid_w + 12, y0,
                  14, int((kPadRows * kCellPx + 7 * kGapPx) *
                          (grf < 0.0 ? 0.0 : (grf > 1.0 ? 1.0 : grf))),
                  220, 90, 90);
        SDL_RenderPresent(ren);
        SDL_Delay(5);                                    // ~200 fps de UI, sobra
    }

    // ---- salida con telemetría ----
    const RunReport& st = pump.stats();
    const double wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    std::cout << "Sesión: " << st.frames_rendered << " marcos en "
              << st.blocks_rendered << " bloques de " << clock.hw_block
              << " | underruns=" << underruns
              << " | x" << (wall > 1e-9 ? double(st.frames_rendered) / clock.sample_rate / wall : 0.0)
              << " realtime\n";
    if (!st.warning.empty()) std::cout << "AVISO: " << st.warning << "\n";

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 2; }
    return run_board(argc - 1, argv + 1);
}

# deluge-sim

**A PC-side audio pipeline harness for developing DSP processors** — inspired
by the workflow of standalone hardware like the Synthstrom Audible Deluge, and
designed so that the same DSP cores can later run on embedded targets
(ARM Cortex-M7) or as desktop plugins.

**v0.1 — offline harness**: load a WAV → process through a chain of
processors → render a WAV, with zero external dependencies.

**v0.2 — board simulator (phase 2)**: `board_sim`, an SDL2 target that
mimics the board workflow — "SD card" (a host folder of WAVs), an 8×8 pad
grid, fixed-hardware block cadence and live audio device output — so
processors can be exercised interactively without flashing firmware. The
core harness stays dependency-free; SDL2 is linked only by this app target.

**v0.3 — processor chain + opto reference**: `simcli render --chain` builds
a chain of reference processors in insertion order (shared k=v parameters
or per-fx `fx.k=v` prefixes), and a second reference processor ships:
`OptoCompRef`, an opto-cell compressor with asymmetric (slow) ballistics
and program-dependent release.

**v0.4 — passive program EQ**: third reference processor `PassiveEQRef`
(resonant low boost + program cut band above it) reproducing the classic
passive-EQ boost/cut trick; its full magnitude response is exposed as an
exact analytic `magnitude_db()`.

**v0.5 — plate reverb**: fourth reference processor `PlateReverbRef` — a
modulated 8-line FDN with Hadamard feedback matrix, per-line RT60 gains
(every line decays at exactly `decay_s`), loop damping, predelay, input
diffusion and stereo-decorrelated tap groups.

- ✅ Working reference processors: `Fet76Ref` (textbook FET-style feedback
  compressor, stereo-linked, one-pole ballistics), `OptoCompRef`
  (opto-cell compressor with soft-knee gain law, slow photoresistor
  attack and program-dependent release), `PassiveEQRef` (passive-style
  program EQ with the boost/cut trick) and `PlateReverbRef` (modulated
  FDN plate reverb)
- ✅ Dependency-free RIFF/WAV reader & writer (PCM16/24/32, float32,
  WAVE_FORMAT_EXTENSIBLE)
- ✅ Test suite with mathematically documented expectations (31 cases)
- ✅ Clean module boundary ("firewall") between the host framework and
  your DSP code — see below

> This is an independent community project — not affiliated with, endorsed by,
> or derived from the code of Synthstrom Audible.

## Why the "firewall" architecture matters

The repository is split into two **independent** static libraries:

```
┌────────────────────────────────────────────┐
│ suite_core/   ← YOUR DSP (portable C++)    │  no includes from the host
│   pure float buffers in/out, no allocations│
├────────────────────────────────────────────┤
│ (the ONLY bridge: a ~30-line adapter)      │
├────────────────────────────────────────────┤
│ include/deluge_sim + src/   ← host harness │  WAV I/O, engine, CLI
└────────────────────────────────────────────┘
```

Because `suite_core` never touches host types, the same source file compiles
unchanged for:

- this PC harness (`simcli`),
- embedded targets (e.g. Cortex-M7 firmware) — phase 2,
- desktop plugin formats (VST3/CLAP/AU) — roadmap.

## Build

Requirements: a C++17 compiler. No external libraries for the core
(`simcli`, `simtests`).

```bash
# CMake (any generator)
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build          # runs the acceptance suite

# or plain g++ (MinGW/Git Bash/Linux)
./build.sh run
```

### Board simulator (v0.2, optional)

`board_sim` needs SDL2 and is built only when SDL2 is found — the rest of
the project builds identically without it.

```bash
# Linux (or CI): install SDL2 first
sudo apt-get install libsdl2-dev

# Windows + MinGW: point CMake at the official SDL devel package
# (download SDL2-devel-<ver>-mingw.tar.gz from libsdl-org releases;
#  <sdk> is the extracted SDL2-<ver>/x86_64-w64-mingw32 folder)
cmake -S . -B build -DSDL2_SDK_DIR=/path/to/SDL2-2.32.10/x86_64-w64-mingw32
cmake --build build --config Release   # SDL2.dll is copied next to the exe
```

## Usage

```bash
# run the acceptance tests (31 cases, mathematically documented)
build/simtests.exe

# render a file through a chain of reference processors
build/simcli.exe render --in input.wav --out output.wav \
    --chain eq,plate freq_hz=150 boost_db=3 decay_s=3.2 damp_pct=45

# per-fx parameters (unprefixed keys apply to every fx that has them)
build/simcli.exe render --in in.wav --out out.wav --chain fet76,opto \
    fet76.ratio=8 opto.knee_db=3 opto.release_slow_s=1.0

# fet76 keys: in_gain_db thresh_db ratio attack_s release_s out_gain_db
# opto keys:  thresh_db ratio knee_db attack_s release_fast_s
#             release_slow_s gr_span_db out_gain_db
# eq keys:    freq_hz boost_db cut_db q_boost q_cut trim_db
# plate keys: decay_s damp_pct predelay_ms mod_rate_hz mod_depth_ms
#             mix_pct out_gain_db
# --block N   (frames per render block, 8..65536)
```

### Board simulator

```bash
# list the songs found on the "SD card" (any folder with WAVs)
build/board_sim.exe --list --sd /path/to/sd_folder

# open the board window (song 0 loads ready to play)
build/board_sim.exe --sd /path/to/sd_folder --song 0 --hw-block 128 \
    thresh_db=-12 ratio=4

# controls: SPACE play/stop | B bypass | UP/DOWN song | click pads | ESC quit
```

The board runs the same `IProcessor` chain as the CLI at a fixed hardware
cadence (default 128-frame blocks @ 48 kHz): the UI pumps blocks to fill
the audio device queue, exactly like an audio ISR on real hardware. The
window shows the 8×8 pad grid, a playhead bar and the compressor GR meter.
Pads `row0col0` (play) and `row0col1` (bypass) respond to clicks.

Every render prints a report (frames, blocks, wall time, realtime factor).
On a modern PC expect well over ×500 realtime for one compressor instance.

## Data flow

```
WAV file ──read_wav──► WavData (interleaved float)
                          │  FileAudioSource (de-interleave, mono→stereo)
                          ▼
                 Block{L,R} ── guard: non-finite inputs ──►
                     IProcessor chain (in insertion order)
                          ▼
                 WavAudioSink ──finalize──► WAV PCM16 + RunReport
```

## Roadmap

See [TODO.md](TODO.md). Highlights:

1. ~~**Phase 2 — board simulator**~~ ✅ delivered in v0.2 (`board_sim`,
   SDL2). Next steps for it: preset/clip persistence on the "SD card" and
   a song-browsing UI.
2. ~~More reference processors (opto-style...)~~ **opto shipped in v0.3**,
   **passive EQ with boost/cut trick shipped in v0.4**, **plate FDN reverb
   shipped in v0.5**. Next: tape-style saturation — same `IProcessor`
   contract.
3. Streaming file source for very long renders.
4. Preset import/export.

## Contributing

PRs are welcome — especially: new reference processors (with tests!), DSP
accuracy improvements, and ports/build fixes. Please keep `suite_core/`
free of host-framework includes (that's the point of the firewall).

## License

GPL-3.0 — see [LICENSE](LICENSE). Code comments are partially in Spanish;
English translations are welcome.

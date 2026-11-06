# deluge-sim

**A PC-side audio pipeline harness for developing DSP processors** — inspired
by the workflow of standalone hardware like the Synthstrom Audible Deluge, and
designed so that the same DSP cores can later run on embedded targets
(ARM Cortex-M7) or as desktop plugins.

**v0.1 — offline harness**: load a WAV → process through a chain of
processors → render a WAV, with zero external dependencies.

- ✅ Working reference processor: `Fet76Ref`, a textbook FET-style
  feedback compressor (stereo-linked, one-pole ballistics)
- ✅ Dependency-free RIFF/WAV reader & writer (PCM16/24/32, float32,
  WAVE_FORMAT_EXTENSIBLE)
- ✅ Test suite with mathematically documented expectations
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

Requirements: a C++17 compiler. No external libraries.

```bash
# CMake (any generator)
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build          # runs the acceptance suite

# or plain g++ (MinGW/Git Bash/Linux)
./build.sh run
```

## Usage

```bash
# run the acceptance tests (8 cases, mathematically documented)
build/simtests.exe

# render a file through the reference compressor
build/simcli.exe render --in input.wav --out output.wav \
    thresh_db=-18 ratio=8 attack_s=0.0008 release_s=0.25 in_gain_db=3

# parameters: thresh_db ratio attack_s release_s in_gain_db out_gain_db
#             --block N   (frames per render block, 8..65536)
```

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

1. **Phase 2 — board simulator**: an SDL2-based board target (display, pads,
   audio device) so processors can be exercised inside a hardware-like loop
   without flashing firmware.
2. More reference processors (opto-style, passive-EQ-style, plate-style FDN
   reverb) behind the same `IProcessor` contract.
3. Streaming file source for very long renders.
4. Preset import/export.

## Contributing

PRs are welcome — especially: new reference processors (with tests!), DSP
accuracy improvements, and ports/build fixes. Please keep `suite_core/`
free of host-framework includes (that's the point of the firewall).

## License

GPL-3.0 — see [LICENSE](LICENSE). Code comments are partially in Spanish;
English translations are welcome.

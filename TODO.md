# TODO — deluge-sim

## v0.2 — board simulator (phase 2) ✅ (entregado)
- [x] SDL2 board target: display window, pad grid, audio device output
- [x] Clock/timing stubs so the render loop behaves like fixed-hardware audio
- [x] "SD card" = host folder (songs/presets readable directly)

## More reference processors (each with tests + documented expectations)
- [x] Opto-style compressor (slow photoresistor ballistics + program-
      dependent release) ✅ v0.3 (`OptoCompRef`)
- [x] Passive-style program EQ (boost/cut trick) ✅ v0.4 (`PassiveEQRef`,
      recorte a 2.5×f0 y el truco verificado contra |H| analítica)
- [x] Plate-style FDN reverb (modulated) ✅ v0.5 (`PlateReverbRef`, FDN8
      Hadamard + RT60 por línea + damping + LFOs; RT60 medido = objetivo)
- [x] Tape-style saturation (per-track insert) ✅ v0.6 (`TapeSatRef`,
      bias asimétrico, impar: y(0)=0 y y(-x)=-y(x) exactos)

## Infrastructure
- [x] Streaming file source (blocks from disk) ✅ v0.6 (`StreamWavSource`
      + interfaz `IAudioSource`; bit-exacto vs la fuente en RAM)
- [x] Preset import/export (JSON) ✅ v0.6 (`preset.hpp` JSON mini sin
      dependencias; `simcli preset --save/--load` y `render --preset`)
- [x] GitHub Actions CI matrix (Windows MSVC + Ubuntu gcc + MinGW)
      ✅ v0.6 (job windows-mingw con MSYS2 UCRT64)
- [x] English pass over code comments ✅ (37 files; string literals and
      CLI messages intentionally remain Spanish — documented behavior
      covered by tests)
- [x] Board: persistencia de presets en la SD y slots en la ventana
      ✅ v0.7 (fila 7 de pads + IZQ/DER + S guarda `preset_NN.json`;
      fábrica de fx compartida `fx_factory.hpp`)
- [x] q31 bridge (float <-> firmware fixed point) + golden vectors CLI +
      parity tests against the firmware-side adapter
      ✅ v0.8 (`q31_bridge` unity 2^31 verified vs firmware ONE_Q31;
      `simcli golden --engine <name>`; parity 0 LSB over 960 samples;
      59 cases / 254 checks)
- [x] ARM target verification of suite_core (Deluge Cortex-A9)
      ✅ v0.8 (`scripts/arm_check.sh`, xPack 13.2.1, Thumb-2 + VFPv3
      hard float, 5/5 objects 0 warnings)

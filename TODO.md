# TODO — deluge-sim

## v0.2 — board simulator (phase 2) ✅ (entregado)
- [x] SDL2 board target: display window, pad grid, audio device output
- [x] Clock/timing stubs so the render loop behaves like fixed-hardware audio
- [x] "SD card" = host folder (songs/presets readable directly)
- [ ] Siguientes: persistencia de clips/presets en la "SD" y navegador de
      canciones en la ventana

## More reference processors (each with tests + documented expectations)
- [x] Opto-style compressor (slow photoresistor ballistics + program-
      dependent release) ✅ v0.3 (`OptoCompRef`)
- [x] Passive-style program EQ (boost/cut trick) ✅ v0.4 (`PassiveEQRef`,
      recorte a 2.5×f0 y el truco verificado contra |H| analítica)
- [x] Plate-style FDN reverb (modulated) ✅ v0.5 (`PlateReverbRef`, FDN8
      Hadamard + RT60 por línea + damping + LFOs; RT60 medido = objetivo)
- [ ] Tape-style saturation (per-track insert)

## Infrastructure
- [ ] Streaming file source (blocks from disk, for hour-long renders)
- [ ] Preset import/export (JSON)
- [ ] GitHub Actions CI matrix (Windows MSVC + Ubuntu gcc + MinGW)
- [ ] English pass over code comments (currently partial Spanish)

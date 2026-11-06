# TODO — deluge-sim

## v0.2 — board simulator (phase 2)
- [ ] SDL2 board target: display window, pad grid, audio device output
- [ ] Clock/timing stubs so the render loop behaves like fixed-hardware audio
- [ ] "SD card" = host folder (songs/presets readable directly)

## More reference processors (each with tests + documented expectations)
- [ ] Opto-style compressor (slow photoresistor ballistics)
- [ ] Passive-style program EQ (boost/cut trick)
- [ ] Plate-style FDN reverb (modulated)
- [ ] Tape-style saturation (per-track insert)

## Infrastructure
- [ ] Streaming file source (blocks from disk, for hour-long renders)
- [ ] Preset import/export (JSON)
- [ ] GitHub Actions CI matrix (Windows MSVC + Ubuntu gcc + MinGW)
- [ ] English pass over code comments (currently partial Spanish)

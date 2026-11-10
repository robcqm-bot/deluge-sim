# ARCHITECTURE.md — deluge-sim v0.3

> (Documento de arquitectura en español — traducciones al inglés bienvenidas.)

## 1. Architecture Summary

**Patrones usados y por qué:**

| Patrón | Dónde | Decisión |
|---|---|---|
| **Strategy** | `IProcessor` | Cada FX (Fet76Ref hoy, PLATE-140 mañana) es intercambiable sin tocar el motor |
| **Pipeline** | `Engine::run` / `BoardPump::pump` | Render por bloques: fuente → cadena ordenada → sumidero; espejo del flujo real del firmware Deluge (blocks → FX → master) |
| **Firewall (module boundary)** | `suite_core/` vs `include/deluge_sim/` | Dos bibliotecas estáticas INDEPENDIENTES; la dependencia va en una sola dirección (`app → ambas`). Ningún header cruza la frontera. El único puente es un adaptador de ~30 líneas (`AdapterFet76` en `app/adapter_fet76.hpp`) |
| **Adapter** | `AdapterFet76`, `AdapterOpto` | Traduce `deluge_sim::Block` (tipo del sim) → punteros crudos `float*` (tipo de suite-core). En la integración real al firmware estos archivos viven en `src/deluge/dsp/suite/`. Compartidos por simcli y boardsim (headers `app/adapter_*.hpp`) |
| **DTO + Report** | `RunReport` | Telemetría explícita en vez de prints dispersos |
| **Facade + composición** | `BoardPump` (v0.2) | El lazo interactivo del board REUSA `FileAudioSource`, `IProcessor` y la guardia del engine en vez de duplicar el pipeline: solo cambia el modo de conducción (cadencia fija a demanda vs batch completo) |

**Decisiones clave:**
1. **Planar stereo** (`L[]`/`R[]` separados) y no intercalado: el DSP del M7 del Deluge trabaja por canal; evita mezclas de strides en suite-core.
2. **Memoria del bloque propiedad del Engine**, asignada UNA vez: cero allocaciones dentro del render (contrato obligatorio para IProcessor).
3. **suite_core no enlaza deluge_sim**: así el mismo `.cpp` compila mañana para el firmware (M7), para Teensy o como VST — la razón de ser del cortafuegos.
4. **SDL2 es la primera dependencia externa (v0.2) y vive SOLO en la capa app**: el núcleo (`deluge_sim_lib`, `suite_core`, `board_core`, `simcli`, `simtests`) sigue siendo cero-dependencias; solo `board_sim` enlaza SDL2 y solo él incluye `<SDL2/SDL.h>`. En CMake el target es OPCIONAL: si no hay SDL2 se omite con un aviso y todo lo demás compila igual (ci.yml instala SDL solo en el job Ubuntu).

## 2. Folder Structure (exacta)

```
deluge-sim\
├── CMakeLists.txt                        # 6 targets (ver §6)
├── build.sh                              # fallback directo g++ sin CMake
├── README.md
├── TODO.md                               # roadmap público
├── docs\ARCHITECTURE.md                  # este documento
├── include\deluge_sim\
│   ├── core.hpp                          # Block, ProcessContext, IProcessor, RunReport
│   ├── wav_io.hpp                        # WavData, read_wav, write_wav16
│   └── engine.hpp                        # FileAudioSource, WavAudioSink, Engine
├── src\
│   ├── wav_io.cpp
│   └── engine.cpp
├── suite_core\                           # <<< ZONA PROPIA (cortafuegos)
│   ├── include\suite_core\fet76_ref.hpp  # compresor FET feedback
│   ├── include\suite_core\opto_comp.hpp  # compresor óptico (v0.3)
│   └── src\fet76_ref.cpp
│   └── src\opto_comp.cpp
├── app\
│   ├── adapter_fet76.hpp                 # adaptador fet76 (el puente)
│   ├── adapter_opto.hpp                  # adaptador opto (v0.3)
│   ├── main.cpp                          # simcli (--chain desde v0.3)
│   └── board_sim\                        # <<< fase 2 (v0.2)
│       ├── board_clock.hpp               # stubs de reloj (48 kHz, bloque 128)
│       ├── board_core.hpp/.cpp           # SdCard + PadGrid + BoardPump (SDL-free)
│       └── board_main.cpp                # boardsim: ÚNICO archivo que enlaza SDL2
└── tests\
    └── test_all.cpp                      # simtests (exit 0 = aceptación, 19 pruebas)
```

## 3. Data Flow

```
archivo .wav
   │  read_wav()                        (RIFF→float intercalado)
   ▼
WavData (RAM, intercalado)
   │  FileAudioSource::read()           (de-interleave → L/R planar,
   │                                     mono→estéreo si 1 canal)
   ▼
Block{L,R,frames} ──► guardia no-finitos (engine)
   │
   ▼  por cada IProcessor en orden de inserción:
   │     AdapterFet76.process(Block&)
   │        └─ suite_core::Fet76Ref.process(float* L, float* R, n)
   │             detector feedback → balística one-pole → ganancia
   ▼
WavAudioSink.write()  (acumula intercalado)
   │  finalize() → write_wav16 (clamp ±1, PCM16)
   ▼
archivo .wav de salida + RunReport (marcos, bloques, tiempo, RT factor)
```

El adaptador es el ÚNICO lugar donde un tipo de `deluge_sim` toca un tipo de
`suite_core` — y solo pasando punteros crudos: cero tipos compartidos.

## 4. Edge Case & Error Management

| Caso | Manejo | Dónde |
|---|---|---|
| WAV truncado / data > archivo | clamp del tamaño al real del archivo | `read_wav` (escaneo tolerante) |
| `fmt` ausente, `data` vacío, no-WAV, <44 B | error accionable por `err`, nunca excepción | `read_wav` |
| WAVE_FORMAT_EXTENSIBLE | lee SubFormat GUID (2 primeros bytes) | `read_wav` |
| PCM 16/24/32 y float32 | los tres decodificados con signo correcto (24-bit sign-extend) | `decode_sample` |
| Canales > 2 | lectura acepta (hasta 8) pero simcli rechaza con mensaje claro; `FileAudioSource` se defiende igual (false) | main + `FileAudioSource` |
| Sample rate 0 o absurdo | rechazo con rango válido | `read_wav` |
| Último bloque parcial (marcos % block ≠ 0) | bucle entrega n=min(max, restantes); test 1000/512 lo verifica | `FileAudioSource::read` |
| Entrada de 0 marcos | corrida válida: WAV solo-cabecera + `RunReport.warning` | `Engine::run` |
| NaN/Inf en entrada | guardia doble: engine sustituye y cuenta; Fet76Ref además ignora internamente | ambos lados |
| Denormales (1e-30) | floor del detector a −240 dB + salida finita (test dedicado) | `Fet76Ref` |
| Procesador que lanza excepción | capturada por bloque con nombre del procesador y nº de bloque; corrida aborta LIMPIA (reporte parcial) | `Engine::run` |
| Parámetros fuera de rango | clamp centralizado en `set_params` (nunca estado inválido) | `Fet76Ref::set_params` |
| CLI: k=v desconocido, números inválidos, --block fuera de rango | rechazo con lista de claves válidas, exit 2 | `cmd_render` |
| Disco lleno / permisos al escribir | `finalize` devuelve false con `err` (no lanza) | `write_wav16` |
| Mono → estéreo y estéreo → mono | fold explícito documentado en fuente y sumidero | ambos |

**Filosofía**: errores en `err` + bool (no excepciones cruzando fronteras);
excepciones solo se capturan, nunca se lanzan hacia fuera del engine.

## 5. Performance Evaluation

**Complejidad temporal**: render O(N·P) con N = marcos totales y P = procesadores
en cadena — lineal, streaming: cada muestra se toca exactamente una vez por
procesador. Sin passes adicionales (salvo la guardia O(N) integrada en el mismo
bucle de lectura).

**Complejidad espacial**: O(entrada) por cargar el WAV fuente a RAM en v0.1
(decisión documentada: renders de prueba); el pipeline en sí es O(max_block) =
512×2 floats ≈ 4 KB. El sumidero acumula la salida: O(salida). Para archivos
de horas, la fase 2 cambiará `FileAudioSource` a streaming por bloques desde
disco (interfaz ya compatible: solo cambia la implementación de `read`).

**Costo por muestra de Fet76Ref**: 1 `log` + 1 `exp` + aritmética escalar
(el `pow` fue sustituido por `exp(gr·ln10/20)`). En el M7 a 600 MHz esto es
~50-80 ciclos/muestra ≈ **0.25% de CPU por instancia a 48k** — el presupuesto
de la suite completa (canal+master+plate) queda holgado; la medición real
corresponde a la fase 0 (DWT en silicio, no en el sim).

**Realtime factor esperado en PC**: >1000× (referencia); el reporte lo mide
en cada corrida — es el indicador de regresión de rendimiento del proyecto.

## 6. Build

- Preferido: `cmake -B build -A x64 && cmake --build build --config Release`
  → `build\Release\simtests.exe` + `simcli.exe`
- Alternativa (MinGW/g++): `./build.sh run`
- Puerta de aceptación: `simtests` exit 0 (19 pruebas con expectativa matemática)
- **board_sim (opcional)**: pasa `-DSDL2_SDK_DIR=<SDK>/x86_64-w64-mingw32`
  (SDK oficial MinGW de libsdl-org; la DLL se copia junto al exe) o instala
  `libsdl2-dev` (Linux, find_package). Sin SDL2 el target se omite con un
  aviso y el resto del proyecto es idéntico.

## 7. Board simulator (fase 2, v0.2)

**Objetivo**: ejercitar los procesadores dentro de un lazo semejante al del
hardware — cadencia fija de audio, salida por dispositivo, "SD card" de
canciones — sin flashear firmware.

**Piezas (todas SDL-free salvo el shell):**

| Pieza | Qué hace | Testable headless |
|---|---|---|
| `board_clock.hpp` | Stubs de reloj: 48 kHz, bloque hardware de 128 marcos (2.667 ms). En silicio se sustituyen por los timers DWT/SAI del M7 | sí (matemática de conversión marcos↔tiempo) |
| `SdCard` | "Tarjeta SD" = carpeta del host; escanea `*.wav` (extensión case-insensitive) ordenado por ruta; no abre los WAV (eso ocurre al seleccionar canción) | sí |
| `PadGrid` | Rejilla 8×8; pads de rol fijos: `(0,0)` PLAY/STOP, `(0,1)` BYPASS; resto libres (estado visual persistente) | sí |
| `BoardPump` | Lazo de CADENCIA FIJA: cada `pump()` entrega exactamente 1 bloque hardware (128 marcos) salvo el tramo final (resto + auto-stop). Salida intercalada 2ch lista para la cola del dispositivo | sí |
| `board_main.cpp` | Shell SDL2: ventana con grid, cola de audio (`SDL_QueueAudio`, F32 2ch @48k), eventos (teclado/ratón), playhead y medidor GR | no (solo este archivo enlaza SDL) |

**Flujo del board:**

```
SD (carpeta host) ──read_wav──► WavData (viva mientras la canción cargada)
                                    │ FileAudioSource (reusada del engine)
                                    ▼
   UI (60 fps) ──mientras cola SDL < watermark──► BoardPump.pump()
                                    │ 1 bloque hardware (128) por llamada
                                    │ guardia no-finitos → cadena IProcessor
                                    │ (o bypass directo a la salida)
                                    ▼
                        float intercalado ──SDL_QueueAudio──► dispositivo
```

**Decisiones clave del board:**

1. **Cadencia fija, no streaming por hilos**: el UI bombea a demanda hasta la
   marca de agua de cola (8 bloques ≈ 21 ms). Sin productor/consumidor por
   hilos ni locks en v0.2 — mismo contrato futuro que la interrupción de
   audio de la placa, y un contador de underruns como indicador.
2. **Reuso estricto del engine**: BoardPump NO duplica el pipeline; compone
   `FileAudioSource` + `IProcessor` + la misma guardia. El bypass salta la
   cadena sin desmontarla (bit a bit == fuente).
3. **SDL aislado**: solo `board_main.cpp` incluye SDL. `board_core` se compila
   también dentro de `simtests`, por eso las 5 pruebas nuevas del board son
   headless y matemáticas.
4. **Sample rate incompatible = rechazo accionable** (el resample es fase
   futura, documentada en TODO).

## 8. Procesadores de referencia (suite_core)

Todos los FX comparten el mismo contrato: `set_params` (clamp centralizado),
`set_sample_rate`, `prepare`, `process(float* L, float* R, n)` in-place sin
allocaciones ni excepciones, `reset`, y telemetría `current_gr_db` /
`bad_input_count`. Cada ley estática es PÚBLICA y EXACTA — es la expectativa
matemática que `simtests` verifica.

| FX | Modelo | Ley de ganancia |
|---|---|---|
| `Fet76Ref` | FET de realimentación (detector = salida de la celda, muestra previa) | curva feedback: out_peak = (in_peak + k·thresh)/(1+k), k = 1−1/R |
| `OptoCompRef` | fotocélula: detector = envolvente one-pole ASIMÉTRICA de max(\|L\|,\|R\|) | rodilla suave C1 sobre el exceso e: `e≤0: 0 · 0<e≤w: s·e²/(2w) · e>w: s·(e−w/2)`, s = 1−1/R, w = knee_db |

**OptoCompRef, decisiones documentadas:**

1. **Ballística asimétrica**: attack one-pole al subir el nivel (10 ms por
   defecto — fotocélula lenta), release al bajar.
2. **Release program-dependent** (la firma del opto): los COEFICIENTES
   one-pole interpola linealmente entre `release_slow_s` (GR→0) y
   `release_fast_s` (|GR| ≥ `gr_span_db`). Como coef ≈ 1/(fs·tau), interpolar
   coeficientes = interpolar 1/tau: la caída inicial sale rápida y la cola se
   alarga al despejar. Los tests fijan la banda medida (profundo ~170 ms vs
   somero ~70 ms a −1 dB, razón > 1.5).
3. **Ley EXACTA verificable con DC**: el detector converge exactamente a la
   entrada constante, así que la GR estacionaria es la ley evaluada — los
   tests la comprueban a 1e-3 dB en tres puntos (rodilla, juntura, lineal).
4. **Costo por muestra**: 1 `log`/muestra del detector + 1 `exp` (ganancia) +
   aritmética escalar — mismo orden que Fet76Ref.

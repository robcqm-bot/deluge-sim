# ARCHITECTURE.md — deluge-sim v0.1

> (Documento de arquitectura en español — traducciones al inglés bienvenidas.)

## 1. Architecture Summary

**Patrones usados y por qué:**

| Patrón | Dónde | Decisión |
|---|---|---|
| **Strategy** | `IProcessor` | Cada FX (Fet76Ref hoy, PLATE-140 mañana) es intercambiable sin tocar el motor |
| **Pipeline** | `Engine::run` | Render por bloques: fuente → cadena ordenada → sumidero; espejo del flujo real del firmware Deluge (blocks → FX → master) |
| **Firewall (module boundary)** | `suite_core/` vs `include/deluge_sim/` | Dos bibliotecas estáticas INDEPENDIENTES; la dependencia va en una sola dirección (`app → ambas`). Ningún header cruza la frontera. El único puente es un adaptador de ~30 líneas (`AdapterFet76` en `app/main.cpp`) |
| **Adapter** | `AdapterFet76` | Traduce `deluge_sim::Block` (tipo del sim) → punteros crudos `float*` (tipo de suite-core). En la integración real al firmware este archivo vive en `src/deluge/dsp/suite/` |
| **DTO + Report** | `RunReport` | Telemetría explícita en vez de prints dispersos |

**Decisiones clave:**
1. **Planar stereo** (`L[]`/`R[]` separados) y no intercalado: el DSP del M7 del Deluge trabaja por canal; evita mezclas de strides en suite-core.
2. **Memoria del bloque propiedad del Engine**, asignada UNA vez: cero allocaciones dentro del render (contrato obligatorio para IProcessor).
3. **suite_core no enlaza deluge_sim**: así el mismo `.cpp` compila mañana para el firmware (M7), para Teensy o como VST — la razón de ser del cortafuegos.
4. **Cero dependencias externas** en v0.1 (ni SDL ni libs): WAV I/O propio. El board-sim SDL2 interactivo es fase 2 (registrada, no implantada) — v0.1 entrega el pipeline y la zona propia VERIFICABLES hoy.

## 2. Folder Structure (exacta)

```
D:\DelugeLab\deluge-sim\
├── CMakeLists.txt                        # 4 targets (ver §5)
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
│   ├── include\suite_core\fet76_ref.hpp
│   └── src\fet76_ref.cpp
├── app\
│   └── main.cpp                          # simcli + AdapterFet76 (el puente)
└── tests\
    └── test_all.cpp                      # simtests (exit 0 = aceptación)
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
- Puerta de aceptación: `simtests` exit 0 (8 pruebas con expectativa matemática)

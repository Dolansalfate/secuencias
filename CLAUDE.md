# CLAUDE.md: contexto del proyecto Secuencias

Este archivo es para agentes de IA (Claude Code u otros) y para cualquier persona que se sume al
proyecto. Léelo completo antes de cambiar código. Mantenlo al día cuando cambie la arquitectura,
los formatos de archivo o las convenciones (sección "Bitácora" al final).

---

## 1. Qué es y para quién

**Secuencias** es una app de escritorio nativa (C++17 + JUCE 8.0.4) para **lanzar secuencias
(backing tracks multipista) en vivo**. Es para músicos que tocan con pistas: bandas, iglesias,
solistas. Incluye separación de canciones completas en stems con IA (**Demucs**, el mismo tipo de
modelo que usa Moises).

- **Plataforma principal: Ubuntu Desktop 24.04 (x86_64)** con PipeWire. Se instala en el
  usuario con `install.sh` (acceso directo en el menú e ícono).
- **Plataforma secundaria: macOS** (el mismo código compila; no romper la compatibilidad).
- **Idioma**: la interfaz, los mensajes, los comentarios y la documentación van en **español**
  (el usuario es hispanohablante, de Chile). Los identificadores del código van en inglés.
- **Prioridades**, en este orden: 1) estabilidad en vivo (nunca cortes, clics ni bloqueos del
  audio), 2) respuesta rápida y predecible a teclado o pedal, 3) liviana, 4) todo lo demás.

## 2. Funciones actuales (v0.2)

- Setlist ordenable (clic derecho: subir, bajar, renombrar, mover a la papelera).
- Mezclador por canción: fader en dB, mute, solo, medidor de pico y **salida estéreo por pista**
  (pares 1-2, 3-4…), por ejemplo click y guía a in-ears y el resto al PA. Se guarda por canción.
- Marcadores por canción (añadir con M, saltar con 1-9, clic derecho para renombrar, mover o
  eliminar) y **loop de sección** (de un marcador al siguiente).
- Click generado (4/4 fijo): BPM, inicio en segundos, volumen y salida propia.
- Fundidos de unos 6 ms al dar play, pausa o stop y al saltar: nunca hay clics.
- Separación con IA: se elige un archivo o se arrastra uno solo a la ventana. Se ejecuta
  `python -m demucs` en un hilo aparte con barra de progreso, y al terminar se importa como
  canción nueva. Modelos: `htdemucs` (4 stems), `htdemucs_6s` (6 stems), `htdemucs_ft`
  (mejor calidad, 4 veces más lento).
- Importar stems: varios archivos o una carpeta (o arrastrar varios archivos a la ventana).
- Atajos: Espacio (play/pausa), Esc (stop y volver al inicio), flechas izquierda/derecha y
  RePág/AvPág (canción anterior/siguiente; los pedales Bluetooth suelen enviar esas teclas),
  1-9 (marcador), M (añadir marcador), L (loop).

## 3. Compilar, ejecutar y probar

Dependencias en Ubuntu:
```bash
sudo apt install build-essential cmake ninja-build git pkg-config gdb \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev \
  pipewire-jack
```

Con CMake Presets (JUCE se descarga solo con FetchContent la primera vez):
```bash
cmake --preset debug                 # configura build/debug (incluye tests)
cmake --build --preset debug         # app + tests
ctest --preset debug                 # tests sin interfaz
pw-jack ./build/debug/Secuencias_artefacts/Debug/Secuencias   # ejecutar con JACK
cmake --preset release && cmake --build --preset release      # build/release (con LTO)
./install.sh                         # instala en ~/.local (usa build/release si existe)
```
Para usar una copia local de JUCE: `cmake --preset debug -DJUCE_DIR=/ruta/a/JUCE`.

En VS Code: extensiones CMake Tools + C/C++ (ver `.vscode/extensions.json`). `Ctrl+Shift+B`
compila, la tarea "Tests" corre los tests, y F5 depura (hay configuraciones con y sin JACK).

**Antes de dar por terminado un cambio**:
1. `cmake --build --preset debug` sin errores ni warnings nuevos en `Source/` o `Tests/`.
2. `ctest --preset debug` pasa.
3. Si tocaste la UI y no hay pantalla: `xvfb-run -a timeout 8 ./build/debug/Secuencias_artefacts/Debug/Secuencias`
   debe terminar por timeout (código 124), no por crash, y sin "JUCE Assertion failure" en la salida.
4. Si agregas lógica al motor o a la biblioteca, agrega un caso en `Tests/EngineTests.cpp`.

## 4. Estructura

```
CMakeLists.txt         App (juce_add_gui_app) + SecuenciasTests (juce_add_console_app, opcional)
CMakePresets.json      Presets debug / release / tests (Ninja, build/<preset>)
Source/
  Main.cpp             JUCEApplication + ventana principal
  MainComponent.h/.cpp Toda la UI: barra superior, setlist, transporte, marcadores, click,
                       mezclador (ChannelStrip y MarkerButton están definidos en el .cpp)
  AudioEngine.h/.cpp   Motor de reproducción (AudioIODeviceCallback)
  Library.h/.cpp       Modelo de datos + persistencia (setlist.json, song.json)
  Separator.h/.cpp     Hilo que convierte a WAV y ejecuta Demucs
Tests/EngineTests.cpp  Tests sin dispositivo: llaman al callback de audio a mano
linux/                 .desktop (plantilla con @EXEC@) e ícono SVG
install.sh / uninstall.sh   Instalación por usuario (~/.local/bin, ~/.local/share/applications)
.vscode/               Tareas, depuración, ajustes
LEEME.md               Guía para el usuario final
```

## 5. Arquitectura

### 5.1 Hilos
| Hilo | Qué hace | Reglas |
|---|---|---|
| **Audio** (dispositivo) | `AudioEngine::audioDeviceIOCallbackWithContext` | **Tiempo real**: sin memoria dinámica, sin locks bloqueantes, sin E/S, sin logs. Solo atómicos y `SpinLock::ScopedTryLockType`. |
| **Mensajes (UI)** | Toda la UI; `Timer` a 30 Hz que refresca tiempo, medidores y estado del separador | Nunca espera al hilo de audio. |
| **Carga** | `juce::ThreadPool loaderPool {1}`: `AudioEngine::loadSong` lee y remuestrea los stems a RAM | Cada carga lleva un número de generación (`loadGeneration`); las cargas viejas se abortan o descartan. El resultado vuelve a la UI con `MessageManager::callAsync` + `SafePointer`. |
| **Separador** | `Separator` (`juce::Thread`) ejecuta Demucs con `ChildProcess` | La UI consulta `getState()`, `getProgress()` y `getMessage()` desde el Timer. Sin callbacks cruzados. |

### 5.2 AudioEngine
- `LoadedSong` contiene `LoadedTrack`s. Cada pista es un `AudioBuffer<float>` **estéreo, a la
  frecuencia del dispositivo, del mismo largo que la canción** (se rellenan con ceros al cargar),
  así el callback nunca lee fuera del buffer. Los mono se duplican y el remuestreo usa
  `LagrangeInterpolator`.
- Controles por pista como atómicos (`gain`, `muted`, `solo`, `outputPair`); `meter` guarda el
  pico y la UI lo lee con `exchange(0)`. `smoothedGain` hace una rampa lineal por bloque (solo
  lo toca el hilo de audio).
- `setSong()` cambia el `shared_ptr` bajo `songLock` (SpinLock). El callback usa
  `ScopedTryLock` y, si no lo obtiene, entrega silencio en ese bloque. La canción anterior se
  libera **fuera** del lock y en el hilo de UI.
- Transporte: `playing`, `position`, `pendingSeek` (-1 = ninguno), `loopStart/loopEnd` (loop
  activo si `end > start`). Toda búsqueda pasa por `pendingSeek`: el callback funde a 0, salta y
  vuelve a subir. `getPositionSeconds()` devuelve `pendingSeek` si hay uno pendiente, para que
  la UI no parpadee.
- `renderChunk` trabaja en trozos de hasta 2048 muestras. Primero calcula la posición y la
  envolvente de cada muestra (`positions[]`, `envelope[]`, prealocados), después mezcla cada
  pista y al final genera el click.
- Ruteo: `outputPairChannels(pair)` apunta a los canales `2*pair`, `2*pair+1` **entre los canales
  activos** del dispositivo (JUCE los compacta). Si el par no existe, cae a 1-2. Con salida mono
  suma L+R×0,5.
- Fin de la canción: `playing = false` y la posición queda al final; `play()` vuelve a 0.
- `audioDeviceAboutToStart` actualiza la frecuencia y la cantidad de salidas; si la frecuencia
  cambió, pone `needsReload`, y la UI recarga la canción actual en el siguiente tick.

### 5.3 Library (formato en disco)
Carpeta raíz: `Library::defaultRoot()` = carpeta de Música del usuario (XDG; en Ubuntu en
español es `~/Música`) + `/Secuencias`. Los tests usan una carpeta temporal.

```
Secuencias/
  setlist.json            ["Carpeta Canción 1", "Carpeta Canción 2", ...]  (orden del setlist)
  <Canción>/
    song.json
    drums.wav, bass.wav, ...   (cualquier wav/aif/aiff/flac/mp3/m4a/ogg)
```
`song.json`:
```json
{
  "name": "Nombre visible",
  "bpm": 120.0, "clickOffset": 0.0, "clickEnabled": false,
  "clickGainDb": -6.0, "clickOutputPair": 0,
  "markers": [ { "name": "Coro", "seconds": 42.5 } ],
  "stems":   [ { "name": "Voces", "file": "vocals.wav", "gainDb": 0.0, "muted": false, "outputPair": 0 } ]
}
```
- `load()`: primero las carpetas en el orden de `setlist.json`, después agrega al final las
  carpetas con audio que no estén en la lista. Por eso **borrar una canción = mover su carpeta
  a la papelera** (si solo se sacara de la lista, volvería a aparecer). En Linux se usa
  `gio trash`, y `File::moveToTrash` como respaldo.
- Si en la carpeta aparecen archivos de audio que no están en `song.json`, se agregan como
  stems. Los nombres de Demucs se traducen (`vocals` → "Voces", etc.).
- Los marcadores se mantienen ordenados (`SongInfo::sortMarkers()`); la UI depende de eso.
- **Compatibilidad**: si cambias el esquema, lee los campos viejos con valores por defecto
  (`getProperty(name, default)`); no rompas las bibliotecas existentes.

Ajustes de la app (`juce::ApplicationProperties`): en Linux `~/.config/Secuencias/Secuencias.settings`,
en macOS `~/Library/Application Support/Secuencias/`. Claves: `audioDevice` (XML de
`AudioDeviceManager`), `pythonPath`, `model`.

### 5.4 Separator (Demucs)
1. Convierte la entrada a `input.wav` de 24 bits en `/tmp/secuencias_separacion/trabajoN/`
   (así Demucs no necesita ffmpeg).
2. Ejecuta `<python> -u -m demucs -n <modelo> -o <trabajo>/out <trabajo>/input.wav`.
3. Lee stdout y stderr, se queda solo con el ASCII y toma el último `NN%` de la barra de tqdm
   como progreso (`htdemucs_ft` reinicia la barra 4 veces).
4. El resultado queda en `out/<modelo>/input/*.wav`. La UI lo importa con
   `importStemFolder(folder, nombre)` y llama a `reset()`, que borra los temporales.
- Python por defecto: `~/demucs-env/bin/python` (venv). Instalación recomendada en Ubuntu:
  `pip install "torch<2.9" "torchaudio<2.9" --index-url https://download.pytorch.org/whl/cpu`
  y luego `pip install demucs soundfile`. **Hay que fijar `torchaudio<2.9`**: desde 2.9,
  load/save dependen de torchcodec y Demucs 4.0.1 falla al guardar.
- Cancelar: `cancel()` hace `kill()` del proceso (el puntero está protegido por `processLock`).

### 5.5 UI (MainComponent)
- **Foco de teclado**: ningún hijo acepta foco (`disableFocus()` recursivo, y también en los
  strips y marcadores que se crean después). Así todas las teclas llegan a
  `MainComponent::keyPressed`. El Timer recupera el foco si no hay ventanas modales. Si
  agregas un control nuevo, ponle `setWantsKeyboardFocus(false)`, salvo los TextEditor dentro
  de diálogos modales.
- El setlist es un `ListBox` con `MainComponent` como `ListBoxModel`. Cargar una canción =
  `setlist.selectRow(i)` → `selectedRowsChanged` → `loadSongAt`.
- `saveCurrentMix()` copia el estado de las pistas cargadas a `SongInfo` y lo guarda. Se llama
  al cambiar de canción y al cerrar. Los cambios de click y marcadores se guardan al instante.
- Diálogos: siempre asíncronos (`AlertWindow` + `enterModalState(..., deleteWhenDismissed=true)`,
  `showMessageBoxAsync`, `FileChooser::launchAsync`). Nunca uses modales bloqueantes.
- En Linux el `FileChooser` de importar stems usa el selector de JUCE (no el nativo), porque
  zenity no permite elegir archivos y carpetas a la vez.

## 6. Convenciones y trampas conocidas

- **Texto con tildes, ñ o símbolos**: siempre con `tr("...")` (definido en `Library.h`, equivale a
  `String::fromUTF8`). `juce::String("canción")` con un `const char*` no ASCII **dispara un
  assert** en Debug. Los textos ASCII pueden ir directos.
- **Nada de símbolos Unicode en botones** (▶ ■ ✓ ←…): muchas fuentes de Ubuntu no los tienen y
  se ven como cuadrados. Usa texto ("Play", "Stop", "<<") o dibújalos con `Path`.
- Fuentes: `juce::Font (juce::FontOptions (h, style))` (el constructor `Font(float)` está
  deprecado en JUCE 8). En el .cpp está el helper `font(h, bold)`.
- Listas de componentes en `for`: `std::initializer_list<juce::Component*> { &a, &b }`. Sin el
  tipo explícito no compila en GCC.
- Si una subclase sobreescribe `Button::clicked(const ModifierKeys&)`, agrega
  `using juce::TextButton::clicked;` para evitar `-Woverloaded-virtual`.
- API de JUCE 8.0.4: `AudioFormat::createWriterFor(OutputStream*, sr, channels, bits, {}, 0)`
  (firma clásica; después de llamarla, hacer `release()` del stream), y
  `AudioFormatWriter::writeFromAudioSampleBuffer`. Si subes la versión de JUCE, revisa
  `BREAKING_CHANGES.md`.
- Hilo de audio: nada de `new`, `std::vector::push_back`, `String`, `DBG`, archivos ni locks
  bloqueantes. Si necesitas pasar datos, usa atómicos o una cola lock-free prealocada.
- Parámetros que cambian en vivo: siempre con rampa o fundido (ver `smoothedGain` y `fade`).
- El estilo del código es parecido al de JUCE (llaves en línea propia, espacio antes de `(`,
  4 espacios). Hay un `.clang-format`, pero no reformatees archivos completos sin motivo.
- No agregues dependencias de sistema nuevas sin actualizar el README, `install.sh` y esta guía.
  El binario de release hoy solo necesita libasound, fontconfig, freetype y libstdc++.

## 7. Limitaciones conocidas

- Todo el audio va a RAM: unos 23 MB por minuto por stem a 48 kHz (5 min × 8 stems ≈ 1 GB).
- Sin cambio de tono ni de tempo.
- Click en 4/4 fijo; sin cuenta regresiva (count-in).
- Sin MIDI (solo teclas; los pedales que emulan teclado funcionan).
- Al cambiar de canción se detiene la reproducción; no hay avance automático ni crossfade.
- Si se arrastra a la ventana un solo archivo, siempre se separa (no pregunta).
- Con JACK, las conexiones las decide PipeWire/JACK; la app no conecta puertos por nombre.
- Probado con tests y en Xvfb (sin tarjeta de sonido real); falta probar con interfaces
  multicanal reales.

## 8. Hoja de ruta sugerida (en orden de valor para tocar en vivo)

1. **Probar con hardware real** y ajustar lo que haga falta (latencia, auto-conexión de JACK,
   nombres de salidas en los ComboBox usando `getOutputChannelNames()`).
2. **Control MIDI**: `AudioDeviceManager::addMidiInputDeviceCallback` + "MIDI learn" para
   play, stop, siguiente/anterior, marcadores y loop. Los mensajes MIDI llegan en otro hilo:
   pásalos a la UI con `callAsync` o una cola.
3. **Streaming desde disco** en vez de RAM (por ejemplo `BufferingAudioReader` por pista con un
   hilo de lectura compartido, o mmap de WAV). Mantener la carga instantánea al saltar.
4. **Transposición y tempo** con Rubber Band (licencia GPL o comercial; revisar) o SoundTouch
   (LGPL), procesando fuera del hilo de audio o en tiempo real con buffers prealocados.
5. Compás configurable, count-in y acentos; tap tempo; detección de BPM tras separar
   (por ejemplo con `librosa` en el mismo venv).
6. Saltos cuantizados ("ir al coro al final del compás") y cola de secciones.
7. Avance automático de canción, pausa entre canciones y varios setlists (guardar/abrir).
8. Forma de onda en la barra de posición (`AudioThumbnail`) con los marcadores dibujados.
9. Separación integrada sin Python (demucs.cpp u ONNX Runtime) como opción avanzada.
10. Paquete `.deb` o AppImage (con CPack) y `.app` firmado en macOS.

## 9. Bitácora

- **v0.1**: primera versión para macOS: motor, setlist, mezclador, marcadores, click, Demucs.
- **v0.2**: Ubuntu como plataforma principal: JACK/ALSA, `install.sh`, `.desktop`, papelera con
  `gio trash`, selector de archivos de JUCE en Linux, textos ASCII en los botones, ajustes en
  `~/.config/Secuencias`, `Library` acepta una carpeta raíz, tests con CTest, CMake Presets y
  configuración de VS Code.

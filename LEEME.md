# Secuencias v0.2 (Ubuntu Desktop)

App nativa (C++ / JUCE) para lanzar secuencias en vivo, con separación de pistas por
IA (Demucs, el mismo tipo de modelo que usa Moises). Probada en Ubuntu 24.04.
El mismo código sigue compilando en macOS.

## Qué hace

- **Setlist** con todas tus canciones (se guardan en `~/Music/Secuencias`, o `~/Música/Secuencias` si tu sistema está en español).
- **Mezclador** por canción: volumen, mute, solo, medidor y **salida por pista**
  (por ejemplo, click y guía a la salida 3-4 para in-ears, el resto a 1-2 para el PA).
- **Marcadores** (intro, verso, coro…) para saltar en vivo, y **loop de sección**.
- **Click generado** con BPM, inicio, volumen y salida propia.
- **Separar canción (IA)**: separa una canción completa en 4 o 6 stems y la agrega al setlist.
- Fundidos automáticos al dar play, stop o saltar (sin clics).

## Atajos (sirven con pedales tipo teclado)

| Tecla | Acción |
|---|---|
| Espacio | Play / pausa |
| Esc | Stop (vuelve al inicio) |
| Flecha derecha o AvPág | Siguiente canción |
| Flecha izquierda o RePág | Canción anterior |
| 1 – 9 | Ir al marcador |
| M | Añadir marcador |
| L | Loop de sección on/off |

Clic derecho sobre una canción o un marcador para más opciones.
Arrastrar a la ventana: **1 archivo** = separar con IA, **varios archivos o una carpeta** = importar stems.

---

## 1. Dependencias (una vez)

```bash
sudo apt install build-essential cmake ninja-build git pkg-config gdb \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev \
  pipewire-jack
```

## 2. Abrir en VS Code o instalar

**Para desarrollar**: abre la carpeta en VS Code (`code .`) e instala las extensiones
recomendadas (CMake Tools, C/C++ y Claude Code). `Ctrl+Shift+B` compila y F5 depura.
Los detalles para agentes de IA están en `CLAUDE.md`.

**Para instalarla en el menú de aplicaciones**: `./install.sh` (compila en modo release la
primera vez). Para desinstalar: `./uninstall.sh` (tus canciones no se borran).

## 3. Instalar Demucs para separar canciones (una vez)

```bash
sudo apt install python3-venv
python3 -m venv ~/demucs-env

# Sin tarjeta NVIDIA (descarga más liviana, solo CPU):
~/demucs-env/bin/pip install "torch<2.9" "torchaudio<2.9" --index-url https://download.pytorch.org/whl/cpu
~/demucs-env/bin/pip install demucs soundfile

# Con tarjeta NVIDIA (usa la GPU, mucho más rápido), en vez de lo anterior:
# ~/demucs-env/bin/pip install demucs soundfile "torchaudio<2.9"
```

La app busca Python en `~/demucs-env/bin/python`; si lo instalaste en otro lugar,
cámbialo en **Ajustes IA**. La primera separación descarga el modelo (unos 80 MB).
En CPU, una canción de 4 minutos tarda de 2 a 6 minutos según el procesador; con GPU
NVIDIA, menos de un minuto.

## 4. Configurar el audio

Ubuntu usa PipeWire. Tienes dos opciones en el botón **Audio**:

- **JACK (recomendado para tocar en vivo)**: el acceso directo del menú ya abre la app con
  `pw-jack`. Elige el tipo "JACK", activa las salidas que quieras (1-2, 3-4…) y ajusta el
  tamaño de buffer (256 o 128 muestras es un buen punto de partida). Para revisar o cambiar
  conexiones puedes usar `qpwgraph` (`sudo apt install qpwgraph`).
- **ALSA**: el dispositivo "default" funciona para ensayar, pero solo con 2 salidas y más latencia.

Con una interfaz de varias salidas, asegúrate en la configuración de sonido de Ubuntu de
que el perfil de la interfaz sea "Pro Audio", así PipeWire expone todos los canales.

---

## Compilar desde la terminal

```bash
cmake --preset debug && cmake --build --preset debug   # app + tests
ctest --preset debug                                     # tests
cmake --preset release && cmake --build --preset release
```

## Estructura del código

| Archivo | Qué contiene |
|---|---|
| `AudioEngine` | Motor: mezcla, fundidos, loop, click, ruteo de salidas |
| `Library` | Setlist y canciones en disco (`song.json` por canción) |
| `Separator` | Ejecuta Demucs en segundo plano y lee el progreso |
| `MainComponent` | Interfaz, atajos, arrastrar y soltar |
| `Tests/` | Tests del motor y la biblioteca (sin tarjeta de sonido) |
| `install.sh`, `linux/` | Instalación en Ubuntu, acceso directo e ícono |

## Límites de esta versión

- Las pistas se cargan completas en RAM (unos 23 MB por minuto por stem a 48 kHz).
- Sin cambio de tono ni de tempo todavía.
- Click en 4/4 fijo.
- Sin control MIDI todavía (los pedales que envían teclas sí funcionan).

## Licencia de JUCE

JUCE es gratis para uso personal y proyectos con ingresos bajo cierto límite
(ver juce.com/get-juce). Si algún día vendes la app, revisa su licencia.

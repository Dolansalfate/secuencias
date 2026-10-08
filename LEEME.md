# Secuencias v0.3 (Ubuntu Desktop)

App nativa (C++ / JUCE) para lanzar secuencias en vivo, con separación de pistas por
IA (Demucs, el mismo tipo de modelo que usa Moises). Probada en Ubuntu 24.04.
El mismo código sigue compilando en macOS.

## Qué hace

- **Setlist** con todas tus canciones (se guardan en `~/Music/Secuencias`, o `~/Música/Secuencias` si tu sistema está en español).
- **Vista de arreglo** tipo DAW: una pista horizontal por stem con su forma de onda, regla
  con compases, tiempos y marcadores, cabezal y loop. Clic para saltar, Ctrl+rueda para hacer
  zoom, rueda para desplazarte, "Ajustar" para ver la canción entera. Los marcadores se
  arrastran en la regla.
- **Mezclador** por canción: fader en dB, mute, solo, medidor estéreo con pico, RMS y clip,
  fader maestro y **salida por pista** (por ejemplo, click y guía a la salida 3-4 para in-ears,
  el resto a 1-2 para el PA).
- **Modo en vivo** (botón "En vivo" o F11): esconde la vista de arreglo y agranda el título,
  la sección y el tiempo para leerlos desde lejos.
- **Guía de escenario** (botón "Pantalla" o F12): una ventana aparte para poner en una segunda
  pantalla, de cara a los músicos: el acorde actual y el siguiente grandes al centro, el compás
  y el tiempo en que va (con puntos que marcan el pulso), el tempo, la tonalidad, la sección
  actual y la siguiente, y el tiempo. Doble clic sobre ella (o F11) la pone a pantalla
  completa; las teclas del pedal siguen funcionando aunque esa ventana tenga el foco.
- **Triggers con tus propios sonidos**: botón "T" en la cabecera de una pista (por ejemplo el
  bombo separado): sus golpes disparan un banco de muestras, con timing exacto y sin latencia,
  porque los golpes se detectan de antemano sobre el audio. El banco es una carpeta con wav
  (de suave a fuerte; se importan desde el menú del "T" y pronto se podrán grabar desde la
  app): cada golpe elige la muestra según su fuerza. El sampler aparece como un canal más del
  mezclador ("Bombo (muestras)"), con fader y salida propia. En el canal del bombo, bajo el
  fader, una casilla dice qué suena: "Audio" (la pista tal cual), "Trig: <banco>" (solo tu
  sonido: la pista original se silencia) o "Trig+Audio" (ambas, para reforzar). Un clic en esa
  casilla (o en el "T") abre el menú: el modo, el sonido (banco grabado o, cuando cargues uno,
  un instrumento VST/AU con la nota MIDI que quieras, por ejemplo 36 = bombo), y el umbral,
  sensibilidad y tiempo mínimo de la detección. Las marcas naranjas al pie del carril
  muestran los golpes detectados y su fuerza.
- **Pistas MIDI (corregir a mano los golpes)**: en el menú del trigger de una pista (el "T" o la
  casilla del trigger en el mezclador), "Crear pista MIDI con estos golpes". Aparece bajo esa pista
  una pista con los golpes detectados, como un editor de batería: cada fila es un sonido (un banco
  de muestras tuyas o un instrumento como Addictive Drums, con su nota). Así corriges lo que separó
  la IA: por ejemplo, en la pista de toms eliges los golpes que en realidad son congas y con clic
  derecho > "Pasar a la fila" los mandas a una fila "Conga" con su propio sonido (clic derecho en el
  título > "Agregar una fila"). Clic elige un golpe (Ctrl o Shift para varios, o arrastra en un hueco
  para elegir con un rectángulo), arrastrar lo mueve, Alt + arrastre hacia arriba o abajo cambia su
  fuerza, doble clic agrega uno, Supr borra; el menú del clic derecho también cuantiza y vuelve a
  detectar un tramo. Mientras la pista MIDI suena, la pista original se calla (se puede cambiar en el
  menú del título). Ctrl+Z deshace. El triángulo a la izquierda de cada fila toca un golpe de esa
  fila, también con la canción detenida. Con Addictive Drums sirve para enlazar una pieza sin buscar
  números: en su ventana MIDI Mapping aprieta "Learn" en la pieza (por ejemplo la conga de un Flexi)
  y luego el triángulo de la fila.
- **Grabar** (botón junto al transporte): dos destinos en el mismo diálogo. **Pista nueva**:
  graba una entrada de tu interfaz como pista de la canción, que se suma a la mezcla. **Banco de
  muestras**: grabas golpes sueltos (de suave a fuerte, con silencio entre ellos) que no quedan
  como pista sino como sonido para el trigger: cada golpe se recorta solo justo en su ataque, y
  al dispararse su transiente cae exactamente sobre la del golpe original, sin retardo. Eliges la
  entrada (mono o un par estéreo), el nombre, por qué salida quieres
  escucharte y, si hace falta, una compensación extra en milisegundos. La canción arranca desde
  el cabezal; al detener (el mismo botón, Stop o el final de la canción) la toma se agrega como
  una pista más, ya alineada con la canción (la app descuenta la latencia de la interfaz). Si tu
  interfaz no tenía entradas activas, la app activa las dos primeras; en "Audio" puedes elegir
  otras. Para grabar hay que estar en el tempo y tono originales. Para **grabar tu propio bombo
  para el trigger**: en el menú del trigger de la pista, "Sonido" > "Grabar golpes como banco
  nuevo...": toca golpes sueltos de suave a fuerte con silencio entre ellos y detén; cada golpe
  queda como una muestra del banco y el trigger de esa pista pasa a usarlo.
- **Armar mix (antes de separar)**: botón "Armar mix" > "Mix nuevo...". Con "+ Canción" cargas las
  canciones originales completas; la app las analiza sola (tiempos y compases, con la IA de
  análisis). Eliges una canción en la lista, arrastras sobre su onda para seleccionar un tramo
  (se ajusta a los compases y al golpe) y "Agregar al mix"; repites con otras canciones. Para
  afinar el tramo, arrastra solo su inicio o su fin (por la línea o por el asa de arriba), usa
  Shift + clic para llevar ahí el borde más cercano, o escribe los compases arriba a la derecha
  ("Compases [8] a [16]": un clic en el número, escribes y Enter). Un clic suelto solo mueve la
  marca blanca desde donde suena "Escuchar"; el clic derecho ofrece "Inicio del tramo aquí" y
  "Fin del tramo aquí". Arriba, "Nivelar a -14 LUFS" (o -12, -16, -18, o "Sin nivelar") deja cada
  tramo a la misma sonoridad, así un tema no suena más fuerte que el otro en el mix; los mixes nuevos
  empiezan en -14. Un tramo nunca se sube tanto que sus picos pasen de -1 dBTP: si una grabación
  antigua tiene picos altos, queda algo más baja (el panel del tramo dice "Niv +x dB (tope)" y el
  pie avisa cuántos tramos quedaron así). La ganancia de cada tramo se suma encima. Abajo ves
  los tramos en orden: cada uno se ajusta al tempo del mix de modo que las uniones caen exactas en
  el pulso (puedes elegir "Cada tramo a su tempo" o darle a un tramo su propio tempo). Por tramo
  puedes moverlo, quitarlo, alargarlo o acortarlo por compases, cambiarle el tono, la ganancia y el
  fundido con el anterior (corte o 1-2 tiempos o compases). "Escuchar el mix" o "Escuchar la unión"
  (con "Click" para comprobar el pulso). Ctrl+Z deshace. Al final, "Crear canción...": separarla con
  IA (con las opciones de pistas y calidad de arriba) o agregarla sin separar; la canción aparece en
  el setlist con sus tiempos, compases, acordes, secciones de tempo y un marcador en cada tramo, sin
  tener que analizarla de nuevo. El mix queda guardado para seguir otro día ("Armar mix" > su nombre).
- **Instrumentos VST3 / AU (por ejemplo Addictive Drums)**: botón "Instrumentos" arriba. La primera
  vez, "Buscar instrumentos instalados" (recorre las carpetas habituales de VST3, y de AU en Mac;
  si tienes plugins en otra carpeta, "Buscar en una carpeta..."). Después, "Añadir instrumento" y
  eliges uno: aparece como un canal lila en el mezclador, con su fader, salida y medidor; un clic
  en su nombre abre la ventana del plugin para elegir el kit y ajustarlo. Todo eso se recuerda
  entre sesiones. Para que una pista lo dispare: en la casilla del trigger de esa pista (o el "T"),
  "Sonido" > el instrumento, y en "Nota MIDI" la pieza que quieres: de la lista General MIDI (36
  bombo, 38 caja...) o, en el submenú "Addictive Drums 2", de su mapa "AD2 Standard", el que trae
  por defecto (hi-hat, toms, platillos y los Flexi, donde cada kit pone su percusión). Si en
  Addictive Drums eliges el mapa General MIDI (ventana MIDI Mapping), usa la lista General MIDI.
  "Escuchar" en el mismo menú toca un golpe para comprobarlo. Cada golpe detectado llega al plugin como
  una nota con la fuerza del golpe, adelantada la latencia del plugin, así suena exactamente en el
  golpe. Con "Es trigger" la pista original se silencia; con "Trigger y audio" suenan las dos. Un
  mismo instrumento puede recibir varias pistas (bombo a la nota 36, caja a la 38...). Los
  instrumentos son globales: sirven para todas las canciones del setlist. Solo se cargan
  instrumentos (no efectos) y se usa su salida estéreo principal.
- **Notas de texto** ("+ Nota" o tecla N, o clic derecho en la fila de notas de la regla):
  un comentario anclado a un punto de la canción, por ejemplo "corte de batería, entra solo
  el bajo" en el compás 111, con los segundos que debe quedarse visible. En la guía de
  escenario aparece como un letrero amarillo grande cuando llega ese momento, y unos segundos
  antes se anuncia como "próxima nota".
- **Marcadores** (intro, verso, coro…) para saltar en vivo, y **loop de sección**.
- **Click generado** con BPM, inicio, volumen y salida propia; en el mezclador aparece como un
  canal más ("CLICK", con fader, medidor, encendido y salida), así puedes mandarlo a los
  in-ears por otro par de salidas. Tras analizar la canción, el click sigue los tiempos
  detectados, aunque el tempo varíe. Las salidas disponibles son las del dispositivo elegido
  en "Audio": con una interfaz multicanal aparecen los pares 3-4, 5-6, etc.
- **Tempo y tono**: cambia el BPM sin que cambie la afinación, y transpone en semitonos si lo
  quieres. Tras analizar, la regla muestra una fila con las **secciones de tempo** detectadas
  (por ejemplo, cada canción de un mix con su BPM); con un clic en una sección puedes darle su
  propio tempo, igualar todas las secciones a un mismo BPM, dividirla o unirla, o corregir el
  tempo detectado, o darle a esa sección su propio tono en semitonos ("Tono de la sección...",
  por ejemplo para subir medio tono solo un tema del mix). El campo "Tempo" es el BPM al que
  suenan todas las secciones y "Tono" el de toda la canción, salvo las secciones con tono propio. El cambio se
  prepara en segundo plano (unos 40 s para 6 pistas de 7 minutos) y entra sin cortar el audio.
- **Nivelar volumen**: mide la sonoridad real (LUFS, como Spotify) de cada pista y de la
  mezcla en cada tramo entre marcadores. Primero empareja cada pista consigo misma a lo largo
  de la canción (la batería del segundo tema al nivel de la del primero, y así con cada stem,
  sin mover los faders; tope 12 dB) y luego sube o baja la mezcla de cada tramo para que
  todos suenen parejos, sin distorsionar. En el mezclador, debajo de cada fader, "Niv +x dB"
  muestra la ganancia de esa pista en el tramo que está sonando; un clic permite escribir
  otro valor y cambia el tramo completo. Ideal para mezclas de varias canciones pegadas: pon
  un marcador donde empieza cada tema (o usa "Crear marcadores en los cambios de tempo" en la
  banda de tempo); sin marcadores toda la canción es un solo tramo. Al mover o añadir
  marcadores se vuelve a medir solo (y se reemplazan los valores escritos a mano). "Nivelar setlist" hace lo mismo entre canciones. El objetivo se elige al lado del
  botón; con clic derecho en un marcador se retoca su ganancia. Un tramo muy bajo pero con
  picos altos no se puede subir del todo (tope -1 dBTP): baja el objetivo para que sean los
  fuertes los que bajen.
- **Análisis musical (IA)**: tempo, compases, acordes y tonalidad de cada canción. Los acordes
  se ven en la regla y, en grande, en el modo en vivo con el acorde siguiente; cada sección
  muestra su BPM junto al marcador.
- **Enarmonías y métrica**: clic en la tonalidad (arriba a la derecha) para escribir las notas
  con sostenidos (Re#) o con bemoles (Mib), o para fijar la tonalidad a mano si la detectada
  no es la correcta; los acordes se reescriben igual. Clic derecho sobre un tiempo en la fila
  de compases de la regla (acerca el zoom hasta ver los tiempos) para marcarlo como primer
  tiempo de compás, definir compases de 2 a 7 tiempos desde ahí, hacer un compás corto de 1 a
  3 tiempos solo en ese punto (con su "1" acentuado en el click), o quitar e insertar tiempos:
  así un enganche entre canciones de un mix puede tener un compás más corto y la siguiente
  entra en su "1". El click sigue esos cambios. En la banda de tempo de cada sección,
  "Click de esta sección" corrige un click que suena en corcheas o en blancas (tiempos a la
  mitad o al doble), pone una rejilla fija al tempo de la sección (un cambio de tempo manual),
  reparte tiempos parejos que caben justos hasta el siguiente compás (para las transiciones de
  un mix donde el análisis se enreda) o define compases de N tiempos. Clic derecho en la fila de acordes para cambiar, quitar,
  dividir, unir o añadir acordes.
- **Cortar y mover el audio** sin tocar la grilla: clic derecho sobre un carril para cortar
  los stems en ese punto (siempre todos a la vez, así nunca se desalinean), desplazar el
  tramo en milisegundos, alinear su inicio al tiempo más cercano, unir, eliminar (dejando
  silencio o cerrando el hueco) o restaurar el audio original. También copiar un tramo y
  pegarlo en otro punto insertando (se abre espacio y todo lo que sigue, audio y grilla, se
  corre; la copia lleva sus tiempos y acordes) o encima, y duplicarlo a continuación: así se
  reordena una canción o se repite un coro. Shift + arrastrar sobre un carril mueve el tramo
  a mano. Ctrl+Z deshace la última edición. El selector junto a
  "+ Marcador" elige cómo corta: libre, a la rejilla (al tiempo más cercano) o a la
  transiente (al golpe más cercano de la pista donde haces clic; usa la batería, que marca
  mejor el inicio de cada tiempo). Con el mismo modo, al arrastrar un tramo su inicio o su
  primer golpe encajan en el tiempo más cercano.
- **Separar canción (IA)**: separa la canción seleccionada (sus pistas se reemplazan por los
  stems y se conservan marcadores, análisis, tempo y cortes; el audio anterior queda en la
  subcarpeta "original") o un archivo que elijas, que se agrega al setlist. Con la casilla
  "Batería en partes", la batería sale además dividida en bombo, caja, toms, hi-hat, ride y
  crash; y en una canción ya separada, el mismo botón ofrece "Solo la batería en partes".
  Bombo y caja salen bien; toms, hi-hat y platillos se mezclan más entre sí. Requiere la
  instalación opcional de audio-separator ("Ajustes IA" > "Instalar también Roformer y
  DrumSep"). La separación normal
  con tres niveles de calidad. En calidad alta y máxima las voces se sacan con Roformer, y
  la pista "otros" es exactamente lo que falta para reconstruir la mezcla original.
- Fundidos automáticos al dar play, stop, saltar, cerrar el loop y cambiar de canción (sin clics).
- Formatos de stems: wav, aiff, flac, mp3 y ogg (m4a solo en macOS).

## Llevar canciones a otro equipo

La carpeta de cada canción (dentro de `~/Música/Secuencias`) es el proyecto completo: los stems
y un archivo `song.json` con la mezcla, marcadores, análisis, tempo, cortes y nivelado. Con
clic derecho en una canción del setlist, "Exportar canción..." copia esa carpeta donde elijas
(un pendrive, por ejemplo) y "Exportar todo el setlist..." copia todas más el orden. En el otro
equipo, "Importar stems" y elegir la carpeta de la canción (o arrastrarla a la ventana) la
levanta con todo. También sirve copiar la carpeta `Secuencias` entera a `~/Música`.

## Atajos (sirven con pedales tipo teclado)

| Tecla | Acción |
|---|---|
| Espacio | Play / pausa |
| Esc | Stop (vuelve al inicio) |
| Flecha derecha o AvPág | Siguiente canción |
| Flecha izquierda o RePág | Canción anterior |
| 1 – 9 (también en el teclado numérico) | Ir al marcador |
| M | Añadir marcador |
| L | Loop de sección on/off |
| F11 | Modo en vivo on/off |

Clic derecho sobre una canción o un marcador para más opciones (el clic derecho no cambia la
canción que suena). Mantener una tecla o el pedal pisado no repite la acción. Si cierras la
ventana con una canción sonando, la app pide confirmación.
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

**Instaladores listos** (sin compilar): en la página de Releases del repositorio de GitHub hay,
para cada versión, un `.deb` para Ubuntu (`sudo apt install ./secuencias_<versión>_amd64.deb`,
queda en el menú), un `.dmg` para macOS y un `Secuencias-<versión>-windows-setup.exe` para
Windows. Los arma GitHub Actions en cada versión.

**En Windows**: ejecuta el `-windows-setup.exe`. Como no está firmado, SmartScreen muestra
"Windows protegió tu PC": pulsa "Más información" y "Ejecutar de todas formas". Se instala en
tu usuario sin pedir administrador y queda en el menú Inicio. El audio usa WASAPI (elige la
interfaz y sus salidas en el botón "Audio"). Para separar y analizar canciones necesitas Python
3.10 a 3.12 de python.org (marca "Add python.exe to PATH" al instalarlo) y luego, en la app,
"Ajustes IA" > "Instalar motores de IA" (o el acceso "Instalar motores de IA" del menú Inicio):
abre PowerShell, instala Demucs con PyTorch (con CUDA si detecta tarjeta NVIDIA) y madmom
precompilado que viene dentro del instalador.

**En macOS**: abre el DMG y arrastra Secuencias a Aplicaciones. La app no está firmada con una
cuenta de desarrollador, así que la primera vez macOS avisa: en macOS 13 y 14, clic derecho
sobre la app y "Abrir"; en macOS 15 o posterior, intenta abrirla y luego en Ajustes del Sistema,
Privacidad y seguridad, pulsa "Abrir de todos modos". Solo una vez por versión. Sirve en Mac
Intel y con chip Apple. Para separar y analizar canciones, instala los motores de IA desde la
app: "Ajustes IA" > "Instalar motores de IA" (abre la Terminal, tarda unos minutos y descarga
varios GB; en Mac Intel usa PyTorch 2.2.2, el último disponible para esa arquitectura).

## 3. Instalar Demucs para separar canciones (una vez)

Lo más simple: en la app, **"Ajustes IA" > "Instalar motores de IA"** abre una terminal y crea
los dos entornos (Demucs y madmom) con la receta de abajo, eligiendo la versión de PyTorch según
el sistema y la tarjeta de video. Es lo mismo que ejecutar `scripts/instalar-ia.sh`
(`--roformer` añade audio-separator). Si prefieres hacerlo a mano:

```bash
sudo apt install python3-venv ffmpeg
python3 -m venv ~/demucs-env

# Con tarjeta NVIDIA (usa la GPU; requiere el driver propietario, nvidia-smi debe responder).
# El índice cu126 sirve para tarjetas viejas y nuevas. Sin tarjeta NVIDIA, cambia cu126 por cpu:
~/demucs-env/bin/pip install torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0 --index-url https://download.pytorch.org/whl/cu126
~/demucs-env/bin/pip install demucs soundfile

# Opcional: voces de mejor calidad con Roformer (se usa en calidad alta y máxima). Los pines de
# torch son necesarios: sin ellos pip cambia PyTorch por otra versión y Demucs deja de funcionar.
~/demucs-env/bin/pip install audio-separator onnxruntime torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0
```

Para el **análisis de tempo y acordes** hace falta un segundo entorno, porque madmom usa
otra versión de numpy:

```bash
python3 -m venv ~/analisis-env
~/analisis-env/bin/pip install --upgrade pip setuptools wheel "numpy<2" "cython<3"
~/analisis-env/bin/pip install --no-build-isolation git+https://github.com/CPJKU/madmom
```

Analizar una canción de 7 minutos tarda alrededor de un minuto en el procesador.

La app busca Python en `~/demucs-env/bin/python` y `~/analisis-env/bin/python`; si los
instalaste en otro lugar, cámbialos en **Ajustes IA**. La primera separación descarga los modelos (80 MB por modelo de
Demucs, 913 MB el de Roformer).

Opciones al separar, arriba en la ventana:

| Pistas | Calidad | Qué hace | Tiempo en una GTX 1050 Ti, canción de 7 min |
|---|---|---|---|
| 4 | normal | `htdemucs` | 35 s |
| 6 | normal | `htdemucs_6s` (+ guitarra y piano) | 40 s |
| 4 | alta | voces con Roformer + `htdemucs_ft` (modelos afinados) | unos 6 min |
| 6 | alta | lo anterior + `htdemucs_6s` para guitarra y piano | unos 7 min |
| 4 o 6 | máxima | igual que alta, con 3 pasadas promediadas y más solapamiento | 3 veces alta |

El modelo de 6 pistas es bueno en guitarra y flojo en piano: es un límite del modelo. Roformer
necesita unos 4 GB de memoria de video; sin tarjeta, en CPU, es muy lento.
En CPU, con `htdemucs` una canción de 4 minutos tarda de 2 a 6 minutos según el procesador.

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

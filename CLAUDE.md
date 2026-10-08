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

## 2. Funciones actuales (v0.3)

- Setlist ordenable (clic derecho: subir, bajar, renombrar, mover a la papelera).
- Mezclador por canción: fader en dB, mute, solo, medidor de pico y **salida estéreo por pista**
  (pares 1-2, 3-4…), por ejemplo click y guía a in-ears y el resto al PA. Se guarda por canción.
- Marcadores por canción (añadir con M, saltar con 1-9, clic derecho para renombrar, mover o
  eliminar) y **loop de sección** (de un marcador al siguiente).
- Click generado (4/4 fijo): BPM, inicio en segundos, volumen y salida propia.
- Fundidos de unos 6 ms al dar play, pausa o stop y al saltar: nunca hay clics. El cierre del
  loop hace un crossfade (equal-power) con lo que precede al inicio de la región, el final de la
  canción se funde, y al cambiar de canción primero se apaga con fundido la que suena.
- Separación con IA: con una canción cargada, "Separar canción (IA)" ofrece separar **esa**
  canción (sus pistas se reemplazan por los stems y el audio anterior queda en la subcarpeta
  `original`; marcadores, análisis, tempo y cortes se conservan; `Library::replaceStems`) o
  elegir otro archivo, que se agrega como canción nueva; también se puede arrastrar uno solo a
  la ventana. Dos selectores:
  **pistas** (4, o 6 con guitarra y piano) y **calidad** (normal, alta, máxima). El pipeline
  corre en un hilo aparte con barra de progreso global y al terminar se importa como canción
  nueva. Normal usa `htdemucs` o `htdemucs_6s`; alta usa los modelos afinados `htdemucs_ft`
  (más `htdemucs_6s` solo para guitarra y piano) y, si está instalado `audio-separator`, saca
  antes las voces con Mel-Band Roformer; máxima suma 3 pasadas promediadas y más solapamiento.
  La pista "otros" siempre es el residuo exacto (mezcla menos las demás), así la suma de los
  stems reproduce la mezcla original. Si al terminar hay una canción sonando, la nueva solo se
  agrega al final del setlist (no se cambia de canción). Cancelar no muestra error.
  **Batería en partes**: la casilla junto a la calidad añade una etapa final con el modelo
  MDX23C DrumSep de audio-separator sobre `drums.wav`, que se reemplaza por bombo, caja, toms,
  hi-hat, ride y crash (`drums_kick.wav`… traducidos por `displayNameFor`); "otros" absorbe la
  pequeña diferencia entre la batería y la suma de sus partes. Para canciones ya separadas, el
  diálogo de "Separar canción (IA)" ofrece "Solo la batería en partes" (`startDrumParts`,
  `Library::replaceStem`: la batería pasa a `original/` y entran las partes). Ambas requieren
  audio-separator y ffmpeg ("Ajustes IA" > "Instalar también Roformer y DrumSep").
- **Armar mix antes de separar** (botón "Armar mix", `MixProject`, `MixEditor`): una sección que
  reemplaza la vista de la canción mientras está abierta. Se cargan las canciones originales
  completas ("+ Canción"; se copian a `_mixes/<mix>/fuentes`), madmom las analiza solas una tras
  otra (tiempos, primeros tiempos de compás, acordes y tonalidad; con un `Analyzer` propio,
  `mixAnalyzer`), y en la vista de la fuente se seleccionan tramos arrastrando sobre la forma de
  onda, ajustados a compases o tiempos y a la transiente (`mix::refineToOnset`), que se agregan al
  mix en orden. Cada borde de la selección se arrastra solo (por su línea o por su asa en la regla),
  Shift + clic lleva ahí el borde más cercano, el clic derecho ofrece "Inicio / Fin del tramo aquí", y
  en la cabecera de la fuente los compases se escriben ("Compases [8] a [16]"; sin análisis, tiempos
  m:ss). Un clic suelto solo mueve el cabezal desde donde escucha "Escuchar". **Nivelado** (selector de
  la cabecera: sin nivelar o -12/-14/-16/-18 LUFS; los mixes nuevos empiezan en -14): al renderizar,
  cada tramo se mide (EBU R128) y se lleva a esa sonoridad sin pasar de -1 dBTP (tope ±12 dB), con su
  ganancia propia encima; el panel muestra "Niv +x dB" (naranja con "tope" si no llegó para no
  saturar) y el pie la sonoridad del mix y cuántos tramos quedaron más bajos. Así la canción que se
  separa ya va pareja. Cada tramo se estira con razón constante para durar exactamente sus tiempos al
  tempo del mix (el del primer tramo o el que se escriba; o "Cada tramo a su tempo"), así cada
  unión cae justo en la rejilla, y dentro del tramo se conserva el pulso de la grabación. Por
  tramo: mover, quitar, duplicar, inicio y fin por compases, tempo propio, tono, ganancia,
  fundido cruzado (corte de 10 ms, 1 o 2 tiempos, 1 o 2 compases) y nombre. "Escuchar el mix" y
  "Escuchar la unión" (4 s antes) renderizan a `mezcla.wav` y lo reproducen con el click opcional
  siguiendo los tiempos del mix; también se escucha la fuente con su click para comprobar el
  análisis (con menú para tiempos a la mitad o al doble, compases de 3 o 4 y "este tiempo es el
  1"). Ctrl+Z deshace (hasta 40 pasos). "Crear canción..." separa el mix con IA (con las opciones
  de la barra de arriba) o lo agrega sin separar; la canción nueva ya trae los tiempos, compases,
  acordes y tonalidad (transpuestos si hace falta), secciones de tempo y un marcador por tramo
  (`mix::describeSong`), así el click, la guía de escenario y el nivelado funcionan sin analizar.
- Importar stems: varios archivos o una carpeta (o arrastrar varios archivos a la ventana).
  Formatos: wav, aiff, flac, mp3, ogg (m4a solo en macOS: JUCE no decodifica AAC en Linux).
- **Vista de arreglo** (`TimelineView`): regla con marcadores (clic = ir, arrastrar = mover,
  clic derecho = menú), compases y tiempos a partir del BPM y el inicio del click, y tiempo en
  m:ss; un carril por stem con forma de onda (escalada al pico de la pista), color, nombre, M y
  S; cabezal; loop sombreado; clic o arrastre = saltar; Ctrl+rueda = zoom, rueda = desplazar,
  "Ajustar" = ver toda la canción. Sigue al cabezal mientras suena.
- **Mezclador** (`MixerPanel`): un canal por stem con fader en dB, medidor estéreo de pico y
  RMS con retención de pico, clip que se apaga con un clic, mute, solo y salida; **canal del
  click** (`ClickStrip`: encendido, fader, medidor del click generado con `takeClickPeak`, y
  salida propia, sincronizado con la fila del click); canal maestro con fader (se guarda por
  canción como `masterGainDb`) y medidor con escala en dB. Las salidas que se pueden elegir
  son los pares de canales del dispositivo activo: con "default" de PulseAudio solo 1-2; con
  una interfaz multicanal elegida en "Audio" (ALSA directo) o con JACK/PipeWire, los demás.
- **Guía de escenario** (botón "Pantalla" o F12, `StageView` / `StageWindow`): ventana aparte
  para una segunda pantalla: acorde actual enorme y siguiente al centro, compás y tiempo con
  puntos que marcan el pulso (el 1 en color de acento) y barra de avance, tempo de la sección,
  tonalidad, título, sección actual y siguiente, tiempo, y "Detenido · siguiente canción". La
  alimenta el Timer (`updateStage`, con `currentChords` y `barAndBeat`: del análisis o de la
  rejilla fija). Doble clic o F11 en esa ventana = pantalla completa (kiosk); el resto de
  teclas (pedal) se reenvían a la ventana principal. Su posición se guarda (`stageWindow`).
- **Notas de texto** ("+ Nota" o tecla N en el cabezal; clic derecho en la fila de notas de la
  regla para añadir, editar, mover o eliminar): texto libre anclado a un instante de la
  canción (ajustado al tiempo detectado más cercano) con una duración en segundos (0 = hasta
  la siguiente nota). En la regla son banderitas amarillas con el texto; en la guía de
  escenario, un letrero amarillo grande mientras dura y, hasta 12 s antes, "Próxima nota (en
  N s): ...". Se guardan en `song.json` (`notes`), se mueven o recortan con la grilla
  (`shiftGrid`) y Ctrl+Z las deshace.
- **Triggers y sampler interno** (`Triggers`, `SamplerLane` en el motor): botón "T" en la cabecera
  de cada carril (o clic derecho en la cabecera): los golpes de esa pista (por ejemplo el bombo
  separado) disparan un **banco de muestras** (carpeta con wav en `<biblioteca>/_bancos/<nombre>`,
  compartida por todas las canciones; se importan wav sueltos como banco nuevo, o se graban,
  punto 2). La detección es fuera de línea sobre el audio ya renderizado (`triggers::detect`:
  umbral en dBFS, sensibilidad y tiempo mínimo por pista), así los golpes se conocen de
  antemano: timing exacto a la muestra y sin latencia. Cada golpe elige la muestra por su
  fuerza (capas de suave a fuerte, alternando vecinas) y suena en un canal propio del
  mezclador ("<pista> (muestras)", naranja: fader, mute, solo, salida y medidor, guardados en
  `trigger` de la pista). Las marcas naranjas al pie del carril muestran los golpes y su fuerza.
  En el canal del mezclador, bajo el fader, la casilla del trigger dice qué suena: "Audio"
  (la pista por sí misma), "Trig: <banco>" (solo el sonido del trigger: la pista queda
  silenciada, `LoadedTrack::replaced`) o "Trig+Audio: <banco>" (ambas); un clic abre el mismo
  menú que el botón T: los tres modos, el sonido (bancos grabados o importados; instrumentos
  VST/AU cuando haya), la nota MIDI que se enviará a un instrumento (mapa de batería General
  MIDI o cualquier nota) y los ajustes de detección.
- **Pistas MIDI** (`MidiTracks`, `SongInfo::midiTracks`): en el menú del trigger de una pista, "Crear
  pista MIDI con estos golpes" congela los golpes detectados (sobre el arreglo, con los ajustes de
  detección del trigger) en una pista MIDI editable que aparece bajo esa pista en la vista de
  arreglo, al estilo de un editor de batería: una fila por sonido ("pad": nombre, nota y sonido,
  un banco de muestras o un instrumento del rack con esa nota). Sirve para corregir a mano lo que
  separó la IA (por ejemplo, pasar las congas que cayeron en "Toms" a su propia fila con su propio
  sonido). Gestos: clic elige (Ctrl o Shift suma), arrastrar mueve en el tiempo y entre filas,
  Alt + arrastre vertical cambia la fuerza, arrastrar en un hueco elige con un rectángulo, doble
  clic agrega un golpe, Supr borra, clic derecho abre menús (golpes: eliminar, pasar a otra fila,
  fuerza, cuantizar a tiempos, corcheas, tresillos o semicorcheas, volver a detectar un tramo;
  fila: sonido, nota (General MIDI o, en un submenú, el mapa "AD2 Standard" de Addictive Drums 2),
  escuchar, renombrar, silenciar, agregar o quitar filas; título: renombrar, silenciar
  la pista de origen, volver a detectar todo, eliminar). Agregar y mover se ajustan según el modo
  de corte (libre, a la semicorchea o a la transiente de la pista de origen). Cada fila con banco
  tiene su canal en el mezclador (verde); las de instrumento suenan por el canal del instrumento.
  Al crearla, la pista MIDI reemplaza al trigger en vivo de esa pista (sin apagarlo: si se elimina
  la pista MIDI, el trigger vuelve) y la pista de origen se calla mientras la MIDI suena (se puede
  desactivar). Ctrl+Z deshace las ediciones. El triángulo a la izquierda del nombre de cada fila
  (o "Escuchar" en su menú) toca un golpe de esa fila aunque la canción esté detenida
  (`AudioEngine::audition`): sirve para comprobar el sonido y para el "Learn" de Addictive Drums
  (se aprieta Learn en la pieza, por ejemplo un Flexi con congas, y luego el triángulo).
- **Grabación de entradas** (botón "Grabar", `Recorder`, `AudioEngine::setRecorder`): graba una
  entrada del dispositivo (mono o par estéreo) **como pista nueva de la canción**: la canción
  arranca desde el cabezal, la entrada se escucha por el par de salida elegido y al detener
  (botón, Stop, pausa o final) la toma se guarda alineada con la canción (se descuenta la
  latencia de entrada más salida que informa el dispositivo, más una compensación manual en
  ms) como `<nombre>.wav` en la carpeta de la canción con `songTime` (ya está en la línea de
  tiempo del arreglo: no pasa por los tramos). O **golpes para un banco de muestras** (el
  mismo diálogo, "Qué grabar"; o "Grabar golpes como banco nuevo..." en el menú del trigger,
  que además asigna el banco a esa pista): se tocan golpes sueltos, al detener se cortan
  (`sliceHits`) y se guardan en `_bancos/<nombre>/golpe-NN.wav`. Al cargar un banco cada
  muestra queda con exactamente 2 ms antes de su ataque (`SampleBankData::preRoll`) y el motor
  adelanta las voces esos 2 ms: la transiente de la muestra cae sobre la transiente detectada
  en la pista, sin retardo. Si el dispositivo no tiene entradas activas se activan las dos
  primeras solas (y "Audio" ahora deja elegir hasta 32 entradas). Grabar una pista exige tempo
  y tono originales (la toma se alinea con el audio guardado). Ajustes: `recordInput`,
  `recordMonitor` (-1 = no escuchar), `recordOffsetMs`.
- **Instrumentos VST3/AU** (botón "Instrumentos", `InstrumentRack`, `InstrumentLane` en el motor):
  rack global (el mismo para todo el setlist) de instrumentos VST3 (y AU en macOS), por ejemplo
  Addictive Drums. "Buscar instrumentos instalados" recorre las carpetas estándar (o una carpeta
  elegida) en un hilo aparte; "Añadir instrumento" carga uno de los conocidos; cada uno tiene su
  canal en el mezclador (lila: fader, mute, solo, salida y medidor; clic en el nombre abre la
  ventana del plugin) y su estado, mezcla y descripción se guardan en `instrumentos.xml` junto a
  los ajustes y se restauran al abrir la app. En el menú del trigger de una pista, "Sonido" ofrece
  los instrumentos del rack: los golpes de esa pista salen como notas MIDI hacia el plugin (la nota
  de "Nota MIDI", velocidad 0,25 + 0,75 × fuerza del golpe, 50 ms de duración), adelantadas la
  latencia que el plugin declara, así el sonido cae exacto en el golpe. La pista original se
  silencia o no según el modo (solo trigger / ambas). La nota se elige de la lista General MIDI o
  del mapa "AD2 Standard" de Addictive Drums 2 (el que trae por defecto; su ventana MIDI Mapping
  también tiene uno General MIDI); "Escuchar" en el mismo menú toca un golpe con ese sonido.
- **Modo en vivo** (botón "En vivo" o F11): oculta la vista de arreglo, muestra la barra de
  posición simple y agranda título, sección, acorde actual y tiempo. El mezclador queda visible.
- **Análisis musical** (botón "Analizar (IA)", `Analyzer`): madmom en un venv aparte detecta
  tiempos y primeros tiempos de compás (3/4 o 4/4), tempo, acordes (mayor, menor, séptimas)
  sobre la mezcla sin batería, y tonalidad. Se guarda en `song.json` (`analysis`). Con
  análisis: la regla dibuja los compases y tiempos reales, una fila de acordes con el actual
  resaltado; el click sigue los tiempos detectados; el BPM y el inicio del click se ajustan
  solos; la ventana muestra acorde actual y siguiente, tonalidad y tempo. Unos 60 s por canción
  de 7 min en CPU.
- **Enarmonías y tonalidad a mano** (`Music`): clic en la tonalidad (arriba a la derecha)
  abre un menú para escribir las notas con sostenidos (Re#, Sol#) o con bemoles (Mib, Lab), o
  según la tonalidad (armaduras con bemoles = bemoles); los acordes de la regla y de la
  cabecera se reescriben igual. También permite cambiar la tonalidad a mano (mayores y
  menores) y volver a la detectada. Se guarda por canción (`spelling`, `keyOverride`).
- **Métrica editable**: clic derecho sobre un tiempo en la fila de compases de la regla (con
  zoom suficiente para ver los tiempos): "Primer tiempo de compás desde aquí", "Compás de N
  tiempos desde aquí" (2 a 7), "Compás corto solo aquí" (1 a 3 tiempos que forman un compás
  propio, con su 1 acentuado en el click, y después siguen los compases normales:
  `shortBar`), quitar o insertar un tiempo. La renumeración llega hasta el
  final de la sección de tempo (o de la canción, con la opción del menú); el compás anterior
  queda más corto, que es lo que hace falta en un enganche entre canciones de un mix para que
  la siguiente entre en su "1". El click y la regla siguen los tiempos editados.
- **Click por sección** (submenú "Click de esta sección" en la banda de tempo): "Tiempos a
  la mitad" (madmom detectó corcheas: se deja uno de cada dos, conservando el 1 del compás),
  "Tiempos al doble", "Rejilla fija a N BPM desde el inicio de la sección" (reemplaza los
  tiempos detectados por un click regular al tempo de la sección: un cambio de tempo manual,
  útil tras "Dividir la sección" y "Corregir el tempo detectado"), "Rejilla pareja: N tiempos
  iguales hasta el fin de la sección" (N = los que caben al tempo de la sección, repartidos
  exactos hasta el siguiente compás, en dos variantes: el sobrante se suma al último compás,
  por ejemplo uno de 5, o queda como compás aparte con su propio 1; arregla las transiciones
  de un mix donde madmom mete corcheas) y "Compases de N tiempos" para toda la sección.
  Mitad, doble y rejilla pareja recalculan el tempo original de la sección.
- **Acordes editables**: clic derecho en la fila de acordes: cambiar el acorde (texto libre,
  por ejemplo "Bbmaj7"), sin acorde, dividir en ese punto (ajustado al tiempo más cercano),
  unir con el siguiente, o añadir un acorde en un hueco (hasta el siguiente acorde).
- **Arreglo del audio** (fase 4, `Arrangement`): clic derecho sobre un carril: cortar los
  stems en ese punto (todos a la vez), desplazar el tramo (o el tramo y los siguientes) en
  milisegundos, alinear su inicio al tiempo detectado más cercano, unir con el anterior,
  eliminar dejando silencio o cerrando el hueco, restaurar el audio original. Shift + arrastre
  sobre un carril mueve el tramo bajo el mouse (se ve el desplazamiento en ms). "Copiar
  tramo", "Pegar insertando" (abre espacio: el audio y la grilla que siguen se corren, y la
  copia trae los tiempos y acordes de su rango y, si venía de una sección con otro tempo, su
  propia sección de tempo), "Pegar encima" (superpone) y "Duplicar tramo" (pega insertando a
  continuación): con eso se reordena una canción (cortar en los compases, copiar, pegar
  insertando, eliminar el original cerrando el hueco); el portapapeles solo se pega en la canción
  donde se copió (`ClipClipboard::folder`: un tramo es un rango de su audio). La grilla no se mueve con el audio,
  salvo al cerrar o abrir un hueco (`shiftGrid`). Los bordes de los tramos se
  dibujan sobre los carriles y los huecos quedan sombreados. Ctrl+Z (o el menú) deshace la
  última edición de audio o de grilla (hasta 30 pasos; la mezcla no se deshace).
- **Modo de corte** (selector junto a "+ Marcador", ajuste `cutMode`): "Corte libre" corta
  donde se hizo clic; "Corte a la rejilla" en el tiempo detectado más cercano (o de la
  rejilla fija del click); "Corte a la transiente" busca el ataque más cercano (±200 ms) en la
  pista del carril donde se hizo clic (la batería es la mejor referencia) y corta justo antes
  del golpe (`findOnset`). El menú muestra dónde caerá el corte. El mismo modo rige el
  Shift + arrastre: a la rejilla encaja el inicio del tramo en el tiempo más cercano; a la
  transiente encaja el primer golpe del tramo (en su primer cuarto de segundo) en el tiempo.
- **Nivelado de sonoridad** (EBU R128, `Loudness`): "Nivelar" mide primero **cada stem** por
  tramo entre marcadores y le da a cada stem, en cada tramo, la ganancia que lo lleva al nivel
  de ese mismo stem en toda la canción (tope ±12 dB; un stem bajo -50 LUFS en el tramo se deja
  en 0: así la batería del tema 2 de un mix suena como la del tema 1 sin tocar los faders, y no
  se levanta ruido donde no toca); después mide la **suma** con esas ganancias (a unidad, sin
  normalizar) y aplica a cada tramo la ganancia general que lo lleva al objetivo
  (-12/-14/-16/-18 LUFS), sin pasar de -1 dBTP. El motor aplica ambas con rampas de 50 ms
  (`GainCurve` con `trackGains` por stem), encima de los faders y sin tocar el click. En el
  mezclador, cada canal muestra "Niv +x dB": la ganancia por pista del tramo donde está el
  cabezal; un clic la edita y cambia ese tramo completo (volver a medir la reemplaza). Las
  ondas de la vista de arreglo se dibujan con la ganancia total del tramo. La regla muestra "LUFS (+dB)" en cada marcador; clic derecho en el marcador
  permite retocar la ganancia del tramo. **Sin marcadores la canción es un solo tramo** (un
  mix de varios temas recibe una sola ganancia): hay que poner marcadores en los cambios
  (por ejemplo "Crear marcadores en los cambios de tempo"). Al añadir, mover o borrar
  marcadores con el nivelado activo se vuelve a medir solo (`remeasureIfLeveling`), la
  casilla mide si hay tramos sin medir (`needsLevelMeasure`) y al cargar una canción con
  nivelado activo y tramos sin medir también se mide. Tramos de menos de un par de segundos
  dan mediciones sin sentido: los marcadores para nivelar van en los cambios de tema. "Nivelar setlist" mide todas las canciones y les da
  una ganancia global (`songGainDb`) para que suenen parejas al pasar de una a otra.
- **Tempo y tono** (`TempoMap`, `Stretcher`, Signalsmith Stretch): fila "Tempo" con el BPM al
  que suenan todas las secciones (botón "Original" lo devuelve), "Tono" en semitonos para la
  canción; cada sección de tempo puede tener además su propio tono ("Tono de la sección..." en
  el menú de la banda; `TempoRegion::transpose`, `followSong` = el de la canción), que se ve
  en la banda como "+2 st". El tempo cambia sin alterar el tono; el tono solo con esos
  controles. Se renderiza en segundo
  plano a partir de la canción original y se intercambia con fundido conservando el punto y el
  estado de reproducción. Marcadores, tiempos, acordes, click y nivelado se muestran y aplican
  en el tiempo de reproducción mediante el mapa. Sirve para igualar el BPM de varias canciones
  para un mix.
- **Secciones de tempo** (`tempoRegions`, `detectTempoRegions`): a partir de los tiempos del
  análisis se detectan los tramos con tempo distinto (un mix de canciones a distinto BPM) y se
  muestran en una fila de la regla, sobre los compases, con su BPM ("82.7 -> 77.5 BPM" si está
  estirada). Clic en la banda: tempo de reproducción de esa sección, volver a su original,
  igualar todas las secciones a ese tempo, dividir o unir secciones, corregir el tempo
  detectado y crear marcadores en los cambios de tempo. Con análisis, el campo BPM del click
  no se edita: muestra el tempo de la sección que suena (el click sigue los tiempos
  detectados). Arriba a la derecha, la tonalidad va con el tempo de la sección actual
  ("96 -> 78 BPM" si está estirada).
- Atajos: Espacio (play/pausa), Esc (stop y volver al inicio), flechas izquierda/derecha y
  RePág/AvPág (canción anterior/siguiente; los pedales Bluetooth suelen enviar esas teclas),
  1-9 o teclado numérico (marcador), M (añadir marcador), L (loop), F11 (en vivo). La
  autorrepetición de una tecla mantenida se ignora (un pedal pisado no salta varias canciones).
  Cerrar la ventana con una canción sonando pide confirmación.

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

**Paquetes e instaladores**:
- Ubuntu: `./linux/empaquetar-deb.sh` deja `dist/secuencias_<versión>_amd64.deb` (instala en
  `/opt/secuencias`, lanzador `/usr/bin/secuencias` que usa `pw-jack` si existe, `.desktop` e
  ícono). `./install.sh` sigue sirviendo para instalar en el usuario sin paquete.
- macOS: `./mac/empaquetar.sh` (en un Mac con Xcode CLT, CMake y Ninja) compila el preset
  `release-mac` (binario universal arm64 + x86_64, macOS 11+), firma ad hoc el `.app` (sin
  cuenta de desarrollador; imprescindible en chips Apple) y arma `dist/Secuencias-<versión>-mac.dmg`
  con el `.app`, un enlace a Aplicaciones y "Primera vez.txt" (cómo saltar Gatekeeper). El ícono
  sale de `mac/icono.png` (`ICON_BIG`; JUCE genera el `.icns`). Desde Linux no se puede compilar
  para Mac.
- Windows: preset `release-win` (Visual Studio 2022, x64, multi-config: `cmake --build --preset
  release-win` y `ctest --preset release-win`), y `windows/instalador.iss` (Inno Setup 6) que
  arma `dist/Secuencias-<versión>-windows-setup.exe`: instala por usuario en
  `%LOCALAPPDATA%\Programs\Secuencias` sin administrador, con `instalar-ia.ps1` y la carpeta
  `wheels\` (madmom precompilado para Python 3.10 a 3.12, porque PyPI no trae ruedas de madmom
  para Windows y compilarlo exige Visual Studio). Sin firma: SmartScreen avisa la primera vez.
  Audio con WASAPI y DirectSound (sin ASIO: exigiría el SDK de Steinberg). m4a se decodifica
  con Media Foundation. MSVC compila con `/utf-8` (los fuentes tienen tildes en `tr()`).
- GitHub Actions (`.github/workflows/build.yml`): en cada push a `main` compila el DMG en
  `macos-14`, los tests más el `.deb` en `ubuntu-22.04`, y en `windows-2022` los tests, las
  ruedas de madmom y el instalador; los deja como artefactos y con una etiqueta `v*` publica
  una Release con los tres instaladores. Las compilaciones de Mac y Windows solo se ven ahí.
  El repositorio es `git@github.com:Dolansalfate/secuencias.git` (privado); el token de acceso
  limitado al repositorio está en `~/.config/secuencias/gh-token` (`GH_TOKEN` para `gh`).
- Motores de IA: `scripts/instalar-ia.sh` (macOS y Ubuntu) e `instalar-ia.ps1` (Windows,
  venvs en `%USERPROFILE%` con `Scripts\python.exe`; `venvPython()` en MainComponent da la ruta
  por defecto según plataforma) crean `~/demucs-env` y `~/analisis-env` (elige PyTorch según sistema y tarjeta: cu126, cpu, o 2.2.2 en Mac Intel, el último con
  versión para esa arquitectura; exige Python 3.9 a 3.12). Viaja en el bundle de macOS
  (`Contents/Resources`), en `/opt/secuencias` (.deb) y en `~/.local/share/secuencias`
  (`install.sh`); "Ajustes IA" > "Instalar motores de IA" lo abre en una terminal
  (`launchInstaller`: osascript con Terminal en macOS; `powershell -NoExit -File` en Windows;
  `x-terminal-emulator`, gnome-terminal, konsole o xterm en Linux).

**Antes de dar por terminado un cambio**:
1. `cmake --build --preset debug` sin errores ni warnings nuevos en `Source/` o `Tests/`.
2. `ctest --preset debug` pasa.
3. Si tocaste la UI, mírala: `./build/debug/Secuencias_artefacts/Debug/Secuencias --captura=/tmp/v.png --cancion=2`
   abre la ventana, espera a que cargue la canción 2 del setlist, guarda un PNG renderizado por
   JUCE y sale (con `--vivo` captura el modo en vivo). Funciona aunque haya otra instancia
   abierta y en Wayland, donde las capturas X11 salen en blanco. Sin pantalla, `xvfb-run -a`
   delante. No debe haber "JUCE Assertion failure" en la salida.
4. Si agregas lógica al motor o a la biblioteca, agrega un caso en `Tests/EngineTests.cpp`.

Los tests compilan además un VST3 mínimo (`Tests/TestSynth.cpp`, target `SecuenciasTestSynth_VST3`:
en cada noteOn emite 50 ms de nivel igual a la velocidad) y lo cargan con el host real
(`VST3PluginFormat`); su ruta llega por la definición `SECUENCIAS_TEST_SYNTH`. En la CI de Ubuntu
`ctest` corre bajo `xvfb-run` por si el host VST3 necesita pantalla.

## 4. Estructura

```
CMakeLists.txt         App (juce_add_gui_app) + SecuenciasTests (juce_add_console_app, opcional)
CMakePresets.json      Presets debug / release / tests (Ninja, build/<preset>)
Source/
  Main.cpp             JUCEApplication + ventana principal
  MainComponent.h/.cpp Ventana principal: barra superior, setlist, transporte, marcadores, click,
                       modo en vivo; integra TimelineView y MixerPanel (MarkerButton vive en el .cpp)
  TimelineView.h/.cpp  Vista de arreglo: regla, carriles con forma de onda, cabezal, zoom
  MixerPanel.h/.cpp    ChannelStrip, MasterStrip y MixerPanel
  FilePicker.h/.cpp    Selector de archivos dentro de la ventana (Linux y Windows)
  StageView.h/.cpp     Guía de escenario: StageState, StageView (dibujo) y StageWindow (segunda pantalla)
  Meters.h/.cpp        LevelMeter (pico, RMS, retención, clip, escala)
  UiUtils.h            Colores, fuentes, paleta de pistas, disableFocus, formatTime
  AudioEngine.h/.cpp   Motor de reproducción (AudioIODeviceCallback)
  Library.h/.cpp       Modelo de datos + persistencia (setlist.json, song.json)
  Separator.h/.cpp     Hilo que convierte a WAV y ejecuta Demucs (y Roformer)
  Analyzer.h/.cpp      Hilo que mezcla los stems y ejecuta madmom (script Python embebido)
  Loudness.h/.cpp      Sonoridad EBU R128 y pico real (ponderación K, bloques, puertas, x4)
  TempoMap.h/.cpp      TimeMap: tramos con BPM original y de reproducción; original <-> reproducción;
                       detección de secciones de tempo
  Music.h/.cpp         Notas, acordes y tonalidades: enarmonías, sostenidos o bemoles, solfeo
  Arrangement.h/.cpp   Arreglo: tramos de audio (cortar, mover, eliminar, unir), render a RAM, shiftGrid
  Triggers.h/.cpp      Golpes detectados (detect), bancos de muestras (loadBank, sliceHits, writeWav)
  Recorder.h/.cpp      Grabación: alinear la toma con la canción (writeAligned), banco desde golpes (saveBank)
  MidiTracks.h/.cpp    Pistas MIDI: golpes congelados desde la detección, edición (mover, fuerza, cuantizar, filas)
  Instruments.h/.cpp   InstrumentRack: plugins VST3/AU (carga, estado, editor, búsqueda) y su entrega al motor
  MixProject.h/.cpp    Armar mix: fuentes, tramos, mix.json, ubicación (layout), render y la canción que resulta
  MixEditor.h/.cpp     Sección "Armar mix": lista de fuentes, vista de la fuente, línea del mix, panel del tramo
  Stretcher.h/.cpp     Render de tempo y tono con Signalsmith Stretch (FetchContent, MIT)
Tests/EngineTests.cpp  Tests sin dispositivo: llaman al callback de audio a mano
Tests/TestSynth.cpp    VST3 mínimo que compilan los tests para probar el host de instrumentos
linux/                 .desktop (plantilla con @EXEC@), ícono SVG y empaquetar-deb.sh
mac/                   icono.png (ícono del bundle) y empaquetar.sh (DMG universal, firma ad hoc)
windows/               icono.ico e instalador.iss (Inno Setup)
scripts/               instalar-ia.sh (macOS y Ubuntu) e instalar-ia.ps1 (Windows): venvs de Demucs y madmom
.github/workflows/     build.yml: DMG y .deb en cada push, Release en cada etiqueta v*
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
| **Analizador** | `Analyzer` (`juce::Thread`) mezcla los stems en RAM a WAV y ejecuta madmom | Mismo patrón que el separador; el resultado (`Analysis`) se aplica en la UI al ver `done`. |
| **Grabación** | `TimeSliceThread` de un `AudioFormatWriter::ThreadedWriter` (JUCE): vuelca a disco lo que el callback deja en su cola sin bloqueo | Vive en `MainComponent::recording` mientras se graba; al detener, la toma se alinea o se corta en el hilo de carga. |
| **Búsqueda de plugins** | `InstrumentRack::Scanner` (`juce::Thread`): `PluginDirectoryScanner` sobre las carpetas de cada formato | Progreso y fin llegan con `callAsync`; `KnownPluginList` tiene su propio lock. Los plugins se **crean y destruyen en el hilo de mensajes**. |

### 5.2 AudioEngine
- `LoadedSong` contiene `LoadedTrack`s. Cada pista es un `AudioBuffer<float>` **estéreo, a la
  frecuencia del dispositivo, del mismo largo que la canción** (se rellenan con ceros al cargar),
  así el callback nunca lee fuera del buffer. Los mono se duplican y el remuestreo usa
  `LagrangeInterpolator`.
- Controles por pista como atómicos (`gain`, `muted`, `solo`, `outputPair`). Medidores
  post-fader por canal: `peakL/peakR` guardan el pico máximo y la UI los lee con
  `exchange(0)`; `rmsL/rmsR` son el RMS del último bloque. `smoothedGain` hace una rampa lineal
  por bloque (solo lo toca el hilo de audio). `waveform` (`WaveformCache`) guarda mín y máx por
  bloque de 256 muestras y el pico de la pista, calculado al cargar, para dibujar a cualquier zoom.
- Click: rejilla fija (BPM e inicio, 4/4) o, con `setBeatGrid`, los tiempos detectados
  (`BeatGrid` en muestras, bajo `songLock` como la canción; búsqueda binaria por muestra, sin
  memoria dinámica; acento en `beatInBar == 1`). `setSong` la descarta.
- Nivelado: `setGainCurve` (`GainCurve`, escalón por tramo en muestras, bajo `songLock`) se
  aplica por muestra a las pistas con suavizado de un polo de 50 ms (`levelSmooth`,
  `levelGain[]`, y `levelSegment[]` con el tramo de cada muestra); `trackGains[stemIndex][tramo]`
  añade la ganancia por pista, suavizada con `LoadedTrack::levelSmooth`; no afecta al click.
  `setSongGain` (nivelado entre canciones) se multiplica
  con el fader maestro en `applyMasterAndMeter`.
- Sampler de triggers: `setSamplers (SamplerSet)` bajo `songLock` (`setSong` lo descarta).
  Cada `SamplerLane` tiene sus `events` (muestra y velocidad, ordenados), su banco y un
  `LoadedTrack` de control (ganancia, mute, solo, salida, medidores) sin buffer. En
  `renderChunk`, tras las pistas: por muestra, con `positions[]`, se disparan los golpes cuya
  posición coincide (tras un salto se relocaliza con búsqueda binaria; los que quedaron atrás
  no suenan; con banco se compara `position + bank->preRoll`, así la voz arranca antes y su
  ataque cae en el golpe), en 16 voces prealocadas (se roba la más avanzada), ganancia 0,5 + 0,5 × velocidad,
  mezcla en `laneL/laneR`, envolvente global, salida al par de la línea y medidores. El solo de
  una línea silencia las pistas y viceversa. `LoadedTrack::replaced` (atómico, lo pone
  `buildSamplers` cuando el trigger está en modo "solo el sonido" y tiene banco) silencia la
  pista como un mute aparte del del usuario.
- Pistas MIDI: `setMidiSamplers (SamplerSet)` es un segundo conjunto de líneas, igual que el de los
  triggers (bajo `songLock`, `setSong` lo descarta, el solo cuenta para ambos) pero se rehace al
  instante en cada edición sin volver a detectar los triggers en vivo; `renderChunk` recorre los
  dos conjuntos en 2b). Sus líneas llevan `rawVelocity` (la velocidad MIDI / 127 va tal cual a un
  instrumento; con banco se deshace la curva 0,25 + 0,75 del trigger en vivo para elegir la capa y
  la ganancia, así un golpe congelado suena igual que el trigger que reemplaza), `midiTrack` y `midiPad`.
- Escuchar (`audition (stem, pistaMidi, fila, velocidad)`): la UI deja una petición empaquetada en un
  atómico (`auditionRequest`; gana la última) y el callback la toma en 2b): la línea que corresponde
  (fila de una pista MIDI, o el trigger en vivo del stem) dispara un golpe en la primera muestra del
  trozo aunque el transporte esté detenido. Con banco, una voz marcada `audition` que se mezcla en
  `auditionL/R` sin la envolvente del transporte (sí con el fader de la línea); con instrumento, la
  nota (`auditionNoteIn`: sale en 2c) cuando la compuerta ya abrió, 256 muestras menos la latencia, así
  el ataque llega entero) y `auditionHold` = 3 s, durante los cuales la compuerta `auditionGate` (sube
  en 256 muestras, baja en 300 ms) deja pasar la salida del plugin aunque la envolvente esté en 0.
  Respeta mute y solo; si la línea no existe no pasa nada. `pause` y `stop` piden fundirlo
  (`auditionCancel`: `auditionLevel` baja a 0 en un trozo y después se sueltan sus voces y se cierran
  las compuertas) e `isSilent()` espera también a `auditionActive`, así cambiar de canción justo
  después de escuchar no corta nada; `setSong` deja la compuerta cerrada.
- Líneas que no disparan: `SamplerLane::silenced` (fila de una pista MIDI silenciada: la línea existe
  para que su canal baje a 0 con rampa, sin canal en el mezclador) y `superseded` (atómico; la UI lo
  pone en el trigger en vivo de un stem que acaba de recibir una pista MIDI, hasta que el próximo
  render lo quite del conjunto: ningún golpe suena dos veces).
- Relevo de conjuntos sin cortes (`setSamplers` y `setMidiSamplers` con la canción sonando, por
  ejemplo al editar un golpe): `installSamplerSet` (bajo `songLock`, en la UI) encadena el conjunto
  nuevo con el que reemplaza (`SamplerSet::previous`, un solo eslabón; lo viejo se suelta fuera del
  lock, nunca en el hilo de audio). La primera vez que el callback ve el conjunto nuevo,
  `adoptPlayback` (sin memoria dinámica) pasa a cada línea equivalente (misma pista, fila, nota,
  archivo de origen e instrumento) hasta dónde miró la anterior y, con el mismo banco, sus voces; las voces que no pasan
  (sonido cambiado, fila quitada) se terminan de oír desde el anterior en pasadas de solo voces.
- Anticipación con loop (`lookAhead`): el pre-roll del banco o la latencia del instrumento que pasa
  del final del loop sigue desde su inicio, así el golpe del inicio suena exacto en cada vuelta; los
  golpes después del final no suenan mientras el loop siga. Tras un salto, los golpes que ya
  debían haber empezado su pre-roll suenan enseguida con la voz adelantada lo que llegan tarde.
- Instrumentos del rack: `setInstruments (InstrumentSet)` bajo `songLock` (`setSong` no lo toca;
  el conjunto anterior se libera fuera del lock en el hilo que llama, la UI, donde pueden morir
  los plugins quitados). Cada `InstrumentLane` lleva el plugin ya preparado con `maxBlockSize`,
  `work` (canales del plugin × 2048), `midi` (`ensureSize`), `latency`, `holdSamples` y 64
  note-offs pendientes. Las líneas del sampler con `instrumentId` no tienen voces: en 2b) buscan
  su instrumento por id y meten `noteOn` en su `midi` en la muestra en que `position + latency`
  coincide con el golpe (misma relocalización binaria tras un salto), y programan el `noteOff`;
  en 2c) cada instrumento emite los note-offs vencidos, `processBlock` sobre una vista de `work`
  de n muestras (salvo `isSuspended`), y su salida (canales 0 y 1) va con ganancia en rampa,
  envolvente global, par de salida y medidores, como las líneas del sampler. El solo cuenta
  también para ellos.
- Grabación de la entrada (`processInput`, antes de la canción): `setRecorder (RecordSetup)`
  publica un puntero atómico a una de dos configuraciones alternas (writer, canales de entrada
  entre los activos, escucha y su par) y espera con `waitForCallback` (`callbackDepth`) a que
  termine el callback en curso, igual que `clearRecorder`: al volver, el `ThreadedWriter` ya no
  se usa y la UI lo destruye. El callback copia los canales al writer (`write` no bloquea; si la
  cola se llena cuenta `droppedSamples`), guarda en `recordStartPosition` la posición del
  transporte del primer bloque grabado, mide `inputPeak` y suma la entrada al par de escucha
  con rampa por bloque (`monitorSmooth`, que también la apaga al dejar de grabar).
  `audioDeviceAboutToStart` guarda `numInputs` y las latencias de entrada y salida.
  El fader maestro se aplica también a la escucha (y aunque no haya canción cargada).
- Fader maestro: `masterGain` se aplica con rampa a todas las salidas al final del callback
  (`applyMasterAndMeter`), que además mide pico (`takeOutputPeak`) y RMS (`getOutputRms`) por
  canal de salida, hasta `maxMeteredOutputs`.
- `setSong()` cambia el `shared_ptr` bajo `songLock` (SpinLock). El callback usa
  `ScopedTryLock` y, si no lo obtiene, entrega silencio en ese bloque. La canción anterior se
  libera **fuera** del lock y en el hilo de UI.
- Transporte: `playing`, `position`, `pendingSeek` (-1 = ninguno), `loopRange` (inicio y fin
  empaquetados en un `uint64`, 32 bits cada uno, para leerlos de una sola vez; loop activo si
  `end > start`). Toda búsqueda pasa por `pendingSeek`: el callback funde a 0, salta y vuelve a
  subir. `getPositionSeconds()` devuelve `pendingSeek` si hay uno pendiente, para que la UI no
  parpadee. `currentFade` publica el nivel del fundido global; `isSilent()` es true cuando llegó
  a 0 (la UI lo usa para cambiar de canción sin cortes).
- `renderChunk` trabaja en trozos de hasta 2048 muestras. Primero calcula la posición y la
  envolvente de cada muestra (`positions[]`, `envelope[]`, prealocados), después mezcla cada
  pista y al final genera el click. En las últimas `fadeSamples` (256) muestras antes del fin del
  loop, `positions2[]` apunta a las muestras que preceden al inicio de la región y `xfadeOut[]` /
  `xfadeIn[]` hacen el crossfade, así el salto a `loopStart` es continuo y el largo del loop no
  cambia. Sin loop, la envolvente se funde en las últimas 256 muestras de la canción. Las pistas
  con ganancia 0 y objetivo 0 se saltan.
- Ruteo: `outputPairChannels(pair)` apunta a los canales `2*pair`, `2*pair+1` **entre los canales
  activos** del dispositivo (JUCE los compacta). Si el par no existe o es negativo, cae a 1-2.
  Con salida mono suma L+R×0,5.
- Fin de la canción: `playing = false` y la posición queda al final; `play()` vuelve a 0.
- `audioDeviceAboutToStart` actualiza la frecuencia y la cantidad de salidas; si la frecuencia
  cambió, pone `needsReload`, y la UI recarga la canción actual en el siguiente tick.
- `audioDeviceError` (por ejemplo JACK se cerró) guarda el mensaje; la UI lo recoge con
  `takeDeviceError()` desde el Timer y lo muestra.

### 5.3 Library (formato en disco)
Carpeta raíz: `Library::defaultRoot()` = carpeta de Música del usuario (XDG; en Ubuntu en
español es `~/Música`) + `/Secuencias`. Los tests usan una carpeta temporal.

```
Secuencias/
  setlist.json            ["Carpeta Canción 1", "Carpeta Canción 2", ...]  (orden del setlist)
  <Canción>/
    song.json
    drums.wav, bass.wav, ...   (wav/aif/aiff/flac/mp3/ogg; m4a solo en macOS)
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
  carpetas con audio que no estén en la lista. Solo cuentan las carpetas con archivos de audio
  reproducibles. Por eso **borrar una canción = mover su carpeta a la papelera** (si solo se
  sacara de la lista, volvería a aparecer). En Linux se usa `gio trash`, y `File::moveToTrash`
  como respaldo. `saveSetlist()` no reescribe el archivo si no cambió.
- `readSong()` acota todo lo numérico (`outputPair >= 0`, ganancias entre -60 y 12 dB, BPM entre
  20 y 400): un `song.json` editado a mano no debe poder sacar al motor de rango.
- Tempo y tono: `playBpm` (0 = original) y `transpose` (semitonos) por canción, y
  `tempoRegions`: `[{ "start", "bpm", "playBpm", "transpose"? }]`, las secciones de tempo en
  segundos del audio original (`bpm` = tempo detectado o corregido, `playBpm` 0 = el de la
  canción, `transpose` solo si la sección tiene tono propio). Se
  ordenan al leer y siempre hay una que empieza en 0. Si una canción analizada no las tiene
  (guardada antes de esta versión), `readSong` las detecta de los tiempos guardados
  (`detectTempoRegions`). Sin secciones ni análisis, el tempo original es el `bpm` de la canción.
- Nivelado: por canción `loudnessLufs`, `truePeakDb`, `songGainDb`, `levelingEnabled` y el
  tramo anterior al primer marcador (`headGainDb`, `headLufs`, `headTruePeakDb`); por marcador
  `gainDb`, `lufs`, `truePeakDb`. Por pista: `stemSongLufs` (canción entera), `headStemGainsDb`
  y `headStemLufs`, y por marcador `stemGainsDb` y `stemLufs`, todos indexados como `stems`
  (`fitStemArrays` los ajusta al leer). `unmeasuredDb` (-100) significa sin medir.
- `analysis` (opcional): `{ "bpm", "meter", "key", "beats": [[segundos, tiempoEnCompás], ...],
  "chords": [[inicio, fin, "Am"], ...] }`. `Analysis::bpmBetween`, `chordAt`, `downbeatAfter`
  y `nearestBeat` son las consultas que usa la UI; `renumberBeats` (con ancla), `halveBeats`,
  `doubleBeats`, `replaceBeatsWithGrid`, `evenGrid` y `shortBar` editan el click; `detectedBpmBetween` (TempoMap) mide
  el tempo de un tramo con los mismos criterios que la detección de secciones. Los
  acordes se guardan como los escribe madmom (siempre con sostenidos) y la tonalidad
  traducida ("Mib mayor"); la escritura se decide al mostrar.
- Arreglo: `clips`: `[{ "start", "end", "at" }]`, tramos del audio original (segundos) y
  dónde empiezan en la línea de tiempo de la canción; vacío = el audio entero. Todo lo demás
  (tiempos, acordes, marcadores, secciones de tempo, nivelado) está en la línea de tiempo de
  la canción, no en la del audio original.
- Triggers: por stem, `trigger`: `{ enabled, keepAudio (false = solo suena el trigger), sound
  ("banco:<nombre>" o "vst:<id>", el id estable del instrumento en el rack), note (MIDI, para instrumentos), thresholdDb,
  sensitivity, minMs, gainDb, muted, outputPair }` (`TriggerSettings`), y `songTime` (la pista
  ya está en la línea de tiempo del arreglo: `arrangement::render` la copia tal cual en vez de
  pasarla por los clips; lo llevan las grabaciones, `<nombre>.wav` en la carpeta de la
  canción, `Library::newRecordingFile`). Los
  bancos viven en `<raíz>/_bancos/<nombre>/*.wav` (`Library::listBanks`, `bankFolder`;
  `load()` no los toma como canción; `exportAll` los copia).
- Pistas MIDI: `midiTracks`: `[{ "name", "source" (archivo del stem de origen), "muteSource", "muted",
  "pads": [{ "name", "note", "sound", "gainDb", "muted", "outputPair" }], "hits": [[segundos, nota,
  velocidad], ...] }]` en la línea de tiempo de la canción (`MidiTrack`, `MidiPad`, `MidiHit`; los
  golpes ordenados con `sortHits`; `shiftGrid` los mueve con los huecos del arreglo).
- Notas: `notes`: `[{ "seconds", "duration", "text" }]` en la línea de tiempo de la canción
  (`SongNote`, ordenadas por `sortNotes`; las de texto vacío no se guardan).
- Escritura y tonalidad: `spelling` (0 = según la tonalidad, 1 = sostenidos, 2 = bemoles) y
  `keyOverride` (tonalidad elegida a mano, canónica como madmom: "Eb major"; vacío = la
  detectada).
- `importStemFiles(files, name, moveFiles)`: con `moveFiles` los archivos se mueven en vez de
  copiarse (solo para temporales propios, como el resultado de Demucs).
- **Proyectos portables**: la carpeta de una canción (song.json + stems, rutas relativas) es el
  proyecto. `exportSong` / `exportAll` copian la carpeta (o todas más `setlist.json`) a otro
  lugar; `isProjectFolder` reconoce una carpeta con `song.json` y audio, e `importProject` la
  copia a la biblioteca con todos sus ajustes (mezcla, marcadores, análisis, tempo, cortes,
  nivelado, tonalidad). En la UI: menú de la canción ("Exportar canción...", "Exportar todo el
  setlist...") e "Importar stems" (o arrastrar) con una carpeta de canción. Copiar entera la
  carpeta `Secuencias` a `~/Música` de otro equipo también funciona.
- Si en la carpeta aparecen archivos de audio que no están en `song.json`, se agregan como
  stems. Los nombres de Demucs se traducen (`vocals` → "Voces", etc.).
- Los marcadores se mantienen ordenados (`SongInfo::sortMarkers()`); la UI depende de eso.
- **Compatibilidad**: si cambias el esquema, lee los campos viejos con valores por defecto
  (`getProperty(name, default)`); no rompas las bibliotecas existentes.

Ajustes de la app (`juce::ApplicationProperties`): en Linux `~/.config/Secuencias/Secuencias.settings`,
en macOS `~/Library/Application Support/Secuencias/`. Claves: `audioDevice` (XML de
`AudioDeviceManager`), `pythonPath`, `analysisPythonPath`, `stems` (1 = 4 pistas, 2 = 6),
`quality` (1 normal, 2 alta, 3 máxima), `levelTarget` (1 = -12 LUFS, 2 = -14, 3 = -16, 4 = -18); `model` es el selector antiguo de la v0.2 y solo se lee para migrarlo.

### 5.4 Separator (Demucs + Roformer)
`SeparationOptions` (pistas 4/6, calidad 0-2, `roformerVocals`) se traduce con `planFor()` en
una lista de `SeparationStage` (herramienta, modelo, shifts, overlap, barras esperadas, peso):
- normal: `htdemucs` o `htdemucs_6s`, 1 pasada.
- alta: `htdemucs_ft` (bolsa de 4 modelos afinados) y, si son 6 pistas, además `htdemucs_6s`
  del que solo se toman guitarra y piano. No existe versión afinada del modelo de 6 pistas.
- máxima: lo mismo con `--shifts 3 --overlap 0.5` (3 pasadas promediadas; unas 4,5 veces más
  lento; mejora modesta, del orden de 0,2 dB).
- `drumParts`: etapa final `drumsep` con `audio-separator` y `MDX23C-DrumSep-aufr33-jarredou.ckpt`
  (partes kick, snare, toms, hh, ride, crash; peso 1,5) sobre la batería elegida, en `run()`
  después de armar `resultado/` y antes del residuo (`splitDrums`); `drumPartFileName` traduce
  los nombres de salida. `startDrumParts` corre solo esa etapa sobre un archivo de batería.
- `roformerVocals`: etapa previa con `audio-separator` y el modelo `vocals_mel_band_roformer.ckpt`
  (SDR 12,6 de voces, el mejor de su lista). Demucs corre después sobre el instrumental.
  Solo se activa si `isRoformerAvailable()`: existe `<venv>/bin/audio-separator` y hay `ffmpeg`
  en el PATH (lo exige aunque reciba WAV). Necesita unos 4 GB de VRAM (`--mdxc_batch_size 1`).
  Se pasa `--normalization 1.0`: por defecto escala la mezcla a 0,9 de pico y las voces dejarían
  de restar exacto.

Pasos de `run()`:
1. `convertToWav()`: la entrada a `input.wav` **44,1 kHz** (la frecuencia de los modelos, con
   `WindowedSincInterpolator`) y 24 bits en `/tmp/secuencias_separacion/trabajoN/`. Así ninguna
   herramienta remuestrea y todos los stems quedan alineados muestra a muestra con la mezcla.
2. Cada etapa en `etapaN/`: `<python> -u -m demucs -n <modelo> --shifts S --overlap O --int24
   -o etapaN <entrada>` o `audio-separator <entrada> --model_filename ... --output_dir etapaN`.
   La salida se lee **byte a byte** (`readProcessOutput` bloquea hasta llenar el buffer), por
   líneas, solo ASCII; `parsePercent` toma el `NN%|` de tqdm; cada reinicio de la barra es una
   pasada más (`modelsInBag x shifts`); las barras de descarga de modelos se ignoran porque traen
   velocidad en `B/s`. El progreso global pondera las etapas por su peso.
3. Se eligen las pistas: voces de Roformer si hubo, si no las de `htdemucs_ft` o del modelo
   base; batería y bajo del primer modelo de Demucs; guitarra y piano de `htdemucs_6s`.
   Se mueven a `resultado/` y `writeResidual()` escribe `other.wav = input.wav - suma` por
   bloques. La UI importa `resultado/` con `importStemFolder(folder, nombre, true)` y llama a
   `reset()`, que espera al hilo y borra los temporales.
- Demucs traga las excepciones por pista y sale con código 0: si no deja stems, el mensaje de
  error incluye las últimas líneas de su salida.
- Python por defecto: `~/demucs-env/bin/python` (venv). La receta validada está en el LEEME:
  `torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0` desde el índice `cu126` (o `cpu`), luego
  `demucs soundfile`, y opcionalmente `audio-separator onnxruntime` **repitiendo los pines de
  torch en el mismo comando**. Sin los pines, pip sube torch a la última versión (CUDA 13), mete
  bibliotecas `nvidia-*` de CUDA 13 en el mismo directorio que las de CUDA 12 y Demucs muere con
  "GET was unable to find an engine" en cuDNN. Torchaudio ≥ 2.9 tampoco sirve (depende de
  torchcodec para guardar). El índice por defecto de PyPI ya no incluye las tarjetas Pascal
  (GTX 10xx); `cu126` sí. Demucs y audio-separator eligen `cuda` solos si está disponible.
- Medido en una GTX 1050 Ti (4 GB), canción de 6,9 min: `htdemucs` 34 s de inferencia (12x
  tiempo real); Roformer 2x tiempo real y casi 4 GB de VRAM; 6 pistas en calidad alta con
  Roformer unos 7 min en total. En 22.04 con kernel 6.8 el driver que compila es
  `nvidia-driver-580` (545 falla); con Secure Boot hay que inscribir la clave MOK al reiniciar.
- Cancelar: `cancel()` hace `kill()` del proceso (el puntero está protegido por `processLock`) y
  el hilo termina en `State::cancelled`, que la UI trata sin diálogo de error.

### 5.4b Analyzer (madmom)
- Python aparte: `~/analisis-env/bin/python` (ajuste `analysisPythonPath`), porque madmom exige
  numpy 1.x y el venv de Demucs usa numpy 2. Instalación validada: `pip install setuptools "numpy<2"
  "cython<3"` (setuptools porque Python 3.12 ya no lo incluye) y luego `pip install --no-build-isolation git+https://github.com/CPJKU/madmom`
  (la versión de PyPI no compila con Python 3.10).
- `run()`: `writeMix` suma las pistas en RAM (unidad, normalizado a 0,9) a `mezcla.wav` y, sin
  las pistas de batería (`isDrumsTrack` por nombre o archivo), a `armonico.wav`; escribe el
  script embebido `analizar.py` y lo ejecuta. El script imprime `progreso=NN` y deja
  `resultado.json`; `parseResult` traduce etiquetas (`A:min` → `Am`, `A minor` → `La menor`) y
  une acordes iguales consecutivos.
- Al aplicar: `bpm` de la canción = tempo detectado, `clickOffset` = primer tiempo de compás,
  `setBeatGrid` al motor, `TimelineView::setAnalysis`. Medido: 61 s para 6,9 min en un i7-9700K.

### 5.4c Loudness (EBU R128)
- `loudness::measure (buffer, fs, tramos)`: ponderación K (biquads derivados para cualquier
  frecuencia, como libebur128), potencia por bloques de 400 ms cada 100 ms, puerta absoluta de
  -70 LUFS y relativa de -10 LU; los tramos usan solo los bloques enteramente dentro. Pico real
  con un sinc enventanado de 4 fases y 12 coeficientes. Verificado con la señal de referencia:
  seno de 997 Hz a -23 dBFS = -23 LUFS.
- `gainToTarget (medición, objetivo, picoMáximo = -1 dBTP)`: dB para llegar al objetivo sin
  superar el pico; 0 si no hay medición. La UI mide en `levelPool` (un hilo aparte del de carga):
  primero cada stem por tramo (`StemLevel`: ganancia = sonoridad del stem en la canción menos la
  del tramo, tope ±12 dB, 0 si el tramo está bajo -50 LUFS o no se pudo medir) y luego la suma
  con esas ganancias, sin normalizar; `applyLeveling` guarda ambas. `sectionIndexAt`,
  `sectionStemGains` y `levelGainAt` sirven al mezclador (`setLevelGains`, `onLevelEdited` →
  `stemLevelEdited`) y a las ondas (`TimelineView::levelGainAt`). `Nivelar setlist` carga cada canción
  no medida con `AudioEngine::loadSong` en ese mismo hilo (`abortJobs` para salir).

### 5.4d TempoMap y Stretcher (tempo y tono)
- `TimeMap::build (info, largoOriginal)`: un `TempoSegment` por sección de tempo
  (`tempoRegions`; sin ellas, una sola con el tempo del análisis o `bpm`) con `origBpm`,
  `playBpm` (`effectivePlayBpm`: el de la sección, si no el de la canción, si no el original),
  `ratio = origBpm / playBpm` y `transpose` (`effectiveTranspose`: el de la sección o el de la
  canción). `toPlayback`, `toOriginal`, `playbackLength`, `isIdentity` (tempo), `hasPitchShift`,
  `isPlain` (nada que renderizar), `segmentAtPlayback`. Los marcadores no afectan al mapa.
- `detectTempoRegions (analysis, bpmRespaldo)`: tempo local entre tiempos consecutivos;
  compara la mediana de los 8 anteriores con la de los 8 siguientes y, donde la diferencia
  supera el 4 %, sitúa el cambio en el intervalo que mejor separa el tempo de antes del de
  después, ajustado al primer tiempo de compás más cercano. El tempo de cada sección es el
  promedio de los intervalos a menos del 25 % de la mediana (los tiempos de madmom van en
  pasos de 10 ms y la mediana sola se desvía hasta un 2 %); las vecinas a menos del 2 % se
  unen. Se llama al aplicar el análisis (se pierden los tempos por sección) y al leer canciones
  antiguas.
- `stretcher::render (fuente, mapa, sr, abort, progreso)`: por pista,
  `SignalsmithStretch<float>` con `presetDefault`, `outputSeek` de pre-roll alineado, bloques
  de 1024 muestras de salida pidiendo la entrada que marca el mapa (más `inputLatency`), y
  `flush` al final; en cada bloque `setTransposeSemitones` con los semitonos del tramo en que
  cae (`segmentAtPlayback`), así el tono cambia en las fronteras de sección. Vistas de
  entrada/salida con ceros fuera del audio, sin copiar. Un mapa `isPlain` devuelve el mismo
  `shared_ptr`.
- En `MainComponent`: `sourceSong` (original) y `currentSong` (lo que suena); `timeMap` es el
  mapa de lo que suena. La carga renderiza antes de mostrar si hay tempo o tono guardados. Los
  cambios de controles piden un render con 800 ms de retardo (`requestRender` / `renderTempo`
  en `loaderPool`), y `songRendered` intercambia con `afterFadeOut` conservando la posición
  original y si estaba sonando. Todo lo que dibuja o salta usa `mappedMarkers()`,
  `mappedAnalysis()`, `mappedTempoBands()` o `timeMap.toPlayback`; lo que guarda usa
  `timeMap.toOriginal`. El análisis y el nivelado se calculan sobre la canción original.
  `tempoBandMenu` edita las secciones (con una sola sección, "Tempo de la sección" es el de la
  canción; "Igualar todas" pone el tempo pedido en la canción y borra los de las secciones);
  `tempoAt` da el tempo original y de reproducción en un instante (campo BPM del click y
  cabecera). `renderTempo` compara el mapa nuevo con el actual (`sameStretch`) y no renderiza
  si no cambia el estirado.
- Costo: Signalsmith en release va a decenas de veces tiempo real por pista; en Debug es muy
  lento. Signalsmith Stretch 1.4.0 trae su dependencia `signalsmith-linear` por FetchContent
  desde su propio CMakeLists (target `signalsmith-stretch`).

### 5.4e Arrangement (arreglo, fase 4)
- `arrangement::render (fuente, clips, sr, abort)`: por pista, un buffer del largo del
  arreglo; cada tramo copia sus muestras a su posición con fundidos lineales de 5 ms en los
  bordes y se **suma** (los solapes son un crossfade, los huecos silencio). Identidad (un
  tramo con todo el audio en 0 o `clips` vacío) devuelve el mismo puntero.
- Edición: `ensureClips`, `cutAt`, `moveClip` (uno o "este y los siguientes", nunca antes de
  0), `removeClip` (con `closeGap` los siguientes se adelantan e informa qué se quitó),
  `joinWithPrevious` (solo si continúan en la fuente y en la posición), `clipAt` (de dos
  solapados manda el último), `insertGap` (parte el tramo que atraviesa el punto y corre lo
  que sigue) y `pasteClip` (copia de un rango de la fuente, superpuesta o insertando).
  `shiftGrid (info, desde, delta)` mueve o recorta tiempos, acordes, marcadores, secciones de
  tempo y `clickOffset` al cerrar un hueco; con delta > 0 abre uno y parte el acorde que lo
  atraviesa. `copyGrid` / `pasteGrid` (`GridSlice`) llevan los tiempos y acordes de un rango
  con el tramo copiado. En MainComponent: `copyClip`, `pasteClipboard` (a la rejilla si el
  modo de corte es rejilla; con inserción añade secciones de tempo si el origen tenía otro
  tempo), `duplicateClip`; `--captura --duplicar=seg`.
- Cadena de render en `MainComponent`: `sourceSong` (archivos) → `arrangedSong`
  (`arrangement::render`, la línea de tiempo de la canción) → `currentSong`
  (`stretcher::render` con el `TimeMap` construido sobre el largo del arreglo). La carga
  hace los tres pasos en el hilo de carga; `renderTempo` vuelve a hacerlos cuando cambian
  los tramos (`renderedClips`, `sameClips`), el mapa o la transposición, reutilizando
  `arrangedSong` si los tramos no cambiaron. Análisis y nivelado se calculan sobre
  `arrangedSong`. `songLengthSeconds()` es el largo del arreglo.
- `findOnset (buffer, sr, alrededor, ventana)`: envolvente RMS en dB por bloques de 1 ms;
  candidato = mayor salto en 3 ms (mínimo 6 dB) penalizado con la distancia al punto pedido
  (hasta 6 dB en el borde de la ventana); el instante devuelto es un bloque antes de que el
  nivel supere en 6 dB el valle previo (hasta 23 ms antes), para cortar antes del golpe. En
  `MainComponent`, `snapCutTime` aplica el modo de corte y `nearestGridTime` da el tiempo
  detectado más cercano o el de la rejilla fija; `onsetNear` usa la pista del carril, o la
  batería si no hay carril (captura).
- Deshacer: `pushUndo` guarda una copia de `SongInfo` antes de cada edición de audio o de
  grilla (menús de tramo, tiempo, acorde y sección de tempo); `undoLastEdit` restaura todo
  salvo `stems` y llama a `refreshFromInfo`. `--captura --corte=T --desplazar=ms
  [--modocorte=1|2|3]` corta (con el modo elegido; imprime dónde cayó el corte) y desplaza
  para revisar el render.

### 5.4f Triggers (golpes y sampler)
- `triggers::detect (buffer, sr, umbralDb, sensibilidad, minMs)`: envolvente RMS en dB por
  bloques de 1 ms; golpe donde sube ≥ 6 dB en 3 ms y supera el umbral; el inicio es el bloque
  anterior a superar en 6 dB el valle previo (como `findOnset`); tras un golpe hay que bajar
  6 dB desde su pico (o pasar `minMs`) para admitir otro; velocidad = (pico − umbral) / (0 −
  umbral), elevada a 1/sensibilidad. `sliceHits` corta una grabación de golpes sueltos en
  muestras (para bancos); `loadBank` lee una carpeta (`trimHit`: cada golpe queda con
  exactamente `preRoll` = 2 ms antes de su ataque, con ceros si faltan, fundidos de 1 y 5 ms,
  tope 4 s, remuestreo, orden por pico) y `SampleBankData::pick` elige por velocidad
  alternando vecinas; `readAudio` y `writeWav` son utilidades.
- En `MainComponent`: `buildSamplers (renderizado, info, sr, carpetaBancos, formatos, caché)`
  corre en el hilo de carga al final de cada render (y solo eso, reutilizando `currentSong`,
  cuando únicamente cambian los triggers: `renderedTriggers`); `applySamplers` lo entrega al
  motor y al mezclador; `syncTriggerMarks` pinta las marcas, los botones T y la casilla del
  trigger de cada canal (`MixerPanel::setTriggerLabel`, texto de `triggerLabelFor`);
  `triggerMenu` (desde el T, la cabecera del carril o la casilla del mezclador:
  `onTriggerClicked`), `setTriggerEnabled`, `importBankFor`, `triggerSettingsDialog`;
  `saveCurrentMix` guarda el canal del sampler en `trigger`. La caché de bancos (`BankCache`) se vacía al importar.
  `--captura --trigger=<banco>` activa el trigger de la batería.

### 5.4g Recorder (grabación de entradas)
- `recorder::writeAligned (cruda, muestraInicial, salida, formatos)`: copia la toma cruda de
  modo que su primera muestra caiga en `muestraInicial` de la canción (silencio delante si es
  positiva; recorte si es negativa). `compensation (latEntrada, latSalida, extraMs, sr)` es lo
  que la toma llega tarde respecto de lo que oyó el músico. `saveBank (grabación, sr, carpeta)`
  = `sliceHits` + `writeWav` como `golpe-NN.wav`.
- En `MainComponent`: `recordDialog (aBanco, pista)` (`ensureInputsEnabled` activa las dos
  primeras entradas con `setAudioDeviceSetup` si no hay ninguna; lista entradas mono y pares
  estéreo entre las activas; escucha por un par de salida o "No escuchar"; compensación extra),
  `startRecording` (wav temporal de 24 bits en `<tmp>/secuencias_grabacion`, `ThreadedWriter`
  con 8 s de cola, `setRecorder`, y `play()` si es una pista), el Timer muestra "GRABANDO
  m:ss · entrada N dB" y cierra la toma cuando la canción se detiene, `stopRecording` (lee
  posición inicial, muestras y latencia, `clearRecorder`, destruye el writer, y en `loaderPool`
  alinea con `startPos - compensation` o corta el banco), `recordingFinished` (agrega el
  `StemInfo` con `songTime` a la canción de origen, por carpeta, y recarga si es la actual; o
  vacía la caché de bancos y asigna el banco al trigger de la pista). Cambiar de canción cierra
  y guarda la toma; cerrar la app la descarta.

### 5.4h Instruments (rack VST3/AU)
- `InstrumentRack (motor, carpetaDeAjustes)`: `formats.addDefaultFormats()` (VST3; AU en macOS),
  `known` (`KnownPluginList`, persistida en `plugins.xml`), `slots` (id estable, `PluginDescription`,
  `lane`, ventana del editor, ganancia/mute/salida, `savedState`). `restore()` lee
  `instrumentos.xml` (`<INSTRUMENTO id gainDb muted outputPair estado=base64><PLUGIN/>`), aplica
  el "dead man's pedal" (`plugin-en-prueba.txt`: el plugin que tumbó la app en una búsqueda
  queda vetado) y carga cada plugin con `createPluginInstanceAsync`; `makeLane` pone el estado,
  `prepareToPlay (sr, maxBlockSize)`, latencia, buffer y cola MIDI; `publish()` arma un
  `InstrumentSet` nuevo con las líneas cargadas (compartidas por `shared_ptr`: quitar una no toca a
  las demás) y llama a `onChanged`. `save()` (al cargar, quitar, cerrar el editor y al salir)
  guarda `getStateInformation` y la mezcla de cada línea. `openEditor` usa
  `createEditorIfNeeded` o `GenericAudioProcessorEditor` en una `DocumentWindow` que al cerrarse
  se destruye en el siguiente mensaje. `prepareAll (sr)` saca el conjunto del motor, vuelve a
  preparar y publica (lo llama el Timer con `needsReload`). `scan` (hilo `Scanner`) recorre
  `getDefaultLocationsToSearch()` de cada formato más las carpetas extra; `describeFile` registra
  los plugins de un archivo concreto (`--captura --instrumento=<ruta.vst3>` lo usa y asigna el
  instrumento al trigger de la batería). Callbacks tardíos se protegen con un `weak_ptr` (`alive`).
- En `MainComponent`: `rack` se crea tras abrir el dispositivo y restaura en el primer mensaje;
  `rack->onChanged` refresca los canales del mezclador (`MixerPanel::setInstruments`, nombre
  clicable → `openEditor`), `refreshReplaced` (la pista solo se silencia si el instrumento existe) y
  las casillas; `instrumentsMenu` (cargados con abrir/quitar, "Añadir instrumento" de los
  conocidos, buscar instalados o en una carpeta, cancelar) y `scanInstruments`; `buildSamplers`
  crea para `"vst:<id>"` una línea con `instrumentId` y `note` (sin banco, sin canal propio en el
  mezclador); el menú del trigger lista los instrumentos (ids 300+) y "Añadir o buscar
  instrumentos..." (298). El destructor cierra editores, guarda y destruye el rack después de
  quitar el callback de audio.

### 5.4j Pistas MIDI (golpes congelados y editables)
- `miditrack::fromDetection (eventos, sr, stem)`: una fila con el sonido y la nota del trigger del
  stem, golpes con `velocityFromStrength` (la misma velocidad que manda el trigger en vivo a un
  instrumento: 127 · (0,25 + 0,75 · fuerza)). Edición pura y probada: `addHit`, `removeHits`,
  `moveHits` (tiempo y filas; devuelve los índices nuevos para conservar la selección),
  `setNote`, `changeVelocity` / `setVelocity`, `quantize` / `snapToSubdivision` (subdivisiones de
  los tiempos detectados), `replaceRange` (volver a detectar un tramo), `removePad`,
  `eventsForNote` (al motor, con el `TimeMap`), `drumNoteName` (General MIDI en español).
- En `MainComponent`: `applyMidiTracks` arma las vistas (`TimelineView::setMidiLanes`, cada pista
  bajo su carril de origen, tiempos de reproducción), las líneas (`engine.setMidiSamplers`, una
  por fila con sonido; bancos por `bankFor`, de la caché) y los canales (`MixerPanel::setMidiSamplers`),
  y `refreshReplaced` calla la pista de origen si corresponde. Lo llama `applySamplers` (tras cada
  carga o render, porque `setSong` descarta el conjunto) y `midiEdited` (tras cada edición: guarda,
  aplica y conserva la selección). `createMidiTrack` detecta sobre `arrangedSong` con los ajustes
  del trigger y crea la pista; mientras exista una pista MIDI de un stem (`isMidiSource`), su
  trigger en vivo queda reemplazado sin apagarse (`buildSamplers` lo salta y `triggersOf` lo cuenta
  como apagado, así crear o quitar la pista MIDI vuelve a armar los triggers, y deshacer devuelve
  el trigger; hasta ese render, `applyMidiTracks` pone `superseded` en su línea). `pullMidiControls`
  pasa los canales de las filas a `SongInfo` antes de rehacer las líneas y antes de `pushUndo`, solo
  a la fila de la que salió cada línea (`padForLane`: la misma canción, `midiSamplersFolder`, y la
  pista con el mismo origen, fila y nota; tras quitar una fila o una pista, o al cambiar de canción,
  los índices ya no sirven). `undoLastEdit` conserva lo que no se deshace: la mezcla de las filas,
  sus nombres y sonidos, el nombre y los silencios de la pista. El solo se conserva por pista y
  nota. Una pista silenciada arma sus líneas con `silenced`. `arrangementEdited` vuelve a aplicar
  las pistas MIDI al instante (los golpes ya se movieron con los tramos y los índices de la vista
  deben seguirlos); crear una pista MIDI y volver a detectar esperan a que el arreglo que suena sea
  el de `SongInfo` (`arrangementReady`). `refreshReplaced` calla el stem de origen solo si alguna fila suena de verdad.
  Las ediciones de tramos mueven los golpes con el audio: `arrangement::moveMidiHits` (desplazar,
  alinear, Shift + arrastre; con el delta que `moveClip` aplica de verdad), `removeMidiHits`
  (eliminar dejando silencio), `midiHitsToSource` (restaurar el original), `copyMidiHits` /
  `pasteMidiHits` (copiar, pegar y duplicar); cerrar o abrir huecos los mueve `shiftGrid`. Callbacks de la vista (`onMidiAdd`,
  `onMidiMove`, `onMidiVelocity`, `onMidiDelete`, menús `midiHitsMenu`, `midiPadMenu`,
  `midiHeaderMenu`, `snapMidiTime`); cada edición hace `pushUndo` (Ctrl+Z la deshace; `refreshFromInfo`
  vuelve a aplicar). Supr borra los golpes elegidos. `saveCurrentMix` guarda los canales en los pads.
  `importBank (alTerminar)` importa un banco para un trigger o para una fila. `--captura --pistamidi`
  crea la pista MIDI de la batería con una fila de conga. `auditionMidiPad` / `auditionTrigger` piden
  el golpe al motor y avisan en la barra de estado qué sonó o por qué no (sin sonido, silenciada,
  instrumento no cargado). Los menús de nota del trigger y de la fila se arman con `addNoteItems`:
  General MIDI y el submenú "Addictive Drums 2" con `miditrack::addictiveDrumsMap` (el mapa "AD2
  Standard" por grupos, según el keymap de XLN); ids `noteMenuBase` + nota (1000+, lejos de los demás
  ids de esos menús).
- `TimelineView`: la pista MIDI (`MidiLane`) es un carril más del `Viewport` (22 px de título +
  16 px por fila, hasta 220), con sus gestos y su selección; avisa con índices de
  `MidiLaneView::hits`. Es opaca y se dibuja en una imagen en caché (`setBufferedToImage`), así el
  cabezal a 30 Hz no la vuelve a pintar; tras una edición, la selección se reubica por tiempo y
  nota (`remapSelection`). Clic en el nombre de una fila elige todos sus golpes; los golpes cuya
  nota no tiene fila se ven en gris en la franja del título. Para mover en el tiempo hay que
  desplazarse al menos 4 px de lado (si no, solo cambia de fila). El triángulo a la izquierda del
  nombre de cada fila (`isOnPlayButton`, solo con filas de 11 px o más) llama a `onMidiAudition`.

### 5.4i MixProject y MixEditor (armar mix antes de separar)
- Disco: `<raíz>/_mixes/<carpeta>/mix.json` (`{ name, bpm, keepTempos, levelLufs, sources: [{ name, file, length,
  analysis }], segments: [{ source, start, end, label, playBpm, transpose, gainDb, fadeBeats }] }`),
  `fuentes/` (copias de las canciones originales) y `mezcla.wav` (último render, 44,1 kHz, 24 bits).
  La carpeta es la identidad del mix (`Library::legalFolderName`: sin puntos ni espacios al final,
  que Windows quitaría); el nombre visible es el `name` de mix.json (renombrar no mueve la carpeta)
  y `MixProject::entries` lo lee para el menú "Armar mix" (abrir y borrar van por carpeta; borrar
  usa `Library::sendToTrash`, con `gio trash` en Linux).
  `Library::isReservedFolder` hace que `load()` no tome `_mixes` ni `_bancos` como canciones; el
  análisis se guarda con `Library::analysisToVar` / `analysisFromVar` (el mismo formato de song.json).
- Tiempo: un tramo va de un tiempo detectado (`start`) al tiempo siguiente al último incluido
  (`end`), en segundos de su fuente. `mix::segmentBpm` = 60·(l−f)/(b_l−b_f) con los tiempos dentro
  del tramo; `ratio = srcBpm / playBpm` (constante por tramo); duración en el mix
  (end−start)·ratio, que con start y end en tiempos da exactamente sus tiempos al tempo del mix.
  `mix::layout` encadena los tramos (`outStart` = `outEnd` del anterior) y calcula los fundidos:
  el que entra lee su audio previo y sube con un seno en [J−F, J]; el que sale baja con un coseno
  en el mismo intervalo y termina en J (F = fadeBeats·60/playBpm, o 10 ms; acotado por el audio
  previo disponible). Sin análisis, ratio = 1.
- `mix::render` lee de cada fuente solo su tramo (`readRange`, con remuestreo sinc a 44,1 kHz),
  lo estira y transpone con `stretcher::renderBuffer` y un `TimeMap` de una sección (o lo copia si
  no hace falta), aplica ganancia y fundidos y lo suma. Con `levelLufs` < 0 (0 = sin nivelar; los mixes
  guardados sin el campo quedan así; `MixProject::create` pone `defaultLevelLufs` = -14), antes de los
  fundidos mide el cuerpo del tramo (desde la unión, sin el audio previo del fundido) con
  `loudness::measure` y le suma `gainToTarget (medida, objetivo, -1 dBTP)` acotado a ±12 dB; lo
  aplicado sale en `MixLevel` (`render (..., &levels)`: dB, sonoridad resultante, `peakLimited`).
  `renderMix` (MainComponent) mide además el mix entero para el pie y pasa los niveles al editor
  (`MixEditor::setSegmentLevels`, que `changed()` vacía en la próxima edición). `mix::describeSong` arma la canción: tiempos
  y acordes llevados al mix con `toMix` (acordes y tonalidad transpuestos con
  `music::transposeChord` / `transposeKey`), una sección de tempo por tramo (unidas si coinciden),
  un marcador por tramo, `bpm` y `clickOffset`.
- `MixEditor` (no toca el motor ni archivos): cabecera, fuentes, vista de la fuente (onda con
  compases, selección ajustada con `snapToBeat` + `refineCut`), línea del mix y panel del tramo;
  edita el proyecto en el hilo de mensajes y avisa con `onChanged`; pide lo demás con callbacks.
  Selección: arrastrar en un hueco la rehace (`dragSelection` / `finishSelection`); `SourceWaveView::edgeAt`
  reconoce un borde (6 px de su línea o su asa de 9 px en la regla; en una selección angosta manda la mitad
  del clic) y `dragEdge` / `finishEdge` mueven solo ese borde y ajustan a la transiente solo ese;
  `extendSelectionTo` (Shift + clic), `setEdgeAt` (menú) y `applySelection` (pone, ajusta los bordes
  pedidos, lleva el cabezal al inicio y la muestra). Los campos de la cabecera (`selFromEdit`,
  `selToEdit`, `updateSelectionFields`, `selectionFieldEdited`) usan `mix::barCount` y `mix::barRange`
  (compases first a last: del primer tiempo de first al de last + 1, o al final; 0 = desde el inicio);
  `selectionFirstBar` mira 0,1 s después del inicio (el corte ajustado queda unos ms antes del 1).
  `MixEditor::selectBars` hace lo mismo desde fuera (`--captura --mix=... --mixsel=5-8`). El selector
  `levelBox` (cabecera) edita `levelLufs`; `levelInfo` (panel) muestra el `MixLevel` del tramo elegido.
  En la captura, `--mixtramos` también renderiza el mix (se ve el nivelado).
- En `MainComponent`: `openMix` descarga la canción (`unloadSong`; se recuerda por carpeta en
  `mixReturnFolder`, que se conserva al pasar de un mix a otro) y pone el editor encima del área
  de la canción; no se abre mientras la canción se analiza o se nivela (el resultado se aplica a
  la canción cargada). `closeMix` lo quita y vuelve a la canción. Las fuentes se copian en
  segundo plano (`addMixSourceFiles`, también al arrastrar archivos sobre la ventana con el mix
  abierto) y se analizan en cola (`queueMixAnalysis`: se leen a 44,1 kHz en `mixPool` y van a
  `mixAnalyzer`; `mixAnalyzerFile` es la fuente para la que se arrancó, y solo sus estados finales
  se aplican: un análisis cancelado de un mix cerrado se descarta con `reset` al pedir el
  siguiente; el Timer aplica el resultado por nombre de archivo, también a los estados de
  deshacer); las formas de onda se calculan en `mixPool` (`mix::computePeaks`, 200 por segundo,
  abortable). `mixPool` es un hilo aparte de `loaderPool` para que la escucha no espere detrás.
  `renderMix` renderiza en `loaderPool` si `mixRenderedVersion != mixVersion` (si algún pico pasa
  de 1, baja todo el mix por igual antes de escribirlo, para no recortar en 24 bits) y guarda
  `mixRenderedInfo` (describeSong del render); `playMix` / `playMixSource` cargan el render o la
  fuente con `AudioEngine::loadSong` a la frecuencia del dispositivo y `setMixPreview` los pone en el
  motor con fundido, con su `BeatGrid` y el click si está pedido; la fuente que suena se guarda
  por archivo (`mixPreviewSourceFile`), y `mixPlayRequest` hace que una carga vieja (tras otra
  escucha, un Stop o un cierre) no toque el motor; `mixPreviewVersion` se fija recién al instalar
  el render, y si lo cargado quedó a otra frecuencia del dispositivo se vuelve a pedir. Pedir
  escuchar el mix mientras se prepara (por otra escucha o por "Crear canción") queda en
  `mixWantedPlay` y suena al terminar el render. Al abrir el mix se quita la selección del
  setlist (un clic en cualquier canción, también la de antes, cierra el mix y la carga). Si cambia la frecuencia del dispositivo, `dropMixPreview` descarta lo
  cargado. `mixGeneration` (atómico) invalida los trabajos de un mix cerrado. `createSongFromMix` / `startMixSong`: separar copia
  el render a un temporal (`pendingMixInput`), guarda `pendingMixSong` y llama a
  `startSeparation (archivo, nombre)`; al terminar la separación, `applyMixSongInfo` pone los
  metadatos en la canción importada; con el mix abierto la canción nueva no se selecciona (no se
  cierra el mix bajo el usuario). Si el mix se editó mientras se preparaba, `startMixSong` vuelve
  a renderizar antes de crear la canción. Sin separar: `importStemFiles ({ mezcla.wav })` y lo
  mismo. Elegir una canción del setlist cierra el mix. `--captura --mix=<nombre> [--mixfuentes=/a.wav,/b.wav]
  [--mixtramos] [--mixcancion=1|2] [--mixsel=5-8]` (rutas sin espacios).

### 5.5 UI (MainComponent)
- **Foco de teclado**: ningún hijo acepta foco (`disableFocus()` recursivo, y también en los
  strips y marcadores que se crean después). Así todas las teclas llegan a
  `MainComponent::keyPressed`. El Timer recupera el foco si no hay ventanas modales **y el foco
  no lo tiene un `TextEditor`** (los cuadros de valor de los faders y del BPM abren uno al hacer
  clic; sin esa excepción no se podría escribir en ellos). Si agregas un control nuevo, ponle
  `setWantsKeyboardFocus(false)`, salvo los TextEditor dentro de diálogos modales.
- `keyPressed` ignora la autorrepetición: guarda las teclas pulsadas en `heldKeys` y las limpia
  al soltar (`keyStateChanged`), al perder el foco y, desde el Timer, cuando
  `KeyPress::isKeyCurrentlyDown` dice que ya no están.
- El setlist es un `ListBox` con `MainComponent` como `ListBoxModel`. Cargar una canción =
  `setlist.selectRow(i)` → `selectedRowsChanged` → `loadSongAt`. El clic derecho también
  selecciona la fila, pero `selectedRowsChanged` lo ignora (`isPopupMenu`) y el menú, al
  cerrarse, devuelve la selección a la canción cargada (`restoreSetlistSelection`).
- `loadSongAt` / `unloadSong`: actualizan la UI, hacen `engine.pause()` y con `afterFadeOut`
  esperan a que `engine.isSilent()` (o 100 ms si el dispositivo no corre) antes de
  `setSong(nullptr)` y de encolar la carga. Nunca se corta el audio de golpe. El job de carga
  captura `std::bad_alloc` (el `ThreadPool` de JUCE se traga las excepciones y la UI quedaría
  en "Cargando..." para siempre) y devuelve el error a `songLoaded`.
- `saveCurrentMix()` copia el estado de las pistas cargadas a `SongInfo` (solo si `loadedFolder`
  coincide con la canción seleccionada) y lo guarda. Se llama al cambiar de canción y al cerrar;
  además `markSongDirty()` (faders, mute, salida, click) hace que el Timer guarde medio segundo
  después del último cambio. Los marcadores se guardan al instante.
- La configuración del dispositivo de audio se guarda al cambiarla (`changeListenerCallback`),
  no solo al cerrar. Si `AudioDeviceManager::initialise` falla, se avisa al arrancar.
- Al terminar una separación o una importación con una canción sonando, no se cambia de
  canción: solo se agrega al setlist.
- `TimelineView` no toca el motor: el Timer le pasa posición, estado y loop (`setPosition`,
  `setPlaying`, `setLoop`) y ella avisa con `onSeek`, `onMarkerClicked`, `onMarkerMoved`,
  `onTempoBandClicked`, `onBeatClicked` (clic derecho sobre un tiempo detectado en la fila
  de compases), `onChordClicked` (clic derecho en la fila de acordes), `onNoteClicked` (clic
  derecho en la fila de notas), `onLaneMenu` (clic
  derecho sobre un carril), `onClipDragged` (Shift + arrastre sobre un carril), `onMute`,
  `onSolo`. `setClips` recibe los tramos en tiempo de reproducción; la capa `Overlay` dibuja
  sus bordes, los huecos y el tramo que se está arrastrando. La regla (`Ruler`, 82 px) tiene filas de marcadores,
  secciones de tempo (`setTempoBands`, en tiempo de reproducción), compases, tiempo y
  acordes. El análisis que recibe (`mappedAnalysis`) ya viene en tiempo de reproducción y con
  los acordes y la tonalidad reescritos según `useFlats()`; los índices de los tiempos
  coinciden con los de `analysis.beats` originales. `keyMenu` (clic en `keyLabel`, que tiene a
  MainComponent como MouseListener), `beatMenu`, `chordMenu` y el submenú de click de
  `tempoBandMenu` editan `SongInfo` y llaman a `analysisEdited` (guardar, regla, `BeatGrid`,
  cabecera). `sectionRange` da los límites de una sección en tiempo original.
  Las formas de onda se dibujan en una imagen por carril que solo se
  regenera al cambiar zoom, desplazamiento o tamaño; el cabezal va en una capa aparte que
  repinta solo su franja. Los carriles van en un `Viewport` vertical (mínimo 44 px cada uno).
  `MixerPanel` recibe `fillOutputBox` de MainComponent y sincroniza M/S con los atómicos en
  cada tick, igual que los carriles (`refreshTrackStates`).
- `--captura=archivo.png [--cancion=N] [--vivo] [--analizar] [--nivelar] [--separar] [--selector] [--posicion=seg] [--escenario=guia.png] [--tempo=BPM] [--tono=N] [--corte=seg --desplazar=ms --modocorte=N]`
  (Main.cpp): herramienta de desarrollo que permite una segunda instancia, espera a que cargue
  la canción (y a que termine el análisis, el nivelado o el render pedidos) y guarda
  `createComponentSnapshot`.
- Diálogos: siempre asíncronos (`AlertWindow` + `enterModalState(..., deleteWhenDismissed=true)`,
  `showMessageBoxAsync`, `FileChooser::launchAsync`). Nunca uses modales bloqueantes.
- Selector de archivos: `pickFiles (título, flags, carpeta, patrones, callback)`. En macOS usa
  el `FileChooser` nativo; en Linux y Windows, `FilePicker`, un panel **dentro de la ventana**
  con un `FileBrowserComponent` y Aceptar / Cancelar. Motivo: cualquier ventana nueva (zenity,
  el selector de JUCE) se abría a veces detrás de la principal por la prevención de robo de
  foco de GNOME y, al ser modal, la app parecía colgada. Mientras el panel está abierto, el
  Timer no roba el foco y `keyPressed` le pasa las teclas (Esc cancela, Enter acepta). Con
  `canSelectDirectories` y nada marcado devuelve la carpeta abierta. `--captura --selector`
  lo muestra.

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
- Jobs del `ThreadPool`: JUCE captura cualquier excepción con un `jassert` y el job muere en
  silencio. Si el job debe avisar a la UI, captura tú la excepción (ver `loadSongAt`).
- `juce::File(String)` con una ruta relativa dispara un assert en Debug: valida que empiece
  con `/` (ver `startSeparation`).
- m4a/AAC solo se decodifica en macOS (`CoreAudioFormat`); en Linux `Library::isAudioFile` lo
  excluye. Usa `Library::audioFilePatterns()` en los selectores de archivos.
- **Terminal de VS Code instalado como snap**: los procesos lanzados desde ahí heredan una
  docena de variables del snap (`GTK_PATH`, `GDK_PIXBUF_MODULE_FILE`, `GTK_IM_MODULE_FILE`,
  `GIO_MODULE_DIR`, `XDG_DATA_DIRS`, `LOCPATH`...) apuntando a `/snap/code/...`, y zenity muere
  al arrancar con un "symbol lookup error" (con `env -i` funciona). Con el selector nativo de
  JUCE el botón "parecía no hacer nada"; por eso se usa el selector propio de JUCE. Python y
  `gio` sí funcionan con ese entorno. Para reproducir el comportamiento real del usuario, lanza
  la app desde una terminal normal o desde el menú.
- Parámetros que cambian en vivo: siempre con rampa o fundido (ver `smoothedGain` y `fade`).
- **Rutas y ejecutables por plataforma**: nunca compruebes rutas absolutas con `startsWith ("/")`
  (en Windows empiezan con `C:\`): usa `juce::File::isAbsolutePath`. Los venvs tienen
  `bin/python` en Unix y `Scripts\python.exe` en Windows (`venvPython`, `audioSeparatorExe`);
  el `PATH` se separa con `;` en Windows y `ffmpeg` es `ffmpeg.exe` (`ffmpegInPath`).
- **MSVC y las init-captures con `this`**: `[sp = SafePointer<MainComponent> (this)]` dentro de otra
  lambda que ya capturó `this` no compila en Visual Studio (toma `this` como el closure). Crea el
  `SafePointer` en una variable local y captúrala por valor. Tampoco acepta que una lambda use una
  constante local (`constexpr int bins = 200;`) sin capturarla, aunque GCC y Clang sí: decláralas
  `static constexpr`. En la CI de Windows los pasos corren en bash (`defaults: run: shell: bash`)
  para que un error de compilación corte el paso; en pwsh quedaba tapado por el último comando.
- **Nombres globales que chocan con los SDK de macOS**: los headers de JUCE en Mac arrastran
  Carbon/CoreServices, que define `struct Marker` (AIFF.h), `Rect`, `Point`, `Comment`,
  `Fixed`, `Style`, `Cell`... Los tipos del modelo van con nombre propio (`SongMarker`,
  `SongInfo`, `TempoRegion`, `Clip`) o dentro de un namespace. La compilación en Mac solo se ve
  en GitHub Actions, así que revisa el nombre antes de subir.
- **Plugins**: se crean, editan y destruyen solo en el hilo de mensajes; el motor únicamente
  llama a `processBlock` (el plugin es responsable de su tiempo real). Nunca destruyas un
  `AudioPluginInstance` con su editor abierto. En CMake, la propiedad `JUCE_PLUGIN_ARTEFACT_FILE`
  de un target `_VST3` trae genexes anidados: léela con `$<GENEX_EVAL:...>`.
- El estilo del código es parecido al de JUCE (llaves en línea propia, espacio antes de `(`,
  4 espacios). Hay un `.clang-format`, pero no reformatees archivos completos sin motivo.
- No agregues dependencias de sistema nuevas sin actualizar el README, `install.sh` y esta guía.
  El binario de release hoy solo necesita libasound, fontconfig, freetype y libstdc++.

## 7. Limitaciones conocidas

- Todo el audio va a RAM: unos 23 MB por minuto por stem a 48 kHz (5 min × 8 stems ≈ 1 GB).
- El cambio de tempo y tono se renderiza entero antes de sonar (no es en tiempo real): con la
  build de release tarda unos segundos por canción; con la de Debug, minutos.
- Click en 4/4 fijo; sin cuenta regresiva (count-in).
- Sin MIDI (solo teclas; los pedales que emulan teclado funcionan).
- Al cambiar de canción se detiene la reproducción (con fundido); no hay avance automático.
  Un pedal mal pisado (AvPág) igual cambia de canción: falta una opción "solo cuando está
  detenido".
- Si se arrastra a la ventana un solo archivo, siempre se separa (no pregunta).
- Con JACK, JUCE conecta las salidas del cliente a los puertos físicos al abrir el dispositivo;
  la app no elige puertos por nombre (PipeWire puede reconectarlos).
- La carga de una canción tarda: unos 190 ms por cada 4 stems de 3 minutos a la frecuencia del
  dispositivo, y unos 760 ms si hay que remuestrear (medido con -O2). No hay precarga de la
  siguiente canción.
- Probado con tests y en Xvfb (sin tarjeta de sonido real); falta probar con interfaces
  multicanal reales.
- Pistas MIDI: un banco nuevo se carga en el hilo de mensajes la primera vez que una fila lo usa
  (un instante con bancos grandes); las voces de un sonido cambiado se cortan si hay otro cambio
  antes de que se apaguen (hasta 4 s); con un instrumento con latencia, los golpes que caen dentro
  de esa latencia tras un salto salen juntos, algo tarde. Mover tramos del arreglo mueve los
  golpes del rango completo del tramo (también los de un hueco dentro de "este y los siguientes").
- Instrumentos: solo instrumentos (no efectos), una salida estéreo por plugin (las salidas
  múltiples de Addictive Drums se ignoran), sin `AudioPlayHead` (no reciben tempo ni posición),
  solo noteOn/noteOff (sin CC ni aftertouch). La búsqueda de plugins es en el mismo proceso:
  un plugin roto puede cerrar la app durante la búsqueda (a la siguiente queda vetado). Los
  plugins con latencia se compensan solo en los golpes del trigger. Los nombres de las notas no se
  leen del plugin (VST3 `IUnitInfo` o MIDNAM en AU): hay listas fijas (General MIDI y el mapa "AD2
  Standard" de Addictive Drums 2, transcrito del keymap de XLN y sin verificar en un AD2 real) y
  "Escuchar" para comprobar; el contenido de los Flexi depende del kit cargado.
- Armar mix: cada tramo se estira con una razón constante (conserva el pulso de la grabación
  dentro del tramo; una canción tocada sin click no queda cuantizada); la exactitud de las uniones
  depende del análisis de madmom (los menús de la fuente corrigen tiempos a la mitad, al doble o
  el 1 del compás). Al crear la canción, la copia del render para separar y la importación sin
  separar se hacen en el hilo de mensajes (un instante con mixes de varios minutos).
- Grabación: la alineación confía en las latencias que informa el dispositivo (con PipeWire
  o ALSA suelen ser correctas; si no, está la compensación extra en ms). Saltar con el cabezal
  durante una toma desalinea lo grabado después del salto (la toma se alinea por su inicio).
  Solo con tempo y tono originales; cerrar la app descarta la toma en curso.

## 8. Hoja de ruta acordada (vista DAW)

Acordado con el usuario el 2026-09-24. Objetivo: que la ventana funcione como un DAW simple
(sin plugins) sin perder el modo en vivo. Decisiones tomadas:
- **Edición**: el arreglo se edita a nivel de canción (cortar, eliminar, mover o duplicar
  tramos afecta a todos los stems a la vez, así nunca se desalinean) y además se pueden
  silenciar tramos por pista. No hay clips independientes por pista.
- **Tempo**: cambios de BPM en varios puntos de la canción (mapa de tempo) que estiran el
  audio **sin cambiar el tono**. La transposición existe aparte, solo si el usuario la pide.
  Sirve para igualar el BPM de varias canciones y mezclarlas.
- **Acordes**: basta con lo que dan los modelos abiertos (mayor, menor, séptimas).
- El motor sigue reproduciendo buffers ya renderizados en RAM: los cambios de tempo, tono y
  arreglo se renderizan en segundo plano y se intercambian con fundido. Nada nuevo en el hilo
  de audio salvo atómicos.

Fases, en orden:
1. **Vista de arreglo y mezclador** (`TimelineView`, `MixerPanel`, `Meters`): pistas
   horizontales con forma de onda (caché de picos calculada al cargar), regla en compases y
   tiempos a partir del BPM y del inicio del click, marcadores sobre la regla (clic para saltar,
   arrastrar para mover), cabezal, loop sombreado, zoom y desplazamiento, seguimiento del
   cabezal; mezclador con medidores de pico y RMS por canal con retención de pico y escala en
   dB, fader maestro y medidor maestro; botón "En vivo" que oculta la vista de arreglo y
   agranda la información para pedal.
2. **Análisis musical**: script Python en el mismo venv que detecte tempo, tiempos y compases,
   y acordes (sobre la mezcla sin batería ni voces). Se guarda en `song.json` (`beats`,
   `chords`, `tempoMap`, `key`) y se dibuja: compases reales en la regla, pista de acordes,
   acorde actual en grande en modo en vivo, BPM detectado como BPM del click y por sección.
3. **Tempo y tono** (hecho): mapa de tempo editable (BPM original y BPM de reproducción por
   tramo) y transposición en semitonos, con Signalsmith Stretch (MIT). Render en segundo plano
   a RAM por pista; el motor sigue leyendo buffers. Las posiciones de marcadores, acordes y
   tiempos se convierten con un mapa tiempo original → tiempo de reproducción.
4. **Edición** (en curso): `song.json` guarda el arreglo como lista de tramos del audio
   original (`clips`) colocados en la línea de tiempo. Hecho: cortar (todos los stems), mover
   (en ms, al tiempo más cercano o con Shift + arrastre), eliminar (con o sin cerrar el hueco),
   unir, restaurar, copiar, pegar (insertando o encima), duplicar, deshacer. Pendiente:
   rehacer, silenciar tramos por pista, recorte de inicio y fin como gesto directo.

**Plan de estudio en vivo** (acordado el 2026-09-27): 1) triggers de las pistas separadas con
sampler interno y bancos grabados (hecho, v0.4.0), 2) grabación de entradas como pistas nuevas y
como bancos (hecho, v0.4.1), 3) instrumentos VST3/AU disparados por los triggers (hecho, v0.5.0),
4) disparo desde un micrófono en vivo (no hace falta por ahora).

Pendientes de antes que siguen vigentes: probar con interfaces multicanal reales, control
MIDI, precarga de la siguiente canción, opción "cambiar de canción solo cuando está detenido",
avance automático, count-in y compases distintos de 4/4 en la rejilla fija, AppImage, y
verificar el DMG en un Mac real (el flujo de Actions se escribió desde Linux).

## 9. Bitácora

- **v0.1**: primera versión para macOS: motor, setlist, mezclador, marcadores, click, Demucs.
- **v0.2**: Ubuntu como plataforma principal: JACK/ALSA, `install.sh`, `.desktop`, papelera con
  `gio trash`, selector de archivos de JUCE en Linux, textos ASCII en los botones, ajustes en
  `~/.config/Secuencias`, `Library` acepta una carpeta raíz, tests con CTest, CMake Presets y
  configuración de VS Code.
- **v0.7.1**: selección del armado de mix: cada borde se arrastra solo (asas en la regla), Shift + clic,
  "Inicio / Fin del tramo aquí" en el menú y compases escritos en la cabecera ("Compases [8] a [16]");
  antes, arrastrar desde un borde empezaba una selección nueva y el inicio saltaba al clic.
  `mix::barCount`, `mix::barRange`, `MixEditor::selectBars`, `--mixsel`. Nivelado del mix por tramo a
  -12/-14/-16/-18 LUFS (`MixProject::levelLufs`, `MixLevel`, -14 en los mixes nuevos, sin pasar de
  -1 dBTP), selector en la cabecera, "Niv" en el panel y sonoridad del mix en el pie.
- **v0.7.0 (pistas MIDI)**: `MidiTracks` (golpes congelados y editables, filas con sonido propio),
  `SongInfo::midiTracks` en song.json, `shiftGrid` los mueve, segundo conjunto de líneas en el
  motor (`setMidiSamplers`, `rawVelocity`), pista MIDI editable en la vista de arreglo, canales
  por fila en el mezclador, menús de golpes, filas y pista, "Crear pista MIDI con estos golpes"
  en el menú del trigger, `importBank`, `--captura --pistamidi`. "Escuchar" por fila y en el menú del
  trigger (`AudioEngine::audition`: voces sin la envolvente del transporte, compuerta del instrumento)
  y el mapa "AD2 Standard" de Addictive Drums 2 en los menús de nota (`addictiveDrumsMap`, `addNoteItems`).
  Revisión: canales de las filas por identidad (`padForLane`; no pasan a otra canción ni a otra fila),
  `superseded` y `silenced` en el motor, relevo por nota y origen, fundido de "Escuchar" con pause y
  stop (`isSilent` lo espera), nota de "Escuchar" tras abrir la compuerta, la vista MIDI se rehace tras
  editar tramos, `arrangementReady`, deshacer conserva nombres, sonidos y silencios, restaurar el audio
  original deja un golpe por instante, el portapapeles de tramos es de su canción, silenciar una fila
  desde su menú se mantiene, el desplazamiento de varios golpes se convierte con el arrastrado.
- **v0.6.0 (armar mix antes de separar)**: `MixProject` (fuentes, tramos, `mix.json`, `layout`
  con razón constante por tramo y uniones en la rejilla, fundidos seno/coseno, `render`,
  `describeSong`, `readRange`, `refineToOnset`, `computePeaks`), `MixEditor` (sección con
  fuentes, vista de la fuente, línea del mix y panel del tramo), integración en `MainComponent`
  (botón "Armar mix", análisis en cola con `mixAnalyzer`, render y escucha con click, deshacer,
  crear la canción separando o sin separar con sus tiempos, secciones y marcadores),
  `Library::analysisToVar/FromVar` e `isReservedFolder`, `music::transposeChord/transposeKey`,
  `startSeparation (archivo, nombre)`, `--captura --mix`. Revisión: estado del analizador del mix
  por fuente (`mixAnalyzerFile`), `mixPool`, `mixReturnFolder`, `mixPlayRequest`, fuentes
  arrastradas al mix, sin saturar el render, tiempos pegados a la unión (el "1" que queda justo
  antes de un corte ajustado a la transiente) y deduplicación en uniones a medio tiempo.
  Segunda ronda: `mixWantedPlay`, versión de la escucha al instalar, frecuencia del dispositivo,
  `MixProject::entries` (nombre visible en el menú), `Library::sendToTrash` público y
  `legalFolderName` (también para las canciones importadas), selección del setlist con el mix abierto.
  Arreglado de paso: el analizador escribía su script de Python sin convertirlo desde UTF-8.
- **v0.5.1**: el diálogo "Grabar" ofrece los dos destinos (pista nueva o banco de muestras);
  `SampleBankData::preRoll` y `trimHit` uniforme para que el ataque de cada muestra caiga
  exacto sobre el golpe detectado (el motor adelanta las voces el pre-roll).
- **v0.5.0 (instrumentos VST3/AU, punto 3)**: `Instruments` (`InstrumentRack`: búsqueda con
  `PluginDirectoryScanner` en un hilo, carga asíncrona, estado en `instrumentos.xml`, editor en
  ventana propia, `prepareAll`), `InstrumentLane`/`InstrumentSet` en el motor (MIDI desde los
  golpes con compensación de latencia, note-offs, `processBlock` por trozo, canal en el
  mezclador), `SamplerLane::instrumentId` y `note`, botón "Instrumentos" y su menú, instrumentos
  en el menú del trigger (`"vst:<id>"`), canales lila en el mezclador con nombre clicable,
  `JUCE_PLUGINHOST_VST3` (y `_AU` en macOS), VST3 de prueba (`Tests/TestSynth.cpp`) y test del
  host, `--captura --instrumento=<ruta>`, `xvfb-run` en la CI.
- **v0.4.1 (grabación de entradas, punto 2)**: `RecordSetup` / `setRecorder` /
  `processInput` en el motor (cola sin bloqueo, escucha con rampa, latencias, `callbackDepth`),
  `Recorder` (`writeAligned`, `saveBank`, `compensation`), `LoadedTrack::songTime` respetado
  por `arrangement::render`, botón "Grabar" y diálogo (`recordDialog`, `ensureInputsEnabled`,
  `startRecording`, `stopRecording`, `recordingFinished`), "Grabar golpes como banco nuevo..."
  en el menú del trigger, casilla del trigger en el mezclador con los modos suena sola / solo
  el trigger / ambas (`keepAudio`, `LoadedTrack::replaced`), nota MIDI por pista, "Audio" con
  hasta 32 entradas.
- **v0.4.0 (triggers y sampler, punto 1 del plan de estudio en vivo)**: `Triggers`
  (detección de golpes, bancos de muestras), `SamplerLane`/`SamplerSet` en el motor, botón T y
  marcas de golpes en los carriles, canal del sampler en el mezclador, `trigger` por stem en
  `song.json`, bancos en `_bancos`, `--captura --trigger`. Siguen: grabación de entradas
  (punto 2) e instrumentos VST/AU (punto 3).
- **v0.3.8**: batería en partes (etapa `drumsep` con MDX23C DrumSep, casilla "Batería en
  partes", "Solo la batería en partes" para canciones ya separadas, `startDrumParts`,
  `replaceStem`, nombres Bombo/Caja/Toms/Hi-hat/Ride/Crash, botón "Instalar también Roformer y
  DrumSep" en Ajustes IA, `--captura --bateria --separarbateria`).
- **v0.3.7**: notas de texto (`SongNote`, `notes` en `song.json`, fila de notas de 96 px en la
  regla, `editNote` con editor multilínea, `activeNote` y letrero en la guía de escenario).
- **v0.3.6**: tono por sección (`TempoRegion::transpose`, `TempoSegment::transpose`,
  `effectiveTranspose`, `setTransposeSemitones` por bloque en el stretcher, menú de la banda).
- **v0.3.5**: guía de escenario (`StageView`, `StageWindow`, botón "Pantalla", F12,
  `--captura --escenario --posicion`).
- **v0.3.4**: el click como canal del mezclador (`ClickStrip`, `AudioEngine::takeClickPeak`,
  `onClickChanged`), sincronizado con la fila del click.
- **v0.3.3**: copiar, pegar (insertando o encima) y duplicar tramos con su grilla
  (`insertGap`, `pasteClip`, `copyGrid`, `pasteGrid`, `ClipClipboard` con las secciones de
  tempo del rango; `SongInfo::mergeEqualTempoRegions`); `shiftGrid` parte el acorde que
  atraviesa un hueco nuevo.
- **v0.3.2**: `FilePicker` (selector dentro de la ventana en Linux y Windows), "Separar
  canción (IA)" sobre la canción seleccionada (`separateCurrentSong`, `Library::replaceStems`,
  `separationTarget`), `--captura --separar --selector`.
- **v0.3.1 (Windows)**: preset `release-win`, `/utf-8` en MSVC, rutas por plataforma
  (`venvPython`, `isAbsolutePath`, `ffmpeg.exe`, `audio-separator.exe`), m4a en Windows,
  `instalar-ia.ps1`, instalador Inno Setup con ruedas de madmom compiladas en Actions, job
  `windows` con tests, Release con tres instaladores.
- **v0.3 (instaladores)**: `.deb` (`linux/empaquetar-deb.sh`), DMG universal para macOS
  (`mac/empaquetar.sh`, preset `release-mac`, `ICON_BIG`, firma ad hoc), GitHub Actions con
  Release por etiqueta, `scripts/instalar-ia.sh` y el botón "Instalar motores de IA" en
  Ajustes IA. El proyecto pasa a git (repositorio privado en GitHub).
- **v0.3 (nivelado por pista y proyectos)**: ganancias por stem y tramo (`stemGainsDb`,
  `headStemGainsDb`, `stemSongLufs`; `GainCurve::trackGains` y `LoadedTrack::levelSmooth` en el
  motor), medición en dos pasos (stems y suma nivelada), casilla "Niv" editable por canal en
  el mezclador, ondas dibujadas con la ganancia del tramo; exportar e importar carpetas de
  canción (`exportSong`, `exportAll`, `importProject`); nueva medición automática al cambiar
  marcadores o al cargar con tramos sin medir.
- **v0.3 (fase 4, arreglo)**: `Arrangement` (`Clip` y `clips` en `song.json`, `render` con
  fundidos y suma, `cutAt`, `moveClip`, `removeClip`, `joinWithPrevious`, `shiftGrid`),
  cadena `sourceSong` → `arrangedSong` → `currentSong`, menú de tramo en los carriles,
  Shift + arrastre, bordes y huecos en la capa superior, deshacer con Ctrl+Z (`undoStack`),
  `--captura --corte --desplazar`. Modos de corte (libre, rejilla, transiente) con
  `findOnset`, selector `cutModeBox`, encaje del arrastre según el modo, `--modocorte`.
- **v0.3 (enarmonías y métrica)**: `Music` (clase de altura, nombres con sostenidos o bemoles,
  letras o solfeo, `spellChord`, `spellKey`, `keyPrefersFlats`, `canonicalKey`), `spelling` y
  `keyOverride` por canción, menú en la tonalidad; `Analysis::renumberBeats` y `nearestBeat`,
  menú del tiempo en la regla (primer tiempo de compás, compases de N tiempos, quitar o
  insertar un tiempo; hasta el fin de la sección de tempo o de la canción). Click por
  sección (`halveBeats`, `doubleBeats`, `replaceBeatsWithGrid`, compases de N) y acordes
  editables (`chordMenu`).
- **v0.3 (secciones de tempo)**: `TempoRegion` en `song.json` (`tempoRegions`),
  `detectTempoRegions` a partir de los tiempos del análisis (también al leer canciones
  analizadas antes), `TimeMap` construido desde las secciones (los marcadores ya no definen
  tramos de tempo; se quitó `playBpm` por marcador), fila de tempo en la regla con menú por
  sección (tempo, original, igualar todas, dividir, unir, corregir el detectado, marcadores en
  los cambios), campo BPM del click que sigue la sección con análisis, tempo de la sección en
  la cabecera. `--tempo` en la captura aplica la misma tolerancia que el control.
- **v0.3 (fase 3, tempo y tono)**: `TimeMap`, `stretcher::render` con Signalsmith Stretch
  1.4.0 (FetchContent), controles de tempo, tono y tempo por tramo, render diferido con
  intercambio sin corte, posiciones mapeadas en toda la UI; `--captura --tempo --tono`.
- **v0.3 (nivelado)**: `Loudness` (EBU R128 + pico real, con test de referencia), ganancia por
  tramo (`GainCurve` en el motor, con rampa) y por canción (`setSongGain`), botones "Nivelar"
  con objetivo y "Nivelar setlist", ganancia editable por marcador, LUFS en la regla.
- **v0.3 (fase 2, análisis musical)**: `Analyzer` con madmom (tempo, compases, acordes,
  tonalidad) en `~/analisis-env`; `Analysis` en `song.json`; regla con compases reales, fila
  de acordes y BPM por sección; acorde actual y siguiente, tonalidad y tempo en la ventana;
  click que sigue los tiempos detectados (`BeatGrid`); `--captura --analizar`.
- **v0.3 (fase 1 de la vista DAW)**: `TimelineView` (regla con compases, marcadores
  arrastrables, carriles con forma de onda cacheada, cabezal, loop, zoom, seguimiento),
  `MixerPanel` con `LevelMeter` (pico, RMS, retención, clip, escala) y canal maestro
  (`masterGainDb` en `song.json`), modo en vivo (F11), `WaveformCache` y medidores por canal en
  el motor, opción `--captura` para revisar la interfaz. Ventana inicial 1360x860.
- **v0.3**: separación configurable. Selectores de pistas (4/6) y calidad (normal/alta/máxima)
  en vez del selector de modelo (ajustes `stems` y `quality`; `model` se migra una vez).
  `Separator` planifica etapas (`planFor`), convierte a 44,1 kHz con sinc, encadena Roformer
  (voces) y uno o dos modelos de Demucs, y escribe "otros" como residuo exacto. Progreso global
  ponderado, barras de descarga ignoradas, `--int24`. Tests de plan, conversión y residuo.
  Receta de instalación con pines de torch y `ffmpeg`; selector de archivos de JUCE en Linux.
- **v0.2.1**: revisión de estabilidad en vivo. Motor: crossfade al cerrar el loop, fundido al
  final de la canción, `loopRange` atómico, `isSilent()`, `audioDeviceError`, pares de salida
  negativos acotados. UI: cambio de canción con fundido previo (`afterFadeOut`), clic derecho
  sin cargar, sin cambio automático al terminar una separación con algo sonando, foco que
  respeta los `TextEditor`, autorrepetición de teclas ignorada, teclado numérico, guardado con
  retardo, errores del dispositivo visibles, confirmación al salir, marcadores en varias filas,
  `bad_alloc` capturado al cargar. Separator: lectura byte a byte, progreso por pasadas,
  estado `cancelled`, `reset()` espera al hilo, resultado movido en vez de copiado. Library:
  valores acotados, m4a solo en macOS, carpetas sin audio ignoradas. Versión desde CMake.

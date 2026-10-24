---
name: verificar-ui
description: Revisa con capturas la interfaz de Secuencias (la app C++/JUCE de este repositorio) sin tocar la biblioteca ni los ajustes reales del usuario - canción de prueba desechable, respaldo y limpieza de setlist.json, ajustes, shows.json y caché, la herramienta --captura con sus opciones (tamaño, vista grande, Show, herramientas, editor de tempo) y qué mirar en el PNG. Úsala siempre que cambies algo visible en Secuencias (MainComponent, TimelineView, MixerPanel, MixEditor, TempoTrack, StageView, diálogos, menús), antes de dar por terminado un cambio de interfaz, o cuando el usuario pida ver, revisar o capturar cómo se ve algo, aunque no nombre la skill.
---

# Verificar la interfaz de Secuencias

La app no tiene tests de interfaz: lo que se ve se comprueba con capturas que la propia app genera
(`--captura`, renderizadas por JUCE, funcionan en Wayland y con otra instancia abierta). El riesgo no es la captura
sino los datos: la app de captura **usa la biblioteca y los ajustes reales** (`~/Music/Secuencias`,
`~/.config/Secuencias`), y el usuario suele tener la app abierta trabajando al mismo tiempo. Una prueba descuidada
ya le pisó ajustes y le dejó canciones de prueba en el setlist. Por eso el flujo es: respaldar, probar con una
canción desechable, mirar, limpiar.

## 1. Preparar

```bash
SP=<tu carpeta scratchpad>        # PNG y respaldos van ahí, nunca en el repositorio
mkdir -p $SP/respaldo && cp ~/Music/Secuencias/setlist.json $SP/respaldo/ && cp -r ~/.config/Secuencias $SP/respaldo/config
ls ~/Music/Secuencias/shows.json ~/.cache/Secuencias 2>&1    # anota qué existía antes
ps aux | grep -i "[S]ecuencias"                               # ¿el usuario tiene la app abierta?
python3 .claude/skills/verificar-ui/scripts/cancion_prueba.py crear --nombre PruebaX   # imprime --cancion=N
```

**No uses las canciones ni los mixes del usuario** (por ejemplo "MIX RED", "MIX GILDA", "Mix Amar Azul",
"Mix la Cumbia"): una captura puede reescribir su `song.json`. Si hace falta una situación concreta (cortes,
secciones de tempo, fundidos), escríbela en el `song.json` de la canción de prueba (formato en CLAUDE.md, 5.3) o
créala con las opciones de captura. Para tempos y transiciones, `--segundos 15 --bpm 120` basta.

## 2. Capturar

Compila primero (`cmake --build --preset debug`). La build de Debug estira el audio muy lento: si la captura
cambia tempo o tono, dale varios minutos de timeout.

```bash
./build/debug/Secuencias_artefacts/Debug/Secuencias --captura=$SP/v.png --cancion=N [opciones] 2>&1 | grep -iE "assert|error"
```

Que no salga nada en ese grep: un "JUCE Assertion failure" es un error aunque la imagen se vea bien (el típico:
texto con tildes fuera de `tr()`). Opciones más usadas (la lista completa está en CLAUDE.md, 5.5 y 5.4m):

| Qué revisar | Opciones |
|---|---|
| Pantalla chica (mínimo 1000 x 640) | `--tamano=1000x640` |
| Vista grande | `--vivo [--posicion=seg]` |
| Show (escribe `shows.json` y el ajuste `playbackMode`: límpialos) | `--reproduccion --show=N` |
| Guía de escenario | `--escenario=$SP/guia.png --posicion=seg` |
| Herramientas | `--herramienta=objeto` (o rango, cortar, unir, borrar, zoom, silenciar, dibujar, reproducir, warp, color), `--clic=seg`, `--arrastrar=a:b`, `--pista=N`, `--shift` |
| Tempo y compás | `--tempopista=sección:bpm [--soloclick]`, `--curva=sección:curva` (sección desde 1), `--compas=N/D:compás` |
| Editor de tempo | `--editortempo=$SP/editor.png` |
| Tempo, tono, cortes | `--tempo=BPM`, `--tono=N`, `--corte=seg --desplazar=ms --modocorte=1` (2 rejilla, 3 transiente) |
| Armar mix | `--mix=<nombre de prueba> [--mixfuentes=...] [--mixtramos] [--mixsel=5-8]` |

Revisa siempre en dos tamaños: el normal (1360 x 860) y `--tamano=1000x640`.

## 3. Mirar

Abre el PNG con la herramienta de lectura de imágenes; para un detalle, recórtalo con PIL
(`Image.open(p).crop((x0, y0, x1, y1)).save(...)`). Busca:

- Textos cortados o encimados, etiquetas de la escala que se pisan, botones que se salen de la ventana.
- Cuadrados en lugar de símbolos (falta de glifo: dibuja con `Path` o usa texto).
- El color de acento solo en lo elegido o encendido.
- Que lo nuevo aparezca donde dice CLAUDE.md, y que en el Show no quede nada editable a la vista.

Si algo se ve mal, arregla, compila y vuelve a capturar el mismo caso antes de seguir.

## 4. Limpiar (siempre, también si algo falló)

```bash
python3 .claude/skills/verificar-ui/scripts/cancion_prueba.py borrar     # solo carpetas con .prueba
diff <(cat $SP/respaldo/setlist.json) ~/Music/Secuencias/setlist.json
```

- `setlist.json`: la app lo reescribe al cargar. Si la única diferencia son tus canciones de prueba, restaura el
  respaldo; si hay cambios del usuario (tiene la app abierta), quita solo tus entradas.
- Ajustes: compara `~/.config/Secuencias/Secuencias.settings` con el respaldo antes de restaurar, por la misma
  razón. Con `--reproduccion`, devuelve `playbackMode` al valor de antes.
- Borra `~/Music/Secuencias/shows.json` y `~/.cache/Secuencias` solo si no existían antes.
- `instrumentos.xml` y `plugins.xml` en `~/.config/Secuencias`: si no existían antes, bórralos.

Al terminar, cuéntale al usuario qué revisaste (con los tamaños o modos) y qué arreglaste; no hace falta
mostrarle cada PNG salvo que lo pida.

---
name: publicar-version
description: Cierra y publica una versión de Secuencias (la app C++/JUCE de este repositorio) - sube el número de versión en CMakeLists, pone al día CLAUDE.md (con su copia en la carpeta padre) y LEEME.md, compila release, hace el commit, el push y vigila la compilación de GitHub Actions en Ubuntu, Mac y Windows. Úsala siempre que termines un cambio grande o un "haz todo" en Secuencias, cuando el usuario diga "súbelo", "publica", "cierra la versión", "commit y push", "nueva versión", "vX.Y.Z" o pregunte si pasó la CI, aunque no nombre la skill.
---

# Publicar una versión de Secuencias

Así se cerraron la v0.11, v0.12 y v0.13. El orden importa: la documentación y la versión van en el mismo commit
que el código, y la CI de Mac y Windows solo se ve después del push (desde Linux no se compila para ellas).

## 0. Antes de empezar

- El cambio tiene que estar terminado y verificado: `cmake --build --preset debug` sin warnings nuevos en `Source/`
  o `Tests/`, `ctest --preset debug` al 100 % y, si tocaste la interfaz, capturas revisadas (skill `verificar-ui`).
- Mira `git status`: entra solo lo de esta versión. Nada de `graphify-out/`, PNG de capturas ni datos de prueba.

## 1. Número de versión

En `CMakeLists.txt`: `project(Secuencias VERSION X.Y.Z ...)`. Funciones nuevas suben la Y (0.12.0 → 0.13.0); solo
arreglos suben la Z (0.11.0 → 0.11.1). La app y los instaladores toman la versión de ahí; no hay otro lugar que
cambiar (el título del LEEME dice "v0.3" a propósito, no se toca).

## 2. CLAUDE.md (y su copia)

CLAUDE.md es la memoria del proyecto para los agentes: lo que no quede ahí se pierde en la próxima sesión. Revisa,
según lo que cambió:

- §2 Funciones actuales: qué hace, para el usuario, con los nombres de los botones y el identificador clave entre
  backticks.
- §4 Estructura: archivos nuevos.
- §5 Arquitectura: la sección del módulo tocado (5.2 motor, 5.3 formato en disco y **claves de ajustes**, 5.4x...).
  Todo campo nuevo de `song.json`, `mix.json` o de los ajustes va en 5.3 con su valor por defecto.
- §6 Trampas y §7 Limitaciones: lo que aprendiste y lo que quedó pendiente.
- §8 Hoja de ruta: marca lo hecho.
- §9 Bitácora: una entrada nueva **justo después de la de v0.2** (las demás van de la más nueva a la más vieja),
  `- **vX.Y.Z (tema)**: ...` con los identificadores nuevos y, si hubo, "Revisión:" con los arreglos.

Edita con pequeños reemplazos verificados (que el texto viejo exista), no reescribiendo el archivo entero: pesa
unos 160 KB y es fácil perder secciones. Después, la copia es obligatoria (el usuario abre las sesiones en la
carpeta padre):

```bash
cp CLAUDE.md /home/ubuntu/Documents/GitHub/daw/CLAUDE.md && cmp CLAUDE.md /home/ubuntu/Documents/GitHub/daw/CLAUDE.md && echo copiado
```

## 3. LEEME.md

Es la guía del músico: en español simple, sin identificadores de código. Agrega lo que el usuario va a usar (qué
botón, qué hace, un ejemplo) y actualiza la tabla "Atajos" si hay teclas nuevas.

## 4. Release y grafo

```bash
cmake --preset release && cmake --build --preset release   # solo el aviso "lto-wrapper: serial compilation" es normal
graphify update .                                           # deja al día graphify-out/ (local, sin red; no va en git)
```

El usuario suele tener abierta la build release: en Linux el binario se reemplaza sin afectar al proceso que corre.

## 5. Commit y push

Hay permiso para hacer cambios en git en este proyecto. Agrega los archivos por nombre (o `git add -A` después de
revisar `git status`). Mensaje en español: primera línea `vX.Y.Z: resumen corto`, luego viñetas con lo importante
y al final la línea de atribución `Co-Authored-By` que indique la conversación.

```bash
git commit -q -F - <<'EOF'
vX.Y.Z: resumen

- ...

Co-Authored-By: ...
EOF
GIT_SSH_COMMAND="ssh -o BatchMode=yes" git push -q origin main
```

## 6. Vigilar la CI

El token de GitHub está en `~/.config/secuencias/gh-token`: úsalo como variable y **nunca lo muestres**.

```bash
export GH_TOKEN=$(cat ~/.config/secuencias/gh-token)
gh api repos/Dolansalfate/secuencias/actions/runs --jq '.workflow_runs[0] | "\(.id) \(.head_sha[0:7]) \(.status)"'
```

Comprueba que el SHA sea el de tu commit. Espera con una sola tarea en segundo plano que termine cuando el run
diga `completed` (no consultes cada pocos segundos: tarda unos 25 a 35 minutos) y luego lista los trabajos:

```bash
gh api repos/Dolansalfate/secuencias/actions/runs/<id>/jobs --jq '.jobs[] | "\(.name): \(.conclusion)"'
```

Deben pasar "macOS (DMG universal)", "Ubuntu (tests y .deb)" y "Windows (tests e instalador)"; "Publicar Release"
sale `skipped` sin etiqueta. Si algo falla: `gh run view <id> --repo Dolansalfate/secuencias --log-failed | tail -80`.
Las causas típicas solo aparecen ahí: en Windows, init-captures con `this` dentro de otra lambda o constantes
locales sin `static constexpr`; en Mac, nombres globales que chocan con Carbon (`Marker`, `Rect`, `Point`...).
Arregla, vuelve a compilar y probar en local, y sube un commit nuevo (no reescribas el historial).

## 7. Release con instaladores (solo si el usuario lo pide)

Una etiqueta `vX.Y.Z` hace que la CI publique una Release con el `.deb`, el DMG y el instalador de Windows. El
repositorio es **público**: publicar deja los instaladores a la vista de cualquiera, así que se pregunta antes.

```bash
git tag vX.Y.Z && GIT_SSH_COMMAND="ssh -o BatchMode=yes" git push -q origin vX.Y.Z
```

## 8. Cierre

Responde en español, breve: qué entró en la versión, el commit, el estado de cada trabajo de la CI y, si no hay
Release, ofrece publicarla. Si dejaste algo local sin subir, dilo.

#!/usr/bin/env bash
# Instala los motores de IA de Secuencias, una vez, en el usuario actual:
#   ~/demucs-env    Demucs (separar canciones en stems) con PyTorch
#   ~/analisis-env  madmom (tempo, compases, acordes, tonalidad)
# Sirve en macOS (chips Apple e Intel) y en Ubuntu. Se puede volver a ejecutar sin problema.
#
#   ./instalar-ia.sh              instala Demucs y madmom
#   ./instalar-ia.sh --roformer   además audio-separator (voces de mejor calidad; pesado, requiere ffmpeg)
set -uo pipefail

OS=$(uname -s)
ARCH=$(uname -m)
echo "== Secuencias: instalación de los motores de IA ($OS $ARCH) =="
echo

fallo() { echo; echo "ERROR: $1"; echo; read -r -p "Pulsa Enter para cerrar." _; exit 1; }

# ------------------------------------------------------------------ Python 3 (3.9 a 3.12)
PY=""
for candidate in python3.12 python3.11 python3.10 python3.9 python3; do
    if command -v "$candidate" >/dev/null 2>&1; then
        ver=$("$candidate" -c 'import sys; print(sys.version_info[0]*100 + sys.version_info[1])' 2>/dev/null || echo 0)
        if [ "$ver" -ge 309 ] && [ "$ver" -le 312 ]; then PY=$candidate; break; fi
    fi
done
if [ -z "$PY" ]; then
    if [ "$OS" = "Darwin" ]; then
        if ! command -v python3 >/dev/null 2>&1; then
            echo "Falta Python 3. macOS lo instala con las herramientas de Xcode; acepta el diálogo que aparece."
            xcode-select --install 2>/dev/null || true
            fallo "Cuando termine la instalación de las herramientas de Xcode, vuelve a ejecutar este instalador."
        fi
        fallo "Se necesita Python 3.9 a 3.12 (PyTorch y madmom no tienen versiones para uno más nuevo). Instálalo desde https://www.python.org/downloads/macos/ y vuelve a ejecutar."
    fi
    fallo "Se necesita Python 3.9 a 3.12: sudo apt install python3 python3-venv"
fi
echo "Python: $($PY --version) ($PY)"
"$PY" -m venv --help >/dev/null 2>&1 || fallo "Falta el módulo venv: sudo apt install python3-venv"
if [ "$OS" = "Darwin" ] && ! xcode-select -p >/dev/null 2>&1; then
    echo "Faltan las herramientas de Xcode (compilador y git); acepta el diálogo que aparece."
    xcode-select --install 2>/dev/null || true
    fallo "Cuando termine esa instalación, vuelve a ejecutar este instalador."
fi
command -v git >/dev/null 2>&1 || fallo "Falta git (madmom se instala desde su repositorio): sudo apt install git"

# ------------------------------------------------------------------ Demucs
echo
echo "== 1/2  Demucs (separación en stems) en ~/demucs-env =="
[ -x ~/demucs-env/bin/python ] || "$PY" -m venv ~/demucs-env || fallo "No se pudo crear ~/demucs-env"
PIP=~/demucs-env/bin/pip
"$PIP" install --upgrade pip wheel >/dev/null || fallo "No se pudo actualizar pip"

TORCH_INDEX=""
if [ "$OS" = "Darwin" ]; then
    if [ "$ARCH" = "x86_64" ]; then
        TORCH="torch==2.2.2 torchaudio==2.2.2"          # el último PyTorch con versión para Mac Intel
    else
        TORCH="torch==2.8.0 torchaudio==2.8.0"          # chips Apple (usa la GPU cuando puede)
    fi
else
    TORCH="torch==2.8.0 torchaudio==2.8.0 torchvision==0.23.0"
    if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi >/dev/null 2>&1; then
        TORCH_INDEX="--index-url https://download.pytorch.org/whl/cu126"   # NVIDIA, incluidas tarjetas viejas
        echo "Tarjeta NVIDIA detectada: PyTorch con CUDA 12.6"
    else
        TORCH_INDEX="--index-url https://download.pytorch.org/whl/cpu"
        echo "Sin tarjeta NVIDIA: PyTorch para CPU"
    fi
fi
# PyTorch exacto desde su índice; después, Demucs desde PyPI con el índice de PyTorch solo como extra y
# los pines de torch repetidos: sin ellos pip cambia PyTorch y Demucs deja de funcionar
EXTRA_INDEX=${TORCH_INDEX/--index-url/--extra-index-url}
# shellcheck disable=SC2086
"$PIP" install $TORCH $TORCH_INDEX || fallo "No se pudo instalar PyTorch"
# shellcheck disable=SC2086
"$PIP" install demucs soundfile $TORCH $EXTRA_INDEX || fallo "No se pudo instalar Demucs"
if [ "${1:-}" = "--roformer" ]; then
    command -v ffmpeg >/dev/null 2>&1 || echo "AVISO: Roformer necesita ffmpeg en el PATH (brew install ffmpeg / sudo apt install ffmpeg)"
    # shellcheck disable=SC2086
    "$PIP" install audio-separator onnxruntime $TORCH $EXTRA_INDEX || fallo "No se pudo instalar audio-separator"
fi
~/demucs-env/bin/python -c "import demucs, torch; print('Demucs listo, PyTorch', torch.__version__)" || fallo "Demucs no importa"

# ------------------------------------------------------------------ madmom
echo
echo "== 2/2  madmom (tempo, compases y acordes) en ~/analisis-env =="
[ -x ~/analisis-env/bin/python ] || "$PY" -m venv ~/analisis-env || fallo "No se pudo crear ~/analisis-env"
PIP2=~/analisis-env/bin/pip
"$PIP2" install --upgrade pip wheel "numpy<2" "cython<3" || fallo "No se pudo instalar numpy"
if ! ~/analisis-env/bin/python -c "import madmom" >/dev/null 2>&1; then
    "$PIP2" install --no-build-isolation "git+https://github.com/CPJKU/madmom" || fallo "No se pudo compilar madmom (¿faltan las herramientas de compilación?)"
fi
~/analisis-env/bin/python -c "import madmom; print('madmom listo')" || fallo "madmom no importa"

echo
echo "== Todo listo. Los modelos se descargan la primera vez que separes o analices una canción. =="
echo "La app busca Python en ~/demucs-env y ~/analisis-env (Ajustes IA permite cambiarlo)."
echo
read -r -p "Pulsa Enter para cerrar." _

#!/usr/bin/env bash
# Instala Secuencias en tu usuario (sin sudo) y la agrega al menú de aplicaciones.
set -e
cd "$(dirname "$0")"

BIN=""
REL=build/release/Secuencias_artefacts/Release/Secuencias
if [ -x "$REL" ]; then
    BIN="$REL"
elif [ -x bin/secuencias ] && ! ldd bin/secuencias | grep -q "not found"; then
    BIN=bin/secuencias
    echo "Usando el binario precompilado (Ubuntu 24.04, x86_64)."
else
    echo "Compilando Secuencias (la primera vez descarga JUCE y tarda unos minutos)..."
    cmake --preset release
    cmake --build --preset release
    BIN="$REL"
fi

mkdir -p ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps ~/.local/share/secuencias
install -m 755 "$BIN" ~/.local/bin/secuencias
install -m 755 scripts/instalar-ia.sh ~/.local/share/secuencias/instalar-ia.sh   # Ajustes IA > Instalar motores de IA
install -m 644 linux/secuencias.svg ~/.local/share/icons/hicolor/scalable/apps/secuencias.svg

EXEC="$HOME/.local/bin/secuencias"
if command -v pw-jack >/dev/null 2>&1; then
    EXEC="pw-jack $EXEC"     # PipeWire como JACK: baja latencia y varias salidas
fi
sed "s|@EXEC@|$EXEC|" linux/secuencias.desktop.in > ~/.local/share/applications/secuencias.desktop
update-desktop-database ~/.local/share/applications >/dev/null 2>&1 || true
gtk-update-icon-cache ~/.local/share/icons/hicolor >/dev/null 2>&1 || true

echo ""
echo "Listo. Busca \"Secuencias\" en el menú de aplicaciones."
echo "También puedes abrirla desde la terminal con: $EXEC"

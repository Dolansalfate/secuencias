#!/usr/bin/env bash
# Arma el paquete dist/secuencias_<versión>_amd64.deb para Ubuntu (instala en /opt/secuencias y
# agrega la app al menú). Requiere el binario de release ya compilado (cmake --preset release &&
# cmake --build --preset release) o lo compila si no existe.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(Secuencias VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
BIN=build/release/Secuencias_artefacts/Release/Secuencias
if [ ! -x "$BIN" ]; then
    cmake --preset release
    cmake --build --preset release
fi

ARCH=$(dpkg --print-architecture)
WORK=$(mktemp -d)
ROOT="$WORK/secuencias_$VERSION"
mkdir -p "$ROOT/DEBIAN" "$ROOT/opt/secuencias" "$ROOT/usr/bin" \
         "$ROOT/usr/share/applications" "$ROOT/usr/share/icons/hicolor/scalable/apps"

install -m 755 "$BIN" "$ROOT/opt/secuencias/Secuencias"
install -m 755 scripts/instalar-ia.sh "$ROOT/opt/secuencias/instalar-ia.sh"
install -m 644 LEEME.md "$ROOT/opt/secuencias/LEEME.md"
install -m 644 linux/secuencias.svg "$ROOT/usr/share/icons/hicolor/scalable/apps/secuencias.svg"

# Lanzador: con PipeWire usa pw-jack (baja latencia y varias salidas); si no, ALSA directo
cat > "$ROOT/usr/bin/secuencias" <<'EOF'
#!/bin/sh
if command -v pw-jack >/dev/null 2>&1; then
    exec pw-jack /opt/secuencias/Secuencias "$@"
fi
exec /opt/secuencias/Secuencias "$@"
EOF
chmod 755 "$ROOT/usr/bin/secuencias"
sed "s|@EXEC@|/usr/bin/secuencias|" linux/secuencias.desktop.in > "$ROOT/usr/share/applications/secuencias.desktop"

cat > "$ROOT/DEBIAN/control" <<EOF
Package: secuencias
Version: $VERSION
Section: sound
Priority: optional
Architecture: $ARCH
Depends: libasound2, libfreetype6, libfontconfig1, libstdc++6, libc6
Recommends: python3-venv, ffmpeg
Maintainer: Secuencias <secuencias@localhost>
Description: Secuencias: secuencias (backing tracks) multipista en vivo
 Lanzador de pistas multipista para tocar en vivo, con separación de canciones en stems (Demucs),
 análisis de tempo y acordes, nivelado de volumen, cambio de tempo y tono y edición del arreglo.
EOF

mkdir -p dist
DEB=dist/secuencias_${VERSION}_${ARCH}.deb
rm -f "$DEB"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB" >/dev/null
rm -rf "$WORK"
echo "Listo: $DEB"
echo "Instalar con: sudo apt install ./$DEB"

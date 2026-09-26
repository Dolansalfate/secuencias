#!/usr/bin/env bash
# Compila Secuencias para macOS (binario universal: chips Apple e Intel) y arma el instalador
# dist/Secuencias-<versión>-mac.dmg. Se ejecuta en un Mac con las herramientas de Xcode
# (xcode-select --install), CMake y Ninja (brew install cmake ninja). GitHub Actions lo usa tal cual.
#
#   ./mac/empaquetar.sh            compila y arma el DMG
#   ./mac/empaquetar.sh --sin-compilar   solo arma el DMG con lo ya compilado
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(Secuencias VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
APP=build/release-mac/Secuencias_artefacts/Release/Secuencias.app

if [ "${1:-}" != "--sin-compilar" ]; then
    cmake --preset release-mac
    cmake --build --preset release-mac
fi
[ -d "$APP" ] || { echo "No se encontró $APP"; exit 1; }

# Firma "ad hoc": no requiere cuenta de desarrollador, pero los chips Apple exigen que el binario
# esté firmado para ejecutarlo. Al abrirla por primera vez macOS pedirá confirmación.
codesign --force --deep --sign - "$APP"

mkdir -p dist
STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Aplicaciones"
cp LEEME.md "$STAGE/LEEME.md"
cat > "$STAGE/Primera vez.txt" <<'EOF'
Arrastra Secuencias a la carpeta Aplicaciones.

La app no está firmada con una cuenta de desarrollador de Apple. La primera vez que la abras:
  - macOS 13 y 14: clic derecho sobre Secuencias, "Abrir", y confirma.
  - macOS 15 o posterior: intenta abrirla, luego ve a Ajustes del Sistema > Privacidad y
    seguridad y pulsa "Abrir de todos modos".
Solo hace falta una vez por versión.

Para separar canciones y analizar tempo y acordes hay que instalar los motores de IA una vez:
en la app, botón "Ajustes IA" > "Instalar motores de IA".
EOF

DMG=dist/Secuencias-$VERSION-mac.dmg
rm -f "$DMG"
hdiutil create -volname "Secuencias $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"
echo "Listo: $DMG"

#!/usr/bin/env bash
# Quita la app. Tus canciones en ~/Music/Secuencias NO se borran.
rm -f ~/.local/bin/secuencias \
      ~/.local/share/applications/secuencias.desktop \
      ~/.local/share/icons/hicolor/scalable/apps/secuencias.svg \
      ~/.local/share/secuencias/instalar-ia.sh
update-desktop-database ~/.local/share/applications >/dev/null 2>&1 || true
echo "Secuencias desinstalada. Tus canciones siguen en ~/Music/Secuencias."

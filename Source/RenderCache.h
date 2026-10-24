#pragma once
#include "AudioEngine.h"
#include <functional>

// Caché en disco de canciones ya renderizadas (arreglo, tempo y tono aplicados) para el modo reproducción: una
// canción estirada que no cabe en la RAM se vuelve a cargar leyendo sus pistas, sin estirarla de nuevo. Cada entrada
// es una carpeta con las pistas en WAV de coma flotante (32 bits: lo que pasa de 0 dBFS antes del fader no se recorta) y
// meta.json (la clave completa, la frecuencia y cada pista). Se escribe en una carpeta temporal que se renombra al
// terminar: quien lee ve la entrada entera o ninguna.
namespace rendercache
{
    // ~/.cache/Secuencias/render (Linux), ~/Library/Caches/Secuencias/render (macOS), %LOCALAPPDATA%\Secuencias\render
    juce::File defaultFolder();
    // Lo guardado con esa clave a esa frecuencia, con la mezcla de `info` (fader, mute y salida de cada pista);
    // nullptr si no está, no coincide o no se pudo leer
    std::shared_ptr<LoadedSong> load (const juce::File& root, const juce::String& key, const SongInfo& info, double sampleRate,
                                      juce::AudioFormatManager&, const std::function<bool()>& shouldAbort = {});
    // true si quedó guardada (o ya estaba)
    bool store (const juce::File& root, const juce::String& key, const LoadedSong&, const std::function<bool()>& shouldAbort = {});
    // Borra las entradas menos usadas hasta quedar bajo `maxBytes`
    void prune (const juce::File& root, juce::int64 maxBytes);
}

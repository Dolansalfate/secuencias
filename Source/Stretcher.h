#pragma once
#include "AudioEngine.h"
#include "TempoMap.h"
#include <functional>

// Cambio de tempo (sin cambiar el tono) y transposición con Signalsmith Stretch (MIT), fuera de
// línea: a partir de la canción original en RAM produce otra LoadedSong ya estirada y transpuesta,
// que el motor reproduce como cualquier otra. Corre en el hilo de carga.
namespace stretcher
{
    // Devuelve la misma canción (mismo puntero) si el mapa es identidad y ningún tramo transpone.
    // nullptr si se abortó. progress recibe 0..1. Cada tramo del mapa lleva su tempo y sus semitonos.
    std::shared_ptr<LoadedSong> render (std::shared_ptr<LoadedSong> source, const TimeMap&,
                                        double sampleRate, const std::function<bool()>& shouldAbort,
                                        const std::function<void (float)>& progress = {});

    // Estira un buffer estéreo según el mapa (en segundos) y transpone cada tramo con sus
    // semitonos. Largo de salida = playbackLength * sampleRate. Expuesto para los tests.
    juce::AudioBuffer<float> renderBuffer (const juce::AudioBuffer<float>& stereo, const TimeMap&,
                                           double sampleRate, const std::function<bool()>& shouldAbort = {});
}

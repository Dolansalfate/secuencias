#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// Grabación de entradas (punto 2): lo que pasa fuera del hilo de audio. El motor graba la entrada
// cruda con AudioEngine::setRecorder (un ThreadedWriter de JUCE, cola sin bloqueo); aquí la toma se
// alinea con la canción (latencia del dispositivo) o se corta en golpes para un banco de muestras.
namespace recorder
{
    // Copia `raw` a `out` (wav de 24 bits, misma frecuencia y canales) de modo que su primera muestra
    // caiga en `startSample` de la canción: antepone silencio si es positivo y descarta el comienzo
    // si es negativo. false si no se pudo leer o escribir.
    bool writeAligned (const juce::File& raw, juce::int64 startSample, const juce::File& out, juce::AudioFormatManager&);

    // Corta una grabación de golpes sueltos (triggers::sliceHits) y guarda cada uno como golpe-NN.wav
    // en `folder` (la crea). Devuelve cuántos guardó.
    int saveBank (const juce::AudioBuffer<float>& recording, double sampleRate, const juce::File& folder);

    // Muestras que la toma llega tarde respecto de lo que oyó el músico: latencia de entrada más la de
    // salida que informa el dispositivo, más un ajuste manual en ms (positivo = adelantar la toma).
    juce::int64 compensation (int inputLatency, int outputLatency, double extraMs, double sampleRate);
}

#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

// Triggers: los golpes detectados en una pista (por ejemplo la batería separada) disparan un
// sonido: un banco de muestras grabadas o importadas (sampler interno) o, más adelante, un
// instrumento VST. La detección es fuera de línea, sobre el audio ya renderizado, así los
// golpes se conocen de antemano: timing exacto a la muestra y sin latencia.
struct TriggerEvent
{
    juce::int64 sample = 0;   // posición en muestras (línea de tiempo de reproducción)
    float velocity = 1.0f;    // 0..1 según la fuerza del golpe
};

// Banco de muestras en RAM: golpes ordenados de suave a fuerte, ya recortados a su ataque y a la
// frecuencia del dispositivo. `pick` elige la capa por velocidad alternando muestras parecidas.
struct SampleBankData
{
    struct Hit
    {
        juce::AudioBuffer<float> buffer;   // estéreo
        float peak = 0.0f;
        juce::String name;
    };
    juce::String name;
    double sampleRate = 44100.0;
    std::vector<Hit> hits;   // ordenados por pico creciente

    int pick (float velocity, int& lastIndex) const;   // índice en hits, o -1 si está vacío
};

namespace triggers
{
    // Golpes de un buffer: envolvente en bloques de 1 ms; un golpe empieza donde el nivel sube al
    // menos 6 dB en 3 ms y supera `thresholdDb`; después hay que bajar 6 dB desde el pico (o dejar
    // pasar `minMs`) para admitir otro. Velocidad = posición del pico entre el umbral y 0 dBFS,
    // elevada a 1/sensitivity (sensibilidad > 1 realza los golpes suaves).
    std::vector<TriggerEvent> detect (const juce::AudioBuffer<float>&, double sampleRate,
                                      double thresholdDb, double sensitivity, double minMs);

    // Lee cualquier archivo de audio a estéreo a `sampleRate` (remuestreo Lagrange); false si no se pudo
    bool readAudio (const juce::File&, double sampleRate, juce::AudioFormatManager&, juce::AudioBuffer<float>& out);

    // Carga una carpeta de banco: cada wav es un golpe; se recorta a 2 ms antes de su ataque, se le
    // pone un fundido de entrada de 1 ms y de salida de 5 ms, y se limita a 4 s. nullptr si no hay golpes.
    std::shared_ptr<SampleBankData> loadBank (const juce::File& folder, double sampleRate, juce::AudioFormatManager&);

    // Corta una grabación de golpes sueltos en muestras individuales (para crear un banco):
    // cada golpe va desde 2 ms antes de su ataque hasta el siguiente golpe (o 4 s), con fundido final.
    std::vector<juce::AudioBuffer<float>> sliceHits (const juce::AudioBuffer<float>& recording, double sampleRate,
                                                     double thresholdDb = -35.0, double minMs = 80.0);

    // Escribe un buffer como WAV de 24 bits
    bool writeWav (const juce::AudioBuffer<float>&, double sampleRate, const juce::File&);
}

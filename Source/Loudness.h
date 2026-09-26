#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Sonoridad según EBU R128 / ITU-R BS.1770-4: ponderación K, bloques de 400 ms con 75 % de
// solapamiento, puerta absoluta de -70 LUFS y relativa de -10 LU. Pico real con sobremuestreo x4.
// Todo fuera de línea sobre buffers en RAM (hilo de carga); nada de esto corre en el hilo de audio.
namespace loudness
{
    constexpr double unknown = -100.0;   // "sin medir" o silencio total

    struct Measurement
    {
        double lufs = unknown;          // sonoridad integrada
        double truePeakDb = unknown;    // pico real en dBTP
    };

    struct Section
    {
        double startSeconds = 0.0, endSeconds = 0.0;
        Measurement result;
    };

    struct Result
    {
        Measurement whole;
        std::vector<Section> sections;
    };

    // Mide la mezcla estéreo completa y cada tramo. Los tramos se dan en segundos (fin <= inicio
    // o fuera del audio = se ignoran; se devuelven en el mismo orden).
    Result measure (const juce::AudioBuffer<float>& stereo, double sampleRate, const std::vector<Section>& sections);

    // Ganancia en dB para llevar un tramo a targetLufs sin que su pico real supere maxPeakDb.
    // Devuelve 0 si no se midió.
    double gainToTarget (const Measurement&, double targetLufs, double maxPeakDb = -1.0);
}

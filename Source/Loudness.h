#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

// Sonoridad según EBU R128 / ITU-R BS.1770-4: ponderación K, bloques de 400 ms con 75 % de
// solapamiento, puerta absoluta de -70 LUFS y relativa de -10 LU. Pico real con sobremuestreo x4.
// Todo fuera de línea sobre buffers en RAM (hilo de carga); nada de esto corre en el hilo de audio.
namespace loudness
{
    constexpr double unknown = -100.0;   // "sin medir" o silencio total
    // Objetivos de nivelado que se pueden pedir (LUFS): más bajo no tiene sentido para una mezcla y más alto solo se
    // lograría aplastando todo con el limitador (el techo de pico se respeta siempre)
    constexpr double minTargetLufs = -30.0, maxTargetLufs = -6.0;
    // Un objetivo escrito a mano ("-15,5", "15", "-16 LUFS"): un número positivo se toma como negativo; se acota a
    // [minTargetLufs, maxTargetLufs]. false si no hay un número.
    bool parseTarget (const juce::String& text, double& lufs);
    juce::String targetText (double lufs);   // "-14 LUFS", "-15,5 LUFS"

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
    // o fuera del audio = se ignoran; se devuelven en el mismo orden). Sin truePeak no se calcula
    // el pico real (truePeakDb queda en unknown): es lo que más tarda.
    Result measure (const juce::AudioBuffer<float>& stereo, double sampleRate, const std::vector<Section>& sections,
                    bool truePeak = true);

    // Ganancia en dB para llevar un tramo a targetLufs sin que su pico real supere maxPeakDb.
    // Devuelve 0 si no se midió.
    double gainToTarget (const Measurement&, double targetLufs, double maxPeakDb = -1.0);

    // Limitador con anticipación, fuera de línea: en `out` (largo del audio) la ganancia de cada muestra (0..1)
    // para que audio · gain · out no pase de `ceiling` (lineal). El pico de cada muestra incluye una estimación
    // entre muestras (cúbica a media muestra), para acercarse al pico real. La ganancia baja durante `lookahead`
    // muestras antes de cada pico (media de un mínimo hacia adelante: llega a tiempo y sin saltos) y vuelve con
    // un polo de `release` muestras. Devuelve la mayor reducción en dB (0 si no hizo falta).
    double limiterGains (const juce::AudioBuffer<float>& audio, float gain, float ceiling, int lookahead, int release,
                         std::vector<float>& out);

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void makeKWeighting (double fs, Biquad& shelf, Biquad& highpass);

    // Sonoridad en vivo de la salida maestra (EBU R128): momentánea (400 ms) y de corto plazo (3 s), sin puertas.
    // process() corre en el hilo de audio: sin memoria dinámica (todo prealocado en prepare). Los valores se leen
    // desde la UI (atómicos).
    class LiveLoudness
    {
    public:
        void prepare (double sampleRate);
        void process (const float* left, const float* right, int numSamples);   // right = nullptr con salida mono
        float momentary() const  { return momentaryLufs.load(); }
        float shortTerm() const  { return shortTermLufs.load(); }

    private:
        Biquad shelf[2], highpass[2];
        double blockSum = 0.0;
        int blockCount = 0, blockLength = 4410, filled = 0, next = 0;
        std::array<double, 30> blocks {};
        std::atomic<float> momentaryLufs { (float) unknown }, shortTermLufs { (float) unknown };
    };
}

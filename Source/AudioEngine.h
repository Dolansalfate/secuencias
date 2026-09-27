#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library.h"
#include "Triggers.h"
#include <atomic>
#include <functional>
#include <memory>

// Mínimos y máximos por bloque de 256 muestras, por canal, para dibujar la forma de onda a
// cualquier zoom sin volver a recorrer el audio. Se calcula al cargar (hilo de carga).
struct WaveformCache
{
    static constexpr int binSize = 256;
    std::vector<float> minL, maxL, minR, maxR;

    int numBins() const { return (int) maxL.size(); }
    float peak = 0.0f;   // valor absoluto máximo de toda la pista (para escalar el dibujo)
    // Mín y máx del rango de muestras [from, to). false si el rango queda fuera del audio.
    bool rangeMinMax (juce::int64 from, juce::int64 to, float& mnL, float& mxL, float& mnR, float& mxR) const;
    static WaveformCache build (const juce::AudioBuffer<float>& stereo);
};

struct LoadedTrack
{
    juce::String name;
    int stemIndex = -1;
    bool songTime = false;                    // ya está en la línea de tiempo de la canción (grabación): no pasa por los tramos
    juce::AudioBuffer<float> buffer;          // estéreo, a la frecuencia del dispositivo
    WaveformCache waveform;
    std::atomic<float> gain { 1.0f };
    std::atomic<bool> muted { false }, solo { false };
    std::atomic<bool> replaced { false };     // su trigger la reemplaza: no suena (aparte del mute del usuario)
    std::atomic<int> outputPair { 0 };
    // Medidores post-fader: pico máximo desde la última lectura de la UI (exchange (0)) y RMS del último bloque
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
    float smoothedGain = 1.0f;                // solo hilo de audio
    float levelSmooth = 1.0f;                 // ganancia de nivelado por pista suavizada (solo hilo de audio)
};

struct LoadedSong
{
    std::vector<std::unique_ptr<LoadedTrack>> tracks;
    juce::int64 length = 0;
    double sampleRate = 44100.0;
};

// Sampler de triggers: los golpes detectados en una pista disparan muestras de un banco. Una
// "línea" por pista con trigger; sus controles y medidores son un LoadedTrack sin buffer, así el
// mezclador la trata como un canal más. Todo lo que toca el hilo de audio está prealocado.
struct SamplerLane
{
    LoadedTrack control;                         // gain, muted, solo, outputPair y medidores
    juce::String name;
    int stemIndex = -1;                          // pista de origen
    std::vector<TriggerEvent> events;            // ordenados por muestra (línea de tiempo de reproducción)
    std::shared_ptr<const SampleBankData> bank;
    // Solo hilo de audio
    size_t nextEvent = 0;
    juce::int64 lastPos = -2;
    int lastHit = -1;
    struct Voice
    {
        const juce::AudioBuffer<float>* buffer = nullptr;
        int pos = 0;
        float gain = 0.0f;
    };
    Voice voices[16];
};

struct SamplerSet
{
    std::vector<std::unique_ptr<SamplerLane>> lanes;
};

// Grabación de una entrada (punto 2). La UI crea el ThreadedWriter (y lo destruye después de
// clearRecorder, que espera a que el callback deje de usarlo); el hilo de audio solo copia los
// canales de entrada elegidos al writer (cola sin bloqueo), los escucha por un par de salida con
// rampa y mide su pico. `recordStartPosition` es la posición del transporte al grabar el primer bloque.
struct RecordSetup
{
    juce::AudioFormatWriter::ThreadedWriter* writer = nullptr;   // nullptr = solo escuchar y medir
    int inputL = 0, inputR = -1;      // canales de entrada entre los activos; inputR = -1: mono
    bool monitor = true;
    float monitorGain = 1.0f;
    int monitorPair = 0;
};

// Ganancia por tramo (nivelado): gains[i] rige desde positions[i] hasta positions[i+1].
// Antes del primer punto rige gains[0]. El motor suaviza los cambios con una rampa de 50 ms.
struct GainCurve
{
    std::vector<juce::int64> positions;   // ordenadas
    std::vector<float> gains;             // lineales, para todas las pistas
    // Nivelado por pista: trackGains[stemIndex][tramo], lineal; vacío o corto = 1. Se multiplica
    // con `gains` y se suaviza por pista (LoadedTrack::levelSmooth).
    std::vector<std::vector<float>> trackGains;
};

// Tiempos detectados por el análisis, en muestras del dispositivo, para que el click los siga.
struct BeatGrid
{
    std::vector<juce::int64> positions;   // ordenadas
    std::vector<int> beatInBar;           // 1 = acento
};

// Motor de reproducción multipista con fundidos anti-clic, loop y click generado.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();

    // Largo de los fundidos (play, pausa, saltos, cierre del loop y final de la canción), en muestras.
    static constexpr int fadeSamples = 256;
    static constexpr int maxMeteredOutputs = 64;

    // Se llama desde un hilo de fondo. Devuelve nullptr si se aborta.
    static std::shared_ptr<LoadedSong> loadSong (const SongInfo&, double sampleRate,
                                                 juce::AudioFormatManager&,
                                                 const std::function<bool()>& shouldAbort);
    void setSong (std::shared_ptr<LoadedSong>);

    void play();
    void pause();
    void togglePlay();
    void stop();                 // pausa y vuelve al inicio
    bool isPlaying() const       { return playing.load(); }

    // true cuando el fundido global llegó a 0 (tras pause/stop ya no sale audio de la canción).
    bool isSilent() const        { return currentFade.load() <= 0.0f; }

    void seekSeconds (double);
    double getPositionSeconds() const;
    double getLengthSeconds() const;

    void setLoop (double startSeconds, double endSeconds);
    void clearLoop();

    void setClick (bool enabled, double bpm, double offsetSeconds, float gainDb, int outputPair);
    // Con rejilla, el click suena en los tiempos detectados (acento en el primero del compás);
    // sin ella (nullptr o vacía), en una rejilla fija de 4/4 según BPM e inicio.
    void setBeatGrid (std::shared_ptr<const BeatGrid>);

    // Ganancia general (lineal) aplicada a todas las salidas con rampa
    void setMasterGain (float linear)  { masterGain = juce::jmax (0.0f, linear); }
    float getMasterGain() const        { return masterGain.load(); }
    // Ganancia de la canción (nivelado entre canciones), lineal, también con rampa y sobre todas las salidas
    void setSongGain (float linear)    { songGain = juce::jmax (0.0f, linear); }
    // Ganancia por tramo (nivelado dentro de la canción), aplicada a las pistas y no al click
    void setGainCurve (std::shared_ptr<const GainCurve>);   // nullptr = sin nivelado
    void setSamplers (std::shared_ptr<SamplerSet>);          // nullptr = sin triggers; se cambia bajo songLock
    std::shared_ptr<SamplerSet> getSamplers() const          { return samplers; }

    // Medidores de las salidas del dispositivo (después del fader maestro)
    float takeOutputPeak (int channel);         // pico desde la última lectura; 0 si el canal no existe
    float takeClickPeak()                       { return clickPeak.exchange (0.0f); }   // pico del click generado desde la última lectura
    float getOutputRms (int channel) const;

    double getSampleRate() const    { return sampleRate.load(); }
    int getNumOutputChannels() const { return numOutputs.load(); }
    int getNumInputChannels() const  { return numInputs.load(); }
    int getInputLatency() const      { return inputLatency.load(); }    // muestras, según el dispositivo
    int getOutputLatency() const     { return outputLatency.load(); }

    // Grabación: empieza en el siguiente bloque. Las dos funciones esperan (unos ms, desde la UI) a que
    // termine el callback en curso, así al volver de clearRecorder el writer ya no se usa y se puede destruir.
    void setRecorder (const RecordSetup&);
    void clearRecorder();
    bool isRecording() const                       { return recorder.load() != nullptr; }
    juce::int64 getRecordStartPosition() const     { return recordStartPosition.load(); }   // -1 = aún nada grabado
    juce::int64 getRecordedSamples() const         { return recordedSamples.load(); }
    juce::int64 getDroppedSamples() const          { return droppedSamples.load(); }        // no cupieron en la cola
    float takeInputPeak()                          { return inputPeak.exchange (0.0f); }    // pico de la entrada armada

    std::atomic<bool> needsReload { false };   // cambió la frecuencia del dispositivo

    // Si el dispositivo reportó un error (por ejemplo JACK se cerró), lo devuelve una vez.
    bool takeDeviceError (juce::String& message);

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String& errorMessage) override;

    // Canales (entre los activos del dispositivo) de un par de salida; pares inválidos caen a 1-2.
    static void outputPairChannels (int pair, int numOutputs, int& left, int& right);

private:
    void renderChunk (LoadedSong&, float* const* outputs, int numOutputs, int offset, int n);
    void applyMasterAndMeter (float* const* outputs, int numOutputs, int numSamples);
    void processInput (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
    void waitForCallback();

    // Grabación: dos configuraciones alternas (la UI escribe la que el callback no lee) y un puntero atómico
    RecordSetup recordSlots[2];
    int recordSlotIndex = 0;                                   // solo UI
    std::atomic<const RecordSetup*> recorder { nullptr };
    std::atomic<int> callbackDepth { 0 };                      // 1 mientras corre el callback
    std::atomic<juce::int64> recordStartPosition { -1 }, recordedSamples { 0 }, droppedSamples { 0 };
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<int> numInputs { 0 }, inputLatency { 0 }, outputLatency { 0 };
    float monitorSmooth = 0.0f;                                // hilo de audio
    int monitorL = 0, monitorR = -1, monitorOutPair = 0;       // últimos canales escuchados (para apagar con rampa)

    juce::SpinLock songLock;
    std::shared_ptr<LoadedSong> song;
    std::shared_ptr<const BeatGrid> beatGrid;   // también bajo songLock
    std::shared_ptr<const GainCurve> gainCurve; // también bajo songLock
    std::shared_ptr<SamplerSet> samplers;       // también bajo songLock
    float levelSmooth = 1.0f;                   // ganancia por tramo suavizada (hilo de audio)
    std::atomic<float> songGain { 1.0f };

    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> numOutputs { 2 };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> position { 0 }, pendingSeek { -1 }, songLength { 0 };
    // Inicio y fin del loop empaquetados (32 bits cada uno) para leerlos de una sola vez
    std::atomic<juce::uint64> loopRange { 0 };
    std::atomic<float> currentFade { 0.0f };

    std::atomic<bool> clickOn { false };
    std::atomic<double> clickBpm { 120.0 }, clickOffset { 0.0 };
    std::atomic<float> clickGain { 0.5f };
    std::atomic<int> clickPair { 0 };
    std::atomic<float> clickPeak { 0.0f };

    std::atomic<float> masterGain { 1.0f };
    float smoothedMaster = 1.0f;             // solo hilo de audio
    std::atomic<float> outputPeak[maxMeteredOutputs] {};
    std::atomic<float> outputRms[maxMeteredOutputs] {};

    juce::CriticalSection errorLock;
    juce::String deviceError;
    std::atomic<bool> deviceErrorPending { false };

    float fade = 0.0f;                       // envolvente global (hilo de audio)
    std::vector<juce::int64> positions;      // posición por muestra del bloque (-1 = silencio)
    std::vector<float> envelope;
    std::vector<juce::int64> positions2;     // segunda posición para el crossfade del loop (-1 = ninguna)
    std::vector<float> xfadeOut, xfadeIn;    // pesos del crossfade (1 y 0 fuera del cierre del loop)
    std::vector<float> levelGain;            // ganancia de nivelado por muestra
    std::vector<int> levelSegment;           // tramo del nivelado en el que cae cada muestra
    std::vector<float> laneL, laneR;         // mezcla de las voces de una línea del sampler
};

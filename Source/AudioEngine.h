#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library.h"
#include <atomic>
#include <functional>
#include <memory>

struct LoadedTrack
{
    juce::String name;
    int stemIndex = -1;
    juce::AudioBuffer<float> buffer;          // estéreo, a la frecuencia del dispositivo
    std::atomic<float> gain { 1.0f };
    std::atomic<bool> muted { false }, solo { false };
    std::atomic<int> outputPair { 0 };
    std::atomic<float> meter { 0.0f };        // pico desde la última lectura de la UI
    float smoothedGain = 1.0f;                // solo hilo de audio
};

struct LoadedSong
{
    std::vector<std::unique_ptr<LoadedTrack>> tracks;
    juce::int64 length = 0;
    double sampleRate = 44100.0;
};

// Motor de reproducción multipista con fundidos anti-clic, loop y click generado.
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();

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

    void seekSeconds (double);
    double getPositionSeconds() const;
    double getLengthSeconds() const;

    void setLoop (double startSeconds, double endSeconds);
    void clearLoop();

    void setClick (bool enabled, double bpm, double offsetSeconds, float gainDb, int outputPair);

    double getSampleRate() const    { return sampleRate.load(); }
    int getNumOutputChannels() const { return numOutputs.load(); }

    std::atomic<bool> needsReload { false };   // cambió la frecuencia del dispositivo

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    void renderChunk (LoadedSong&, float* const* outputs, int numOutputs, int offset, int n);
    static void outputPairChannels (int pair, int numOutputs, int& left, int& right);

    juce::SpinLock songLock;
    std::shared_ptr<LoadedSong> song;

    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> numOutputs { 2 };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> position { 0 }, pendingSeek { -1 }, songLength { 0 };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };

    std::atomic<bool> clickOn { false };
    std::atomic<double> clickBpm { 120.0 }, clickOffset { 0.0 };
    std::atomic<float> clickGain { 0.5f };
    std::atomic<int> clickPair { 0 };

    float fade = 0.0f;                       // envolvente global (hilo de audio)
    std::vector<juce::int64> positions;      // posición por muestra del bloque (-1 = silencio)
    std::vector<float> envelope;
};

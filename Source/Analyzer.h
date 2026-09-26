#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "AudioEngine.h"
#include "Library.h"
#include <atomic>

// Análisis musical de la canción cargada (tempo, tiempos y compases, acordes, tonalidad) con
// madmom en un Python aparte. Pipeline: mezcla los stems en RAM a dos WAV (mezcla completa
// y mezcla sin batería, para los acordes), ejecuta el script embebido, lee el JSON resultante.
class Analyzer : private juce::Thread
{
public:
    enum class State { idle, running, done, failed, cancelled };

    Analyzer();
    ~Analyzer() override;

    // isDrums: por pista, true si es la batería (se excluye de la mezcla armónica)
    bool start (std::shared_ptr<LoadedSong>, double sampleRate, std::vector<bool> isDrums,
                const juce::String& pythonPath);
    void cancel();
    void reset();

    State getState() const        { return (State) state.load(); }
    float getProgress() const     { return progress.load(); }   // -1 = indeterminado
    juce::String getMessage() const;
    const Analysis& getResult() const { return result; }        // válido con State::done

    // --- Utilidades sin estado (con tests) ---
    static bool isAvailable (const juce::String& pythonPath);   // existe el Python del análisis
    static Analysis parseResult (const juce::var& json, juce::String& error);
    // "C:maj" -> "C", "A:min" -> "Am", "G:7" -> "G7", "N" -> "N"
    static juce::String displayChord (const juce::String& madmomLabel);
    // "A minor" -> "La menor"
    static juce::String displayKey (const juce::String& madmomKey);
    static bool isDrumsTrack (const juce::String& name, const juce::String& fileName);
    // Mezcla las pistas indicadas (unidad, normalizada a 0.9 de pico) y las escribe como WAV de 24 bits
    static bool writeMix (const LoadedSong&, const std::vector<bool>& include, double sampleRate,
                          const juce::File& out, juce::String& error);
    static const char* pythonScript();   // contenido de analizar.py

private:
    void run() override;
    void setStatus (State, const juce::String&);

    std::shared_ptr<LoadedSong> song;
    std::vector<bool> drums;
    double sampleRate = 44100.0;
    juce::String python;
    juce::File workDir;
    Analysis result;

    std::atomic<int> state { (int) State::idle };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection messageLock, processLock;
    juce::String message;
    juce::ChildProcess* process = nullptr;
};

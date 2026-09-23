#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library.h"
#include <atomic>

// Separa una canción completa en stems ejecutando Demucs (IA) en segundo plano.
class Separator : private juce::Thread
{
public:
    enum class State { idle, running, done, failed };

    Separator();
    ~Separator() override;

    bool start (const juce::File& input, const juce::String& pythonPath,
                const juce::String& model, const juce::String& songName);
    void cancel();
    void reset();      // vuelve a idle y borra temporales

    State getState() const        { return (State) state.load(); }
    float getProgress() const     { return progress.load(); }
    juce::String getMessage() const;
    juce::File getResultFolder() const { return resultFolder; }
    juce::String getSongName() const   { return songName; }

private:
    void run() override;
    void setStatus (State, const juce::String&);
    void parseProgress (const juce::String&);

    juce::File inputFile, workDir, resultFolder;
    juce::String python, model, songName;

    std::atomic<int> state { (int) State::idle };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection messageLock, processLock;
    juce::String message;
    juce::ChildProcess* process = nullptr;
};

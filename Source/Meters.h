#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Medidor de nivel de uno o dos canales: RMS relleno, pico como línea, retención de pico
// durante 1,5 s, indicador de clip que se apaga con un clic, y escala en dB opcional.
// La balística se calcula en tick() (30 Hz); los valores llegan lineales (1.0 = 0 dBFS).
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (int numChannels = 2, bool showScale = false);

    void setLevels (int channel, float peak, float rms);   // del último intervalo
    void tick();
    void resetClip();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { resetClip(); }

    static constexpr float minDb = -60.0f, maxDb = 6.0f;
    static float dbToFraction (float db);   // minDb..maxDb -> 0..1

private:
    struct Channel
    {
        float peak = 0.0f, rms = 0.0f, hold = 0.0f;
        float newPeak = 0.0f, newRms = 0.0f;
        int holdTicks = 0;
        bool clipped = false;
    };
    std::vector<Channel> channels;
    bool showScale;
};

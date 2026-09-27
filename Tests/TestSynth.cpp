// Sintetizador VST3 mínimo para probar el host de instrumentos de Secuencias (solo se compila con
// los tests). En cada noteOn emite 50 ms de nivel constante igual a la velocidad, en L y R; ignora
// los noteOff (como una batería). Sin editor.
#include <juce_audio_processors/juce_audio_processors.h>

class TestSynth : public juce::AudioProcessor
{
public:
    TestSynth() : AudioProcessor (BusesProperties().withOutput ("Salida", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "SecuenciasTestSynth"; }
    void prepareToPlay (double sr, int) override
    {
        holdSamples = (int) (0.05 * sr);
        remaining = 0;
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        auto it = midi.begin();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            while (it != midi.end() && (*it).samplePosition <= i)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    level = m.getFloatVelocity();
                    remaining = holdSamples;
                }
                ++it;
            }
            const float v = remaining > 0 ? level : 0.0f;
            if (remaining > 0)
                --remaining;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, v);
        }
        midi.clear();
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override { dest.append ("estado", 6); }
    void setStateInformation (const void*, int) override {}

private:
    int holdSamples = 0, remaining = 0;
    float level = 0.0f;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynth();
}

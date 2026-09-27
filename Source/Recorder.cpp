#include "Recorder.h"
#include "Triggers.h"
#include <cmath>

namespace recorder
{
    bool writeAligned (const juce::File& raw, juce::int64 startSample, const juce::File& out, juce::AudioFormatManager& formats)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (raw));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;
        out.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), reader->sampleRate, reader->numChannels, 24, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();

        const int channels = (int) reader->numChannels;
        if (startSample > 0)
        {
            juce::AudioBuffer<float> zeros (channels, 8192);
            zeros.clear();
            for (juce::int64 left = startSample; left > 0;)
            {
                const int n = (int) juce::jmin<juce::int64> (left, zeros.getNumSamples());
                if (! writer->writeFromAudioSampleBuffer (zeros, 0, n))
                    return false;
                left -= n;
            }
        }
        const juce::int64 skip = startSample < 0 ? -startSample : 0;
        if (skip >= reader->lengthInSamples)
            return writer->flush();
        return writer->writeFromAudioReader (*reader, skip, reader->lengthInSamples - skip) && writer->flush();
    }

    int saveBank (const juce::AudioBuffer<float>& recording, double sampleRate, const juce::File& folder)
    {
        const auto hits = triggers::sliceHits (recording, sampleRate);
        if (hits.empty())
            return 0;
        folder.createDirectory();
        int saved = 0;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto file = folder.getNonexistentChildFile ("golpe-" + juce::String ((int) i + 1).paddedLeft ('0', 2), ".wav", false);
            if (triggers::writeWav (hits[i], sampleRate, file))
                ++saved;
        }
        return saved;
    }

    juce::int64 compensation (int inputLatency, int outputLatency, double extraMs, double sampleRate)
    {
        return (juce::int64) juce::jmax (0, inputLatency) + (juce::int64) juce::jmax (0, outputLatency)
               + (juce::int64) std::llround (extraMs * 0.001 * sampleRate);
    }
}

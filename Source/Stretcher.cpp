#include "Stretcher.h"
#include <cmath>

// Signalsmith Stretch es solo cabeceras y genera muchos warnings con nuestras flags: se silencian aquí.
#if defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma GCC diagnostic ignored "-Wswitch-enum"
 #pragma GCC diagnostic ignored "-Wredundant-decls"
 #pragma GCC diagnostic ignored "-Wcast-align"
 #pragma GCC diagnostic ignored "-Wunreachable-code"
 #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
 #pragma GCC diagnostic ignored "-Wstrict-aliasing"
 #pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif
#include "signalsmith-stretch/signalsmith-stretch.h"
#if defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace
{
    // Vistas de entrada y salida para Signalsmith: lectura con ceros fuera del audio, sin copiar
    struct InputChannel
    {
        const float* data;
        int length, offset;
        float operator[] (int i) const
        {
            const int k = i + offset;
            return (k >= 0 && k < length) ? data[k] : 0.0f;
        }
    };
    struct InputView
    {
        const juce::AudioBuffer<float>& buffer;
        int offset = 0;
        InputChannel operator[] (int c) const { return { buffer.getReadPointer (c), buffer.getNumSamples(), offset }; }
    };
    struct OutputChannel
    {
        float* data;
        int length, offset;
        float dummy = 0.0f;
        float& operator[] (int i)
        {
            const int k = i + offset;
            return (k >= 0 && k < length) ? data[k] : dummy;
        }
    };
    struct OutputView
    {
        juce::AudioBuffer<float>& buffer;
        int offset = 0;
        OutputChannel operator[] (int c) { return { buffer.getWritePointer (c), buffer.getNumSamples(), offset }; }
    };
}

namespace stretcher
{
    juce::AudioBuffer<float> renderBuffer (const juce::AudioBuffer<float>& in, const TimeMap& map,
                                           double sr, const std::function<bool()>& shouldAbort)
    {
        const int channels = juce::jmin (2, in.getNumChannels());
        const int outLen = juce::jmax (1, (int) std::llround (map.playbackLength() * sr));
        juce::AudioBuffer<float> out (channels, outLen);
        out.clear();
        if (in.getNumSamples() <= 0)
            return out;

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        stretch.presetDefault (channels, (float) sr);
        int semitones = map.segments().empty() ? 0 : map.segments().front().transpose;
        stretch.setTransposeSemitones ((float) semitones);

        // Posición del audio original (en muestras) que corresponde a una muestra de salida
        auto inputPosFor = [&] (int outSample) { return map.toOriginal ((double) outSample / sr) * sr; };

        // Pre-roll alineado: los siguientes samples de salida corresponden al inicio del audio
        const double firstRate = map.segments().empty() ? 1.0 : 1.0 / map.segments().front().ratio;
        const int seekLength = stretch.outputSeekLength ((float) firstRate);
        InputView input { in, 0 };
        stretch.outputSeek (input, seekLength);
        int inputIndex = seekLength;   // próxima muestra de entrada por entregar

        // Bloques de salida; la entrada avanza según el mapa de tempo (con su latencia por delante)
        constexpr int block = 1024;
        const int flushSamples = juce::jmin (outLen, stretch.outputLatency() + block);
        const int mainEnd = juce::jmax (0, outLen - flushSamples);
        OutputView output { out, 0 };
        for (int outIndex = 0; outIndex < mainEnd; outIndex += block)
        {
            if (shouldAbort && shouldAbort())
                return {};
            const int n = juce::jmin (block, mainEnd - outIndex);
            // Semitonos del tramo en el que cae este bloque (cambian en las fronteras de sección)
            if (auto* seg = map.segmentAtPlayback ((outIndex + n * 0.5) / sr); seg != nullptr && seg->transpose != semitones)
            {
                semitones = seg->transpose;
                stretch.setTransposeSemitones ((float) semitones);
            }
            const int wantedInput = (int) std::llround (inputPosFor (outIndex + n + stretch.outputLatency())) + stretch.inputLatency();
            const int inputSamples = juce::jmax (0, wantedInput - inputIndex);
            input.offset = inputIndex;
            output.offset = outIndex;
            stretch.process (input, inputSamples, output, n);
            inputIndex += inputSamples;
        }
        output.offset = mainEnd;
        stretch.flush (output, outLen - mainEnd);
        return out;
    }

    std::shared_ptr<LoadedSong> render (std::shared_ptr<LoadedSong> source, const TimeMap& map,
                                        double sr, const std::function<bool()>& shouldAbort,
                                        const std::function<void (float)>& progress)
    {
        if (source == nullptr)
            return nullptr;
        if (map.isPlain())
            return source;

        auto out = std::make_shared<LoadedSong>();
        out->sampleRate = sr;
        const int total = (int) source->tracks.size();
        int done = 0;
        for (auto& src : source->tracks)
        {
            if (shouldAbort && shouldAbort())
                return nullptr;
            auto t = std::make_unique<LoadedTrack>();
            t->name = src->name;
            t->stemIndex = src->stemIndex;
            t->gain = src->gain.load();
            t->smoothedGain = t->gain.load();
            t->muted = src->muted.load();
            t->solo = src->solo.load();
            t->outputPair = src->outputPair.load();
            t->buffer = renderBuffer (src->buffer, map, sr, shouldAbort);
            if (t->buffer.getNumSamples() == 0)
                return nullptr;   // abortado
            out->length = juce::jmax (out->length, (juce::int64) t->buffer.getNumSamples());
            out->tracks.push_back (std::move (t));
            if (progress)
                progress ((float) ++done / (float) juce::jmax (1, total));
        }
        for (auto& t : out->tracks)
        {
            t->buffer.setSize (2, (int) out->length, true, true, false);
            t->waveform = WaveformCache::build (t->buffer);
        }
        return out;
    }
}

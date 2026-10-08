#include "Triggers.h"
#include "Arrangement.h"
#include <cmath>

namespace
{
    // Envolvente en dB por bloques de `hop` muestras (potencia media de todos los canales)
    std::vector<double> envelopeDb (const juce::AudioBuffer<float>& b, int hop)
    {
        const int blocks = b.getNumSamples() / hop;
        std::vector<double> env ((size_t) juce::jmax (0, blocks));
        for (int i = 0; i < blocks; ++i)
        {
            double acc = 0.0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float* d = b.getReadPointer (ch, i * hop);
                for (int k = 0; k < hop; ++k)
                    acc += (double) d[k] * d[k];
            }
            env[(size_t) i] = 10.0 * std::log10 (acc / (double) (hop * juce::jmax (1, b.getNumChannels())) + 1.0e-10);
        }
        return env;
    }

    float peakOf (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = juce::jmax (p, b.getMagnitude (ch, 0, b.getNumSamples()));
        return p;
    }
}

int SampleBankData::pick (float velocity, int& lastIndex) const
{
    const int n = (int) hits.size();
    if (n == 0)
        return -1;
    const float target = juce::jlimit (0.0f, 1.0f, velocity) * (float) (n - 1);
    int idx = (int) std::lround (target);
    if (n >= 3 && idx == lastIndex)
    {
        // Alternar con la muestra vecina más cercana al objetivo, para no repetir siempre la misma
        const int alt = target >= (float) idx ? juce::jmin (n - 1, idx + 1) : juce::jmax (0, idx - 1);
        if (alt != idx)
            idx = alt;
        else
            idx = juce::jlimit (0, n - 1, idx + (idx == n - 1 ? -1 : 1));
    }
    lastIndex = idx;
    return idx;
}

namespace triggers
{
    std::vector<TriggerEvent> detect (const juce::AudioBuffer<float>& b, double sr, double thresholdDb, double sensitivity, double minMs)
    {
        std::vector<TriggerEvent> out;
        if (b.getNumSamples() <= 0 || b.getNumChannels() <= 0 || sr <= 0.0)
            return out;
        const int hop = juce::jmax (1, (int) std::llround (sr * 0.001));
        const auto env = envelopeDb (b, hop);
        const int blocks = (int) env.size();
        constexpr int rise = 3;
        constexpr double minRiseDb = 6.0;
        const int minBlocks = juce::jmax (1, (int) std::llround (minMs));
        const int peakWindow = 20;   // ms tras el ataque para medir la fuerza
        const double gamma = 1.0 / juce::jlimit (0.2, 5.0, sensitivity);

        int lastOnset = -1000000;
        double lastPeak = -200.0;
        bool armed = true;
        for (int i = rise; i < blocks; ++i)
        {
            if (! armed && (env[(size_t) i] < lastPeak - 6.0 || i - lastOnset >= minBlocks))
                armed = true;
            if (! armed || i - lastOnset < minBlocks)
                continue;
            if (env[(size_t) i] - env[(size_t) i - rise] < minRiseDb || env[(size_t) i] < thresholdDb)
                continue;
            // Inicio del ataque: valle previo (hasta 20 ms) y primer bloque que lo supera en 6 dB
            int trough = juce::jmax (0, i - rise - 20);
            for (int k = trough; k <= i; ++k)
                if (env[(size_t) k] < env[(size_t) trough])
                    trough = k;
            int start = i;
            for (int k = trough + 1; k <= i; ++k)
                if (env[(size_t) k] >= env[(size_t) trough] + 6.0)
                {
                    start = k;
                    break;
                }
            start = juce::jmax (0, start - 1);
            double peakDb = env[(size_t) i];
            for (int k = i; k < juce::jmin (blocks, i + peakWindow); ++k)
                peakDb = juce::jmax (peakDb, env[(size_t) k]);
            if (peakDb < thresholdDb)
                continue;
            const double norm = juce::jlimit (0.0, 1.0, (peakDb - thresholdDb) / juce::jmax (1.0, -thresholdDb));
            TriggerEvent e;
            e.sample = (juce::int64) start * hop;
            e.velocity = (float) std::pow (norm, gamma);
            out.push_back (e);
            lastOnset = i;
            lastPeak = peakDb;
            armed = false;
        }
        return out;
    }

    bool readAudio (const juce::File& file, double sr, juce::AudioFormatManager& formats, juce::AudioBuffer<float>& out)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;
        const int srcLen = (int) juce::jmin ((juce::int64) 60 * 60 * 48000, reader->lengthInSamples);
        juce::AudioBuffer<float> src (2, srcLen);
        reader->read (&src, 0, srcLen, 0, true, true);
        if (reader->numChannels == 1)
            src.copyFrom (1, 0, src, 0, 0, srcLen);
        if (std::abs (reader->sampleRate - sr) < 1.0)
        {
            out = std::move (src);
            return true;
        }
        const double ratio = reader->sampleRate / sr;
        const int outLen = (int) ((double) srcLen / ratio);
        out.setSize (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src.getReadPointer (ch), out.getWritePointer (ch), outLen);
        }
        return true;
    }

    // Deja el golpe con exactamente `pre` muestras antes de su ataque (ceros si la grabación no las tiene),
    // fundido de entrada de 1 ms en esas muestras previas, fundido de salida de 5 ms y tope de 4 s.
    static void trimHit (juce::AudioBuffer<float>& hit, double sr, int pre)
    {
        const int n = hit.getNumSamples();
        if (n <= 0)
            return;
        const double onset = arrangement::findOnset (hit, sr, juce::jmin (0.5, n / sr * 0.5), juce::jmin (0.5, n / sr * 0.5));
        const int attack = onset >= 0.0 ? juce::jlimit (0, n, (int) (onset * sr)) : 0;
        const int maxLen = (int) (4.0 * sr);
        const int lead = juce::jmax (0, pre - attack);   // ceros que faltan delante del ataque
        const int from = attack - (pre - lead);          // primera muestra que se copia (>= 0)
        const int copy = juce::jmin (n - from, maxLen - lead);
        if (copy <= 0)
            return;
        juce::AudioBuffer<float> trimmed (2, lead + copy);
        trimmed.clear();
        for (int ch = 0; ch < 2; ++ch)
            trimmed.copyFrom (ch, lead, hit, juce::jmin (ch, hit.getNumChannels() - 1), from, copy);
        const int len = trimmed.getNumSamples();
        const int fadeIn = juce::jmin (len / 2, (int) (0.001 * sr)), fadeOut = juce::jmin (len / 2, (int) (0.005 * sr));
        if (fadeIn > 0 && lead + fadeIn <= len) trimmed.applyGainRamp (lead, fadeIn, 0.0f, 1.0f);
        if (fadeOut > 0) trimmed.applyGainRamp (len - fadeOut, fadeOut, 1.0f, 0.0f);
        hit = std::move (trimmed);
    }

    std::shared_ptr<SampleBankData> loadBank (const juce::File& folder, double sr, juce::AudioFormatManager& formats)
    {
        auto bank = std::make_shared<SampleBankData>();
        bank->name = folder.getFileName();
        bank->sampleRate = sr;
        bank->preRoll = (int) (0.002 * sr);   // 2 ms antes del ataque, que el motor adelanta
        for (auto& f : Library::audioFilesIn (folder))
        {
            SampleBankData::Hit hit;
            if (! readAudio (f, sr, formats, hit.buffer))
                continue;
            trimHit (hit.buffer, sr, bank->preRoll);
            hit.peak = peakOf (hit.buffer);
            hit.name = f.getFileNameWithoutExtension();
            if (hit.buffer.getNumSamples() > 0 && hit.peak > 0.0f)
                bank->hits.push_back (std::move (hit));
        }
        if (bank->hits.empty())
            return nullptr;
        std::stable_sort (bank->hits.begin(), bank->hits.end(), [] (const SampleBankData::Hit& a, const SampleBankData::Hit& b) { return a.peak < b.peak; });
        return bank;
    }

    std::vector<juce::AudioBuffer<float>> sliceHits (const juce::AudioBuffer<float>& rec, double sr, double thresholdDb, double minMs)
    {
        std::vector<juce::AudioBuffer<float>> hits;
        const auto events = detect (rec, sr, thresholdDb, 1.0, minMs);
        const int pre = (int) (0.002 * sr), maxLen = (int) (4.0 * sr);
        for (size_t i = 0; i < events.size(); ++i)
        {
            const int start = juce::jmax (0, (int) events[i].sample - pre);
            const int end = juce::jmin (rec.getNumSamples(), i + 1 < events.size() ? juce::jmax (start + 1, (int) events[i + 1].sample - pre) : start + maxLen);
            const int len = juce::jmin (end - start, maxLen);
            if (len < (int) (0.01 * sr))
                continue;
            juce::AudioBuffer<float> hit (2, len);
            for (int ch = 0; ch < 2; ++ch)
                hit.copyFrom (ch, 0, rec, juce::jmin (ch, rec.getNumChannels() - 1), start, len);
            const int fade = juce::jmin (len / 2, (int) (0.01 * sr));
            if (fade > 0)
                hit.applyGainRamp (len - fade, fade, 1.0f, 0.0f);
            hits.push_back (std::move (hit));
        }
        return hits;
    }

    bool writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& file, int bitsPerSample)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return false;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, (unsigned) b.getNumChannels(), bitsPerSample, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

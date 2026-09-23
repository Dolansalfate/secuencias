#include "AudioEngine.h"
#include <cmath>

static constexpr int maxChunk = 2048;

AudioEngine::AudioEngine()
{
    positions.resize (maxChunk);
    envelope.resize (maxChunk);
}

std::shared_ptr<LoadedSong> AudioEngine::loadSong (const SongInfo& info, double sr,
                                                   juce::AudioFormatManager& formats,
                                                   const std::function<bool()>& shouldAbort)
{
    auto song = std::make_shared<LoadedSong>();
    song->sampleRate = sr;

    for (size_t i = 0; i < info.stems.size(); ++i)
    {
        if (shouldAbort && shouldAbort())
            return nullptr;

        const auto& stem = info.stems[i];
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (info.folder.getChildFile (stem.fileName)));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            continue;

        const int srcLen = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> src (2, srcLen + 16);
        src.clear();
        reader->read (&src, 0, srcLen, 0, true, true);
        if (reader->numChannels == 1)
            src.copyFrom (1, 0, src, 0, 0, srcLen);

        auto track = std::make_unique<LoadedTrack>();
        track->name = stem.name;
        track->stemIndex = (int) i;
        track->gain = stem.gainDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (stem.gainDb);
        track->smoothedGain = track->gain.load();
        track->muted = stem.muted;
        track->outputPair = stem.outputPair;

        if (std::abs (reader->sampleRate - sr) < 1.0)
        {
            track->buffer = std::move (src);
            track->buffer.setSize (2, srcLen, true, false, true);
        }
        else
        {
            const double ratio = reader->sampleRate / sr;
            const int outLen = (int) ((double) srcLen / ratio);
            track->buffer.setSize (2, outLen);
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::LagrangeInterpolator interp;
                interp.process (ratio, src.getReadPointer (ch), track->buffer.getWritePointer (ch), outLen);
            }
        }

        song->length = juce::jmax (song->length, (juce::int64) track->buffer.getNumSamples());
        song->tracks.push_back (std::move (track));
    }

    // Todas las pistas con el mismo largo, así el audio nunca lee fuera del buffer
    for (auto& t : song->tracks)
        t->buffer.setSize (2, (int) song->length, true, true, false);

    return song;
}

void AudioEngine::setSong (std::shared_ptr<LoadedSong> newSong)
{
    std::shared_ptr<LoadedSong> old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (song);
        song = std::move (newSong);
        songLength = song != nullptr ? song->length : 0;
        position = 0;
        pendingSeek = -1;
        playing = false;
        loopStart = 0;
        loopEnd = 0;
        fade = 0.0f;
    }
    // "old" se libera aquí, fuera del lock y fuera del hilo de audio
}

void AudioEngine::play()
{
    if (songLength.load() <= 0)
        return;
    if (position.load() >= songLength.load() && pendingSeek.load() < 0)
        pendingSeek = 0;
    playing = true;
}

void AudioEngine::pause()      { playing = false; }
void AudioEngine::togglePlay() { if (isPlaying()) pause(); else play(); }
void AudioEngine::stop()       { playing = false; pendingSeek = 0; }

void AudioEngine::seekSeconds (double s)
{
    const auto len = songLength.load();
    pendingSeek = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, len - 1),
                                (juce::int64) (s * sampleRate.load()));
}

double AudioEngine::getPositionSeconds() const
{
    auto p = pendingSeek.load();
    if (p < 0)
        p = position.load();
    return (double) p / sampleRate.load();
}

double AudioEngine::getLengthSeconds() const
{
    return (double) songLength.load() / sampleRate.load();
}

void AudioEngine::setLoop (double startSeconds, double endSeconds)
{
    const double sr = sampleRate.load();
    loopStart = (juce::int64) (startSeconds * sr);
    loopEnd = (juce::int64) (endSeconds * sr);
}

void AudioEngine::clearLoop()
{
    loopStart = 0;
    loopEnd = 0;
}

void AudioEngine::setClick (bool enabled, double bpm, double offsetSeconds, float gainDb, int outputPair)
{
    clickBpm = juce::jlimit (20.0, 400.0, bpm);
    clickOffset = juce::jmax (0.0, offsetSeconds);
    clickGain = juce::Decibels::decibelsToGain (gainDb);
    clickPair = outputPair;
    clickOn = enabled;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    sampleRate = sr;
    numOutputs = juce::jmax (1, device->getActiveOutputChannels().countNumberOfSetBits());

    const juce::SpinLock::ScopedLockType sl (songLock);
    if (song != nullptr && std::abs (song->sampleRate - sr) > 1.0)
        needsReload = true;
}

void AudioEngine::outputPairChannels (int pair, int numOuts, int& left, int& right)
{
    left = pair * 2;
    right = left + 1;
    if (right >= numOuts)
    {
        left = 0;
        right = numOuts > 1 ? 1 : 0;
    }
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputs, int numOuts, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOuts; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    if (numOuts <= 0)
        return;

    const juce::SpinLock::ScopedTryLockType sl (songLock);
    if (! sl.isLocked() || song == nullptr)
        return;

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, maxChunk);
        renderChunk (*song, outputs, numOuts, done, n);
        done += n;
    }
}

void AudioEngine::renderChunk (LoadedSong& s, float* const* outputs, int numOuts, int offset, int n)
{
    const bool wantPlay = playing.load();
    auto pending = pendingSeek.load();
    const auto ls = loopStart.load(), le = loopEnd.load();
    const float fadeStep = 1.0f / 256.0f;
    auto pos = position.load();

    // 1) Posición y envolvente de cada muestra (fundidos al arrancar, parar y saltar)
    for (int i = 0; i < n; ++i)
    {
        const bool audible = wantPlay && pending < 0;
        fade = audible ? juce::jmin (1.0f, fade + fadeStep) : juce::jmax (0.0f, fade - fadeStep);

        if (fade <= 0.0f && pending >= 0)
        {
            pos = juce::jlimit ((juce::int64) 0, s.length, pending);
            pendingSeek.compare_exchange_strong (pending, -1);
            pending = -1;
        }

        if (fade > 0.0f && pos < s.length)
        {
            positions[(size_t) i] = pos;
            envelope[(size_t) i] = fade;
            ++pos;
            if (le > ls && pos >= le)
                pos = ls;
        }
        else
        {
            positions[(size_t) i] = -1;
            envelope[(size_t) i] = 0.0f;
        }
    }

    if (pos >= s.length && wantPlay)
        playing = false;   // fin de la canción
    position = pos;

    // 2) Mezcla de pistas
    bool anySolo = false;
    for (auto& t : s.tracks)
        anySolo = anySolo || t->solo.load();

    for (auto& t : s.tracks)
    {
        const bool silent = t->muted.load() || (anySolo && ! t->solo.load());
        const float target = silent ? 0.0f : t->gain.load();

        int l, r;
        outputPairChannels (t->outputPair.load(), numOuts, l, r);
        float* outL = outputs[l];
        float* outR = outputs[r];
        if (outL == nullptr || outR == nullptr)
            continue;

        const float* inL = t->buffer.getReadPointer (0);
        const float* inR = t->buffer.getReadPointer (1);
        float g = t->smoothedGain;
        const float gStep = (target - g) / (float) n;
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            g += gStep;
            const auto p = positions[(size_t) i];
            if (p < 0)
                continue;
            const float e = envelope[(size_t) i] * g;
            const float sl = inL[p] * e, sr = inR[p] * e;
            if (outL == outR)
                outL[offset + i] += 0.5f * (sl + sr);
            else
            {
                outL[offset + i] += sl;
                outR[offset + i] += sr;
            }
            peak = juce::jmax (peak, std::abs (sl), std::abs (sr));
        }

        t->smoothedGain = target;
        if (peak > t->meter.load())
            t->meter = peak;
    }

    // 3) Click (metrónomo) generado
    if (clickOn.load())
    {
        const double sr = sampleRate.load();
        const double spb = sr * 60.0 / clickBpm.load();
        const double off = clickOffset.load() * sr;
        const double clickLen = 0.03 * sr;
        const float cg = clickGain.load();

        int l, r;
        outputPairChannels (clickPair.load(), numOuts, l, r);
        float* outL = outputs[l];
        float* outR = outputs[r];

        if (outL != nullptr && outR != nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto p = positions[(size_t) i];
                if (p < 0)
                    continue;
                const double rel = (double) p - off;
                if (rel < 0.0)
                    continue;
                const double beat = std::floor (rel / spb);
                const double phase = rel - beat * spb;
                if (phase >= clickLen)
                    continue;

                const bool accent = ((juce::int64) beat % 4) == 0;
                const double freq = accent ? 1600.0 : 1000.0;
                const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * freq * phase / sr)
                                         * std::exp (-phase / (0.008 * sr)))
                                * cg * envelope[(size_t) i];
                outL[offset + i] += v;
                if (outR != outL)
                    outR[offset + i] += v;
            }
        }
    }
}

#include "AudioEngine.h"
#include <cmath>
#include <limits>
#include <algorithm>

static constexpr int maxChunk = 2048;

//==============================================================================
WaveformCache WaveformCache::build (const juce::AudioBuffer<float>& b)
{
    WaveformCache c;
    const int n = b.getNumSamples();
    const int bins = (n + binSize - 1) / binSize;
    c.minL.resize ((size_t) bins); c.maxL.resize ((size_t) bins);
    c.minR.resize ((size_t) bins); c.maxR.resize ((size_t) bins);
    const float* l = b.getReadPointer (0);
    const float* r = b.getNumChannels() > 1 ? b.getReadPointer (1) : l;
    for (int i = 0; i < bins; ++i)
    {
        const int start = i * binSize, end = juce::jmin (n, start + binSize);
        auto rl = juce::FloatVectorOperations::findMinAndMax (l + start, end - start);
        auto rr = juce::FloatVectorOperations::findMinAndMax (r + start, end - start);
        c.minL[(size_t) i] = rl.getStart(); c.maxL[(size_t) i] = rl.getEnd();
        c.minR[(size_t) i] = rr.getStart(); c.maxR[(size_t) i] = rr.getEnd();
        c.peak = juce::jmax (c.peak, juce::jmax (std::abs (rl.getStart()), std::abs (rl.getEnd())),
                                      juce::jmax (std::abs (rr.getStart()), std::abs (rr.getEnd())));
    }
    return c;
}

bool WaveformCache::rangeMinMax (juce::int64 from, juce::int64 to, float& mnL, float& mxL, float& mnR, float& mxR) const
{
    const auto bins = (juce::int64) numBins();
    auto b0 = juce::jmax ((juce::int64) 0, from / binSize);
    auto b1 = juce::jmin (bins - 1, (juce::jmax (from, to - 1)) / binSize);
    if (bins == 0 || b0 >= bins || b1 < 0 || to <= from)
        return false;
    mnL = 1.0f; mxL = -1.0f; mnR = 1.0f; mxR = -1.0f;
    for (auto b = b0; b <= b1; ++b)
    {
        mnL = juce::jmin (mnL, minL[(size_t) b]); mxL = juce::jmax (mxL, maxL[(size_t) b]);
        mnR = juce::jmin (mnR, minR[(size_t) b]); mxR = juce::jmax (mxR, maxR[(size_t) b]);
    }
    return true;
}

//==============================================================================

AudioEngine::AudioEngine()
{
    positions.resize (maxChunk);
    envelope.resize (maxChunk);
    positions2.resize (maxChunk);
    xfadeOut.resize (maxChunk);
    xfadeIn.resize (maxChunk);
    levelGain.resize (maxChunk);
    levelSegment.resize (maxChunk);
    laneL.resize (maxChunk);
    laneR.resize (maxChunk);
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
        track->outputPair = juce::jmax (0, stem.outputPair);

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
    {
        t->buffer.setSize (2, (int) song->length, true, true, false);
        t->waveform = WaveformCache::build (t->buffer);
    }

    return song;
}

void AudioEngine::setSamplers (std::shared_ptr<SamplerSet> set)
{
    std::shared_ptr<SamplerSet> old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (samplers);
        samplers = std::move (set);
    }
    // "old" se libera aquí, fuera del lock
}

void AudioEngine::setBeatGrid (std::shared_ptr<const BeatGrid> grid)
{
    std::shared_ptr<const BeatGrid> old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (beatGrid);
        beatGrid = std::move (grid);
    }
    // "old" se libera fuera del lock
}

void AudioEngine::setGainCurve (std::shared_ptr<const GainCurve> curve)
{
    std::shared_ptr<const GainCurve> old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (gainCurve);
        gainCurve = std::move (curve);
    }
}

void AudioEngine::setSong (std::shared_ptr<LoadedSong> newSong)
{
    std::shared_ptr<LoadedSong> old;
    std::shared_ptr<const BeatGrid> oldGrid;
    std::shared_ptr<const GainCurve> oldCurve;
    std::shared_ptr<SamplerSet> oldSamplers;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (song);
        oldGrid = std::move (beatGrid);
        oldCurve = std::move (gainCurve);
        oldSamplers = std::move (samplers);
        levelSmooth = 1.0f;
        song = std::move (newSong);
        songLength = song != nullptr ? song->length : 0;
        position = 0;
        pendingSeek = -1;
        playing = false;
        loopRange = 0;
        fade = 0.0f;
        currentFade = 0.0f;
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
    const auto maxPos = (double) std::numeric_limits<juce::uint32>::max();
    const auto s = (juce::uint64) juce::jlimit (0.0, maxPos, startSeconds * sr);
    const auto e = (juce::uint64) juce::jlimit (0.0, maxPos, endSeconds * sr);
    loopRange = (s << 32) | e;
}

void AudioEngine::clearLoop()
{
    loopRange = 0;
}

void AudioEngine::setClick (bool enabled, double bpm, double offsetSeconds, float gainDb, int outputPair)
{
    clickBpm = juce::jlimit (20.0, 400.0, bpm);
    clickOffset = juce::jmax (0.0, offsetSeconds);
    clickGain = juce::Decibels::decibelsToGain (gainDb);
    clickPair = juce::jmax (0, outputPair);
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

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    // Lo llama JUCE fuera del callback de audio (por ejemplo cuando JACK se cierra)
    {
        const juce::ScopedLock sl (errorLock);
        deviceError = errorMessage;
    }
    deviceErrorPending = true;
}

float AudioEngine::takeOutputPeak (int channel)
{
    return juce::isPositiveAndBelow (channel, maxMeteredOutputs) ? outputPeak[channel].exchange (0.0f) : 0.0f;
}

float AudioEngine::getOutputRms (int channel) const
{
    return juce::isPositiveAndBelow (channel, maxMeteredOutputs) ? outputRms[channel].load() : 0.0f;
}

bool AudioEngine::takeDeviceError (juce::String& message)
{
    if (! deviceErrorPending.exchange (false))
        return false;
    const juce::ScopedLock sl (errorLock);
    message = deviceError;
    return true;
}

void AudioEngine::outputPairChannels (int pair, int numOuts, int& left, int& right)
{
    left = pair < 0 ? 0 : pair * 2;
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

    applyMasterAndMeter (outputs, numOuts, numSamples);
}

void AudioEngine::applyMasterAndMeter (float* const* outputs, int numOuts, int numSamples)
{
    const float target = masterGain.load() * songGain.load();
    const float step = (target - smoothedMaster) / (float) juce::jmax (1, numSamples);
    for (int ch = 0; ch < numOuts; ++ch)
    {
        float* out = outputs[ch];
        if (out == nullptr)
            continue;
        float g = smoothedMaster, peak = 0.0f, sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            g += step;
            out[i] *= g;
            const float a = std::abs (out[i]);
            peak = juce::jmax (peak, a);
            sum += a * a;
        }
        if (ch < maxMeteredOutputs)
        {
            if (peak > outputPeak[ch].load())
                outputPeak[ch] = peak;
            outputRms[ch] = std::sqrt (sum / (float) juce::jmax (1, numSamples));
        }
    }
    smoothedMaster = target;
}

void AudioEngine::renderChunk (LoadedSong& s, float* const* outputs, int numOuts, int offset, int n)
{
    const bool wantPlay = playing.load();
    auto pending = pendingSeek.load();
    const auto range = loopRange.load();
    const auto ls = (juce::int64) (range >> 32), le = (juce::int64) (range & 0xffffffffu);
    const bool looping = le > ls;
    // Crossfade al cerrar el loop: el final de la región se funde con lo que precede a su inicio
    const auto xfLen = looping ? juce::jmin ((juce::int64) fadeSamples, (le - ls) / 2) : (juce::int64) 0;
    const float fadeStep = 1.0f / (float) fadeSamples;
    auto pos = position.load();
    const GainCurve* curve = (gainCurve != nullptr && ! gainCurve->positions.empty()) ? gainCurve.get() : nullptr;
    const float levelCoeff = 1.0f / (0.05f * (float) sampleRate.load());   // rampa de ~50 ms

    // 1) Posición y envolvente de cada muestra (fundidos al arrancar, parar y saltar)
    for (int i = 0; i < n; ++i)
    {
        // Ganancia de nivelado del tramo en el que cae esta muestra (escalón suavizado)
        float levelTarget = 1.0f;
        int segment = 0;
        if (curve != nullptr)
        {
            auto it = std::upper_bound (curve->positions.begin(), curve->positions.end(), pos);
            const auto idx = it == curve->positions.begin() ? 0 : (size_t) (it - curve->positions.begin()) - 1;
            levelTarget = curve->gains[idx];
            segment = (int) idx;
        }
        levelSmooth += (levelTarget - levelSmooth) * levelCoeff;
        levelGain[(size_t) i] = levelSmooth;
        levelSegment[(size_t) i] = segment;

        const bool audible = wantPlay && pending < 0;
        fade = audible ? juce::jmin (1.0f, fade + fadeStep) : juce::jmax (0.0f, fade - fadeStep);

        if (fade <= 0.0f && pending >= 0)
        {
            pos = juce::jlimit ((juce::int64) 0, s.length, pending);
            pendingSeek.compare_exchange_strong (pending, -1);
            pending = -1;
        }

        positions2[(size_t) i] = -1;
        xfadeOut[(size_t) i] = 1.0f;
        xfadeIn[(size_t) i] = 0.0f;

        if (fade > 0.0f && pos < s.length)
        {
            positions[(size_t) i] = pos;
            float env = fade;

            if (looping)
            {
                if (xfLen > 0 && pos >= le - xfLen)
                {
                    // t va de casi 0 a 1 en las últimas xfLen muestras; al llegar a 1 ya suena
                    // la muestra anterior al inicio del loop, así que el salto a ls es continuo.
                    const float t = (float) (pos - (le - xfLen) + 1) / (float) xfLen;
                    xfadeOut[(size_t) i] = std::cos (t * juce::MathConstants<float>::halfPi);
                    xfadeIn[(size_t) i] = std::sin (t * juce::MathConstants<float>::halfPi);
                    const auto pre = pos - (le - ls);
                    positions2[(size_t) i] = pre >= 0 ? pre : -1;   // sin pre-roll si el loop empieza en 0
                }
            }
            else if (s.length - pos <= (juce::int64) fadeSamples)
            {
                env *= (float) (s.length - pos) / (float) fadeSamples;   // fundido al final de la canción
            }

            envelope[(size_t) i] = env;
            ++pos;
            if (looping && pos >= le)
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
    currentFade = fade;

    // 2) Mezcla de pistas
    bool anySolo = false;
    for (auto& t : s.tracks)
        anySolo = anySolo || t->solo.load();
    SamplerSet* sampler = samplers.get();
    if (sampler != nullptr)
        for (auto& lane : sampler->lanes)
            anySolo = anySolo || lane->control.solo.load();

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
        float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;

        if (g <= 0.0f && target <= 0.0f)
        {
            t->smoothedGain = 0.0f;   // pista en silencio: no hay nada que mezclar
            t->rmsL = 0.0f;
            t->rmsR = 0.0f;
            continue;
        }

        // Nivelado por pista: la curva de este stem (si la hay), suavizada con su propio estado
        const std::vector<float>* trackCurve = nullptr;
        if (curve != nullptr && t->stemIndex >= 0 && t->stemIndex < (int) curve->trackGains.size()
            && ! curve->trackGains[(size_t) t->stemIndex].empty())
            trackCurve = &curve->trackGains[(size_t) t->stemIndex];
        float tl = t->levelSmooth;

        for (int i = 0; i < n; ++i)
        {
            g += gStep;
            if (trackCurve != nullptr)
            {
                const auto seg = (size_t) juce::jmin (levelSegment[(size_t) i], (int) trackCurve->size() - 1);
                tl += ((*trackCurve)[seg] - tl) * levelCoeff;
            }
            else
                tl += (1.0f - tl) * levelCoeff;
            const auto p = positions[(size_t) i];
            if (p < 0)
                continue;
            const float e = envelope[(size_t) i] * g * levelGain[(size_t) i] * tl;
            float sl = inL[p], sr = inR[p];
            const auto p2 = positions2[(size_t) i];
            if (p2 >= 0)
            {
                sl = sl * xfadeOut[(size_t) i] + inL[p2] * xfadeIn[(size_t) i];
                sr = sr * xfadeOut[(size_t) i] + inR[p2] * xfadeIn[(size_t) i];
            }
            else
            {
                sl *= xfadeOut[(size_t) i];
                sr *= xfadeOut[(size_t) i];
            }
            sl *= e;
            sr *= e;
            if (outL == outR)
                outL[offset + i] += 0.5f * (sl + sr);
            else
            {
                outL[offset + i] += sl;
                outR[offset + i] += sr;
            }
            peakL = juce::jmax (peakL, std::abs (sl));
            peakR = juce::jmax (peakR, std::abs (sr));
            sumL += sl * sl;
            sumR += sr * sr;
        }

        t->smoothedGain = target;
        t->levelSmooth = tl;
        if (peakL > t->peakL.load()) t->peakL = peakL;
        if (peakR > t->peakR.load()) t->peakR = peakR;
        t->rmsL = std::sqrt (sumL / (float) n);
        t->rmsR = std::sqrt (sumR / (float) n);
    }

    // 2b) Sampler de triggers: las voces disparadas por los golpes de cada línea
    if (sampler != nullptr)
    {
        for (auto& lanePtr : sampler->lanes)
        {
            auto& lane = *lanePtr;
            auto& c = lane.control;
            const bool silent = c.muted.load() || (anySolo && ! c.solo.load());
            const float target = silent ? 0.0f : c.gain.load();
            float g = c.smoothedGain;
            const float gStep = (target - g) / (float) n;
            const bool hasBank = lane.bank != nullptr && ! lane.bank->hits.empty();

            for (int i = 0; i < n; ++i)
            {
                laneL[(size_t) i] = laneR[(size_t) i] = 0.0f;
                const auto p = positions[(size_t) i];
                if (p < 0 || ! hasBank)
                    continue;
                if (p != lane.lastPos + 1)
                {
                    // Salto o arranque: buscar el primer golpe en o después de esta muestra
                    auto it = std::lower_bound (lane.events.begin(), lane.events.end(), p,
                                                [] (const TriggerEvent& e, juce::int64 v) { return e.sample < v; });
                    lane.nextEvent = (size_t) (it - lane.events.begin());
                }
                lane.lastPos = p;
                while (lane.nextEvent < lane.events.size() && lane.events[lane.nextEvent].sample <= p)
                {
                    const auto& ev = lane.events[lane.nextEvent++];
                    if (ev.sample != p)
                        continue;   // quedó atrás (por ejemplo tras un salto): no se dispara
                    const int hit = lane.bank->pick (ev.velocity, lane.lastHit);
                    if (hit < 0)
                        continue;
                    // Voz libre, o la más avanzada
                    SamplerLane::Voice* v = nullptr;
                    for (auto& voice : lane.voices)
                        if (voice.buffer == nullptr) { v = &voice; break; }
                    if (v == nullptr)
                    {
                        v = &lane.voices[0];
                        for (auto& voice : lane.voices)
                            if (voice.pos > v->pos) v = &voice;
                    }
                    v->buffer = &lane.bank->hits[(size_t) hit].buffer;
                    v->pos = -i;   // empieza en la muestra i de este bloque
                    v->gain = 0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, ev.velocity);
                }
            }
            // Render de las voces activas
            for (auto& v : lane.voices)
            {
                if (v.buffer == nullptr)
                    continue;
                const int len = v.buffer->getNumSamples();
                const float* bl = v.buffer->getReadPointer (0);
                const float* br = v.buffer->getReadPointer (juce::jmin (1, v.buffer->getNumChannels() - 1));
                for (int i = 0; i < n; ++i)
                {
                    const int k = v.pos + i;
                    if (k < 0) continue;
                    if (k >= len) break;
                    laneL[(size_t) i] += bl[k] * v.gain;
                    laneR[(size_t) i] += br[k] * v.gain;
                }
                v.pos += n;
                if (v.pos >= len)
                    v.buffer = nullptr;
            }
            // Ganancia, envolvente, salida y medidores
            int l, r;
            outputPairChannels (c.outputPair.load(), numOuts, l, r);
            float* outL = outputs[l];
            float* outR = outputs[r];
            float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
            if (outL != nullptr && outR != nullptr && (g > 0.0f || target > 0.0f))
            {
                for (int i = 0; i < n; ++i)
                {
                    g += gStep;
                    const float e = envelope[(size_t) i] * g;
                    const float sl = laneL[(size_t) i] * e, sr2 = laneR[(size_t) i] * e;
                    if (outL == outR)
                        outL[offset + i] += 0.5f * (sl + sr2);
                    else
                    {
                        outL[offset + i] += sl;
                        outR[offset + i] += sr2;
                    }
                    peakL = juce::jmax (peakL, std::abs (sl));
                    peakR = juce::jmax (peakR, std::abs (sr2));
                    sumL += sl * sl;
                    sumR += sr2 * sr2;
                }
            }
            c.smoothedGain = target;
            if (peakL > c.peakL.load()) c.peakL = peakL;
            if (peakR > c.peakR.load()) c.peakR = peakR;
            c.rmsL = std::sqrt (sumL / (float) n);
            c.rmsR = std::sqrt (sumR / (float) n);
        }
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

        const BeatGrid* grid = (beatGrid != nullptr && ! beatGrid->positions.empty()) ? beatGrid.get() : nullptr;

        if (outL != nullptr && outR != nullptr)
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto p = positions[(size_t) i];
                if (p < 0)
                    continue;
                double phase;
                bool accent;
                if (grid != nullptr)
                {
                    // Último tiempo detectado en o antes de p (búsqueda binaria, sin memoria dinámica)
                    auto it = std::upper_bound (grid->positions.begin(), grid->positions.end(), p);
                    if (it == grid->positions.begin())
                        continue;
                    const auto idx = (size_t) (it - grid->positions.begin()) - 1;
                    phase = (double) (p - grid->positions[idx]);
                    accent = grid->beatInBar[idx] == 1;
                }
                else
                {
                    const double rel = (double) p - off;
                    if (rel < 0.0)
                        continue;
                    const double beat = std::floor (rel / spb);
                    phase = rel - beat * spb;
                    accent = ((juce::int64) beat % 4) == 0;
                }
                if (phase >= clickLen)
                    continue;

                const double freq = accent ? 1600.0 : 1000.0;
                const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * freq * phase / sr)
                                         * std::exp (-phase / (0.008 * sr)))
                                * cg * envelope[(size_t) i];
                outL[offset + i] += v;
                if (outR != outL)
                    outR[offset + i] += v;
                peak = juce::jmax (peak, std::abs (v));
            }
            if (peak > clickPeak.load())
                clickPeak = peak;
        }
    }
}

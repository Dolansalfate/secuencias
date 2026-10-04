#include "AudioEngine.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <iterator>

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
    auditionL.resize (maxChunk);
    auditionR.resize (maxChunk);
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
        track->songTime = stem.songTime;
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

void AudioEngine::setInstruments (std::shared_ptr<InstrumentSet> set)
{
    std::shared_ptr<InstrumentSet> old;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (instruments);
        instruments = std::move (set);
    }
    // "old" se libera aquí, fuera del lock y en el hilo de mensajes (destrucción de plugins)
}

namespace
{
    // Lo que un cambio de conjunto deja para liberar fuera del lock (y fuera del hilo de audio)
    struct SamplerGarbage
    {
        std::shared_ptr<SamplerSet> sets[3];
    };

    // Pone `set` en `slot` (bajo songLock) y lo encadena con el que reemplaza para el relevo en el hilo de audio
    // (SamplerSet::previous). Solo se guarda un eslabón: si el anterior ya sonó, él es el previo (y el suyo se
    // suelta); si nunca sonó (dos cambios seguidos), las voces siguen en su previo, que pasa al nuevo.
    void installSamplerSet (std::shared_ptr<SamplerSet>& slot, std::shared_ptr<SamplerSet> set, SamplerGarbage& garbage)
    {
        if (set != nullptr && set == slot)
            return;   // el mismo conjunto otra vez: nada que relevar
        auto old = std::move (slot);
        if (set != nullptr)
            garbage.sets[0] = std::move (set->previous);   // un conjunto que se vuelve a poner no arrastra su relevo viejo
        if (old != nullptr)
        {
            if (set != nullptr && ! old->adopted && old->previous != set)
                set->previous = std::move (old->previous);
            else
                garbage.sets[1] = std::move (old->previous);
            if (set != nullptr && old->adopted)
                set->previous = old;
        }
        garbage.sets[2] = std::move (old);
        slot = std::move (set);
    }

    // Relevo (hilo de audio, la primera vez que suena un conjunto): cada línea nueva sigue desde donde iba la
    // equivalente del conjunto anterior (sin repetir los golpes que esa ya disparó) y, si usa el mismo banco, se
    // queda con sus voces. Sin memoria dinámica: búsqueda lineal entre líneas y binaria en los golpes.
    void adoptPlayback (SamplerSet& set)
    {
        set.adopted = true;
        if (set.previous == nullptr)
            return;
        for (auto& lanePtr : set.lanes)
        {
            auto& lane = *lanePtr;
            for (auto& oldPtr : set.previous->lanes)
            {
                auto& old = *oldPtr;
                if (old.stemIndex != lane.stemIndex || old.midiTrack != lane.midiTrack || old.midiPad != lane.midiPad
                    || old.instrumentId != lane.instrumentId || old.note != lane.note || old.midiSource != lane.midiSource)
                    continue;   // (tras quitar una fila o una pista los índices pasan a otra: sus voces no se cambian de canal)
                lane.lastPos = old.lastPos;
                lane.lastHit = old.lastHit;
                const auto it = std::lower_bound (lane.events.begin(), lane.events.end(), old.lastPos + 1,
                                                  [] (const TriggerEvent& e, juce::int64 v) { return e.sample < v; });
                lane.nextEvent = (size_t) (it - lane.events.begin());
                if (old.bank != nullptr && old.bank == lane.bank)
                    for (size_t v = 0; v < std::size (lane.voices); ++v)
                    {
                        lane.voices[v] = old.voices[v];
                        old.voices[v].buffer = nullptr;   // ya no es del anterior (no suena dos veces)
                    }
                break;
            }
        }
    }

    // Muestra de la línea de tiempo que se mira en la posición p0 con `ahead` muestras de anticipación (el pre-roll
    // del banco o la latencia del instrumento). Con loop, lo que pasa del final sigue desde el inicio: el golpe del
    // inicio del loop se prepara antes de dar la vuelta, igual que al llegar a él de corrido, y los que están después
    // del final no suenan.
    juce::int64 lookAhead (juce::int64 p0, juce::int64 ahead, bool looping, juce::int64 ls, juce::int64 le)
    {
        const auto p = p0 + ahead;
        if (looping && p0 < le && p >= le && le - ls > ahead)
            return ls + (p - le);
        return p;
    }

    // Nota hacia un instrumento del rack en la muestra `sample` del trozo, con su note-off holdSamples después (en un
    // hueco libre; si no hay, se cierra antes la nota más antigua)
    void sendInstrumentNote (InstrumentLane& dest, int note, float velocity, int sample)
    {
        dest.midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), sample);
        InstrumentLane::PendingOff* slot = nullptr;
        for (auto& o : dest.offs)
            if (o.note < 0) { slot = &o; break; }
        if (slot == nullptr)
        {
            slot = &dest.offs[0];
            for (auto& o : dest.offs)
                if (o.remaining < slot->remaining) slot = &o;
            dest.midi.addEvent (juce::MidiMessage::noteOff (1, slot->note), sample);
        }
        slot->note = note;
        slot->remaining = sample + dest.holdSamples;
    }

    // Voz libre de una línea del sampler, o la más avanzada (se roba)
    SamplerLane::Voice& voiceFor (SamplerLane& lane)
    {
        for (auto& voice : lane.voices)
            if (voice.buffer == nullptr)
                return voice;
        auto* v = &lane.voices[0];
        for (auto& voice : lane.voices)
            if (voice.pos > v->pos) v = &voice;
        return *v;
    }
}

void AudioEngine::audition (int stemIndex, int midiTrack, int midiPad, int velocity)
{
    // Velocidad en los bits 0-7, fila + 1 en 8-23, pista MIDI + 1 en 24-39 y stem + 1 en 40-55
    const auto field = [] (int v) { return (juce::uint64) (juce::jlimit (-1, 0xfffe, v) + 1); };
    auditionRequest = (juce::uint64) juce::jlimit (1, 127, velocity) | (field (midiPad) << 8) | (field (midiTrack) << 24)
                      | (field (stemIndex) << 40);
}

void AudioEngine::setMidiSamplers (std::shared_ptr<SamplerSet> set)
{
    SamplerGarbage garbage;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        installSamplerSet (midiSamplers, std::move (set), garbage);
    }
    // lo viejo se libera aquí, fuera del lock (salvo el anterior, que queda como previo del nuevo para el relevo)
}

void AudioEngine::setSamplers (std::shared_ptr<SamplerSet> set)
{
    SamplerGarbage garbage;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        installSamplerSet (samplers, std::move (set), garbage);
    }
    // lo viejo se libera aquí, fuera del lock
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
    std::shared_ptr<SamplerSet> oldSamplers, oldMidi;
    {
        const juce::SpinLock::ScopedLockType sl (songLock);
        old = std::move (song);
        oldGrid = std::move (beatGrid);
        oldCurve = std::move (gainCurve);
        oldSamplers = std::move (samplers);
        oldMidi = std::move (midiSamplers);
        levelSmooth = 1.0f;
        song = std::move (newSong);
        songLength = song != nullptr ? song->length : 0;
        position = 0;
        pendingSeek = -1;
        playing = false;
        loopRange = 0;
        fade = 0.0f;
        currentFade = 0.0f;
        // Lo de "Escuchar" se va con las líneas; la compuerta de los instrumentos (que siguen) se cierra
        auditionActive = false;
        auditionFading = false;
        auditionLevel = 1.0f;
        if (instruments != nullptr)
            for (auto& lane : instruments->lanes)
            {
                lane->auditionHold = 0;
                lane->auditionGate = 0.0f;
                lane->auditionNoteIn = -1;
            }
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

void AudioEngine::pause()      { playing = false; auditionCancel = true; }
void AudioEngine::togglePlay() { if (isPlaying()) pause(); else play(); }
void AudioEngine::stop()       { playing = false; pendingSeek = 0; auditionCancel = true; }

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
    numInputs = device->getActiveInputChannels().countNumberOfSetBits();
    inputLatency = device->getInputLatencyInSamples();
    outputLatency = device->getOutputLatencyInSamples();

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

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* ins, int numIns,
                                                    float* const* outputs, int numOuts, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    struct DepthGuard
    {
        explicit DepthGuard (std::atomic<int>& d) : depth (d) { ++depth; }
        ~DepthGuard() { --depth; }
        std::atomic<int>& depth;
    } guard (callbackDepth);

    for (int ch = 0; ch < numOuts; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Entrada: grabación, medidor y escucha (antes de la canción, así pasa por el fader maestro)
    processInput (ins, numIns, outputs, numOuts, numSamples);

    if (numOuts <= 0)
        return;

    {
        const juce::SpinLock::ScopedTryLockType sl (songLock);
        if (sl.isLocked() && song != nullptr)
            for (int done = 0; done < numSamples;)
            {
                const int n = juce::jmin (numSamples - done, maxChunk);
                renderChunk (*song, outputs, numOuts, done, n);
                done += n;
            }
    }

    applyMasterAndMeter (outputs, numOuts, numSamples);
}

void AudioEngine::processInput (const float* const* ins, int numIns, float* const* outputs, int numOuts, int n)
{
    const RecordSetup* rec = recorder.load();
    float target = 0.0f;
    if (rec != nullptr && numIns > 0 && ins != nullptr)
    {
        const int li = juce::jlimit (0, numIns - 1, rec->inputL);
        const int ri = rec->inputR >= 0 && rec->inputR < numIns ? rec->inputR : -1;
        const float* inL = ins[li];
        const float* inR = ri >= 0 ? ins[ri] : nullptr;
        if (inL != nullptr)
        {
            if (rec->writer != nullptr)
            {
                if (recordStartPosition.load() < 0)
                    recordStartPosition = position.load();
                const float* chans[2] = { inL, inR != nullptr ? inR : inL };
                if (rec->writer->write (chans, n))
                    recordedSamples += n;
                else
                    droppedSamples += n;
            }
            float pk = 0.0f;
            for (int i = 0; i < n; ++i)
                pk = juce::jmax (pk, std::abs (inL[i]), inR != nullptr ? std::abs (inR[i]) : 0.0f);
            if (pk > inputPeak.load())
                inputPeak = pk;
            monitorL = li;
            monitorR = ri;
            monitorOutPair = rec->monitorPair;
            target = rec->monitor ? juce::jmax (0.0f, rec->monitorGain) : 0.0f;
        }
    }

    // Escucha de la entrada por el par elegido, con rampa (también para apagarse al dejar de grabar)
    if (numOuts <= 0 || numIns <= 0 || ins == nullptr || (target <= 0.0f && monitorSmooth <= 0.0005f))
    {
        monitorSmooth = target;
        return;
    }
    const int li = juce::jlimit (0, numIns - 1, monitorL);
    const int ri = monitorR >= 0 && monitorR < numIns ? monitorR : li;
    const float* inL = ins[li];
    const float* inR = ins[ri] != nullptr ? ins[ri] : inL;
    if (inL == nullptr)
    {
        monitorSmooth = target;
        return;
    }
    int l, r;
    outputPairChannels (monitorOutPair, numOuts, l, r);
    float* outL = outputs[l];
    float* outR = outputs[r];
    const float step = (target - monitorSmooth) / (float) juce::jmax (1, n);
    float g = monitorSmooth;
    if (outL != nullptr && outL == outR)
    {
        for (int i = 0; i < n; ++i)
        {
            g += step;
            outL[i] += 0.5f * (inL[i] + inR[i]) * g;
        }
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            g += step;
            if (outL != nullptr) outL[i] += inL[i] * g;
            if (outR != nullptr) outR[i] += inR[i] * g;
        }
    }
    monitorSmooth = target;
}

void AudioEngine::setRecorder (const RecordSetup& setup)
{
    recordSlotIndex = 1 - recordSlotIndex;
    recordSlots[recordSlotIndex] = setup;
    recordStartPosition = -1;
    recordedSamples = 0;
    droppedSamples = 0;
    recorder = &recordSlots[recordSlotIndex];
    waitForCallback();
}

void AudioEngine::clearRecorder()
{
    recorder = nullptr;
    waitForCallback();
}

void AudioEngine::waitForCallback()
{
    // Tras publicar la configuración nueva basta ver el callback fuera una vez: el siguiente ya lee la nueva
    for (int i = 0; i < 200 && callbackDepth.load() != 0; ++i)
        juce::Thread::sleep (1);
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
    SamplerSet* midiSampler = midiSamplers.get();
    for (SamplerSet* set : { sampler, midiSampler })
        if (set != nullptr)
            for (auto& lane : set->lanes)
                anySolo = anySolo || lane->control.solo.load();
    InstrumentSet* inst = instruments.get();
    if (inst != nullptr)
        for (auto& lane : inst->lanes)
            anySolo = anySolo || lane->control.solo.load();

    for (auto& t : s.tracks)
    {
        const bool silent = t->muted.load() || t->replaced.load() || (anySolo && ! t->solo.load());
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

    // 2b) Sampler de triggers (en vivo y pistas MIDI): las voces disparadas por los golpes de cada línea.
    // Un conjunto recién puesto toma primero el estado del que reemplaza (voces que suenan, hasta dónde miró); las
    // voces del anterior que no pasaron (sonido cambiado, fila quitada) se terminan de oír desde él (pasadas 2 y 3).
    for (SamplerSet* set : { sampler, midiSampler })
        if (set != nullptr && ! set->adopted)
            adoptPlayback (*set);
    // "Escuchar" pedido desde la UI: un golpe ya en la línea que corresponde, aunque el transporte esté detenido
    const auto aud = auditionRequest.exchange (0);
    if (auditionCancel.exchange (false))
        auditionFading = true;
    if (aud != 0)
    {
        auditionFading = false;   // un golpe pedido después de la pausa suena entero
        auditionLevel = 1.0f;
    }
    // Nivel de lo de "Escuchar" en este trozo: 1, o bajando a 0 en fadeSamples tras pause/stop
    const float audFrom = auditionLevel;
    if (auditionFading)
        auditionLevel = juce::jmax (0.0f, auditionLevel - fadeStep * (float) n);
    const float audStep = (auditionLevel - audFrom) / (float) n;
    bool auditionAlive = false;
    const int audVelocity = (int) (aud & 0xff);
    const int audPad = (int) ((aud >> 8) & 0xffff) - 1, audTrack = (int) ((aud >> 24) & 0xffff) - 1,
              audStem = (int) ((aud >> 40) & 0xffff) - 1;
    const auto isAudition = [&] (const SamplerLane& lane)
    {
        return aud != 0 && (audTrack >= 0 ? lane.midiTrack == audTrack && lane.midiPad == audPad
                                          : lane.midiTrack < 0 && lane.stemIndex == audStem);
    };
    SamplerSet* const passes[4] = { sampler, midiSampler,
                                    sampler != nullptr ? sampler->previous.get() : nullptr,
                                    midiSampler != nullptr ? midiSampler->previous.get() : nullptr };
    for (int pass = 0; pass < 4; ++pass)
    {
        SamplerSet* set = passes[pass];
        const bool triggering = pass < 2;   // las pasadas 2 y 3 solo terminan las voces que quedaron
        if (set == nullptr)
            continue;
        for (auto& lanePtr : set->lanes)
        {
            auto& lane = *lanePtr;
            auto& c = lane.control;
            if (! triggering)
            {
                bool sounding = false;
                for (auto& v : lane.voices)
                    sounding = sounding || v.buffer != nullptr;
                if (! sounding)
                    continue;
            }
            const bool silent = lane.silenced || c.muted.load() || (anySolo && ! c.solo.load());
            const float target = silent ? 0.0f : c.gain.load();
            float g = c.smoothedGain;
            const float gStep = (target - g) / (float) n;
            // Línea que dispara un instrumento del rack: sus golpes salen como notas MIDI hacia ese plugin
            if (lane.instrumentId >= 0)
            {
                if (! triggering)
                    continue;
                InstrumentLane* dest = nullptr;
                if (inst != nullptr)
                    for (auto& il : inst->lanes)
                        if (il->id == lane.instrumentId)
                        {
                            dest = il.get();
                            break;
                        }
                if (dest == nullptr)
                    continue;
                if (isAudition (lane))
                {
                    dest->auditionNote = lane.note;
                    dest->auditionVelocity = (float) audVelocity / 127.0f;
                    dest->auditionNoteIn = juce::jmax (0, fadeSamples - dest->latency);   // sale en 2c)
                    dest->auditionHold = (int) (3.0 * sampleRate.load());
                }
                if (lane.silenced || lane.superseded.load())
                    continue;
                const juce::int64 lead = dest->latency;   // el plugin tarda esto en sonar: el golpe se le manda antes
                for (int i = 0; i < n; ++i)
                {
                    const auto p0 = positions[(size_t) i];
                    if (p0 < 0)
                        continue;
                    const auto p = lookAhead (p0, lead, looping, ls, le);
                    if (p != lane.lastPos + 1)
                    {
                        // Salto, arranque o vuelta del loop: desde lo que suena ahora (o desde el inicio del loop si la
                        // anticipación ya dio la vuelta); los golpes entre eso y p salen ya, algo tarde, en vez de perderse
                        const auto from = p >= p0 ? p0 : ls;
                        auto it = std::lower_bound (lane.events.begin(), lane.events.end(), from,
                                                    [] (const TriggerEvent& e, juce::int64 v) { return e.sample < v; });
                        lane.nextEvent = (size_t) (it - lane.events.begin());
                    }
                    lane.lastPos = p;
                    while (lane.nextEvent < lane.events.size() && lane.events[lane.nextEvent].sample <= p)
                    {
                        const auto& ev = lane.events[lane.nextEvent++];
                        if (looping && ev.sample >= le)
                            continue;   // después del final del loop: no se oye mientras el loop siga
                        const float vel = lane.rawVelocity ? juce::jlimit (1.0f / 127.0f, 1.0f, ev.velocity)
                                                           : 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, ev.velocity);
                        sendInstrumentNote (*dest, lane.note, vel, i);   // note-off 50 ms después
                    }
                }
                continue;
            }

            const bool hasBank = lane.bank != nullptr && ! lane.bank->hits.empty();
            const juce::int64 preRoll = hasBank ? lane.bank->preRoll : 0;   // las muestras traen esto antes del ataque: se adelantan
            if (triggering && hasBank && isAudition (lane))
            {
                // La muestra desde su inicio (el pre-roll son 2 ms), con la fuerza de un golpe de pista MIDI de esa velocidad
                const float strength = juce::jlimit (0.0f, 1.0f, ((float) audVelocity / 127.0f - 0.25f) / 0.75f);
                const int hit = lane.bank->pick (strength, lane.lastHit);
                if (hit >= 0)
                {
                    auto& v = voiceFor (lane);
                    v.buffer = &lane.bank->hits[(size_t) hit].buffer;
                    v.pos = 0;
                    v.gain = 0.5f + 0.5f * strength;
                    v.audition = true;
                }
            }

            const bool firing = triggering && hasBank && ! lane.silenced && ! lane.superseded.load();
            for (int i = 0; i < n; ++i)
            {
                laneL[(size_t) i] = laneR[(size_t) i] = 0.0f;
                const auto p0 = positions[(size_t) i];
                if (p0 < 0 || ! firing)
                    continue;
                const auto p = lookAhead (p0, preRoll, looping, ls, le);
                if (p != lane.lastPos + 1)
                {
                    // Salto, arranque o vuelta del loop: los golpes desde lo que suena ahora (o desde el inicio del loop
                    // si la anticipación ya dio la vuelta) hasta p ya debían haber empezado su pre-roll: suenan ahora,
                    // con la voz adelantada lo que llegan tarde (el ataque cae en su lugar)
                    const auto from = p >= p0 ? p0 : ls;
                    auto it = std::lower_bound (lane.events.begin(), lane.events.end(), from,
                                                [] (const TriggerEvent& e, juce::int64 v) { return e.sample < v; });
                    lane.nextEvent = (size_t) (it - lane.events.begin());
                }
                lane.lastPos = p;
                while (lane.nextEvent < lane.events.size() && lane.events[lane.nextEvent].sample <= p)
                {
                    const auto& ev = lane.events[lane.nextEvent++];
                    const auto late = p - ev.sample;
                    if (late > preRoll || (looping && ev.sample >= le))
                        continue;   // quedó atrás, o está después del final del loop
                    // Fuerza del golpe: la del trigger en vivo, o la velocidad de la pista MIDI sin la curva de los instrumentos
                    const float strength = lane.rawVelocity ? juce::jlimit (0.0f, 1.0f, (ev.velocity - 0.25f) / 0.75f)
                                                            : juce::jlimit (0.0f, 1.0f, ev.velocity);
                    const int hit = lane.bank->pick (strength, lane.lastHit);
                    if (hit < 0)
                        continue;
                    auto& v = voiceFor (lane);
                    v.buffer = &lane.bank->hits[(size_t) hit].buffer;
                    v.pos = (int) late - i;   // empieza en la muestra i de este bloque (más adentro si llega tarde)
                    v.gain = 0.5f + 0.5f * strength;
                    v.audition = false;
                }
            }
            // Render de las voces activas (las de "Escuchar" aparte: no llevan la envolvente del transporte)
            bool anyAudition = false;
            for (auto& v : lane.voices)
                anyAudition = anyAudition || (v.buffer != nullptr && v.audition);
            auditionAlive = auditionAlive || anyAudition;
            if (anyAudition)
            {
                juce::FloatVectorOperations::clear (auditionL.data(), n);
                juce::FloatVectorOperations::clear (auditionR.data(), n);
            }
            for (auto& v : lane.voices)
            {
                if (v.buffer == nullptr)
                    continue;
                const int len = v.buffer->getNumSamples();
                const float* bl = v.buffer->getReadPointer (0);
                const float* br = v.buffer->getReadPointer (juce::jmin (1, v.buffer->getNumChannels() - 1));
                float* dl = v.audition ? auditionL.data() : laneL.data();
                float* dr = v.audition ? auditionR.data() : laneR.data();
                for (int i = 0; i < n; ++i)
                {
                    const int k = v.pos + i;
                    if (k < 0) continue;
                    if (k >= len) break;
                    dl[i] += bl[k] * v.gain;
                    dr[i] += br[k] * v.gain;
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
                    float sl = laneL[(size_t) i] * e, sr2 = laneR[(size_t) i] * e;
                    if (anyAudition)
                    {
                        const float a = g * (audFrom + audStep * (float) (i + 1));
                        sl += auditionL[(size_t) i] * a;
                        sr2 += auditionR[(size_t) i] * a;
                    }
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

    // 2c) Instrumentos del rack: note-offs pendientes, proceso del plugin y mezcla de su salida
    if (inst != nullptr)
    {
        for (auto& lanePtr : inst->lanes)
        {
            auto& lane = *lanePtr;
            auto& c = lane.control;
            if (lane.plugin == nullptr)
                continue;
            if (lane.auditionNoteIn >= 0)
            {
                if (lane.auditionNoteIn < n)
                {
                    sendInstrumentNote (lane, lane.auditionNote, lane.auditionVelocity, lane.auditionNoteIn);
                    lane.auditionNoteIn = -1;
                }
                else
                    lane.auditionNoteIn -= n;
            }
            for (auto& o : lane.offs)
            {
                if (o.note < 0)
                    continue;
                if (o.remaining < n)
                {
                    lane.midi.addEvent (juce::MidiMessage::noteOff (1, o.note), juce::jmax (0, o.remaining));
                    o.note = -1;
                }
                else
                    o.remaining -= n;
            }
            const int chans = lane.work.getNumChannels();
            juce::AudioBuffer<float> view (lane.work.getArrayOfWritePointers(), chans, n);
            view.clear();
            if (! lane.plugin->isSuspended())
                lane.plugin->processBlock (view, lane.midi);
            lane.midi.clear();

            const bool silent = c.muted.load() || (anySolo && ! c.solo.load());
            const float target = silent ? 0.0f : c.gain.load();
            float g = c.smoothedGain;
            const float gStep = (target - g) / (float) n;
            const float* srcL = view.getReadPointer (0);
            const float* srcR = view.getReadPointer (juce::jmin (1, chans - 1));
            int l, r;
            outputPairChannels (c.outputPair.load(), numOuts, l, r);
            float* outL = outputs[l];
            float* outR = outputs[r];
            float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
            // Compuerta de "Escuchar": deja pasar el plugin aunque el transporte esté detenido mientras dura
            const float gateUp = fadeStep, gateDown = 1.0f / (0.3f * (float) sampleRate.load());
            float gate = lane.auditionGate;
            int hold = lane.auditionHold;
            if (outL != nullptr && outR != nullptr && (g > 0.0f || target > 0.0f))
            {
                for (int i = 0; i < n; ++i)
                {
                    g += gStep;
                    if (hold > 0)
                    {
                        --hold;
                        gate = juce::jmin (1.0f, gate + gateUp);
                    }
                    else
                        gate = juce::jmax (0.0f, gate - gateDown);
                    const float e = juce::jmax (envelope[(size_t) i], gate * (audFrom + audStep * (float) (i + 1))) * g;
                    const float sl = srcL[i] * e, sr2 = srcR[i] * e;
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
            else
            {
                hold = juce::jmax (0, hold - n);   // canal en silencio: la compuerta sigue su curso igual
                gate = hold > 0 ? 1.0f : 0.0f;
            }
            lane.auditionHold = hold;
            lane.auditionGate = gate;
            auditionAlive = auditionAlive || hold > 0 || gate > 0.0f || lane.auditionNoteIn >= 0;
            c.smoothedGain = target;
            if (peakL > c.peakL.load()) c.peakL = peakL;
            if (peakR > c.peakR.load()) c.peakR = peakR;
            c.rmsL = std::sqrt (sumL / (float) n);
            c.rmsR = std::sqrt (sumR / (float) n);
        }
    }

    // Fin del fundido de "Escuchar" (pause o stop): se sueltan sus voces y se cierran las compuertas. Mientras quede algo
    // sonando, isSilent() espera (así cambiar de canción no lo corta de golpe)
    if (auditionFading && auditionLevel <= 0.0f)
    {
        for (SamplerSet* set : passes)
            if (set != nullptr)
                for (auto& lanePtr : set->lanes)
                    for (auto& v : lanePtr->voices)
                        if (v.audition)
                            v.buffer = nullptr;
        if (inst != nullptr)
            for (auto& lanePtr : inst->lanes)
            {
                lanePtr->auditionHold = 0;
                lanePtr->auditionGate = 0.0f;
                lanePtr->auditionNoteIn = -1;
            }
        auditionFading = false;
        auditionLevel = 1.0f;
        auditionAlive = false;
    }
    auditionActive = auditionAlive;

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

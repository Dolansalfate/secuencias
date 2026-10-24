#include "Bounce.h"
#include "Loudness.h"
#include <cmath>

namespace bounce
{
    namespace
    {
        // Una fuente del motor y lo que tenía antes del render (se repone al terminar)
        struct Routed
        {
            LoadedTrack* control = nullptr;
            juce::String name;
            int pair = 0;
            int savedPair = 0;
            bool savedSolo = false;
        };
    }

    Result render (AudioEngine& engine, LoadedSong& song, const Options& options,
                   const std::function<bool()>& shouldAbort, const std::function<void (float)>& progress)
    {
        Result result;
        const auto total = song.length;
        if (total <= 0)
            return result;

        // Cada fuente a un par propio: pistas, líneas del sampler con banco (las que mandan notas a un instrumento
        // suenan por el instrumento) e instrumentos; el click al par siguiente
        std::vector<Routed> routed;
        auto add = [&routed] (LoadedTrack& control, const juce::String& name)
        {
            Routed r;
            r.control = &control;
            r.name = name;
            r.pair = (int) routed.size();
            r.savedPair = control.outputPair.load();
            r.savedSolo = control.solo.load();
            routed.push_back (r);
        };
        for (auto& t : song.tracks)
            add (*t, t->name);
        const auto samplers = engine.getSamplers();
        const auto midiSamplers = engine.getMidiSamplers();
        for (const auto& set : { samplers, midiSamplers })
            if (set != nullptr)
                for (auto& lane : set->lanes)
                    if (lane->instrumentId < 0 && lane->bank != nullptr && ! lane->silenced)
                        add (lane->control, lane->name);
        const auto instruments = engine.getInstruments();
        if (instruments != nullptr)
            for (auto& lane : instruments->lanes)
                if (lane != nullptr)
                    add (lane->control, lane->name);
        const int clickPair = (int) routed.size();
        const int outputs = 2 * (clickPair + 1);
        // Todo vuelve a como estaba al salir, también si algo lanza una excepción (falta de memoria al reservar el audio
        // de cada fuente): si no, el ruteo de exportar quedaba en el motor y se guardaba en la canción
        struct Restore
        {
            AudioEngine& engine;
            std::vector<Routed>& routed;
            std::shared_ptr<InstrumentSet> instruments;
            bool clickWasOn;
            int clickWasPair;
            ~Restore()
            {
                engine.pause();
                for (auto& r : routed)
                {
                    r.control->outputPair = r.savedPair;
                    r.control->solo = r.savedSolo;
                }
                engine.setClickRouting (clickWasOn, clickWasPair);
                if (instruments != nullptr)
                    for (auto& lane : instruments->lanes)
                        if (lane != nullptr && lane->plugin != nullptr)
                            lane->plugin->setNonRealtime (false);
            }
        } restore { engine, routed, instruments, engine.isClickOn(), engine.getClickOutputPair() };
        for (auto& r : routed)
        {
            r.control->outputPair = r.pair;
            r.control->solo = false;   // el solo es para escuchar en el momento: la mezcla lleva todo lo que no está muteado
        }
        // El click suena si se pidió en la exportación, aunque esté apagado para tocar (con su tempo, rejilla y volumen)
        engine.setClickRouting (options.click || options.clickFile, clickPair);
        if (instruments != nullptr)
            for (auto& lane : instruments->lanes)
                if (lane != nullptr && lane->plugin != nullptr)
                    lane->plugin->setNonRealtime (true);

        result.mix.setSize (2, (int) total);
        result.mix.clear();
        if (options.clickFile)
        {
            result.click.setSize (2, (int) total);
            result.click.clear();
        }
        if (options.stems)
            for (auto& r : routed)
            {
                Source s;
                s.name = r.name;
                s.audio.setSize (2, (int) total);
                s.audio.clear();
                result.stems.push_back (std::move (s));
            }

        engine.clearLoop();
        engine.pause();
        engine.seekSeconds (0.0);
        constexpr int block = 1024;
        juce::AudioBuffer<float> scratch (outputs, block);
        juce::AudioIODeviceCallbackContext context;
        // Detenido, el motor termina su fundido y recién ahí hace el salto pendiente: un par de bloques en silencio
        // antes de arrancar, así la muestra 0 del archivo es la 0 de la canción (si no, salía 256 muestras tarde)
        for (int k = 0; k < 2; ++k)
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, scratch.getArrayOfWritePointers(), outputs,
                                                     AudioEngine::fadeSamples + 16, context);
        engine.play();
        juce::int64 done = 0;
        while (done < total)
        {
            if (shouldAbort && shouldAbort())
            {
                result.aborted = true;
                break;
            }
            const int n = (int) juce::jmin ((juce::int64) block, total - done);
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, scratch.getArrayOfWritePointers(), outputs, n, context);
            const int at = (int) done;
            for (size_t k = 0; k < routed.size(); ++k)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* src = scratch.getReadPointer (2 * (int) k + ch);
                    result.mix.addFrom (ch, at, src, n);
                    if (options.stems)
                        result.stems[k].audio.copyFrom (ch, at, src, n);
                }
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = scratch.getReadPointer (2 * clickPair + ch);
                if (options.click)
                    result.mix.addFrom (ch, at, src, n);
                if (options.clickFile)
                    result.click.copyFrom (ch, at, src, n);
            }
            done += n;
            if (progress)
                progress ((float) done / (float) total);
        }
        return result;   // (Restore repone el ruteo)
    }

    double normalize (Result& result, double sampleRate, double targetLufs)
    {
        const int n = result.mix.getNumSamples();
        if (n <= 0)
            return 0.0;
        const double measured = loudness::measure (result.mix, sampleRate, {}, false).whole.lufs;
        if (! (measured > loudness::unknown + 1.0))
            return 0.0;
        // Ganancia hasta el objetivo y limitador de picos; el limitador resta algo de sonoridad: se corrige (3 pasadas)
        const int lookahead = juce::jmax (1, (int) std::lround (0.005 * sampleRate));
        const int release = juce::jmax (1, (int) std::lround (0.1 * sampleRate));
        const float ceiling = juce::Decibels::decibelsToGain (-1.5f);
        double gainDb = juce::jlimit (-24.0, 24.0, targetLufs - measured);
        std::vector<float> limiter;
        for (int pass = 0; pass < 3; ++pass)
        {
            const float g = juce::Decibels::decibelsToGain ((float) gainDb);
            const double reduction = loudness::limiterGains (result.mix, g, ceiling, lookahead, release, limiter);
            if (reduction > -0.01)
            {
                limiter.clear();
                break;
            }
            juce::AudioBuffer<float> trial (result.mix);
            for (int ch = 0; ch < trial.getNumChannels(); ++ch)
            {
                float* d = trial.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    d[i] *= g * limiter[(size_t) i];
            }
            const double after = loudness::measure (trial, sampleRate, {}, false).whole.lufs;
            const double missing = targetLufs - after;
            if (pass == 2 || std::abs (missing) < 0.15 || ! (after > loudness::unknown + 1.0))
                break;
            gainDb = juce::jlimit (-24.0, 24.0, gainDb + missing);
        }
        if (! limiter.empty())
        {
            // El limitador final es el de la ganancia final
            loudness::limiterGains (result.mix, juce::Decibels::decibelsToGain ((float) gainDb), ceiling, lookahead, release, limiter);
        }
        const float g = juce::Decibels::decibelsToGain ((float) gainDb);
        auto apply = [&] (juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                float* d = b.getWritePointer (ch);
                const int len = juce::jmin (n, b.getNumSamples());
                for (int i = 0; i < len; ++i)
                    d[i] *= g * (limiter.empty() ? 1.0f : limiter[(size_t) i]);
            }
        };
        apply (result.mix);
        for (auto& s : result.stems)
            apply (s.audio);
        return gainDb;
    }

    double preventClipping (Result& result)
    {
        float peak = result.mix.getNumSamples() > 0 ? result.mix.getMagnitude (0, result.mix.getNumSamples()) : 0.0f;
        for (auto& s : result.stems)
            if (s.audio.getNumSamples() > 0)
                peak = juce::jmax (peak, s.audio.getMagnitude (0, s.audio.getNumSamples()));
        if (! (peak > 0.999f))
            return 0.0;
        const float g = 0.999f / peak;
        result.mix.applyGain (g);
        for (auto& s : result.stems)
            s.audio.applyGain (g);
        return -juce::Decibels::gainToDecibels (g);
    }
}

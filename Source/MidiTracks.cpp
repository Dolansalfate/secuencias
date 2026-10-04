#include "MidiTracks.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Índices válidos de `mt.hits`, sin repetir y en orden creciente (los fuera de rango se ignoran)
    std::vector<int> validIndices (const MidiTrack& mt, const std::vector<int>& indices)
    {
        std::vector<int> v;
        v.reserve (indices.size());
        for (int i : indices)
            if (i >= 0 && i < (int) mt.hits.size())
                v.push_back (i);
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        return v;
    }

    // Ordena los golpes por tiempo (estable, como MidiTrack::sortHits) y devuelve dónde quedaron los de
    // `touched` (índices de antes de ordenar), en orden creciente
    std::vector<int> sortAndTrack (MidiTrack& mt, const std::vector<int>& touched)
    {
        const size_t n = mt.hits.size();
        std::vector<int> order (n);
        for (size_t i = 0; i < n; ++i)
            order[i] = (int) i;
        std::stable_sort (order.begin(), order.end(), [&mt] (int a, int b)
                          { return mt.hits[(size_t) a].seconds < mt.hits[(size_t) b].seconds; });
        std::vector<MidiHit> sorted;
        sorted.reserve (n);
        std::vector<int> newIndex (n, -1);
        for (size_t k = 0; k < n; ++k)
        {
            sorted.push_back (mt.hits[(size_t) order[k]]);
            newIndex[(size_t) order[k]] = (int) k;
        }
        mt.hits = std::move (sorted);
        std::vector<int> result;
        result.reserve (touched.size());
        for (int i : touched)
            if (i >= 0 && i < (int) n)
                result.push_back (newIndex[(size_t) i]);
        std::sort (result.begin(), result.end());
        return result;
    }

    // Un tiempo de golpe válido: finito y nunca antes de 0
    double hitTime (double seconds)
    {
        return std::isfinite (seconds) ? juce::jmax (0.0, seconds) : 0.0;
    }

    constexpr double timeEps = 1.0e-9;   // tolerancia al comparar instantes (como shiftGrid)
    // replaceRange: un golpe detectado a menos de esto de uno que se conserva es el mismo golpe (el tiempo mínimo
    // entre golpes por omisión del trigger, TriggerSettings::minMs)
    constexpr double keptHitTolerance = 0.04;

    int clampNote (int note)      { return juce::jlimit (0, 127, note); }
    int clampVelocity (int v)     { return juce::jlimit (1, 127, v); }

    bool hitsSorted (const MidiTrack& mt)
    {
        return std::is_sorted (mt.hits.begin(), mt.hits.end(),
                               [] (const MidiHit& a, const MidiHit& b) { return a.seconds < b.seconds; });
    }
}

namespace miditrack
{
    int velocityFromStrength (float strength)
    {
        const float s = std::isfinite (strength) ? juce::jlimit (0.0f, 1.0f, strength) : 0.0f;
        return clampVelocity (juce::roundToInt (127.0f * (0.25f + 0.75f * s)));
    }

    float strengthFromVelocity (int velocity)
    {
        return (float) juce::jlimit (0, 127, velocity) / 127.0f;
    }

    const std::vector<NoteGroup>& addictiveDrumsMap()
    {
        // Según el keymap de XLN Audio (AD2 Standard): cada pieza tiene varias notas (golpes y articulaciones)
        static const std::vector<NoteGroup> map {
            { "Bombo y caja", { { 36, "Bombo" }, { 38, "Caja" }, { 37, "Caja (rimshot)" }, { 43, "Caja (golpe suave)" },
                                { 41, "Caja (rimshot suave)" }, { 42, "Caja (aro, side stick)" }, { 44, "Caja (rim click)" },
                                { 40, "Caja (doble)" }, { 39, "Caja (rimshot doble)" } } },
            { "Hi-hat", { { 49, "Hi-hat cerrado (punta)" }, { 50, "Hi-hat cerrado (borde)" }, { 51, "Hi-hat cerrado 2 (punta)" },
                          { 52, "Hi-hat cerrado 2 (borde)" }, { 53, "Hi-hat cerrado (campana)" }, { 54, "Hi-hat abierto A" },
                          { 55, "Hi-hat abierto B" }, { 56, "Hi-hat abierto C" }, { 57, "Hi-hat abierto D" },
                          { 58, "Hi-hat abierto (campana)" }, { 48, "Hi-hat pedal" }, { 59, "Hi-hat splash con el pie" } } },
            { "Toms", { { 71, "Tom 1" }, { 72, "Tom 1 (rimshot)" }, { 69, "Tom 2" }, { 70, "Tom 2 (rimshot)" },
                        { 67, "Tom 3" }, { 68, "Tom 3 (rimshot)" }, { 65, "Tom 4" }, { 66, "Tom 4 (rimshot)" } } },
            { "Rides", { { 60, "Ride 1" }, { 61, "Ride 1 (campana)" }, { 62, "Ride 1 (borde)" }, { 63, "Ride 1 (apagado)" },
                         { 84, "Ride 2" }, { 85, "Ride 2 (campana)" }, { 86, "Ride 2 (borde)" }, { 87, "Ride 2 (apagado)" } } },
            { "Platillos", { { 77, "Platillo 1" }, { 78, "Platillo 1 (apagado)" }, { 79, "Platillo 2" }, { 80, "Platillo 2 (apagado)" },
                             { 81, "Platillo 3" }, { 82, "Platillo 3 (apagado)" }, { 89, "Platillo 4" }, { 90, "Platillo 4 (apagado)" },
                             { 91, "Platillo 5" }, { 92, "Platillo 5 (apagado)" }, { 93, "Platillo 6" }, { 94, "Platillo 6 (apagado)" } } },
            { "Flexi (percusión del kit)", { { 47, "Flexi 1 A" }, { 73, "Flexi 1 B" }, { 74, "Flexi 1 C" }, { 76, "Flexi 1 D" },
                                             { 96, "Flexi 2 A" }, { 97, "Flexi 2 B" }, { 98, "Flexi 2 C" }, { 99, "Flexi 2 D" },
                                             { 101, "Flexi 3 A" }, { 102, "Flexi 3 B" }, { 103, "Flexi 3 C" }, { 104, "Flexi 3 D" } } },
            { "Otros", { { 75, "Baquetas (sticks)" } } },
        };
        return map;
    }

    juce::String drumNoteName (int note)
    {
        switch (note)
        {
            case 35: case 36: return tr ("Bombo");
            case 37:          return tr ("Aro");
            case 38: case 40: return tr ("Caja");
            case 39:          return tr ("Palmas");
            case 41: case 43: return tr ("Tom de piso");
            case 45: case 47: return tr ("Tom medio");
            case 48: case 50: return tr ("Tom alto");
            case 42:          return tr ("Hi-hat cerrado");
            case 44:          return tr ("Hi-hat pedal");
            case 46:          return tr ("Hi-hat abierto");
            case 49: case 57: return tr ("Crash");
            case 51: case 59: return tr ("Ride");
            case 52:          return tr ("China");
            case 53:          return tr ("Campana del ride");
            case 54:          return tr ("Pandereta");
            case 55:          return tr ("Splash");
            case 56:          return tr ("Cencerro");
            case 60:          return tr ("Bongó alto");
            case 61:          return tr ("Bongó bajo");
            case 62:          return tr ("Conga alta (tapada)");
            case 63:          return tr ("Conga alta");
            case 64:          return tr ("Conga baja");
            case 65:          return tr ("Timbal alto");
            case 66:          return tr ("Timbal bajo");
            case 69:          return tr ("Cabasa");
            case 70:          return tr ("Maracas");
            case 73:          return tr ("Güiro corto");
            case 74:          return tr ("Güiro largo");
            case 75:          return tr ("Claves");
            case 76: case 77: return tr ("Bloque de madera");
            default:          break;
        }
        return "Nota " + juce::String (note);
    }

    MidiTrack fromDetection (const std::vector<TriggerEvent>& events, double sampleRate, const StemInfo& stem)
    {
        const juce::String base = stem.name.trim().isNotEmpty() ? stem.name : juce::String ("Pista");
        MidiTrack mt;
        mt.name = base + " (MIDI)";
        mt.sourceFile = stem.fileName;
        mt.muteSource = ! stem.trigger.keepAudio;

        // Una fila con el sonido del trigger; hereda también su canal (volumen y salida), así la pista MIDI
        // suena igual que el trigger que reemplaza
        MidiPad pad;
        pad.name = base;
        pad.note = clampNote (stem.trigger.note);
        pad.sound = stem.trigger.sound;
        pad.gainDb = stem.trigger.gainDb;
        pad.outputPair = juce::jmax (0, stem.trigger.outputPair);
        mt.pads.push_back (pad);

        if (sampleRate > 0.0)
        {
            mt.hits.reserve (events.size());
            for (auto& e : events)
                if (e.sample >= 0)
                    mt.hits.push_back ({ (double) e.sample / sampleRate, pad.note, velocityFromStrength (e.velocity) });
        }
        mt.sortHits();
        return mt;
    }

    int padIndexForNote (const MidiTrack& mt, int note)
    {
        for (size_t i = 0; i < mt.pads.size(); ++i)
            if (mt.pads[i].note == note)
                return (int) i;
        return -1;
    }

    std::vector<TriggerEvent> eventsForNote (const MidiTrack& mt, int note, double sampleRate,
                                              const std::function<double (double)>& toPlayback)
    {
        std::vector<TriggerEvent> events;
        if (! (sampleRate > 0.0))
            return events;
        for (auto& h : mt.hits)
        {
            if (h.note != note)
                continue;
            const double t = toPlayback ? toPlayback (h.seconds) : h.seconds;
            if (! std::isfinite (t))
                continue;
            const auto sample = (juce::int64) std::llround (t * sampleRate);
            if (sample < 0)
                continue;
            events.push_back ({ sample, strengthFromVelocity (h.velocity) });
        }
        std::stable_sort (events.begin(), events.end(),
                          [] (const TriggerEvent& a, const TriggerEvent& b) { return a.sample < b.sample; });
        return events;
    }

    int addHit (MidiTrack& mt, double seconds, int note, int velocity)
    {
        if (! hitsSorted (mt))
            mt.sortHits();
        const MidiHit h { hitTime (seconds), clampNote (note), clampVelocity (velocity) };
        // Después de los que caen en el mismo instante (el mismo orden que daría sortHits)
        auto it = std::upper_bound (mt.hits.begin(), mt.hits.end(), h.seconds,
                                    [] (double t, const MidiHit& x) { return t < x.seconds; });
        const int index = (int) (it - mt.hits.begin());
        mt.hits.insert (it, h);
        return index;
    }

    void removeHits (MidiTrack& mt, const std::vector<int>& indices)
    {
        const auto valid = validIndices (mt, indices);
        for (auto it = valid.rbegin(); it != valid.rend(); ++it)
            mt.hits.erase (mt.hits.begin() + *it);
        if (! hitsSorted (mt))
            mt.sortHits();
    }

    std::vector<int> moveHits (MidiTrack& mt, const std::vector<int>& indices, double deltaSeconds, int rowDelta)
    {
        const auto touched = validIndices (mt, indices);
        const double delta = std::isfinite (deltaSeconds) ? deltaSeconds : 0.0;
        const auto lastPad = (juce::int64) mt.pads.size() - 1;
        for (int i : touched)
        {
            auto& h = mt.hits[(size_t) i];
            h.seconds = hitTime (h.seconds + delta);
            if (rowDelta != 0)
            {
                const int pad = padIndexForNote (mt, h.note);
                if (pad >= 0)   // un golpe sin fila (nota que ninguna fila toca) conserva su nota
                    h.note = mt.pads[(size_t) juce::jlimit ((juce::int64) 0, lastPad, (juce::int64) pad + rowDelta)].note;
            }
        }
        return sortAndTrack (mt, touched);
    }

    std::vector<int> setNote (MidiTrack& mt, const std::vector<int>& indices, int note)
    {
        const auto touched = validIndices (mt, indices);
        for (int i : touched)
            mt.hits[(size_t) i].note = clampNote (note);
        return sortAndTrack (mt, touched);
    }

    void changeVelocity (MidiTrack& mt, const std::vector<int>& indices, int delta)
    {
        for (int i : validIndices (mt, indices))
        {
            auto& v = mt.hits[(size_t) i].velocity;
            v = (int) juce::jlimit ((juce::int64) 1, (juce::int64) 127, (juce::int64) v + delta);
        }
        if (! hitsSorted (mt))
            mt.sortHits();
    }

    void setVelocity (MidiTrack& mt, const std::vector<int>& indices, int velocity)
    {
        for (int i : validIndices (mt, indices))
            mt.hits[(size_t) i].velocity = clampVelocity (velocity);
        if (! hitsSorted (mt))
            mt.sortHits();
    }

    double snapToSubdivision (const Analysis& analysis, double seconds, int subdivisions)
    {
        const auto& beats = analysis.beats;
        if (beats.size() < 2 || ! std::isfinite (seconds))
            return seconds;
        const int sub = juce::jmax (1, subdivisions);
        const auto it = std::upper_bound (beats.begin(), beats.end(), seconds,
                                          [] (double t, const Beat& b) { return t < b.seconds; });
        const size_t after = (size_t) (it - beats.begin());   // primer tiempo posterior a `seconds`

        if (after == 0 || after >= beats.size())
        {
            // Antes del primer tiempo o desde el último: se sigue con el primer o el último intervalo
            const bool before = after == 0;
            const double anchor = before ? beats.front().seconds : beats.back().seconds;
            const double interval = before ? beats[1].seconds - beats[0].seconds
                                           : beats.back().seconds - beats[beats.size() - 2].seconds;
            const double step = interval / sub;
            if (! (step > 1.0e-9))
                return anchor;
            return anchor + std::round ((seconds - anchor) / step) * step;
        }

        // Entre dos tiempos consecutivos: el intervalo se reparte parejo
        const double start = beats[after - 1].seconds, end = beats[after].seconds;
        const double step = (end - start) / sub;
        if (! (step > 1.0e-9))
            return seconds - start < end - seconds ? start : end;
        const double k = std::round ((seconds - start) / step);
        if (k <= 0.0)
            return start;
        if (k >= sub)
            return end;
        return start + k * step;
    }

    std::vector<int> quantize (MidiTrack& mt, const std::vector<int>& indices, const Analysis& analysis, int subdivisions)
    {
        const auto touched = validIndices (mt, indices);
        for (int i : touched)
        {
            auto& h = mt.hits[(size_t) i];
            h.seconds = hitTime (snapToSubdivision (analysis, h.seconds, subdivisions));
        }
        return sortAndTrack (mt, touched);
    }

    void replaceRange (MidiTrack& mt, double from, double to, const std::vector<TriggerEvent>& events, double sampleRate, int note)
    {
        if (! (to > from))
            return;
        auto inside = [from, to] (double t) { return t >= from - timeEps && t < to - timeEps; };

        mt.hits.erase (std::remove_if (mt.hits.begin(), mt.hits.end(),
                                       [&] (const MidiHit& h) { return inside (h.seconds) && (note < 0 || h.note == note); }),
                       mt.hits.end());

        const int newNote = note >= 0 ? clampNote (note) : (mt.pads.empty() ? MidiHit().note : mt.pads.front().note);
        if (sampleRate > 0.0)
        {
            // Los golpes que quedan (de otras filas, o de esta fuera de la ventana), ordenados: un golpe detectado que cae
            // junto a uno de ellos ya está en la pista (por ejemplo, una conga que se pasó de la fila de los toms a la
            // suya) y no se agrega otra vez
            mt.sortHits();
            const size_t kept = mt.hits.size();
            auto alreadyThere = [&mt, kept] (double t)
            {
                const auto end = mt.hits.begin() + (std::ptrdiff_t) kept;
                const auto it = std::lower_bound (mt.hits.begin(), end, t - keptHitTolerance,
                                                  [] (const MidiHit& h, double v) { return h.seconds < v; });
                return it != end && it->seconds <= t + keptHitTolerance;
            };
            for (auto& e : events)
            {
                const double t = (double) e.sample / sampleRate;
                if (inside (t) && ! alreadyThere (t))
                    mt.hits.push_back ({ hitTime (t), newNote, velocityFromStrength (e.velocity) });
            }
        }
        mt.sortHits();
    }

    void removePad (MidiTrack& mt, int padIndex, int moveToNote)
    {
        if (padIndex < 0 || padIndex >= (int) mt.pads.size())
            return;
        const int oldNote = mt.pads[(size_t) padIndex].note;
        mt.pads.erase (mt.pads.begin() + padIndex);

        // Si otra fila toca la misma nota, los golpes siguen siendo suyos
        if (padIndexForNote (mt, oldNote) >= 0)
            return;
        if (moveToNote != oldNote && padIndexForNote (mt, moveToNote) >= 0)
        {
            for (auto& h : mt.hits)
                if (h.note == oldNote)
                    h.note = moveToNote;
        }
        else
        {
            mt.hits.erase (std::remove_if (mt.hits.begin(), mt.hits.end(),
                                           [oldNote] (const MidiHit& h) { return h.note == oldNote; }),
                           mt.hits.end());
        }
    }
}

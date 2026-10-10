#include "Arrangement.h"
#include <algorithm>
#include <cmath>

namespace arrangement
{
    void sortClips (std::vector<Clip>& clips)
    {
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.position < b.position; });
    }

    void ensureClips (std::vector<Clip>& clips, double sourceLength)
    {
        if (clips.empty() && sourceLength > 0.0)
            clips.push_back ({ 0.0, sourceLength, 0.0 });
    }

    bool isIdentity (const std::vector<Clip>& clips, double sourceLength)
    {
        if (clips.empty())
            return true;
        if (clips.size() != 1)
            return false;
        const auto& c = clips.front();
        return std::abs (c.position) < 1.0e-6 && std::abs (c.srcStart) < 1.0e-6 && std::abs (c.srcEnd - sourceLength) < 1.0e-6;
    }

    double lengthSeconds (const std::vector<Clip>& clips, double sourceLength)
    {
        if (clips.empty())
            return sourceLength;
        double end = 0.0;
        for (auto& c : clips)
            end = std::max (end, c.end());
        return end;
    }

    int clipAt (const std::vector<Clip>& clips, double t)
    {
        for (int i = (int) clips.size(); --i >= 0;)   // de dos solapados, el de más arriba (el último)
            if (t >= clips[(size_t) i].position - 1.0e-9 && t < clips[(size_t) i].end())
                return i;
        return -1;
    }

    bool cutAt (std::vector<Clip>& clips, double t, double sourceLength)
    {
        ensureClips (clips, sourceLength);
        const int i = clipAt (clips, t);
        if (i < 0)
            return false;
        auto& c = clips[(size_t) i];
        const double offset = t - c.position;
        if (offset < 0.001 || offset > c.length() - 0.001)
            return false;   // ya hay un corte ahí (o casi)
        Clip second { c.srcStart + offset, c.srcEnd, t };
        c.srcEnd = c.srcStart + offset;
        clips.insert (clips.begin() + i + 1, second);
        sortClips (clips);
        return true;
    }

    void moveClip (std::vector<Clip>& clips, int index, double delta, bool andFollowing)
    {
        if (! juce::isPositiveAndBelow (index, (int) clips.size()))
            return;
        // Nada puede empezar antes de 0
        double minPos = clips[(size_t) index].position;
        if (andFollowing)
            for (size_t i = (size_t) index; i < clips.size(); ++i)
                minPos = std::min (minPos, clips[i].position);
        delta = std::max (delta, -minPos);
        for (size_t i = (size_t) index; i < clips.size(); ++i)
        {
            clips[i].position += delta;
            if (! andFollowing)
                break;
        }
        sortClips (clips);
    }

    void removeClip (std::vector<Clip>& clips, int index, bool closeGap, double& removedFrom, double& removedLength)
    {
        removedFrom = removedLength = 0.0;
        if (! juce::isPositiveAndBelow (index, (int) clips.size()))
            return;
        const auto removed = clips[(size_t) index];
        clips.erase (clips.begin() + index);
        if (! closeGap)
            return;
        removedFrom = removed.position;
        removedLength = removed.length();
        for (auto& c : clips)
            if (c.position >= removed.position)
                c.position = std::max (removed.position, c.position - removed.length());
        sortClips (clips);
    }

    void insertGap (std::vector<Clip>& clips, double at, double length)
    {
        if (length <= 0.0)
            return;
        for (int i = (int) clips.size(); --i >= 0;)
        {
            auto& c = clips[(size_t) i];
            if (c.position < at - 1.0e-9 && c.end() > at + 1.0e-9)
            {
                // Atraviesa el punto: se parte y la segunda mitad se corre
                const double offset = at - c.position;
                Clip second { c.srcStart + offset, c.srcEnd, at + length };
                c.srcEnd = c.srcStart + offset;
                clips.insert (clips.begin() + i + 1, second);
            }
            else if (c.position >= at - 1.0e-9)
                c.position += length;
        }
        sortClips (clips);
    }

    int pasteClip (std::vector<Clip>& clips, const Clip& source, double at, bool insert)
    {
        if (source.length() <= 0.0)
            return -1;
        if (insert)
            insertGap (clips, at, source.length());
        Clip copy { source.srcStart, source.srcEnd, juce::jmax (0.0, at) };
        clips.push_back (copy);
        sortClips (clips);
        for (int i = 0; i < (int) clips.size(); ++i)
            if (std::abs (clips[(size_t) i].position - copy.position) < 1.0e-9 && std::abs (clips[(size_t) i].srcStart - copy.srcStart) < 1.0e-9
                && std::abs (clips[(size_t) i].srcEnd - copy.srcEnd) < 1.0e-9)
                return i;
        return -1;
    }

    bool canJoinWithPrevious (const std::vector<Clip>& clips, int index)
    {
        if (index <= 0 || index >= (int) clips.size())
            return false;
        const auto& p = clips[(size_t) index - 1];
        const auto& c = clips[(size_t) index];
        return std::abs (p.srcEnd - c.srcStart) < 1.0e-6 && std::abs (p.end() - c.position) < 1.0e-6;
    }

    bool joinWithPrevious (std::vector<Clip>& clips, int index)
    {
        if (! canJoinWithPrevious (clips, index))
            return false;
        clips[(size_t) index - 1].srcEnd = clips[(size_t) index].srcEnd;
        clips.erase (clips.begin() + index);
        return true;
    }

    void splitAllAt (std::vector<Clip>& clips, double t)
    {
        for (int i = (int) clips.size(); --i >= 0;)
        {
            auto& c = clips[(size_t) i];
            const double offset = t - c.position;
            if (offset > 0.001 && offset < c.length() - 0.001)
            {
                Clip second { c.srcStart + offset, c.srcEnd, t };
                c.srcEnd = c.srcStart + offset;
                clips.insert (clips.begin() + i + 1, second);
            }
        }
        sortClips (clips);
    }

    namespace
    {
        // Fin del audio de un arreglo propio (vacío = la pista no suena: 0)
        double ownEnd (const std::vector<Clip>& clips)
        {
            double end = 0.0;
            for (auto& c : clips)
                end = std::max (end, c.end());
            return end;
        }

        bool isRecording (const SongInfo& info, const juce::String& stemFile)
        {
            return std::any_of (info.stems.begin(), info.stems.end(),
                                [&stemFile] (const StemInfo& s) { return s.fileName == stemFile && s.songTime; });
        }

        // Cada arreglo propio que sigue las ediciones de la canción (no los de las grabaciones)
        template <typename Fn>
        void forEachOwn (SongInfo& info, Fn&& fn)
        {
            for (auto& own : info.trackClips)
                if (! isRecording (info, own.stem))
                    fn (own.clips);
        }
    }

    std::vector<Clip> clipsOf (const SongInfo& info, const StemInfo& stem, double sourceLength)
    {
        if (const auto* own = info.ownClips (stem.fileName))
            return *own;
        if (stem.songTime)
            return { Clip { 0.0, sourceLength, 0.0 } };
        auto clips = info.clips;
        ensureClips (clips, sourceLength);
        return clips;
    }

    std::vector<Clip>& ownClips (SongInfo& info, const StemInfo& stem, double sourceLength)
    {
        if (auto* own = info.ownClips (stem.fileName))
            return *own;
        TrackClips own { stem.fileName, clipsOf (info, stem, sourceLength) };
        info.trackClips.push_back (std::move (own));
        return info.trackClips.back().clips;
    }

    void dropOwnClips (SongInfo& info, const juce::String& stemFile)
    {
        info.trackClips.erase (std::remove_if (info.trackClips.begin(), info.trackClips.end(),
                                               [&stemFile] (const TrackClips& own) { return own.stem == stemFile; }),
                               info.trackClips.end());
    }

    double songLength (const SongInfo& info, double sourceLength)
    {
        double length = lengthSeconds (info.clips, sourceLength);
        for (auto& own : info.trackClips)
            if (std::any_of (info.stems.begin(), info.stems.end(), [&own] (const StemInfo& s) { return s.fileName == own.stem; }))
                length = std::max (length, ownEnd (own.clips));
        return length;
    }

    bool isPlainArrangement (const SongInfo& info, double sourceLength)
    {
        return info.trackClips.empty() && isIdentity (info.clips, sourceLength);
    }

    void cutOwnAt (SongInfo& info, double t)
    {
        forEachOwn (info, [t] (std::vector<Clip>& clips) { splitAllAt (clips, t); });
    }

    void moveOwnRange (SongInfo& info, double from, double to, double delta, bool andFollowing)
    {
        if (! std::isfinite (delta) || std::abs (delta) < 1.0e-12)
            return;
        forEachOwn (info, [=] (std::vector<Clip>& clips)
        {
            splitAllAt (clips, from);
            if (! andFollowing)
                splitAllAt (clips, to);
            for (auto& c : clips)
                if (c.position >= from - 1.0e-6 && (andFollowing || c.position < to - 1.0e-6))
                    c.position = std::max (0.0, c.position + delta);
            sortClips (clips);
        });
    }

    void removeOwnRange (SongInfo& info, double from, double to, bool closeGap)
    {
        if (! (to > from))
            return;
        forEachOwn (info, [=] (std::vector<Clip>& clips)
        {
            splitAllAt (clips, from);
            splitAllAt (clips, to);
            clips.erase (std::remove_if (clips.begin(), clips.end(),
                                         [=] (const Clip& c) { return c.position >= from - 1.0e-6 && c.end() <= to + 1.0e-6; }),
                         clips.end());
            if (closeGap)
                for (auto& c : clips)
                    if (c.position >= from - 1.0e-6)
                        c.position = std::max (from, c.position - (to - from));
            sortClips (clips);
        });
    }

    void insertOwnGap (SongInfo& info, double at, double length)
    {
        forEachOwn (info, [=] (std::vector<Clip>& clips) { insertGap (clips, at, length); });
    }

    std::vector<Clip> clipsInRange (const std::vector<Clip>& clips, double from, double to)
    {
        std::vector<Clip> out;
        for (auto& c : clips)
        {
            const double a = std::max (c.position, from), b = std::min (c.end(), to);
            if (b - a > 1.0e-6)
                out.push_back ({ c.srcStart + (a - c.position), c.srcStart + (b - c.position), a - from });
        }
        return out;
    }

    void shiftGrid (SongInfo& info, double from, double delta)
    {
        // delta < 0: lo que había en [from, from - delta) desaparece y lo demás se adelanta; delta > 0: se abre un hueco
        const double removedEnd = from - delta;
        auto removed = [&] (double t) { return delta < 0.0 && t >= from && t < removedEnd; };
        auto mapT = [&] (double t)
        {
            if (delta >= 0.0) return t >= from ? t + delta : t;
            if (t < from) return t;
            if (t < removedEnd) return from;
            return t + delta;
        };
        auto& a = info.analysis;
        for (int i = (int) a.beats.size(); --i >= 0;)
        {
            if (removed (a.beats[(size_t) i].seconds)) a.beats.erase (a.beats.begin() + i);
            else a.beats[(size_t) i].seconds = mapT (a.beats[(size_t) i].seconds);
        }
        auto mapEnd = [&] (double t)   // un final justo en `from` no se mueve al abrir un hueco
        {
            if (delta >= 0.0) return t > from ? t + delta : t;
            if (t <= from) return t;
            if (t < removedEnd) return from;
            return t + delta;
        };
        for (int i = (int) a.chords.size(); --i >= 0;)
        {
            auto& c = a.chords[(size_t) i];
            if (delta > 0.0 && c.start < from - 1.0e-9 && c.end > from + 1.0e-9)
            {
                // Atraviesa el hueco: se parte, y el hueco queda sin acorde
                Chord second { from + delta, c.end + delta, c.name };
                c.end = from;
                a.chords.insert (a.chords.begin() + i + 1, second);
                continue;
            }
            c.start = mapT (c.start);
            c.end = mapEnd (c.end);
            if (c.end - c.start < 0.01)
                a.chords.erase (a.chords.begin() + i);
        }
        for (int i = (int) info.markers.size(); --i >= 0;)
        {
            if (removed (info.markers[(size_t) i].seconds)) info.markers.erase (info.markers.begin() + i);
            else info.markers[(size_t) i].seconds = mapT (info.markers[(size_t) i].seconds);
        }
        for (int i = (int) info.notes.size(); --i >= 0;)
        {
            if (removed (info.notes[(size_t) i].seconds)) info.notes.erase (info.notes.begin() + i);
            else info.notes[(size_t) i].seconds = mapT (info.notes[(size_t) i].seconds);
        }
        // Golpes de las pistas MIDI: van con el audio al cerrar o abrir un hueco
        for (auto& mt : info.midiTracks)
        {
            for (int i = (int) mt.hits.size(); --i >= 0;)
            {
                if (removed (mt.hits[(size_t) i].seconds)) mt.hits.erase (mt.hits.begin() + i);
                else mt.hits[(size_t) i].seconds = mapT (mt.hits[(size_t) i].seconds);
            }
            mt.sortHits();
        }
        for (int i = (int) info.tempoRegions.size(); --i >= 1;)
        {
            if (removed (info.tempoRegions[(size_t) i].start)) info.tempoRegions.erase (info.tempoRegions.begin() + i);
            else info.tempoRegions[(size_t) i].start = mapT (info.tempoRegions[(size_t) i].start);
        }
        info.sortTempoRegions();
        info.clickOffset = removed (info.clickOffset) ? from : mapT (info.clickOffset);
    }

    GridSlice copyGrid (const Analysis& a, double from, double to)
    {
        GridSlice g;
        for (auto& b : a.beats)
            if (b.seconds >= from - 1.0e-9 && b.seconds < to - 1.0e-9)
                g.beats.push_back ({ b.seconds - from, b.beatInBar });
        for (auto& c : a.chords)
        {
            const double s0 = juce::jmax (c.start, from), s1 = juce::jmin (c.end, to);
            if (s1 - s0 > 0.01)
                g.chords.push_back ({ s0 - from, s1 - from, c.name });
        }
        return g;
    }

    void pasteGrid (Analysis& a, const GridSlice& g, double at)
    {
        for (auto& b : g.beats)
            a.beats.push_back ({ b.seconds + at, b.beatInBar });
        for (auto& c : g.chords)
            a.chords.push_back ({ c.start + at, c.end + at, c.name });
        std::stable_sort (a.beats.begin(), a.beats.end(), [] (const Beat& x, const Beat& y) { return x.seconds < y.seconds; });
        std::stable_sort (a.chords.begin(), a.chords.end(), [] (const Chord& x, const Chord& y) { return x.start < y.start; });
    }

    namespace
    {
        bool inRange (double t, double from, double to)
        {
            return t >= from - 1.0e-9 && t < to - 1.0e-9;
        }
    }

    void moveMidiHits (SongInfo& info, double from, double to, double delta, const juce::String& sourceFile)
    {
        if (! std::isfinite (delta) || std::abs (delta) < 1.0e-12)
            return;
        for (auto& mt : info.midiTracks)
        {
            if (sourceFile.isNotEmpty() && mt.sourceFile != sourceFile)
                continue;
            for (auto& h : mt.hits)
                if (inRange (h.seconds, from, to))
                    h.seconds = std::max (0.0, h.seconds + delta);
            mt.sortHits();
        }
    }

    void removeMidiHits (SongInfo& info, double from, double to, const juce::String& sourceFile)
    {
        for (auto& mt : info.midiTracks)
            if (sourceFile.isEmpty() || mt.sourceFile == sourceFile)
                mt.hits.erase (std::remove_if (mt.hits.begin(), mt.hits.end(),
                                               [from, to] (const MidiHit& h) { return inRange (h.seconds, from, to); }),
                               mt.hits.end());
    }

    void remapMidiHits (SongInfo& info, const juce::String& sourceFile, const std::vector<Clip>& from, const std::vector<Clip>& to)
    {
        for (auto& mt : info.midiTracks)
        {
            if (mt.sourceFile != sourceFile)
                continue;
            for (int i = (int) mt.hits.size(); --i >= 0;)
            {
                auto& h = mt.hits[(size_t) i];
                bool placed = false;
                if (const int c = clipAt (from, h.seconds); c >= 0)
                {
                    const double src = from[(size_t) c].srcStart + (h.seconds - from[(size_t) c].position);
                    for (auto& d : to)
                        if (src >= d.srcStart - 1.0e-9 && src < d.srcEnd)
                        {
                            h.seconds = std::max (0.0, d.position + (src - d.srcStart));
                            placed = true;
                            break;
                        }
                }
                if (! placed)
                    mt.hits.erase (mt.hits.begin() + i);   // ese audio no suena en el arreglo nuevo
            }
            mt.sortHits();
        }
    }

    void midiHitsToSource (SongInfo& info, const std::vector<Clip>& songClips)
    {
        for (auto& mt : info.midiTracks)
        {
            // Los tramos que suenan en su pista de origen: los suyos si tiene arreglo propio
            const auto* own = info.ownClips (mt.sourceFile);
            if (own == nullptr && songClips.empty())
                continue;   // ya es el audio original
            const auto& clips = own != nullptr ? *own : songClips;
            for (int i = (int) mt.hits.size(); --i >= 0;)
            {
                auto& h = mt.hits[(size_t) i];
                const int c = clipAt (clips, h.seconds);
                if (c < 0)
                    mt.hits.erase (mt.hits.begin() + i);   // sonaba en un hueco: en el original no hay nada ahí
                else
                    h.seconds = std::max (0.0, clips[(size_t) c].srcStart + (h.seconds - clips[(size_t) c].position));
            }
            mt.sortHits();
            // Un tramo duplicado (o pegado) trae copias de los mismos golpes: en el original caen en el mismo lugar y
            // sonarían dos veces. Se deja uno por nota a menos de 1 ms (el más fuerte)
            std::vector<MidiHit> kept;
            kept.reserve (mt.hits.size());
            for (const auto& h : mt.hits)
            {
                bool merged = false;
                for (auto k = kept.rbegin(); k != kept.rend() && h.seconds - k->seconds < 0.001; ++k)
                    if (k->note == h.note)
                    {
                        k->velocity = std::max (k->velocity, h.velocity);
                        merged = true;
                        break;
                    }
                if (! merged)
                    kept.push_back (h);
            }
            mt.hits = std::move (kept);
        }
    }

    void copyMidiHits (const SongInfo& info, double from, double to, GridSlice& slice, const juce::String& sourceFile)
    {
        slice.midi.clear();
        for (size_t m = 0; m < info.midiTracks.size(); ++m)
        {
            const auto& mt = info.midiTracks[m];
            if (sourceFile.isNotEmpty() && mt.sourceFile != sourceFile)
                continue;
            MidiSlice s;
            s.track = (int) m;
            s.name = mt.name;
            s.sourceFile = mt.sourceFile;
            for (auto& h : mt.hits)
                if (inRange (h.seconds, from, to))
                    s.hits.push_back ({ h.seconds - from, h.note, h.velocity });
            if (! s.hits.empty())
                slice.midi.push_back (std::move (s));
        }
    }

    void pasteMidiHits (SongInfo& info, const GridSlice& slice, double at)
    {
        for (auto& s : slice.midi)
        {
            auto same = [&s] (const MidiTrack& mt) { return mt.name == s.name && mt.sourceFile == s.sourceFile; };
            MidiTrack* dest = nullptr;
            if (juce::isPositiveAndBelow (s.track, (int) info.midiTracks.size()) && same (info.midiTracks[(size_t) s.track]))
                dest = &info.midiTracks[(size_t) s.track];
            else
                for (auto& mt : info.midiTracks)
                    if (same (mt))
                    {
                        dest = &mt;
                        break;
                    }
            if (dest == nullptr)
                continue;   // esa pista MIDI ya no existe (u otra canción): sus golpes no tienen dónde ir
            for (auto& h : s.hits)
                dest->hits.push_back ({ std::max (0.0, h.seconds + at), h.note, h.velocity });
            dest->sortHits();
        }
    }

    double findOnset (const juce::AudioBuffer<float>& buffer, double sr, double around, double window, double* strengthDb)
    {
        if (strengthDb != nullptr)
            *strengthDb = 0.0;
        const int total = buffer.getNumSamples();
        const int channels = buffer.getNumChannels();
        if (total <= 0 || channels <= 0 || sr <= 0.0)
            return -1.0;
        const int hop = juce::jmax (1, (int) std::llround (sr * 0.001));   // 1 ms
        const int from = juce::jlimit (0, total, (int) std::llround ((around - window) * sr));
        const int to = juce::jlimit (0, total, (int) std::llround ((around + window) * sr));
        const int blocks = (to - from) / hop;
        if (blocks < 8)
            return -1.0;

        // Envolvente en dB por bloque (suma de canales)
        std::vector<double> env ((size_t) blocks);
        for (int b = 0; b < blocks; ++b)
        {
            double acc = 0.0;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* d = buffer.getReadPointer (ch, from + b * hop);
                for (int i = 0; i < hop; ++i)
                    acc += (double) d[i] * d[i];
            }
            env[(size_t) b] = 10.0 * std::log10 (acc / (hop * channels) + 1.0e-10);   // -100 dB de piso
        }

        // Salto en 3 ms, penalizado con la distancia al punto pedido (hasta 6 dB en el borde)
        constexpr int rise = 3;
        constexpr double minJumpDb = 6.0;
        int best = -1;
        double bestScore = 0.0, bestJump = 0.0;
        const double centre = (around * sr - from) / hop;
        for (int b = rise; b < blocks; ++b)
        {
            const double jump = env[(size_t) b] - env[(size_t) b - rise];
            if (jump < minJumpDb)
                continue;
            const double score = jump - 6.0 * std::abs (b - centre) / juce::jmax (1.0, (double) blocks * 0.5);
            if (best < 0 || score > bestScore)
            {
                best = b;
                bestScore = score;
                bestJump = jump;
            }
        }
        if (best < 0)
            return -1.0;
        // Inicio del ataque: el valle previo (hasta 23 ms antes) y el primer bloque que lo supera
        // en 6 dB; se devuelve un bloque antes, para cortar justo antes del golpe
        int trough = juce::jmax (0, best - rise - 20);
        for (int i = trough; i <= best; ++i)
            if (env[(size_t) i] < env[(size_t) trough])
                trough = i;
        int start = best;
        for (int i = trough + 1; i <= best; ++i)
            if (env[(size_t) i] >= env[(size_t) trough] + 6.0)
            {
                start = i;
                break;
            }
        start = juce::jmax (0, start - 1);
        if (strengthDb != nullptr)
            *strengthDb = bestJump;
        return (from + start * hop) / sr;
    }

    std::shared_ptr<LoadedSong> render (std::shared_ptr<LoadedSong> source, const std::vector<Clip>& songClipsIn, double sr,
                                        const std::function<bool()>& shouldAbort,
                                        const std::vector<TrackClips>& trackClips, const juce::StringArray& stemFiles)
    {
        if (source == nullptr)
            return nullptr;
        const double sourceLength = (double) source->length / sr;
        // El arreglo propio de una pista, por el archivo de su stem
        auto ownOf = [&trackClips, &stemFiles] (const LoadedTrack& t) -> const std::vector<Clip>*
        {
            if (! juce::isPositiveAndBelow (t.stemIndex, stemFiles.size()))
                return nullptr;
            for (auto& own : trackClips)
                if (own.stem == stemFiles[t.stemIndex])
                    return &own.clips;
            return nullptr;
        };
        bool anyOwn = false;
        double length = lengthSeconds (songClipsIn, sourceLength);
        for (auto& t : source->tracks)
            if (const auto* own = ownOf (*t))
            {
                anyOwn = true;
                length = std::max (length, ownEnd (*own));
            }
        if (! anyOwn && isIdentity (songClipsIn, sourceLength))
            return source;
        auto clips = songClipsIn;
        ensureClips (clips, sourceLength);

        const int outLen = juce::jmax (1, (int) std::llround (length * sr));
        const int fadeN = juce::jmax (1, (int) std::llround (fadeSeconds * sr));
        auto out = std::make_shared<LoadedSong>();
        out->sampleRate = sr;
        out->length = outLen;
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
            t->songTime = src->songTime;
            t->buffer.setSize (2, outLen);
            t->buffer.clear();
            const int channels = juce::jmin (2, src->buffer.getNumChannels());
            const auto* own = ownOf (*src);
            if (src->songTime && own == nullptr)
            {
                // Grabación hecha sobre el arreglo: ya está en la línea de tiempo de la canción, se copia tal cual
                const int n = juce::jmin (outLen, src->buffer.getNumSamples());
                for (int ch = 0; ch < 2; ++ch)
                    t->buffer.copyFrom (ch, 0, src->buffer, juce::jmin (ch, channels - 1), 0, n);
                t->waveform = WaveformCache::build (t->buffer);
                out->tracks.push_back (std::move (t));
                continue;
            }
            for (auto& c : own != nullptr ? *own : clips)
            {
                const int s0 = juce::jlimit (0, src->buffer.getNumSamples(), (int) std::llround (c.srcStart * sr));
                const int s1 = juce::jlimit (0, src->buffer.getNumSamples(), (int) std::llround (c.srcEnd * sr));
                const int p = (int) std::llround (c.position * sr);
                const int n = juce::jmin (s1 - s0, outLen - p);
                if (n <= 0 || p < 0)
                    continue;
                const int fade = juce::jmin (fadeN, n / 2);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* in = src->buffer.getReadPointer (juce::jmin (ch, channels - 1), s0);
                    float* o = t->buffer.getWritePointer (ch, p);
                    for (int i = 0; i < n; ++i)
                    {
                        float g = 1.0f;
                        if (i < fade)              g = (float) (i + 1) / (float) (fade + 1);
                        else if (i >= n - fade)    g = (float) (n - i) / (float) (fade + 1);
                        o[i] += in[i] * g;
                    }
                }
            }
            t->waveform = WaveformCache::build (t->buffer);
            out->tracks.push_back (std::move (t));
        }
        return out;
    }
}

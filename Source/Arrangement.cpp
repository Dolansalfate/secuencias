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

    std::shared_ptr<LoadedSong> render (std::shared_ptr<LoadedSong> source, const std::vector<Clip>& clips, double sr,
                                        const std::function<bool()>& shouldAbort)
    {
        if (source == nullptr)
            return nullptr;
        const double sourceLength = (double) source->length / sr;
        if (isIdentity (clips, sourceLength))
            return source;

        const int outLen = juce::jmax (1, (int) std::llround (lengthSeconds (clips, sourceLength) * sr));
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
            t->buffer.setSize (2, outLen);
            t->buffer.clear();
            const int channels = juce::jmin (2, src->buffer.getNumChannels());
            for (auto& c : clips)
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

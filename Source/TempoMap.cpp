#include "TempoMap.h"
#include <algorithm>
#include <cmath>

double effectivePlayBpm (const SongInfo& info, const TempoRegion& r)
{
    if (r.playBpm > 0.0)
        return r.playBpm;
    if (info.playBpm > 0.0)
        return info.playBpm;
    return r.origBpm;
}

TimeMap TimeMap::build (const SongInfo& info, double length)
{
    TimeMap map;
    map.origLength = juce::jmax (0.0, length);

    // Secciones de tempo; sin ellas, una sola con el tempo del análisis o de la canción
    std::vector<TempoRegion> regions = info.tempoRegions;
    if (regions.empty())
        regions.push_back ({ 0.0, info.analysis.bpm > 0.0 ? info.analysis.bpm : juce::jmax (1.0, info.bpm), 0.0 });

    double playStart = 0.0;
    for (size_t i = 0; i < regions.size(); ++i)
    {
        TempoSegment s;
        s.origStart = i == 0 ? 0.0 : juce::jlimit (0.0, length, regions[i].start);
        s.origEnd = i + 1 < regions.size() ? juce::jlimit (0.0, length, regions[i + 1].start) : length;
        if (i > 0 && s.origStart >= length)
            break;      // sección más allá del audio: la anterior ya llega al final
        if (s.origEnd <= s.origStart + 1.0e-6 && i + 1 < regions.size())
            continue;   // sección vacía (inicio repetido)
        s.playStart = playStart;
        s.origBpm = juce::jmax (1.0, regions[i].origBpm);
        s.playBpm = juce::jmax (1.0, effectivePlayBpm (info, regions[i]));
        s.ratio = s.origBpm / s.playBpm;
        playStart = s.playEnd();
        map.segs.push_back (s);
    }
    return map;
}

double TimeMap::toPlayback (double t) const
{
    if (segs.empty())
        return t;
    for (auto& s : segs)
        if (t < s.origEnd || &s == &segs.back())
            return s.playStart + (t - s.origStart) * s.ratio;
    return t;
}

double TimeMap::toOriginal (double t) const
{
    if (segs.empty())
        return t;
    for (auto& s : segs)
        if (t < s.playEnd() || &s == &segs.back())
            return s.origStart + (t - s.playStart) / s.ratio;
    return t;
}

double TimeMap::playbackLength() const
{
    return segs.empty() ? origLength : segs.back().playEnd();
}

bool TimeMap::isIdentity() const
{
    for (auto& s : segs)
        if (std::abs (s.ratio - 1.0) > 1.0e-6)
            return false;
    return true;
}

//==============================================================================
namespace
{
    double medianOf (const std::vector<double>& v, int from, int to)
    {
        std::vector<double> part (v.begin() + from, v.begin() + to);
        if (part.empty())
            return 0.0;
        std::nth_element (part.begin(), part.begin() + (long) part.size() / 2, part.end());
        return part[part.size() / 2];
    }

    double roundBpm (double bpm) { return std::round (bpm * 10.0) / 10.0; }

    // Tempo de un tramo: promedio de los intervalos que no se alejan más de un 25 % de la mediana
    // (un tiempo perdido o doble no lo mueve; la mediana sola se desvía con tiempos en pasos de 10 ms).
    double regionBpm (const std::vector<double>& localBpm, int from, int to)
    {
        const double median = medianOf (localBpm, from, to);
        if (median <= 0.0)
            return 0.0;
        double sum = 0.0;
        int count = 0;
        for (int i = from; i < to; ++i)
            if (std::abs (localBpm[(size_t) i] - median) <= median * 0.25)
            {
                sum += 60.0 / localBpm[(size_t) i];
                ++count;
            }
        return count > 0 ? 60.0 / (sum / count) : median;
    }

    // Instante del primer tiempo de compás más cercano al tiempo `index` (hasta un compás a cada lado)
    double snapToDownbeat (const std::vector<Beat>& beats, int index, int meter)
    {
        int best = -1;
        for (int k = 0; k <= meter; ++k)
            for (int i : { index - k, index + k })
                if (best < 0 && i >= 0 && i < (int) beats.size() && beats[(size_t) i].beatInBar == 1)
                    best = i;
        return beats[(size_t) (best >= 0 ? best : index)].seconds;
    }
}

double detectedBpmBetween (const Analysis& a, double from, double to)
{
    std::vector<double> local;
    for (size_t i = 1; i < a.beats.size(); ++i)
        if (a.beats[i].seconds > from - 1.0e-6 && a.beats[i].seconds <= to + 1.0e-6 && a.beats[i - 1].seconds >= from - 1.0e-6
            && a.beats[i].seconds > a.beats[i - 1].seconds + 0.05)
            local.push_back (60.0 / (a.beats[i].seconds - a.beats[i - 1].seconds));
    if (local.empty())
        return 0.0;
    return roundBpm (regionBpm (local, 0, (int) local.size()));
}

std::vector<TempoRegion> detectTempoRegions (const Analysis& a, double fallbackBpm)
{
    std::vector<TempoRegion> out;
    const auto& beats = a.beats;
    const double baseBpm = a.bpm > 0.0 ? a.bpm : juce::jmax (1.0, fallbackBpm);

    // Tempo local entre cada par de tiempos consecutivos; beatIndex[i] es el tiempo donde empieza el intervalo
    std::vector<double> local;
    std::vector<int> beatIndex;
    for (size_t i = 1; i < beats.size(); ++i)
        if (beats[i].seconds > beats[i - 1].seconds + 0.05)
        {
            local.push_back (60.0 / (beats[i].seconds - beats[i - 1].seconds));
            beatIndex.push_back ((int) i - 1);
        }
    const int n = (int) local.size();
    constexpr int window = 8;           // tiempos a cada lado (dos compases de 4/4)
    constexpr double threshold = 0.04;  // cambio relativo sostenido que abre una sección
    if (n < 2 * window)
    {
        out.push_back ({ 0.0, roundBpm (n > 0 ? regionBpm (local, 0, n) : baseBpm), 0.0 });
        return out;
    }

    // Diferencia relativa entre la mediana de los tiempos anteriores y la de los siguientes
    std::vector<double> diff ((size_t) n, 0.0);
    for (int i = window; i + window <= n; ++i)
    {
        const double before = medianOf (local, i - window, i);
        const double after = medianOf (local, i, i + window);
        diff[(size_t) i] = before > 0.0 ? std::abs (after - before) / before : 0.0;
    }

    // Puntos de cambio: en cada racha sobre el umbral, el intervalo donde mejor se separa el
    // tempo de antes (mediana al inicio de la racha) del de después (mediana al final)
    std::vector<int> changes;
    for (int i = window; i + window <= n;)
    {
        if (diff[(size_t) i] <= threshold)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j + window <= n && diff[(size_t) j] > threshold)
            ++j;
        const double oldBpm = medianOf (local, i - window, i);
        const double newBpm = medianOf (local, j - 1, j - 1 + window);
        int best = i;
        double bestCost = 1.0e300;
        for (int c = i; c < j; ++c)
        {
            double cost = 0.0;
            for (int k = i - window; k < c; ++k)
                cost += std::abs (local[(size_t) k] - oldBpm);
            for (int k = c; k < j - 1 + window; ++k)
                cost += std::abs (local[(size_t) k] - newBpm);
            if (cost < bestCost)
            {
                bestCost = cost;
                best = c;
            }
        }
        if (changes.empty() || best - changes.back() >= window)
            changes.push_back (best);
        i = j;
    }

    // Secciones: mediana del tempo local de cada una; las vecinas casi iguales se unen
    std::vector<int> starts { 0 };
    starts.insert (starts.end(), changes.begin(), changes.end());
    starts.push_back (n);
    for (size_t k = 0; k + 1 < starts.size(); ++k)
    {
        const double bpm = roundBpm (regionBpm (local, starts[k], starts[k + 1]));
        if (! out.empty() && std::abs (bpm - out.back().origBpm) / out.back().origBpm < 0.02)
            continue;
        TempoRegion r;
        r.start = k == 0 ? 0.0 : snapToDownbeat (beats, beatIndex[(size_t) starts[k]], juce::jlimit (2, 7, a.meter));
        r.origBpm = bpm;
        out.push_back (r);
    }
    return out;
}

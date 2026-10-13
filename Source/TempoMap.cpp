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

int effectiveTranspose (const SongInfo& info, const TempoRegion& r)
{
    return r.transpose != TempoRegion::followSong ? juce::jlimit (-12, 12, r.transpose) : info.transpose;
}

double tempoCurveShape (int curve, double u)
{
    u = juce::jlimit (0.0, 1.0, u);
    switch (curve)
    {
        case TempoRegion::linear:    return u;
        case TempoRegion::slowStart: return u * u;                          // cambia poco al principio y mucho al final
        case TempoRegion::fastStart: return 1.0 - (1.0 - u) * (1.0 - u);   // cambia mucho al principio y se asienta
        case TempoRegion::sCurve:    return u * u * (3.0 - 2.0 * u);        // suave en los dos extremos
        default:                     return 0.0;                           // salto: el tempo de la sección hasta el final
    }
}

juce::String tempoCurveName (int curve)
{
    switch (curve)
    {
        case TempoRegion::linear:    return juce::String::fromUTF8 ("Rampa lineal");
        case TempoRegion::slowStart: return juce::String::fromUTF8 ("Curva lenta al inicio");
        case TempoRegion::fastStart: return juce::String::fromUTF8 ("Curva rápida al inicio");
        case TempoRegion::sCurve:    return juce::String::fromUTF8 ("Curva en S");
        default:                     return juce::String::fromUTF8 ("Salto");
    }
}

double TempoSegment::fractionAt (double t) const
{
    return origEnd > origStart + 1.0e-9 ? juce::jlimit (0.0, 1.0, (t - origStart) / (origEnd - origStart)) : 0.0;
}

double TempoSegment::origBpmAt (double t) const
{
    return origBpm + (origBpmEnd - origBpm) * tempoCurveShape (gridCurve, fractionAt (t));
}

double TempoSegment::playBpmAt (double t) const
{
    if (followsOriginal)
        return origBpmAt (t) * playBpm / origBpm;
    return playBpm + (playBpmEnd - playBpm) * tempoCurveShape (curve, fractionAt (t));
}

double TempoSegment::toPlayback (double t) const
{
    if (! ramps())
        return playStart + (t - origStart) * ratio;
    // Fuera del tramo (el último sigue más allá del audio): con la razón del borde
    if (t <= origStart)
        return playStart + (t - origStart) * ratio;
    if (t >= origEnd)
        return playEnd() + (t - origEnd) * (origBpmEnd / playBpmEnd);
    const double x = (t - origStart) / (origEnd - origStart) * lutSteps;
    const int i = juce::jlimit (0, lutSteps - 1, (int) x);
    return playStart + lut[(size_t) i] + (lut[(size_t) i + 1] - lut[(size_t) i]) * (x - i);
}

double TempoSegment::toOriginal (double p) const
{
    if (! ramps())
        return origStart + (p - playStart) / ratio;
    const double rel = p - playStart;
    if (rel <= 0.0)
        return origStart + rel / ratio;
    if (rel >= playLength)
        return origEnd + (rel - playLength) / (origBpmEnd / playBpmEnd);
    const auto it = std::upper_bound (lut.begin(), lut.end(), rel);
    const int i = juce::jlimit (0, lutSteps - 1, (int) (it - lut.begin()) - 1);
    const double a = lut[(size_t) i], b = lut[(size_t) i + 1];
    const double frac = b > a ? (rel - a) / (b - a) : 0.0;
    return origStart + (i + frac) / lutSteps * (origEnd - origStart);
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
        s.transpose = effectiveTranspose (info, regions[i]);
        s.origBpmEnd = s.origBpm;
        s.playBpmEnd = s.playBpm;
        s.playLength = (s.origEnd - s.origStart) * s.ratio;
        // Transiciones hacia la siguiente sección: los tempos siguen sus curvas y el tiempo de reproducción se integra
        // (dt de reproducción = dt original · tempo original / tempo de reproducción)
        const bool hasNext = i + 1 < regions.size() && s.origEnd > s.origStart + 1.0e-3;
        auto follows = [&info] (const TempoRegion& r) { return r.playBpm <= 0.0 && info.playBpm <= 0.0; };
        if (hasNext)
        {
            s.gridCurve = regions[i].gridCurve;
            if (s.gridCurve != TempoRegion::jump)
                s.origBpmEnd = juce::jmax (1.0, regions[i + 1].origBpm);
            s.followsOriginal = follows (regions[i]) && follows (regions[i + 1]);
            if (s.followsOriginal)
            {
                s.curve = s.gridCurve;   // suena a su tempo: el de reproducción es el de la grabación
                s.playBpmEnd = s.origBpmEnd;
            }
            else if (regions[i].curve != TempoRegion::jump)
            {
                s.curve = regions[i].curve;
                s.playBpmEnd = juce::jmax (1.0, effectivePlayBpm (info, regions[i + 1]));
            }
            const bool varies = std::abs (s.origBpmEnd - s.origBpm) > 1.0e-6 || std::abs (s.playBpmEnd - s.playBpm) > 1.0e-6;
            if (varies && ! s.followsOriginal)
            {
                constexpr int n = TempoSegment::lutSteps;
                const double span = s.origEnd - s.origStart;
                auto rate = [&s] (double u)
                {
                    const double o = s.origBpm + (s.origBpmEnd - s.origBpm) * tempoCurveShape (s.gridCurve, u);
                    const double p = s.playBpm + (s.playBpmEnd - s.playBpm) * tempoCurveShape (s.curve, u);
                    return o / p;
                };
                s.lut.resize ((size_t) n + 1);
                s.lut[0] = 0.0;
                for (int k = 0; k < n; ++k)
                {
                    // Simpson en cada paso
                    const double u0 = (double) k / n, u1 = (double) (k + 1) / n;
                    s.lut[(size_t) k + 1] = s.lut[(size_t) k] + span / n * (rate (u0) + 4.0 * rate (0.5 * (u0 + u1)) + rate (u1)) / 6.0;
                }
                s.playLength = s.lut.back();
            }
        }
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
            return s.toPlayback (t);
    return t;
}

double TimeMap::toOriginal (double t) const
{
    if (segs.empty())
        return t;
    for (auto& s : segs)
        if (t < s.playEnd() || &s == &segs.back())
            return s.toOriginal (t);
    return t;
}

double TimeMap::playbackLength() const
{
    return segs.empty() ? origLength : segs.back().playEnd();
}

bool TimeMap::isIdentity() const
{
    for (auto& s : segs)
        if (std::abs (s.ratio - 1.0) > 1.0e-6 || s.ramps())   // una razón que cambia dentro del tramo estira
            return false;
    return true;
}

bool TimeMap::hasPitchShift() const
{
    for (auto& s : segs)
        if (s.transpose != 0)
            return true;
    return false;
}

const TempoSegment* TimeMap::segmentAtPlayback (double t) const
{
    if (segs.empty())
        return nullptr;
    for (auto& s : segs)
        if (t < s.playEnd())
            return &s;
    return &segs.back();
}

const TempoSegment* TimeMap::segmentAtOriginal (double t) const
{
    if (segs.empty())
        return nullptr;
    for (auto& s : segs)
        if (t < s.origEnd)
            return &s;
    return &segs.back();
}

double TimeMap::playBpmAt (double t) const
{
    const auto* s = segmentAtOriginal (t);
    return s != nullptr ? s->playBpmAt (t) : 120.0;
}

double TimeMap::origBpmAt (double t) const
{
    const auto* s = segmentAtOriginal (t);
    return s != nullptr ? s->origBpmAt (t) : 120.0;
}

void rampGrid (Analysis& a, double from, double to, double bpmFrom, double bpmTo, int curve, int meter, int unit)
{
    if (! (to > from) || bpmFrom <= 0.0 || bpmTo <= 0.0)
        return;
    // Tiempos acumulados a lo largo del tramo (integral del tempo), redondeados a un número entero que termine en `to`
    constexpr int n = 2048;
    std::vector<double> beatsAt ((size_t) n + 1, 0.0);
    auto bpm = [&] (double u) { return bpmFrom + (bpmTo - bpmFrom) * tempoCurveShape (curve, u); };
    for (int k = 0; k < n; ++k)
    {
        const double u0 = (double) k / n, u1 = (double) (k + 1) / n;
        beatsAt[(size_t) k + 1] = beatsAt[(size_t) k] + (to - from) / n * (bpm (u0) + 4.0 * bpm (0.5 * (u0 + u1)) + bpm (u1)) / 6.0 / 60.0;
    }
    const int count = juce::jmax (1, (int) std::lround (beatsAt.back()));
    const double scale = beatsAt.back() / count;   // la curva se estira apenas para que los tiempos calcen
    std::vector<Beat> grid;
    for (int b = 0; b < count; ++b)
    {
        const double target = b * scale;
        const auto it = std::lower_bound (beatsAt.begin(), beatsAt.end(), target);
        const int i = juce::jlimit (1, n, (int) (it - beatsAt.begin()));
        const double x0 = beatsAt[(size_t) i - 1], x1 = beatsAt[(size_t) i];
        const double u = (i - 1 + (x1 > x0 ? (target - x0) / (x1 - x0) : 0.0)) / n;
        grid.push_back ({ from + u * (to - from), 1, unit });
    }
    const int first = a.firstBeatAtOrAfter (from);
    const int last = a.firstBeatAtOrAfter (to);
    a.beats.erase (a.beats.begin() + first, a.beats.begin() + last);
    a.beats.insert (a.beats.begin() + first, grid.begin(), grid.end());
    a.renumberBeats (first, meter, first + (int) grid.size());
    for (int i = first; i < first + (int) grid.size(); ++i)
        a.beats[(size_t) i].unit = unit;
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
        r.start = k == 0 ? 0.0 : snapToDownbeat (beats, beatIndex[(size_t) starts[k]], juce::jlimit (1, maxBeatsPerBar, a.meter));
        r.origBpm = bpm;
        out.push_back (r);
    }
    return out;
}

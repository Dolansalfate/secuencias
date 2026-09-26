#pragma once
#include "Library.h"
#include <vector>

// Un tramo de la canción con su tempo original y el de reproducción. La relación
// ratio = origBpm / playBpm es cuánto se estira el tiempo (2 = el doble de largo).
struct TempoSegment
{
    double origStart = 0.0, origEnd = 0.0;   // en segundos del audio original
    double playStart = 0.0;                  // dónde empieza en el tiempo de reproducción
    double origBpm = 120.0, playBpm = 120.0;
    double ratio = 1.0;
    double playEnd() const { return playStart + (origEnd - origStart) * ratio; }
};

// Mapa entre el tiempo del audio original y el tiempo de reproducción cuando hay cambios de
// tempo. Se construye a partir de las secciones de tempo de la canción (`tempoRegions`: BPM
// original de cada una y BPM de reproducción pedido) o, si no hay, de un solo tramo con el
// tempo del análisis o el BPM de la canción.
class TimeMap
{
public:
    static TimeMap build (const SongInfo&, double originalLengthSeconds);

    double toPlayback (double originalSeconds) const;
    double toOriginal (double playbackSeconds) const;
    double playbackLength() const;
    bool isIdentity() const;                  // ningún tramo cambia de tempo
    const std::vector<TempoSegment>& segments() const { return segs; }

private:
    std::vector<TempoSegment> segs;
    double origLength = 0.0;
};

// Tempo de reproducción efectivo de una sección: el suyo, si no el de la canción, si no el original.
double effectivePlayBpm (const SongInfo&, const TempoRegion&);

// Tempo de los tiempos detectados entre dos instantes: promedio de los intervalos a menos del
// 25 % de la mediana, redondeado a 0,1 BPM; 0 si no hay al menos dos tiempos.
double detectedBpmBetween (const Analysis&, double fromSeconds, double toSeconds);

// Detecta las secciones con tempo distinto a partir de los tiempos del análisis: compara la
// mediana del tempo de los 8 tiempos anteriores con la de los 8 siguientes y abre una sección
// donde la diferencia supera el 4 % de forma sostenida (el inicio se ajusta al primer tiempo de
// compás más cercano). Siempre devuelve al menos una sección que empieza en 0. Sin tiempos
// suficientes, una sola con el tempo del análisis o `fallbackBpm`.
std::vector<TempoRegion> detectTempoRegions (const Analysis&, double fallbackBpm);

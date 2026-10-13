#pragma once
#include "Library.h"
#include <vector>

// Un tramo de la canción con su tempo original y el de reproducción. La relación
// ratio = origBpm / playBpm es cuánto se estira el tiempo (2 = el doble de largo).
struct TempoSegment
{
    double origStart = 0.0, origEnd = 0.0;   // en segundos del audio original
    double playStart = 0.0;                  // dónde empieza en el tiempo de reproducción
    double origBpm = 120.0, playBpm = 120.0; // al inicio del tramo
    double ratio = 1.0;                      // al inicio (origBpm / playBpm)
    int transpose = 0;                       // semitonos efectivos del tramo (los de la sección, si no los de la canción)
    // Transiciones (TempoRegion::Curve) hacia el tramo siguiente: la del tempo de la grabación (gridCurve, hasta
    // origBpmEnd) y la del tempo de reproducción (curve, hasta playBpmEnd; si el tramo y el siguiente suenan a su tempo
    // original, la de reproducción es la de la grabación: nada se estira). lut guarda el tiempo de reproducción
    // acumulado en lutSteps partes cuando la razón cambia dentro del tramo.
    int curve = TempoRegion::jump, gridCurve = TempoRegion::jump;
    bool followsOriginal = false;
    double origBpmEnd = 120.0, playBpmEnd = 120.0;
    double playLength = 0.0;
    std::vector<double> lut;
    static constexpr int lutSteps = 512;
    bool ramps() const { return ! lut.empty(); }
    double playEnd() const { return playStart + playLength; }
    double fractionAt (double originalSeconds) const;                 // 0..1 dentro del tramo
    double origBpmAt (double originalSeconds) const;
    double playBpmAt (double originalSeconds) const;
    double toPlayback (double originalSeconds) const;
    double toOriginal (double playbackSeconds) const;
};

// Forma de la transición: cuánto del cambio de tempo va hecho en la fracción u (0..1) del tramo
double tempoCurveShape (int curve, double u);
juce::String tempoCurveName (int curve);

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
    bool hasPitchShift() const;               // algún tramo transpone
    bool isPlain() const { return isIdentity() && ! hasPitchShift(); }   // nada que renderizar
    const TempoSegment* segmentAtPlayback (double playbackSeconds) const;   // nullptr si no hay tramos
    const TempoSegment* segmentAtOriginal (double originalSeconds) const;
    // Tempo de reproducción y original en un instante del audio original (las rampas cambian dentro del tramo)
    double playBpmAt (double originalSeconds) const;
    double origBpmAt (double originalSeconds) const;
    const std::vector<TempoSegment>& segments() const { return segs; }

private:
    std::vector<TempoSegment> segs;
    double origLength = 0.0;
};

// Tempo de reproducción efectivo de una sección: el suyo, si no el de la canción, si no el original.
double effectivePlayBpm (const SongInfo&, const TempoRegion&);
// Semitonos efectivos de una sección: los suyos, si no los de la canción.
int effectiveTranspose (const SongInfo&, const TempoRegion&);

// Tempo de los tiempos detectados entre dos instantes: promedio de los intervalos a menos del
// 25 % de la mediana, redondeado a 0,1 BPM; 0 si no hay al menos dos tiempos.
double detectedBpmBetween (const Analysis&, double fromSeconds, double toSeconds);

// Detecta las secciones con tempo distinto a partir de los tiempos del análisis: compara la
// mediana del tempo de los 8 tiempos anteriores con la de los 8 siguientes y abre una sección
// donde la diferencia supera el 4 % de forma sostenida (el inicio se ajusta al primer tiempo de
// compás más cercano). Siempre devuelve al menos una sección que empieza en 0. Sin tiempos
// suficientes, una sola con el tempo del análisis o `fallbackBpm`.
std::vector<TempoRegion> detectTempoRegions (const Analysis&, double fallbackBpm);

// "Solo el click" con una transición: reemplaza los tiempos de [from, to) por los de un tempo que va de bpmFrom a bpmTo
// con esa curva (numerados con `meter`); la cantidad de tiempos se redondea y la curva se ajusta para que el último
// termine justo en `to`
void rampGrid (Analysis&, double fromSeconds, double toSeconds, double bpmFrom, double bpmTo, int curve, int meter, int unit);

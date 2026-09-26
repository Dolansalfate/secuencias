#pragma once
#include "AudioEngine.h"
#include "Library.h"
#include <functional>

// Arreglo (fase 4): la canción es una lista de tramos (`Clip`) del audio original colocados en
// la línea de tiempo de la canción. Cortar, mover, eliminar o unir tramos afecta a todos los
// stems a la vez, así nunca se desalinean. La grilla (tiempos, acordes, secciones de tempo,
// marcadores) vive en la línea de tiempo de la canción y no se mueve con el audio, salvo al
// cerrar un hueco. El resultado se renderiza a RAM (`render`) y de ahí sale el estirado.
namespace arrangement
{
    constexpr double fadeSeconds = 0.005;   // fundido en los bordes de cada tramo

    void ensureClips (std::vector<Clip>&, double sourceLength);         // vacío -> un tramo con todo el audio
    bool isIdentity (const std::vector<Clip>&, double sourceLength);    // un solo tramo con todo el audio en 0
    double lengthSeconds (const std::vector<Clip>&, double sourceLength);
    int clipAt (const std::vector<Clip>&, double seconds);              // tramo que suena en ese instante, o -1 (hueco)
    bool cutAt (std::vector<Clip>&, double seconds, double sourceLength);
    void moveClip (std::vector<Clip>&, int index, double deltaSeconds, bool andFollowing);
    // Elimina un tramo; con closeGap los siguientes se adelantan lo que duraba (removedFrom y
    // removedLength lo informan para desplazar también la grilla)
    void removeClip (std::vector<Clip>&, int index, bool closeGap, double& removedFrom, double& removedLength);
    bool canJoinWithPrevious (const std::vector<Clip>&, int index);     // continúa en la fuente y en la posición
    bool joinWithPrevious (std::vector<Clip>&, int index);
    void sortClips (std::vector<Clip>&);

    // Desplaza la grilla (tiempos, acordes, marcadores, secciones de tempo, inicio del click) al
    // cerrar un hueco: con delta < 0 lo que había en [from, from - delta) desaparece y lo que
    // sigue se adelanta; con delta > 0 se abre un hueco. Los tramos de audio no se tocan.
    void shiftGrid (SongInfo&, double fromSeconds, double deltaSeconds);

    // Transiente (ataque) más cercana a `aroundSeconds`, buscada en ±`windowSeconds` sobre la
    // envolvente del buffer (bloques de 1 ms): el mayor salto de nivel en 3 ms, con preferencia
    // por los cercanos; devuelve el instante justo antes de que el ataque supere en 6 dB el
    // valle previo (para cortar antes del golpe), o -1 si no hay un salto de al menos 6 dB.
    // `strengthDb` informa el salto.
    double findOnset (const juce::AudioBuffer<float>&, double sampleRate, double aroundSeconds, double windowSeconds,
                      double* strengthDb = nullptr);

    // Audio del arreglo: mismo puntero si es identidad; nullptr si se abortó. Los tramos que se
    // solapan se suman (con los fundidos de 5 ms es un crossfade); los huecos quedan en silencio.
    std::shared_ptr<LoadedSong> render (std::shared_ptr<LoadedSong> source, const std::vector<Clip>&, double sampleRate,
                                        const std::function<bool()>& shouldAbort = {});
}

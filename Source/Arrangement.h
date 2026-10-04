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
    // Abre un hueco de `length` s en `at`: lo que empieza en o después de `at` se corre; un tramo que
    // atraviesa `at` se parte ahí. Para pegar insertando.
    void insertGap (std::vector<Clip>&, double at, double length);
    // Pega una copia de `source` (su rango del audio original) en `at`; con `insert` abre antes el
    // hueco, si no se superpone. Devuelve el índice del tramo nuevo.
    int pasteClip (std::vector<Clip>&, const Clip& source, double at, bool insert);
    bool canJoinWithPrevious (const std::vector<Clip>&, int index);     // continúa en la fuente y en la posición
    bool joinWithPrevious (std::vector<Clip>&, int index);
    void sortClips (std::vector<Clip>&);

    // Desplaza la grilla (tiempos, acordes, marcadores, secciones de tempo, inicio del click) al
    // cerrar un hueco: con delta < 0 lo que había en [from, from - delta) desaparece y lo que
    // sigue se adelanta; con delta > 0 se abre un hueco (un acorde que lo atraviesa se parte en
    // dos, y el hueco queda sin acorde). Los tramos de audio no se tocan.
    void shiftGrid (SongInfo&, double fromSeconds, double deltaSeconds);

    // Golpes de las pistas MIDI de un rango, relativos a su inicio (uno por pista MIDI)
    struct MidiSlice
    {
        int track = -1;                   // índice en SongInfo::midiTracks al copiar
        juce::String name, sourceFile;    // se pegan en la pista con el mismo nombre y origen (la del mismo índice si coincide)
        std::vector<MidiHit> hits;
    };

    // Grilla de un rango de la canción, relativa a su inicio: los tiempos dentro y los acordes
    // recortados al rango. Es lo que viaja con un tramo copiado (con los golpes MIDI, copyMidiHits).
    struct GridSlice
    {
        std::vector<Beat> beats;
        std::vector<Chord> chords;
        std::vector<MidiSlice> midi;
    };
    GridSlice copyGrid (const Analysis&, double fromSeconds, double toSeconds);
    // Inserta una grilla copiada en `at` (tras abrir el hueco con shiftGrid): tiempos y acordes se
    // suman desplazados y quedan ordenados
    void pasteGrid (Analysis&, const GridSlice&, double at);

    // Golpes de las pistas MIDI: son golpes de un stem, así que van con el audio de los tramos (la grilla no; al
    // cerrar o abrir un hueco los mueve shiftGrid). Todos dejan los golpes ordenados.
    // - Mover un tramo: los de [from, to) se corren delta, nunca antes de 0 (to = infinito para "este y los
    //   siguientes"; delta = lo que el tramo se movió de verdad, que moveClip acota).
    void moveMidiHits (SongInfo&, double fromSeconds, double toSeconds, double deltaSeconds);
    // - Eliminar un tramo dejando silencio: los de [from, to) se borran.
    void removeMidiHits (SongInfo&, double fromSeconds, double toSeconds);
    // - Volver al audio original (antes de vaciar los tramos): cada golpe pasa al instante del audio original que
    //   sonaba bajo él (el tramo de arriba, como clipAt); los que caían en un hueco se borran, y las copias que un
    //   tramo duplicado deja en el mismo instante (misma nota a menos de 1 ms) quedan en una, la más fuerte. Con
    //   tramos pegados encima, los golpes del tramo de abajo en el solape siguen al de arriba (no se sabe de cuál venían).
    void midiHitsToSource (SongInfo&, const std::vector<Clip>&);
    // - Copiar y pegar un tramo: los golpes de [from, to) van en slice.midi (se reemplazan), y pasteMidiHits los suma
    //   desplazados a `at` (tras abrir el hueco con shiftGrid al pegar insertando; encima de los que hay al pegar encima).
    void copyMidiHits (const SongInfo&, double fromSeconds, double toSeconds, GridSlice&);
    void pasteMidiHits (SongInfo&, const GridSlice&, double at);

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

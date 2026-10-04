#pragma once
#include "Library.h"
#include "Triggers.h"
#include <functional>
#include <vector>

// Pistas MIDI: los golpes que detecta el trigger de un stem, congelados como notas editables, para
// corregir a mano lo que separó la IA (por ejemplo, en la pista de toms, pasar las congas a su propia
// fila con su propio sonido). El modelo (MidiTrack, MidiPad, MidiHit) está en Library.h. Todo va en la
// línea de tiempo de la canción (la del arreglo, como los tiempos del análisis y los marcadores); la
// interfaz y el motor la convierten al tiempo de reproducción con el TimeMap.
namespace miditrack
{
    // Velocidad MIDI de un golpe detectado (fuerza 0..1 del trigger), igual que la que manda el trigger en
    // vivo a un instrumento: round (127 · (0,25 + 0,75 · fuerza)), acotada a 1..127
    int velocityFromStrength (float strength);
    // Lo que recibe el motor: velocidad / 127
    float strengthFromVelocity (int velocity);
    constexpr int defaultVelocity = 100;   // la de un golpe agregado a mano

    // Nombre en español de una nota de batería General MIDI ("Bombo", "Caja", "Tom medio", "Hi-hat cerrado",
    // "Conga alta"...), o "Nota N" si no es de batería
    juce::String drumNoteName (int note);

    // Mapa de notas "AD2 Standard" de Addictive Drums 2 (el que trae por defecto; en su ventana MIDI Mapping también
    // hay uno General MIDI), por grupos para los menús: bombo y caja, hi-hat, toms, rides, platillos, Flexi y otros.
    // Los Flexi son espacios que cada kit llena con lo que quiera (por ejemplo congas). Nombres en UTF-8 (pasar por tr).
    struct NamedNote
    {
        int note;
        const char* name;
    };
    struct NoteGroup
    {
        const char* name;
        std::vector<NamedNote> notes;
    };
    const std::vector<NoteGroup>& addictiveDrumsMap();

    // Pista MIDI nueva con los golpes detectados en un stem (events en muestras a sampleRate, en la línea de
    // tiempo de la canción). Una fila con el sonido y la nota del trigger del stem, llamada como el stem;
    // golpes con esa nota y velocityFromStrength. name = "<stem> (MIDI)", sourceFile = stem.fileName,
    // muteSource = ! stem.trigger.keepAudio.
    MidiTrack fromDetection (const std::vector<TriggerEvent>& events, double sampleRate, const StemInfo& stem);

    int padIndexForNote (const MidiTrack&, int note);   // -1 si ninguna fila tiene esa nota

    // Golpes de una nota para el motor: muestra = round (toPlayback (segundos) · sampleRate), fuerza =
    // strengthFromVelocity; ordenados; sin los que caen antes de 0
    std::vector<TriggerEvent> eventsForNote (const MidiTrack&, int note, double sampleRate,
                                              const std::function<double (double)>& toPlayback);

    // --- Edición. Todas dejan los golpes ordenados por tiempo; las que devuelven índices dan dónde quedaron
    // los golpes tocados (para conservar la selección). Índices fuera de rango se ignoran. ---
    int addHit (MidiTrack&, double seconds, int note, int velocity);   // índice del golpe nuevo (tiempo acotado a >= 0)
    void removeHits (MidiTrack&, const std::vector<int>& indices);
    // Tiempo + deltaSeconds (nunca antes de 0) y fila + rowDelta según el orden de `pads` (acotada a las filas
    // existentes; un golpe cuya nota no tiene fila no cambia de nota)
    std::vector<int> moveHits (MidiTrack&, const std::vector<int>& indices, double deltaSeconds, int rowDelta);
    std::vector<int> setNote (MidiTrack&, const std::vector<int>& indices, int note);
    void changeVelocity (MidiTrack&, const std::vector<int>& indices, int delta);   // acotada a 1..127
    void setVelocity (MidiTrack&, const std::vector<int>& indices, int velocity);

    // Subdivisión más cercana de los tiempos detectados (1 = tiempos, 2 = corcheas, 4 = semicorcheas; entre dos
    // tiempos consecutivos se reparte parejo; antes del primero y después del último se sigue con su intervalo).
    // Sin al menos dos tiempos, `seconds`.
    double snapToSubdivision (const Analysis&, double seconds, int subdivisions);
    std::vector<int> quantize (MidiTrack&, const std::vector<int>& indices, const Analysis&, int subdivisions);

    // Reemplaza los golpes de [from, to) de la nota `note` (todas las notas si note < 0) por los detectados de
    // nuevo en esa ventana (events en muestras a sampleRate; se toman solo los que caen en [from, to)), con la
    // nota `note` (o la de la primera fila si note < 0) y velocityFromStrength. Un golpe detectado a menos de 40 ms
    // de uno que se conserva (de otra fila, o fuera de la ventana) no se agrega: ya está en la pista (por ejemplo,
    // las congas pasadas de la fila de los toms a la suya no vuelven a los toms)
    void replaceRange (MidiTrack&, double from, double to, const std::vector<TriggerEvent>& events, double sampleRate, int note);

    // Quita una fila: sus golpes pasan a la nota `moveToNote` si hay otra fila con esa nota, o se borran
    void removePad (MidiTrack&, int padIndex, int moveToNote);
}

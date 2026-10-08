#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Library.h"
#include <functional>
#include <vector>

// Armado de mixes ANTES de separar. Un mix toma canciones originales completas (fuentes), las
// analiza con madmom (tiempos y primeros tiempos de compás, además de acordes y tonalidad) y une
// tramos de ellas, cortados en los tiempos, uno tras otro. Cada tramo se estira con una razón
// constante para que dure exactamente sus tiempos al tempo del mix: así cada unión cae justo en la
// rejilla (el último tiempo del tramo que sale y el primero del que entra quedan a un tiempo de
// distancia) y, dentro del tramo, se conserva el pulso natural de la grabación. El resultado
// (mezcla.wav, 44,1 kHz) se separa con IA o se agrega tal cual, y la canción nueva ya trae los
// tiempos, compases, acordes, secciones de tempo y un marcador por tramo, sin volver a analizar.
//
// En disco: <biblioteca>/_mixes/<nombre>/mix.json, las fuentes copiadas en fuentes/ y el último
// render en mezcla.wav. Todo en segundos; los tiempos de cada fuente están en su propia línea de
// tiempo (la del archivo).

struct MixSource
{
    juce::String name;          // nombre visible (por defecto, el del archivo sin extensión)
    juce::String fileName;      // archivo dentro de <mix>/fuentes
    double length = 0.0;        // segundos
    Analysis analysis;          // tiempos en segundos de la fuente; vacío = sin analizar
};

struct MixSegment
{
    int source = 0;             // índice en MixProject::sources
    double start = 0.0;         // segundos de la fuente (normalmente un tiempo detectado, ajustado a la transiente)
    double end = 0.0;           // segundos de la fuente (el tiempo SIGUIENTE al último que se incluye)
    juce::String label;         // nombre del tramo (marcador en la canción); vacío = el nombre de la fuente
    double playBpm = 0.0;       // tempo de este tramo en el mix; 0 = el del mix
    int transpose = 0;          // semitonos (-12..12)
    float gainDb = 0.0f;        // -24..12
    double fadeBeats = 0.0;     // fundido cruzado con el tramo anterior, en tiempos del mix (0 = corte de 10 ms)
};

struct MixProject
{
    juce::String name;
    juce::File folder;          // <biblioteca>/_mixes/<nombre>
    double bpm = 0.0;           // tempo del mix; <= 0 = el tempo detectado del primer tramo
    bool keepTempos = false;    // true: cada tramo suena a su tempo original (sin estirar salvo playBpm propio)
    std::vector<MixSource> sources;
    std::vector<MixSegment> segments;   // en el orden en que suenan

    juce::File sourcesFolder() const { return folder.getChildFile ("fuentes"); }
    juce::File sourceFile (int index) const;           // fuentes/<fileName>, o File() si el índice no existe
    juce::File renderFile() const { return folder.getChildFile ("mezcla.wav"); }

    // Tempo efectivo del mix (el pedido, o el detectado del primer tramo, o 120)
    double effectiveBpm() const;

    // --- Disco ---
    bool save() const;                                  // mix.json (crea la carpeta si hace falta)
    static bool load (const juce::File& mixFolder, MixProject& out);   // false si no hay mix.json legible
    static juce::StringArray list (const juce::File& mixesRoot);       // nombres de carpeta de los mixes (con mix.json), por nombre
    // Los mixes guardados con su nombre visible (el "name" de mix.json, que cambia al renombrar; si
    // falta, el de la carpeta), ordenados por ese nombre. Solo lee el nombre (no abre las fuentes).
    struct ListEntry
    {
        juce::File folder;
        juce::String name;
    };
    static std::vector<ListEntry> entries (const juce::File& mixesRoot);
    // Crea <mixesRoot>/<nombre legal> (sin pisar uno existente) con un mix.json vacío; File() si falla
    static juce::File create (const juce::File& mixesRoot, const juce::String& name);

    // Copia el archivo a fuentes/ (sin pisar otro con el mismo nombre), lee su largo y agrega la fuente
    // sin analizar. Devuelve su índice, o -1 si no es audio legible o no se pudo copiar.
    int addSource (const juce::File& audioFile, juce::AudioFormatManager&);
    // Quita la fuente y sus tramos, reindexa los tramos de las demás y borra su archivo de fuentes/
    void removeSource (int index);
};

// Cómo queda cada tramo en el mix (resultado de mix::layout, uno por tramo, mismo orden)
struct MixPlacement
{
    double outStart = 0.0, outEnd = 0.0;   // segundos del mix; outStart es la unión con el tramo anterior
    double srcStart = 0.0, srcEnd = 0.0;   // tramo de la fuente (MixSegment::start/end, acotados a la fuente)
    double srcBpm = 0.0;                   // tempo detectado del tramo (segmentBpm; si no hay, el de reproducción)
    double playBpm = 0.0;                  // tempo en el mix
    double ratio = 1.0;                    // duración en el mix / duración en la fuente = srcBpm / playBpm
    double fadeIn = 0.0;                   // segundos del mix antes de outStart en que el tramo ya entra (fundido cruzado)
    double fadeOut = 0.0;                  // segundos antes de outEnd en que se desvanece (= fadeIn del siguiente; 10 ms al final)
};

namespace mix
{
    constexpr double cutFadeSeconds = 0.010;     // fundido de una unión sin fundido musical (corte)
    constexpr double edgeFadeSeconds = 0.002;    // fundido de entrada del primer tramo (o sin audio previo)
    constexpr double renderSampleRate = 44100.0; // la de los modelos de separación: nadie remuestrea después

    // Tempo detectado de un tramo: con los tiempos de la fuente dentro de [start - 0,05, end + 0,05],
    // 60 · (último - primero en índices) / (último - primero en segundos). Así un tramo cortado de un
    // tiempo a otro dura exactamente sus tiempos al tempo del mix. 0 si hay menos de dos tiempos.
    double segmentBpm (const Analysis&, double start, double end);

    // El tiempo detectado más cercano a `seconds` (solo primeros tiempos de compás si downbeatsOnly;
    // si no hay ninguno, cualquier tiempo); `seconds` si no hay tiempos
    double snapToBeat (const Analysis&, double seconds, bool downbeatsOnly);
    // Primer tiempo de compás anterior / posterior (estrictamente, a más de 0,05 s) a `seconds`;
    // `seconds` si no hay
    double previousDownbeat (const Analysis&, double seconds);
    double nextDownbeat (const Analysis&, double seconds);
    // Número de compás (desde 1) que contiene ese instante: primeros tiempos de compás en o antes de
    // `seconds + 0,05`; 0 si está antes del primero o no hay análisis
    int barNumberAt (const Analysis&, double seconds);
    // Compases completos entre dos instantes (primeros tiempos de compás en [start - 0,05, end - 0,05))
    int barsBetween (const Analysis&, double start, double end);
    // Cantidad de compases (primeros tiempos de compás) de la fuente
    int barCount (const Analysis&);
    // Tramo de los compases first a last (inclusive, numerados como barNumberAt): desde el primer tiempo del
    // compás first (0 = desde el inicio de la canción, lo que precede al compás 1) hasta el del compás siguiente
    // a last (el final de la canción, `length`, si last es el último). Se ordenan y se acotan a los compases que
    // hay (last al menos 1). false sin compases.
    bool barRange (const Analysis&, double length, int first, int last, double& start, double& end);

    // Ubicación de todos los tramos (ver MixPlacement). Tramos con fuente inexistente o largo <= 0
    // quedan con outEnd == outStart (no suenan). Tempo de cada tramo: playBpm propio > 0; si no,
    // keepTempos ? su tempo detectado : effectiveBpm(). Sin tempo detectado, ratio = 1.
    // fadeIn (i > 0): fadeBeats > 0 ? fadeBeats · 60 / playBpm : cutFadeSeconds, acotado a lo que hay
    // de audio antes del inicio en la fuente (srcStart · ratio) y a la mitad de la duración de ambos
    // tramos. fadeIn del primero = 0. fadeOut = fadeIn del siguiente; el del último = cutFadeSeconds.
    std::vector<MixPlacement> layout (const MixProject&);
    double length (const MixProject&);   // outEnd del último tramo (0 si no hay)

    // Tiempo de la fuente -> tiempo del mix dentro de un tramo: outStart + (t - srcStart) · ratio
    inline double toMix (const MixPlacement& p, double sourceSeconds) { return p.outStart + (sourceSeconds - p.srcStart) * p.ratio; }

    // La canción que resulta del mix (sobre `out`, que conserva sus demás campos): name = el del mix;
    // analysis: tiempos de cada tramo en [srcStart - 0,05, srcEnd - 0,05) llevados al mix con toMix
    // (con su número en el compás de la fuente; ordenados, sin dos a menos de 30 ms), acordes
    // recortados al tramo, llevados al mix y transpuestos (music::transposeChord), unidos si quedan
    // iguales y contiguos en una unión; key = la del primer tramo transpuesta (music::transposeKey);
    // meter = el de la fuente del primer tramo; bpm = el del primer tramo. tempoRegions: una por
    // tramo en outStart con origBpm = su playBpm (playBpm 0), unidas si el tempo es el mismo (a
    // menos de 0,05 BPM); la primera en 0. markers: uno por tramo en outStart con su label (o el
    // nombre de la fuente). bpm = el del primer tramo; clickOffset = el primer tiempo del mix (o 0).
    // clips, notes y el nivelado quedan vacíos; playBpm = 0; transpose = 0.
    void describeSong (const MixProject&, SongInfo& out);

    // Lee [startSeconds, endSeconds) del archivo en estéreo a `sampleRate` (remuestreo con sinc
    // enventanado si hace falta; los mono se duplican; ceros fuera del audio). false si no se pudo leer.
    bool readRange (const juce::File&, double startSeconds, double endSeconds, double sampleRate,
                    juce::AudioFormatManager&, juce::AudioBuffer<float>& out);

    // Ajusta un corte a la transiente más cercana (arrangement::findOnset sobre ±60 ms del audio de la
    // fuente, ventana de 40 ms): el instante justo antes del golpe. `seconds` si no hay un golpe claro.
    double refineToOnset (const juce::File&, double seconds, juce::AudioFormatManager&);

    // Forma de onda para dibujar: `binsPerSecond` pares (mín, máx) por segundo del archivo, sobre el
    // máximo de los canales, leyendo por bloques (sin cargar el archivo entero). Vacío si no se pudo leer.
    // `shouldAbort` se consulta en cada bloque (vacío si se abortó).
    std::vector<float> computePeaks (const juce::File&, juce::AudioFormatManager&, int binsPerSecond, double& lengthSeconds,
                                     const std::function<bool()>& shouldAbort = {});

    // Render del mix: para cada tramo lee de su fuente solo lo que usa (más 0,5 s de contexto a cada
    // lado cuando hay que estirar o transponer), lo estira con razón constante y lo transpone
    // (stretcher::renderBuffer con un TimeMap de una sola sección: origBpm = srcBpm, playBpm, transpose)
    // o lo copia tal cual si ratio == 1 y no transpone, aplica su ganancia y los fundidos (entrada: seno
    // de 0 a 1 en [outStart - fadeIn, outStart]; salida: coseno de 1 a 0 en [outEnd - fadeOut, outEnd];
    // el primero entra con edgeFadeSeconds lineal) y lo suma en su lugar. Estéreo a `sampleRate`, de
    // largo ceil(length · sampleRate). Buffer vacío si se abortó o falló (con `error`).
    juce::AudioBuffer<float> render (const MixProject&, double sampleRate, juce::AudioFormatManager&,
                                     const std::function<bool()>& shouldAbort,
                                     const std::function<void (float)>& progress, juce::String& error);
}

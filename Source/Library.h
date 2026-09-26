#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// Convierte texto UTF-8 (tildes, ñ, símbolos) a juce::String sin warnings.
inline juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

constexpr double unmeasuredDb = -100.0;   // sonoridad o pico sin medir

struct SongMarker   // "Marker" a secas choca con AIFF.h de macOS (CoreServices)
{
    juce::String name;
    double seconds = 0.0;
    // Nivelado del tramo que empieza en este marcador
    double gainDb = 0.0;
    double lufs = unmeasuredDb, truePeakDb = unmeasuredDb;
    // Nivelado por pista del tramo: una entrada por stem (índice en `stems`), en dB sobre el audio
    std::vector<double> stemGainsDb;
    std::vector<double> stemLufs;
};

// Sección con tempo propio: detectada a partir de los tiempos del análisis (cada cambio de
// tempo sostenido abre una sección) o dividida a mano. La primera siempre empieza en 0.
struct TempoRegion
{
    static constexpr int followSong = -100;   // transpose: la sección sigue el tono de la canción
    double start = 0.0;      // segundos del audio original
    double origBpm = 120.0;  // tempo detectado (o el BPM del click si no hay análisis)
    double playBpm = 0.0;    // tempo de reproducción (0 = el de la canción)
    int transpose = followSong;   // semitonos propios de la sección, o followSong
};

// Tramo del audio original colocado en la línea de tiempo de la canción (arreglo, fase 4)
struct Clip
{
    double srcStart = 0.0, srcEnd = 0.0;   // segundos del audio original
    double position = 0.0;                 // dónde empieza en la línea de tiempo de la canción
    double length() const { return srcEnd - srcStart; }
    double end() const    { return position + length(); }
};

// Nota de texto anclada a un instante de la canción: se muestra en la guía de escenario
struct SongNote
{
    double seconds = 0.0;     // línea de tiempo de la canción
    double duration = 6.0;    // segundos visible (0 = hasta la siguiente nota o el final)
    juce::String text;
};

struct StemInfo
{
    juce::String name;       // nombre visible
    juce::String fileName;   // archivo dentro de la carpeta de la canción
    float gainDb = 0.0f;
    bool muted = false;
    int outputPair = 0;      // 0 = salidas 1-2, 1 = 3-4, ...
};

struct Beat
{
    double seconds = 0.0;
    int beatInBar = 1;       // 1 = primer tiempo del compás
};

struct Chord
{
    double start = 0.0, end = 0.0;
    juce::String name;       // "Am", "F", "G7"... ("N" = sin acorde)
};

// Resultado del análisis musical (tempo, tiempos, acordes, tonalidad). Vacío si no se analizó.
struct Analysis
{
    std::vector<Beat> beats;      // ordenados por tiempo
    std::vector<Chord> chords;    // ordenados, sin solaparse
    juce::String key;             // "La menor"
    int meter = 4;                // tiempos por compás detectados
    double bpm = 0.0;             // tempo global (mediana)

    bool isEmpty() const { return beats.empty() && chords.empty(); }
    // Tempo medio entre dos instantes según los tiempos detectados; 0 si no hay al menos dos tiempos
    double bpmBetween (double from, double to) const;
    // Índice del acorde que suena en ese instante, o -1
    int chordAt (double seconds) const;
    // Índice del primer tiempo de compás (beatInBar == 1) en o después de ese instante, o -1
    int downbeatAfter (double seconds) const;
    // Índice del tiempo más cercano a ese instante (a menos de `maxDistance` s), o -1
    int nearestBeat (double seconds, double maxDistance) const;
    // Vuelve a numerar los tiempos desde `from` hasta `toExclusive` con compases de `meter`
    // tiempos, de modo que `anchor` (por defecto `from`) sea el 1; el compás anterior queda
    // más corto si hace falta
    void renumberBeats (int from, int meter, int toExclusive, int anchor = -1);
    // Índice del primer tiempo en o después de ese instante (beats.size() si no hay)
    int firstBeatAtOrAfter (double seconds) const;
    // Índice del primer tiempo de compás dentro de [from, toExclusive), o `from` si no hay
    int firstDownbeatIn (int from, int toExclusive) const;
    // Edición del click de un tramo [from, toExclusive): deja un tiempo de cada dos (el análisis
    // detectó corcheas), inserta uno entre cada dos (detectó blancas), o reemplaza los tiempos
    // entre dos instantes por una rejilla regular. Todas renumeran el tramo con `meter`.
    void halveBeats (int from, int toExclusive, int meter);
    void doubleBeats (int from, int toExclusive, int meter);
    void replaceBeatsWithGrid (double fromSeconds, double toSeconds, double gridBpm, int meter);
    // Reemplaza los tiempos de [from, to) por `count` tiempos parejos que caben exactos en el
    // tramo (el último termina justo antes de `to`), numerados con `meter`; si sobran menos de
    // medio compás, esos tiempos se suman al último compás completo (compás de 5, por ejemplo)
    // en vez de formar uno de 1 tiempo. Devuelve el tempo resultante en BPM.
    double evenGrid (double fromSeconds, double toSeconds, int count, int meter, bool absorbRemainder = true);
    // Compás corto solo aquí: los tiempos [index, index + beatsInBar) forman un compás propio
    // (1..beatsInBar, con su acento) y desde el siguiente se vuelve a numerar con `meterAfter`
    // hasta `toExclusive`. Sirve para un compás de 1 o 2 tiempos en un enganche.
    void shortBar (int index, int beatsInBar, int meterAfter, int toExclusive);
};

struct SongInfo
{
    juce::String name;
    juce::File folder;
    double bpm = 120.0;
    double clickOffset = 0.0;   // segundos hasta el primer tiempo
    bool clickEnabled = false;
    float clickGainDb = -6.0f;
    int clickOutputPair = 0;
    float masterGainDb = 0.0f;    // fader maestro
    // Nivelado (EBU R128): la canción entera y el tramo anterior al primer marcador
    double loudnessLufs = unmeasuredDb, truePeakDb = unmeasuredDb;
    double songGainDb = 0.0;      // nivelado entre canciones del setlist (se aplica siempre)
    bool levelingEnabled = false; // aplicar las ganancias por tramo
    double headGainDb = 0.0, headLufs = unmeasuredDb, headTruePeakDb = unmeasuredDb;
    std::vector<double> headStemGainsDb, headStemLufs;   // nivelado por pista del tramo anterior al primer marcador
    std::vector<double> stemSongLufs;                    // sonoridad de cada stem en toda la canción (referencia del nivelado por pista)
    // Tempo y tono de reproducción (fase 3): 0 = tempo original; semitonos enteros
    double playBpm = 0.0;
    int transpose = 0;
    // Escritura de notas: 0 = según la tonalidad, 1 = sostenidos (Re#), 2 = bemoles (Mib)
    int spelling = 0;
    juce::String keyOverride;     // tonalidad elegida a mano ("Eb major"); vacío = la detectada
    Analysis analysis;
    std::vector<TempoRegion> tempoRegions;   // ordenadas; vacío = un solo tramo con el tempo del análisis o del click
    std::vector<Clip> clips;                 // arreglo del audio; vacío = el audio original entero
    std::vector<SongNote> notes;             // notas de texto, ordenadas por tiempo
    std::vector<SongMarker> markers;  // siempre ordenados por tiempo
    std::vector<StemInfo> stems;

    void sortMarkers();
    void sortNotes();
    void fitStemArrays();         // deja los vectores por stem del tamaño de `stems` (0 dB, sin medir)
    void sortTempoRegions();      // ordena y deja la primera en 0
    void mergeEqualTempoRegions();   // une secciones vecinas con el mismo tempo original y de reproducción
    int tempoRegionAt (double originalSeconds) const;   // índice de la sección que contiene ese instante, o -1 si no hay
};

// Biblioteca en ~/Music/Secuencias: una carpeta por canción + setlist.json
class Library
{
public:
    // Por defecto: ~/Music/Secuencias (o la carpeta de Música según XDG). Los tests pasan una carpeta temporal.
    explicit Library (const juce::File& rootFolder = defaultRoot());
    static juce::File defaultRoot();

    void load();
    bool saveSong (const SongInfo&) const;
    void saveSetlist() const;

    // moveFiles: mover en vez de copiar (solo para archivos temporales propios, como el resultado de Demucs)
    int importStemFiles (const juce::Array<juce::File>& files, const juce::String& songName, bool moveFiles = false);
    int importStemFolder (const juce::File& folder, const juce::String& songName, bool moveFiles = false);
    // Proyecto: una carpeta de canción completa (song.json + stems). Importar la copia a la biblioteca
    // con todos sus ajustes; exportar copia la carpeta de la canción (o todas y el setlist) a otro lugar.
    static bool isProjectFolder (const juce::File& folder);
    // Separación de una canción del setlist: sus archivos de audio pasan a la subcarpeta "original"
    // y los stems de `resultFolder` (temporales propios) se mueven a la carpeta de la canción; el
    // resto (marcadores, análisis, tempo, cortes) se conserva. El nivelado por pista se reinicia.
    bool replaceStems (int index, const juce::File& resultFolder);
    int importProject (const juce::File& folder);
    bool exportSong (int index, const juce::File& destinationFolder) const;
    bool exportAll (const juce::File& destinationFolder) const;
    bool moveToTrash (int index);
    void move (int index, int delta);

    juce::File getRoot() const { return root; }

    // Formatos que la app puede decodificar en esta plataforma (m4a solo en macOS).
    static bool isAudioFile (const juce::File&);
    static juce::String audioFilePatterns();     // "*.wav;*.flac;..." para los selectores de archivos
    static juce::String audioFormatsDescription();   // "wav, aiff, flac, ..." para mensajes
    static juce::Array<juce::File> audioFilesIn (const juce::File& folder);

    std::vector<SongInfo> songs;

private:
    SongInfo readSong (const juce::File& folder) const;
    juce::File root;
};

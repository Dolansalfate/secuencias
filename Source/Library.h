#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// Convierte texto UTF-8 (tildes, ñ, símbolos) a juce::String sin warnings.
inline juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

struct Marker
{
    juce::String name;
    double seconds = 0.0;
};

struct StemInfo
{
    juce::String name;       // nombre visible
    juce::String fileName;   // archivo dentro de la carpeta de la canción
    float gainDb = 0.0f;
    bool muted = false;
    int outputPair = 0;      // 0 = salidas 1-2, 1 = 3-4, ...
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
    std::vector<Marker> markers;  // siempre ordenados por tiempo
    std::vector<StemInfo> stems;

    void sortMarkers();
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

    int importStemFiles (const juce::Array<juce::File>& files, const juce::String& songName);
    int importStemFolder (const juce::File& folder, const juce::String& songName);
    bool moveToTrash (int index);
    void move (int index, int delta);

    juce::File getRoot() const { return root; }
    static bool isAudioFile (const juce::File&);
    static juce::Array<juce::File> audioFilesIn (const juce::File& folder);

    std::vector<SongInfo> songs;

private:
    SongInfo readSong (const juce::File& folder) const;
    juce::File root;
};

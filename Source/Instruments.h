#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include <functional>
#include <memory>
#include <vector>

// Rack de instrumentos VST3 / AU (punto 3). Es global (no por canción): los mismos plugins sirven
// para todo el setlist. Cada instrumento tiene un id estable (los triggers de las pistas lo
// referencian como "vst:<id>"), su plugin ya preparado, el estado guardado (instrumentos.xml
// junto a los ajustes) y su canal del mezclador (InstrumentLane::control). Los golpes detectados
// en las pistas le llegan como notas MIDI desde el motor (SamplerLane::instrumentId). Todo aquí
// corre en el hilo de mensajes salvo la búsqueda de plugins, que va en un hilo aparte.
class InstrumentRack
{
public:
    InstrumentRack (AudioEngine&, const juce::File& settingsFolder);
    ~InstrumentRack();

    struct Slot
    {
        int id = 0;
        juce::PluginDescription description;
        juce::String error;                       // por qué no cargó (plugin ausente, formato no disponible)
        std::shared_ptr<InstrumentLane> lane;     // nullptr mientras carga o si falló
        std::unique_ptr<juce::DocumentWindow> editor;
        float gainDb = 0.0f;
        bool muted = false;
        int outputPair = 0;
        juce::MemoryBlock savedState;             // estado guardado; se aplica al cargar
        bool loading = false;
    };

    const std::vector<Slot>& getSlots() const { return slots; }
    Slot* slot (int id);
    bool has (int id) const;                      // existe y está cargado
    juce::String nameOf (int id) const;           // nombre del plugin, o "" si no existe
    bool isLoading() const;                       // hay plugins cargándose

    void restore();                               // lee instrumentos.xml y plugins.xml y carga los plugins (asíncrono)
    void save();                                  // instrumentos.xml (descripción, estado y mezcla de cada uno) y plugins.xml
    // Agrega un plugin al rack y lo carga; `done` llega en el hilo de la UI con el id y el error ("" si cargó)
    void add (const juce::PluginDescription&, std::function<void (int id, const juce::String& error)> done);
    void remove (int id);
    void openEditor (int id);                     // ventana del plugin (o un editor genérico si no tiene)
    void closeEditors();
    void prepareAll (double sampleRate);          // cambió la frecuencia del dispositivo
    void pullMixFromLanes();                      // fader, mute y salida de las líneas a los slots (para guardar)

    // Búsqueda de plugins instalados (VST3; AU en macOS) más `extraFolders`, en un hilo aparte;
    // `progress` y `done` llegan en el hilo de la UI.
    void scan (const juce::FileSearchPath& extraFolders, std::function<void (float, const juce::String&)> progress,
               std::function<void (int found)> done);
    bool isScanning() const;
    void cancelScan();
    juce::Array<juce::PluginDescription> knownInstruments() const;   // solo instrumentos, por nombre
    // Plugins de un archivo o bundle concreto (por ejemplo para la captura); quedan como conocidos
    juce::Array<juce::PluginDescription> describeFile (const juce::File&);
    static juce::String formatsAvailable();       // "VST3" o "VST3 y AU"

    std::function<void()> onChanged;              // el conjunto cargado cambió (mezclador, menús, casillas)

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

private:
    class Scanner;
    void publish();                               // entrega al motor un InstrumentSet con las líneas cargadas
    void startLoad (int id, std::function<void (int, const juce::String&)> done);
    std::shared_ptr<InstrumentLane> makeLane (Slot&, std::unique_ptr<juce::AudioPluginInstance>);

    AudioEngine& engine;
    juce::File folder;
    int nextId = 1;
    std::vector<Slot> slots;
    std::unique_ptr<Scanner> scanner;
    std::shared_ptr<int> alive = std::make_shared<int> (0);   // para que los callbacks tardíos no toquen un rack destruido
};

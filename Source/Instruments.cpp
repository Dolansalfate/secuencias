#include "Instruments.h"
#include "Library.h"
#include <algorithm>

namespace
{
    // Ventana del editor de un plugin; al cerrarla avisa al rack, que la destruye en el siguiente mensaje
    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow (const juce::String& title, juce::AudioProcessorEditor* editor, std::function<void()> closed)
            : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
              onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (juce::jmax (200, getWidth()), juce::jmax (100, getHeight()));
            setVisible (true);
            toFront (true);
        }
        void closeButtonPressed() override
        {
            if (onClosed)
                onClosed();
        }
    private:
        std::function<void()> onClosed;
    };
}

//==============================================================================
class InstrumentRack::Scanner : public juce::Thread
{
public:
    Scanner (InstrumentRack& r, juce::FileSearchPath extra, std::function<void (float, const juce::String&)> p, std::function<void (int)> d)
        : Thread ("Busqueda de plugins"), rack (r), extraPaths (std::move (extra)), progress (std::move (p)), done (std::move (d)) {}
    ~Scanner() override { stopThread (10000); }

    void run() override
    {
        const int before = rack.known.getNumTypes();
        const auto pedal = rack.folder.getChildFile ("plugin-en-prueba.txt");   // "dead man's pedal": el plugin que tumbe la app queda vetado
        const int numFormats = rack.formats.getNumFormats();
        for (int f = 0; f < numFormats && ! threadShouldExit(); ++f)
        {
            auto* format = rack.formats.getFormat (f);
            if (! format->canScanForPlugins())
                continue;
            auto paths = format->getDefaultLocationsToSearch();
            for (int i = 0; i < extraPaths.getNumPaths(); ++i)
                paths.add (extraPaths[i]);
            juce::PluginDirectoryScanner dirScanner (rack.known, *format, paths, true, pedal, true);
            juce::String name;
            for (;;)
            {
                if (threadShouldExit())
                    break;
                const bool more = dirScanner.scanNextFile (true, name);
                const float pr = ((float) f + dirScanner.getProgress()) / (float) juce::jmax (1, numFormats);
                juce::MessageManager::callAsync ([cb = progress, pr, name] { if (cb) cb (pr, name); });
                if (! more)
                    break;
            }
        }
        const int found = rack.known.getNumTypes() - before;
        juce::MessageManager::callAsync ([cb = done, found] { if (cb) cb (found); });
    }

private:
    InstrumentRack& rack;
    juce::FileSearchPath extraPaths;
    std::function<void (float, const juce::String&)> progress;
    std::function<void (int)> done;
};

//==============================================================================
InstrumentRack::InstrumentRack (AudioEngine& e, const juce::File& settingsFolder)
    : engine (e), folder (settingsFolder)
{
    formats.addDefaultFormats();   // VST3 en todas las plataformas; AU en macOS
}

InstrumentRack::~InstrumentRack()
{
    scanner.reset();
    closeEditors();
    engine.setInstruments (nullptr);   // el motor suelta las líneas; los plugins se destruyen aquí, en el hilo de mensajes
    slots.clear();
}

juce::String InstrumentRack::formatsAvailable()
{
   #if JUCE_MAC
    return "VST3 y AU";
   #else
    return "VST3";
   #endif
}

InstrumentRack::Slot* InstrumentRack::slot (int id)
{
    for (auto& s : slots)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool InstrumentRack::has (int id) const
{
    for (auto& s : slots)
        if (s.id == id)
            return s.lane != nullptr;
    return false;
}

juce::String InstrumentRack::nameOf (int id) const
{
    for (auto& s : slots)
        if (s.id == id)
            return s.description.name;
    return {};
}

bool InstrumentRack::isLoading() const
{
    for (auto& s : slots)
        if (s.loading)
            return true;
    return false;
}

std::shared_ptr<InstrumentLane> InstrumentRack::makeLane (Slot& s, std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    auto lane = std::make_shared<InstrumentLane>();
    lane->id = s.id;
    lane->name = s.description.name;
    lane->control.name = lane->name;
    lane->control.stemIndex = -1;
    lane->control.gain = s.gainDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (s.gainDb);
    lane->control.smoothedGain = lane->control.gain.load();
    lane->control.muted = s.muted;
    lane->control.outputPair = s.outputPair;

    const double sr = engine.getSampleRate();
    plugin->setNonRealtime (false);
    if (s.savedState.getSize() > 0)
        plugin->setStateInformation (s.savedState.getData(), (int) s.savedState.getSize());
    plugin->prepareToPlay (sr, AudioEngine::maxBlockSize);
    lane->latency = juce::jmax (0, plugin->getLatencySamples());
    lane->holdSamples = juce::jmax (1, (int) (0.05 * sr));
    const int channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    lane->work.setSize (channels, AudioEngine::maxBlockSize);
    lane->work.clear();
    lane->midi.ensureSize (16384);
    lane->plugin = std::move (plugin);
    return lane;
}

void InstrumentRack::publish()
{
    auto set = std::make_shared<InstrumentSet>();
    for (auto& s : slots)
        if (s.lane != nullptr)
            set->lanes.push_back (s.lane);
    engine.setInstruments (set);
    if (onChanged)
        onChanged();
}

void InstrumentRack::startLoad (int id, std::function<void (int, const juce::String&)> done)
{
    auto* s = slot (id);
    if (s == nullptr)
        return;
    s->loading = true;
    std::weak_ptr<int> weak (alive);
    formats.createPluginInstanceAsync (s->description, engine.getSampleRate(), AudioEngine::maxBlockSize,
                                       [this, id, done, weak] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
    {
        if (weak.expired())
            return;   // el rack ya no existe: el plugin se destruye aquí mismo
        auto* sl = slot (id);
        if (sl == nullptr)
            return;   // se quitó mientras cargaba
        sl->loading = false;
        if (instance == nullptr)
            sl->error = error.isNotEmpty() ? error : tr ("No se pudo cargar el plugin");
        else
        {
            sl->error.clear();
            sl->lane = makeLane (*sl, std::move (instance));
        }
        publish();
        save();
        if (done)
            done (id, sl->error);
    });
}

void InstrumentRack::add (const juce::PluginDescription& description, std::function<void (int, const juce::String&)> done)
{
    Slot s;
    s.id = nextId++;
    s.description = description;
    slots.push_back (std::move (s));
    startLoad (slots.back().id, std::move (done));
}

void InstrumentRack::remove (int id)
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i].id == id)
        {
            slots[i].editor.reset();   // antes que el plugin
            slots.erase (slots.begin() + (long) i);
            break;
        }
    publish();   // el InstrumentSet anterior se libera fuera del lock: ahí muere el plugin quitado
    save();
}

void InstrumentRack::openEditor (int id)
{
    auto* s = slot (id);
    if (s == nullptr || s->lane == nullptr || s->lane->plugin == nullptr)
        return;
    if (s->editor != nullptr)
    {
        s->editor->toFront (true);
        return;
    }
    auto& plugin = *s->lane->plugin;
    juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (plugin);
    std::weak_ptr<int> weak (alive);
    s->editor = std::make_unique<PluginWindow> (s->description.name, editor, [this, id, weak]
    {
        juce::MessageManager::callAsync ([this, id, weak]
        {
            if (weak.expired())
                return;
            if (auto* sl = slot (id))
                sl->editor.reset();
            save();   // lo que se haya tocado en el editor
        });
    });
}

void InstrumentRack::closeEditors()
{
    for (auto& s : slots)
        s.editor.reset();
}

void InstrumentRack::prepareAll (double sampleRate)
{
    engine.setInstruments (nullptr);   // mientras se reconfiguran, el motor no los procesa
    for (auto& s : slots)
        if (s.lane != nullptr && s.lane->plugin != nullptr)
        {
            s.lane->plugin->releaseResources();
            s.lane->plugin->prepareToPlay (sampleRate, AudioEngine::maxBlockSize);
            s.lane->latency = juce::jmax (0, s.lane->plugin->getLatencySamples());
            s.lane->holdSamples = juce::jmax (1, (int) (0.05 * sampleRate));
        }
    publish();
}

void InstrumentRack::pullMixFromLanes()
{
    for (auto& s : slots)
        if (s.lane != nullptr)
        {
            const float g = s.lane->control.gain.load();
            s.gainDb = g <= 0.0f ? -60.0f : juce::Decibels::gainToDecibels (g, -60.0f);
            s.muted = s.lane->control.muted.load();
            s.outputPair = s.lane->control.outputPair.load();
        }
}

void InstrumentRack::save()
{
    pullMixFromLanes();
    folder.createDirectory();
    juce::XmlElement root ("INSTRUMENTOS");
    root.setAttribute ("siguienteId", nextId);
    for (auto& s : slots)
    {
        auto* e = root.createNewChildElement ("INSTRUMENTO");
        e->setAttribute ("id", s.id);
        e->setAttribute ("gainDb", (double) s.gainDb);
        e->setAttribute ("muted", s.muted);
        e->setAttribute ("outputPair", s.outputPair);
        if (s.lane != nullptr && s.lane->plugin != nullptr)
        {
            juce::MemoryBlock state;
            s.lane->plugin->getStateInformation (state);
            if (state.getSize() > 0)
                s.savedState = state;
        }
        if (s.savedState.getSize() > 0)
            e->setAttribute ("estado", s.savedState.toBase64Encoding());
        if (auto d = s.description.createXml())
            e->addChildElement (d.release());
    }
    root.writeTo (folder.getChildFile ("instrumentos.xml"));
    if (auto k = known.createXml())
        k->writeTo (folder.getChildFile ("plugins.xml"));
}

void InstrumentRack::restore()
{
    folder.createDirectory();
    if (auto k = juce::parseXML (folder.getChildFile ("plugins.xml")))
        known.recreateFromXml (*k);
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (known, folder.getChildFile ("plugin-en-prueba.txt"));

    auto xml = juce::parseXML (folder.getChildFile ("instrumentos.xml"));
    if (xml == nullptr || ! xml->hasTagName ("INSTRUMENTOS"))
        return;
    nextId = juce::jmax (1, xml->getIntAttribute ("siguienteId", 1));
    for (auto* e : xml->getChildWithTagNameIterator ("INSTRUMENTO"))
    {
        auto* d = e->getChildByName ("PLUGIN");
        if (d == nullptr)
            continue;
        Slot s;
        s.id = e->getIntAttribute ("id", nextId);
        s.gainDb = (float) e->getDoubleAttribute ("gainDb", 0.0);
        s.muted = e->getBoolAttribute ("muted", false);
        s.outputPair = juce::jmax (0, e->getIntAttribute ("outputPair", 0));
        s.description.loadFromXml (*d);
        s.savedState.fromBase64Encoding (e->getStringAttribute ("estado"));
        nextId = juce::jmax (nextId, s.id + 1);
        slots.push_back (std::move (s));
    }
    std::vector<int> ids;
    for (auto& s : slots)
        ids.push_back (s.id);
    for (int id : ids)
        startLoad (id, {});
}

void InstrumentRack::scan (const juce::FileSearchPath& extraFolders, std::function<void (float, const juce::String&)> progress,
                           std::function<void (int)> done)
{
    scanner.reset();
    scanner = std::make_unique<Scanner> (*this, extraFolders, std::move (progress), std::move (done));
    scanner->startThread();
}

bool InstrumentRack::isScanning() const
{
    return scanner != nullptr && scanner->isThreadRunning();
}

void InstrumentRack::cancelScan()
{
    scanner.reset();
}

juce::Array<juce::PluginDescription> InstrumentRack::knownInstruments() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : known.getTypes())
        if (d.isInstrument)
            out.add (d);
    std::sort (out.begin(), out.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

juce::Array<juce::PluginDescription> InstrumentRack::describeFile (const juce::File& file)
{
    juce::Array<juce::PluginDescription> out;
    for (auto* format : formats.getFormats())
    {
        if (! format->fileMightContainThisPluginType (file.getFullPathName()))
            continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, file.getFullPathName());
        for (auto* d : found)
        {
            known.addType (*d);
            out.add (*d);
        }
    }
    return out;
}

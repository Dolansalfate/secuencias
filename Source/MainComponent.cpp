#include "MainComponent.h"
#include "UiUtils.h"
#include "Music.h"
#include <algorithm>
#include <iostream>

using namespace ui;


namespace
{
    constexpr int saveDelayMs = 500;   // retardo para guardar song.json tras un cambio de mezcla o click

    // Python de un venv en la carpeta del usuario: <venv>/bin/python, o <venv>\Scripts\python.exe en Windows
    juce::String venvPython (const char* venvName)
    {
        const auto venv = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (venvName);
       #if JUCE_WINDOWS
        return venv.getChildFile ("Scripts/python.exe").getFullPathName();
       #else
        return venv.getChildFile ("bin/python").getFullPathName();
       #endif
    }

}

//==============================================================================
class MarkerButton : public juce::TextButton
{
public:
    using juce::TextButton::clicked;
    std::function<void (bool popup)> onPress;
    void clicked (const juce::ModifierKeys& mods) override
    {
        if (onPress)
            onPress (mods.isPopupMenu());
    }
};

//==============================================================================
MainComponent::MainComponent()
{
    formatManager.registerBasicFormats();

    juce::PropertiesFile::Options opts;
    opts.applicationName = "Secuencias";
    opts.filenameSuffix = ".settings";
   #if JUCE_LINUX
    opts.folderName = ".config/Secuencias";   // ~/.config/Secuencias/Secuencias.settings
   #else
    opts.folderName = "Secuencias";
   #endif
    opts.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (opts);

    const auto savedAudio = props.getUserSettings()->getXmlValue ("audioDevice");
    const auto audioError = deviceManager.initialise (0, 2, savedAudio.get(), true);
    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    // Rack de instrumentos: junto a los ajustes; los plugins guardados se cargan (asíncrono) cuando la ventana ya existe
    rack = std::make_unique<InstrumentRack> (engine, props.getUserSettings()->getFile().getParentDirectory());
    rack->onChanged = [this]
    {
        mixer.setInstruments (engine.getInstruments());
        refreshReplaced();
        syncTriggerMarks();
    };
    juce::Component::SafePointer<MainComponent> safeSelf (this);   // (MSVC no acepta `this` en una init-capture anidada)
    juce::MessageManager::callAsync ([safeSelf]
    {
        if (safeSelf != nullptr && safeSelf->rack != nullptr)
            safeSelf->rack->restore();
    });

    library.load();

    // --- Barra superior ---
    importBtn.setButtonText ("Importar stems");
    importBtn.onClick = [this] { chooseStems(); };
    separateBtn.setButtonText (tr ("Separar canción (IA)"));
    separateBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    separateBtn.onClick = [this] { chooseSongToSeparate(); };
    stemsBox.addItem ("4 pistas", 1);
    stemsBox.addItem ("6 pistas: + guitarra y piano", 2);
    qualityBox.addItem ("Calidad normal", 1);
    qualityBox.addItem ("Calidad alta", 2);
    qualityBox.addItem (tr ("Calidad máxima (lenta)"), 3);
    {
        // Ajustes nuevos; si solo existe el "model" de la v0.2 se traduce una vez
        auto* settings = props.getUserSettings();
        const int oldModel = settings->getIntValue ("model", 0);
        stemsBox.setSelectedId (settings->getIntValue ("stems", oldModel == 2 ? 2 : 1), juce::dontSendNotification);
        qualityBox.setSelectedId (settings->getIntValue ("quality", oldModel == 3 ? 2 : 1), juce::dontSendNotification);
    }
    stemsBox.onChange = [this] { props.getUserSettings()->setValue ("stems", stemsBox.getSelectedId()); };
    qualityBox.onChange = [this] { props.getUserSettings()->setValue ("quality", qualityBox.getSelectedId()); };
    drumPartsBtn.setButtonText (tr ("Batería en partes"));
    drumPartsBtn.setToggleState (props.getUserSettings()->getBoolValue ("drumParts", false), juce::dontSendNotification);
    drumPartsBtn.onClick = [this] { props.getUserSettings()->setValue ("drumParts", drumPartsBtn.getToggleState()); };
    addAndMakeVisible (drumPartsBtn);
    audioBtn.setButtonText ("Audio");
    audioBtn.onClick = [this] { showAudioSettings(); };
    mixBtn.setButtonText ("Armar mix");
    mixBtn.onClick = [this] { mixMenu(); };
    instrumentsBtn.setButtonText ("Instrumentos");
    instrumentsBtn.onClick = [this] { instrumentsMenu(); };
    aiBtn.setButtonText ("Ajustes IA");
    aiBtn.onClick = [this] { showAiSettings(); };
    cancelSepBtn.setButtonText ("Cancelar");
    cancelSepBtn.onClick = [this] { separator.cancel(); analyzer.cancel(); };
    sepLabel.setFont (font (13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &importBtn, &separateBtn, &mixBtn, &stemsBox, &qualityBox, &audioBtn, &aiBtn, &instrumentsBtn,
                     &sepBar, &sepLabel, &cancelSepBtn })
        addAndMakeVisible (c);
    sepBar.setVisible (false);
    cancelSepBtn.setVisible (false);

    // --- Setlist ---
    setlistTitle.setText ("SETLIST", juce::dontSendNotification);
    setlistTitle.setFont (font (13.0f, true));
    setlistTitle.setColour (juce::Label::textColourId, juce::Colours::grey);
    levelSetlistBtn.setButtonText ("Nivelar setlist");
    levelSetlistBtn.onClick = [this] { levelSetlist(); };
    addAndMakeVisible (levelSetlistBtn);
    setlist.setModel (this);
    setlist.setRowHeight (34);
    setlist.setColour (juce::ListBox::backgroundColourId, panel);
    addAndMakeVisible (setlistTitle);
    addAndMakeVisible (setlist);

    // --- Canción y transporte ---
    songTitle.setFont (font (28.0f, true));
    nextLabel.setFont (font (14.0f));
    nextLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    sectionLabel.setFont (font (22.0f, true));
    sectionLabel.setColour (juce::Label::textColourId, accent);
    timeLabel.setFont (font (22.0f));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    chordLabel.setFont (font (22.0f, true));
    chordLabel.setJustificationType (juce::Justification::centred);
    chordLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    keyLabel.setFont (font (14.0f));
    keyLabel.setJustificationType (juce::Justification::centredRight);
    keyLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    keyLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    keyLabel.addMouseListener (this, false);   // clic = menú de tonalidad y enarmonía

    prevBtn.setButtonText ("<<");
    prevBtn.onClick = [this] { nextSong (-1); };
    playBtn.setButtonText ("Play");
    playBtn.onClick = [this] { engine.togglePlay(); };
    stopBtn.setButtonText ("Stop");
    stopBtn.onClick = [this] { engine.stop(); updateLoopRegion(); };
    nextBtn.setButtonText (">>");
    nextBtn.onClick = [this] { nextSong (1); };
    recordBtn.setButtonText ("Grabar");
    recordBtn.onClick = [this]
    {
        if (recording.active)
            stopRecording (false);
        else
            recordDialog (false, -1);
    };
    loopBtn.setButtonText (tr ("Loop de sección"));
    loopBtn.onClick = [this] { updateLoopRegion(); };

    positionSlider.setSliderStyle (juce::Slider::LinearBar);
    positionSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    positionSlider.setRange (0.0, 1.0);
    positionSlider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.5f));
    positionSlider.onValueChange = [this]
    {
        engine.seekSeconds (positionSlider.getValue());
        updateLoopRegion();
    };

    addMarkerBtn.setButtonText ("+ Marcador");
    addMarkerBtn.onClick = [this] { addMarkerHere(); };
    addNoteBtn.setButtonText ("+ Nota");
    addNoteBtn.onClick = [this] { addNoteHere(); };
    addAndMakeVisible (addNoteBtn);
    cutModeBox.addItem ("Corte libre", 1);
    cutModeBox.addItem ("Corte a la rejilla", 2);
    cutModeBox.addItem ("Corte a la transiente", 3);
    cutModeBox.setSelectedId (juce::jlimit (1, 3, props.getUserSettings()->getIntValue ("cutMode", 1)), juce::dontSendNotification);
    cutModeBox.onChange = [this] { props.getUserSettings()->setValue ("cutMode", cutModeBox.getSelectedId()); };

    for (auto* c : std::initializer_list<juce::Component*> { &songTitle, &nextLabel, &sectionLabel, &timeLabel, &chordLabel, &keyLabel,
                     &prevBtn, &playBtn, &stopBtn, &nextBtn, &recordBtn, &loopBtn, &positionSlider, &addMarkerBtn, &cutModeBox })
        addAndMakeVisible (c);

    // --- Click ---
    clickBtn.setButtonText ("Click");
    clickBtn.onClick = [this] { if (auto* s = currentInfo()) { s->clickEnabled = clickBtn.getToggleState(); pushClick(); } };
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    offsetLabel.setText ("Inicio (s)", juce::dontSendNotification);
    clickVolLabel.setText ("Vol", juce::dontSendNotification);

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    bpmSlider.setRange (30.0, 300.0, 0.5);
    bpmSlider.onValueChange = [this]
    {
        if (auto* s = currentInfo())
        {
            s->bpm = bpmSlider.getValue();
            if (s->analysis.beats.empty())
            {
                // Sin análisis, las secciones de tempo toman su tempo original del click
                for (auto& r : s->tempoRegions)
                    r.origBpm = s->bpm;
                syncTimelineMarkers();
                requestRender();
            }
            pushClick();
        }
    };

    offsetSlider.setSliderStyle (juce::Slider::IncDecButtons);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    offsetSlider.setRange (0.0, 30.0, 0.01);
    offsetSlider.onValueChange = [this] { if (auto* s = currentInfo()) { s->clickOffset = offsetSlider.getValue(); pushClick(); } };

    clickGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    clickGainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    clickGainSlider.setRange (-40.0, 6.0, 0.5);
    clickGainSlider.onValueChange = [this] { if (auto* s = currentInfo()) { s->clickGainDb = (float) clickGainSlider.getValue(); pushClick(); } };

    clickOutBox.onChange = [this] { if (auto* s = currentInfo()) { s->clickOutputPair = juce::jmax (0, clickOutBox.getSelectedId() - 1); pushClick(); } };

    analyzeBtn.setButtonText ("Analizar (IA)");
    analyzeBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    analyzeBtn.onClick = [this] { startAnalysis(); };

    levelBtn.setButtonText ("Nivelar");
    levelBtn.onClick = [this]
    {
        auto* info = currentInfo();
        if (info == nullptr || currentSong == nullptr)
        {
            levelBtn.setToggleState (false, juce::dontSendNotification);
            return;
        }
        if (levelBtn.getToggleState() && needsLevelMeasure (*info))
        {
            measureAndLevel();   // primera vez, o hay marcadores nuevos sin medir: medir y aplicar
            return;
        }
        info->levelingEnabled = levelBtn.getToggleState();
        library.saveSong (*info);
        updateGainCurve();
        rebuildMarkers();
    };
    targetBox.addItem ("-12 LUFS", 1);
    targetBox.addItem ("-14 LUFS", 2);
    targetBox.addItem ("-16 LUFS", 3);
    targetBox.addItem ("-18 LUFS", 4);
    targetBox.setSelectedId (props.getUserSettings()->getIntValue ("levelTarget", 2), juce::dontSendNotification);
    targetBox.onChange = [this]
    {
        props.getUserSettings()->setValue ("levelTarget", targetBox.getSelectedId());
        if (auto* info = currentInfo())
        {
            recomputeSectionGains (*info);   // las mediciones valen; solo cambia el objetivo
            library.saveSong (*info);
            updateGainCurve();
            rebuildMarkers();
        }
    };

    for (auto* c : std::initializer_list<juce::Component*> { &clickBtn, &bpmLabel, &offsetLabel, &clickVolLabel, &bpmSlider,
                     &offsetSlider, &clickGainSlider, &clickOutBox, &analyzeBtn, &levelBtn, &targetBox })
        addAndMakeVisible (c);

    // --- Tempo y tono ---
    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    transposeLabel.setText ("Tono (semitonos)", juce::dontSendNotification);
    tempoInfoLabel.setFont (font (13.0f));
    tempoInfoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    tempoSlider.setSliderStyle (juce::Slider::IncDecButtons);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    tempoSlider.setRange (40.0, 300.0, 0.5);
    tempoSlider.onValueChange = [this]
    {
        if (auto* s = currentInfo())
        {
            const double v = tempoSlider.getValue();
            s->playBpm = std::abs (v - originalBpm()) < 0.05 ? 0.0 : v;   // igual al original = sin cambio
            syncTimelineMarkers();
            requestRender();
        }
    };
    tempoOrigBtn.setButtonText ("Original");
    tempoOrigBtn.onClick = [this]
    {
        if (auto* s = currentInfo())
        {
            s->playBpm = 0.0;
            for (auto& r : s->tempoRegions)
                r.playBpm = 0.0;
            tempoControlsFromSong();
            syncTimelineMarkers();
            requestRender();
        }
    };
    transposeSlider.setSliderStyle (juce::Slider::IncDecButtons);
    transposeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 24);
    transposeSlider.setRange (-12.0, 12.0, 1.0);
    transposeSlider.onValueChange = [this]
    {
        if (auto* s = currentInfo())
        {
            s->transpose = (int) std::lround (transposeSlider.getValue());
            requestRender();
        }
    };
    for (auto* c : std::initializer_list<juce::Component*> { &tempoLabel, &tempoSlider, &tempoOrigBtn, &transposeLabel,
                     &transposeSlider, &tempoInfoLabel })
        addAndMakeVisible (c);

    liveBtn.setButtonText ("En vivo");
    liveBtn.setClickingTogglesState (true);
    liveBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.4f));
    liveBtn.onClick = [this] { setLiveMode (liveBtn.getToggleState()); };
    stageBtn.setButtonText ("Pantalla");
    stageBtn.setClickingTogglesState (true);
    stageBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.4f));
    stageBtn.onClick = [this] { showStage (stageBtn.getToggleState()); };
    addAndMakeVisible (stageBtn);
    zoomFitBtn.setButtonText ("Ajustar");
    zoomFitBtn.onClick = [this] { timeline.zoomToFit(); };
    addAndMakeVisible (liveBtn);
    addAndMakeVisible (zoomFitBtn);

    // --- Vista de arreglo ---
    timeline.onSeek = [this] (double t) { engine.seekSeconds (t); updateLoopRegion(); };
    timeline.onTempoBandClicked = [this] (int index, double t) { tempoBandMenu (index, t); };
    timeline.onBeatClicked = [this] (int index) { beatMenu (index); };
    timeline.onNoteClicked = [this] (int index, double t) { noteMenu (index, t); };
    timeline.onChordClicked = [this] (int index, double t) { chordMenu (index, t); };
    timeline.onLaneMenu = [this] (double t, int lane) { clipMenu (t, lane); };
    timeline.onTrigger = [this] (int track, bool on) { setTriggerEnabled (track, on); };
    timeline.onLaneHeaderMenu = [this] (int track) { triggerMenu (track); };
    // Pistas MIDI: las ediciones llegan en tiempo de reproducción; los golpes se guardan en el de la canción
    timeline.onMidiAdd = [this] (int m, double seconds, int note)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (m, (int) info->midiTracks.size()))
            return;
        pushUndo();
        const int i = miditrack::addHit (info->midiTracks[(size_t) m], timeMap.toOriginal (seconds), note, miditrack::defaultVelocity);
        midiEdited (m, { i });
    };
    timeline.onMidiMove = [this] (int m, std::vector<int> hits, double delta, int rowDelta)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (m, (int) info->midiTracks.size()) || hits.empty())
            return;
        auto& mt = info->midiTracks[(size_t) m];
        double songDelta = delta;
        if (juce::isPositiveAndBelow (hits.front(), (int) mt.hits.size()))
        {
            const double o = mt.hits[(size_t) hits.front()].seconds;
            songDelta = timeMap.toOriginal (timeMap.toPlayback (o) + delta) - o;
        }
        pushUndo();
        midiEdited (m, miditrack::moveHits (mt, hits, songDelta, rowDelta));
    };
    timeline.onMidiVelocity = [this] (int m, std::vector<int> hits, int delta)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (m, (int) info->midiTracks.size()))
            return;
        pushUndo();
        miditrack::changeVelocity (info->midiTracks[(size_t) m], hits, delta);
        midiEdited (m, hits);
    };
    timeline.onMidiDelete = [this] (int m, std::vector<int> hits)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (m, (int) info->midiTracks.size()))
            return;
        pushUndo();
        miditrack::removeHits (info->midiTracks[(size_t) m], hits);
        midiEdited (m, {});
    };
    timeline.onMidiMenu = [this] (int m, std::vector<int> hits, int row, double seconds) { midiHitsMenu (m, hits, row, seconds); };
    timeline.onMidiPadMenu = [this] (int m, int row) { midiPadMenu (m, row); };
    timeline.onMidiAudition = [this] (int m, int row) { auditionMidiPad (m, row); };
    timeline.onMidiHeaderMenu = [this] (int m) { midiHeaderMenu (m); };
    timeline.onMidiMute = [this] (int m, bool muted)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (m, (int) info->midiTracks.size()))
            return;
        info->midiTracks[(size_t) m].muted = muted;
        midiEdited (m, timeline.getSelectedMidiHits (m));
    };
    timeline.snapMidiTime = [this] (int m, double seconds, int note) { return snapMidiTime (m, seconds, note); };
    timeline.levelGainAt = [this] (int stem, double t) { return levelGainAt (stem, t); };
    mixer.onLevelEdited = [this] (int stem, double db) { stemLevelEdited (stem, db); };
    mixer.onTriggerClicked = [this] (int track) { triggerMenu (track); };
    mixer.onInstrumentClicked = [this] (int id) { if (rack != nullptr) rack->openEditor (id); };
    mixer.onClickChanged = [this] (bool on, float db, int pair)
    {
        if (auto* s = currentInfo())
        {
            s->clickEnabled = on;
            s->clickGainDb = db;
            s->clickOutputPair = juce::jmax (0, pair);
            pushClick();
            clickControlsFromSong();   // la fila del click muestra lo mismo
        }
    };
    timeline.onClipDragged = [this] (double t, double delta, int lane) { clipDragged (t, delta, lane); };
    timeline.onMarkerClicked = [this] (int i, bool popup) { if (popup) markerMenu (i); else jumpToMarker (i); };
    timeline.onMarkerMoved = [this] (int i, double t)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (i, (int) info->markers.size()))
            return;
        info->markers[(size_t) i].seconds = timeMap.toOriginal (t);
        info->sortMarkers();
        library.saveSong (*info);
        rebuildMarkers();
        updateLoopRegion();
        remeasureIfLeveling();
    };
    timeline.onMute = [this] (int track, bool state)
    {
        if (currentSong != nullptr && juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
        {
            currentSong->tracks[(size_t) track]->muted = state;
            markSongDirty();
        }
    };
    timeline.onSolo = [this] (int track, bool state)
    {
        if (currentSong != nullptr && juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
            currentSong->tracks[(size_t) track]->solo = state;
    };
    addAndMakeVisible (timeline);

    // --- Mezclador ---
    mixer.fillOutputBox = [this] (juce::ComboBox& box, int pair) { fillOutputBox (box, pair); };
    mixer.onChanged = [this]
    {
        markSongDirty();
        refreshReplaced();   // silenciar la única fila que suena devuelve la pista de origen
    };
    mixer.onMasterGainChanged = [this] (float db)
    {
        engine.setMasterGain (juce::Decibels::decibelsToGain (db, -60.0f));
        if (auto* info = currentInfo())
        {
            info->masterGainDb = db;
            markSongDirty();
        }
    };
    addAndMakeVisible (mixer);

    helpLabel.setFont (font (12.0f));
    helpLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    helpLabel.setText (tr ("Espacio: play/pausa · Esc: stop · Flechas o RePág/AvPág: canción · 1-9: marcador · M: añadir marcador · "
                           "L: loop · F11: en vivo · Ctrl+rueda: zoom · Arrastra archivos (1 = separar, varios = stems)"),
                       juce::dontSendNotification);
    addAndMakeVisible (helpLabel);

    disableFocus (*this);
    setWantsKeyboardFocus (true);
    setOpaque (true);   // paint() cubre todo el fondo

    setSize (1360, 860);
    refreshSetlist();
    clickControlsFromSong();
    startTimerHz (30);

    if (! library.songs.empty())
        setlist.selectRow (0);

    if (audioError.isNotEmpty())
    {
        // Sin dispositivo de audio la app arranca muda: avisar en cuanto exista la ventana
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe, audioError]
        {
            if (safe != nullptr)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        tr ("No se pudo abrir el dispositivo de audio"),
                                                        audioError + tr ("\n\nRevisa la configuración en el botón «Audio»."));
        });
    }
}

MainComponent::~MainComponent()
{
    if (recording.active)
        stopRecording (true);   // sin hilos de grabación vivos al salir
    if (rack != nullptr)
    {
        rack->closeEditors();
        rack->save();
    }
    ++mixGeneration;
    mixAnalyzer.cancel();
    mixPool.removeAllJobs (true, 10000);
    if (mixProject != nullptr)
        mixProject->save();
    ++loadGeneration;
    abortJobs = true;
    levelPool.removeAllJobs (true, 10000);
    loaderPool.removeAllJobs (true, 10000);
    stopTimer();
    saveCurrentMix();
    separator.cancel();
    analyzer.cancel();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
    mixer.setInstruments (nullptr);
    rack.reset();   // ya sin callback de audio: los plugins mueren aquí
    if (mixEditor != nullptr)
        removeChildComponent (mixEditor.get());
    mixEditor.reset();
    mixPreviewSong.reset();
    clearPendingMixSong();
    if (auto xml = deviceManager.createStateXml())
        props.getUserSettings()->setValue ("audioDevice", xml.get());
    props.saveIfNeeded();
    setlist.setModel (nullptr);
    mixer.setSong (nullptr);
    timeline.setSong (nullptr, 44100.0);
    markerButtons.clear();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15191d));

    if (library.songs.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (font (16.0f));
        g.drawFittedText (tr ("Tu setlist está vacío.\n\nUsa «Importar stems» si ya tienes las pistas separadas,\n"
                              "o «Separar canción (IA)» para separar una canción completa."),
                          timeline.getBounds(), juce::Justification::centred, 5);
    }
}

void MainComponent::resized()
{
    if (picker != nullptr)
        picker->setBounds (getLocalBounds());
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (34);
    importBtn.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    separateBtn.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (8);
    mixBtn.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (8);
    stemsBox.setBounds (top.removeFromLeft (185));
    top.removeFromLeft (8);
    qualityBox.setBounds (top.removeFromLeft (175));
    top.removeFromLeft (8);
    drumPartsBtn.setBounds (top.removeFromLeft (150));
    aiBtn.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    audioBtn.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    instrumentsBtn.setBounds (top.removeFromRight (110));

    r.removeFromTop (6);
    auto sepRow = r.removeFromTop (22);
    sepBar.setBounds (sepRow.removeFromLeft (260));
    cancelSepBtn.setBounds (sepRow.removeFromRight (90));
    sepRow.removeFromLeft (8);
    sepLabel.setBounds (sepRow);

    helpLabel.setBounds (r.removeFromBottom (20));
    r.removeFromTop (6);

    auto left = r.removeFromLeft (270);
    auto setlistHeader = left.removeFromTop (24);
    levelSetlistBtn.setBounds (setlistHeader.removeFromRight (120).reduced (0, 1));
    setlistTitle.setBounds (setlistHeader);
    setlist.setBounds (left);
    r.removeFromLeft (12);

    // Armar mix: la sección ocupa el área de la canción (todo lo de la derecha) y queda encima
    if (mixEditor != nullptr)
    {
        mixEditor->setBounds (r);
        mixEditor->toFront (false);
        if (picker != nullptr)
            picker->toFront (false);
    }

    auto titleRow = r.removeFromTop (liveMode ? 60 : 40);
    timeLabel.setBounds (titleRow.removeFromRight (liveMode ? 300 : 220));
    keyLabel.setBounds (titleRow.removeFromRight (220));
    songTitle.setBounds (titleRow);
    auto infoRow = r.removeFromTop (liveMode ? 56 : 30);
    nextLabel.setBounds (infoRow.removeFromRight (300));
    chordLabel.setBounds (infoRow.removeFromRight (liveMode ? 360 : 240));
    sectionLabel.setBounds (infoRow);
    r.removeFromTop (6);

    auto transport = r.removeFromTop (48);
    prevBtn.setBounds (transport.removeFromLeft (70));
    transport.removeFromLeft (6);
    playBtn.setBounds (transport.removeFromLeft (140));
    transport.removeFromLeft (6);
    stopBtn.setBounds (transport.removeFromLeft (110));
    transport.removeFromLeft (6);
    nextBtn.setBounds (transport.removeFromLeft (70));
    transport.removeFromLeft (6);
    recordBtn.setBounds (transport.removeFromLeft (96));
    transport.removeFromLeft (16);
    loopBtn.setBounds (transport.removeFromLeft (170));
    liveBtn.setBounds (transport.removeFromRight (100));
    transport.removeFromRight (6);
    stageBtn.setBounds (transport.removeFromRight (90));
    transport.removeFromRight (6);
    zoomFitBtn.setBounds (transport.removeFromRight (80));

    r.removeFromTop (8);
    positionSlider.setVisible (liveMode);   // en edición la regla de la vista de arreglo la reemplaza
    if (liveMode)
    {
        positionSlider.setBounds (r.removeFromTop (26));
        r.removeFromTop (8);
    }

    // Marcadores: fluyen en varias filas (hasta 4) en vez de salirse de la ventana
    constexpr int markerH = 30, markerGap = 4, maxMarkerRows = 4;
    auto markerRow = r.removeFromTop (markerH);
    addMarkerBtn.setBounds (markerRow.removeFromLeft (110));
    markerRow.removeFromLeft (8);
    addNoteBtn.setBounds (markerRow.removeFromLeft (80));
    markerRow.removeFromLeft (8);
    cutModeBox.setVisible (! liveMode);
    if (! liveMode)
    {
        cutModeBox.setBounds (markerRow.removeFromLeft (180));
        markerRow.removeFromLeft (8);
    }
    int x = markerRow.getX(), y = markerRow.getY(), rows = 1;
    for (auto* b : markerButtons)
    {
        const int w = juce::jmin (140, juce::jmax (60, b->getBestWidthForHeight (markerH) + 10));
        if (x + w > markerRow.getRight() && x > markerRow.getX() && rows < maxMarkerRows)
        {
            x = markerRow.getX();
            y += markerH + markerGap;
            ++rows;
        }
        b->setBounds (x, y, w, markerH);
        x += w + markerGap;
    }
    r.removeFromTop ((rows - 1) * (markerH + markerGap));

    r.removeFromTop (8);
    auto clickRow = r.removeFromTop (28);
    clickBtn.setBounds (clickRow.removeFromLeft (80));
    bpmLabel.setBounds (clickRow.removeFromLeft (40));
    bpmSlider.setBounds (clickRow.removeFromLeft (130));
    clickRow.removeFromLeft (10);
    offsetLabel.setBounds (clickRow.removeFromLeft (70));
    offsetSlider.setBounds (clickRow.removeFromLeft (130));
    clickRow.removeFromLeft (10);
    clickVolLabel.setBounds (clickRow.removeFromLeft (30));
    clickGainSlider.setBounds (clickRow.removeFromLeft (120));
    clickRow.removeFromLeft (10);
    clickOutBox.setBounds (clickRow.removeFromLeft (120));
    clickRow.removeFromLeft (16);
    analyzeBtn.setBounds (clickRow.removeFromLeft (120));
    clickRow.removeFromLeft (16);
    levelBtn.setBounds (clickRow.removeFromLeft (80));
    targetBox.setBounds (clickRow.removeFromLeft (112));

    r.removeFromTop (6);
    auto tempoRow = r.removeFromTop (28);
    tempoLabel.setBounds (tempoRow.removeFromLeft (55));
    tempoSlider.setBounds (tempoRow.removeFromLeft (130));
    tempoRow.removeFromLeft (6);
    tempoOrigBtn.setBounds (tempoRow.removeFromLeft (80));
    tempoRow.removeFromLeft (16);
    transposeLabel.setBounds (tempoRow.removeFromLeft (120));
    transposeSlider.setBounds (tempoRow.removeFromLeft (110));
    tempoRow.removeFromLeft (12);
    tempoInfoLabel.setBounds (tempoRow);

    r.removeFromTop (10);
    // En vivo: solo el mezclador; en edición: vista de arreglo arriba y mezclador abajo
    timeline.setVisible (! liveMode);
    zoomFitBtn.setVisible (! liveMode);
    if (liveMode)
    {
        mixer.setBounds (r);
        return;
    }
    const int mixerH = juce::jlimit (190, 230, r.getHeight() / 3);
    mixer.setBounds (r.removeFromBottom (mixerH));
    r.removeFromBottom (8);
    timeline.setBounds (r);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (picker != nullptr)
        return picker->keyPressed (key);   // el selector de archivos está abierto: Esc cancela, Enter acepta

    // Armando un mix: las teclas son de esa sección (Espacio escucha, Esc detiene, Ctrl+Z deshace)
    if (mixEditor != nullptr)
    {
        if (heldKeys.contains (code))
            return true;
        bool used = mixEditor->handleKey (key);
        if (! used && code == juce::KeyPress::escapeKey)
        {
            stopMixPreview();
            used = true;
        }
        if (! used && key.getModifiers().isCtrlDown() && (code == 'Z' || code == 'z'))
        {
            undoMixEdit();
            used = true;
        }
        if (used)
            heldKeys.addIfNotAlreadyThere (code);
        return used;
    }

    // Autorrepetición: una tecla (o pedal) mantenida no debe repetir la acción, por ejemplo
    // saltar varias canciones o alternar play/pausa varias veces.
    if (heldKeys.contains (code))
        return true;

    bool handled = true;
    if (code == juce::KeyPress::spaceKey)                                          engine.togglePlay();
    else if (code == juce::KeyPress::escapeKey)                                    { engine.stop(); updateLoopRegion(); }
    else if (code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey) nextSong (1);
    else if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::pageUpKey)   nextSong (-1);
    else if (code >= '1' && code <= '9')                                           jumpToMarker (code - '1');
    else if (code >= juce::KeyPress::numberPad1 && code <= juce::KeyPress::numberPad9)
        jumpToMarker (code - juce::KeyPress::numberPad1);
    else if (code == juce::KeyPress::F11Key)
        setLiveMode (! liveMode);
    else if (key.getModifiers().isCtrlDown() && (code == 'Z' || code == 'z'))
        undoLastEdit();
    else if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && timeline.deleteSelectedMidiHits())
        {}
    else if (code == juce::KeyPress::F12Key)
        showStage (stageWindow == nullptr || ! stageWindow->isVisible());
    else
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (c == 'm')      addMarkerHere();
        else if (c == 'n') addNoteHere();
        else if (c == 'l') { loopBtn.setToggleState (! loopBtn.getToggleState(), juce::dontSendNotification); updateLoopRegion(); }
        else               handled = false;
    }

    if (handled)
        heldKeys.addIfNotAlreadyThere (code);
    return handled;
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    // JUCE filtra las liberaciones sintéticas de la autorrepetición en X11, así que una
    // liberación real significa que la tecla ya se soltó.
    if (! isKeyDown)
        heldKeys.clear();
    return false;
}

void MainComponent::focusLost (FocusChangeType)
{
    heldKeys.clear();   // la liberación llegará a otra ventana: no dejar teclas "pegadas"
}

//==============================================================================
int MainComponent::getNumRows() { return (int) library.songs.size(); }

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) library.songs.size()))
        return;

    if (selected)
        g.fillAll (accent.withAlpha (0.25f));
    if (row == currentIndex)
    {
        g.setColour (accent);
        g.fillRect (0, 0, 4, h);
    }

    g.setColour (juce::Colours::grey);
    g.setFont (font (14.0f));
    g.drawText (juce::String (row + 1), 10, 0, 26, h, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (font (16.0f, row == currentIndex));
    g.drawText (library.songs[(size_t) row].name, 38, 0, w - 44, h, juce::Justification::centredLeft, true);
}

void MainComponent::selectedRowsChanged (int lastRow)
{
    // El clic derecho también selecciona la fila; no debe cargar la canción (solo abre el menú)
    if (juce::ModifierKeys::currentModifiers.isPopupMenu())
        return;
    if (lastRow >= 0 && lastRow != currentIndex)
        loadSongAt (lastRow);
}

void MainComponent::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        songMenu (row);
}

void MainComponent::refreshSetlist()
{
    setlist.updateContent();
    setlist.repaint();

    const int next = currentIndex + 1;
    nextLabel.setText (juce::isPositiveAndBelow (next, (int) library.songs.size())
                           ? tr ("Siguiente: ") + library.songs[(size_t) next].name
                           : juce::String(),
                       juce::dontSendNotification);
    repaint();
}

void MainComponent::restoreSetlistSelection()
{
    // La fila seleccionada vuelve a ser la canción cargada (sin disparar una carga)
    setlist.deselectAllRows();
    if (currentIndex >= 0)
        setlist.selectRow (currentIndex, true, true);
}

void MainComponent::songMenu (int row)
{
    juce::PopupMenu m;
    m.addItem (1, "Subir", row > 0);
    m.addItem (2, "Bajar", row < (int) library.songs.size() - 1);
    m.addItem (3, "Renombrar...");
    m.addSeparator();
    m.addItem (5, tr ("Exportar canción (carpeta con stems y ajustes)..."));
    m.addItem (6, tr ("Exportar todo el setlist..."));
    m.addSeparator();
    m.addItem (4, "Mover a la papelera...");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, row] (int result)
    {
        restoreSetlistSelection();

        if (result == 5 || result == 6)
        {
            const bool all = result == 6;
            pickFiles (all ? tr ("Carpeta donde exportar todo el setlist") : tr ("Carpeta donde exportar la canción"),
                       juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                       juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*",
                       [this, row, all] (const juce::Array<juce::File>& files)
            {
                const auto dest = files.size() > 0 ? files[0] : juce::File();
                if (! dest.isDirectory())
                    return;
                saveCurrentMix();
                const bool ok = all ? library.exportAll (dest) : library.exportSong (row, dest);
                juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                        tr ("Exportar"),
                                                        ok ? tr ("Listo. En el otro equipo, usa «Importar stems» y elige la carpeta de la canción (o copia todo dentro de su carpeta Secuencias).\n\n") + dest.getFullPathName()
                                                           : tr ("No se pudo copiar a ") + dest.getFullPathName());
            });
            return;
        }
        if (result == 1 || result == 2)
        {
            const int delta = result == 1 ? -1 : 1;
            library.move (row, delta);
            if (currentIndex == row)              currentIndex = row + delta;
            else if (currentIndex == row + delta) currentIndex = row;
            restoreSetlistSelection();
            refreshSetlist();
        }
        else if (result == 3)
        {
            askText (tr ("Nombre de la canción"), library.songs[(size_t) row].name, [this, row] (const juce::String& name)
            {
                if (! juce::isPositiveAndBelow (row, (int) library.songs.size()) || name.trim().isEmpty())
                    return;
                auto& s = library.songs[(size_t) row];
                s.name = name.trim();
                library.saveSong (s);
                if (row == currentIndex)
                    songTitle.setText (s.name, juce::dontSendNotification);
                refreshSetlist();
            });
        }
        else if (result == 4)
        {
            const auto name = library.songs[(size_t) row].name;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("¿Eliminar canción?"),
                tr ("«") + name + tr ("» y sus stems se moverán a la papelera."), "Eliminar", "Cancelar", this,
                juce::ModalCallbackFunction::create ([this, row] (int ok)
                {
                    if (ok == 0 || ! juce::isPositiveAndBelow (row, (int) library.songs.size()))
                        return;
                    // Primero la papelera: si falla, no cambia nada (la canción sigue cargada)
                    const bool wasCurrent = row == currentIndex;
                    if (! library.moveToTrash (row))
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Eliminar",
                            tr ("No se pudo mover la carpeta a la papelera:\n")
                            + library.songs[(size_t) row].folder.getFullPathName());
                        restoreSetlistSelection();
                        return;
                    }
                    if (wasCurrent)
                        unloadSong();
                    else if (currentIndex > row)
                        --currentIndex;
                    restoreSetlistSelection();
                    refreshSetlist();
                }));
        }
    });
}

//==============================================================================
SongInfo* MainComponent::currentInfo()
{
    return juce::isPositiveAndBelow (currentIndex, (int) library.songs.size())
               ? &library.songs[(size_t) currentIndex] : nullptr;
}

void MainComponent::afterFadeOut (std::function<void()> action, int waitedMs)
{
    // Tras engine.pause(), espera a que el fundido de salida llegue a 0 (unos 10 ms) antes de
    // actuar. Si el dispositivo no está corriendo, el fundido nunca avanza: sigue igual pasado
    // un tiempo prudente.
    if (engine.isSilent() || waitedMs >= 100)
    {
        action();
        return;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::Timer::callAfterDelay (5, [safe, action = std::move (action), waitedMs]
    {
        if (safe != nullptr)
            safe->afterFadeOut (action, waitedMs + 5);
    });
}

void MainComponent::unloadSong()
{
    const int gen = ++loadGeneration;
    currentIndex = -1;
    songDirtySince = 0;
    songTitle.setText ({}, juce::dontSendNotification);
    sectionLabel.setText ({}, juce::dontSendNotification);
    rebuildMarkers();
    clickControlsFromSong();
    refreshSetlist();

    engine.pause();
    afterFadeOut ([this, gen]
    {
        if (gen != loadGeneration.load())
            return;
        engine.setSong (nullptr);
        engine.setSongGain (1.0f);
        currentSong.reset();
        sourceSong.reset();
        arrangedSong.reset();
        undoStack.clear();
        timeMap = TimeMap();
        loadedFolder = juce::File();
        mixer.setSong (nullptr);
        timeline.setLoadingText (tr ("Sin canción cargada"));
        timeline.setSong (nullptr, engine.getSampleRate());
        timeline.setAnalysis ({});
        showChordAndKey (0.0);
        resized();
    });
}

void MainComponent::loadSongAt (int index)
{
    if (mixEditor != nullptr)
        closeMix (false);        // elegir una canción del setlist cierra el mix (queda guardado)
    if (recording.active)
        stopRecording (false);   // una toma en curso se cierra (y se guarda) antes de cambiar de canción
    if (! juce::isPositiveAndBelow (index, (int) library.songs.size()))
        return;

    saveCurrentMix();
    const int gen = ++loadGeneration;
    currentIndex = index;
    const auto info = library.songs[(size_t) index];
    songTitle.setText (info.name, juce::dontSendNotification);
    sectionLabel.setText ("Cargando...", juce::dontSendNotification);
    rebuildMarkers();
    clickControlsFromSong();
    refreshSetlist();

    // Primero se apaga con fundido lo que esté sonando (nunca un corte seco); recién entonces
    // se descarga la canción anterior y se lee la nueva en el hilo de carga.
    engine.pause();
    afterFadeOut ([this, info, gen]
    {
        if (gen != loadGeneration.load())
            return;   // ya se pidió otra canción

        engine.setSong (nullptr);
        currentSong.reset();
        sourceSong.reset();
        arrangedSong.reset();
        undoStack.clear();
        timeMap = TimeMap();
        loadedFolder = juce::File();
        mixer.setSong (nullptr);
        timeline.setLoadingText ("Cargando...");
        timeline.setSong (nullptr, engine.getSampleRate());
        timeline.setAnalysis ({});
        showChordAndKey (0.0);
        resized();

        const double sr = engine.getSampleRate();
        const auto banksDir = library.banksFolder();
        juce::Component::SafePointer<MainComponent> safe (this);

        loaderPool.addJob ([this, safe, info, sr, gen, banksDir]
        {
            std::shared_ptr<LoadedSong> song, source, arranged;
            std::shared_ptr<SamplerSet> samplerSet;
            juce::String error;
            try
            {
                auto abort = [this, gen] { return gen != loadGeneration.load(); };
                source = AudioEngine::loadSong (info, sr, formatManager, abort);
                song = source;
                if (source != nullptr && ! source->tracks.empty())
                {
                    // Arreglo (tramos cortados o movidos) y, si hay tempo o tono guardados, el estirado: la
                    // versión que suena se renderiza antes de mostrarla
                    arranged = arrangement::render (source, info.clips, sr, abort);
                    song = arranged;
                    if (arranged != nullptr)
                    {
                        const auto map = TimeMap::build (info, (double) arranged->length / sr);
                        if (! map.isPlain())
                        {
                            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sectionLabel.setText (tr ("Renderizando tempo y tono..."), juce::dontSendNotification); });
                            song = stretcher::render (arranged, map, sr, abort);
                        }
                        if (song != nullptr)
                            samplerSet = buildSamplers (*song, info, sr, banksDir, formatManager, bankCache);
                    }
                }
            }
            catch (const std::bad_alloc&)
            {
                // Sin esto el job muere en silencio y la UI se queda en "Cargando..." para siempre
                error = tr ("No hay memoria suficiente para cargar la canción");
            }
            catch (const std::exception& e)
            {
                error = tr ("Error al cargar: ") + juce::String (e.what());
            }
            juce::MessageManager::callAsync ([safe, song, source, arranged, samplerSet, gen, error]
            {
                if (auto* self = safe.getComponent())
                    self->songLoaded (song, source, arranged, samplerSet, gen, error);
            });
        });
    });
}

void MainComponent::songLoaded (std::shared_ptr<LoadedSong> song, std::shared_ptr<LoadedSong> source, std::shared_ptr<LoadedSong> arranged,
                                std::shared_ptr<SamplerSet> samplerSet, int generation, const juce::String& error)
{
    if (generation != loadGeneration.load())
        return;

    if (song == nullptr || song->tracks.empty())
    {
        const auto msg = error.isNotEmpty() ? error : tr ("No se pudo cargar el audio");
        sectionLabel.setText (msg, juce::dontSendNotification);
        timeline.setLoadingText (msg);
        return;
    }

    currentSong = song;
    sourceSong = source;
    arrangedSong = arranged != nullptr ? arranged : source;
    auto* info = currentInfo();
    renderedClips = info != nullptr ? info->clips : std::vector<Clip>();
    renderedTriggers = info != nullptr ? triggersOf (*info) : std::vector<TriggerSettings>();
    undoStack.clear();
    loadedFolder = info != nullptr ? info->folder : juce::File();
    renderedTranspose = info != nullptr ? info->transpose : 0;
    rendering = false;
    tempoDirtySince = 0;
    rebuildTimeMap();
    engine.setSong (song);
    applyClick();
    const float masterDb = info != nullptr ? info->masterGainDb : 0.0f;
    engine.setMasterGain (juce::Decibels::decibelsToGain (masterDb, -60.0f));
    engine.setSongGain (juce::Decibels::decibelsToGain ((float) (info != nullptr ? info->songGainDb : 0.0), -60.0f));
    levelBtn.setToggleState (info != nullptr && info->levelingEnabled, juce::dontSendNotification);
    updateGainCurve();
    mixer.setMasterGainDb (masterDb);
    mixer.setSong (currentSong);
    timeline.setSong (currentSong, engine.getSampleRate());
    applySamplers (samplerSet);
    syncTimelineMarkers();
    timeline.setAnalysis (mappedAnalysis());
    updateBeatGrid();
    tempoControlsFromSong();
    showChordAndKey (0.0);
    updateLoopRegion();
    // Nivelado activo con tramos sin medir (marcadores añadidos con otra versión o sin medir): medir ahora
    if (info != nullptr && info->levelingEnabled && needsLevelMeasure (*info))
        measureAndLevel();
}

void MainComponent::saveCurrentMix()
{
    songDirtySince = 0;
    auto* info = currentInfo();
    if (info == nullptr)
        return;

    // Durante un cambio de canción la cargada y la seleccionada no coinciden: no mezclar estados
    if (currentSong != nullptr && loadedFolder == info->folder)
    {
        for (auto& t : currentSong->tracks)
        {
            if (! juce::isPositiveAndBelow (t->stemIndex, (int) info->stems.size()))
                continue;
            auto& st = info->stems[(size_t) t->stemIndex];
            const float g = t->gain.load();
            st.gainDb = g <= 0.0f ? -60.0f : juce::Decibels::gainToDecibels (g, -60.0f);
            st.muted = t->muted.load();
            st.outputPair = t->outputPair.load();
        }
        pullMidiControls();
        if (samplers != nullptr)
            for (auto& lane : samplers->lanes)
                if (juce::isPositiveAndBelow (lane->stemIndex, (int) info->stems.size()))
                {
                    auto& tg = info->stems[(size_t) lane->stemIndex].trigger;
                    const float g = lane->control.gain.load();
                    tg.gainDb = g <= 0.0f ? -60.0f : juce::Decibels::gainToDecibels (g, -60.0f);
                    tg.muted = lane->control.muted.load();
                    tg.outputPair = lane->control.outputPair.load();
                }
    }
    library.saveSong (*info);
}

void MainComponent::markSongDirty()
{
    // Se guarda desde el Timer medio segundo después del último cambio (no en cada paso de un fader)
    songDirtySince = juce::Time::currentTimeMillis();
}

void MainComponent::nextSong (int delta)
{
    if (library.songs.empty())
        return;
    const int idx = juce::jlimit (0, (int) library.songs.size() - 1, currentIndex + delta);
    if (idx >= 0 && idx != currentIndex)
        setlist.selectRow (idx);
}

void MainComponent::requestQuit()
{
    if (! engine.isPlaying())
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
        return;
    }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("¿Salir de Secuencias?"),
        tr ("Hay una canción sonando."), "Salir", "Cancelar", this,
        juce::ModalCallbackFunction::create ([] (int ok)
        {
            if (ok != 0)
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }));
}

//==============================================================================
int MainComponent::numOutputPairs() const
{
    return juce::jmax (1, (engine.getNumOutputChannels() + 1) / 2);
}

void MainComponent::fillOutputBox (juce::ComboBox& box, int selectedPair)
{
    box.clear (juce::dontSendNotification);
    const int pairs = numOutputPairs();
    for (int p = 0; p < pairs; ++p)
        box.addItem ("Salida " + juce::String (p * 2 + 1) + "-" + juce::String (p * 2 + 2), p + 1);
    box.setSelectedId (juce::jlimit (0, pairs - 1, selectedPair) + 1, juce::dontSendNotification);
}

void MainComponent::syncTimelineMarkers()
{
    if (auto* info = currentInfo())
    {
        timeline.setMarkers (mappedMarkers(), info->bpm, timeMap.toPlayback (info->clickOffset));
        timeline.setLevelingEnabled (info->levelingEnabled);
        timeline.setTempoBands (mappedTempoBands());
        timeline.setClips (mappedClips());
        std::vector<TimelineView::NoteView> notes;
        for (auto& n : info->notes)
            notes.push_back ({ timeMap.toPlayback (n.seconds), n.text });
        timeline.setNotes (notes);
        syncTriggerMarks();
    }
    else
    {
        timeline.setMarkers ({}, 120.0, 0.0);
        timeline.setTempoBands ({});
        timeline.setClips ({});
        timeline.setNotes ({});
    }
}

//==============================================================================
void MainComponent::rebuildTimeMap()
{
    auto* info = currentInfo();
    timeMap = (info != nullptr && arrangedSong != nullptr)
                  ? TimeMap::build (*info, songLengthSeconds())
                  : TimeMap();
}

std::vector<SongMarker> MainComponent::mappedMarkers() const
{
    std::vector<SongMarker> out;
    if (juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        for (auto m : library.songs[(size_t) currentIndex].markers)
        {
            m.seconds = timeMap.toPlayback (m.seconds);
            out.push_back (m);
        }
    return out;
}

Analysis MainComponent::mappedAnalysis() const
{
    Analysis a;
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return a;
    a = library.songs[(size_t) currentIndex].analysis;
    const bool flats = useFlats();
    a.key = music::spellKey (effectiveKey(), flats);
    for (auto& b : a.beats)
        b.seconds = timeMap.toPlayback (b.seconds);
    for (auto& c : a.chords)
    {
        c.start = timeMap.toPlayback (c.start);
        c.end = timeMap.toPlayback (c.end);
        c.name = music::spellChord (c.name, flats);
    }
    if (! timeMap.isIdentity())
        a.bpm = a.bpmBetween (0.0, 1.0e9);   // tempo medio de lo que suena
    return a;
}

std::vector<TimelineView::TempoBand> MainComponent::mappedTempoBands() const
{
    std::vector<TimelineView::TempoBand> out;
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()) || arrangedSong == nullptr)
        return out;
    const auto& info = library.songs[(size_t) currentIndex];
    const double length = songLengthSeconds();
    auto regions = info.tempoRegions;
    if (regions.empty())
        regions.push_back ({ 0.0, originalBpm(), 0.0 });
    for (size_t i = 0; i < regions.size(); ++i)
    {
        TimelineView::TempoBand b;
        b.start = timeMap.toPlayback (regions[i].start);
        b.end = timeMap.toPlayback (i + 1 < regions.size() ? juce::jmin (length, regions[i + 1].start) : length);
        b.origBpm = regions[i].origBpm;
        b.playBpm = effectivePlayBpm (info, regions[i]);
        b.transpose = effectiveTranspose (info, regions[i]);
        out.push_back (b);
    }
    return out;
}

void MainComponent::tempoAt (double originalSeconds, double& origBpm, double& playBpm) const
{
    origBpm = playBpm = originalBpm();
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return;
    const auto& info = library.songs[(size_t) currentIndex];
    const int i = info.tempoRegionAt (originalSeconds);
    if (i >= 0)
    {
        origBpm = info.tempoRegions[(size_t) i].origBpm;
        playBpm = effectivePlayBpm (info, info.tempoRegions[(size_t) i]);
    }
    else if (info.playBpm > 0.0)
        playBpm = info.playBpm;
}

double MainComponent::originalBpm() const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return 120.0;
    const auto& info = library.songs[(size_t) currentIndex];
    return info.analysis.bpm > 0.0 ? info.analysis.bpm : info.bpm;
}

void MainComponent::tempoControlsFromSong()
{
    auto* info = currentInfo();
    const bool has = info != nullptr && currentSong != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &tempoSlider, &tempoOrigBtn, &transposeSlider })
        c->setEnabled (has && ! rendering);
    if (! has)
    {
        tempoInfoLabel.setText ({}, juce::dontSendNotification);
        return;
    }
    const double orig = originalBpm();
    tempoSlider.setValue (info->playBpm > 0.0 ? info->playBpm : orig, juce::dontSendNotification);
    transposeSlider.setValue (info->transpose, juce::dontSendNotification);
    juce::String text;
    if (info->tempoRegions.size() > 1)
    {
        text = juce::String ((int) info->tempoRegions.size()) + tr (" secciones: ");
        for (size_t i = 0; i < info->tempoRegions.size() && i < 4; ++i)
            text += (i > 0 ? " / " : "") + juce::String (info->tempoRegions[i].origBpm, 1);
        text += (info->tempoRegions.size() > 4 ? " / ... BPM" : " BPM");
    }
    else
        text = juce::String::formatted ("Original %.1f BPM", orig);
    if (! timeMap.isIdentity())
        text += "  |  " + formatTime (songLengthSeconds()) + " -> " + formatTime (timeMap.playbackLength());
    bool perSection = false;
    for (auto& r : info->tempoRegions)
        perSection = perSection || r.playBpm > 0.0;
    if (perSection)
        text += tr ("  |  secciones con tempo propio");
    if (renderedTranspose != 0)
        text += juce::String::formatted ("  |  %+d st", renderedTranspose);
    bool ownPitch = false;
    for (auto& r : info->tempoRegions)
        ownPitch = ownPitch || r.transpose != TempoRegion::followSong;
    if (ownPitch)
        text += tr ("  |  secciones con tono propio");
    tempoInfoLabel.setText (text, juce::dontSendNotification);
}

void MainComponent::requestRender()
{
    tempoDirtySince = juce::Time::currentTimeMillis();
    markSongDirty();
}

static bool sameStretch (const TimeMap& a, const TimeMap& b)
{
    if (a.segments().size() != b.segments().size())
        return false;
    for (size_t i = 0; i < a.segments().size(); ++i)
    {
        const auto& x = a.segments()[i];
        const auto& y = b.segments()[i];
        if (std::abs (x.origStart - y.origStart) > 1.0e-6 || std::abs (x.origEnd - y.origEnd) > 1.0e-6 || std::abs (x.ratio - y.ratio) > 1.0e-6
            || x.transpose != y.transpose)
            return false;
    }
    return true;
}

static bool sameClips (const std::vector<Clip>& a, const std::vector<Clip>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i].srcStart - b[i].srcStart) > 1.0e-9 || std::abs (a[i].srcEnd - b[i].srcEnd) > 1.0e-9
            || std::abs (a[i].position - b[i].position) > 1.0e-9)
            return false;
    return true;
}

void MainComponent::renderTempo()
{
    tempoDirtySince = 0;
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr || loadedFolder != info->folder || rendering)
        return;
    const double sr = engine.getSampleRate();
    const double sourceLength = (double) sourceSong->length / sr;
    const auto clips = info->clips;
    const bool clipsChanged = ! sameClips (clips, renderedClips);
    const auto newMap = TimeMap::build (*info, arrangement::lengthSeconds (clips, sourceLength));
    const bool stretchChanged = clipsChanged || ! sameStretch (newMap, timeMap) || info->transpose != renderedTranspose;
    const auto trig = triggersOf (*info);
    bool triggersChanged = trig.size() != renderedTriggers.size();
    for (size_t i = 0; ! triggersChanged && i < trig.size(); ++i)
    {
        const auto& a = trig[i];
        const auto& b = renderedTriggers[i];
        triggersChanged = a.enabled != b.enabled || a.sound != b.sound || a.note != b.note || std::abs (a.thresholdDb - b.thresholdDb) > 1.0e-9
                          || std::abs (a.sensitivity - b.sensitivity) > 1.0e-9 || std::abs (a.minMs - b.minMs) > 1.0e-9;
    }
    if (! stretchChanged && ! triggersChanged)
    {
        tempoControlsFromSong();
        return;
    }
    rendering = true;
    tempoControlsFromSong();
    const int gen = loadGeneration.load();
    const int rgen = ++renderGeneration;
    const int transpose = info->transpose;
    auto source = sourceSong;
    auto arrangedBefore = clipsChanged ? nullptr : arrangedSong;     // si el arreglo no cambió, se reutiliza
    auto renderedBefore = stretchChanged ? nullptr : currentSong;   // solo cambiaron los triggers: se reutiliza lo que suena
    const SongInfo infoCopy = *info;
    const auto banksDir = library.banksFolder();
    sepLabel.setText (! stretchChanged ? tr ("Detectando golpes...") : clipsChanged ? tr ("Renderizando el arreglo...") : tr ("Renderizando tempo y tono..."), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    loaderPool.addJob ([this, safe, source, arrangedBefore, renderedBefore, clips, newMap, transpose, sr, gen, rgen, infoCopy, banksDir]
    {
        auto abort = [this, gen, rgen] { return gen != loadGeneration.load() || rgen != renderGeneration || abortJobs.load(); };
        auto progress = [safe] (float p)
        {
            juce::MessageManager::callAsync ([safe, p]
            {
                if (safe != nullptr)
                    safe->sepLabel.setText (tr ("Renderizando tempo y tono... ") + juce::String ((int) (p * 100)) + "%", juce::dontSendNotification);
            });
        };
        std::shared_ptr<LoadedSong> arranged = arrangedBefore, rendered = renderedBefore;
        std::shared_ptr<SamplerSet> samplerSet;
        try
        {
            if (arranged == nullptr)
                arranged = arrangement::render (source, clips, sr, abort);
            if (arranged != nullptr && rendered == nullptr)
                rendered = stretcher::render (arranged, newMap, sr, abort, progress);
            if (rendered != nullptr)
                samplerSet = buildSamplers (*rendered, infoCopy, sr, banksDir, formatManager, bankCache);
        }
        catch (const std::exception&) {}
        juce::MessageManager::callAsync ([safe, rendered, arranged, newMap, transpose, clips, samplerSet, rgen]
        {
            if (auto* self = safe.getComponent())
                self->songRendered (rendered, arranged, newMap, transpose, clips, samplerSet, rgen);
        });
    });
}

void MainComponent::songRendered (std::shared_ptr<LoadedSong> rendered, std::shared_ptr<LoadedSong> arranged, TimeMap map,
                                  int transpose, std::vector<Clip> clips, std::shared_ptr<SamplerSet> samplerSet, int rgen)
{
    if (rgen != renderGeneration)
        return;
    rendering = false;
    if (rendered == nullptr || arranged == nullptr || sourceSong == nullptr)
    {
        sepLabel.setText (tr ("No se pudo renderizar la canción"), juce::dontSendNotification);
        tempoControlsFromSong();
        return;
    }
    // Conservar el punto de la canción donde íbamos y si estaba sonando
    const bool wasPlaying = engine.isPlaying();
    const double songPos = timeMap.toOriginal (engine.getPositionSeconds());
    timeMap = map;
    renderedTranspose = transpose;
    renderedClips = std::move (clips);
    if (auto* info = currentInfo())
        renderedTriggers = triggersOf (*info);
    arrangedSong = arranged;
    const bool sameSong = currentSong == rendered;
    currentSong = rendered;
    if (sameSong)
    {
        // Solo cambiaron los triggers: el audio sigue igual, no hace falta cortar ni recargar la vista
        applySamplers (samplerSet);
        tempoControlsFromSong();
        sepLabel.setText (tr ("Golpes detectados"), juce::dontSendNotification);
        return;
    }
    engine.pause();
    afterFadeOut ([this, wasPlaying, songPos, samplerSet]
    {
        engine.setSong (currentSong);
        mixer.setSong (currentSong);
        timeline.setSong (currentSong, engine.getSampleRate());
        applySamplers (samplerSet);
        syncTimelineMarkers();
        timeline.setAnalysis (mappedAnalysis());
        updateBeatGrid();
        updateGainCurve();
        engine.seekSeconds (timeMap.toPlayback (songPos));
        if (wasPlaying)
            engine.play();
        tempoControlsFromSong();
        updateLoopRegion();
        const bool plain = timeMap.isPlain();
        const bool cut = sourceSong != nullptr && ! arrangement::isIdentity (renderedClips, (double) sourceSong->length / engine.getSampleRate());
        sepLabel.setText (plain ? (cut ? tr ("Arreglo aplicado") : tr ("Tempo y tono originales")) : tr ("Tempo y tono aplicados"),
                          juce::dontSendNotification);
    });
}

void MainComponent::setLiveMode (bool live)
{
    liveMode = live;
    liveBtn.setToggleState (live, juce::dontSendNotification);
    songTitle.setFont (font (live ? 44.0f : 28.0f, true));
    sectionLabel.setFont (font (live ? 40.0f : 22.0f, true));
    timeLabel.setFont (font (live ? 36.0f : 22.0f));
    nextLabel.setFont (font (live ? 18.0f : 14.0f));
    chordLabel.setFont (font (live ? 48.0f : 22.0f, true));
    keyLabel.setFont (font (live ? 18.0f : 14.0f));
    resized();
}

void MainComponent::rebuildMarkers()
{
    updateGainCurve();
    markerButtons.clear();
    if (auto* info = currentInfo())
    {
        for (int i = 0; i < (int) info->markers.size(); ++i)
        {
            auto* b = markerButtons.add (new MarkerButton());
            const auto label = (i < 9 ? juce::String (i + 1) + ". " : juce::String()) + info->markers[(size_t) i].name;
            b->setButtonText (label);
            b->setColour (juce::TextButton::buttonColourId, panelAlt);
            b->onPress = [this, i] (bool popup) { if (popup) markerMenu (i); else jumpToMarker (i); };
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
    }
    syncTimelineMarkers();
    resized();
}

void MainComponent::sectionAt (double seconds, double& start, double& end, juce::String& name) const
{
    start = 0.0;
    end = engine.getLengthSeconds();
    name = {};
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return;

    for (auto& m : library.songs[(size_t) currentIndex].markers)
    {
        const double t = timeMap.toPlayback (m.seconds);
        if (t <= seconds + 0.001)
        {
            start = t;
            name = m.name;
        }
        else
        {
            end = t;
            break;
        }
    }
}

void MainComponent::updateLoopRegion()
{
    loopStartSec = loopEndSec = 0.0;
    if (loopBtn.getToggleState() && currentSong != nullptr)
    {
        double s, e;
        juce::String name;
        sectionAt (engine.getPositionSeconds(), s, e, name);
        if (e - s > 0.05)
        {
            loopStartSec = s;
            loopEndSec = e;
        }
    }
    if (loopEndSec > loopStartSec)
        engine.setLoop (loopStartSec, loopEndSec);
    else
        engine.clearLoop();
    timeline.setLoop (loopStartSec, loopEndSec);
}

void MainComponent::jumpToMarker (int index)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (index, (int) info->markers.size()))
        return;
    engine.seekSeconds (timeMap.toPlayback (info->markers[(size_t) index].seconds));
    updateLoopRegion();
}

void MainComponent::addMarkerHere()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    SongMarker mk;
    mk.name = tr ("Sección ") + juce::String ((int) info->markers.size() + 1);
    mk.seconds = timeMap.toOriginal (engine.getPositionSeconds());
    info->markers.push_back (mk);
    info->sortMarkers();
    library.saveSong (*info);
    rebuildMarkers();
    remeasureIfLeveling();
}

void MainComponent::activeNote (double pos, juce::String& now, juce::String& next, double& nextIn) const
{
    now = next = {};
    nextIn = -1.0;
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return;
    const auto& notes = library.songs[(size_t) currentIndex].notes;
    const double orig = timeMap.toOriginal (pos);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.seconds <= orig + 1.0e-6)
        {
            const double end = n.duration > 0.0 ? n.seconds + n.duration
                                                : (i + 1 < notes.size() ? notes[i + 1].seconds : 1.0e9);
            now = orig < end ? n.text : juce::String();
        }
        else
        {
            const double in = timeMap.toPlayback (n.seconds) - pos;
            if (in <= 12.0)   // aviso con hasta 12 s de anticipación
            {
                next = n.text;
                nextIn = juce::jmax (0.0, in);
            }
            break;
        }
    }
}

void MainComponent::addNoteHere()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    double t = timeMap.toOriginal (engine.getPositionSeconds());
    if (const int nb = info->analysis.nearestBeat (t, 0.25); nb >= 0)
        t = info->analysis.beats[(size_t) nb].seconds;   // al tiempo más cercano
    editNote (-1, t);
}

void MainComponent::editNote (int index, double songSeconds)
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    const bool editing = juce::isPositiveAndBelow (index, (int) info->notes.size());
    const auto initialText = editing ? info->notes[(size_t) index].text : juce::String();
    const double initialDuration = editing ? info->notes[(size_t) index].duration : 6.0;
    const double at = editing ? info->notes[(size_t) index].seconds : songSeconds;

    int bar = 0, beat = 0, beatsInBar = 4;
    double progress = 0.0;
    barAndBeat (timeMap.toPlayback (at), bar, beat, beatsInBar, progress);
    juce::String where = formatTime (timeMap.toPlayback (at));
    if (bar > 0)
        where += tr (" (compás ") + juce::String (bar) + ", " + tr ("tiempo ") + juce::String (beat) + ")";

    auto* w = new juce::AlertWindow (editing ? tr ("Editar nota en ") + where : tr ("Nota en ") + where,
                                     tr ("Se mostrará en la guía de escenario cuando llegue ese momento."), juce::MessageBoxIconType::NoIcon, this);
    auto editor = std::make_shared<juce::TextEditor>();
    editor->setMultiLine (true, true);
    editor->setReturnKeyStartsNewLine (true);
    editor->setText (initialText);
    editor->setSize (420, 90);
    w->addCustomComponent (editor.get());
    w->addTextEditor ("dur", juce::String (initialDuration, 1), tr ("Segundos visible (0 = hasta la siguiente nota)"));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::ctrlModifier, 0));
    if (editing)
        w->addButton ("Eliminar", 2);
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::MessageManager::callAsync ([editor] { editor->grabKeyboardFocus(); });
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, editor, index, at] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0)
            return;
        const bool exists = juce::isPositiveAndBelow (index, (int) inf->notes.size());
        pushUndo();
        if (result == 2)
        {
            if (exists)
                inf->notes.erase (inf->notes.begin() + index);
        }
        else
        {
            const auto text = editor->getText().trim();
            const double duration = juce::jlimit (0.0, 3600.0, w->getTextEditorContents ("dur").replace (",", ".").getDoubleValue());
            if (text.isEmpty())
            {
                if (exists)
                    inf->notes.erase (inf->notes.begin() + index);
            }
            else if (exists)
            {
                inf->notes[(size_t) index].text = text;
                inf->notes[(size_t) index].duration = duration;
            }
            else
                inf->notes.push_back ({ at, duration, text });
        }
        inf->sortNotes();
        library.saveSong (*inf);
        syncTimelineMarkers();
        updateStage (engine.getPositionSeconds());
    }), true);
}

void MainComponent::noteMenu (int index, double playbackSeconds)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    juce::PopupMenu m;
    if (juce::isPositiveAndBelow (index, (int) info->notes.size()))
    {
        m.addSectionHeader (info->notes[(size_t) index].text.upToFirstOccurrenceOf ("\n", false, false));
        m.addItem (1, tr ("Editar nota..."));
        m.addItem (2, tr ("Mover a la posición actual"));
        m.addItem (3, tr ("Eliminar nota"));
    }
    else
        m.addItem (4, tr ("Añadir nota en ") + formatTime (playbackSeconds) + "...");
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, index, playbackSeconds] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0)
            return;
        if (result == 4)
        {
            double t = timeMap.toOriginal (playbackSeconds);
            if (const int nb = inf->analysis.nearestBeat (t, 0.25); nb >= 0)
                t = inf->analysis.beats[(size_t) nb].seconds;
            editNote (-1, t);
            return;
        }
        if (! juce::isPositiveAndBelow (index, (int) inf->notes.size()))
            return;
        if (result == 1)
        {
            editNote (index, inf->notes[(size_t) index].seconds);
            return;
        }
        pushUndo();
        if (result == 2)
            inf->notes[(size_t) index].seconds = timeMap.toOriginal (engine.getPositionSeconds());
        else if (result == 3)
            inf->notes.erase (inf->notes.begin() + index);
        inf->sortNotes();
        library.saveSong (*inf);
        syncTimelineMarkers();
    });
}

void MainComponent::markerMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, "Renombrar...");
    m.addItem (2, tr ("Mover a la posición actual"));
    if (auto* info = currentInfo())
        if (juce::isPositiveAndBelow (index, (int) info->markers.size()))
            m.addItem (4, tr ("Ganancia del tramo... (") + juce::String (info->markers[(size_t) index].gainDb, 1) + " dB)");
    m.addSeparator();
    m.addItem (3, "Eliminar");

    auto* target = juce::isPositiveAndBelow (index, markerButtons.size()) ? markerButtons[index] : nullptr;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, index] (int result)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (index, (int) info->markers.size()))
            return;

        if (result == 4)
        {
            askText (tr ("Ganancia del tramo en dB"), juce::String (info->markers[(size_t) index].gainDb, 1), [this, index] (const juce::String& text)
            {
                auto* inf = currentInfo();
                if (inf == nullptr || ! juce::isPositiveAndBelow (index, (int) inf->markers.size()))
                    return;
                inf->markers[(size_t) index].gainDb = juce::jlimit (-40.0, 40.0, text.replace (",", ".").getDoubleValue());
                inf->levelingEnabled = true;
                levelBtn.setToggleState (true, juce::dontSendNotification);
                library.saveSong (*inf);
                updateGainCurve();
                rebuildMarkers();
            });
            return;
        }
        if (result == 1)
        {
            askText ("Nombre del marcador", info->markers[(size_t) index].name, [this, index] (const juce::String& name)
            {
                auto* inf = currentInfo();
                if (inf == nullptr || ! juce::isPositiveAndBelow (index, (int) inf->markers.size()) || name.trim().isEmpty())
                    return;
                inf->markers[(size_t) index].name = name.trim();
                library.saveSong (*inf);
                rebuildMarkers();
            });
            return;
        }
        if (result == 2)
            info->markers[(size_t) index].seconds = timeMap.toOriginal (engine.getPositionSeconds());
        else if (result == 3)
            info->markers.erase (info->markers.begin() + index);
        else
            return;

        info->sortMarkers();
        library.saveSong (*info);
        rebuildMarkers();
        updateLoopRegion();
        remeasureIfLeveling();
    });
}

void MainComponent::ensureTempoRegions (SongInfo& info) const
{
    if (info.tempoRegions.empty())
        info.tempoRegions.push_back ({ 0.0, info.analysis.bpm > 0.0 ? info.analysis.bpm : info.bpm, 0.0 });
}

void MainComponent::sectionRange (const SongInfo& info, int region, double& tStart, double& tEnd) const
{
    tStart = 0.0;
    tEnd = songLengthSeconds();
    if (juce::isPositiveAndBelow (region, (int) info.tempoRegions.size()))
    {
        tStart = info.tempoRegions[(size_t) region].start;
        if (region + 1 < (int) info.tempoRegions.size())
            tEnd = juce::jmin (tEnd, info.tempoRegions[(size_t) region + 1].start);
    }
}

void MainComponent::tempoBandMenu (int index, double playbackSeconds)
{
    auto* info = currentInfo();
    if (info == nullptr || arrangedSong == nullptr)
        return;
    ensureTempoRegions (*info);
    if (! juce::isPositiveAndBelow (index, (int) info->tempoRegions.size()))
        return;
    const auto& r = info->tempoRegions[(size_t) index];
    const double effective = effectivePlayBpm (*info, r);
    const bool several = info->tempoRegions.size() > 1;
    const double originalSeconds = timeMap.toOriginal (playbackSeconds);
    // Tiempos detectados dentro de la sección y compás en curso al inicio
    double tStart = 0.0, tEnd = 0.0;
    sectionRange (*info, index, tStart, tEnd);
    const auto& an = info->analysis;
    const int firstBeat = an.firstBeatAtOrAfter (tStart), endBeat = an.firstBeatAtOrAfter (tEnd);
    const bool hasBeats = endBeat - firstBeat >= 2;
    int meterHere = juce::jlimit (2, 7, an.meter);
    if (hasBeats)
    {
        const int d = an.firstDownbeatIn (firstBeat, endBeat);
        for (int j = d + 1; j < endBeat; ++j)
            if (an.beats[(size_t) j].beatInBar == 1) { meterHere = juce::jlimit (2, 7, j - d); break; }
    }

    juce::PopupMenu m;
    m.addSectionHeader (tr ("Sección ") + juce::String (index + 1) + ": " + formatTime (timeMap.toPlayback (tStart)) + " - " + formatTime (timeMap.toPlayback (tEnd)));
    m.addItem (1, tr ("Tempo de la sección... (") + juce::String (effective, 1) + " BPM)");
    m.addItem (2, tr ("Tempo original de la sección (") + juce::String (r.origBpm, 1) + " BPM)", std::abs (effective - r.origBpm) >= 0.05);
    m.addItem (3, tr ("Igualar todas las secciones a ") + juce::String (effective, 1) + " BPM", several);
    m.addSeparator();
    m.addItem (4, tr ("Dividir la sección en ") + formatTime (playbackSeconds));
    m.addItem (5, tr ("Unir con la sección anterior"), index > 0);
    m.addItem (6, tr ("Corregir el tempo detectado... (") + juce::String (r.origBpm, 1) + " BPM)");
    m.addSeparator();
    const int tr_ = effectiveTranspose (*info, r);
    m.addItem (20, tr ("Tono de la sección... (") + juce::String::formatted ("%+d st", tr_) + (r.transpose == TempoRegion::followSong ? tr (", el de la canción)") : tr (", propio)")));
    m.addItem (21, tr ("Tono de la canción para esta sección"), r.transpose != TempoRegion::followSong);
    m.addSeparator();
    juce::PopupMenu click;
    click.addItem (40, tr ("Tiempos a la mitad (suena en corcheas)"), hasBeats);
    click.addItem (41, tr ("Tiempos al doble (suena en blancas)"), hasBeats);
    click.addItem (42, tr ("Rejilla fija a ") + juce::String (r.origBpm, 1) + tr (" BPM desde el inicio de la sección"));
    const int evenCount = juce::jmax (1, (int) std::lround ((tEnd - tStart) * r.origBpm / 60.0));
    const juce::String evenText = tr ("Rejilla pareja: ") + juce::String (evenCount) + tr (" tiempos iguales hasta el fin de la sección (")
                                  + juce::String (60.0 * evenCount / juce::jmax (0.001, tEnd - tStart), 1) + " BPM)";
    click.addItem (43, evenText + tr (", sobrante en el último compás"), tEnd > tStart + 0.1);
    click.addItem (44, evenText + tr (", sobrante como compás aparte"), tEnd > tStart + 0.1);
    click.addSeparator();
    for (int n = 2; n <= 7; ++n)
        click.addItem (50 + n, tr ("Compases de ") + juce::String (n) + tr (" tiempos"), hasBeats, n == meterHere);
    m.addSubMenu (tr ("Click de esta sección"), click);
    m.addSeparator();
    m.addItem (7, tr ("Crear marcadores en los cambios de tempo"), several);

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, index, originalSeconds, meterHere] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (index, (int) inf->tempoRegions.size()))
            return;
        auto& reg = inf->tempoRegions[(size_t) index];
        const bool single = inf->tempoRegions.size() == 1;
        if (result != 1 && result != 6 && result != 20)
            pushUndo();   // los diálogos (1, 6 y 20) lo hacen al aceptar

        auto applied = [this] (SongInfo& s)
        {
            library.saveSong (s);
            tempoControlsFromSong();
            syncTimelineMarkers();
            requestRender();
        };

        if (result == 1)
        {
            askText (tr ("Tempo de reproducción de la sección (BPM)"), juce::String (effectivePlayBpm (*inf, reg), 1),
                     [this, index, single] (const juce::String& text)
            {
                auto* s = currentInfo();
                if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) s->tempoRegions.size()))
                    return;
                const double v = juce::jlimit (40.0, 300.0, text.replace (",", ".").getDoubleValue());
                auto& rg = s->tempoRegions[(size_t) index];
                pushUndo();
                if (single)
                    s->playBpm = std::abs (v - rg.origBpm) < 0.05 ? 0.0 : v;   // una sola sección: es el tempo de la canción
                else
                    rg.playBpm = (s->playBpm <= 0.0 && std::abs (v - rg.origBpm) < 0.05) ? 0.0 : v;
                library.saveSong (*s);
                tempoControlsFromSong();
                syncTimelineMarkers();
                requestRender();
            });
            return;
        }
        if (result == 20)
        {
            askText (tr ("Tono de la sección en semitonos (-12 a 12; vacío = el de la canción)"),
                     reg.transpose == TempoRegion::followSong ? juce::String() : juce::String (reg.transpose), [this, index] (const juce::String& text)
            {
                auto* s = currentInfo();
                if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) s->tempoRegions.size()))
                    return;
                pushUndo();
                s->tempoRegions[(size_t) index].transpose = text.trim().isEmpty() ? TempoRegion::followSong : juce::jlimit (-12, 12, text.trim().getIntValue());
                library.saveSong (*s);
                tempoControlsFromSong();
                syncTimelineMarkers();
                requestRender();
            });
            return;
        }
        if (result == 21)
            reg.transpose = TempoRegion::followSong;
        else if (result == 6)
        {
            askText (tr ("Tempo original de la sección (BPM), si el detectado no es correcto"), juce::String (reg.origBpm, 1),
                     [this, index] (const juce::String& text)
            {
                auto* s = currentInfo();
                if (s == nullptr || ! juce::isPositiveAndBelow (index, (int) s->tempoRegions.size()))
                    return;
                const double v = text.replace (",", ".").getDoubleValue();
                if (v < 20.0 || v > 400.0)
                    return;
                pushUndo();
                s->tempoRegions[(size_t) index].origBpm = std::round (v * 10.0) / 10.0;
                library.saveSong (*s);
                tempoControlsFromSong();
                syncTimelineMarkers();
                requestRender();
            });
            return;
        }
        if (result == 21)
        {
            // ya aplicado arriba
        }
        else if (result == 2)
        {
            if (single)
                inf->playBpm = 0.0;
            else
                reg.playBpm = inf->playBpm > 0.0 ? reg.origBpm : 0.0;   // si la canción tiene tempo pedido, esta sección lo esquiva
        }
        else if (result == 3)
        {
            // Igualar: el tempo pedido pasa a ser el de la canción y ninguna sección lo contradice
            inf->playBpm = effectivePlayBpm (*inf, reg);
            for (auto& x : inf->tempoRegions)
                x.playBpm = 0.0;
        }
        else if (result == 4)
        {
            // Dividir en el instante pulsado, ajustado al tiempo detectado más cercano si hay análisis
            double at = originalSeconds;
            const int nearest = inf->analysis.nearestBeat (originalSeconds, 0.5);
            if (nearest >= 0)
                at = inf->analysis.beats[(size_t) nearest].seconds;
            for (auto& x : inf->tempoRegions)
                if (std::abs (x.start - at) < 0.5)
                {
                    undoStack.pop_back();
                    return;   // ya hay una frontera ahí
                }
            TempoRegion nueva { at, reg.origBpm, reg.playBpm };
            inf->tempoRegions.push_back (nueva);
            inf->sortTempoRegions();
        }
        else if (result == 5)
        {
            if (index == 0)
            {
                undoStack.pop_back();
                return;
            }
            inf->tempoRegions.erase (inf->tempoRegions.begin() + index);
        }
        else if (result == 7)
        {
            for (size_t i = 1; i < inf->tempoRegions.size(); ++i)
            {
                const double at = inf->tempoRegions[i].start;
                bool exists = false;
                for (auto& mk : inf->markers)
                    exists = exists || std::abs (mk.seconds - at) < 0.5;
                if (exists)
                    continue;
                SongMarker mk;
                mk.name = juce::String::formatted ("%.0f BPM", inf->tempoRegions[i].origBpm);
                mk.seconds = at;
                inf->markers.push_back (mk);
            }
            inf->sortMarkers();
            library.saveSong (*inf);
            rebuildMarkers();
            updateLoopRegion();
            remeasureIfLeveling();
            return;
        }
        else if (result >= 40 && result <= 57)
        {
            // Click de la sección: se editan los tiempos detectados dentro de ella
            double sStart = 0.0, sEnd = 0.0;
            sectionRange (*inf, index, sStart, sEnd);
            auto& a = inf->analysis;
            const int b0 = a.firstBeatAtOrAfter (sStart), b1 = a.firstBeatAtOrAfter (sEnd);
            if (result == 40)
                a.halveBeats (b0, b1, meterHere);
            else if (result == 41)
                a.doubleBeats (b0, b1, meterHere);
            else if (result == 42)
                a.replaceBeatsWithGrid (sStart, sEnd, reg.origBpm, meterHere);
            else if (result == 43 || result == 44)
            {
                const int count = juce::jmax (1, (int) std::lround ((sEnd - sStart) * reg.origBpm / 60.0));
                const double bpm = a.evenGrid (sStart, sEnd, count, meterHere, result == 43);
                if (bpm > 0.0)
                    reg.origBpm = std::round (bpm * 10.0) / 10.0;
            }
            else
                a.renumberBeats (b0, result - 50, b1, a.firstDownbeatIn (b0, b1));
            if (result == 40 || result == 41)
            {
                const double bpm = detectedBpmBetween (a, sStart, sEnd);   // el tempo original de la sección cambia con el click
                if (bpm > 0.0)
                    reg.origBpm = bpm;
            }
            if (a.bpm > 0.0)
                a.bpm = detectedBpmBetween (a, 0.0, 1.0e9);
            analysisEdited();
        }
        else
            return;
        applied (*inf);
    });
}

void MainComponent::chordMenu (int index, double playbackSeconds)
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    auto& a = info->analysis;
    // Instante en tiempo original, ajustado al tiempo detectado más cercano
    double at = timeMap.toOriginal (playbackSeconds);
    if (const int nb = a.nearestBeat (at, 0.25); nb >= 0)
        at = a.beats[(size_t) nb].seconds;
    const bool flats = useFlats();
    const bool onChord = juce::isPositiveAndBelow (index, (int) a.chords.size()) && a.chords[(size_t) index].name != "N";

    juce::PopupMenu m;
    if (onChord)
    {
        const auto& c = a.chords[(size_t) index];
        m.addSectionHeader (tr ("Acorde ") + music::spellChord (c.name, flats) + " (" + formatTime (timeMap.toPlayback (c.start)) + " - " + formatTime (timeMap.toPlayback (c.end)) + ")");
        m.addItem (1, tr ("Cambiar el acorde..."));
        m.addItem (2, tr ("Sin acorde"));
        m.addItem (3, tr ("Dividir el acorde en ") + formatTime (timeMap.toPlayback (at)), at > c.start + 0.05 && at < c.end - 0.05);
        m.addItem (4, tr ("Unir con el siguiente"), index + 1 < (int) a.chords.size());
    }
    else
    {
        m.addSectionHeader (tr ("Sin acorde en ") + formatTime (timeMap.toPlayback (at)));
        m.addItem (5, tr ("Añadir acorde aquí..."));
    }

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, index, at] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0)
            return;
        auto& an = inf->analysis;
        auto sortChords = [] (Analysis& x)
        {
            std::sort (x.chords.begin(), x.chords.end(), [] (const Chord& p, const Chord& q) { return p.start < q.start; });
        };
        if (result == 1 && juce::isPositiveAndBelow (index, (int) an.chords.size()))
        {
            askText (tr ("Acorde (por ejemplo Am, F#7, Bbmaj7)"), music::spellChord (an.chords[(size_t) index].name, useFlats()), [this, index] (const juce::String& text)
            {
                auto* s = currentInfo();
                const auto name = text.trim();
                if (s == nullptr || name.isEmpty() || ! juce::isPositiveAndBelow (index, (int) s->analysis.chords.size()))
                    return;
                pushUndo();
                s->analysis.chords[(size_t) index].name = name;
                analysisEdited();
            });
            return;
        }
        if (result == 5)
        {
            askText (tr ("Acorde (por ejemplo Am, F#7, Bbmaj7)"), "", [this, at] (const juce::String& text)
            {
                auto* s = currentInfo();
                const auto name = text.trim();
                if (s == nullptr || name.isEmpty())
                    return;
                auto& x = s->analysis;
                // Hasta el siguiente acorde con nombre o el final; si cae dentro de un tramo "N", ese tramo se recorta
                double end = songLengthSeconds() > 0.0 ? songLengthSeconds() : at + 4.0;
                for (auto& c : x.chords)
                    if (c.start > at + 1.0e-6 && c.name != "N")
                        end = juce::jmin (end, c.start);
                for (auto& c : x.chords)
                    if (c.name == "N" && c.start <= at && c.end > at)
                        end = juce::jmin (end, c.end);
                if (end <= at + 0.05)
                    return;
                pushUndo();
                for (auto& c : x.chords)
                    if (c.name == "N" && c.start <= at && c.end > at)
                        c.end = at;
                x.chords.push_back ({ at, end, name });
                std::sort (x.chords.begin(), x.chords.end(), [] (const Chord& p, const Chord& q) { return p.start < q.start; });
                analysisEdited();
            });
            return;
        }
        if (! juce::isPositiveAndBelow (index, (int) an.chords.size()))
            return;
        pushUndo();
        auto& c = an.chords[(size_t) index];
        if (result == 2)
            c.name = "N";
        else if (result == 3)
        {
            Chord second { at, c.end, c.name };
            c.end = at;
            an.chords.insert (an.chords.begin() + index + 1, second);
        }
        else if (result == 4 && index + 1 < (int) an.chords.size())
        {
            c.end = an.chords[(size_t) index + 1].end;
            an.chords.erase (an.chords.begin() + index + 1);
        }
        else
            return;
        sortChords (an);
        analysisEdited();
    });
}

double MainComponent::songLengthSeconds() const
{
    return arrangedSong != nullptr ? (double) arrangedSong->length / engine.getSampleRate() : 0.0;
}

std::vector<TimelineView::ClipView> MainComponent::mappedClips() const
{
    std::vector<TimelineView::ClipView> out;
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()) || sourceSong == nullptr)
        return out;
    const auto& info = library.songs[(size_t) currentIndex];
    if (arrangement::isIdentity (info.clips, (double) sourceSong->length / engine.getSampleRate()))
        return out;   // el audio original entero: sin bordes que mostrar
    for (auto& c : info.clips)
        out.push_back ({ timeMap.toPlayback (c.position), timeMap.toPlayback (c.end()) });
    return out;
}

void MainComponent::pushUndo()
{
    pullMidiControls();   // el estado guardado lleva los canales de las filas como están ahora
    if (auto* info = currentInfo())
    {
        undoStack.push_back (*info);
        if (undoStack.size() > 30)
            undoStack.erase (undoStack.begin());
    }
}

void MainComponent::undoLastEdit()
{
    auto* info = currentInfo();
    if (info == nullptr || undoStack.empty())
        return;
    auto restored = undoStack.back();
    undoStack.pop_back();
    if (restored.folder != info->folder)
    {
        undoStack.clear();
        return;
    }
    restored.stems = info->stems;   // la mezcla (faders, mute, salidas) no se deshace
    restored.name = info->name;
    // Tampoco lo que no pasa por pushUndo en las pistas MIDI: la mezcla de las filas, sus nombres y sonidos, el nombre de la
    // pista y sus silencios. Se toma de la pista actual con el mismo origen (la n-ésima con ese archivo) y de su fila con la
    // misma nota; lo que no tiene pareja (una fila quitada que vuelve, una nota cambiada) queda como se guardó
    pullMidiControls();
    for (size_t i = 0; i < restored.midiTracks.size(); ++i)
    {
        auto& mt = restored.midiTracks[i];
        int nth = 0;
        for (size_t j = 0; j < i; ++j)
            nth += restored.midiTracks[j].sourceFile == mt.sourceFile ? 1 : 0;
        const MidiTrack* now = nullptr;
        for (auto& candidate : info->midiTracks)
            if (candidate.sourceFile == mt.sourceFile && nth-- == 0)
            {
                now = &candidate;
                break;
            }
        if (now == nullptr)
            continue;
        mt.name = now->name;
        mt.muted = now->muted;
        mt.muteSource = now->muteSource;
        for (auto& pad : mt.pads)
            for (auto& nowPad : now->pads)
                if (nowPad.note == pad.note)
                {
                    pad.name = nowPad.name;
                    pad.sound = nowPad.sound;
                    pad.gainDb = nowPad.gainDb;
                    pad.muted = nowPad.muted;
                    pad.outputPair = nowPad.outputPair;
                }
    }
    *info = restored;
    refreshFromInfo();
    sepLabel.setText (tr ("Edición deshecha"), juce::dontSendNotification);
}

void MainComponent::refreshFromInfo()
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    library.saveSong (*info);
    rebuildMarkers();           // marcadores, ganancias por tramo y sincronización de la regla (bandas, tramos)
    clickControlsFromSong();    // incluye los controles de tempo
    applyClick();
    timeline.setAnalysis (mappedAnalysis());
    updateBeatGrid();
    showChordAndKey (engine.getPositionSeconds());
    updateLoopRegion();
    applyMidiTracks();
    syncTriggerMarks();
    requestRender();
}

void MainComponent::arrangementEdited()
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    library.saveSong (*info);
    syncTimelineMarkers();
    tempoControlsFromSong();
    applyMidiTracks();   // los golpes ya se movieron con los tramos: la vista y sus índices los siguen al instante
    requestRender();
}

bool MainComponent::arrangementReady()
{
    auto* info = currentInfo();
    if (info != nullptr && sameClips (info->clips, renderedClips))
        return true;
    sepLabel.setText (tr ("El arreglo se está preparando: prueba en un momento"), juce::dontSendNotification);
    return false;
}

bool MainComponent::isMidiSource (const SongInfo& info, const StemInfo& stem)
{
    for (auto& mt : info.midiTracks)
        if (mt.sourceFile.isNotEmpty() && mt.sourceFile == stem.fileName)
            return true;
    return false;
}

std::vector<TriggerSettings> MainComponent::triggersOf (const SongInfo& info)
{
    // Un stem del que salió una pista MIDI no dispara en vivo (la pista MIDI lo reemplaza); cuenta como apagado
    // para que crear o quitar la pista MIDI vuelva a armar los triggers
    std::vector<TriggerSettings> out;
    for (auto& st : info.stems)
    {
        out.push_back (st.trigger);
        if (isMidiSource (info, st))
            out.back().enabled = false;
    }
    return out;
}

std::shared_ptr<SamplerSet> MainComponent::buildSamplers (LoadedSong& rendered, const SongInfo& info, double sr,
                                                          const juce::File& banksFolder, juce::AudioFormatManager& formats, BankCache& cache)
{
    auto set = std::make_shared<SamplerSet>();
    for (auto& t : rendered.tracks)
    {
        t->replaced = false;
        if (! juce::isPositiveAndBelow (t->stemIndex, (int) info.stems.size()))
            continue;
        const auto& st = info.stems[(size_t) t->stemIndex];
        const auto& tg = st.trigger;
        const bool isBank = tg.sound.startsWith ("banco:");
        const bool isInstrument = tg.sound.startsWith ("vst:");
        if (! tg.enabled || (! isBank && ! isInstrument) || isMidiSource (info, st))
            continue;
        const auto bankName = isBank ? tg.sound.fromFirstOccurrenceOf ("banco:", false, false) : juce::String();
        std::shared_ptr<SampleBankData> bank;
        if (isBank)
        {
            const auto key = bankName + "@" + juce::String (sr);
            const juce::ScopedLock sl (cache.lock);
            auto it = cache.banks.find (key);
            if (it != cache.banks.end())
                bank = it->second;
        }
        if (isBank && bank == nullptr)
        {
            bank = triggers::loadBank (banksFolder.getChildFile (juce::File::createLegalFileName (bankName)), sr, formats);
            if (bank != nullptr)
            {
                const juce::ScopedLock sl (cache.lock);
                cache.banks[bankName + "@" + juce::String (sr)] = bank;
            }
        }
        auto lane = std::make_unique<SamplerLane>();
        lane->name = st.name + " (muestras)";
        lane->stemIndex = t->stemIndex;
        lane->events = triggers::detect (t->buffer, sr, tg.thresholdDb, tg.sensitivity, tg.minMs);
        lane->bank = bank;
        lane->instrumentId = isInstrument ? tg.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue() : -1;
        lane->note = tg.note;
        lane->control.name = lane->name;
        lane->control.stemIndex = -1;
        lane->control.gain = tg.gainDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (tg.gainDb);
        lane->control.smoothedGain = lane->control.gain.load();
        lane->control.muted = tg.muted;
        lane->control.outputPair = tg.outputPair;
        // La pista deja de sonar solo si su trigger tiene un sonido que dar (si no, se oiría nada sin aviso);
        // con un instrumento se confirma en refreshReplaced (el rack vive en la UI)
        t->replaced = ! tg.keepAudio && (bank != nullptr || isInstrument);
        set->lanes.push_back (std::move (lane));
    }
    return set;
}

juce::String MainComponent::triggerLabelFor (const TriggerSettings& tg, bool hasSound, const juce::String& instrumentName, int& mode)
{
    if (! tg.enabled)
    {
        mode = 0;
        return "Audio";
    }
    juce::String sound;
    if (tg.sound.startsWith ("banco:"))
        sound = tg.sound.fromFirstOccurrenceOf ("banco:", false, false);
    else if (tg.sound.startsWith ("vst:") && instrumentName.isNotEmpty())
        sound = instrumentName + " " + juce::MidiMessage::getMidiNoteName (tg.note, true, true, 3);
    if (sound.isEmpty() || ! hasSound)
    {
        mode = 3;
        return tr ("Trig: sin sonido");
    }
    mode = tg.keepAudio ? 2 : 1;
    return (tg.keepAudio ? "Trig+Audio: " : "Trig: ") + sound;
}

void MainComponent::applySamplers (std::shared_ptr<SamplerSet> set)
{
    samplers = std::move (set);
    engine.setSamplers (samplers);
    mixer.setSamplers (samplers);
    applyMidiTracks();   // setSong descarta también las pistas MIDI; y el mapa de tempo pudo cambiar
    syncTriggerMarks();
}

void MainComponent::refreshReplaced()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    for (auto& t : currentSong->tracks)
    {
        if (! juce::isPositiveAndBelow (t->stemIndex, (int) info->stems.size()))
            continue;
        const auto& tg = info->stems[(size_t) t->stemIndex].trigger;
        bool has = false;
        if (tg.enabled && samplers != nullptr)
            for (auto& lane : samplers->lanes)
                if (lane->stemIndex == t->stemIndex)
                    has = lane->instrumentId >= 0 ? (rack != nullptr && rack->has (lane->instrumentId)) : lane->bank != nullptr;
        // Una pista MIDI de este stem lo calla si alguna de sus filas suena de verdad (banco cargado o instrumento presente)
        bool midiReplaces = false;
        if (midiSamplers != nullptr)
            for (auto& lane : midiSamplers->lanes)
            {
                if (padForLane (*info, *lane) == nullptr)
                    continue;   // línea de otra canción o de una fila que ya no está
                const auto& mt = info->midiTracks[(size_t) lane->midiTrack];
                if (mt.muted || ! mt.muteSource || mt.sourceFile.isEmpty() || mt.sourceFile != info->stems[(size_t) t->stemIndex].fileName)
                    continue;
                const bool canSound = lane->instrumentId >= 0 ? (rack != nullptr && rack->has (lane->instrumentId)) : lane->bank != nullptr;
                midiReplaces = midiReplaces || (canSound && ! lane->control.muted.load());
            }
        t->replaced = (tg.enabled && ! tg.keepAudio && has) || midiReplaces;
    }
}

void MainComponent::syncTriggerMarks()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    std::vector<bool> states;
    std::vector<std::vector<TimelineView::TriggerMark>> marks;
    const double sr = engine.getSampleRate();
    for (auto& t : currentSong->tracks)
    {
        const bool has = juce::isPositiveAndBelow (t->stemIndex, (int) info->stems.size());
        states.push_back (has && info->stems[(size_t) t->stemIndex].trigger.enabled && ! isMidiSource (*info, info->stems[(size_t) t->stemIndex]));
        std::vector<TimelineView::TriggerMark> m;
        bool hasSound = false;
        juce::String instrumentName;
        if (samplers != nullptr)
            for (auto& lane : samplers->lanes)
                if (lane->stemIndex == t->stemIndex)
                {
                    if (lane->instrumentId >= 0)
                    {
                        instrumentName = rack != nullptr ? rack->nameOf (lane->instrumentId) : juce::String();
                        hasSound = rack != nullptr && rack->has (lane->instrumentId);
                    }
                    else
                        hasSound = lane->bank != nullptr;
                    for (auto& e : lane->events)
                        m.push_back ({ (double) e.sample / sr, e.velocity });
                }
        marks.push_back (std::move (m));
        if (has)
        {
            int mode = 0;
            auto text = triggerLabelFor (info->stems[(size_t) t->stemIndex].trigger, hasSound, instrumentName, mode);
            for (auto& mt : info->midiTracks)
                if (mt.sourceFile.isNotEmpty() && mt.sourceFile == info->stems[(size_t) t->stemIndex].fileName)
                {
                    text = (mt.muteSource && ! mt.muted ? "MIDI: " : "MIDI+Audio: ") + mt.name;
                    mode = mt.muteSource && ! mt.muted ? 1 : 2;
                }
            mixer.setTriggerLabel ((int) states.size() - 1, text, mode);
        }
    }
    timeline.setTriggerStates (states);
    timeline.setTriggerMarks (marks);
}

void MainComponent::setTriggerEnabled (int track, bool on)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || ! juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
        return;
    const int stem = currentSong->tracks[(size_t) track]->stemIndex;
    if (! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
        return;
    auto& tg = info->stems[(size_t) stem].trigger;
    if (on && isMidiSource (*info, info->stems[(size_t) stem]))
    {
        // La pista MIDI de este stem reemplaza al trigger en vivo: sus sonidos se eligen en sus filas
        syncTriggerMarks();
        sepLabel.setText (tr ("Esta pista ya tiene una pista MIDI que reemplaza al trigger: elige el sonido con clic derecho en sus filas"),
                          juce::dontSendNotification);
        return;
    }
    if (on && tg.sound.isEmpty())
    {
        // Sin sonido asignado todavía: elegirlo primero
        syncTriggerMarks();
        triggerMenu (track);
        return;
    }
    tg.enabled = on;
    library.saveSong (*info);
    syncTriggerMarks();
    requestRender();
}

// Notas de batería General MIDI que ofrecen los menús de nota (trigger y filas de pistas MIDI)
static const int midiDrumNotes[] = { 36, 38, 37, 40, 42, 44, 46, 41, 43, 45, 47, 48, 50, 49, 57, 51, 53, 55, 52, 54,
                                     56, 60, 61, 62, 63, 64, 65, 66, 69, 70, 73, 74, 75, 76 };

// Notas de un menú de nota: las de batería General MIDI (sirven también para Addictive Drums puesto en su mapa General
// MIDI) y, en un submenú, el mapa "AD2 Standard" de Addictive Drums 2 por grupos. Ids = noteMenuBase + nota (lejos de
// los demás ids de esos menús); `enabled` (si está) dice si una nota se puede elegir. La nota actual queda marcada.
static constexpr int noteMenuBase = 1000;

static void addNoteItems (juce::PopupMenu& menu, int current, const std::function<bool (int)>& enabled)
{
    const auto label = [] (int n, const juce::String& name)
    {
        return name + "  (" + juce::MidiMessage::getMidiNoteName (n, true, true, 3) + ", " + juce::String (n) + ")";
    };
    menu.addSectionHeader (tr ("General MIDI"));
    for (int n : midiDrumNotes)
        menu.addItem (noteMenuBase + n, label (n, miditrack::drumNoteName (n)), enabled == nullptr || enabled (n), n == current);
    juce::PopupMenu ad2;
    ad2.addSectionHeader (tr ("Su mapa «AD2 Standard» (compruébalo con Escuchar)"));
    for (auto& group : miditrack::addictiveDrumsMap())
    {
        juce::PopupMenu sub;
        bool hasCurrent = false;
        for (auto& nn : group.notes)
        {
            sub.addItem (noteMenuBase + nn.note, label (nn.note, tr (nn.name)), enabled == nullptr || enabled (nn.note), nn.note == current);
            hasCurrent = hasCurrent || nn.note == current;
        }
        ad2.addSubMenu (tr (group.name), sub, true, nullptr, hasCurrent);
    }
    menu.addSeparator();
    menu.addSubMenu (tr ("Addictive Drums 2"), ad2);
}

void MainComponent::triggerMenu (int track)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || ! juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
        return;
    const int stem = currentSong->tracks[(size_t) track]->stemIndex;
    if (! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
        return;
    const auto& st = info->stems[(size_t) stem];
    const auto& tg = st.trigger;
    int detected = -1;
    if (samplers != nullptr)
        for (auto& lane : samplers->lanes)
            if (lane->stemIndex == stem)
                detected = (int) lane->events.size();

    juce::PopupMenu m;
    m.addSectionHeader (st.name + (detected >= 0 ? ": " + juce::String (detected) + tr (" golpes detectados") : juce::String()));
    m.addItem (10, tr ("Suena por sí misma (sin trigger)"), true, ! tg.enabled);
    m.addItem (11, tr ("Es trigger: suena solo el sonido elegido"), true, tg.enabled && ! tg.keepAudio);
    m.addItem (12, tr ("Trigger y audio: suenan ambas"), true, tg.enabled && tg.keepAudio);
    m.addSeparator();
    juce::PopupMenu sound;
    const auto banks = library.listBanks();
    sound.addSectionHeader (tr ("Bancos de muestras grabadas"));
    for (int i = 0; i < banks.size(); ++i)
        sound.addItem (100 + i, banks[i], true, tg.sound == "banco:" + banks[i]);
    if (banks.isEmpty())
        sound.addItem (99, tr ("(no hay bancos todavía)"), false);
    sound.addItem (2, tr ("Importar muestras (wav) como banco nuevo..."));
    sound.addItem (5, tr ("Grabar golpes como banco nuevo..."));
    sound.addSeparator();
    sound.addSectionHeader (tr ("Instrumentos ") + InstrumentRack::formatsAvailable());
    std::vector<int> instrumentIds;
    if (rack != nullptr)
        for (auto& slot : rack->getSlots())
        {
            instrumentIds.push_back (slot.id);
            sound.addItem (300 + (int) instrumentIds.size() - 1, slot.description.name + (slot.lane == nullptr ? tr (" (no cargado)") : juce::String()),
                           true, tg.sound == "vst:" + juce::String (slot.id));
        }
    if (instrumentIds.empty())
        sound.addItem (299, tr ("(sin instrumentos cargados)"), false);
    sound.addItem (298, tr ("Añadir o buscar instrumentos..."));
    juce::String soundName = tr ("sin sonido");
    if (tg.sound.startsWith ("banco:"))
        soundName = tg.sound.fromFirstOccurrenceOf ("banco:", false, false);
    else if (tg.sound.startsWith ("vst:"))
        soundName = rack != nullptr && rack->nameOf (tg.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue()).isNotEmpty()
                        ? rack->nameOf (tg.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue()) : tr ("instrumento ausente");
    m.addSubMenu (tr ("Sonido: ") + soundName, sound);
    juce::PopupMenu notes;
    addNoteItems (notes, tg.note, {});
    notes.addSeparator();
    notes.addItem (250, tr ("Otra nota (0 a 127)..."));
    m.addSubMenu (tr ("Nota MIDI (para instrumentos VST): ") + juce::MidiMessage::getMidiNoteName (tg.note, true, true, 3) + " (" + juce::String (tg.note) + ")", notes);
    m.addItem (6, tr ("Escuchar (un golpe con este sonido)"), tg.enabled && tg.sound.isNotEmpty() && ! isMidiSource (*info, st));
    m.addItem (3, tr ("Ajustes de detección... (umbral ") + juce::String (tg.thresholdDb, 0) + " dB, sensibilidad " + juce::String (tg.sensitivity, 1) + ")");
    m.addSeparator();
    m.addItem (400, tr ("Crear pista MIDI con estos golpes (para corregirlos a mano)"), arrangedSong != nullptr);
    m.addItem (4, tr ("Quitar el trigger de esta pista"), tg.enabled || tg.sound.isNotEmpty());

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, track, stem, banks, instrumentIds] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (stem, (int) inf->stems.size()))
            return;
        auto& t = inf->stems[(size_t) stem].trigger;
        bool chooseSound = false;
        if (result == 298)
        {
            instrumentsMenu();
            return;
        }
        if (result >= 300 && result < 300 + (int) instrumentIds.size())
        {
            t.sound = "vst:" + juce::String (instrumentIds[(size_t) (result - 300)]);
            t.enabled = true;
            library.saveSong (*inf);
            syncTriggerMarks();
            requestRender();
            return;
        }
        if (result == 10)
            t.enabled = false;
        else if (result == 11 || result == 12)
        {
            t.enabled = true;
            t.keepAudio = result == 12;
            chooseSound = t.sound.isEmpty();
        }
        else if (result == 2)
        {
            importBankFor (track);
            return;
        }
        else if (result == 3)
        {
            triggerSettingsDialog (track);
            return;
        }
        else if (result == 5)
        {
            recordDialog (true, track);
            return;
        }
        else if (result == 400)
        {
            createMidiTrack (track);
            return;
        }
        else if (result == 4)
        {
            t.enabled = false;
            t.sound.clear();
        }
        else if (result >= 100 && result < 100 + banks.size())
        {
            t.sound = "banco:" + banks[result - 100];
            t.enabled = true;
        }
        else if (result >= noteMenuBase && result < noteMenuBase + 128)
            t.note = result - noteMenuBase;
        else if (result == 6)
        {
            auditionTrigger (stem);
            return;
        }
        else if (result == 250)
        {
            askText (tr ("Nota MIDI (0 a 127; 36 = bombo, 38 = caja)"), juce::String (t.note), [this, stem] (const juce::String& text)
            {
                auto* in2 = currentInfo();
                if (in2 == nullptr || ! juce::isPositiveAndBelow (stem, (int) in2->stems.size()))
                    return;
                in2->stems[(size_t) stem].trigger.note = juce::jlimit (0, 127, text.getIntValue());
                library.saveSong (*in2);
                syncTriggerMarks();
            });
            return;
        }
        else
            return;
        library.saveSong (*inf);
        syncTriggerMarks();
        requestRender();
        if (chooseSound)
        {
            // Falta el sonido: se vuelve a abrir el menú para elegirlo en «Sonido»
            sepLabel.setText (tr ("Elige el sonido del trigger en «Sonido»"), juce::dontSendNotification);
            juce::Component::SafePointer<MainComponent> sp (this);
            juce::MessageManager::callAsync ([sp, track]
            {
                if (sp != nullptr)
                    sp->triggerMenu (track);
            });
        }
    });
}

void MainComponent::importBankFor (int track)
{
    importBank ([this, track] (const juce::String& bankName)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || currentSong == nullptr || ! juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
            return;
        const int stem = currentSong->tracks[(size_t) track]->stemIndex;
        if (! juce::isPositiveAndBelow (stem, (int) inf->stems.size()))
            return;
        auto& t = inf->stems[(size_t) stem].trigger;
        t.sound = "banco:" + bankName;
        t.enabled = true;
        library.saveSong (*inf);
        syncTriggerMarks();
        requestRender();
    });
}

void MainComponent::triggerSettingsDialog (int track)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || ! juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()))
        return;
    const int stem = currentSong->tracks[(size_t) track]->stemIndex;
    if (! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
        return;
    const auto& tg = info->stems[(size_t) stem].trigger;
    auto* w = new juce::AlertWindow (tr ("Detección de golpes: ") + info->stems[(size_t) stem].name,
                                     tr ("Umbral: nivel mínimo de un golpe (dBFS). Sensibilidad: > 1 realza los golpes suaves. "
                                         "Tiempo mínimo entre golpes en ms."), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("umbral", juce::String (tg.thresholdDb, 0), tr ("Umbral (dB, de -80 a 0)"));
    w->addTextEditor ("sens", juce::String (tg.sensitivity, 2), tr ("Sensibilidad (0,2 a 5)"));
    w->addTextEditor ("min", juce::String (tg.minMs, 0), tr ("Tiempo mínimo (ms)"));
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, stem] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result != 1 || ! juce::isPositiveAndBelow (stem, (int) inf->stems.size()))
            return;
        auto& t = inf->stems[(size_t) stem].trigger;
        t.thresholdDb = juce::jlimit (-80.0, 0.0, w->getTextEditorContents ("umbral").replace (",", ".").getDoubleValue());
        t.sensitivity = juce::jlimit (0.2, 5.0, w->getTextEditorContents ("sens").replace (",", ".").getDoubleValue());
        t.minMs = juce::jlimit (5.0, 2000.0, w->getTextEditorContents ("min").replace (",", ".").getDoubleValue());
        library.saveSong (*inf);
        requestRender();
    }), true);
}

void MainComponent::setTriggerForCapture (const juce::String& bank)
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    for (auto& st : info->stems)
        if (Analyzer::isDrumsTrack (st.name, st.fileName) || st.name.containsIgnoreCase ("bombo") || st.fileName.containsIgnoreCase ("kick"))
        {
            st.trigger.enabled = true;
            st.trigger.sound = "banco:" + bank;
            break;
        }
    library.saveSong (*info);
    requestRender();
}

//==============================================================================
// Grabación de entradas (punto 2)

bool MainComponent::ensureInputsEnabled (juce::String& problem)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        problem = tr ("No hay un dispositivo de audio abierto. Revísalo en «Audio».");
        return false;
    }
    if (device->getActiveInputChannels().countNumberOfSetBits() > 0)
        return true;
    if (device->getInputChannelNames().isEmpty())
    {
        problem = tr ("El dispositivo de audio actual no tiene entradas. Elige en «Audio» una interfaz con entradas.");
        return false;
    }
    // Se activan las dos primeras entradas (queda guardado con la configuración del dispositivo)
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, juce::jmin (2, device->getInputChannelNames().size()), true);
    if (setup.inputDeviceName.isEmpty())
        setup.inputDeviceName = setup.outputDeviceName;
    const auto err = deviceManager.setAudioDeviceSetup (setup, true);
    device = deviceManager.getCurrentAudioDevice();
    if (err.isNotEmpty() || device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
    {
        problem = tr ("No se pudieron activar las entradas del dispositivo") + (err.isNotEmpty() ? ": " + err : juce::String())
                  + tr ("\n\nActívalas en «Audio».");
        return false;
    }
    return true;
}

void MainComponent::recordDialog (bool toBank, int bankTrack)
{
    if (recording.active)
        return;
    // Una pista nueva solo con canción cargada y en tempo y tono originales (la toma se alinea con el audio guardado)
    const bool canTrack = currentInfo() != nullptr && currentSong != nullptr && timeMap.isPlain();
    if (! toBank && ! canTrack && (currentInfo() == nullptr || currentSong == nullptr))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Grabar"),
                                                tr ("Carga una canción para grabarle una pista nueva (o graba golpes para un banco desde el trigger de una pista)."));
        return;
    }
    juce::String problem;
    if (! ensureInputsEnabled (problem))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Grabar"), problem);
        return;
    }
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto names = device->getInputChannelNames();
    const auto active = device->getActiveInputChannels();
    juce::StringArray items;
    std::vector<std::pair<int, int>> choices;   // canales (entre los activos) izquierdo y derecho (-1 = mono)
    std::vector<int> compactOf ((size_t) names.size(), -1);
    int compact = 0;
    for (int i = 0; i < names.size(); ++i)
        if (active[i])
            compactOf[(size_t) i] = compact++;
    for (int i = 0; i < names.size(); ++i)
        if (active[i])
        {
            items.add (tr ("Entrada ") + juce::String (i + 1) + ": " + names[i]);
            choices.push_back ({ compactOf[(size_t) i], -1 });
        }
    for (int i = 0; i + 1 < names.size(); i += 2)
        if (active[i] && active[i + 1])
        {
            items.add (tr ("Entradas ") + juce::String (i + 1) + "-" + juce::String (i + 2) + tr (" (estéreo)"));
            choices.push_back ({ compactOf[(size_t) i], compactOf[(size_t) i + 1] });
        }

    // Dos destinos: pista nueva (se suma a la mezcla) o golpes sueltos para un banco de muestras (sonido del trigger)
    juce::StringArray destinations;
    std::vector<bool> destIsBank;
    if (canTrack)
    {
        destinations.add (tr ("Pista nueva de la canción (se suma a la mezcla)"));
        destIsBank.push_back (false);
    }
    destinations.add (tr ("Golpes sueltos para un banco de muestras (sonido del trigger)"));
    destIsBank.push_back (true);

    auto* w = new juce::AlertWindow (tr ("Grabar"),
                                     tr ("Pista nueva: la canción arranca desde el cabezal y la entrada se graba alineada con ella (se compensa "
                                         "la latencia del dispositivo); al detener (botón, Stop o final) la toma se agrega como pista.\n"
                                         "Banco de muestras: toca golpes sueltos, de suave a fuerte, con silencio entre ellos; al detener, "
                                         "cada golpe se recorta justo en su ataque y queda como una muestra del banco, listo para el trigger.")
                                         + (canTrack ? juce::String() : tr ("\n(Sin canción cargada o con tempo/tono cambiados solo se puede grabar un banco.)")),
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addComboBox ("destino", destinations, tr ("Qué grabar"));
    w->getComboBoxComponent ("destino")->setSelectedItemIndex (toBank || ! canTrack ? destinations.size() - 1 : 0);
    w->addComboBox ("entrada", items, tr ("Entrada"));
    w->getComboBoxComponent ("entrada")->setSelectedItemIndex (juce::jlimit (0, items.size() - 1, props.getUserSettings()->getIntValue ("recordInput", 0)));
    w->addTextEditor ("nombre", toBank || ! canTrack ? tr ("Bombo") : tr ("Grabación"), tr ("Nombre de la pista o del banco"));
    w->addComboBox ("monitor", {}, tr ("Escuchar la entrada por"));
    auto* mon = w->getComboBoxComponent ("monitor");
    const int savedMonitor = props.getUserSettings()->getIntValue ("recordMonitor", 0);
    fillOutputBox (*mon, juce::jmax (0, savedMonitor));
    mon->addItem (tr ("No escuchar"), 999);
    if (savedMonitor < 0)
        mon->setSelectedId (999, juce::dontSendNotification);
    w->addTextEditor ("ajuste", juce::String (props.getUserSettings()->getDoubleValue ("recordOffsetMs", 0.0), 1),
                      tr ("Compensación extra (ms; positivo = adelanta la toma)"));
    w->addButton (tr ("Grabar"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, choices, destIsBank, bankTrack] (int result)
    {
        if (result != 1 || choices.empty() || destIsBank.empty())
            return;
        const int dest = juce::jlimit (0, (int) destIsBank.size() - 1, w->getComboBoxComponent ("destino")->getSelectedItemIndex());
        const bool bankMode = destIsBank[(size_t) dest];
        const int idx = juce::jlimit (0, (int) choices.size() - 1, w->getComboBoxComponent ("entrada")->getSelectedItemIndex());
        const int monId = w->getComboBoxComponent ("monitor")->getSelectedId();
        const int monitorPair = monId == 999 ? -1 : juce::jmax (0, monId - 1);
        const double extra = juce::jlimit (-500.0, 500.0, w->getTextEditorContents ("ajuste").replace (",", ".").getDoubleValue());
        auto* settings = props.getUserSettings();
        settings->setValue ("recordInput", idx);
        settings->setValue ("recordMonitor", monitorPair);
        settings->setValue ("recordOffsetMs", extra);
        startRecording (choices[(size_t) idx].first, choices[(size_t) idx].second, bankMode, bankMode ? bankTrack : -1,
                        w->getTextEditorContents ("nombre").trim(), monitorPair, extra);
    }), true);
}

void MainComponent::startRecording (int inputL, int inputR, bool toBank, int bankTrack, const juce::String& name, int monitorPair, double extraOffsetMs)
{
    if (recording.active)
        return;
    const double sr = engine.getSampleRate();
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("secuencias_grabacion");
    tempDir.createDirectory();
    const auto file = tempDir.getNonexistentChildFile ("toma", ".wav", false);
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    juce::WavAudioFormat wav;
    const int channels = inputR >= 0 ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), sr, (unsigned) channels, 24, {}, 0) : nullptr);
    if (writer == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Grabar"), tr ("No se pudo crear el archivo temporal de la toma en ") + tempDir.getFullPathName());
        return;
    }
    stream.release();
    recording.thread = std::make_unique<juce::TimeSliceThread> ("Grabacion");
    recording.thread->startThread();
    recording.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), *recording.thread, (int) (sr * 8.0));   // 8 s de cola
    recording.tempFile = file;
    recording.songFolder = currentInfo() != nullptr ? currentInfo()->folder : juce::File();
    recording.name = name.isEmpty() ? (toBank ? tr ("Banco") : tr ("Grabación")) : name;
    recording.toBank = toBank;
    recording.bankTrack = bankTrack;
    recording.extraOffsetMs = extraOffsetMs;
    recording.active = true;
    recording.startedAt = juce::Time::getMillisecondCounter();

    RecordSetup setup;
    setup.writer = recording.writer.get();
    setup.inputL = inputL;
    setup.inputR = inputR;
    setup.monitor = monitorPair >= 0;
    setup.monitorPair = juce::jmax (0, monitorPair);
    setup.monitorGain = 1.0f;
    if (! toBank && engine.getPositionSeconds() >= songLengthSeconds() - 0.05)
        engine.seekSeconds (0.0);   // al final: la toma arranca desde el principio
    engine.setRecorder (setup);
    if (! toBank && ! engine.isPlaying())
        engine.play();
    recordBtn.setButtonText (tr ("Detener REC"));
    recordBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::red.darker (0.3f));
    sepLabel.setText (tr ("GRABANDO"), juce::dontSendNotification);
}

void MainComponent::stopRecording (bool discard)
{
    if (! recording.active)
        return;
    recording.active = false;
    const auto startPos = engine.getRecordStartPosition();
    const auto recorded = engine.getRecordedSamples();
    const auto dropped = engine.getDroppedSamples();
    const double sr = engine.getSampleRate();
    const auto latency = recorder::compensation (engine.getInputLatency(), engine.getOutputLatency(), recording.extraOffsetMs, sr);
    engine.clearRecorder();        // espera a que el callback deje de usar el writer
    recording.writer.reset();      // vacía la cola y cierra el archivo
    if (recording.thread != nullptr)
        recording.thread->stopThread (3000);
    recording.thread.reset();
    recordBtn.setButtonText ("Grabar");
    recordBtn.removeColour (juce::TextButton::buttonColourId);

    const auto temp = recording.tempFile;
    const auto name = recording.name;
    const bool toBank = recording.toBank;
    const int bankTrack = recording.bankTrack;
    const auto songFolder = recording.songFolder;
    if (discard || recorded <= 0 || startPos < 0)
    {
        temp.deleteFile();
        sepLabel.setText (tr ("Grabación descartada"), juce::dontSendNotification);
        return;
    }
    const auto bankDir = toBank ? library.bankFolder (name) : juce::File();
    sepLabel.setText (tr ("Guardando la grabación..."), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    loaderPool.addJob ([this, safe, temp, name, toBank, bankTrack, songFolder, bankDir, startPos, latency, dropped, sr]
    {
        bool ok = false;
        juce::String message, fileName;
        try
        {
            if (toBank)
            {
                juce::AudioBuffer<float> buffer;
                const int count = triggers::readAudio (temp, sr, formatManager, buffer) ? recorder::saveBank (buffer, sr, bankDir) : 0;
                ok = count > 0;
                message = ok ? tr ("Banco «") + bankDir.getFileName() + tr ("»: ") + juce::String (count) + tr (" golpes guardados")
                             : tr ("No se detectaron golpes en la grabación (¿la entrada correcta? ¿nivel suficiente?)");
            }
            else
            {
                const auto out = Library::newRecordingFile (songFolder, name);
                ok = songFolder.isDirectory() && recorder::writeAligned (temp, startPos - latency, out, formatManager);
                fileName = out.getFileName();
                message = ok ? tr ("Pista «") + name + tr ("» grabada") : tr ("No se pudo guardar la grabación");
            }
        }
        catch (const std::exception& e)
        {
            message = tr ("Error al guardar la grabación: ") + juce::String (e.what());
        }
        if (dropped > 0)
            message += tr (" (atención: se perdieron ") + juce::String (dropped) + tr (" muestras porque la cola de grabación se llenó)");
        temp.deleteFile();
        juce::MessageManager::callAsync ([safe, ok, message, fileName, name, toBank, bankTrack, bankDir, songFolder]
        {
            if (auto* self = safe.getComponent())
                self->recordingFinished (ok, message, fileName, name, toBank, bankTrack, bankDir.getFileName(), songFolder);
        });
    });
}

void MainComponent::recordingFinished (bool ok, const juce::String& message, const juce::String& fileName, const juce::String& name,
                                       bool toBank, int bankTrack, const juce::String& bankName, const juce::File& songFolder)
{
    sepLabel.setText (message, juce::dontSendNotification);
    if (! ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Grabar"), message);
        return;
    }
    if (toBank)
    {
        {
            const juce::ScopedLock sl (bankCache.lock);
            bankCache.banks.clear();
        }
        auto* info = currentInfo();
        bool assigned = false;
        if (info != nullptr && currentSong != nullptr && juce::isPositiveAndBelow (bankTrack, (int) currentSong->tracks.size()))
        {
            const int stem = currentSong->tracks[(size_t) bankTrack]->stemIndex;
            if (juce::isPositiveAndBelow (stem, (int) info->stems.size()))
            {
                auto& t = info->stems[(size_t) stem].trigger;
                t.sound = "banco:" + bankName;
                t.enabled = true;
                library.saveSong (*info);
                syncTriggerMarks();
                requestRender();
                assigned = true;
            }
        }
        if (! assigned)
            sepLabel.setText (message + tr ("  ·  elígelo en la casilla del trigger de una pista: Sonido > ") + bankName, juce::dontSendNotification);
        return;
    }
    // Pista nueva en la canción grabada (que puede ya no ser la seleccionada)
    for (int i = 0; i < (int) library.songs.size(); ++i)
    {
        auto& s = library.songs[(size_t) i];
        if (s.folder != songFolder)
            continue;
        StemInfo si;
        si.name = name;
        si.fileName = fileName;
        si.songTime = true;
        s.stems.push_back (si);
        s.fitStemArrays();
        library.saveSong (s);
        if (i == currentIndex)
            loadSongAt (currentIndex);   // recarga con la pista nueva
        break;
    }
}

//==============================================================================
// Instrumentos VST3/AU (punto 3)

void MainComponent::instrumentsMenu()
{
    if (rack == nullptr)
        return;
    juce::PopupMenu m;
    const auto& slots = rack->getSlots();
    std::vector<int> ids;
    m.addSectionHeader (tr ("Instrumentos cargados (") + InstrumentRack::formatsAvailable() + ")");
    if (slots.empty())
        m.addItem (99, tr ("(ninguno)"), false);
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& s = slots[i];
        ids.push_back (s.id);
        juce::PopupMenu sub;
        sub.addItem (1000 + (int) i * 10 + 1, tr ("Abrir la ventana del instrumento"), s.lane != nullptr);
        sub.addItem (1000 + (int) i * 10 + 2, tr ("Quitar del rack"));
        juce::String label = s.description.name + " (" + s.description.pluginFormatName + ")";
        if (s.loading)
            label += tr ("  -  cargando...");
        else if (s.error.isNotEmpty())
            label += tr ("  -  error: ") + s.error;
        m.addSubMenu (label, sub);
    }
    m.addSeparator();
    juce::PopupMenu addMenu;
    const auto knownList = rack->knownInstruments();
    for (int k = 0; k < knownList.size(); ++k)
        addMenu.addItem (2000 + k, knownList[k].name + " (" + knownList[k].pluginFormatName + ")");
    if (knownList.isEmpty())
        addMenu.addItem (98, tr ("(no hay instrumentos conocidos: busca primero)"), false);
    m.addSubMenu (tr ("Añadir instrumento"), addMenu);
    if (rack->isScanning())
        m.addItem (4, tr ("Cancelar la búsqueda"));
    else
    {
        m.addItem (2, tr ("Buscar instrumentos instalados (") + InstrumentRack::formatsAvailable() + ")...");
        m.addItem (3, tr ("Buscar en una carpeta..."));
    }
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, safe, ids, knownList] (int result)
    {
        if (safe == nullptr || rack == nullptr || result == 0)
            return;
        if (result == 2)
            scanInstruments ({});
        else if (result == 3)
            pickFiles (tr ("Carpeta con plugins"), juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                       juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*", [this, safe] (const juce::Array<juce::File>& files)
            {
                if (safe != nullptr && ! files.isEmpty())
                    scanInstruments (juce::FileSearchPath (files[0].getFullPathName()));
            });
        else if (result == 4)
            rack->cancelScan();
        else if (result >= 2000 && result < 2000 + knownList.size())
        {
            sepLabel.setText (tr ("Cargando ") + knownList[result - 2000].name + "...", juce::dontSendNotification);
            rack->add (knownList[result - 2000], [safe] (int, const juce::String& error)
            {
                if (safe != nullptr)
                    safe->sepLabel.setText (error.isNotEmpty() ? tr ("No se pudo cargar el instrumento: ") + error : tr ("Instrumento cargado"),
                                            juce::dontSendNotification);
            });
        }
        else if (result >= 1000)
        {
            const int idx = (result - 1000) / 10, action = (result - 1000) % 10;
            if (idx < (int) ids.size())
            {
                if (action == 1)
                    rack->openEditor (ids[(size_t) idx]);
                else if (action == 2)
                {
                    rack->remove (ids[(size_t) idx]);
                    sepLabel.setText (tr ("Instrumento quitado del rack"), juce::dontSendNotification);
                }
            }
        }
    });
}

void MainComponent::scanInstruments (const juce::FileSearchPath& extraFolders)
{
    if (rack == nullptr)
        return;
    sepLabel.setText (tr ("Buscando instrumentos..."), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    rack->scan (extraFolders, [safe] (float p, const juce::String& name)
    {
        if (safe != nullptr)
            safe->sepLabel.setText (tr ("Buscando instrumentos... ") + juce::String ((int) (p * 100.0f)) + "%  " + name, juce::dontSendNotification);
    }, [safe] (int found)
    {
        if (safe == nullptr || safe->rack == nullptr)
            return;
        safe->rack->save();
        safe->sepLabel.setText (tr ("Búsqueda terminada: ") + juce::String (found) + tr (" plugins nuevos; ")
                                + juce::String (safe->rack->knownInstruments().size()) + tr (" instrumentos conocidos"), juce::dontSendNotification);
        safe->instrumentsMenu();
    });
}

void MainComponent::setInstrumentForCapture (const juce::String& path)
{
    if (rack == nullptr || ! juce::File::isAbsolutePath (path))
        return;
    const auto descriptions = rack->describeFile (juce::File (path));
    if (descriptions.isEmpty())
    {
        sepLabel.setText (tr ("No se encontró un plugin en ") + path, juce::dontSendNotification);
        return;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    rack->add (descriptions[0], [safe] (int id, const juce::String& error)
    {
        if (safe == nullptr)
            return;
        if (error.isNotEmpty())
        {
            safe->sepLabel.setText (tr ("No se pudo cargar el instrumento: ") + error, juce::dontSendNotification);
            return;
        }
        auto* info = safe->currentInfo();
        if (info == nullptr)
            return;
        for (auto& st : info->stems)
            if (Analyzer::isDrumsTrack (st.name, st.fileName) || st.name.containsIgnoreCase ("bombo") || st.fileName.containsIgnoreCase ("kick"))
            {
                st.trigger.enabled = true;
                st.trigger.keepAudio = false;
                st.trigger.sound = "vst:" + juce::String (id);
                break;
            }
        safe->library.saveSong (*info);
        safe->requestRender();
    });
}

//==============================================================================
// Pistas MIDI: los golpes de un stem congelados y editables, con un sonido por fila

int MainComponent::trackIndexForFile (const juce::String& fileName)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || fileName.isEmpty())
        return -1;
    for (size_t k = 0; k < currentSong->tracks.size(); ++k)
    {
        const int stem = currentSong->tracks[k]->stemIndex;
        if (juce::isPositiveAndBelow (stem, (int) info->stems.size()) && info->stems[(size_t) stem].fileName == fileName)
            return (int) k;
    }
    return -1;
}

std::shared_ptr<SampleBankData> MainComponent::bankFor (const juce::String& bankName)
{
    const double sr = engine.getSampleRate();
    const auto key = bankName + "@" + juce::String (sr);
    {
        const juce::ScopedLock sl (bankCache.lock);
        auto it = bankCache.banks.find (key);
        if (it != bankCache.banks.end())
            return it->second;
    }
    auto bank = triggers::loadBank (library.bankFolder (bankName), sr, formatManager);
    if (bank != nullptr)
    {
        const juce::ScopedLock sl (bankCache.lock);
        bankCache.banks[key] = bank;
    }
    return bank;
}

juce::String MainComponent::soundLabel (const juce::String& sound)
{
    if (sound.startsWith ("banco:"))
        return sound.fromFirstOccurrenceOf ("banco:", false, false);
    if (sound.startsWith ("vst:"))
    {
        const auto name = rack != nullptr ? rack->nameOf (sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue()) : juce::String();
        return name.isNotEmpty() ? name : tr ("instrumento ausente");
    }
    return tr ("sin sonido");
}

MidiPad* MainComponent::padForLane (SongInfo& info, const SamplerLane& lane) const
{
    if (info.folder != midiSamplersFolder || ! juce::isPositiveAndBelow (lane.midiTrack, (int) info.midiTracks.size()))
        return nullptr;
    auto& mt = info.midiTracks[(size_t) lane.midiTrack];
    if (mt.sourceFile != lane.midiSource || ! juce::isPositiveAndBelow (lane.midiPad, (int) mt.pads.size()))
        return nullptr;
    auto& pad = mt.pads[(size_t) lane.midiPad];
    return pad.note == lane.note ? &pad : nullptr;
}

void MainComponent::pullMidiControls()
{
    // Lo que se movió en el mezclador (fader, mute, salida) pasa a las filas antes de rehacer las líneas. Solo a la fila de
    // la que salió cada línea: tras quitar una fila o una pista, o al cambiar de canción, los índices ya no son los mismos
    auto* info = currentInfo();
    if (info == nullptr || midiSamplers == nullptr)
        return;
    for (auto& lane : midiSamplers->lanes)
        if (auto* pad = padForLane (*info, *lane))
        {
            const float g = lane->control.gain.load();
            pad->gainDb = g <= 0.0f ? -60.0f : juce::Decibels::gainToDecibels (g, -60.0f);
            pad->muted = lane->control.muted.load();
            pad->outputPair = lane->control.outputPair.load();
        }
}

void MainComponent::applyMidiTracks()
{
    auto* info = currentInfo();
    // El solo no se guarda: se conserva por pista y nota mientras se rehacen las líneas
    std::vector<std::pair<juce::String, int>> soloed;
    if (midiSamplers != nullptr && info != nullptr)
        for (auto& lane : midiSamplers->lanes)
            if (lane->control.solo.load() && padForLane (*info, *lane) != nullptr)
                soloed.push_back ({ info->midiTracks[(size_t) lane->midiTrack].name, lane->note });
    pullMidiControls();
    if (info == nullptr || currentSong == nullptr)
    {
        midiSamplers.reset();
        midiSamplersFolder = juce::File();
        engine.setMidiSamplers (nullptr);
        mixer.setMidiSamplers (nullptr);
        timeline.setMidiLanes ({});
        return;
    }
    const double sr = engine.getSampleRate();
    auto toPlayback = [this] (double t) { return timeMap.toPlayback (t); };
    auto set = std::make_shared<SamplerSet>();
    std::vector<TimelineView::MidiLaneView> views;
    for (size_t m = 0; m < info->midiTracks.size(); ++m)
    {
        const auto& mt = info->midiTracks[m];
        TimelineView::MidiLaneView view;
        view.name = mt.name;
        view.muted = mt.muted;
        view.afterTrack = trackIndexForFile (mt.sourceFile);
        for (auto& pad : mt.pads)
            view.pads.push_back ({ pad.name, pad.note, soundLabel (pad.sound), pad.muted });
        for (auto& h : mt.hits)
            view.hits.push_back ({ toPlayback (h.seconds), h.note, h.velocity });
        views.push_back (std::move (view));

        // Una pista silenciada conserva sus líneas (sin disparar y con el canal a 0 con rampa): así lo que sonaba se
        // apaga sin corte en vez de seguir desde el conjunto anterior
        for (size_t p = 0; p < mt.pads.size(); ++p)
        {
            const auto& pad = mt.pads[p];
            const bool isBank = pad.sound.startsWith ("banco:"), isInstrument = pad.sound.startsWith ("vst:");
            if (! isBank && ! isInstrument)
                continue;
            auto lane = std::make_unique<SamplerLane>();
            lane->name = mt.name + tr (" · ") + pad.name;
            lane->stemIndex = -1;
            lane->midiTrack = (int) m;
            lane->midiPad = (int) p;
            lane->midiSource = mt.sourceFile;
            lane->silenced = mt.muted;
            lane->rawVelocity = true;
            lane->note = pad.note;
            lane->events = miditrack::eventsForNote (mt, pad.note, sr, toPlayback);
            if (isBank)
                lane->bank = bankFor (pad.sound.fromFirstOccurrenceOf ("banco:", false, false));
            else
                lane->instrumentId = pad.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue();
            lane->control.name = lane->name;
            lane->control.stemIndex = -1;
            lane->control.gain = pad.gainDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (pad.gainDb);
            lane->control.smoothedGain = lane->control.gain.load();
            lane->control.muted = pad.muted;
            lane->control.outputPair = pad.outputPair;
            for (auto& s : soloed)
                if (s.first == mt.name && s.second == pad.note)
                    lane->control.solo = true;
            set->lanes.push_back (std::move (lane));
        }
    }
    midiSamplers = set;
    midiSamplersFolder = info->folder;
    // El trigger en vivo de un stem con pista MIDI deja de disparar ya (el próximo render lo quita del conjunto)
    if (samplers != nullptr)
        for (auto& lane : samplers->lanes)
            lane->superseded = juce::isPositiveAndBelow (lane->stemIndex, (int) info->stems.size())
                               && isMidiSource (*info, info->stems[(size_t) lane->stemIndex]);
    engine.setMidiSamplers (set);
    mixer.setMidiSamplers (set);
    timeline.setMidiLanes (views);
    refreshReplaced();
}

void MainComponent::midiEdited (int midiTrack, const std::vector<int>& selection)
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    if (juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        info->midiTracks[(size_t) midiTrack].sortHits();
    library.saveSong (*info);
    applyMidiTracks();
    syncTriggerMarks();
    if (juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        timeline.selectMidiHits (midiTrack, selection);
}

void MainComponent::createMidiTrack (int track)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || arrangedSong == nullptr
        || ! juce::isPositiveAndBelow (track, (int) currentSong->tracks.size()) || ! arrangementReady())
        return;
    const int stem = currentSong->tracks[(size_t) track]->stemIndex;
    if (! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
        return;
    const LoadedTrack* source = nullptr;
    for (auto& t : arrangedSong->tracks)
        if (t->stemIndex == stem)
            source = t.get();
    if (source == nullptr)
        return;
    // Los golpes se detectan sobre el arreglo (la línea de tiempo de la canción, sin estirar)
    const auto& st = info->stems[(size_t) stem];
    const double sr = engine.getSampleRate();
    const auto events = triggers::detect (source->buffer, sr, st.trigger.thresholdDb, st.trigger.sensitivity, st.trigger.minMs);
    pushUndo();
    info->midiTracks.push_back (miditrack::fromDetection (events, sr, st));   // reemplaza al trigger en vivo de este stem (triggersOf)
    library.saveSong (*info);
    requestRender();   // los triggers en vivo cambiaron
    midiEdited ((int) info->midiTracks.size() - 1, {});
    sepLabel.setText (tr ("Pista MIDI «") + info->midiTracks.back().name + tr ("»: ") + juce::String ((int) events.size())
                          + tr (" golpes. Doble clic agrega, arrastrar mueve, Alt + arrastre cambia la fuerza, clic derecho para más"),
                      juce::dontSendNotification);
}

double MainComponent::snapMidiTime (int midiTrack, double seconds, int)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        return seconds;
    const int mode = cutMode();
    if (mode == 2)
        return miditrack::snapToSubdivision (mappedAnalysis(), seconds, 4);   // a la semicorchea más cercana
    if (mode == 3)
    {
        // A la transiente de la pista de origen (la batería separada, por ejemplo), a menos de 50 ms
        const int lane = trackIndexForFile (info->midiTracks[(size_t) midiTrack].sourceFile);
        const double onset = lane >= 0 ? onsetNear (timeMap.toOriginal (seconds), lane, 0.05) : -1.0;
        return onset >= 0.0 ? timeMap.toPlayback (onset) : seconds;
    }
    return seconds;
}

void MainComponent::importBank (std::function<void (const juce::String& bankName)> onDone)
{
    pickFiles (tr ("Elige los wav de los golpes (de suave a fuerte, o varios parecidos)"),
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
               juce::File::getSpecialLocation (juce::File::userMusicDirectory), Library::audioFilePatterns(),
               [this, onDone] (const juce::Array<juce::File>& files)
    {
        if (files.isEmpty())
            return;
        askText (tr ("Nombre del banco de muestras"), files[0].getParentDirectory().getFileName(), [this, files, onDone] (const juce::String& name)
        {
            if (name.trim().isEmpty())
                return;
            const auto folder = library.bankFolder (name.trim());
            folder.createDirectory();
            int copied = 0;
            for (auto& f : files)
                if (Library::isAudioFile (f) && f.copyFileTo (folder.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false)))
                    ++copied;
            if (copied == 0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Banco de muestras"), tr ("No se pudo copiar ningún archivo."));
                return;
            }
            {
                const juce::ScopedLock sl (bankCache.lock);
                bankCache.banks.clear();   // el banco cambió: se vuelve a cargar
            }
            sepLabel.setText (tr ("Banco «") + folder.getFileName() + tr ("»: ") + juce::String (copied) + tr (" muestras"), juce::dontSendNotification);
            if (onDone)
                onDone (folder.getFileName());
        });
    });
}


void MainComponent::midiHeaderMenu (int midiTrack)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        return;
    const auto& mt = info->midiTracks[(size_t) midiTrack];
    const bool hasSource = trackIndexForFile (mt.sourceFile) >= 0;
    juce::PopupMenu m;
    m.addSectionHeader (mt.name + tr (" · ") + juce::String ((int) mt.hits.size()) + tr (" golpes"));
    m.addItem (1, tr ("Renombrar..."));
    m.addItem (2, tr ("Silenciar la pista de origen mientras suena"), hasSource, mt.muteSource);
    m.addItem (3, tr ("Agregar una fila (otro sonido)..."));
    m.addItem (5, tr ("Elegir todos los golpes"), ! mt.hits.empty());
    m.addSeparator();
    m.addItem (4, tr ("Volver a detectar toda la pista (reemplaza todos los golpes)"), hasSource);
    m.addItem (6, tr ("Eliminar la pista MIDI"));
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)), [safe, midiTrack] (int result)
    {
        auto* self = safe.getComponent();
        auto* inf = self != nullptr ? self->currentInfo() : nullptr;
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (midiTrack, (int) inf->midiTracks.size()))
            return;
        auto& track = inf->midiTracks[(size_t) midiTrack];
        if (result == 1)
            self->askText (tr ("Nombre de la pista MIDI"), track.name, [safe, midiTrack] (const juce::String& name)
            {
                auto* s2 = safe.getComponent();
                auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
                if (in2 == nullptr || name.trim().isEmpty() || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size()))
                    return;
                in2->midiTracks[(size_t) midiTrack].name = name.trim();
                s2->midiEdited (midiTrack, s2->timeline.getSelectedMidiHits (midiTrack));
            });
        else if (result == 2)
        {
            track.muteSource = ! track.muteSource;
            self->midiEdited (midiTrack, self->timeline.getSelectedMidiHits (midiTrack));
        }
        else if (result == 3)
            self->midiHeaderMenuAddPad (midiTrack);
        else if (result == 4)
        {
            const int lane = self->trackIndexForFile (track.sourceFile);
            if (lane < 0 || self->arrangedSong == nullptr || ! juce::isPositiveAndBelow (lane, (int) self->arrangedSong->tracks.size())
                || ! self->arrangementReady())
                return;
            const auto& src = *self->arrangedSong->tracks[(size_t) lane];
            const int stem = src.stemIndex;
            const auto tg = juce::isPositiveAndBelow (stem, (int) inf->stems.size()) ? inf->stems[(size_t) stem].trigger : TriggerSettings();
            const double sr = self->engine.getSampleRate();
            const auto events = triggers::detect (src.buffer, sr, tg.thresholdDb, tg.sensitivity, tg.minMs);
            self->pushUndo();
            miditrack::replaceRange (track, 0.0, std::numeric_limits<double>::max(), events, sr, -1);
            self->midiEdited (midiTrack, {});
            self->sepLabel.setText (tr ("Golpes detectados de nuevo: ") + juce::String ((int) events.size()) + tr (" (Ctrl+Z deshace)"), juce::dontSendNotification);
        }
        else if (result == 5)
        {
            std::vector<int> all;
            for (int i = 0; i < (int) track.hits.size(); ++i)
                all.push_back (i);
            self->timeline.selectMidiHits (midiTrack, all);
        }
        else if (result == 6)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("¿Eliminar la pista MIDI?"),
                tr ("«") + track.name + tr ("» y sus ") + juce::String ((int) track.hits.size()) + tr (" golpes se eliminan (Ctrl+Z deshace)."),
                "Eliminar", "Cancelar", self, juce::ModalCallbackFunction::create ([safe, midiTrack] (int ok)
                {
                    auto* s2 = safe.getComponent();
                    auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
                    if (in2 == nullptr || ok == 0 || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size()))
                        return;
                    s2->pushUndo();
                    in2->midiTracks.erase (in2->midiTracks.begin() + midiTrack);
                    s2->midiEdited (-1, {});
                    s2->requestRender();   // el trigger en vivo del stem vuelve (triggersOf cambió)
                }));
        }
    });
}

void MainComponent::midiPadMenu (int midiTrack, int padIndex)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        return;
    const auto& mt = info->midiTracks[(size_t) midiTrack];
    if (! juce::isPositiveAndBelow (padIndex, (int) mt.pads.size()))
        return;
    const auto& pad = mt.pads[(size_t) padIndex];

    juce::PopupMenu m;
    m.addSectionHeader (pad.name + " (" + juce::MidiMessage::getMidiNoteName (pad.note, true, true, 3) + ", " + juce::String (pad.note) + tr (") · ") + soundLabel (pad.sound));
    // Sonido: bancos grabados o importados, e instrumentos del rack
    juce::PopupMenu sound;
    const auto banks = library.listBanks();
    sound.addSectionHeader (tr ("Bancos de muestras grabadas"));
    for (int i = 0; i < banks.size(); ++i)
        sound.addItem (100 + i, banks[i], true, pad.sound == "banco:" + banks[i]);
    if (banks.isEmpty())
        sound.addItem (99, tr ("(no hay bancos todavía)"), false);
    sound.addItem (2, tr ("Importar muestras (wav) como banco nuevo..."));
    sound.addSeparator();
    sound.addSectionHeader (tr ("Instrumentos ") + InstrumentRack::formatsAvailable());
    std::vector<int> instrumentIds;
    if (rack != nullptr)
        for (auto& slot : rack->getSlots())
        {
            instrumentIds.push_back (slot.id);
            sound.addItem (300 + (int) instrumentIds.size() - 1, slot.description.name + (slot.lane == nullptr ? tr (" (no cargado)") : juce::String()),
                           true, pad.sound == "vst:" + juce::String (slot.id));
        }
    sound.addItem (298, tr ("Añadir o buscar instrumentos..."));
    sound.addSeparator();
    sound.addItem (297, tr ("Sin sonido"), true, pad.sound.isEmpty());
    m.addSubMenu (tr ("Sonido: ") + soundLabel (pad.sound), sound);
    // Nota (la que recibe un instrumento; con un banco solo distingue la fila)
    juce::PopupMenu notes;
    addNoteItems (notes, pad.note, [&mt, padIndex] (int n)
    {
        const int owner = miditrack::padIndexForNote (mt, n);
        return owner < 0 || owner == padIndex;   // dos filas no comparten nota
    });
    notes.addSeparator();
    notes.addItem (499, tr ("Otra nota (0 a 127)..."));
    m.addSubMenu (tr ("Nota: ") + juce::String (pad.note), notes);
    m.addItem (14, tr ("Escuchar (un golpe con este sonido y esta nota)"), pad.sound.isNotEmpty());
    m.addItem (10, tr ("Renombrar la fila..."));
    m.addItem (11, tr ("Silenciar la fila"), true, pad.muted);
    m.addItem (12, tr ("Elegir todos los golpes de esta fila"));
    m.addItem (13, tr ("Agregar una fila (otro sonido)..."));
    if (mt.pads.size() > 1)
    {
        juce::PopupMenu remove;
        for (int j = 0; j < (int) mt.pads.size(); ++j)
            if (j != padIndex)
                remove.addItem (600 + j, tr ("y pasar sus golpes a «") + mt.pads[(size_t) j].name + tr ("»"));
        remove.addItem (699, tr ("y borrar sus golpes"));
        m.addSubMenu (tr ("Quitar esta fila"), remove);
    }

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [safe, midiTrack, padIndex, banks, instrumentIds] (int result)
    {
        auto* self = safe.getComponent();
        auto* inf = self != nullptr ? self->currentInfo() : nullptr;
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (midiTrack, (int) inf->midiTracks.size()))
            return;
        auto& track = inf->midiTracks[(size_t) midiTrack];
        if (! juce::isPositiveAndBelow (padIndex, (int) track.pads.size()))
            return;
        auto& p = track.pads[(size_t) padIndex];
        auto setSound = [safe, midiTrack, padIndex] (const juce::String& s)
        {
            auto* s2 = safe.getComponent();
            auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
            if (in2 == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size())
                || ! juce::isPositiveAndBelow (padIndex, (int) in2->midiTracks[(size_t) midiTrack].pads.size()))
                return;
            in2->midiTracks[(size_t) midiTrack].pads[(size_t) padIndex].sound = s;
            s2->midiEdited (midiTrack, s2->timeline.getSelectedMidiHits (midiTrack));
        };
        if (result >= 100 && result < 100 + banks.size())
            setSound ("banco:" + banks[result - 100]);
        else if (result >= 300 && result < 300 + (int) instrumentIds.size())
            setSound ("vst:" + juce::String (instrumentIds[(size_t) (result - 300)]));
        else if (result == 297)
            setSound ({});
        else if (result == 2)
            self->importBank ([setSound] (const juce::String& bank) { setSound ("banco:" + bank); });
        else if (result == 298)
            self->instrumentsMenu();
        else if (result >= noteMenuBase && result < noteMenuBase + 128)
        {
            const int n = result - noteMenuBase;
            const int owner = miditrack::padIndexForNote (track, n);
            if (owner >= 0 && owner != padIndex)
                return;
            self->pushUndo();
            for (auto& h : track.hits)
                if (h.note == p.note)
                    h.note = n;   // los golpes de la fila siguen en ella
            p.note = n;
            self->midiEdited (midiTrack, self->timeline.getSelectedMidiHits (midiTrack));
        }
        else if (result == 499)
            self->askText (tr ("Nota MIDI de la fila (0 a 127)"), juce::String (p.note), [safe, midiTrack, padIndex] (const juce::String& text)
            {
                auto* s2 = safe.getComponent();
                auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
                if (in2 == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size()))
                    return;
                auto& tr2 = in2->midiTracks[(size_t) midiTrack];
                if (! juce::isPositiveAndBelow (padIndex, (int) tr2.pads.size()))
                    return;
                const int n = juce::jlimit (0, 127, text.getIntValue());
                const int owner = miditrack::padIndexForNote (tr2, n);
                if (owner >= 0 && owner != padIndex)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Nota MIDI"),
                                                            tr ("Esa nota ya la usa la fila «") + tr2.pads[(size_t) owner].name + tr ("»."));
                    return;
                }
                s2->pushUndo();
                for (auto& h : tr2.hits)
                    if (h.note == tr2.pads[(size_t) padIndex].note)
                        h.note = n;
                tr2.pads[(size_t) padIndex].note = n;
                s2->midiEdited (midiTrack, s2->timeline.getSelectedMidiHits (midiTrack));
            });
        else if (result == 10)
            self->askText (tr ("Nombre de la fila"), p.name, [safe, midiTrack, padIndex] (const juce::String& name)
            {
                auto* s2 = safe.getComponent();
                auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
                if (in2 == nullptr || name.trim().isEmpty() || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size())
                    || ! juce::isPositiveAndBelow (padIndex, (int) in2->midiTracks[(size_t) midiTrack].pads.size()))
                    return;
                in2->midiTracks[(size_t) midiTrack].pads[(size_t) padIndex].name = name.trim();
                s2->midiEdited (midiTrack, s2->timeline.getSelectedMidiHits (midiTrack));
            });
        else if (result == 11)
        {
            p.muted = ! p.muted;
            // El canal del mezclador también (si no, al rehacer las líneas se volvería a tomar su mute)
            if (self->midiSamplers != nullptr)
                for (auto& lane : self->midiSamplers->lanes)
                    if (self->padForLane (*inf, *lane) == &p)
                        lane->control.muted = p.muted;
            self->midiEdited (midiTrack, self->timeline.getSelectedMidiHits (midiTrack));
        }
        else if (result == 12)
        {
            std::vector<int> rowHits;
            for (int i = 0; i < (int) track.hits.size(); ++i)
                if (track.hits[(size_t) i].note == p.note)
                    rowHits.push_back (i);
            self->timeline.selectMidiHits (midiTrack, rowHits);
        }
        else if (result == 13)
            self->midiHeaderMenuAddPad (midiTrack);
        else if (result == 14)
            self->auditionMidiPad (midiTrack, padIndex);
        else if (result >= 600 && result <= 699)
        {
            const int target = result == 699 ? -1 : result - 600;
            self->pushUndo();
            miditrack::removePad (track, padIndex, target >= 0 && target < (int) track.pads.size() ? track.pads[(size_t) target].note : -1);
            self->midiEdited (midiTrack, {});
        }
    });
}

void MainComponent::auditionMidiPad (int midiTrack, int padIndex)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size())
        || ! juce::isPositiveAndBelow (padIndex, (int) info->midiTracks[(size_t) midiTrack].pads.size()))
        return;
    const auto& mt = info->midiTracks[(size_t) midiTrack];
    const auto& pad = mt.pads[(size_t) padIndex];
    const auto row = tr ("La fila «") + pad.name + tr ("»");
    juce::String why;
    if (pad.sound.isEmpty())
        why = row + tr (" no tiene sonido: elígelo con clic derecho en su nombre");
    else if (mt.muted || pad.muted)
        why = row + tr (" está silenciada");
    else if (pad.sound.startsWith ("vst:") && (rack == nullptr || ! rack->has (pad.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue())))
        why = row + tr (" usa un instrumento que no está cargado");
    if (why.isNotEmpty())
    {
        sepLabel.setText (why, juce::dontSendNotification);
        return;
    }
    engine.audition (-1, midiTrack, padIndex, miditrack::defaultVelocity);
    sepLabel.setText (tr ("Escuchar: «") + pad.name + tr ("», nota ") + juce::String (pad.note) + " ("
                          + juce::MidiMessage::getMidiNoteName (pad.note, true, true, 3) + tr (") en ") + soundLabel (pad.sound),
                      juce::dontSendNotification);
}

void MainComponent::auditionTrigger (int stem)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
        return;
    const auto& st = info->stems[(size_t) stem];
    const auto& tg = st.trigger;
    bool hasLane = false;
    if (samplers != nullptr)
        for (auto& lane : samplers->lanes)
            hasLane = hasLane || (lane->stemIndex == stem && lane->midiTrack < 0);
    juce::String why;
    if (isMidiSource (*info, st))
        why = tr ("El trigger de «") + st.name + tr ("» lo reemplaza su pista MIDI: escucha sus filas");
    else if (! tg.enabled || tg.sound.isEmpty())
        why = tr ("El trigger de «") + st.name + tr ("» está apagado o sin sonido");
    else if (tg.sound.startsWith ("vst:") && (rack == nullptr || ! rack->has (tg.sound.fromFirstOccurrenceOf ("vst:", false, false).getIntValue())))
        why = tr ("El instrumento del trigger no está cargado");
    else if (! hasLane)
        why = tr ("El trigger se está preparando: prueba en un momento");
    if (why.isNotEmpty())
    {
        sepLabel.setText (why, juce::dontSendNotification);
        return;
    }
    engine.audition (stem, -1, -1, miditrack::defaultVelocity);
    sepLabel.setText (tr ("Escuchar: trigger de «") + st.name + tr ("», nota ") + juce::String (tg.note) + tr (" en ") + soundLabel (tg.sound),
                      juce::dontSendNotification);
}

void MainComponent::midiHeaderMenuAddPad (int midiTrack)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        return;
    auto& track = info->midiTracks[(size_t) midiTrack];
    int note = -1;
    for (int candidate : { 63, 62, 64, 60, 61, 65, 66, 56, 70, 73, 75, 36, 38, 45, 42 })
        if (note < 0 && miditrack::padIndexForNote (track, candidate) < 0)
            note = candidate;
    for (int candidate = 35; note < 0 && candidate < 128; ++candidate)
        if (miditrack::padIndexForNote (track, candidate) < 0)
            note = candidate;
    if (note < 0)
        return;
    pushUndo();
    MidiPad pad;
    pad.name = miditrack::drumNoteName (note);
    pad.note = note;
    track.pads.push_back (pad);
    const int newPad = (int) track.pads.size() - 1;
    midiEdited (midiTrack, timeline.getSelectedMidiHits (midiTrack));
    sepLabel.setText (tr ("Fila «") + pad.name + tr ("» agregada: elige su sonido"), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe, midiTrack, newPad] { if (auto* s2 = safe.getComponent()) s2->midiPadMenu (midiTrack, newPad); });
}

void MainComponent::midiHitsMenu (int midiTrack, std::vector<int> hits, int row, double seconds)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) info->midiTracks.size()))
        return;
    const auto& mt = info->midiTracks[(size_t) midiTrack];
    const bool hasBeats = info->analysis.beats.size() >= 2;
    juce::PopupMenu m;
    if (juce::isPositiveAndBelow (row, (int) mt.pads.size()))
        m.addItem (1, tr ("Agregar un golpe aquí (") + mt.pads[(size_t) row].name + ")");
    if (! hits.empty())
    {
        m.addSectionHeader (juce::String ((int) hits.size()) + (hits.size() == 1 ? tr (" golpe elegido") : tr (" golpes elegidos")));
        m.addItem (2, tr ("Eliminar (Supr)"));
        juce::PopupMenu toRow;
        for (int j = 0; j < (int) mt.pads.size(); ++j)
            toRow.addItem (100 + j, mt.pads[(size_t) j].name + " (" + juce::String (mt.pads[(size_t) j].note) + ")");
        m.addSubMenu (tr ("Pasar a la fila"), toRow, mt.pads.size() > 1);
        juce::PopupMenu vel;
        vel.addItem (200, tr ("Más fuerte (+10)"));
        vel.addItem (201, tr ("Más suave (-10)"));
        vel.addSeparator();
        const int fixed[] = { 127, 110, 100, 80, 60, 40 };
        for (int k = 0; k < 6; ++k)
            vel.addItem (210 + k, juce::String (fixed[k]));
        vel.addItem (220, tr ("Otra..."));
        m.addSubMenu (tr ("Fuerza (velocidad MIDI)"), vel);
        juce::PopupMenu quant;
        quant.addItem (301, tr ("A los tiempos"));
        quant.addItem (302, tr ("A las corcheas"));
        quant.addItem (303, tr ("A los tresillos"));
        quant.addItem (304, tr ("A las semicorcheas"));
        m.addSubMenu (tr ("Cuantizar"), quant, hasBeats);
    }
    m.addSeparator();
    m.addItem (5, tr ("Elegir todos los golpes de esta fila"), juce::isPositiveAndBelow (row, (int) mt.pads.size()));
    m.addItem (6, tr ("Elegir todos los golpes"));
    const bool hasSource = trackIndexForFile (mt.sourceFile) >= 0;
    m.addItem (7, hits.empty() ? tr ("Volver a detectar esta fila entre los marcadores de alrededor")
                               : tr ("Volver a detectar esta fila en el tramo elegido"),
               hasSource && juce::isPositiveAndBelow (row, (int) mt.pads.size()));

    const double songSeconds = timeMap.toOriginal (seconds);
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [safe, midiTrack, hits, row, seconds, songSeconds] (int result)
    {
        auto* self = safe.getComponent();
        auto* inf = self != nullptr ? self->currentInfo() : nullptr;
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (midiTrack, (int) inf->midiTracks.size()))
            return;
        auto& track = inf->midiTracks[(size_t) midiTrack];
        const bool rowOk = juce::isPositiveAndBelow (row, (int) track.pads.size());
        if (result == 1 && rowOk)
        {
            const int note = track.pads[(size_t) row].note;
            const double t = self->timeMap.toOriginal (self->snapMidiTime (midiTrack, seconds, note));
            self->pushUndo();
            const int i = miditrack::addHit (track, t, note, miditrack::defaultVelocity);
            self->midiEdited (midiTrack, { i });
        }
        else if (result == 2)
        {
            self->pushUndo();
            miditrack::removeHits (track, hits);
            self->midiEdited (midiTrack, {});
        }
        else if (result >= 100 && result < 100 + (int) track.pads.size())
        {
            self->pushUndo();
            self->midiEdited (midiTrack, miditrack::setNote (track, hits, track.pads[(size_t) (result - 100)].note));
        }
        else if (result == 200 || result == 201)
        {
            self->pushUndo();
            miditrack::changeVelocity (track, hits, result == 200 ? 10 : -10);
            self->midiEdited (midiTrack, hits);
        }
        else if (result >= 210 && result < 216)
        {
            const int fixed[] = { 127, 110, 100, 80, 60, 40 };
            self->pushUndo();
            miditrack::setVelocity (track, hits, fixed[result - 210]);
            self->midiEdited (midiTrack, hits);
        }
        else if (result == 220)
            self->askText (tr ("Fuerza (velocidad MIDI, 1 a 127)"), juce::String (miditrack::defaultVelocity), [safe, midiTrack, hits] (const juce::String& text)
            {
                auto* s2 = safe.getComponent();
                auto* in2 = s2 != nullptr ? s2->currentInfo() : nullptr;
                if (in2 == nullptr || ! juce::isPositiveAndBelow (midiTrack, (int) in2->midiTracks.size()))
                    return;
                s2->pushUndo();
                miditrack::setVelocity (in2->midiTracks[(size_t) midiTrack], hits, juce::jlimit (1, 127, text.getIntValue()));
                s2->midiEdited (midiTrack, hits);
            });
        else if (result >= 301 && result <= 304)
        {
            const int subdivisions[] = { 1, 2, 3, 4 };
            self->pushUndo();
            self->midiEdited (midiTrack, miditrack::quantize (track, hits, inf->analysis, subdivisions[result - 301]));
        }
        else if (result == 5 && rowOk)
        {
            std::vector<int> rowHits;
            for (int i = 0; i < (int) track.hits.size(); ++i)
                if (track.hits[(size_t) i].note == track.pads[(size_t) row].note)
                    rowHits.push_back (i);
            self->timeline.selectMidiHits (midiTrack, rowHits);
        }
        else if (result == 6)
        {
            std::vector<int> all;
            for (int i = 0; i < (int) track.hits.size(); ++i)
                all.push_back (i);
            self->timeline.selectMidiHits (midiTrack, all);
        }
        else if (result == 7 && rowOk)
        {
            // Tramo: el de los golpes elegidos, o entre los marcadores que rodean el clic (o toda la canción)
            double from = 0.0, to = std::numeric_limits<double>::max();
            if (! hits.empty())
            {
                from = std::numeric_limits<double>::max();
                to = 0.0;
                for (int i : hits)
                    if (juce::isPositiveAndBelow (i, (int) track.hits.size()))
                    {
                        from = juce::jmin (from, track.hits[(size_t) i].seconds - 0.05);
                        to = juce::jmax (to, track.hits[(size_t) i].seconds + 0.05);
                    }
            }
            else
                for (auto& mk : inf->markers)
                {
                    if (mk.seconds <= songSeconds)
                        from = mk.seconds;
                    else
                    {
                        to = mk.seconds;
                        break;
                    }
                }
            const int lane = self->trackIndexForFile (track.sourceFile);
            if (lane < 0 || self->arrangedSong == nullptr || ! juce::isPositiveAndBelow (lane, (int) self->arrangedSong->tracks.size()) || to <= from
                || ! self->arrangementReady())
                return;
            const auto& src = *self->arrangedSong->tracks[(size_t) lane];
            const auto tg = juce::isPositiveAndBelow (src.stemIndex, (int) inf->stems.size()) ? inf->stems[(size_t) src.stemIndex].trigger : TriggerSettings();
            const double sr = self->engine.getSampleRate();
            const auto events = triggers::detect (src.buffer, sr, tg.thresholdDb, tg.sensitivity, tg.minMs);
            self->pushUndo();
            miditrack::replaceRange (track, juce::jmax (0.0, from), to, events, sr, track.pads[(size_t) row].note);
            self->midiEdited (midiTrack, {});
            self->sepLabel.setText (tr ("Fila detectada de nuevo en el tramo (Ctrl+Z deshace)"), juce::dontSendNotification);
        }
    });
}

void MainComponent::createMidiTrackForCapture()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    for (int k = 0; k < (int) currentSong->tracks.size(); ++k)
    {
        const int stem = currentSong->tracks[(size_t) k]->stemIndex;
        if (! juce::isPositiveAndBelow (stem, (int) info->stems.size()))
            continue;
        const auto& st = info->stems[(size_t) stem];
        if (! (Analyzer::isDrumsTrack (st.name, st.fileName) || st.name.containsIgnoreCase ("tom") || st.name.containsIgnoreCase ("bombo")))
            continue;
        createMidiTrack (k);
        // Para la captura: una segunda fila (conga) con los golpes de 5 a 10 s de la canción
        auto& mt = info->midiTracks.back();
        MidiPad conga;
        conga.name = miditrack::drumNoteName (63);
        conga.note = 63;
        mt.pads.push_back (conga);
        std::vector<int> moved;
        for (int i = 0; i < (int) mt.hits.size(); ++i)
            if (mt.hits[(size_t) i].seconds >= 5.0 && mt.hits[(size_t) i].seconds < 10.0)
                moved.push_back (i);
        const auto selection = miditrack::setNote (mt, moved, 63);
        midiEdited ((int) info->midiTracks.size() - 1, selection);
        return;
    }
}

//==============================================================================
// Armar mix (antes de separar): canciones originales, tramos cortados en los tiempos y alineados

int MainComponent::mixSourceIndex (const juce::String& fileName) const
{
    if (mixProject != nullptr)
        for (int i = 0; i < (int) mixProject->sources.size(); ++i)
            if (mixProject->sources[(size_t) i].fileName == fileName)
                return i;
    return -1;
}

MainComponent::MixSnapshot MainComponent::mixSnapshot() const
{
    MixSnapshot s;
    if (mixProject == nullptr)
        return s;
    s.bpm = mixProject->bpm;
    s.keepTempos = mixProject->keepTempos;
    s.segments = mixProject->segments;
    for (auto& src : mixProject->sources)
        s.analyses.push_back (src.analysis);
    return s;
}

void MainComponent::mixMenu()
{
    const auto list = MixProject::entries (library.mixesFolder());
    // Nombre visible; si dos se llaman igual, también la carpeta para distinguirlos
    auto labelOf = [&list] (size_t i)
    {
        int same = 0;
        for (auto& e : list)
            same += e.name == list[i].name ? 1 : 0;
        return same > 1 ? list[i].name + " (" + list[i].folder.getFileName() + ")" : list[i].name;
    };
    juce::PopupMenu m;
    m.addItem (1, tr ("Mix nuevo..."));
    if (! list.empty())
    {
        m.addSectionHeader (tr ("Abrir un mix"));
        for (size_t i = 0; i < list.size(); ++i)
            m.addItem (100 + (int) i, labelOf (i), true, mixProject != nullptr && mixProject->folder == list[i].folder);
        juce::PopupMenu del;
        for (size_t i = 0; i < list.size(); ++i)
            del.addItem (1000 + (int) i, labelOf (i));
        m.addSeparator();
        m.addSubMenu (tr ("Borrar un mix"), del);
    }
    if (mixEditor != nullptr)
    {
        m.addSeparator();
        m.addItem (2, tr ("Cerrar el mix"));
    }
    // Nombre propuesto para uno nuevo: "Mix N" que no exista
    juce::String proposed;
    for (int n = (int) list.size() + 1; proposed.isEmpty(); ++n)
    {
        const auto candidate = "Mix " + juce::String (n);
        bool used = false;
        for (auto& e : list)
            used = used || e.name.equalsIgnoreCase (candidate) || e.folder.getFileName().equalsIgnoreCase (candidate);
        if (! used)
            proposed = candidate;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mixBtn), [safe, list, proposed] (int result)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || result == 0)
            return;
        if (result == 1)
        {
            self->askText (tr ("Nombre del mix"), proposed, [safe] (const juce::String& name)
            {
                auto* s2 = safe.getComponent();
                if (s2 == nullptr || name.trim().isEmpty())
                    return;
                s2->library.mixesFolder().createDirectory();
                const auto folder = MixProject::create (s2->library.mixesFolder(), name.trim());
                if (folder == juce::File())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Armar mix"),
                                                            tr ("No se pudo crear la carpeta del mix en ") + s2->library.mixesFolder().getFullPathName());
                else
                    s2->openMix (folder);
            });
        }
        else if (result == 2)
            self->closeMix (true);
        else if (result >= 100 && result < 100 + (int) list.size())
            self->openMix (list[(size_t) (result - 100)].folder);
        else if (result >= 1000 && result < 1000 + (int) list.size())
        {
            const auto entry = list[(size_t) (result - 1000)];
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("¿Borrar el mix?"),
                tr ("«") + entry.name + tr ("» (sus canciones originales copiadas y tramos) se moverá a la papelera. Las canciones ya creadas con él no se tocan."),
                "Borrar", "Cancelar", self, juce::ModalCallbackFunction::create ([safe, entry] (int ok)
                {
                    auto* s2 = safe.getComponent();
                    if (s2 == nullptr || ok == 0)
                        return;
                    if (s2->mixProject != nullptr && s2->mixProject->folder == entry.folder)
                        s2->closeMix (true);
                    if (! Library::sendToTrash (entry.folder) && entry.folder.isDirectory())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Borrar el mix"),
                                                                tr ("No se pudo mover a la papelera:\n") + entry.folder.getFullPathName());
                }));
        }
    });
}

void MainComponent::openMix (const juce::File& folder)
{
    // El análisis y el nivelado se aplican a la canción cargada: si se descargara ahora, se perderían
    if (mixEditor == nullptr && (analyzer.getState() == Analyzer::State::running || isLeveling()))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Armar mix"),
            tr ("Espera a que termine el análisis o el nivelado de la canción cargada y vuelve a abrir el mix."));
        return;
    }
    auto project = std::make_unique<MixProject>();
    if (! MixProject::load (folder, *project))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Armar mix"),
                                                tr ("No se pudo abrir el mix de ") + folder.getFullPathName());
        return;
    }
    // Canción a la que se vuelve al cerrar: al pasar de un mix a otro se conserva la de antes
    juce::File returnFolder = mixReturnFolder;
    if (mixEditor == nullptr)
        returnFolder = currentInfo() != nullptr ? currentInfo()->folder : juce::File();
    if (mixEditor != nullptr)
        closeMix (false);
    if (recording.active)
        stopRecording (false);
    mixReturnFolder = returnFolder;

    // La canción se descarga con fundido: el motor queda para escuchar las fuentes y el mix
    if (currentIndex >= 0)
        unloadSong();
    else
        engine.pause();
    // Sin fila elegida: un clic en cualquier canción (también en la que estaba) cierra el mix y la carga
    setlist.deselectAllRows();
    ++mixPlayRequest;
    // Un análisis de otro mix (cancelado al cerrarlo) no debe tomarse como de este
    if (mixAnalyzer.getState() != Analyzer::State::running)
        mixAnalyzer.reset();
    mixAnalyzerFile.clear();

    ++mixGeneration;
    mixProject = std::move (project);
    mixVersion = 0;
    mixRenderedVersion = -1;   // el mezcla.wav que haya en la carpeta puede ser viejo: se vuelve a preparar
    mixRendering = false;
    mixRenderedInfo = SongInfo();
    mixPreviewSong.reset();
    mixPreviewKind = 0;
    mixPreviewSourceFile.clear();
    mixPreviewVersion = -1;
    mixAnalysisQueue.clear();
    mixAnalyzingFile.clear();
    mixUndo.clear();
    mixLastSnapshot = mixSnapshot();

    mixEditor = std::make_unique<MixEditor>();
    auto* ed = mixEditor.get();
    ed->onChanged = [this] { mixEdited(); };
    ed->onAddSources = [this] { addMixSources(); };
    ed->onRemoveSource = [this] (int s) { removeMixSource (s); };
    ed->onAnalyzeSource = [this] (int s)
    {
        if (mixProject != nullptr && juce::isPositiveAndBelow (s, (int) mixProject->sources.size()))
            queueMixAnalysis (mixProject->sources[(size_t) s].fileName);
    };
    ed->onPlaySource = [this] (int s, double from) { playMixSource (s, from); };
    ed->onPlayMix = [this] (double from) { playMix (from); };
    ed->onStop = [this] { stopMixPreview(); };
    ed->onClickToggled = [this] (bool on)
    {
        mixClick = on;
        const int playing = mixSourceIndex (mixPreviewSourceFile);
        if (mixPreviewKind == 1 && playing >= 0)
            applyMixPreviewGrid (mixProject->sources[(size_t) playing].analysis);
        else if (mixPreviewKind == 2)
            applyMixPreviewGrid (mixRenderedInfo.analysis);
    };
    ed->onCreateSong = [this] { createSongFromMix(); };
    ed->onClose = [this]
    {
        // No se destruye el editor dentro de su propio botón: después
        juce::Component::SafePointer<MainComponent> sp (this);
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->closeMix (true); });
    };
    ed->onRename = [this] (const juce::String& newName)
    {
        if (mixProject != nullptr && newName.trim().isNotEmpty())
        {
            mixProject->name = newName.trim();   // la carpeta conserva su nombre (los trabajos en curso la usan)
            mixProject->save();
            sepLabel.setText (tr ("Armando el mix «") + mixProject->name + tr ("»"), juce::dontSendNotification);
        }
    };
    ed->refineCut = [this] (int s, double t)
    {
        if (mixProject == nullptr || ! juce::isPositiveAndBelow (s, (int) mixProject->sources.size()))
            return t;
        return mix::refineToOnset (mixProject->sourceFile (s), t, formatManager);
    };
    addAndMakeVisible (*ed);
    ed->setProject (mixProject.get());
    resized();

    const bool canAnalyze = Analyzer::isAvailable (analysisPythonPath());
    for (auto& src : mixProject->sources)
    {
        loadMixPeaks (src.fileName);
        if (src.analysis.beats.empty())
        {
            if (canAnalyze)
                queueMixAnalysis (src.fileName);
            else
                ed->setSourceStatus (mixSourceIndex (src.fileName), tr ("Sin analizar: falta el motor de análisis"));
        }
    }
    ed->setStatus (mixProject->sources.empty()
                       ? tr ("Agrega las canciones originales con «+ Canción»: se analizan solas y después eliges los tramos.")
                       : tr ("Mix «") + mixProject->name + tr ("»: ") + juce::String ((int) mixProject->segments.size()) + tr (" tramos"));
    sepLabel.setText (tr ("Armando el mix «") + mixProject->name + tr ("»"), juce::dontSendNotification);
}

void MainComponent::closeMix (bool reloadSong)
{
    if (mixEditor == nullptr)
        return;
    ++mixGeneration;             // los trabajos en curso de este mix se descartan
    ++mixPlayRequest;
    mixAnalyzer.cancel();        // su estado final lo limpia el próximo pedido (startNextMixAnalysis / openMix)
    mixAnalyzerFile.clear();
    mixAnalysisQueue.clear();
    mixAnalyzingFile.clear();
    if (mixProject != nullptr)
        mixProject->save();

    removeChildComponent (mixEditor.get());
    mixEditor.reset();
    mixProject.reset();
    mixRendering = false;
    mixCapture = MixCapture();
    mixUndo.clear();
    mixPreviewKind = 0;
    mixPreviewSourceFile.clear();
    mixPreviewVersion = -1;

    engine.pause();
    int back = -1;
    for (int i = 0; i < (int) library.songs.size() && mixReturnFolder != juce::File(); ++i)
        if (library.songs[(size_t) i].folder == mixReturnFolder)
            back = i;
    mixReturnFolder = juce::File();
    if (reloadSong && back >= 0)
    {
        mixPreviewSong.reset();
        loadSongAt (back);        // descarga lo que sonaba con fundido y vuelve a la canción
    }
    else
    {
        juce::Component::SafePointer<MainComponent> sp (this);
        afterFadeOut ([sp]
        {
            if (sp == nullptr || sp->mixEditor != nullptr)
                return;           // se abrió otro mix mientras tanto
            if (sp->currentIndex < 0)
            {
                sp->engine.setSong (nullptr);
                sp->engine.setMasterGain (1.0f);
            }
            sp->mixPreviewSong.reset();
        });
    }
    restoreSetlistSelection();   // la fila de la canción a la que se volvió (o ninguna)
    resized();
    sepLabel.setText ({}, juce::dontSendNotification);
}

void MainComponent::mixEdited()
{
    if (mixProject == nullptr)
        return;
    mixUndo.push_back (mixLastSnapshot);
    if (mixUndo.size() > 40)
        mixUndo.erase (mixUndo.begin());
    mixLastSnapshot = mixSnapshot();
    ++mixVersion;
    mixProject->save();
    // Si suena una fuente, su click sigue los tiempos (quizá recién editados)
    if (const int playing = mixSourceIndex (mixPreviewSourceFile); mixPreviewKind == 1 && playing >= 0)
        applyMixPreviewGrid (mixProject->sources[(size_t) playing].analysis);
    else if (mixPreviewKind == 2 && mixEditor != nullptr)
        mixEditor->setStatus (tr ("El mix cambió: «Escuchar el mix» lo vuelve a preparar"));
}

void MainComponent::undoMixEdit()
{
    if (mixProject == nullptr || mixUndo.empty() || mixEditor == nullptr)
        return;
    auto snap = mixUndo.back();
    mixUndo.pop_back();
    if (snap.analyses.size() != mixProject->sources.size())
    {
        mixUndo.clear();
        return;
    }
    mixProject->bpm = snap.bpm;
    mixProject->keepTempos = snap.keepTempos;
    mixProject->segments = snap.segments;
    for (size_t i = 0; i < snap.analyses.size(); ++i)
        mixProject->sources[i].analysis = snap.analyses[i];
    mixLastSnapshot = snap;
    ++mixVersion;
    mixProject->save();
    mixEditor->refresh();
    mixEditor->setStatus (tr ("Se deshizo el último cambio del mix"));
}

void MainComponent::addMixSources()
{
    if (mixProject == nullptr)
        return;
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    pickFiles (tr ("Canciones originales para el mix"),
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
               juce::File::getSpecialLocation (juce::File::userMusicDirectory), Library::audioFilePatterns(),
               [safe, gen] (const juce::Array<juce::File>& files)
    {
        auto* self = safe.getComponent();
        if (self != nullptr && gen == self->mixGeneration.load())
            self->addMixSourceFiles (files);
    });
}

void MainComponent::addMixSourceFiles (const juce::Array<juce::File>& files)
{
    if (mixProject == nullptr || mixEditor == nullptr)
        return;
    juce::Array<juce::File> audio;
    for (auto& f : files)
        if (f.existsAsFile() && Library::isAudioFile (f))
            audio.add (f);
    if (audio.isEmpty())
    {
        if (! files.isEmpty())
            mixEditor->setStatus (tr ("Eso no es audio que la app pueda leer (") + Library::audioFormatsDescription() + ")");
        return;
    }
    // La copia a fuentes/ va en segundo plano: una canción en WAV puede pesar cientos de MB
    mixEditor->setStatus (tr ("Copiando ") + juce::String (audio.size()) + (audio.size() == 1 ? tr (" canción al mix...") : tr (" canciones al mix...")));
    const auto folder = mixProject->folder;
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    ++mixCopyJobs;
    mixPool.addJob ([this, safe, audio, folder, gen]
    {
        MixProject scratch;   // solo para copiar y leer el largo; las fuentes se agregan al proyecto en el hilo de mensajes
        scratch.folder = folder;
        std::vector<MixSource> added;
        juce::StringArray failed;
        for (auto& f : audio)
        {
            if (gen != mixGeneration.load() || abortJobs.load())
                break;
            const int i = scratch.addSource (f, formatManager);
            if (i >= 0)
                added.push_back (scratch.sources[(size_t) i]);
            else
                failed.add (f.getFileName());
        }
        juce::MessageManager::callAsync ([safe, added, failed, folder, gen]
        {
            auto* self = safe.getComponent();
            if (self != nullptr)
                self->mixCopyJobs = juce::jmax (0, self->mixCopyJobs - 1);
            if (self == nullptr || gen != self->mixGeneration.load() || self->mixProject == nullptr || self->mixEditor == nullptr)
            {
                // El mix se cerró mientras se copiaba: las copias no quedan huérfanas
                for (auto& s : added)
                    folder.getChildFile ("fuentes").getChildFile (s.fileName).deleteFile();
                return;
            }
            const bool canAnalyze = Analyzer::isAvailable (self->analysisPythonPath());
            for (auto& s : added)
            {
                self->mixProject->sources.push_back (s);
                self->loadMixPeaks (s.fileName);
            }
            self->mixProject->save();
            self->mixUndo.clear();   // deshacer no cruza cambios de fuentes
            self->mixLastSnapshot = self->mixSnapshot();
            ++self->mixVersion;
            self->mixEditor->refresh();
            for (auto& s : added)
            {
                if (canAnalyze)
                    self->queueMixAnalysis (s.fileName);
                else
                    self->mixEditor->setSourceStatus (self->mixSourceIndex (s.fileName), tr ("Sin analizar: falta el motor de análisis"));
            }
            if (! added.empty())
                self->mixEditor->selectSource ((int) self->mixProject->sources.size() - 1);
            const int n = (int) added.size();
            self->mixEditor->setStatus (juce::String (n) + (n == 1 ? tr (" canción agregada") : tr (" canciones agregadas"))
                                        + (canAnalyze ? tr (": se analizan una tras otra") : tr (": sin motor de análisis no se detectan los tiempos (Ajustes IA > Instalar motores de IA)"))
                                        + (failed.isEmpty() ? juce::String() : tr (". No se pudo leer: ") + failed.joinIntoString (", ")));
        });
    });
}

void MainComponent::removeMixSource (int source)
{
    if (mixProject == nullptr || ! juce::isPositiveAndBelow (source, (int) mixProject->sources.size()))
        return;
    const auto src = mixProject->sources[(size_t) source];
    int used = 0;
    for (auto& seg : mixProject->segments)
        if (seg.source == source)
            ++used;
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("¿Quitar la canción del mix?"),
        tr ("«") + src.name + tr ("» se quita del mix") + (used > 0 ? tr (" junto con sus ") + juce::String (used) + tr (" tramos") : juce::String()) + ".",
        "Quitar", "Cancelar", this, juce::ModalCallbackFunction::create ([safe, gen, fileName = src.fileName] (int ok)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || ok == 0 || gen != self->mixGeneration.load() || self->mixProject == nullptr)
                return;
            const int index = self->mixSourceIndex (fileName);
            if (index < 0)
                return;
            // Si suena esa fuente, se detiene; si se estaba analizando, el resultado se descarta
            if (self->mixPreviewKind == 1 && self->mixPreviewSourceFile == fileName)
            {
                self->stopMixPreview();
                self->mixPreviewKind = 0;
                self->mixPreviewSourceFile.clear();
                self->mixPreviewSong.reset();
            }
            self->mixAnalysisQueue.erase (std::remove (self->mixAnalysisQueue.begin(), self->mixAnalysisQueue.end(), fileName), self->mixAnalysisQueue.end());
            if (self->mixAnalyzingFile == fileName)
                self->mixAnalyzer.cancel();
            self->mixProject->removeSource (index);
            self->mixProject->save();
            self->mixUndo.clear();
            self->mixLastSnapshot = self->mixSnapshot();
            ++self->mixVersion;
            if (self->mixEditor != nullptr)
            {
                self->mixEditor->refresh();
                self->mixEditor->setStatus (tr ("Canción quitada del mix"));
            }
        }));
}

void MainComponent::queueMixAnalysis (const juce::String& fileName)
{
    if (mixProject == nullptr || fileName.isEmpty() || fileName == mixAnalyzingFile
        || std::find (mixAnalysisQueue.begin(), mixAnalysisQueue.end(), fileName) != mixAnalysisQueue.end())
        return;
    if (! Analyzer::isAvailable (analysisPythonPath()))
    {
        if (mixEditor != nullptr)
            mixEditor->setSourceStatus (mixSourceIndex (fileName), tr ("Sin analizar: falta el motor de análisis"));
        return;
    }
    mixAnalysisQueue.push_back (fileName);
    if (mixEditor != nullptr)
        mixEditor->setSourceStatus (mixSourceIndex (fileName), tr ("En cola para analizar"));
    startNextMixAnalysis();
}

void MainComponent::startNextMixAnalysis()
{
    if (mixProject == nullptr || mixAnalyzingFile.isNotEmpty() || mixAnalysisQueue.empty())
        return;
    // Sin nada propio en curso, un estado final del analizador es viejo (de un mix cerrado): se limpia
    const auto state = mixAnalyzer.getState();
    if (state == Analyzer::State::running)
        return;
    if (state != Analyzer::State::idle)
        mixAnalyzer.reset();
    mixAnalyzerFile.clear();
    const auto fileName = mixAnalysisQueue.front();
    mixAnalysisQueue.erase (mixAnalysisQueue.begin());
    const int index = mixSourceIndex (fileName);
    if (index < 0)
    {
        startNextMixAnalysis();
        return;
    }
    mixAnalyzingFile = fileName;
    if (mixEditor != nullptr)
        mixEditor->setSourceStatus (index, tr ("Leyendo el audio..."));

    // El analizador trabaja sobre audio en RAM: la fuente se lee a 44,1 kHz en segundo plano
    SongInfo info;
    info.folder = mixProject->sourcesFolder();
    StemInfo st;
    st.name = mixProject->sources[(size_t) index].name;
    st.fileName = fileName;
    info.stems.push_back (st);
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    mixPool.addJob ([this, safe, info, gen, fileName]
    {
        std::shared_ptr<LoadedSong> song;
        try
        {
            song = AudioEngine::loadSong (info, mix::renderSampleRate, formatManager,
                                          [this, gen] { return gen != mixGeneration.load() || abortJobs.load(); });
        }
        catch (const std::exception&) { song = nullptr; }
        juce::MessageManager::callAsync ([safe, song, gen, fileName]
        {
            auto* self = safe.getComponent();
            if (self != nullptr && gen == self->mixGeneration.load())
                self->mixSourceLoadedForAnalysis (song, fileName);
        });
    });
}

void MainComponent::mixSourceLoadedForAnalysis (std::shared_ptr<LoadedSong> song, const juce::String& fileName)
{
    if (fileName != mixAnalyzingFile)
        return;   // ya no es el pedido en curso (la fuente se quitó o se reinició la cola)
    const int index = mixSourceIndex (fileName);
    if (song == nullptr || song->tracks.empty() || index < 0)
    {
        if (mixEditor != nullptr && index >= 0)
            mixEditor->setSourceStatus (index, tr ("No se pudo leer el audio"));
        mixAnalyzingFile.clear();
        startNextMixAnalysis();
        return;
    }
    if (mixAnalyzer.getState() != Analyzer::State::running)
        mixAnalyzer.reset();
    if (! mixAnalyzer.start (song, mix::renderSampleRate, std::vector<bool> (song->tracks.size(), false), analysisPythonPath()))
    {
        // Sigue terminando uno anterior (cancelado): se vuelve a intentar en un momento
        mixAnalysisQueue.insert (mixAnalysisQueue.begin(), fileName);
        mixAnalyzingFile.clear();
        return;
    }
    mixAnalyzerFile = fileName;
    if (mixEditor != nullptr)
        mixEditor->setSourceStatus (index, tr ("Analizando tiempos y compases..."));
}

void MainComponent::loadMixPeaks (const juce::String& fileName)
{
    if (mixProject == nullptr)
        return;
    const auto file = mixProject->sourcesFolder().getChildFile (fileName);
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    mixPool.addJob ([this, safe, file, fileName, gen]
    {
        if (gen != mixGeneration.load() || abortJobs.load())
            return;
        static constexpr int bins = 200;   // static: la lambda de abajo lo usa sin capturarlo (MSVC no acepta un constexpr local)
        double length = 0.0;
        std::vector<float> peaks;
        try { peaks = mix::computePeaks (file, formatManager, bins, length, [this, gen] { return gen != mixGeneration.load() || abortJobs.load(); }); }
        catch (const std::exception&) { peaks.clear(); }
        juce::MessageManager::callAsync ([safe, peaks, fileName, gen]
        {
            auto* self = safe.getComponent();
            if (self == nullptr || gen != self->mixGeneration.load() || self->mixEditor == nullptr)
                return;
            const int index = self->mixSourceIndex (fileName);
            if (index >= 0)
                self->mixEditor->setSourcePeaks (index, peaks, bins);
        });
    });
}

void MainComponent::applyMixPreviewGrid (const Analysis& a)
{
    const double sr = engine.getSampleRate();
    if (a.beats.empty())
        engine.setBeatGrid (nullptr);
    else
    {
        auto grid = std::make_shared<BeatGrid>();
        for (auto& b : a.beats)
        {
            grid->positions.push_back ((juce::int64) std::llround (b.seconds * sr));
            grid->beatInBar.push_back (b.beatInBar);
        }
        engine.setBeatGrid (std::move (grid));
    }
    engine.setClick (mixClick, a.bpm > 0.0 ? a.bpm : 120.0, 0.0, -6.0f, 0);
}

void MainComponent::setMixPreview (std::shared_ptr<LoadedSong> song, int kind, const juce::String& sourceFile, const Analysis& grid, double from,
                                   int request, int version)
{
    // Una carga vieja (llegó otra escucha, un Stop o se cerró el mix) no toca lo que suena
    if (request != mixPlayRequest)
        return;
    if (song == nullptr || song->tracks.empty())
    {
        if (mixEditor != nullptr)
            mixEditor->setStatus (tr ("No se pudo leer el audio para escuchar"));
        return;
    }
    // Lo que sonaba se apaga con fundido antes de cambiar el audio del motor
    engine.pause();
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    afterFadeOut ([safe, song, kind, sourceFile, grid, from, gen, request, version]
    {
        auto* self = safe.getComponent();
        if (self == nullptr || gen != self->mixGeneration.load() || self->mixEditor == nullptr || request != self->mixPlayRequest)
            return;
        if (std::abs (song->sampleRate - self->engine.getSampleRate()) > 1.0)
        {
            // Se cargó a la frecuencia anterior del dispositivo: se vuelve a pedir a la actual
            if (kind == 1)
            {
                const int index = self->mixSourceIndex (sourceFile);
                if (index >= 0)
                    self->playMixSource (index, from);
            }
            else
                self->playMix (from);
            return;
        }
        self->engine.setSong (song);
        self->engine.setMasterGain (1.0f);
        self->engine.setSongGain (1.0f);
        self->mixPreviewSong = song;
        self->mixPreviewKind = kind;
        self->mixPreviewSourceFile = sourceFile;
        self->mixPreviewVersion = kind == 2 ? version : -1;   // recién ahora es lo que está en el motor
        self->applyMixPreviewGrid (grid);
        self->engine.seekSeconds (juce::jmax (0.0, from));
        self->engine.play();
    });
}

void MainComponent::dropMixPreview()
{
    ++mixPlayRequest;
    engine.pause();
    mixPreviewKind = 0;
    mixPreviewSourceFile.clear();
    mixPreviewVersion = -1;
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    afterFadeOut ([safe, gen]
    {
        if (safe == nullptr || gen != safe->mixGeneration.load() || safe->mixEditor == nullptr || safe->mixPreviewKind != 0)
            return;
        safe->engine.setSong (nullptr);
        safe->mixPreviewSong.reset();
    });
}

void MainComponent::playMixSource (int source, double from)
{
    if (mixProject == nullptr || ! juce::isPositiveAndBelow (source, (int) mixProject->sources.size()))
        return;
    const int request = ++mixPlayRequest;
    const auto& src = mixProject->sources[(size_t) source];
    if (mixPreviewKind == 1 && mixPreviewSourceFile == src.fileName && mixPreviewSong != nullptr)
    {
        applyMixPreviewGrid (src.analysis);
        engine.seekSeconds (juce::jmax (0.0, from));
        engine.play();
        return;
    }
    SongInfo info;
    info.folder = mixProject->sourcesFolder();
    StemInfo st;
    st.name = src.name;
    st.fileName = src.fileName;
    info.stems.push_back (st);
    const auto fileName = src.fileName;
    const double sr = engine.getSampleRate();
    const int gen = mixGeneration.load();
    if (mixEditor != nullptr)
        mixEditor->setStatus (tr ("Cargando «") + src.name + tr ("» para escuchar..."));
    juce::Component::SafePointer<MainComponent> safe (this);
    loaderPool.addJob ([this, safe, info, sr, gen, fileName, from, request]
    {
        std::shared_ptr<LoadedSong> song;
        try { song = AudioEngine::loadSong (info, sr, formatManager, [this, gen] { return gen != mixGeneration.load() || abortJobs.load(); }); }
        catch (const std::exception&) { song = nullptr; }
        juce::MessageManager::callAsync ([safe, song, gen, fileName, from, request]
        {
            auto* self = safe.getComponent();
            if (self == nullptr || gen != self->mixGeneration.load())
                return;
            const int index = self->mixSourceIndex (fileName);
            if (index < 0)
                return;   // la fuente se quitó mientras se cargaba
            if (self->mixEditor != nullptr)
                self->mixEditor->setStatus ({});
            // Los tiempos más recientes de la fuente (pudieron editarse mientras se cargaba)
            self->setMixPreview (song, 1, fileName, self->mixProject->sources[(size_t) index].analysis, from, request, -1);
        });
    });
}

void MainComponent::renderMix (std::function<void (bool)> then)
{
    if (mixProject == nullptr)
        return;
    if (mixProject->segments.empty())
    {
        if (mixEditor != nullptr)
            mixEditor->setStatus (tr ("El mix no tiene tramos: elige una canción, selecciona un tramo y «Agregar al mix»"));
        return;
    }
    if (mixRendering)
    {
        if (mixEditor != nullptr)
            mixEditor->setStatus (tr ("Ya se está preparando el mix..."));
        return;
    }
    if (mixRenderedVersion == mixVersion && mixProject->renderFile().existsAsFile())
    {
        if (then)
            then (true);
        return;
    }
    mixRendering = true;
    if (mixEditor != nullptr)
    {
        mixEditor->setBusy (true);
        mixEditor->setStatus (tr ("Preparando el mix (estirando y uniendo los tramos)..."));
    }
    const MixProject copy = *mixProject;
    const int version = mixVersion;
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    loaderPool.addJob ([this, safe, copy, version, gen, then]
    {
        juce::String error;
        bool ok = false;
        float reducedDb = 0.0f;
        SongInfo meta;
        std::vector<MixLevel> levels;
        double mixLufs = loudness::unknown;
        try
        {
            auto abort = [this, gen] { return gen != mixGeneration.load() || abortJobs.load(); };
            auto progress = [safe] (float p)
            {
                juce::MessageManager::callAsync ([safe, p]
                {
                    if (safe != nullptr && safe->mixEditor != nullptr && safe->mixRendering)
                        safe->mixEditor->setStatus (tr ("Preparando el mix... ") + juce::String (juce::roundToInt (p * 100.0f)) + " %");
                });
            };
            auto buffer = mix::render (copy, mix::renderSampleRate, formatManager, abort, progress, error, &levels);
            if (buffer.getNumSamples() > 0)
            {
                // Sin margen se recortaría al escribir en 24 bits (y la distorsión pasaría a los stems):
                // si algún pico pasa de 1, se baja todo el mix por igual
                const float peak = buffer.getMagnitude (0, buffer.getNumSamples());
                if (peak > 0.999f)
                {
                    buffer.applyGain (0.999f / peak);
                    reducedDb = juce::Decibels::gainToDecibels (peak / 0.999f);
                }
                ok = triggers::writeWav (buffer, mix::renderSampleRate, copy.renderFile());
                if (! ok)
                    error = tr ("No se pudo escribir ") + copy.renderFile().getFullPathName();
                else
                {
                    mix::describeSong (copy, meta);
                    if (copy.levelLufs < 0.0 && ! abort())
                        mixLufs = loudness::measure (buffer, mix::renderSampleRate, {}).whole.lufs;   // cómo quedó el mix entero
                }
            }
            else if (error.isEmpty() && ! abort())
                error = tr ("El mix quedó vacío");
        }
        catch (const std::bad_alloc&) { error = tr ("No hay memoria suficiente para preparar el mix"); }
        catch (const std::exception& e) { error = tr ("Error al preparar el mix: ") + juce::String (e.what()); }
        juce::MessageManager::callAsync ([safe, ok, error, version, gen, then, meta, reducedDb, levels, mixLufs]
        {
            auto* self = safe.getComponent();
            if (self == nullptr || gen != self->mixGeneration.load())
                return;
            self->mixRendering = false;
            if (self->mixEditor != nullptr)
                self->mixEditor->setBusy (false);
            if (ok)
            {
                self->mixRenderedVersion = version;
                self->mixRenderedInfo = meta;
                // Nivelado: cómo quedó el mix, en cuántos tramos actuó el limitador (y cuánto) y si alguno no llegó
                juce::String level;
                if (mixLufs > loudness::unknown + 1.0)
                {
                    int limited = 0, below = 0;
                    double deepest = 0.0;
                    for (auto& lv : levels)
                    {
                        limited += lv.limiterDb < -0.1 ? 1 : 0;
                        below += lv.belowTarget ? 1 : 0;
                        deepest = juce::jmin (deepest, lv.limiterDb);
                    }
                    auto count = [] (int n) { return juce::String (n) + (n == 1 ? tr (" tramo") : tr (" tramos")); };
                    level = tr (" · ") + juce::String (mixLufs, 1).replaceCharacter ('.', ',') + " LUFS"
                          + (limited > 0 ? tr (" · limitador en ") + count (limited) + tr (" (hasta ")
                                               + juce::String (deepest, 1).replaceCharacter ('.', ',') + " dB)"
                                         : juce::String())
                          + (below > 0 ? tr (" · ") + count (below) + tr (" bajo el objetivo (tope de 12 dB)") : juce::String());
                }
                if (self->mixEditor != nullptr)
                {
                    if (version == self->mixVersion)
                        self->mixEditor->setSegmentLevels (levels);
                    self->mixEditor->setStatus (tr ("Mix listo: ") + ui::formatTime (self->mixProject != nullptr ? mix::length (*self->mixProject) : 0.0)
                                                + level
                                                + (reducedDb > 0.05f ? tr (" · se bajó ") + juce::String (reducedDb, 1) + tr (" dB para no saturar") : juce::String())
                                                + tr (" · clic en «Click» para comprobar las uniones con el metrónomo"));
                }
            }
            else if (error.isNotEmpty() && version != self->mixVersion)
            {
                // El mix cambió mientras se preparaba (por ejemplo, se quitó una fuente): el próximo pedido lo rehace
                if (self->mixEditor != nullptr)
                    self->mixEditor->setStatus (tr ("El mix cambió mientras se preparaba: vuelve a pedirlo"));
            }
            else if (error.isNotEmpty())
            {
                if (self->mixEditor != nullptr)
                    self->mixEditor->setStatus (error);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Armar mix"), error);
            }
            if (then)
                then (ok);
            // Una escucha pedida mientras se preparaba (y que sigue siendo la última) suena ahora
            const int wanted = std::exchange (self->mixWantedPlay, 0);
            if (ok && wanted != 0 && wanted == self->mixPlayRequest && self->mixEditor != nullptr)
                self->playMix (self->mixWantedFrom);
        });
    });
}

void MainComponent::playMix (double from)
{
    if (mixProject == nullptr)
        return;
    const int request = ++mixPlayRequest;
    if (mixPreviewKind == 2 && mixPreviewSong != nullptr && mixPreviewVersion == mixVersion)
    {
        applyMixPreviewGrid (mixRenderedInfo.analysis);
        engine.seekSeconds (juce::jmax (0.0, from));
        engine.play();
        return;
    }
    if (mixRendering)
    {
        // Ya se está preparando (por otra escucha o por "Crear canción"): suena al terminar
        mixWantedPlay = request;
        mixWantedFrom = from;
        if (mixEditor != nullptr)
            mixEditor->setStatus (tr ("Preparando el mix: suena en cuanto esté listo..."));
        return;
    }
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    renderMix ([safe, from, gen, request] (bool ok)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || ! ok || gen != self->mixGeneration.load() || self->mixProject == nullptr
            || request != self->mixPlayRequest)
            return;   // entretanto se pidió otra escucha o Stop
        SongInfo info;
        info.folder = self->mixProject->folder;
        StemInfo st;
        st.name = "Mezcla";
        st.fileName = self->mixProject->renderFile().getFileName();
        info.stems.push_back (st);
        const double sr = self->engine.getSampleRate();
        const int version = self->mixRenderedVersion;
        self->loaderPool.addJob ([self, safe, info, sr, gen, version, from, request]
        {
            std::shared_ptr<LoadedSong> song;
            try { song = AudioEngine::loadSong (info, sr, self->formatManager, [self, gen] { return gen != self->mixGeneration.load() || self->abortJobs.load(); }); }
            catch (const std::exception&) { song = nullptr; }
            juce::MessageManager::callAsync ([safe, song, gen, version, from, request]
            {
                auto* s2 = safe.getComponent();
                if (s2 == nullptr || gen != s2->mixGeneration.load())
                    return;
                s2->setMixPreview (song, 2, {}, s2->mixRenderedInfo.analysis, from, request, version);
            });
        });
    });
}

void MainComponent::stopMixPreview()
{
    ++mixPlayRequest;   // una escucha que todavía se está preparando ya no arranca sola
    engine.pause();
}

void MainComponent::createSongFromMix()
{
    if (mixProject == nullptr)
        return;
    if (mixProject->segments.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Crear canción"),
                                                tr ("El mix no tiene tramos todavía: elige una canción, selecciona un tramo y «Agregar al mix»."));
        return;
    }
    juce::StringArray unanalyzed;
    for (auto& seg : mixProject->segments)
        if (juce::isPositiveAndBelow (seg.source, (int) mixProject->sources.size())
            && mixProject->sources[(size_t) seg.source].analysis.beats.empty())
            unanalyzed.addIfNotAlreadyThere (mixProject->sources[(size_t) seg.source].name);

    auto* w = new juce::AlertWindow (tr ("Crear canción con el mix"),
        tr ("El mix (") + ui::formatTime (mix::length (*mixProject)) + tr (", ") + juce::String ((int) mixProject->segments.size())
            + tr (" tramos) se agrega al setlist con sus tiempos, compases, secciones de tempo y un marcador por tramo.")
            + (unanalyzed.isEmpty() ? juce::String()
                                    : tr ("\n\nSin analizar (sus uniones no quedan alineadas): ") + unanalyzed.joinIntoString (", ")),
        juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("nombre", mixProject->name, tr ("Nombre de la canción"));
    w->addComboBox ("modo", { tr ("Separar en pistas con IA (pistas, calidad y batería en partes: los de la barra de arriba)"),
                              tr ("Agregar sin separar (una sola pista con la mezcla)") }, tr ("Qué hacer"));
    w->getComboBoxComponent ("modo")->setSelectedItemIndex (0, juce::dontSendNotification);
    w->addButton ("Crear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || result != 1 || self->mixProject == nullptr)
            return;
        auto name = w->getTextEditorContents ("nombre").trim();
        if (name.isEmpty())
            name = self->mixProject->name;
        self->startMixSong (w->getComboBoxComponent ("modo")->getSelectedItemIndex() == 0, name);
    }), true);
}

void MainComponent::startMixSong (bool separate, const juce::String& name)
{
    if (mixProject == nullptr)
        return;
    if (separate && separator.getState() == Separator::State::running)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Crear canción"),
                                                tr ("Ya hay una separación en curso. Espera a que termine o crea la canción sin separar."));
        return;
    }
    const int gen = mixGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    renderMix ([safe, separate, name, gen] (bool ok)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || ! ok || gen != self->mixGeneration.load() || self->mixProject == nullptr)
            return;
        if (self->mixRenderedVersion != self->mixVersion)
        {
            // Se editó mientras se preparaba: la canción debe llevar la última versión
            self->startMixSong (separate, name);
            return;
        }
        SongInfo meta = self->mixRenderedInfo;
        meta.name = name;
        if (separate)
        {
            // Se separa una copia: el mix puede volver a prepararse mientras tanto
            const auto input = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getNonexistentChildFile ("secuencias_mix", ".wav", false);
            if (! self->mixProject->renderFile().copyFileTo (input))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Crear canción"),
                                                        tr ("No se pudo copiar el mix a ") + input.getFullPathName());
                return;
            }
            self->clearPendingMixSong();
            self->pendingMixSong = std::make_unique<SongInfo> (meta);
            self->pendingMixSongName = name;
            self->pendingMixInput = input;
            if (! self->startSeparation (input, name))
            {
                self->clearPendingMixSong();
                return;
            }
            self->closeMix (true);
            self->sepLabel.setText (tr ("Separando el mix «") + name + tr ("»: al terminar queda en el setlist con sus tiempos y marcadores"),
                                    juce::dontSendNotification);
        }
        else
        {
            const int idx = self->library.importStemFiles ({ self->mixProject->renderFile() }, name, false);
            if (idx < 0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Crear canción"),
                                                        tr ("No se pudo agregar el mix a la biblioteca."));
                return;
            }
            self->applyMixSongInfo (idx, meta);
            self->closeMix (false);
            self->refreshSetlist();
            self->setlist.selectRow (idx);
            self->sepLabel.setText (tr ("«") + name + tr ("» agregada al setlist (sin separar)"), juce::dontSendNotification);
        }
    });
}

void MainComponent::applyMixSongInfo (int songIndex, const SongInfo& meta)
{
    if (! juce::isPositiveAndBelow (songIndex, (int) library.songs.size()))
        return;
    auto& s = library.songs[(size_t) songIndex];
    if (meta.name.isNotEmpty())
        s.name = meta.name;
    s.analysis = meta.analysis;
    s.tempoRegions = meta.tempoRegions;
    s.markers = meta.markers;
    s.bpm = meta.bpm;
    s.clickOffset = meta.clickOffset;
    s.clips.clear();
    s.notes.clear();
    s.playBpm = 0.0;
    s.transpose = 0;
    s.sortMarkers();
    s.sortTempoRegions();
    s.fitStemArrays();
    library.saveSong (s);
}

void MainComponent::clearPendingMixSong()
{
    pendingMixSong.reset();
    pendingMixSongName.clear();
    if (pendingMixInput != juce::File())
        pendingMixInput.deleteFile();
    pendingMixInput = juce::File();
}

void MainComponent::updateMixTimer()
{
    if (mixEditor == nullptr)
        return;

    // Análisis de las fuentes, una tras otra. Los estados del analizador cuentan solo si se arrancó para la
    // fuente en curso (mientras se lee su audio, lo que tenga es de antes)
    const auto st = mixAnalyzer.getState();
    const int index = mixSourceIndex (mixAnalyzingFile);
    const bool ours = mixAnalyzerFile.isNotEmpty() && mixAnalyzerFile == mixAnalyzingFile;
    if (ours && st == Analyzer::State::running)
    {
        const float p = mixAnalyzer.getProgress();
        if (index >= 0)
            mixEditor->setSourceStatus (index, p >= 0.0f ? tr ("Analizando... ") + juce::String (juce::roundToInt (p * 100.0f)) + " %"
                                                         : tr ("Analizando tiempos y compases..."));
    }
    else if (ours && st == Analyzer::State::done)
    {
        if (index >= 0 && mixProject != nullptr)
        {
            mixProject->sources[(size_t) index].analysis = mixAnalyzer.getResult();
            // Los estados guardados para deshacer también reciben el análisis (si no, deshacer lo borraría)
            for (auto& snap : mixUndo)
                if ((size_t) index < snap.analyses.size())
                    snap.analyses[(size_t) index] = mixProject->sources[(size_t) index].analysis;
            if ((size_t) index < mixLastSnapshot.analyses.size())
                mixLastSnapshot.analyses[(size_t) index] = mixProject->sources[(size_t) index].analysis;
            ++mixVersion;
            mixProject->save();
            mixEditor->setSourceStatus (index, {});
            mixEditor->refresh();
        }
        mixAnalyzer.reset();
        mixAnalyzerFile.clear();
        mixAnalyzingFile.clear();
        startNextMixAnalysis();
    }
    else if (ours && (st == Analyzer::State::failed || st == Analyzer::State::cancelled))
    {
        if (index >= 0)
            mixEditor->setSourceStatus (index, st == Analyzer::State::failed ? tr ("No se pudo analizar: ") + mixAnalyzer.getMessage()
                                                                             : tr ("Sin analizar"));
        mixAnalyzer.reset();
        mixAnalyzerFile.clear();
        mixAnalyzingFile.clear();
        startNextMixAnalysis();
    }
    else if (mixAnalyzingFile.isEmpty() && ! mixAnalysisQueue.empty())
    {
        if (st != Analyzer::State::running)
        {
            if (st != Analyzer::State::idle)
                mixAnalyzer.reset();
            startNextMixAnalysis();
        }
    }

    mixEditor->setPlayback (mixPreviewKind, mixSourceIndex (mixPreviewSourceFile), engine.getPositionSeconds(), engine.isPlaying());
    mixCaptureStep();
}

bool MainComponent::isMixBusy() const
{
    if (pendingMixSong != nullptr)
        return true;   // la separación del mix sigue (o su resultado aún no se aplica)
    if (! mixCapture.active || mixProject == nullptr)
        return false;
    return mixCopyJobs > 0 || mixAnalyzingFile.isNotEmpty() || ! mixAnalysisQueue.empty() || mixRendering
           || (mixCapture.autoSegments && ! (mixCapture.segmentsDone && mixCapture.renderStarted)) || ! mixCapture.selectionDone
           || (mixCapture.createMode > 0 && ! mixCapture.createStarted);
}

void MainComponent::openMixForCapture (const juce::String& name, const juce::StringArray& sourceFiles, bool autoSegments, int createMode,
                                       int selectFirstBar, int selectLastBar)
{
    library.mixesFolder().createDirectory();
    auto folder = library.mixesFolder().getChildFile (juce::File::createLegalFileName (name));
    if (! folder.getChildFile ("mix.json").existsAsFile())
        folder = MixProject::create (library.mixesFolder(), name);
    if (folder == juce::File())
        return;
    openMix (folder);
    if (mixEditor == nullptr || mixProject == nullptr)
        return;
    mixCapture = MixCapture();
    mixCapture.active = true;
    mixCapture.autoSegments = autoSegments;
    mixCapture.createMode = createMode;
    mixCapture.selFirst = selectFirstBar;
    mixCapture.selLast = selectLastBar;
    mixCapture.selectionDone = selectFirstBar < 0;
    // Las fuentes que falten entran por el mismo camino que "+ Canción" (copia en segundo plano)
    juce::Array<juce::File> missing;
    for (auto& path : sourceFiles)
    {
        if (! juce::File::isAbsolutePath (path))
            continue;
        const juce::File f (path);
        bool present = false;
        for (auto& s : mixProject->sources)
            present = present || s.name == f.getFileNameWithoutExtension();
        if (! present)
            missing.add (f);
    }
    if (! missing.isEmpty())
        addMixSourceFiles (missing);
}

void MainComponent::mixCaptureStep()
{
    if (! mixCapture.active || mixProject == nullptr || mixEditor == nullptr)
        return;
    if (mixCopyJobs > 0 || mixAnalyzingFile.isNotEmpty() || ! mixAnalysisQueue.empty() || mixRendering)
        return;
    if (mixCapture.autoSegments && ! mixCapture.segmentsDone)
    {
        mixCapture.segmentsDone = true;
        if (mixProject->segments.empty())
        {
            for (int i = 0; i < (int) mixProject->sources.size(); ++i)
            {
                std::vector<double> downbeats;
                for (auto& b : mixProject->sources[(size_t) i].analysis.beats)
                    if (b.beatInBar == 1)
                        downbeats.push_back (b.seconds);
                if (downbeats.size() < 4)
                    continue;
                const size_t first = juce::jmin ((size_t) 2, downbeats.size() - 2);
                const size_t last = juce::jmin (first + 8, downbeats.size() - 1);
                MixSegment seg;
                seg.source = i;
                seg.start = mix::refineToOnset (mixProject->sourceFile (i), downbeats[first], formatManager);
                seg.end = mix::refineToOnset (mixProject->sourceFile (i), downbeats[last], formatManager);
                if (seg.end > seg.start + 0.5)
                    mixProject->segments.push_back (seg);
            }
            mixEditor->refresh();
            if (! mixProject->segments.empty())
                mixEditor->selectSegment ((int) mixProject->segments.size() - 1);
            mixEdited();
        }
        return;
    }
    if (mixCapture.autoSegments && ! mixCapture.renderStarted)
    {
        mixCapture.renderStarted = true;
        if (! mixProject->segments.empty())
            renderMix ({});
        return;
    }
    if (! mixCapture.selectionDone)
    {
        // Compases escritos a mano en la primera fuente (el mismo camino que los campos de la cabecera)
        mixCapture.selectionDone = true;
        if (! mixProject->sources.empty())
        {
            mixEditor->selectSource (0);
            mixEditor->selectBars (mixCapture.selFirst, mixCapture.selLast);
        }
    }
    if (mixCapture.createMode > 0 && ! mixCapture.createStarted)
    {
        mixCapture.createStarted = true;
        startMixSong (mixCapture.createMode == 1, mixProject->name);
    }
}

void MainComponent::clipMenu (double playbackSeconds, int lane)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr || arrangedSong == nullptr)
        return;
    const double sourceLength = (double) sourceSong->length / engine.getSampleRate();
    juce::String how;
    const double t = snapCutTime (timeMap.toOriginal (playbackSeconds), lane, how);   // según el modo de corte
    auto clips = info->clips;
    arrangement::ensureClips (clips, sourceLength);
    const int idx = arrangement::clipAt (clips, t);

    juce::PopupMenu m;
    if (idx >= 0)
    {
        const auto& c = clips[(size_t) idx];
        m.addSectionHeader (tr ("Tramo ") + juce::String (idx + 1) + ": " + formatTime (timeMap.toPlayback (c.position)) + " - " + formatTime (timeMap.toPlayback (c.end()))
                            + tr ("  (audio original ") + formatTime (c.srcStart) + " - " + formatTime (c.srcEnd) + ")");
    }
    else
        m.addSectionHeader (tr ("Silencio en ") + formatTime (playbackSeconds));
    m.addItem (1, tr ("Cortar los stems en ") + formatTime (timeMap.toPlayback (t)) + how, idx >= 0);
    m.addItem (2, tr ("Desplazar este tramo..."), idx >= 0);
    m.addItem (3, tr ("Desplazar este tramo y los siguientes..."), idx >= 0);
    m.addItem (4, tr ("Alinear el inicio del tramo al tiempo más cercano"), idx >= 0 && ! info->analysis.beats.empty());
    m.addItem (5, tr ("Unir con el tramo anterior"), arrangement::canJoinWithPrevious (clips, idx));
    m.addItem (6, tr ("Eliminar este tramo (deja silencio)"), idx >= 0 && clips.size() > 1);
    m.addItem (7, tr ("Eliminar este tramo y cerrar el hueco (la grilla se adelanta)"), idx >= 0 && clips.size() > 1);
    m.addSeparator();
    m.addItem (11, tr ("Copiar tramo"), idx >= 0);
    m.addItem (12, tr ("Duplicar tramo (pegar a continuación)"), idx >= 0);
    juce::String pegarEn = formatTime (timeMap.toPlayback (t));
    const bool canPaste = clipboard.valid && clipboard.folder == info->folder;   // un tramo es del audio de su canción
    m.addItem (13, tr ("Pegar insertando en ") + pegarEn + (canPaste ? juce::String::formatted (" (%.1f s)", clipboard.clip.length()) : juce::String()), canPaste);
    m.addItem (14, tr ("Pegar encima en ") + pegarEn, canPaste);
    m.addSeparator();
    m.addItem (8, tr ("Deshacer la última edición (Ctrl+Z)"), ! undoStack.empty());
    m.addItem (9, tr ("Restaurar el audio original (sin cortes)"), ! info->clips.empty());
    m.addSeparator();
    m.addItem (10, tr ("Shift + arrastrar sobre un carril mueve el tramo"), false);

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, t, idx, sourceLength] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0)
            return;
        if (result == 8)
        {
            undoLastEdit();
            return;
        }
        if (result == 11) { copyClip (idx); return; }
        if (result == 12) { duplicateClip (idx); return; }
        if (result == 13 || result == 14) { pasteClipboard (t, result == 13); return; }
        if (result == 2 || result == 3)
        {
            askText (tr ("Desplazamiento en milisegundos (+ = más tarde, - = antes)"), "0", [this, idx, result, sourceLength] (const juce::String& text)
            {
                auto* s = currentInfo();
                const double ms = text.replace (",", ".").getDoubleValue();
                if (s == nullptr || std::abs (ms) < 0.01)
                    return;
                pushUndo();
                arrangement::ensureClips (s->clips, sourceLength);
                if (juce::isPositiveAndBelow (idx, (int) s->clips.size()))
                {
                    const auto c = s->clips[(size_t) idx];
                    const double delta = juce::jmax (ms / 1000.0, -c.position);   // lo que moveClip mueve de verdad
                    arrangement::moveClip (s->clips, idx, ms / 1000.0, result == 3);
                    arrangement::moveMidiHits (*s, c.position, result == 3 ? std::numeric_limits<double>::max() : c.end(), delta);
                }
                arrangementEdited();
            });
            return;
        }
        pushUndo();
        arrangement::ensureClips (inf->clips, sourceLength);
        auto& cl = inf->clips;
        bool gridChanged = false;
        if (result == 1)
        {
            if (! arrangement::cutAt (cl, t, sourceLength))
            {
                undoStack.pop_back();
                return;
            }
        }
        else if (result == 4 && juce::isPositiveAndBelow (idx, (int) cl.size()))
        {
            const int nb = inf->analysis.nearestBeat (cl[(size_t) idx].position, 0.5);
            if (nb >= 0)
            {
                const auto c = cl[(size_t) idx];
                const double d = inf->analysis.beats[(size_t) nb].seconds - c.position;
                arrangement::moveClip (cl, idx, d, false);
                arrangement::moveMidiHits (*inf, c.position, c.end(), juce::jmax (d, -c.position));
            }
        }
        else if (result == 5)
            arrangement::joinWithPrevious (cl, idx);
        else if (result == 6 || result == 7)
        {
            double from = 0.0, length = 0.0;
            if (result == 6 && juce::isPositiveAndBelow (idx, (int) cl.size()))
                arrangement::removeMidiHits (*inf, cl[(size_t) idx].position, cl[(size_t) idx].end());   // dejando silencio
            arrangement::removeClip (cl, idx, result == 7, from, length);   // cerrando el hueco: shiftGrid mueve los golpes
            if (result == 7 && length > 0.0)
            {
                arrangement::shiftGrid (*inf, from, -length);
                gridChanged = true;
            }
        }
        else if (result == 9)
        {
            arrangement::midiHitsToSource (*inf, inf->clips);   // cada golpe vuelve al audio original que sonaba bajo él
            inf->clips.clear();
        }
        else
        {
            undoStack.pop_back();
            return;
        }
        if (gridChanged)
            refreshFromInfo();
        else
            arrangementEdited();
    });
}

void MainComponent::copyClip (int index)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr)
        return;
    auto clips = info->clips;
    arrangement::ensureClips (clips, (double) sourceSong->length / engine.getSampleRate());
    if (! juce::isPositiveAndBelow (index, (int) clips.size()))
        return;
    const auto& c = clips[(size_t) index];
    clipboard.valid = true;
    clipboard.folder = info->folder;
    clipboard.clip = c;
    clipboard.grid = arrangement::copyGrid (info->analysis, c.position, c.end());   // la grilla que se ve sobre ese tramo
    arrangement::copyMidiHits (*info, c.position, c.end(), clipboard.grid);          // y los golpes de las pistas MIDI
    // Secciones de tempo del rango: la vigente al inicio y las que empiezan dentro, relativas al tramo
    clipboard.regions.clear();
    const int first = info->tempoRegionAt (c.position);
    if (first >= 0)
        clipboard.regions.push_back ({ 0.0, info->tempoRegions[(size_t) first].origBpm, info->tempoRegions[(size_t) first].playBpm });
    for (auto& r : info->tempoRegions)
        if (r.start > c.position + 1.0e-6 && r.start < c.end() - 1.0e-6)
            clipboard.regions.push_back ({ r.start - c.position, r.origBpm, r.playBpm });
    sepLabel.setText (juce::String::formatted ("Tramo copiado (%.1f s, ", c.length()) + juce::String ((int) clipboard.grid.beats.size()) + tr (" tiempos)"), juce::dontSendNotification);
}

void MainComponent::pasteClipboard (double at, bool insert)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr || ! clipboard.valid || clipboard.folder != info->folder)
        return;
    const double sourceLength = (double) sourceSong->length / engine.getSampleRate();
    if (cutMode() == 2)
        if (const double g = nearestGridTime (at); g >= 0.0)
            at = g;   // a la rejilla: el pegado cae en el tiempo más cercano
    at = juce::jmax (0.0, at);
    pushUndo();
    arrangement::ensureClips (info->clips, sourceLength);
    if (! insert)
    {
        arrangement::pasteClip (info->clips, clipboard.clip, at, false);
        arrangement::pasteMidiHits (*info, clipboard.grid, at);
        arrangementEdited();
        sepLabel.setText (tr ("Tramo pegado encima en ") + formatTime (timeMap.toPlayback (at)), juce::dontSendNotification);
        return;
    }
    const double len = clipboard.clip.length();
    // Tempo de la sección donde se pega, para restaurarlo después del tramo insertado
    const int dstRegion = info->tempoRegionAt (at);
    const double dstBpm = dstRegion >= 0 ? info->tempoRegions[(size_t) dstRegion].origBpm : 0.0;
    const double dstPlay = dstRegion >= 0 ? info->tempoRegions[(size_t) dstRegion].playBpm : 0.0;
    arrangement::pasteClip (info->clips, clipboard.clip, at, true);
    arrangement::shiftGrid (*info, at, len);                 // abre el hueco en tiempos, acordes, marcadores, secciones y golpes MIDI
    arrangement::pasteGrid (info->analysis, clipboard.grid, at);
    arrangement::pasteMidiHits (*info, clipboard.grid, at);
    if (! clipboard.regions.empty() && dstBpm > 0.0)
    {
        // El tramo trae sus secciones de tempo; después de él vuelve el tempo de destino. Las vecinas iguales se unen.
        for (auto& r : clipboard.regions)
            info->tempoRegions.push_back ({ at + r.start, r.origBpm, r.playBpm });
        info->tempoRegions.push_back ({ at + len, dstBpm, dstPlay });
        info->sortTempoRegions();
        info->mergeEqualTempoRegions();
    }
    refreshFromInfo();
    sepLabel.setText (tr ("Tramo pegado en ") + formatTime (timeMap.toPlayback (at)) + juce::String::formatted (" (%.1f s insertados)", len), juce::dontSendNotification);
}

void MainComponent::duplicateClip (int index)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr)
        return;
    auto clips = info->clips;
    arrangement::ensureClips (clips, (double) sourceSong->length / engine.getSampleRate());
    if (! juce::isPositiveAndBelow (index, (int) clips.size()))
        return;
    const auto saved = clipboard;
    copyClip (index);
    const int mode = cutMode();
    cutModeBox.setSelectedId (1, juce::dontSendNotification);   // justo a continuación, sin ajustar a la rejilla
    pasteClipboard (clips[(size_t) index].end(), true);
    cutModeBox.setSelectedId (mode, juce::dontSendNotification);
    clipboard = saved.valid ? saved : clipboard;
}

void MainComponent::duplicateForCapture (double seconds)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr)
        return;
    auto clips = info->clips;
    arrangement::ensureClips (clips, (double) sourceSong->length / engine.getSampleRate());
    const int idx = arrangement::clipAt (clips, seconds);
    if (idx >= 0)
        duplicateClip (idx);
    std::cout << "captura: duplicado el tramo " << idx << ", tramos " << info->clips.size() << ", largo " << arrangement::lengthSeconds (info->clips, (double) sourceSong->length / engine.getSampleRate()) << "\n";
}

void MainComponent::clipDragged (double playbackSeconds, double delta, int lane)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr)
        return;
    const double sourceLength = (double) sourceSong->length / engine.getSampleRate();
    const double t0 = timeMap.toOriginal (playbackSeconds);
    double d0 = timeMap.toOriginal (playbackSeconds + delta) - t0;
    auto clips = info->clips;
    arrangement::ensureClips (clips, sourceLength);
    const int idx = arrangement::clipAt (clips, t0);
    if (idx < 0 || std::abs (d0) < 1.0e-4)
        return;
    const auto& clip = clips[(size_t) idx];
    juce::String note;
    if (cutMode() == 2)
    {
        // A la rejilla: el inicio del tramo cae en el tiempo más cercano
        const double target = nearestGridTime (clip.position + d0);
        if (target >= 0.0)
        {
            d0 = target - clip.position;
            note = tr (" (inicio en el tiempo)");
        }
    }
    else if (cutMode() == 3)
    {
        // A la transiente: el primer golpe del tramo (en su primer cuarto de segundo) cae en el tiempo más cercano
        const double onset = onsetNear (clip.position + 0.12, lane, 0.13);
        if (onset >= clip.position)
        {
            const double offset = onset - clip.position;
            const double target = nearestGridTime (clip.position + offset + d0);
            if (target >= 0.0)
            {
                d0 = target - offset - clip.position;
                note = tr (" (transiente en el tiempo)");
            }
        }
    }
    if (std::abs (d0) < 1.0e-4)
        return;
    pushUndo();
    info->clips = clips;
    const auto moved = info->clips[(size_t) idx];
    arrangement::moveClip (info->clips, idx, d0, false);
    arrangement::moveMidiHits (*info, moved.position, moved.end(), juce::jmax (d0, -moved.position));
    arrangementEdited();
    sepLabel.setText (juce::String::formatted ("Tramo desplazado %+.0f ms", d0 * 1000.0) + note, juce::dontSendNotification);
}

double MainComponent::nearestGridTime (double t) const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return -1.0;
    const auto& info = library.songs[(size_t) currentIndex];
    if (! info.analysis.beats.empty())
    {
        const int nb = info.analysis.nearestBeat (t, 0.5);
        return nb >= 0 ? info.analysis.beats[(size_t) nb].seconds : -1.0;
    }
    if (info.bpm <= 0.0)
        return -1.0;
    const double beat = 60.0 / info.bpm;   // rejilla fija del click
    return juce::jmax (0.0, info.clickOffset + std::round ((t - info.clickOffset) / beat) * beat);
}

double MainComponent::onsetNear (double t, int lane, double window) const
{
    if (arrangedSong == nullptr || arrangedSong->tracks.empty())
        return -1.0;
    const double sr = engine.getSampleRate();
    if (juce::isPositiveAndBelow (lane, (int) arrangedSong->tracks.size()))
        return arrangement::findOnset (arrangedSong->tracks[(size_t) lane]->buffer, sr, t, window);
    // Sin carril: la pista de batería si existe, si no la primera
    for (auto& tr : arrangedSong->tracks)
        if (Analyzer::isDrumsTrack (tr->name, {}))
            return arrangement::findOnset (tr->buffer, sr, t, window);
    return arrangement::findOnset (arrangedSong->tracks.front()->buffer, sr, t, window);
}

double MainComponent::snapCutTime (double t, int lane, juce::String& how) const
{
    how = {};
    if (cutMode() == 2)
    {
        const double g = nearestGridTime (t);
        if (g >= 0.0)
        {
            how = tr (" (al tiempo)");
            return g;
        }
        how = tr (" (sin rejilla: libre)");
    }
    else if (cutMode() == 3)
    {
        const double o = onsetNear (t, lane, 0.2);
        if (o >= 0.0)
        {
            const auto name = arrangedSong != nullptr && juce::isPositiveAndBelow (lane, (int) arrangedSong->tracks.size())
                                  ? arrangedSong->tracks[(size_t) lane]->name : juce::String();
            how = tr (" (transiente") + (name.isNotEmpty() ? " de " + name : juce::String()) + ")";
            return o;
        }
        how = tr (" (sin transiente cerca: libre)");
    }
    return t;
}

void MainComponent::editForCapture (double cutSeconds, double moveMs)
{
    auto* info = currentInfo();
    if (info == nullptr || sourceSong == nullptr)
        return;
    const double sourceLength = (double) sourceSong->length / engine.getSampleRate();
    int lane = -1;
    if (arrangedSong != nullptr)
        for (int i = 0; i < (int) arrangedSong->tracks.size(); ++i)
            if (Analyzer::isDrumsTrack (arrangedSong->tracks[(size_t) i]->name, {})) lane = i;
    juce::String how;
    cutSeconds = snapCutTime (cutSeconds, lane, how);
    std::cout << "captura: corte en " << cutSeconds << how << "\n";
    pushUndo();
    arrangement::ensureClips (info->clips, sourceLength);
    arrangement::cutAt (info->clips, cutSeconds, sourceLength);
    const int idx = arrangement::clipAt (info->clips, cutSeconds + 0.001);
    if (idx >= 0 && std::abs (moveMs) > 0.0)
    {
        const auto c = info->clips[(size_t) idx];
        arrangement::moveClip (info->clips, idx, moveMs / 1000.0, false);
        arrangement::moveMidiHits (*info, c.position, c.end(), juce::jmax (moveMs / 1000.0, -c.position));
    }
    arrangementEdited();
}

void MainComponent::clickControlsFromSong()
{
    const auto* info = currentInfo();
    const bool has = info != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &clickBtn, &bpmSlider, &offsetSlider, &clickGainSlider, &clickOutBox, &addMarkerBtn })
        c->setEnabled (has);
    if (! has)
    {
        mixer.setClickVisible (false);
        return;
    }

    tempoControlsFromSong();
    clickBtn.setToggleState (info->clickEnabled, juce::dontSendNotification);
    // Con análisis, el click sigue los tiempos detectados: BPM e inicio solo informan (el BPM, el de la sección que suena)
    const bool detected = ! info->analysis.beats.empty();
    bpmSlider.setEnabled (! detected);
    offsetSlider.setEnabled (! detected);
    bpmSlider.setValue (info->bpm, juce::dontSendNotification);
    offsetSlider.setValue (info->clickOffset, juce::dontSendNotification);
    clickGainSlider.setValue (info->clickGainDb, juce::dontSendNotification);
    fillOutputBox (clickOutBox, info->clickOutputPair);
    mixer.setClickVisible (true);
    mixer.refreshOutputs();
    mixer.setClickState (info->clickEnabled, info->clickGainDb, info->clickOutputPair);
}

void MainComponent::applyClick()
{
    if (auto* s = currentInfo())
        engine.setClick (s->clickEnabled, s->bpm, s->clickOffset, s->clickGainDb, s->clickOutputPair);
    syncTimelineMarkers();
}

void MainComponent::pushClick()
{
    applyClick();
    markSongDirty();
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Mantener el foco para los atajos de teclado / pedal, salvo mientras se escribe en un
    // editor de texto (los valores de los faders, el BPM...) o hay una ventana modal.
    const bool typing = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    if (isShowing() && ! typing && ! hasKeyboardFocus (false) && picker == nullptr
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
        && juce::Process::isForegroundProcess())
        grabKeyboardFocus();

    // Teclas retenidas que ya no están pulsadas (por si se perdió la liberación)
    for (int i = heldKeys.size(); --i >= 0;)
        if (! juce::KeyPress::isKeyCurrentlyDown (heldKeys[i]))
            heldKeys.remove (i);

    if (engine.needsReload.exchange (false))
    {
        if (rack != nullptr)
            rack->prepareAll (engine.getSampleRate());   // los plugins se preparan a la frecuencia nueva
        if (currentIndex >= 0)
            loadSongAt (currentIndex);   // guarda la mezcla y recarga a la nueva frecuencia
        if (mixEditor != nullptr)
            dropMixPreview();            // lo cargado para escuchar el mix está a la frecuencia anterior
    }

    if (mixEditor != nullptr)
        updateMixTimer();

    if (recording.active)
    {
        if (! recording.toBank && ! engine.isPlaying())
            stopRecording (false);   // la canción se detuvo (Stop, pausa o final): la toma se cierra
        else
        {
            const float pk = engine.takeInputPeak();
            sepLabel.setText (tr ("GRABANDO ") + ui::formatTime ((juce::Time::getMillisecondCounter() - recording.startedAt) / 1000.0)
                              + tr ("  ·  entrada ") + juce::String (juce::Decibels::gainToDecibels (pk, -60.0f), 0) + " dB"
                              + (engine.getDroppedSamples() > 0 ? tr ("  ·  se están perdiendo muestras") : juce::String()),
                              juce::dontSendNotification);
        }
    }

    juce::String deviceError;
    if (engine.takeDeviceError (deviceError))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                tr ("Problema con el dispositivo de audio"),
                                                deviceError + tr ("\n\nRevisa la configuración en el botón «Audio»."));

    if (songDirtySince != 0 && juce::Time::currentTimeMillis() - songDirtySince >= saveDelayMs)
        saveCurrentMix();
    if (tempoDirtySince != 0 && juce::Time::currentTimeMillis() - tempoDirtySince >= 800 && ! rendering)
        renderTempo();

    const double pos = engine.getPositionSeconds();
    const double len = engine.getLengthSeconds();
    timeLabel.setText (formatTime (pos) + " / " + formatTime (len), juce::dontSendNotification);

    if (std::abs (len - lastLength) > 0.001)
    {
        lastLength = len;
        positionSlider.setRange (0.0, juce::jmax (0.1, len), 0.0);
    }
    if (! positionSlider.isMouseButtonDown())
        positionSlider.setValue (pos, juce::dontSendNotification);

    playBtn.setButtonText (engine.isPlaying() ? "Pausa" : "Play");
    playBtn.setColour (juce::TextButton::buttonColourId,
                       engine.isPlaying() ? juce::Colours::green.darker (0.4f) : panelAlt);

    if (currentSong != nullptr)
    {
        double s, e;
        juce::String name;
        sectionAt (pos, s, e, name);
        sectionLabel.setText (name, juce::dontSendNotification);
        showChordAndKey (pos);
        updateStage (pos);
        if (auto* info = currentInfo())
            if (! info->analysis.beats.empty())
            {
                double orig = 0.0, play = 0.0;
                tempoAt (timeMap.toOriginal (pos), orig, play);
                if (std::abs (bpmSlider.getValue() - play) >= 0.05)
                    bpmSlider.setValue (play, juce::dontSendNotification);
            }
    }

    // Estado del análisis musical (comparte la barra de estado con el separador cuando este está libre)
    const auto an = analyzer.getState();
    if (an == Analyzer::State::done)
    {
        const auto result = analyzer.getResult();
        analyzer.reset();
        applyAnalysis (result);
    }
    else if (an == Analyzer::State::failed)
    {
        const auto msg = analyzer.getMessage();
        analyzer.reset();
        sepLabel.setText ({}, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("No se pudo analizar la canción"), msg);
    }
    else if (an == Analyzer::State::cancelled)
    {
        analyzer.reset();
        sepLabel.setText (tr ("Análisis cancelado."), juce::dontSendNotification);
    }

    timeline.setPlaying (engine.isPlaying());
    timeline.setPosition (pos);
    timeline.refreshTrackStates();
    mixer.tick (engine);
    if (auto* info = currentInfo(); info != nullptr && currentSong != nullptr)
        mixer.setLevelGains (sectionStemGains (*info, sectionIndexAt (timeMap.toOriginal (pos))), info->levelingEnabled);

    // Estado del separador
    const auto st = separator.getState();
    const bool running = st == Separator::State::running;
    const bool analyzing = analyzer.getState() == Analyzer::State::running;
    sepBar.setVisible (running || analyzing);
    cancelSepBtn.setVisible (running || analyzing);
    separateBtn.setEnabled (! running);
    analyzeBtn.setEnabled (! analyzing && currentSong != nullptr);
    levelBtn.setEnabled (currentSong != nullptr);

    if (running)
    {
        const float p = separator.getProgress();
        sepProgress = p < 0.0f ? -1.0 : (double) p;
        sepLabel.setText (separator.getSongName() + " [" + separator.getPlanDescription() + "]: " + separator.getMessage(),
                          juce::dontSendNotification);
    }
    else if (analyzing)
    {
        const float p = analyzer.getProgress();
        sepProgress = p < 0.0f ? -1.0 : (double) p;
        sepLabel.setText (tr ("Análisis: ") + analyzer.getMessage(), juce::dontSendNotification);
    }
    else if (st == Separator::State::done && separationTarget != juce::File())
    {
        // Separación de una canción del setlist: sus pistas se reemplazan por los stems
        int target = -1;
        for (int i = 0; i < (int) library.songs.size(); ++i)
            if (library.songs[(size_t) i].folder == separationTarget)
                target = i;
        const bool drumsOnly = separationStemIndex >= 0;
        const bool ok = target >= 0 && (drumsOnly ? library.replaceStem (target, separationStemIndex, separator.getResultFolder())
                                                  : library.replaceStems (target, separator.getResultFolder()));
        const auto name = target >= 0 ? library.songs[(size_t) target].name : juce::String();
        separator.reset();
        separationTarget = juce::File();
        separationStemIndex = -1;
        if (! ok)
            sepLabel.setText (tr ("No se pudieron reemplazar las pistas"), juce::dontSendNotification);
        else
        {
            sepLabel.setText (tr ("«") + name + (drumsOnly ? tr ("»: batería en partes, ") : tr ("» separada: "))
                              + juce::String ((int) library.songs[(size_t) target].stems.size()) + tr (" pistas"), juce::dontSendNotification);
            if (target == currentIndex)
                loadSongAt (currentIndex);   // recarga con los stems nuevos (con fundido si estaba sonando)
        }
        refreshSetlist();
    }
    else if (st == Separator::State::done)
    {
        const auto name = separator.getSongName();
        // Los stems son temporales propios: se mueven (instantáneo) en vez de copiarse
        const int idx = library.importStemFolder (separator.getResultFolder(), name, true);
        separator.reset();
        // Si la separación era la de un mix: la canción trae sus tiempos, compases, secciones y marcadores
        if (idx >= 0 && pendingMixSong != nullptr && name == pendingMixSongName)
            applyMixSongInfo (idx, *pendingMixSong);
        clearPendingMixSong();
        refreshSetlist();
        if (idx < 0)
            sepLabel.setText (tr ("No se pudieron importar los stems"), juce::dontSendNotification);
        else if (engine.isPlaying())
            sepLabel.setText (tr ("«") + name + tr ("» separada y agregada al final del setlist"), juce::dontSendNotification);
        else if (mixEditor != nullptr)
        {
            // Armando un mix: no se cierra bajo el usuario; la canción queda en el setlist
            sepLabel.setText (tr ("«") + name + tr ("» separada y agregada al setlist (el mix sigue abierto)"), juce::dontSendNotification);
            mixEditor->setStatus (tr ("«") + name + tr ("» ya está en el setlist"));
        }
        else
        {
            // Solo se cambia a la canción nueva si no hay nada sonando
            sepLabel.setText (tr ("«") + name + tr ("» separada y agregada al setlist"), juce::dontSendNotification);
            setlist.selectRow (idx);
        }
    }
    else if (st == Separator::State::cancelled)
    {
        clearPendingMixSong();
        separator.reset();
        separationTarget = juce::File();
        separationStemIndex = -1;
        sepLabel.setText (tr ("Separación cancelada."), juce::dontSendNotification);
    }
    else if (st == Separator::State::failed)
    {
        clearPendingMixSong();
        const auto msg = separator.getMessage();
        separator.reset();
        separationTarget = juce::File();
        separationStemIndex = -1;
        sepLabel.setText ({}, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                tr ("No se pudo separar la canción"), msg);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Cambió el dispositivo de audio: actualizar las salidas disponibles y guardar la configuración
    // (ApplicationProperties la escribe sola a los pocos segundos; así sobrevive a un cierre brusco)
    mixer.refreshOutputs();
    if (auto* info = currentInfo())
        fillOutputBox (clickOutBox, info->clickOutputPair);
    if (auto xml = deviceManager.createStateXml())
        props.getUserSettings()->setValue ("audioDevice", xml.get());
}

//==============================================================================
juce::String MainComponent::pythonPath() const
{
    const auto def = venvPython ("demucs-env");
    auto p = props.getUserSettings()->getValue ("pythonPath", def).trim();
    if (p.startsWith ("~"))
        p = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() + p.substring (1);
    return p;
}

SeparationOptions MainComponent::selectedOptions() const
{
    SeparationOptions o;
    o.stems = stemsBox.getSelectedId() == 2 ? 6 : 4;
    o.quality = juce::jlimit (0, 2, qualityBox.getSelectedId() - 1);
    // Con calidad alta o máxima, las voces se sacan con BS-Roformer si audio-separator está instalado
    o.roformerVocals = o.quality >= 1 && Separator::isRoformerAvailable (pythonPath());
    o.drumParts = drumPartsBtn.getToggleState() && Separator::isRoformerAvailable (pythonPath());
    return o;
}

void MainComponent::askText (const juce::String& title, const juce::String& initial,
                             std::function<void (const juce::String&)> onOk)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int result)
    {
        if (result == 1 && onOk)
            onOk (w->getTextEditorContents ("text"));
    }), true);
}

void MainComponent::pickFiles (const juce::String& title, int browserFlags, const juce::File& startDir, const juce::String& patterns,
                               std::function<void (const juce::Array<juce::File>&)> onDone)
{
   #if JUCE_MAC
    chooser = std::make_unique<juce::FileChooser> (title, startDir, patterns, true);
    chooser->launchAsync (browserFlags, [onDone] (const juce::FileChooser& fc) { if (onDone) onDone (fc.getResults()); });
   #else
    // Panel dentro de la ventana: no depende del gestor de ventanas (en GNOME las ventanas nuevas
    // se abrían detrás de la principal y, al ser modales, la app parecía colgada)
    picker = std::make_unique<FilePicker> (title, browserFlags, startDir, patterns,
                                           [this, onDone] (const juce::Array<juce::File>& files)
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe, onDone, files]
        {
            if (safe == nullptr)
                return;
            safe->picker.reset();
            safe->grabKeyboardFocus();
            if (! files.isEmpty() && onDone)
                onDone (files);
        });
    });
    addAndMakeVisible (*picker);
    picker->setBounds (getLocalBounds());
    picker->toFront (true);
    picker->grabKeyboardFocus();
   #endif
}

void MainComponent::chooseStems()
{
    pickFiles (tr ("Elige los stems de una canción (varios archivos o una carpeta)"),
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                   | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
               juce::File::getSpecialLocation (juce::File::userMusicDirectory), Library::audioFilePatterns(),
               [this] (const juce::Array<juce::File>& files) { importStems (files); });
}

void MainComponent::importStems (const juce::Array<juce::File>& selection)
{
    juce::Array<juce::File> audio;
    juce::String defaultName;

    // Carpetas de canción exportadas (song.json + stems): se importan con todos sus ajustes
    int lastProject = -1, projects = 0;
    for (auto& f : selection)
        if (Library::isProjectFolder (f))
        {
            const int idx = library.importProject (f);
            if (idx >= 0)
            {
                lastProject = idx;
                ++projects;
            }
        }
    if (projects > 0)
    {
        refreshSetlist();
        if (! engine.isPlaying())
            setlist.selectRow (lastProject);
        sepLabel.setText (juce::String (projects) + tr (projects == 1 ? " canción importada con sus ajustes" : " canciones importadas con sus ajustes"), juce::dontSendNotification);
        return;
    }

    for (auto& f : selection)
    {
        if (f.isDirectory())
        {
            audio.addArray (Library::audioFilesIn (f));
            if (defaultName.isEmpty())
                defaultName = f.getFileName();
        }
        else if (Library::isAudioFile (f))
        {
            audio.add (f);
            if (defaultName.isEmpty())
                defaultName = f.getParentDirectory().getFileName();
        }
    }

    if (audio.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Importar stems",
                                                tr ("No encontré archivos de audio (") + Library::audioFormatsDescription() + ").");
        return;
    }

    askText (tr ("Nombre de la canción"), defaultName, [this, audio] (const juce::String& name)
    {
        const int idx = library.importStemFiles (audio, name);
        refreshSetlist();
        if (idx < 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Importar stems",
                                                    tr ("No se pudieron copiar los archivos a la biblioteca."));
        else if (! engine.isPlaying())
            setlist.selectRow (idx);   // con algo sonando, solo se agrega al setlist
    });
}

void MainComponent::chooseSongToSeparate()
{
    auto pickFile = [this]
    {
        pickFiles (tr ("Elige la canción completa a separar"),
                   juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), Library::audioFilePatterns(),
                   [this] (const juce::Array<juce::File>& files) { if (files.size() > 0 && files[0].existsAsFile()) startSeparation (files[0]); });
    };
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || arrangedSong == nullptr)
    {
        pickFile();
        return;
    }
    // Con una canción cargada, lo normal es separar esa: sus pistas se reemplazan por los stems
    const int numStems = (int) info->stems.size();
    auto* w = new juce::AlertWindow (tr ("Separar canción (IA)"),
                                     tr ("«") + info->name + tr ("» tiene ") + juce::String (numStems) + (numStems == 1 ? tr (" pista") : tr (" pistas"))
                                         + tr (". Separar la canción seleccionada reemplaza sus pistas por los stems (el audio actual queda guardado en la subcarpeta «original») "
                                               "y conserva marcadores, análisis, tempo y cortes.\n\nTambién puedes elegir otro archivo, que se agrega como canción nueva."),
                                     juce::MessageBoxIconType::QuestionIcon, this);
    w->addButton (tr ("Separar «") + info->name + tr ("»"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    bool hasDrums = false;
    for (auto& st : info->stems)
        hasDrums = hasDrums || Analyzer::isDrumsTrack (st.name, st.fileName);
    if (hasDrums && Separator::isRoformerAvailable (pythonPath()))
        w->addButton (tr ("Solo la batería en partes"), 3);
    w->addButton (tr ("Elegir un archivo..."), 2);
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, pickFile] (int result)
    {
        if (result == 1)
            separateCurrentSong();
        else if (result == 3)
            separateDrumsOfCurrentSong();
        else if (result == 2)
            pickFile();
    }), true);
}

void MainComponent::separateCurrentSong()
{
    auto* info = currentInfo();
    if (info == nullptr || arrangedSong == nullptr || loadedFolder != info->folder)
        return;
    if (separator.getState() == Separator::State::running)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Separar", tr ("Ya hay una separación en curso. Espera a que termine."));
        return;
    }
    juce::File input;
    if (info->stems.size() == 1)
        input = info->folder.getChildFile (info->stems[0].fileName);   // una sola pista: el archivo tal cual, sin remezclar
    if (! input.existsAsFile())
    {
        // Varias pistas: la mezcla del arreglo (línea de tiempo de la canción) a un WAV temporal
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("secuencias_mezcla");
        dir.createDirectory();
        input = dir.getNonexistentChildFile ("mezcla", ".wav", false);
        std::vector<bool> all (arrangedSong->tracks.size(), true);
        juce::String error;
        if (! Analyzer::writeMix (*arrangedSong, all, engine.getSampleRate(), input, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Separar", tr ("No se pudo escribir la mezcla: ") + error);
            return;
        }
    }
    separationTarget = info->folder;
    separationStemIndex = -1;
    startSeparation (input);
    if (separator.getState() != Separator::State::running)
        separationTarget = juce::File();
}

void MainComponent::separateDrumsOfCurrentSong()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || loadedFolder != info->folder)
        return;
    if (separator.getState() == Separator::State::running)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Separar", tr ("Ya hay una separación en curso. Espera a que termine."));
        return;
    }
    int drumsIndex = -1;
    for (int i = 0; i < (int) info->stems.size(); ++i)
        if (Analyzer::isDrumsTrack (info->stems[(size_t) i].name, info->stems[(size_t) i].fileName))
            drumsIndex = i;
    if (drumsIndex < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Separar", tr ("Esta canción no tiene una pista de batería separada."));
        return;
    }
    const auto py = pythonPath();
    if (! Separator::isRoformerAvailable (py))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Falta audio-separator"),
            tr ("Partir la batería usa el modelo DrumSep de audio-separator, que no está instalado (o falta ffmpeg).\n\n"
                "Instálalo desde «Ajustes IA» > «Instalar también Roformer y DrumSep»."));
        return;
    }
    const auto file = info->folder.getChildFile (info->stems[(size_t) drumsIndex].fileName);
    separationTarget = info->folder;
    separationStemIndex = drumsIndex;
    if (! separator.startDrumParts (file, py, info->name))
    {
        separationTarget = juce::File();
        separationStemIndex = -1;
    }
}

void MainComponent::startSeparation (const juce::File& file)
{
    startSeparation (file, file.getFileNameWithoutExtension());
}

bool MainComponent::startSeparation (const juce::File& file, const juce::String& songName)
{
    if (separator.getState() == Separator::State::running)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Separar",
                                                tr ("Ya hay una separación en curso. Espera a que termine."));
        return false;
    }

    const auto py = pythonPath();
    if (! juce::File::isAbsolutePath (py) || ! juce::File (py).existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Falta Demucs"),
            tr ("No encuentro Python con Demucs en:\n") + py
            + tr ("\n\nInstálalo desde «Ajustes IA» > «Instalar motores de IA» (o cambia ahí la ruta si ya lo tienes en otro lugar)."));
        return false;
    }

    if (drumPartsBtn.getToggleState() && ! Separator::isRoformerAvailable (py))
        sepLabel.setText (tr ("«Batería en partes» necesita audio-separator y ffmpeg (Ajustes IA > Instalar también Roformer y DrumSep): se separa sin partir la batería"), juce::dontSendNotification);
    return separator.start (file, py, selectedOptions(), songName);
}

void MainComponent::filesDropped (const juce::StringArray& paths, int, int)
{
    juce::Array<juce::File> files;
    for (auto& p : paths)
        files.add (juce::File (p));

    if (mixEditor != nullptr)
    {
        addMixSourceFiles (files);        // armando un mix: lo que se arrastra son canciones para el mix
        return;
    }
    if (files.size() == 1 && Library::isAudioFile (files[0]))
        startSeparation (files[0]);       // una canción completa → separar
    else
        importStems (files);              // varios archivos o carpeta → stems
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 32, 2, 64, false, false, true, false);
    selector->setSize (560, 440);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = tr ("Configuración de audio");
    o.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showAiSettings()
{
    const bool demucsOk = juce::File (pythonPath()).existsAsFile();
    const bool madmomOk = Analyzer::isAvailable (analysisPythonPath());
    juce::String estado = tr ("Demucs (separar): ") + (demucsOk ? tr ("instalado") : tr ("no encontrado")) + "\n"
                        + tr ("madmom (tempo y acordes): ") + (madmomOk ? tr ("instalado") : tr ("no encontrado")) + "\n\n"
                        + tr ("«Instalar» abre una terminal que crea los entornos de Python en tu usuario (tarda unos minutos y descarga varios GB). Se puede repetir sin problema.");
    const bool separatorOk = Separator::isRoformerAvailable (pythonPath());
    estado += tr ("\n\nRoformer y DrumSep (voces de mejor calidad y batería en partes, con audio-separator y ffmpeg): ") + (separatorOk ? tr ("instalados") : tr ("no encontrados"));
    auto* w = new juce::AlertWindow (tr ("Ajustes IA"), estado, juce::MessageBoxIconType::NoIcon, this);
    w->addButton (tr ("Instalar motores de IA"), 1);
    w->addButton (tr ("Instalar también Roformer y DrumSep"), 3);
    w->addButton (tr ("Rutas de Python..."), 2);
    w->addButton ("Cerrar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 || result == 3)
        {
            launchInstaller (result == 3);
            return;
        }
        if (result != 2)
            return;
        askText (tr ("Ruta de Python con Demucs instalado"), pythonPath(), [this] (const juce::String& p)
        {
            if (p.trim().isNotEmpty())
                props.getUserSettings()->setValue ("pythonPath", p.trim());
            askText (tr ("Ruta de Python con madmom (análisis de tempo y acordes)"), analysisPythonPath(), [this] (const juce::String& a)
            {
                if (a.trim().isNotEmpty())
                    props.getUserSettings()->setValue ("analysisPythonPath", a.trim());
            });
        });
    }), true);
}

juce::File MainComponent::installerScript() const
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
   #if JUCE_WINDOWS
    const juce::String name = "instalar-ia.ps1";
   #else
    const juce::String name = "instalar-ia.sh";
   #endif
    const juce::File candidates[] = {
        exe.getSiblingFile (name),                                                                        // instalador de Windows y paquete .deb (/opt/secuencias)
        exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/" + name),                 // macOS: Secuencias.app/Contents/Resources
        home.getChildFile (".local/share/secuencias/" + name),                                            // install.sh
        exe.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("scripts/" + name),   // build/<preset>/Secuencias_artefacts/<config>/
        exe.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("scripts/" + name)
    };
    for (auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return {};
}

void MainComponent::launchInstaller (bool withRoformer)
{
    const auto script = installerScript();
    if (! script.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Ajustes IA"),
                                                tr ("No encontré el instalador (scripts/instalar-ia.sh). Puedes ejecutarlo desde una terminal siguiendo el LEEME."));
        return;
    }
    bool launched = false;
    juce::ChildProcess proc;
    const juce::String extra = withRoformer ? " --roformer" : "";
   #if JUCE_MAC
    const auto cmd = "bash " + script.getFullPathName().quoted() + extra;
    launched = proc.start (juce::StringArray { "osascript", "-e", "tell application \"Terminal\" to activate",
                                               "-e", "tell application \"Terminal\" to do script " + cmd.quoted() });
   #elif JUCE_WINDOWS
    launched = proc.start ("powershell -NoExit -ExecutionPolicy Bypass -File " + script.getFullPathName().quoted() + (withRoformer ? " -Roformer" : ""));
   #else
    juce::StringArray runArgs { "bash", script.getFullPathName() };
    if (withRoformer)
        runArgs.add ("--roformer");
    for (auto& term : { juce::StringArray ("x-terminal-emulator", "-e") , juce::StringArray ("gnome-terminal", "--"),
                        juce::StringArray ("konsole", "-e"), juce::StringArray ("xterm", "-e") })
    {
        juce::StringArray full (term);
        full.addArray (runArgs);
        if (proc.start (full))
        {
            launched = true;
            break;
        }
    }
   #endif
    if (! launched)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Ajustes IA"),
                                                tr ("No pude abrir una terminal. Ejecuta a mano:\n\n")
                                               #if JUCE_WINDOWS
                                                + "powershell -ExecutionPolicy Bypass -File " + script.getFullPathName().quoted());
                                               #else
                                                + "bash " + script.getFullPathName().quoted());
                                               #endif
    else
        sepLabel.setText (tr ("Instalador de IA abierto en una terminal"), juce::dontSendNotification);
}

//==============================================================================
double MainComponent::levelTargetLufs() const
{
    switch (targetBox.getSelectedId())
    {
        case 1:  return -12.0;
        case 3:  return -16.0;
        case 4:  return -18.0;
        default: return -14.0;
    }
}

void MainComponent::recomputeSectionGains (SongInfo& info) const
{
    const double target = levelTargetLufs();
    info.headGainDb = loudness::gainToTarget ({ info.headLufs, info.headTruePeakDb }, target);
    for (auto& m : info.markers)
        m.gainDb = loudness::gainToTarget ({ m.lufs, m.truePeakDb }, target);
}

void MainComponent::updateGainCurve()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || loadedFolder != info->folder || ! info->levelingEnabled)
    {
        engine.setGainCurve (nullptr);
        timeline.setLevelingEnabled (false);
        timeline.refreshWaveforms();
        return;
    }
    auto curve = std::make_shared<GainCurve>();
    const double sr = engine.getSampleRate();
    curve->positions.push_back (0);
    curve->gains.push_back (juce::Decibels::decibelsToGain ((float) info->headGainDb, -60.0f));
    for (auto& m : info->markers)
    {
        curve->positions.push_back ((juce::int64) std::llround (timeMap.toPlayback (m.seconds) * sr));
        curve->gains.push_back (juce::Decibels::decibelsToGain ((float) m.gainDb, -60.0f));
    }
    // Por pista: trackGains[stem][tramo]
    const size_t numStems = info->stems.size();
    curve->trackGains.assign (numStems, std::vector<float> (info->markers.size() + 1, 1.0f));
    for (size_t stem = 0; stem < numStems; ++stem)
    {
        if (stem < info->headStemGainsDb.size())
            curve->trackGains[stem][0] = juce::Decibels::decibelsToGain ((float) info->headStemGainsDb[stem], -60.0f);
        for (size_t i = 0; i < info->markers.size(); ++i)
            if (stem < info->markers[i].stemGainsDb.size())
                curve->trackGains[stem][i + 1] = juce::Decibels::decibelsToGain ((float) info->markers[i].stemGainsDb[stem], -60.0f);
    }
    engine.setGainCurve (std::move (curve));
    timeline.setLevelingEnabled (true);
    timeline.refreshWaveforms();
}

bool MainComponent::needsLevelMeasure (const SongInfo& info)
{
    if (info.headLufs <= unmeasuredDb + 1.0)
        return true;
    for (auto& m : info.markers)
        if (m.lufs <= unmeasuredDb + 1.0)
            return true;
    return false;
}

void MainComponent::remeasureIfLeveling()
{
    // Los tramos son los marcadores: al añadir, mover o borrar uno, las mediciones ya no corresponden
    auto* info = currentInfo();
    if (info != nullptr && info->levelingEnabled && currentSong != nullptr && loadedFolder == info->folder && ! isLeveling())
        measureAndLevel();
}

void MainComponent::measureAndLevel()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || loadedFolder != info->folder)
        return;
    // Se mide el arreglo (la línea de tiempo de la canción, donde viven los marcadores) sin estirar: el estirado conserva la sonoridad
    const double sr = engine.getSampleRate();
    auto song = arrangedSong != nullptr ? arrangedSong : currentSong;
    const double length = (double) song->length / sr;
    std::vector<loudness::Section> sections;
    sections.push_back ({ 0.0, info->markers.empty() ? length : info->markers[0].seconds, {} });
    for (size_t i = 0; i < info->markers.size(); ++i)
        sections.push_back ({ info->markers[i].seconds, i + 1 < info->markers.size() ? info->markers[i + 1].seconds : length, {} });

    sepLabel.setText (tr ("Midiendo sonoridad de los tramos..."), juce::dontSendNotification);
    const int gen = loadGeneration.load();
    juce::Component::SafePointer<MainComponent> safe (this);
    levelPool.addJob ([this, safe, song, sr, sections, gen]
    {
        // 1) Cada stem por tramo: la ganancia por pista lleva cada tramo al nivel del stem en toda la canción.
        //    Tope ±12 dB (más que eso ya no es balance sino un instrumento que no toca) y un stem casi
        //    ausente en el tramo (bajo -50 LUFS) se deja como está: subirlo solo levantaría ruido y sangrado.
        constexpr double maxStemGainDb = 12.0, absentLufs = -50.0;
        std::vector<StemLevel> stems;
        for (auto& t : song->tracks)
        {
            if (abortJobs.load() || gen != loadGeneration.load())
                return;
            StemLevel sl;
            sl.stemIndex = t->stemIndex;
            const auto r = loudness::measure (t->buffer, sr, sections);
            sl.whole = r.whole.lufs;
            for (auto& sec : r.sections)
            {
                sl.sections.push_back (sec.result.lufs);
                const bool measured = sl.whole > unmeasuredDb + 1.0 && sec.result.lufs > absentLufs;
                sl.gainsDb.push_back (measured ? juce::jlimit (-maxStemGainDb, maxStemGainDb, std::round ((sl.whole - sec.result.lufs) * 10.0) / 10.0) : 0.0);
            }
            stems.push_back (sl);
        }
        // 2) La suma con esas ganancias aplicadas (faders a 0 dB), sin normalizar: de ahí sale la ganancia general por tramo
        juce::AudioBuffer<float> mix (2, (int) song->length);
        mix.clear();
        for (size_t ti = 0; ti < song->tracks.size(); ++ti)
        {
            auto& t = song->tracks[ti];
            for (size_t si = 0; si < sections.size(); ++si)
            {
                const int a = juce::jlimit (0, (int) song->length, (int) std::llround (sections[si].startSeconds * sr));
                const int b = juce::jlimit (0, (int) song->length, (int) std::llround (sections[si].endSeconds * sr));
                if (b <= a)
                    continue;
                const float g = juce::Decibels::decibelsToGain ((float) stems[ti].gainsDb[si], -60.0f);
                for (int ch = 0; ch < 2; ++ch)
                    mix.addFrom (ch, a, t->buffer, ch, a, b - a, g);
            }
        }
        const auto result = loudness::measure (mix, sr, sections);
        juce::MessageManager::callAsync ([safe, result, stems, gen]
        {
            if (auto* self = safe.getComponent())
                if (gen == self->loadGeneration.load())
                    self->applyLeveling (result, stems);
        });
    });
}

int MainComponent::sectionIndexAt (double songSeconds) const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return 0;
    const auto& info = library.songs[(size_t) currentIndex];
    int section = 0;
    for (size_t i = 0; i < info.markers.size(); ++i)
        if (info.markers[i].seconds <= songSeconds + 1.0e-6)
            section = (int) i + 1;
    return section;
}

std::vector<double> MainComponent::sectionStemGains (const SongInfo& info, int section) const
{
    if (section <= 0)
        return info.headStemGainsDb;
    if (juce::isPositiveAndBelow (section - 1, (int) info.markers.size()))
        return info.markers[(size_t) section - 1].stemGainsDb;
    return {};
}

float MainComponent::levelGainAt (int stemIndex, double playbackSeconds) const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return 1.0f;
    const auto& info = library.songs[(size_t) currentIndex];
    if (! info.levelingEnabled)
        return 1.0f;
    const int section = sectionIndexAt (timeMap.toOriginal (playbackSeconds));
    double db = section <= 0 ? info.headGainDb : info.markers[(size_t) section - 1].gainDb;
    const auto stems = sectionStemGains (info, section);
    if (juce::isPositiveAndBelow (stemIndex, (int) stems.size()))
        db += stems[(size_t) stemIndex];
    return juce::Decibels::decibelsToGain ((float) db, -60.0f);
}

void MainComponent::stemLevelEdited (int stemIndex, double db)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || loadedFolder != info->folder)
        return;
    info->fitStemArrays();
    if (! juce::isPositiveAndBelow (stemIndex, (int) info->stems.size()))
        return;
    const int section = sectionIndexAt (timeMap.toOriginal (engine.getPositionSeconds()));
    auto& gains = section <= 0 ? info->headStemGainsDb : info->markers[(size_t) section - 1].stemGainsDb;
    gains[(size_t) stemIndex] = juce::jlimit (-40.0, 40.0, db);
    info->levelingEnabled = true;
    levelBtn.setToggleState (true, juce::dontSendNotification);
    library.saveSong (*info);
    updateGainCurve();
}

void MainComponent::applyLeveling (const loudness::Result& r, const std::vector<StemLevel>& stems)
{
    auto* info = currentInfo();
    if (info == nullptr || r.sections.size() != info->markers.size() + 1)
        return;
    info->loudnessLufs = r.whole.lufs;
    info->truePeakDb = r.whole.truePeakDb;
    info->headLufs = r.sections[0].result.lufs;
    info->headTruePeakDb = r.sections[0].result.truePeakDb;
    for (size_t i = 0; i < info->markers.size(); ++i)
    {
        info->markers[i].lufs = r.sections[i + 1].result.lufs;
        info->markers[i].truePeakDb = r.sections[i + 1].result.truePeakDb;
    }
    // Por pista: la medición y la ganancia propuesta de cada tramo (se pueden retocar en el mezclador)
    info->fitStemArrays();
    for (auto& sl : stems)
    {
        if (! juce::isPositiveAndBelow (sl.stemIndex, (int) info->stems.size()) || sl.sections.size() != r.sections.size())
            continue;
        const auto stem = (size_t) sl.stemIndex;
        info->stemSongLufs[stem] = sl.whole;
        info->headStemLufs[stem] = sl.sections[0];
        info->headStemGainsDb[stem] = sl.gainsDb[0];
        for (size_t i = 0; i < info->markers.size(); ++i)
        {
            info->markers[i].stemLufs[stem] = sl.sections[i + 1];
            info->markers[i].stemGainsDb[stem] = sl.gainsDb[i + 1];
        }
    }
    recomputeSectionGains (*info);
    info->levelingEnabled = true;
    levelBtn.setToggleState (true, juce::dontSendNotification);
    library.saveSong (*info);
    updateGainCurve();
    rebuildMarkers();
    juce::String text = juce::String::formatted ("Nivelado a %.0f LUFS: cancion %.1f LUFS, pico %.1f dBTP, %d tramos",
                                                 levelTargetLufs(), r.whole.lufs, r.whole.truePeakDb, (int) r.sections.size());
    sepLabel.setText (text.replace ("cancion", tr ("canción")), juce::dontSendNotification);
}

void MainComponent::levelSetlist()
{
    if (library.songs.empty())
        return;
    // Copia de lo necesario para leer cada canción fuera del hilo de la interfaz
    std::vector<SongInfo> songs = library.songs;
    const double sr = engine.getSampleRate();
    const double target = levelTargetLufs();
    sepLabel.setText (tr ("Nivelando setlist..."), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    levelPool.addJob ([this, safe, songs, sr, target]
    {
        int done = 0;
        for (auto info : songs)
        {
            if (abortJobs.load())
                return;
            ++done;
            juce::MessageManager::callAsync ([safe, done, n = (int) songs.size(), name = info.name]
            {
                if (safe != nullptr)
                    safe->sepLabel.setText (tr ("Nivelando setlist: ") + juce::String (done) + "/" + juce::String (n) + " " + name, juce::dontSendNotification);
            });
            if (info.loudnessLufs <= unmeasuredDb + 1.0)
            {
                auto song = AudioEngine::loadSong (info, sr, formatManager, [this] { return abortJobs.load(); });
                if (song == nullptr || song->tracks.empty())
                    continue;
                juce::AudioBuffer<float> mix (2, (int) song->length);
                mix.clear();
                for (auto& t : song->tracks)
                    for (int ch = 0; ch < 2; ++ch)
                        mix.addFrom (ch, 0, t->buffer, ch, 0, (int) song->length);
                const auto r = loudness::measure (mix, sr, {});
                info.loudnessLufs = r.whole.lufs;
                info.truePeakDb = r.whole.truePeakDb;
            }
            const double gain = loudness::gainToTarget ({ info.loudnessLufs, info.truePeakDb }, target);
            juce::MessageManager::callAsync ([safe, folder = info.folder, lufs = info.loudnessLufs, peak = info.truePeakDb, gain]
            {
                auto* self = safe.getComponent();
                if (self == nullptr)
                    return;
                for (auto& s : self->library.songs)
                    if (s.folder == folder)
                    {
                        s.loudnessLufs = lufs;
                        s.truePeakDb = peak;
                        s.songGainDb = gain;
                        self->library.saveSong (s);
                        if (self->currentInfo() == &s)
                            self->engine.setSongGain (juce::Decibels::decibelsToGain ((float) gain, -60.0f));
                    }
            });
        }
        juce::MessageManager::callAsync ([safe, target, n = (int) songs.size()]
        {
            if (safe != nullptr)
                safe->sepLabel.setText (juce::String::formatted ("Setlist nivelado a %.0f LUFS (%d canciones)", target, n), juce::dontSendNotification);
        });
    });
}

//==============================================================================
juce::String MainComponent::analysisPythonPath() const
{
    const auto def = venvPython ("analisis-env");
    auto p = props.getUserSettings()->getValue ("analysisPythonPath", def).trim();
    if (p.startsWith ("~"))
        p = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() + p.substring (1);
    return p;
}

void MainComponent::startAnalysis()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || loadedFolder != info->folder)
        return;
    if (analyzer.getState() == Analyzer::State::running)
        return;
    const auto py = analysisPythonPath();
    if (! Analyzer::isAvailable (py))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Falta el analizador"),
            tr ("No encuentro el Python del análisis en:\n") + py
            + tr ("\n\nInstálalo una vez en la Terminal (usa un entorno aparte del de Demucs):\n\n"
                  "python3 -m venv ~/analisis-env\n"
                  "~/analisis-env/bin/pip install --upgrade pip wheel \"numpy<2\" \"cython<3\"\n"
                  "~/analisis-env/bin/pip install --no-build-isolation git+https://github.com/CPJKU/madmom\n\n"
                  "Si lo instalaste en otro lugar, cambia la ruta en «Ajustes IA» (segunda pregunta)."));
        return;
    }
    std::vector<bool> drums;
    for (auto& t : currentSong->tracks)
    {
        const auto file = juce::isPositiveAndBelow (t->stemIndex, (int) info->stems.size()) ? info->stems[(size_t) t->stemIndex].fileName : juce::String();
        drums.push_back (Analyzer::isDrumsTrack (t->name, file));
    }
    sepLabel.setText (tr ("Análisis: preparando..."), juce::dontSendNotification);
    analyzer.start (arrangedSong != nullptr ? arrangedSong : currentSong, engine.getSampleRate(), drums, py);   // el arreglo, sin estirar
}

void MainComponent::applyAnalysis (const Analysis& a)
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    info->analysis = a;
    if (a.bpm > 0.0)
        info->bpm = juce::jlimit (20.0, 400.0, std::round (a.bpm * 10.0) / 10.0);
    const int firstDownbeat = a.downbeatAfter (0.0);
    if (firstDownbeat >= 0)
        info->clickOffset = a.beats[(size_t) firstDownbeat].seconds;   // el click y la regla arrancan en el primer compás
    info->tempoRegions = detectTempoRegions (a, info->bpm);           // secciones con tempo distinto (se pierden los ajustes por sección)
    library.saveSong (*info);
    clickControlsFromSong();
    applyClick();
    rebuildTimeMap();
    timeline.setAnalysis (mappedAnalysis());
    updateBeatGrid();
    rebuildMarkers();
    showChordAndKey (engine.getPositionSeconds());
    juce::String summary = tr ("Análisis listo");
    if (a.key.isNotEmpty()) summary += ": " + a.key;
    if (a.bpm > 0.0) summary += juce::String::formatted (", %.1f BPM", a.bpm);
    if (info->tempoRegions.size() > 1) summary += ", " + juce::String ((int) info->tempoRegions.size()) + tr (" secciones de tempo");
    summary += ", " + juce::String ((int) a.chords.size()) + tr (" acordes");
    sepLabel.setText (summary, juce::dontSendNotification);
}

void MainComponent::currentChords (double pos, juce::String& now, juce::String& next) const
{
    now = next = {};
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return;
    const auto& a = library.songs[(size_t) currentIndex].analysis;
    const bool flats = useFlats();
    const int i = a.chordAt (timeMap.toOriginal (pos));
    if (i >= 0 && a.chords[(size_t) i].name != "N")
        now = music::spellChord (a.chords[(size_t) i].name, flats);
    else if (i >= 0 || ! a.chords.empty())
        now = "-";
    // Siguiente acorde distinto, para anticiparse
    for (int j = juce::jmax (0, i + 1); j < (int) a.chords.size(); ++j)
        if (a.chords[(size_t) j].name != "N" && (i < 0 || a.chords[(size_t) j].name != a.chords[(size_t) i].name))
        {
            next = music::spellChord (a.chords[(size_t) j].name, flats);
            break;
        }
}

void MainComponent::barAndBeat (double pos, int& bar, int& beat, int& beatsInBar, double& progress) const
{
    bar = beat = 0;
    beatsInBar = 4;
    progress = 0.0;
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return;
    const auto& info = library.songs[(size_t) currentIndex];
    const auto& beats = info.analysis.beats;
    const double orig = timeMap.toOriginal (pos);
    if (beats.size() >= 2)
    {
        int i = -1;
        for (int k = 0; k < (int) beats.size(); ++k)
            if (beats[(size_t) k].seconds <= orig + 1.0e-6) i = k; else break;
        if (i < 0)
            return;   // antes del primer tiempo detectado
        beat = beats[(size_t) i].beatInBar;
        int down = i;
        while (down > 0 && beats[(size_t) down].beatInBar != 1) --down;
        for (int k = 0; k <= i; ++k)
            if (beats[(size_t) k].beatInBar == 1) ++bar;
        bar = juce::jmax (1, bar);
        int nextDown = (int) beats.size();
        for (int k = down + 1; k < (int) beats.size(); ++k)
            if (beats[(size_t) k].beatInBar == 1) { nextDown = k; break; }
        beatsInBar = juce::jlimit (1, 12, nextDown - down);
        if (i + 1 < (int) beats.size())
            progress = juce::jlimit (0.0, 1.0, (orig - beats[(size_t) i].seconds) / juce::jmax (1.0e-6, beats[(size_t) i + 1].seconds - beats[(size_t) i].seconds));
        return;
    }
    // Rejilla fija del click (4/4)
    if (info.bpm <= 0.0)
        return;
    const double beatLen = 60.0 / info.bpm, rel = orig - info.clickOffset;
    if (rel < 0.0)
        return;
    const auto k = (juce::int64) std::floor (rel / beatLen);
    beat = (int) (k % 4) + 1;
    bar = (int) (k / 4) + 1;
    beatsInBar = 4;
    progress = juce::jlimit (0.0, 1.0, (rel - (double) k * beatLen) / beatLen);
}

void MainComponent::updateStage (double pos)
{
    if (stageWindow == nullptr || ! stageWindow->isVisible())
        return;
    StageState st;
    auto* info = currentInfo();
    st.hasSong = info != nullptr && currentSong != nullptr;
    if (st.hasSong)
    {
        st.song = info->name;
        st.hasAnalysis = ! info->analysis.chords.empty() || ! info->analysis.beats.empty();
        double s, e;
        sectionAt (pos, s, e, st.section);
        for (auto& m : info->markers)
            if (timeMap.toPlayback (m.seconds) > pos + 0.001) { st.nextSection = m.name; break; }
        currentChords (pos, st.chord, st.nextChord);
        barAndBeat (pos, st.bar, st.beat, st.beatsInBar, st.beatProgress);
        double orig = 0.0, play = 0.0;
        tempoAt (timeMap.toOriginal (pos), orig, play);
        st.bpm = play;
        st.key = music::spellKey (effectiveKey(), useFlats());
        st.position = pos;
        st.length = engine.getLengthSeconds();
        st.playing = engine.isPlaying();
        activeNote (pos, st.note, st.nextNote, st.nextNoteIn);
        const int next = currentIndex + 1;
        if (juce::isPositiveAndBelow (next, (int) library.songs.size()))
            st.nextSong = library.songs[(size_t) next].name;
    }
    stageWindow->view().setState (st);
}

void MainComponent::showStage (bool show)
{
    if (stageWindow == nullptr)
    {
        stageWindow = std::make_unique<StageWindow> ([this]
        {
            stageBtn.setToggleState (false, juce::dontSendNotification);
            props.getUserSettings()->setValue ("stageWindow", stageWindow->getWindowStateAsString());
        });
        stageWindow->view().onKey = [this] (const juce::KeyPress& key) { return keyPressed (key); };
        const auto saved = props.getUserSettings()->getValue ("stageWindow");
        if (saved.isNotEmpty())
            stageWindow->restoreWindowStateFromString (saved);
    }
    stageBtn.setToggleState (show, juce::dontSendNotification);
    if (! show)
    {
        if (stageWindow->isVisible())
            props.getUserSettings()->setValue ("stageWindow", stageWindow->getWindowStateAsString());
        stageWindow->setVisible (false);
        return;
    }
    stageWindow->setVisible (true);
    stageWindow->toFront (false);
    updateStage (engine.getPositionSeconds());
}

juce::Image MainComponent::stageSnapshot()
{
    if (stageWindow == nullptr)
        return {};
    auto& v = stageWindow->view();
    return v.createComponentSnapshot (v.getLocalBounds());
}

juce::String MainComponent::effectiveKey() const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return {};
    const auto& info = library.songs[(size_t) currentIndex];
    return info.keyOverride.isNotEmpty() ? info.keyOverride : info.analysis.key;
}

bool MainComponent::useFlats() const
{
    if (! juce::isPositiveAndBelow (currentIndex, (int) library.songs.size()))
        return false;
    const auto& info = library.songs[(size_t) currentIndex];
    if (info.spelling == 1) return false;
    if (info.spelling == 2) return true;
    return music::keyPrefersFlats (effectiveKey());
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &keyLabel && e.mouseWasClicked() && keyLabel.getText().isNotEmpty())
        keyMenu();
}

void MainComponent::keyMenu()
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    const bool flats = useFlats();
    const auto detected = info->analysis.key;
    const int currentPc = music::pitchClass (effectiveKey().upToFirstOccurrenceOf (" ", false, false));
    const bool currentMinor = music::isMinorKey (effectiveKey());

    juce::PopupMenu m;
    m.addSectionHeader (tr ("Tonalidad: ") + music::spellKey (effectiveKey(), flats));
    if (info->keyOverride.isNotEmpty() && detected.isNotEmpty())
        m.addItem (4, tr ("Usar la detectada (") + music::spellKey (detected, music::keyPrefersFlats (detected)) + ")");
    m.addSeparator();
    m.addItem (1, tr ("Escribir con sostenidos (Re#, Sol#, La#)"), true, info->spelling == 1);
    m.addItem (2, tr ("Escribir con bemoles (Mib, Lab, Sib)"), true, info->spelling == 2);
    m.addItem (3, tr ("Según la tonalidad (") + (music::keyPrefersFlats (effectiveKey()) ? tr ("bemoles") : tr ("sostenidos")) + ")", true, info->spelling == 0);
    m.addSeparator();
    juce::PopupMenu majors, minors;
    for (int pc = 0; pc < 12; ++pc)
    {
        majors.addItem (100 + pc, music::noteName (pc, music::keyPrefersFlats (music::canonicalKey (pc, false)), true) + " mayor", true, ! currentMinor && pc == currentPc);
        minors.addItem (200 + pc, music::noteName (pc, music::keyPrefersFlats (music::canonicalKey (pc, true)), true) + " menor", true, currentMinor && pc == currentPc);
    }
    m.addSubMenu (tr ("Cambiar la tonalidad a mayor"), majors);
    m.addSubMenu (tr ("Cambiar la tonalidad a menor"), minors);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyLabel), [this] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0)
            return;
        if (result >= 1 && result <= 3)
            inf->spelling = result % 3;   // 1 sostenidos, 2 bemoles, 3 -> 0 automático
        else if (result == 4)
            inf->keyOverride.clear();
        else if (result >= 100 && result < 112)
            inf->keyOverride = music::canonicalKey (result - 100, false);
        else if (result >= 200 && result < 212)
            inf->keyOverride = music::canonicalKey (result - 200, true);
        else
            return;
        library.saveSong (*inf);
        timeline.setAnalysis (mappedAnalysis());
        showChordAndKey (engine.getPositionSeconds());
    });
}

void MainComponent::beatMenu (int index)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (index, (int) info->analysis.beats.size()))
        return;
    auto& a = info->analysis;
    // Compás en curso: tiempos entre el primer tiempo anterior (o este) y el siguiente
    int prevDown = index, nextDown = -1, barNumber = 0;
    while (prevDown > 0 && a.beats[(size_t) prevDown].beatInBar != 1) --prevDown;
    for (int j = prevDown + 1; j < (int) a.beats.size(); ++j)
        if (a.beats[(size_t) j].beatInBar == 1) { nextDown = j; break; }
    for (int j = 0; j <= index; ++j)
        if (a.beats[(size_t) j].beatInBar == 1) ++barNumber;
    const int meterHere = nextDown > prevDown ? nextDown - prevDown : juce::jlimit (2, 7, a.meter);

    juce::PopupMenu m;
    m.addSectionHeader (tr ("Tiempo ") + juce::String (a.beats[(size_t) index].beatInBar) + tr (" del compás ") + juce::String (juce::jmax (1, barNumber))
                        + " (" + formatTime (timeMap.toPlayback (a.beats[(size_t) index].seconds)) + ")");
    m.addItem (1, tr ("Primer tiempo de compás desde aquí (compases de ") + juce::String (meterHere) + ")");
    juce::PopupMenu meters;
    for (int n = 2; n <= 7; ++n)
        meters.addItem (10 + n, tr ("Compases de ") + juce::String (n) + tr (" tiempos"), true, n == meterHere);
    m.addSubMenu (tr ("Compás de N tiempos desde aquí"), meters);
    juce::PopupMenu shortBar;
    for (int n = 1; n <= 3; ++n)
        shortBar.addItem (60 + n, tr ("Compás de ") + juce::String (n) + (n == 1 ? tr (" tiempo") : tr (" tiempos")) + tr (" solo aquí, y después compases de ") + juce::String (meterHere));
    m.addSubMenu (tr ("Compás corto solo aquí (el click acentúa este tiempo)"), shortBar);
    m.addItem (20, tr ("Renumerar hasta el final de la canción (si no, hasta el fin de la sección de tempo)"), true, renumberToEnd);
    m.addSeparator();
    m.addItem (30, tr ("Quitar este tiempo"));
    m.addItem (31, tr ("Insertar un tiempo entre este y el siguiente"), index + 1 < (int) a.beats.size());

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1)),
                     [this, index, meterHere] (int result)
    {
        auto* inf = currentInfo();
        if (inf == nullptr || result == 0 || ! juce::isPositiveAndBelow (index, (int) inf->analysis.beats.size()))
            return;
        if (result != 20)
            pushUndo();
        auto& an = inf->analysis;
        if (result == 20)
        {
            renumberToEnd = ! renumberToEnd;
            beatMenu (index);
            return;
        }
        if (result == 30)
        {
            an.beats.erase (an.beats.begin() + index);
        }
        else if (result == 31)
        {
            Beat b;
            b.seconds = 0.5 * (an.beats[(size_t) index].seconds + an.beats[(size_t) index + 1].seconds);
            b.beatInBar = an.beats[(size_t) index].beatInBar % juce::jmax (1, an.meter) + 1;
            an.beats.insert (an.beats.begin() + index + 1, b);
        }
        else if (result == 1 || (result >= 12 && result <= 17) || (result >= 61 && result <= 63))
        {
            const int meter = result == 1 || result >= 61 ? meterHere : result - 10;
            // Hasta el final de la sección de tempo que contiene este tiempo (o de la canción)
            int to = (int) an.beats.size();
            if (! renumberToEnd)
            {
                const double t = an.beats[(size_t) index].seconds;
                for (auto& r : inf->tempoRegions)
                    if (r.start > t + 1.0e-6)
                    {
                        for (int j = index; j < (int) an.beats.size(); ++j)
                            if (an.beats[(size_t) j].seconds >= r.start - 1.0e-6) { to = j; break; }
                        break;
                    }
            }
            if (result >= 61)
                an.shortBar (index, result - 60, meter, to);
            else
                an.renumberBeats (index, meter, to);
        }
        else
            return;
        analysisEdited();
    });
}

void MainComponent::analysisEdited()
{
    auto* info = currentInfo();
    if (info == nullptr)
        return;
    library.saveSong (*info);
    timeline.setAnalysis (mappedAnalysis());
    updateBeatGrid();
    showChordAndKey (engine.getPositionSeconds());
}

void MainComponent::updateBeatGrid()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || info->analysis.beats.empty())
    {
        engine.setBeatGrid (nullptr);
        return;
    }
    auto grid = std::make_shared<BeatGrid>();
    const double sr = engine.getSampleRate();
    for (auto& b : info->analysis.beats)
    {
        grid->positions.push_back ((juce::int64) std::llround (timeMap.toPlayback (b.seconds) * sr));
        grid->beatInBar.push_back (b.beatInBar);
    }
    engine.setBeatGrid (std::move (grid));
}

void MainComponent::showChordAndKey (double pos)
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr || info->analysis.isEmpty())
    {
        chordLabel.setText ({}, juce::dontSendNotification);
        keyLabel.setText ({}, juce::dontSendNotification);
        return;
    }
    const bool flats = useFlats();
    juce::String now, next;
    currentChords (pos, now, next);
    chordLabel.setText (now.isEmpty() ? juce::String ("-") : now + (next.isNotEmpty() ? "   >  " + next : juce::String()), juce::dontSendNotification);
    juce::String key = music::spellKey (effectiveKey(), flats);
    double orig = 0.0, play = 0.0;
    tempoAt (timeMap.toOriginal (pos), orig, play);
    if (orig > 0.0)
        key += (key.isEmpty() ? juce::String() : tr ("  ·  "))
               + (std::abs (play - orig) >= 0.05 ? juce::String::formatted ("%.0f -> %.0f BPM", orig, play)
                                                  : juce::String::formatted ("%.0f BPM", orig));
    keyLabel.setText (key, juce::dontSendNotification);
}

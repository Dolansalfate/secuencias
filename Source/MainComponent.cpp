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
    audioBtn.setButtonText ("Audio");
    audioBtn.onClick = [this] { showAudioSettings(); };
    aiBtn.setButtonText ("Ajustes IA");
    aiBtn.onClick = [this] { showAiSettings(); };
    cancelSepBtn.setButtonText ("Cancelar");
    cancelSepBtn.onClick = [this] { separator.cancel(); analyzer.cancel(); };
    sepLabel.setFont (font (13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &importBtn, &separateBtn, &stemsBox, &qualityBox, &audioBtn, &aiBtn,
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
    cutModeBox.addItem ("Corte libre", 1);
    cutModeBox.addItem ("Corte a la rejilla", 2);
    cutModeBox.addItem ("Corte a la transiente", 3);
    cutModeBox.setSelectedId (juce::jlimit (1, 3, props.getUserSettings()->getIntValue ("cutMode", 1)), juce::dontSendNotification);
    cutModeBox.onChange = [this] { props.getUserSettings()->setValue ("cutMode", cutModeBox.getSelectedId()); };

    for (auto* c : std::initializer_list<juce::Component*> { &songTitle, &nextLabel, &sectionLabel, &timeLabel, &chordLabel, &keyLabel,
                     &prevBtn, &playBtn, &stopBtn, &nextBtn, &loopBtn, &positionSlider, &addMarkerBtn, &cutModeBox })
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
    timeline.onChordClicked = [this] (int index, double t) { chordMenu (index, t); };
    timeline.onLaneMenu = [this] (double t, int lane) { clipMenu (t, lane); };
    timeline.levelGainAt = [this] (int stem, double t) { return levelGainAt (stem, t); };
    mixer.onLevelEdited = [this] (int stem, double db) { stemLevelEdited (stem, db); };
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
    mixer.onChanged = [this] { markSongDirty(); };
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
    stemsBox.setBounds (top.removeFromLeft (200));
    top.removeFromLeft (8);
    qualityBox.setBounds (top.removeFromLeft (190));
    aiBtn.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    audioBtn.setBounds (top.removeFromRight (90));

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
    else if (code == juce::KeyPress::F12Key)
        showStage (stageWindow == nullptr || ! stageWindow->isVisible());
    else
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (c == 'm')      addMarkerHere();
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
        juce::Component::SafePointer<MainComponent> safe (this);

        loaderPool.addJob ([this, safe, info, sr, gen]
        {
            std::shared_ptr<LoadedSong> song, source, arranged;
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
                        if (! map.isIdentity() || info.transpose != 0)
                        {
                            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sectionLabel.setText (tr ("Renderizando tempo y tono..."), juce::dontSendNotification); });
                            song = stretcher::render (arranged, map, info.transpose, sr, abort);
                        }
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
            juce::MessageManager::callAsync ([safe, song, source, arranged, gen, error]
            {
                if (auto* self = safe.getComponent())
                    self->songLoaded (song, source, arranged, gen, error);
            });
        });
    });
}

void MainComponent::songLoaded (std::shared_ptr<LoadedSong> song, std::shared_ptr<LoadedSong> source, std::shared_ptr<LoadedSong> arranged,
                                int generation, const juce::String& error)
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
    }
    else
    {
        timeline.setMarkers ({}, 120.0, 0.0);
        timeline.setTempoBands ({});
        timeline.setClips ({});
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
        if (std::abs (x.origStart - y.origStart) > 1.0e-6 || std::abs (x.origEnd - y.origEnd) > 1.0e-6 || std::abs (x.ratio - y.ratio) > 1.0e-6)
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
    if (! clipsChanged && sameStretch (newMap, timeMap) && info->transpose == renderedTranspose)
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
    auto arrangedBefore = clipsChanged ? nullptr : arrangedSong;   // si el arreglo no cambió, se reutiliza
    sepLabel.setText (clipsChanged ? tr ("Renderizando el arreglo...") : tr ("Renderizando tempo y tono..."), juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safe (this);
    loaderPool.addJob ([this, safe, source, arrangedBefore, clips, newMap, transpose, sr, gen, rgen]
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
        std::shared_ptr<LoadedSong> arranged = arrangedBefore, rendered;
        try
        {
            if (arranged == nullptr)
                arranged = arrangement::render (source, clips, sr, abort);
            if (arranged != nullptr)
                rendered = stretcher::render (arranged, newMap, transpose, sr, abort, progress);
        }
        catch (const std::exception&) {}
        juce::MessageManager::callAsync ([safe, rendered, arranged, newMap, transpose, clips, rgen]
        {
            if (auto* self = safe.getComponent())
                self->songRendered (rendered, arranged, newMap, transpose, clips, rgen);
        });
    });
}

void MainComponent::songRendered (std::shared_ptr<LoadedSong> rendered, std::shared_ptr<LoadedSong> arranged, TimeMap map,
                                  int transpose, std::vector<Clip> clips, int rgen)
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
    arrangedSong = arranged;
    currentSong = rendered;
    engine.pause();
    afterFadeOut ([this, wasPlaying, songPos]
    {
        engine.setSong (currentSong);
        mixer.setSong (currentSong);
        timeline.setSong (currentSong, engine.getSampleRate());
        syncTimelineMarkers();
        timeline.setAnalysis (mappedAnalysis());
        updateBeatGrid();
        updateGainCurve();
        engine.seekSeconds (timeMap.toPlayback (songPos));
        if (wasPlaying)
            engine.play();
        tempoControlsFromSong();
        updateLoopRegion();
        const bool plain = timeMap.isIdentity() && renderedTranspose == 0;
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
        if (result != 1 && result != 6)
            pushUndo();   // los diálogos (1 y 6) lo hacen al aceptar

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
        if (result == 6)
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
        if (result == 2)
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
    requestRender();
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
    m.addItem (13, tr ("Pegar insertando en ") + pegarEn + (clipboard.valid ? juce::String::formatted (" (%.1f s)", clipboard.clip.length()) : juce::String()), clipboard.valid);
    m.addItem (14, tr ("Pegar encima en ") + pegarEn, clipboard.valid);
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
                arrangement::moveClip (s->clips, idx, ms / 1000.0, result == 3);
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
                arrangement::moveClip (cl, idx, inf->analysis.beats[(size_t) nb].seconds - cl[(size_t) idx].position, false);
        }
        else if (result == 5)
            arrangement::joinWithPrevious (cl, idx);
        else if (result == 6 || result == 7)
        {
            double from = 0.0, length = 0.0;
            arrangement::removeClip (cl, idx, result == 7, from, length);
            if (result == 7 && length > 0.0)
            {
                arrangement::shiftGrid (*inf, from, -length);
                gridChanged = true;
            }
        }
        else if (result == 9)
            inf->clips.clear();
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
    clipboard.clip = c;
    clipboard.grid = arrangement::copyGrid (info->analysis, c.position, c.end());   // la grilla que se ve sobre ese tramo
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
    if (info == nullptr || sourceSong == nullptr || ! clipboard.valid)
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
    arrangement::shiftGrid (*info, at, len);                 // abre el hueco en tiempos, acordes, marcadores y secciones
    arrangement::pasteGrid (info->analysis, clipboard.grid, at);
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
    arrangement::moveClip (info->clips, idx, d0, false);
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
        arrangement::moveClip (info->clips, idx, moveMs / 1000.0, false);
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

    if (engine.needsReload.exchange (false) && currentIndex >= 0)
        loadSongAt (currentIndex);   // guarda la mezcla y recarga a la nueva frecuencia

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
        const bool ok = target >= 0 && library.replaceStems (target, separator.getResultFolder());
        const auto name = target >= 0 ? library.songs[(size_t) target].name : juce::String();
        separator.reset();
        separationTarget = juce::File();
        if (! ok)
            sepLabel.setText (tr ("No se pudieron reemplazar las pistas"), juce::dontSendNotification);
        else
        {
            sepLabel.setText (tr ("«") + name + tr ("» separada: ") + juce::String ((int) library.songs[(size_t) target].stems.size()) + tr (" pistas"), juce::dontSendNotification);
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
        refreshSetlist();
        if (idx < 0)
            sepLabel.setText (tr ("No se pudieron importar los stems"), juce::dontSendNotification);
        else if (engine.isPlaying())
            sepLabel.setText (tr ("«") + name + tr ("» separada y agregada al final del setlist"), juce::dontSendNotification);
        else
        {
            // Solo se cambia a la canción nueva si no hay nada sonando
            sepLabel.setText (tr ("«") + name + tr ("» separada y agregada al setlist"), juce::dontSendNotification);
            setlist.selectRow (idx);
        }
    }
    else if (st == Separator::State::cancelled)
    {
        separator.reset();
        separationTarget = juce::File();
        sepLabel.setText (tr ("Separación cancelada."), juce::dontSendNotification);
    }
    else if (st == Separator::State::failed)
    {
        const auto msg = separator.getMessage();
        separator.reset();
        separationTarget = juce::File();
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
    w->addButton (tr ("Elegir un archivo..."), 2);
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, pickFile] (int result)
    {
        if (result == 1)
            separateCurrentSong();
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
    startSeparation (input);
    if (separator.getState() != Separator::State::running)
        separationTarget = juce::File();
}

void MainComponent::startSeparation (const juce::File& file)
{
    if (separator.getState() == Separator::State::running)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Separar",
                                                tr ("Ya hay una separación en curso. Espera a que termine."));
        return;
    }

    const auto py = pythonPath();
    if (! juce::File::isAbsolutePath (py) || ! juce::File (py).existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Falta Demucs"),
            tr ("No encuentro Python con Demucs en:\n") + py
            + tr ("\n\nInstálalo desde «Ajustes IA» > «Instalar motores de IA» (o cambia ahí la ruta si ya lo tienes en otro lugar)."));
        return;
    }

    separator.start (file, py, selectedOptions(), file.getFileNameWithoutExtension());
}

void MainComponent::filesDropped (const juce::StringArray& paths, int, int)
{
    juce::Array<juce::File> files;
    for (auto& p : paths)
        files.add (juce::File (p));

    if (files.size() == 1 && Library::isAudioFile (files[0]))
        startSeparation (files[0]);       // una canción completa → separar
    else
        importStems (files);              // varios archivos o carpeta → stems
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 2, 64, false, false, true, false);
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
    auto* w = new juce::AlertWindow (tr ("Ajustes IA"), estado, juce::MessageBoxIconType::NoIcon, this);
    w->addButton (tr ("Instalar motores de IA"), 1);
    w->addButton (tr ("Rutas de Python..."), 2);
    w->addButton ("Cerrar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1)
        {
            launchInstaller();
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

void MainComponent::launchInstaller()
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
   #if JUCE_MAC
    const auto cmd = "bash " + script.getFullPathName().quoted();
    launched = proc.start (juce::StringArray { "osascript", "-e", "tell application \"Terminal\" to activate",
                                               "-e", "tell application \"Terminal\" to do script " + cmd.quoted() });
   #elif JUCE_WINDOWS
    launched = proc.start ("powershell -NoExit -ExecutionPolicy Bypass -File " + script.getFullPathName().quoted());
   #else
    for (auto& term : { juce::StringArray { "x-terminal-emulator", "-e", "bash", script.getFullPathName() },
                        juce::StringArray { "gnome-terminal", "--", "bash", script.getFullPathName() },
                        juce::StringArray { "konsole", "-e", "bash", script.getFullPathName() },
                        juce::StringArray { "xterm", "-e", "bash", script.getFullPathName() } })
        if (proc.start (term))
        {
            launched = true;
            break;
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

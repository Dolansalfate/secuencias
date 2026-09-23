#include "MainComponent.h"

namespace
{
    const juce::Colour accent   { 0xff4fc3f7 };
    const juce::Colour panel    { 0xff1e2329 };
    const juce::Colour panelAlt { 0xff262c33 };

    juce::String formatTime (double s)
    {
        s = juce::jmax (0.0, s);
        const int m = (int) (s / 60.0);
        const double sec = s - m * 60.0;
        return juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4);
    }

    juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    const char* modelIds[] = { "htdemucs", "htdemucs_6s", "htdemucs_ft" };

   #if JUCE_LINUX
    // En Linux el diálogo nativo (zenity/kdialog) no permite elegir archivos y carpetas
    // a la vez, así que usamos el selector propio de JUCE.
    constexpr bool useNativeChooserForFolders = false;
   #else
    constexpr bool useNativeChooserForFolders = true;
   #endif
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
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (std::shared_ptr<LoadedSong> s, LoadedTrack& t) : song (std::move (s)), track (t)
    {
        nameLabel.setText (track.name, juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (font (14.0f, true));
        addAndMakeVisible (nameLabel);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setTextValueSuffix (" dB");
        fader.setDoubleClickReturnValue (true, 0.0);
        const float g = track.gain.load();
        fader.setValue (g <= 0.0f ? -60.0 : juce::Decibels::gainToDecibels (g, -60.0f), juce::dontSendNotification);
        fader.onValueChange = [this]
        {
            const double db = fader.getValue();
            track.gain = db <= -59.9 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
        };
        addAndMakeVisible (fader);

        muteBtn.setButtonText ("M");
        muteBtn.setClickingTogglesState (true);
        muteBtn.setToggleState (track.muted.load(), juce::dontSendNotification);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
        muteBtn.onClick = [this] { track.muted = muteBtn.getToggleState(); };
        addAndMakeVisible (muteBtn);

        soloBtn.setButtonText ("S");
        soloBtn.setClickingTogglesState (true);
        soloBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold.darker (0.2f));
        soloBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        soloBtn.onClick = [this] { track.solo = soloBtn.getToggleState(); };
        addAndMakeVisible (soloBtn);

        outBox.onChange = [this] { track.outputPair = juce::jmax (0, outBox.getSelectedId() - 1); };
        addAndMakeVisible (outBox);
    }

    juce::ComboBox& outputBox() { return outBox; }
    LoadedTrack& getTrack()     { return track; }

    void tick()
    {
        const float peak = track.meter.exchange (0.0f);
        const float newLevel = juce::jmax (peak, level * 0.85f);
        if (std::abs (newLevel - level) > 0.001f)
        {
            level = newLevel;
            repaint (meterArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelAlt);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (meterArea);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto bar = meterArea.toFloat();
        bar = bar.removeFromBottom (bar.getHeight() * frac);
        g.setColour (db > -3.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::yellow : juce::Colours::limegreen);
        g.fillRect (bar);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        nameLabel.setBounds (r.removeFromTop (22));
        outBox.setBounds (r.removeFromBottom (24));
        r.removeFromBottom (4);
        auto buttons = r.removeFromBottom (26);
        muteBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        soloBtn.setBounds (buttons.reduced (2, 0));
        r.removeFromBottom (4);
        meterArea = r.removeFromRight (8).withTrimmedBottom (22);
        r.removeFromRight (2);
        fader.setBounds (r);
    }

private:
    std::shared_ptr<LoadedSong> song;   // mantiene viva la pista
    LoadedTrack& track;
    juce::Label nameLabel;
    juce::Slider fader;
    juce::TextButton muteBtn, soloBtn;
    juce::ComboBox outBox;
    juce::Rectangle<int> meterArea;
    float level = 0.0f;
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
    deviceManager.initialise (0, 2, savedAudio.get(), true);
    deviceManager.addAudioCallback (&engine);
    deviceManager.addChangeListener (this);

    library.load();

    // --- Barra superior ---
    importBtn.setButtonText ("Importar stems");
    importBtn.onClick = [this] { chooseStems(); };
    separateBtn.setButtonText (tr ("Separar canción (IA)"));
    separateBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    separateBtn.onClick = [this] { chooseSongToSeparate(); };
    modelBox.addItem ("4 pistas (htdemucs)", 1);
    modelBox.addItem ("6 pistas: + guitarra y piano (htdemucs_6s)", 2);
    modelBox.addItem ("4 pistas, mejor calidad, lento (htdemucs_ft)", 3);
    modelBox.setSelectedId (props.getUserSettings()->getIntValue ("model", 1), juce::dontSendNotification);
    modelBox.onChange = [this] { props.getUserSettings()->setValue ("model", modelBox.getSelectedId()); };
    audioBtn.setButtonText ("Audio");
    audioBtn.onClick = [this] { showAudioSettings(); };
    aiBtn.setButtonText ("Ajustes IA");
    aiBtn.onClick = [this] { showAiSettings(); };
    cancelSepBtn.setButtonText ("Cancelar");
    cancelSepBtn.onClick = [this] { separator.cancel(); };
    sepLabel.setFont (font (13.0f));

    for (auto* c : std::initializer_list<juce::Component*> { &importBtn, &separateBtn, &modelBox, &audioBtn, &aiBtn,
                     &sepBar, &sepLabel, &cancelSepBtn })
        addAndMakeVisible (c);
    sepBar.setVisible (false);
    cancelSepBtn.setVisible (false);

    // --- Setlist ---
    setlistTitle.setText ("SETLIST", juce::dontSendNotification);
    setlistTitle.setFont (font (13.0f, true));
    setlistTitle.setColour (juce::Label::textColourId, juce::Colours::grey);
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

    for (auto* c : std::initializer_list<juce::Component*> { &songTitle, &nextLabel, &sectionLabel, &timeLabel, &prevBtn, &playBtn,
                     &stopBtn, &nextBtn, &loopBtn, &positionSlider, &addMarkerBtn })
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
    bpmSlider.onValueChange = [this] { if (auto* s = currentInfo()) { s->bpm = bpmSlider.getValue(); pushClick(); } };

    offsetSlider.setSliderStyle (juce::Slider::IncDecButtons);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    offsetSlider.setRange (0.0, 30.0, 0.01);
    offsetSlider.onValueChange = [this] { if (auto* s = currentInfo()) { s->clickOffset = offsetSlider.getValue(); pushClick(); } };

    clickGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    clickGainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    clickGainSlider.setRange (-40.0, 6.0, 0.5);
    clickGainSlider.onValueChange = [this] { if (auto* s = currentInfo()) { s->clickGainDb = (float) clickGainSlider.getValue(); pushClick(); } };

    clickOutBox.onChange = [this] { if (auto* s = currentInfo()) { s->clickOutputPair = juce::jmax (0, clickOutBox.getSelectedId() - 1); pushClick(); } };

    for (auto* c : std::initializer_list<juce::Component*> { &clickBtn, &bpmLabel, &offsetLabel, &clickVolLabel, &bpmSlider,
                     &offsetSlider, &clickGainSlider, &clickOutBox })
        addAndMakeVisible (c);

    // --- Mezclador ---
    mixerView.setViewedComponent (&stripHolder, false);
    mixerView.setScrollBarsShown (false, true);
    addAndMakeVisible (mixerView);

    helpLabel.setFont (font (12.0f));
    helpLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    helpLabel.setText (tr ("Espacio: play/pausa · Esc: stop · Flechas izq/der o RePág/AvPág: canción · 1-9: ir a marcador · "
                           "M: añadir marcador · L: loop · Arrastra archivos aquí (1 archivo = separar, varios = stems)"),
                       juce::dontSendNotification);
    addAndMakeVisible (helpLabel);

    disableFocus (*this);
    setWantsKeyboardFocus (true);

    setSize (1200, 760);
    refreshSetlist();
    clickControlsFromSong();
    startTimerHz (30);

    if (! library.songs.empty())
        setlist.selectRow (0);
}

MainComponent::~MainComponent()
{
    ++loadGeneration;
    loaderPool.removeAllJobs (true, 10000);
    stopTimer();
    saveCurrentMix();
    separator.cancel();
    deviceManager.removeAudioCallback (&engine);
    deviceManager.removeChangeListener (this);
    if (auto xml = deviceManager.createStateXml())
        props.getUserSettings()->setValue ("audioDevice", xml.get());
    props.saveIfNeeded();
    setlist.setModel (nullptr);
    strips.clear();
    markerButtons.clear();
}

void MainComponent::disableFocus (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        child->setWantsKeyboardFocus (false);
        child->setMouseClickGrabsKeyboardFocus (false);
        disableFocus (*child);
    }
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
                          mixerView.getBounds(), juce::Justification::centred, 5);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (34);
    importBtn.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    separateBtn.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (8);
    modelBox.setBounds (top.removeFromLeft (310));
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
    setlistTitle.setBounds (left.removeFromTop (22));
    setlist.setBounds (left);
    r.removeFromLeft (12);

    auto titleRow = r.removeFromTop (40);
    timeLabel.setBounds (titleRow.removeFromRight (220));
    songTitle.setBounds (titleRow);
    auto infoRow = r.removeFromTop (30);
    nextLabel.setBounds (infoRow.removeFromRight (360));
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

    r.removeFromTop (8);
    positionSlider.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);

    auto markerRow = r.removeFromTop (30);
    addMarkerBtn.setBounds (markerRow.removeFromLeft (110));
    markerRow.removeFromLeft (8);
    for (auto* b : markerButtons)
    {
        const int w = juce::jmin (140, juce::jmax (60, b->getBestWidthForHeight (30) + 10));
        b->setBounds (markerRow.removeFromLeft (w));
        markerRow.removeFromLeft (4);
    }

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

    r.removeFromTop (10);
    mixerView.setBounds (r);

    const int stripW = 112, gap = 6;
    const int h = juce::jmax (200, mixerView.getHeight() - mixerView.getScrollBarThickness() - 2);
    stripHolder.setSize (juce::jmax (1, strips.size() * (stripW + gap)), h);
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * (stripW + gap), 0, stripW, h);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey)                                     { engine.togglePlay(); return true; }
    if (code == juce::KeyPress::escapeKey)                                    { engine.stop(); updateLoopRegion(); return true; }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey) { nextSong (1); return true; }
    if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::pageUpKey)   { nextSong (-1); return true; }
    if (code >= '1' && code <= '9')                                           { jumpToMarker (code - '1'); return true; }

    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 'm') { addMarkerHere(); return true; }
    if (c == 'l') { loopBtn.setToggleState (! loopBtn.getToggleState(), juce::dontSendNotification); updateLoopRegion(); return true; }

    return false;
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

void MainComponent::songMenu (int row)
{
    juce::PopupMenu m;
    m.addItem (1, "Subir", row > 0);
    m.addItem (2, "Bajar", row < (int) library.songs.size() - 1);
    m.addItem (3, "Renombrar...");
    m.addSeparator();
    m.addItem (4, "Mover a la papelera...");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, row] (int result)
    {
        if (result == 1 || result == 2)
        {
            const int delta = result == 1 ? -1 : 1;
            library.move (row, delta);
            if (currentIndex == row)              currentIndex = row + delta;
            else if (currentIndex == row + delta) currentIndex = row;
            setlist.selectRow (currentIndex, true, true);
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
                    if (row == currentIndex)
                        unloadSong();
                    if (! library.moveToTrash (row))
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Eliminar",
                            tr ("No se pudo mover la carpeta a la papelera:\n")
                            + library.songs[(size_t) row].folder.getFullPathName());
                        refreshSetlist();
                        return;
                    }
                    if (currentIndex > row)
                        --currentIndex;
                    setlist.deselectAllRows();
                    if (currentIndex >= 0)
                        setlist.selectRow (currentIndex, true, true);
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

void MainComponent::unloadSong()
{
    saveCurrentMix();
    ++loadGeneration;
    engine.setSong (nullptr);
    currentSong.reset();
    strips.clear();
    currentIndex = -1;
    songTitle.setText ({}, juce::dontSendNotification);
    sectionLabel.setText ({}, juce::dontSendNotification);
    rebuildMarkers();
    resized();
}

void MainComponent::loadSongAt (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) library.songs.size()))
        return;

    saveCurrentMix();
    engine.setSong (nullptr);
    currentSong.reset();
    strips.clear();

    currentIndex = index;
    const auto info = library.songs[(size_t) index];
    songTitle.setText (info.name, juce::dontSendNotification);
    sectionLabel.setText ("Cargando...", juce::dontSendNotification);
    rebuildMarkers();
    clickControlsFromSong();
    refreshSetlist();
    resized();

    const int gen = ++loadGeneration;
    const double sr = engine.getSampleRate();
    juce::Component::SafePointer<MainComponent> safe (this);

    loaderPool.addJob ([this, safe, info, sr, gen]
    {
        auto song = AudioEngine::loadSong (info, sr, formatManager,
                                           [this, gen] { return gen != loadGeneration.load(); });
        juce::MessageManager::callAsync ([safe, song, gen]
        {
            if (auto* self = safe.getComponent())
                self->songLoaded (song, gen);
        });
    });
}

void MainComponent::songLoaded (std::shared_ptr<LoadedSong> song, int generation)
{
    if (generation != loadGeneration.load())
        return;

    if (song == nullptr || song->tracks.empty())
    {
        sectionLabel.setText (tr ("No se pudo cargar el audio"), juce::dontSendNotification);
        return;
    }

    currentSong = song;
    engine.setSong (song);
    pushClick();
    rebuildStrips();
    updateLoopRegion();
}

void MainComponent::saveCurrentMix()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;

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
    library.saveSong (*info);
}

void MainComponent::nextSong (int delta)
{
    const int idx = juce::jlimit (0, (int) library.songs.size() - 1, currentIndex + delta);
    if (idx >= 0 && idx != currentIndex)
        setlist.selectRow (idx);
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

void MainComponent::rebuildStrips()
{
    strips.clear();
    if (currentSong != nullptr)
    {
        for (auto& t : currentSong->tracks)
        {
            auto* strip = strips.add (new ChannelStrip (currentSong, *t));
            fillOutputBox (strip->outputBox(), t->outputPair.load());
            stripHolder.addAndMakeVisible (strip);
            disableFocus (*strip);
            strip->setWantsKeyboardFocus (false);
        }
    }
    resized();
}

void MainComponent::rebuildMarkers()
{
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
        if (m.seconds <= seconds + 0.001)
        {
            start = m.seconds;
            name = m.name;
        }
        else
        {
            end = m.seconds;
            break;
        }
    }
}

void MainComponent::updateLoopRegion()
{
    if (! loopBtn.getToggleState() || currentSong == nullptr)
    {
        engine.clearLoop();
        return;
    }
    double s, e;
    juce::String name;
    sectionAt (engine.getPositionSeconds(), s, e, name);
    if (e - s > 0.05)
        engine.setLoop (s, e);
    else
        engine.clearLoop();
}

void MainComponent::jumpToMarker (int index)
{
    auto* info = currentInfo();
    if (info == nullptr || ! juce::isPositiveAndBelow (index, (int) info->markers.size()))
        return;
    engine.seekSeconds (info->markers[(size_t) index].seconds);
    updateLoopRegion();
}

void MainComponent::addMarkerHere()
{
    auto* info = currentInfo();
    if (info == nullptr || currentSong == nullptr)
        return;
    info->markers.push_back ({ tr ("Sección ") + juce::String ((int) info->markers.size() + 1),
                               engine.getPositionSeconds() });
    info->sortMarkers();
    library.saveSong (*info);
    rebuildMarkers();
}

void MainComponent::markerMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, "Renombrar...");
    m.addItem (2, tr ("Mover a la posición actual"));
    m.addSeparator();
    m.addItem (3, "Eliminar");

    auto* target = juce::isPositiveAndBelow (index, markerButtons.size()) ? markerButtons[index] : nullptr;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, index] (int result)
    {
        auto* info = currentInfo();
        if (info == nullptr || ! juce::isPositiveAndBelow (index, (int) info->markers.size()))
            return;

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
            info->markers[(size_t) index].seconds = engine.getPositionSeconds();
        else if (result == 3)
            info->markers.erase (info->markers.begin() + index);
        else
            return;

        info->sortMarkers();
        library.saveSong (*info);
        rebuildMarkers();
        updateLoopRegion();
    });
}

void MainComponent::clickControlsFromSong()
{
    const auto* info = currentInfo();
    const bool has = info != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &clickBtn, &bpmSlider, &offsetSlider, &clickGainSlider, &clickOutBox, &addMarkerBtn })
        c->setEnabled (has);
    if (! has)
        return;

    clickBtn.setToggleState (info->clickEnabled, juce::dontSendNotification);
    bpmSlider.setValue (info->bpm, juce::dontSendNotification);
    offsetSlider.setValue (info->clickOffset, juce::dontSendNotification);
    clickGainSlider.setValue (info->clickGainDb, juce::dontSendNotification);
    fillOutputBox (clickOutBox, info->clickOutputPair);
}

void MainComponent::pushClick()
{
    if (auto* s = currentInfo())
    {
        engine.setClick (s->clickEnabled, s->bpm, s->clickOffset, s->clickGainDb, s->clickOutputPair);
        library.saveSong (*s);
    }
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Mantener el foco para los atajos de teclado / pedal
    if (isShowing() && ! hasKeyboardFocus (false)
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
        && juce::Process::isForegroundProcess())
        grabKeyboardFocus();

    if (engine.needsReload.exchange (false) && currentIndex >= 0)
    {
        const int idx = currentIndex;
        currentIndex = -1;
        loadSongAt (idx);
    }

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
    }

    for (auto* strip : strips)
        strip->tick();

    // Estado del separador
    const auto st = separator.getState();
    const bool running = st == Separator::State::running;
    sepBar.setVisible (running);
    cancelSepBtn.setVisible (running);
    separateBtn.setEnabled (! running);

    if (running)
    {
        const float p = separator.getProgress();
        sepProgress = p < 0.0f ? -1.0 : (double) p;
        sepLabel.setText (separator.getSongName() + ": " + separator.getMessage(), juce::dontSendNotification);
    }
    else if (st == Separator::State::done)
    {
        const auto name = separator.getSongName();
        const int idx = library.importStemFolder (separator.getResultFolder(), name);
        separator.reset();
        refreshSetlist();
        sepLabel.setText (idx >= 0 ? tr ("«") + name + tr ("» separada y agregada al setlist")
                                   : tr ("No se pudieron importar los stems"),
                          juce::dontSendNotification);
        if (idx >= 0)
            setlist.selectRow (idx);
    }
    else if (st == Separator::State::failed)
    {
        const auto msg = separator.getMessage();
        separator.reset();
        sepLabel.setText ({}, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                tr ("No se pudo separar la canción"), msg);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Cambió el dispositivo de audio: actualizar las salidas disponibles
    for (auto* strip : strips)
        fillOutputBox (strip->outputBox(), strip->getTrack().outputPair.load());
    if (auto* info = currentInfo())
        fillOutputBox (clickOutBox, info->clickOutputPair);
}

//==============================================================================
juce::String MainComponent::pythonPath() const
{
    const auto def = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                         .getChildFile ("demucs-env/bin/python").getFullPathName();
    auto p = props.getUserSettings()->getValue ("pythonPath", def).trim();
    if (p.startsWith ("~"))
        p = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() + p.substring (1);
    return p;
}

juce::String MainComponent::selectedModel() const
{
    const int id = juce::jlimit (1, 3, modelBox.getSelectedId());
    return modelIds[id - 1];
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

void MainComponent::chooseStems()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Elige los stems de una canción (varios archivos o una carpeta)"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a",
                                                   useNativeChooserForFolders);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty())
                                  importStems (fc.getResults());
                          });
}

void MainComponent::importStems (const juce::Array<juce::File>& selection)
{
    juce::Array<juce::File> audio;
    juce::String defaultName;

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
                                                tr ("No encontré archivos de audio (wav, aiff, flac, mp3, m4a)."));
        return;
    }

    askText (tr ("Nombre de la canción"), defaultName, [this, audio] (const juce::String& name)
    {
        const int idx = library.importStemFiles (audio, name);
        refreshSetlist();
        if (idx >= 0)
            setlist.selectRow (idx);
    });
}

void MainComponent::chooseSongToSeparate()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Elige la canción completa a separar"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  startSeparation (fc.getResult());
                          });
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
    if (! juce::File (py).existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Falta Demucs"),
            tr ("No encuentro Python con Demucs en:\n") + py
            + tr ("\n\nInstálalo una vez en la Terminal:\n\n"
                 #if JUCE_LINUX
                  "sudo apt install python3-venv\n"
                  "python3 -m venv ~/demucs-env\n"
                  "~/demucs-env/bin/pip install \"torch<2.9\" \"torchaudio<2.9\" "
                  "--index-url https://download.pytorch.org/whl/cpu\n"
                  "~/demucs-env/bin/pip install demucs soundfile\n\n"
                  "(Con tarjeta NVIDIA, omite --index-url para usar la GPU.)\n\n"
                 #else
                  "python3 -m venv ~/demucs-env\n"
                  "~/demucs-env/bin/pip install demucs soundfile \"torchaudio<2.9\"\n\n"
                 #endif
                  "Si lo instalaste en otro lugar, cambia la ruta en «Ajustes IA»."));
        return;
    }

    separator.start (file, py, selectedModel(), file.getFileNameWithoutExtension());
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
    askText (tr ("Ruta de Python con Demucs instalado"), pythonPath(), [this] (const juce::String& p)
    {
        if (p.trim().isNotEmpty())
            props.getUserSettings()->setValue ("pythonPath", p.trim());
    });
}

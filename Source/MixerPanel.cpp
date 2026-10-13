#include "MixerPanel.h"
#include "UiUtils.h"
#include <cmath>

//==============================================================================
ChannelStrip::ChannelStrip (std::shared_ptr<void> keep, LoadedTrack& t, juce::Colour c)
    : keepAlive (std::move (keep)), track (t), colour (c)
{
    nameLabel.setText (track.name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (ui::font (13.0f, true));
    nameLabel.setMinimumHorizontalScale (0.7f);
    nameLabel.onClick = [this] { if (onNameClicked) onNameClicked(); };
    addAndMakeVisible (nameLabel);

    addAndMakeVisible (meter);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setColour (juce::Slider::trackColourId, colour.withAlpha (0.6f));
    fader.setColour (juce::Slider::thumbColourId, colour);
    const float g = track.gain.load();
    fader.setValue (g <= 0.0f ? -60.0 : juce::Decibels::gainToDecibels (g, -60.0f), juce::dontSendNotification);
    fader.onValueChange = [this]
    {
        const double db = fader.getValue();
        track.gain = db <= -59.9 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
        changed();
    };
    addAndMakeVisible (fader);

    // Balance: perilla (arrastre vertical u horizontal; doble clic al centro) y su valor
    panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    panKnob.setColour (juce::Slider::rotarySliderFillColourId, colour);
    panKnob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.25f));
    panKnob.setColour (juce::Slider::thumbColourId, colour);
    panKnob.setValue (track.pan.load(), juce::dontSendNotification);
    panKnob.setTooltip ("Balance (doble clic: al centro)");
    panKnob.onValueChange = [this]
    {
        track.pan = (float) panKnob.getValue();
        showPan();
        changed();
    };
    addAndMakeVisible (panKnob);
    panLabel.setFont (ui::font (11.0f));
    panLabel.setJustificationType (juce::Justification::centredRight);
    panLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    panLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (panLabel);
    showPan();

    // Nivelado por pista del tramo: se ve y se edita aquí (un clic abre el editor)
    levelBox.setJustificationType (juce::Justification::centred);
    levelBox.setFont (ui::font (11.0f));
    levelBox.setEditable (true, false, false);
    levelBox.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    levelBox.setColour (juce::Label::textColourId, juce::Colours::grey);
    levelBox.setText ("Niv --", juce::dontSendNotification);
    levelBox.onEditorShow = [this]
    {
        if (auto* ed = levelBox.getCurrentTextEditor())
        {
            ed->setText (juce::String (shownLevelDb, 1), juce::dontSendNotification);
            ed->selectAll();
        }
    };
    levelBox.onTextChange = [this]
    {
        const auto text = levelBox.getText().replace ("Niv", "").replace ("dB", "").replace (",", ".").trim();
        if (text.isEmpty() || text == "--")
        {
            setLevelGain (shownLevelDb, levelEnabled);
            return;
        }
        const double db = juce::jlimit (-40.0, 40.0, text.getDoubleValue());
        setLevelGain (db, true);
        if (onLevelEdited)
            onLevelEdited (db);
    };
    addChildComponent (levelBox);   // se ve con el nivelado (setLevelGain)

    // Trigger de la pista: si suena por sí misma, si dispara un sonido (banco o VST) o ambas; un clic abre el menú
    trigBox.setJustificationType (juce::Justification::centred);
    trigBox.setFont (ui::font (11.0f));
    trigBox.setMinimumHorizontalScale (0.6f);
    trigBox.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
    trigBox.setColour (juce::Label::textColourId, juce::Colours::grey);
    trigBox.setText ("Audio", juce::dontSendNotification);
    trigBox.onClick = [this] { if (onTriggerClicked) onTriggerClicked(); };
    addChildComponent (trigBox);    // se ve con un trigger (setTriggerLabel)

    muteBtn.setButtonText ("M");
    muteBtn.setClickingTogglesState (true);
    muteBtn.setToggleState (track.muted.load(), juce::dontSendNotification);
    muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
    muteBtn.onClick = [this] { track.muted = muteBtn.getToggleState(); changed(); };
    addAndMakeVisible (muteBtn);

    soloBtn.setButtonText ("S");
    soloBtn.setClickingTogglesState (true);
    soloBtn.setToggleState (track.solo.load(), juce::dontSendNotification);
    soloBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold.darker (0.2f));
    soloBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    soloBtn.onClick = [this] { track.solo = soloBtn.getToggleState(); };
    addAndMakeVisible (soloBtn);

    outBox.onChange = [this] { track.outputPair = juce::jmax (0, outBox.getSelectedId() - 1); changed(); };
    addAndMakeVisible (outBox);

    ui::disableFocus (*this);
    setWantsKeyboardFocus (false);
}

void ChannelStrip::showPan()
{
    const int v = (int) std::lround (panKnob.getValue() * 100.0);
    panLabel.setText (v == 0 ? juce::String() : (v < 0 ? "L" : "R") + juce::String (std::abs (v)), juce::dontSendNotification);
    panKnob.setTooltip (v == 0 ? juce::String ("Balance: centro (doble clic: al centro)")
                               : "Balance: " + juce::String (v < 0 ? "izquierda " : "derecha ") + juce::String (std::abs (v)) + " (doble clic: al centro)");
}

void ChannelStrip::setLevelGain (double db, bool enabled)
{
    shownLevelDb = db;
    if (levelEnabled != enabled)
    {
        levelEnabled = enabled;
        levelBox.setVisible (enabled);   // sin nivelado no hay nada que mostrar
        resized();
    }
    if (levelBox.isBeingEdited())
        return;
    const auto text = enabled ? juce::String::formatted ("Niv %+.1f dB", db) : juce::String ("Niv --");
    if (levelBox.getText() != text)
        levelBox.setText (text, juce::dontSendNotification);
    levelBox.setColour (juce::Label::textColourId, enabled && std::abs (db) >= 0.05 ? ui::accent : juce::Colours::grey);
}

void ChannelStrip::setTriggerLabel (const juce::String& text, int mode)
{
    // Sin trigger ("Audio") la casilla no se ve: el menú del trigger está en el botón T del carril
    const bool active = mode != 0;
    if (active != trigActive)
    {
        trigActive = active;
        trigBox.setVisible (trigBoxVisible && trigActive);
        resized();
    }
    if (trigBox.getText() != text)
        trigBox.setText (text, juce::dontSendNotification);
    trigBox.setColour (juce::Label::textColourId, mode == 0 ? juce::Colours::grey : mode == 3 ? juce::Colours::orangered : juce::Colours::orange);
    trigBox.setColour (juce::Label::backgroundColourId, mode == 0 ? juce::Colours::black.withAlpha (0.25f) : juce::Colours::orange.withAlpha (0.15f));
}

void ChannelStrip::setReadOnly (bool ro)
{
    levelBox.setEditable (! ro, false, false);
    trigBox.setInterceptsMouseClicks (! ro, false);
    nameLabel.setInterceptsMouseClicks (! ro, false);
}

void ChannelStrip::setTriggerBoxVisible (bool visible)
{
    trigBoxVisible = visible;
    trigBox.setVisible (visible && trigActive);
    resized();
}

void ChannelStrip::tick()
{
    meter.setLevels (0, track.peakL.exchange (0.0f), track.rmsL.load());
    meter.setLevels (1, track.peakR.exchange (0.0f), track.rmsR.load());
    meter.tick();
    // Mute y solo también se cambian desde la vista de arreglo
    if (muteBtn.getToggleState() != track.muted.load())
        muteBtn.setToggleState (track.muted.load(), juce::dontSendNotification);
    if (soloBtn.getToggleState() != track.solo.load())
        soloBtn.setToggleState (track.solo.load(), juce::dontSendNotification);
    if (std::abs (panKnob.getValue() - track.pan.load()) > 1.0e-4 && ! panKnob.isMouseButtonDown())
        panKnob.setValue (track.pan.load(), juce::dontSendNotification);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    g.setColour (ui::panelAlt);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (getLocalBounds().removeFromTop (4).toFloat().reduced (6.0f, 0.0f), 2.0f);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromTop (4);
    auto nameRow = r.removeFromTop (20);
    // El balance va en la fila de M y S (su valor, junto al nombre cuando no está al centro)
    panLabel.setBounds (nameRow.removeFromRight (30));
    nameLabel.setBounds (nameRow);
    outBox.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    auto buttons = r.removeFromBottom (26);
    panKnob.setBounds (buttons.removeFromRight (28));
    buttons.removeFromRight (2);
    muteBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 1));
    soloBtn.setBounds (buttons.reduced (1, 1));
    r.removeFromBottom (4);
    if (levelEnabled)
    {
        levelBox.setBounds (r.removeFromBottom (16));
        r.removeFromBottom (2);
    }
    if (trigBoxVisible && trigActive)
    {
        trigBox.setBounds (r.removeFromBottom (16));
        r.removeFromBottom (2);
    }
    meter.setBounds (r.removeFromRight (40).withTrimmedBottom (18));
    r.removeFromRight (2);
    fader.setBounds (r);
}

//==============================================================================
ClickStrip::ClickStrip()
{
    nameLabel.setText ("CLICK", juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (ui::font (13.0f, true));
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (meter);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    fader.setRange (-40.0, 6.0, 0.5);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, -6.0);
    fader.setColour (juce::Slider::trackColourId, juce::Colours::lightgrey.withAlpha (0.5f));
    fader.setColour (juce::Slider::thumbColourId, juce::Colours::lightgrey);
    fader.setValue (-6.0, juce::dontSendNotification);
    fader.onValueChange = [this] { changed(); };
    addAndMakeVisible (fader);

    onBtn.setButtonText ("On");
    onBtn.setClickingTogglesState (true);
    onBtn.setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.2f));
    onBtn.onClick = [this] { changed(); };
    addAndMakeVisible (onBtn);

    outBox.onChange = [this] { currentPair = juce::jmax (0, outBox.getSelectedId() - 1); changed(); };
    addAndMakeVisible (outBox);

    ui::disableFocus (*this);
    setWantsKeyboardFocus (false);
}

void ClickStrip::changed()
{
    if (onChanged)
        onChanged (onBtn.getToggleState(), (float) fader.getValue(), currentPair);
}

void ClickStrip::setState (bool enabled, float gainDb, int outputPair)
{
    currentPair = juce::jmax (0, outputPair);
    if (onBtn.getToggleState() != enabled)
        onBtn.setToggleState (enabled, juce::dontSendNotification);
    if (std::abs (fader.getValue() - gainDb) >= 0.05)
        fader.setValue (gainDb, juce::dontSendNotification);
    if (outBox.getSelectedId() != outputPair + 1 && outBox.getNumItems() > 0)
        outBox.setSelectedId (juce::jmin (outBox.getNumItems(), outputPair + 1), juce::dontSendNotification);
}

void ClickStrip::setLevel (float peak)
{
    meter.setLevels (0, peak, 0.0f);
}

void ClickStrip::paint (juce::Graphics& g)
{
    g.setColour (ui::panelAlt);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (juce::Colours::lightgrey);
    g.fillRoundedRectangle (getLocalBounds().removeFromTop (4).toFloat().reduced (6.0f, 0.0f), 2.0f);
}

void ClickStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromTop (4);
    nameLabel.setBounds (r.removeFromTop (20));
    outBox.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    onBtn.setBounds (r.removeFromBottom (24).reduced (2, 0));
    r.removeFromBottom (4);
    meter.setBounds (r.removeFromRight (16).withTrimmedBottom (18));
    r.removeFromRight (2);
    fader.setBounds (r);
}

//==============================================================================
MasterStrip::MasterStrip()
{
    label.setText ("MASTER", juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (ui::font (13.0f, true));
    addAndMakeVisible (label);
    // Sonoridad de la salida: momentánea (M, 400 ms) y de corto plazo (S, 3 s), en LUFS
    loudnessLabel.setJustificationType (juce::Justification::centred);
    loudnessLabel.setFont (ui::font (11.0f, true));
    loudnessLabel.setColour (juce::Label::textColourId, ui::accent);
    loudnessLabel.setTooltip (juce::String::fromUTF8 ("Sonoridad de la salida 1-2 en LUFS: M momentánea (400 ms), S de corto plazo (3 s)"));
    addAndMakeVisible (loudnessLabel);
    addAndMakeVisible (meter);

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setValue (0.0, juce::dontSendNotification);
    fader.onValueChange = [this] { if (onGainChanged) onGainChanged ((float) fader.getValue()); };
    addAndMakeVisible (fader);

    ui::disableFocus (*this);
    setWantsKeyboardFocus (false);
}

void MasterStrip::setGainDb (float db)
{
    fader.setValue (db, juce::dontSendNotification);
}

void MasterStrip::setLoudness (float momentary, float shortTerm)
{
    auto text = [] (float v) { return v > -70.0f ? juce::String (v, 1).replaceCharacter ('.', ',') : juce::String ("--"); };
    const auto s = "M " + text (momentary) + "  S " + text (shortTerm);
    if (loudnessLabel.getText() != s)
        loudnessLabel.setText (s, juce::dontSendNotification);
}

void MasterStrip::setLevels (float peakL, float peakR, float rmsL, float rmsR)
{
    meter.setLevels (0, peakL, rmsL);
    meter.setLevels (1, peakR, rmsR);
}

void MasterStrip::paint (juce::Graphics& g)
{
    g.setColour (ui::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (ui::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void MasterStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    label.setBounds (r.removeFromTop (20));
    loudnessLabel.setBounds (r.removeFromTop (16));
    r.removeFromBottom (4);
    meter.setBounds (r.removeFromRight (44).withTrimmedBottom (18));
    r.removeFromRight (4);
    fader.setBounds (r);
}

//==============================================================================
MixerPanel::MixerPanel()
{
    view.setViewedComponent (&holder, false);
    view.setScrollBarsShown (false, true);
    addAndMakeVisible (view);
    holder.addAndMakeVisible (click);   // el click va junto a los canales (sin un hueco hasta el maestro)
    addAndMakeVisible (master);
    master.onGainChanged = [this] (float db) { if (onMasterGainChanged) onMasterGainChanged (db); };
    click.onChanged = [this] (bool on, float db, int pair) { if (onClickChanged) onClickChanged (on, db, pair); };
    setWantsKeyboardFocus (false);
}

void MixerPanel::setClickVisible (bool visible)
{
    if (click.isVisible() != visible)
    {
        click.setVisible (visible);
        resized();
    }
}

void MixerPanel::setReadOnly (bool ro)
{
    readOnly = ro;
    applyReadOnly();
}

void MixerPanel::applyReadOnly()
{
    for (auto* list : { &strips, &samplerStrips, &midiStrips, &instrumentStrips })
        for (auto* strip : *list)
            strip->setReadOnly (readOnly);
}

void MixerPanel::setSong (std::shared_ptr<LoadedSong> song)
{
    strips.clear();
    if (song != nullptr)
    {
        int index = 0;
        for (auto& t : song->tracks)
        {
            const int trackIndex = index++;
            auto* strip = strips.add (new ChannelStrip (song, *t, ui::trackColour (trackIndex)));
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), t->outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            const int stem = t->stemIndex;
            strip->onLevelEdited = [this, stem] (double db) { if (onLevelEdited) onLevelEdited (stem, db); };
            strip->onTriggerClicked = [this, trackIndex] { if (onTriggerClicked) onTriggerClicked (trackIndex); };
            holder.addAndMakeVisible (strip);
        }
    }
    applyReadOnly();
    resized();
}

void MixerPanel::setSamplers (std::shared_ptr<SamplerSet> set)
{
    samplerStrips.clear();
    if (set != nullptr)
        for (auto& lane : set->lanes)
        {
            if (lane->instrumentId >= 0)
                continue;   // suena a través de un instrumento del rack: su canal es el del instrumento
            auto* strip = samplerStrips.add (new ChannelStrip (set, lane->control, juce::Colours::orange));
            strip->setTriggerBoxVisible (false);
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), lane->control.outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            holder.addAndMakeVisible (strip);
        }
    applyReadOnly();
    resized();
}

void MixerPanel::setMidiSamplers (std::shared_ptr<SamplerSet> set)
{
    midiStrips.clear();
    if (set != nullptr)
        for (auto& lane : set->lanes)
        {
            if (lane->instrumentId >= 0 || lane->silenced)
                continue;   // suena por el canal del instrumento, o su pista MIDI está silenciada
            auto* strip = midiStrips.add (new ChannelStrip (set, lane->control, juce::Colour (0xff9ccc65)));
            strip->setTriggerBoxVisible (false);
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), lane->control.outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            holder.addAndMakeVisible (strip);
        }
    applyReadOnly();
    resized();
}

void MixerPanel::setInstruments (std::shared_ptr<InstrumentSet> set)
{
    instrumentStrips.clear();
    if (set != nullptr)
        for (auto& lane : set->lanes)
        {
            auto* strip = instrumentStrips.add (new ChannelStrip (set, lane->control, juce::Colour (0xffb08cff)));
            strip->setTriggerBoxVisible (false);
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), lane->control.outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            const int id = lane->id;
            strip->onNameClicked = [this, id] { if (onInstrumentClicked) onInstrumentClicked (id); };
            holder.addAndMakeVisible (strip);
        }
    applyReadOnly();
    resized();
}

void MixerPanel::setTriggerLabel (int track, const juce::String& text, int mode)
{
    if (juce::isPositiveAndBelow (track, strips.size()))
        strips[track]->setTriggerLabel (text, mode);
}

void MixerPanel::setLevelGains (const std::vector<double>& dbPerStem, bool enabled)
{
    for (auto* strip : strips)
    {
        const int stem = strip->getTrack().stemIndex;
        const bool has = enabled && juce::isPositiveAndBelow (stem, (int) dbPerStem.size());
        strip->setLevelGain (has ? dbPerStem[(size_t) stem] : 0.0, has);
    }
}

void MixerPanel::refreshOutputs()
{
    if (! fillOutputBox)
        return;
    for (auto* strip : strips)
        fillOutputBox (strip->outputBox(), strip->getTrack().outputPair.load());
    for (auto* strip : samplerStrips)
        fillOutputBox (strip->outputBox(), strip->getTrack().outputPair.load());
    for (auto* strip : midiStrips)
        fillOutputBox (strip->outputBox(), strip->getTrack().outputPair.load());
    for (auto* strip : instrumentStrips)
        fillOutputBox (strip->outputBox(), strip->getTrack().outputPair.load());
    fillOutputBox (click.outputBox(), juce::jmax (0, click.outputBox().getSelectedId() - 1));
}

void MixerPanel::tick (AudioEngine& engine)
{
    for (auto* strip : strips)
        strip->tick();
    for (auto* strip : samplerStrips)
        strip->tick();
    for (auto* strip : midiStrips)
        strip->tick();
    for (auto* strip : instrumentStrips)
        strip->tick();
    click.setLevel (engine.takeClickPeak());
    click.tick();
    master.setLevels (engine.takeOutputPeak (0), engine.takeOutputPeak (1), engine.getOutputRms (0), engine.getOutputRms (1));
    master.setLoudness (engine.getMomentaryLufs(), engine.getShortTermLufs());
    master.tick();
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    master.setBounds (r.removeFromRight (MasterStrip::width));
    r.removeFromRight (8);
    view.setBounds (r);

    const int gap = 6;
    const int h = juce::jmax (150, view.getHeight() - view.getScrollBarThickness() - 2);
    const int total = strips.size() + samplerStrips.size() + midiStrips.size() + instrumentStrips.size();
    juce::ignoreUnused (total);
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
    int x0 = strips.size() * (ChannelStrip::width + gap) + gap * 2;   // los samplers, algo separados
    for (int i = 0; i < samplerStrips.size(); ++i)
        samplerStrips[i]->setBounds (x0 + i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
    x0 += samplerStrips.size() * (ChannelStrip::width + gap) + (samplerStrips.size() > 0 ? gap * 2 : 0);   // las pistas MIDI
    for (int i = 0; i < midiStrips.size(); ++i)
        midiStrips[i]->setBounds (x0 + i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
    x0 += midiStrips.size() * (ChannelStrip::width + gap) + (midiStrips.size() > 0 ? gap * 2 : 0);   // y los instrumentos
    for (int i = 0; i < instrumentStrips.size(); ++i)
        instrumentStrips[i]->setBounds (x0 + i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
    x0 += instrumentStrips.size() * (ChannelStrip::width + gap);
    if (click.isVisible())
    {
        click.setBounds (x0 + gap * 2, 0, ClickStrip::width, h);
        x0 = click.getRight() + gap;
    }
    holder.setSize (juce::jmax (1, x0), h);   // hasta el último canal (el click incluido)
}

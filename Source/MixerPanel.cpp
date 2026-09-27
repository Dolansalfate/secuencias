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
    addAndMakeVisible (levelBox);

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

void ChannelStrip::setLevelGain (double db, bool enabled)
{
    shownLevelDb = db;
    levelEnabled = enabled;
    if (levelBox.isBeingEdited())
        return;
    const auto text = enabled ? juce::String::formatted ("Niv %+.1f dB", db) : juce::String ("Niv --");
    if (levelBox.getText() != text)
        levelBox.setText (text, juce::dontSendNotification);
    levelBox.setColour (juce::Label::textColourId, enabled && std::abs (db) >= 0.05 ? ui::accent : juce::Colours::grey);
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
    nameLabel.setBounds (r.removeFromTop (20));
    outBox.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    auto buttons = r.removeFromBottom (24);
    muteBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
    soloBtn.setBounds (buttons.reduced (2, 0));
    r.removeFromBottom (4);
    levelBox.setBounds (r.removeFromBottom (16));
    r.removeFromBottom (2);
    meter.setBounds (r.removeFromRight (16).withTrimmedBottom (18));
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
    addAndMakeVisible (click);
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

void MixerPanel::setSong (std::shared_ptr<LoadedSong> song)
{
    strips.clear();
    if (song != nullptr)
    {
        int index = 0;
        for (auto& t : song->tracks)
        {
            auto* strip = strips.add (new ChannelStrip (song, *t, ui::trackColour (index++)));
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), t->outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            const int stem = t->stemIndex;
            strip->onLevelEdited = [this, stem] (double db) { if (onLevelEdited) onLevelEdited (stem, db); };
            holder.addAndMakeVisible (strip);
        }
    }
    resized();
}

void MixerPanel::setSamplers (std::shared_ptr<SamplerSet> set)
{
    samplerStrips.clear();
    if (set != nullptr)
        for (auto& lane : set->lanes)
        {
            auto* strip = samplerStrips.add (new ChannelStrip (set, lane->control, juce::Colours::orange));
            if (fillOutputBox)
                fillOutputBox (strip->outputBox(), lane->control.outputPair.load());
            strip->onChanged = [this] { if (onChanged) onChanged(); };
            holder.addAndMakeVisible (strip);
        }
    resized();
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
    fillOutputBox (click.outputBox(), juce::jmax (0, click.outputBox().getSelectedId() - 1));
}

void MixerPanel::tick (AudioEngine& engine)
{
    for (auto* strip : strips)
        strip->tick();
    for (auto* strip : samplerStrips)
        strip->tick();
    click.setLevel (engine.takeClickPeak());
    click.tick();
    master.setLevels (engine.takeOutputPeak (0), engine.takeOutputPeak (1), engine.getOutputRms (0), engine.getOutputRms (1));
    master.tick();
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    master.setBounds (r.removeFromRight (MasterStrip::width));
    r.removeFromRight (8);
    if (click.isVisible())
    {
        click.setBounds (r.removeFromRight (ClickStrip::width));
        r.removeFromRight (8);
    }
    view.setBounds (r);

    const int gap = 6;
    const int h = juce::jmax (150, view.getHeight() - view.getScrollBarThickness() - 2);
    const int total = strips.size() + samplerStrips.size();
    holder.setSize (juce::jmax (1, total * (ChannelStrip::width + gap) + (samplerStrips.size() > 0 ? gap * 2 : 0)), h);
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
    const int x0 = strips.size() * (ChannelStrip::width + gap) + gap * 2;   // los samplers, algo separados
    for (int i = 0; i < samplerStrips.size(); ++i)
        samplerStrips[i]->setBounds (x0 + i * (ChannelStrip::width + gap), 0, ChannelStrip::width, h);
}

#include "TempoEditor.h"
#include "TempoMap.h"
#include <cmath>

//==============================================================================
// Regla del editor: compases (de los tiempos, o de la rejilla fija) y tiempo en m:ss; clic lleva el cabezal
class TempoEditor::BarRuler : public juce::Component
{
public:
    explicit BarRuler (TempoEditor& o) : owner (o) { setWantsKeyboardFocus (false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::panel);
        g.setColour (ui::background);
        g.fillRect (0, 0, headerWidth, getHeight());
        auto x = [this] (double t) { return (float) (headerWidth + (t - owner.viewStart) * owner.pps); };
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().withTrimmedLeft (headerWidth));
        const double t0 = owner.viewStart, t1 = owner.viewStart + owner.contentWidth() / owner.pps;
        const auto& beats = owner.analysis.beats;
        g.setFont (ui::font (11.0f, true));
        int bar = 0;
        double lastLabel = -1.0e9;
        for (auto& b : beats)
        {
            if (b.beatInBar == 1) ++bar;
            if (b.seconds < t0 - 1.0) continue;
            if (b.seconds > t1) break;
            const float bx = x (b.seconds);
            if (b.beatInBar == 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.fillRect (bx, 0.0f, 1.0f, (float) getHeight() * 0.6f);
                if (bx - lastLabel >= 28.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.drawText (juce::String (bar), (int) bx + 3, 0, 40, getHeight() / 2 + 2, juce::Justification::centredLeft);
                    lastLabel = bx;
                }
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (bx, 0.0f, 1.0f, (float) getHeight() * 0.3f);
            }
        }
        // Tiempo
        double step = 1.0;
        for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 })
        {
            step = s;
            if (s * owner.pps >= 70.0) break;
        }
        g.setFont (ui::font (10.0f));
        g.setColour (juce::Colours::grey);
        for (double t = std::floor (t0 / step) * step; t <= t1; t += step)
            if (t >= 0.0)
                g.drawText (ui::formatTime (t), (int) x (t) + 3, getHeight() / 2, 60, getHeight() / 2, juce::Justification::centredLeft);
        g.setColour (ui::playhead);
        g.fillRect (x (owner.position), 0.0f, 2.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x >= headerWidth && owner.view.onSeek)
            owner.view.onSeek (juce::jlimit (0.0, owner.length, owner.viewStart + (e.x - headerWidth) / owner.pps));
    }

    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }

private:
    TempoEditor& owner;
};

//==============================================================================
TempoEditor::TempoEditor()
{
    ruler = std::make_unique<BarRuler> (*this);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (view);
    view.setShowsPlayhead (true);
    view.setShowsEditorButton (false);
    // La sección que se elige en el gráfico pasa a la barra
    view.onBandSelected = [this] (int band)
    {
        view.setSelectedBand (band);
        refreshToolbar();
    };

    sectionLabel.setFont (ui::font (13.0f, true));
    bpmLabel.setText ("Tempo", juce::dontSendNotification);
    curveLabel.setText (juce::String::fromUTF8 ("Transición"), juce::dontSendNotification);
    for (auto* l : { &sectionLabel, &bpmLabel, &curveLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (l);
    }
    bpmEdit.setJustification (juce::Justification::centred);
    bpmEdit.setInputRestrictions (6, "0123456789.,");
    bpmEdit.onReturnKey = [this]
    {
        const int band = view.getSelectedBand();
        const double v = bpmEdit.getText().replace (",", ".").getDoubleValue();
        if (band >= 0 && v >= 20.0 && v <= 400.0 && view.onTempoSet)
            view.onTempoSet (band, v);
        grabKeyboardFocus();
    };
    bpmEdit.onEscapeKey = [this] { refreshToolbar(); grabKeyboardFocus(); };
    addAndMakeVisible (bpmEdit);
    for (int c = 0; c <= TempoRegion::sCurve; ++c)
        curveBox.addItem (tempoCurveName (c), c + 1);
    curveBox.onChange = [this]
    {
        const int band = view.getSelectedBand();
        if (band >= 0 && onCurveSet)
            onCurveSet (band, curveBox.getSelectedId() - 1);
    };
    addAndMakeVisible (curveBox);
    addBtn.setButtonText (juce::String::fromUTF8 ("+ Cambio en el cabezal"));
    addBtn.onClick = [this]
    {
        if (view.onTempoAdd)
        {
            double bpm = 120.0;
            for (auto& b : bands)
                if (position >= b.start)
                    bpm = stretches ? b.playBpm : b.origBpm;
            view.onTempoAdd (position, bpm);
        }
    };
    removeBtn.setButtonText (juce::String::fromUTF8 ("Quitar el cambio"));
    removeBtn.onClick = [this]
    {
        const int band = view.getSelectedBand();
        if (band > 0 && view.onTempoRemove)
            view.onTempoRemove (band);
    };
    drawBtn.setButtonText ("Dibujar");
    drawBtn.setClickingTogglesState (true);
    drawBtn.setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.3f));
    drawBtn.onClick = [this] { view.setTool (drawBtn.getToggleState() ? ui::EditTool::draw : ui::EditTool::select); };
    fitBtn.setButtonText ("Ajustar");
    fitBtn.onClick = [this] { zoomToFit(); };
    zoomOutBtn.setButtonText ("-");
    zoomOutBtn.onClick = [this] { setView (viewStart, pps / 1.5); };
    zoomInBtn.setButtonText ("+");
    zoomInBtn.onClick = [this] { setView (viewStart, pps * 1.5); };
    for (auto* b : { &addBtn, &removeBtn, &drawBtn, &fitBtn, &zoomOutBtn, &zoomInBtn })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    curveBox.setWantsKeyboardFocus (false);
    scroll.setAutoHide (false);
    scroll.addListener (this);
    addAndMakeVisible (scroll);
    setWantsKeyboardFocus (true);
    refreshToolbar();
}

void TempoEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);
    g.setColour (ui::panel);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
}

void TempoEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).reduced (8, 6);
    // Primero lo de la derecha (zoom y dibujo); el nombre de la sección se queda con lo que sobre
    zoomInBtn.setBounds (bar.removeFromRight (30));
    zoomOutBtn.setBounds (bar.removeFromRight (30));
    bar.removeFromRight (6);
    fitBtn.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    drawBtn.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (12);
    removeBtn.setBounds (bar.removeFromRight (124));
    bar.removeFromRight (6);
    addBtn.setBounds (bar.removeFromRight (170));
    bar.removeFromRight (12);
    curveBox.setBounds (bar.removeFromRight (170));
    curveLabel.setBounds (bar.removeFromRight (76));
    bar.removeFromRight (12);
    bpmEdit.setBounds (bar.removeFromRight (70));
    bpmLabel.setBounds (bar.removeFromRight (50));
    bar.removeFromRight (8);
    sectionLabel.setBounds (bar);
    ruler->setBounds (r.removeFromTop (rulerHeight));
    scroll.setBounds (r.removeFromBottom (scrollHeight).withTrimmedLeft (headerWidth));
    view.setBounds (r);
    setView (viewStart, pps);
}

double TempoEditor::minPps() const
{
    return length > 0.0 ? contentWidth() / length : 1.0;
}

void TempoEditor::setView (double start, double newPps)
{
    newPps = juce::jlimit (minPps(), 2000.0, newPps);
    const double visible = contentWidth() / newPps;
    start = juce::jlimit (0.0, juce::jmax (0.0, length - visible), start);
    viewStart = start;
    pps = newPps;
    view.setView (viewStart, pps);
    scroll.setRangeLimits (0.0, juce::jmax (0.001, length), juce::dontSendNotification);
    scroll.setCurrentRange (viewStart, visible, juce::dontSendNotification);
    ruler->repaint();
}

void TempoEditor::zoomToFit()
{
    setView (0.0, minPps());
}

void TempoEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    setView (newRangeStart, pps);
}

void TempoEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const int cx = juce::jmax (0, e.getEventRelativeTo (this).x - headerWidth);
        const double anchor = viewStart + cx / pps;
        const double factor = std::pow (1.5, juce::jlimit (-1.0, 1.0, w.deltaY * 6.0));
        const double p = juce::jlimit (minPps(), 2000.0, pps * factor);
        setView (anchor - cx / p, p);
    }
    else
    {
        const double delta = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        setView (viewStart - delta * (contentWidth() / pps) * 0.6, pps);
    }
}

bool TempoEditor::keyPressed (const juce::KeyPress& key)
{
    if (bpmEdit.hasKeyboardFocus (false))
        return false;   // escribiendo el tempo
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'T' || key.getKeyCode() == 't'))
    {
        if (auto* w = findParentComponentOfClass<TempoEditorWindow>())
            w->closeButtonPressed();
        return true;
    }
    return onKey ? onKey (key) : false;
}

void TempoEditor::setBands (const std::vector<TempoBandView>& b)
{
    bands = b;
    view.setBands (bands);
    refreshToolbar();
}

void TempoEditor::setAnalysis (const Analysis& a)
{
    analysis.beats = a.beats;
    view.setAnalysis (a);
    ruler->repaint();
}

void TempoEditor::setFixedGrid (double bpm, double clickOffset)
{
    view.setFixedGrid (bpm, clickOffset);
}

void TempoEditor::setLength (double seconds)
{
    const bool first = length <= 0.0;
    length = seconds;
    view.setLength (seconds);
    if (first)
        zoomToFit();
    else
        setView (viewStart, pps);
}

void TempoEditor::setPosition (double seconds)
{
    if (std::abs (seconds - position) < 1.0e-4)
        return;
    const int oldX = headerWidth + (int) ((position - viewStart) * pps);
    position = seconds;
    view.setPosition (seconds);
    ruler->repaint (oldX - 2, 0, 5, ruler->getHeight());
    ruler->repaint (headerWidth + (int) ((position - viewStart) * pps) - 2, 0, 5, ruler->getHeight());
}

void TempoEditor::setStretches (bool s)
{
    stretches = s;
    view.setStretches (s);
    refreshToolbar();
}

void TempoEditor::setReadOnly (bool ro)
{
    readOnly = ro;
    view.setReadOnly (ro);
    for (auto* c : std::initializer_list<juce::Component*> { &bpmEdit, &curveBox, &addBtn, &removeBtn, &drawBtn })
        c->setEnabled (! ro);
}

void TempoEditor::selectBandAt (double seconds)
{
    int band = bands.empty() ? -1 : 0;
    for (int i = 0; i < (int) bands.size(); ++i)
        if (seconds >= bands[(size_t) i].start)
            band = i;
    view.setSelectedBand (band);
    refreshToolbar();
}

void TempoEditor::refreshToolbar()
{
    const int band = view.getSelectedBand();
    const bool has = juce::isPositiveAndBelow (band, (int) bands.size());
    if (! has)
    {
        sectionLabel.setText (juce::String::fromUTF8 ("Elige una sección en el gráfico"), juce::dontSendNotification);
        bpmEdit.setText ({}, false);
        curveBox.setSelectedId (0, juce::dontSendNotification);
    }
    else
    {
        const auto& b = bands[(size_t) band];
        int bar = 0;
        for (auto& beat : analysis.beats)
            if (beat.beatInBar == 1 && beat.seconds <= b.start + 0.01)
                ++bar;
        juce::String text = juce::String::fromUTF8 ("Sección ") + juce::String (band + 1);
        if (bar > 0)
            text += juce::String::fromUTF8 (" · compás ") + juce::String (bar);
        text += juce::String::fromUTF8 (" · ") + ui::formatTime (b.start);
        sectionLabel.setText (text, juce::dontSendNotification);
        if (! bpmEdit.hasKeyboardFocus (false))
            bpmEdit.setText (juce::String (stretches ? b.playBpm : b.origBpm, 1), false);
        curveBox.setSelectedId ((stretches ? b.curve : b.gridCurve) + 1, juce::dontSendNotification);
    }
    const bool last = ! has || band + 1 >= (int) bands.size();
    curveBox.setEnabled (has && ! last && ! readOnly);   // la última sección no tiene a dónde llegar
    removeBtn.setEnabled (has && band > 0 && ! readOnly);
    bpmEdit.setEnabled (has && ! readOnly);
}

//==============================================================================
TempoEditorWindow::TempoEditorWindow (std::function<void()> close)
    : DocumentWindow (juce::String::fromUTF8 ("Secuencias: pista de tempo"), ui::background, DocumentWindow::allButtons),
      onClose (std::move (close))
{
    setUsingNativeTitleBar (true);
    content = new TempoEditor();
    setContentOwned (content, true);
    setResizable (true, true);
    setResizeLimits (980, 320, 8192, 8192);   // la barra de arriba necesita unos 960 px
    centreWithSize (1180, 520);
}

void TempoEditorWindow::closeButtonPressed()
{
    setVisible (false);
    if (onClose)
        onClose();
}

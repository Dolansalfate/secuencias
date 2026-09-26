#include "StageView.h"
#include "UiUtils.h"
#include "Library.h"

namespace
{
    const juce::Colour stageBackground (0xff0b0e11);
    const juce::Colour dim (0xff6b7480);
}

StageView::StageView()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

void StageView::setState (const StageState& s)
{
    state = s;
    repaint();
}

bool StageView::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        if (onToggleFullScreen) onToggleFullScreen();
        return true;
    }
    return onKey ? onKey (key) : false;
}

void StageView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onToggleFullScreen)
        onToggleFullScreen();
}

void StageView::paint (juce::Graphics& g)
{
    g.fillAll (stageBackground);
    const auto area = getLocalBounds();
    const float W = (float) area.getWidth(), H = (float) area.getHeight();
    const float unit = juce::jmin (H / 10.0f, W / 16.0f);   // escala de las fuentes con la ventana

    if (! state.hasSong)
    {
        g.setColour (dim);
        g.setFont (ui::font (unit * 0.9f));
        g.drawText (tr ("Sin canción cargada"), area, juce::Justification::centred);
        return;
    }

    // --- Cabecera: título, sección actual y siguiente, tiempo ---
    auto header = area.withHeight ((int) (H * 0.15f)).reduced ((int) (unit * 0.5f), (int) (unit * 0.25f));
    g.setColour (juce::Colours::white);
    g.setFont (ui::font (unit * 0.8f, true));
    g.drawText (state.song, header.removeFromLeft ((int) (W * 0.40f)), juce::Justification::centredLeft, true);
    g.setFont (ui::font (unit * 0.7f));
    g.drawText (ui::formatTime (state.position) + " / " + ui::formatTime (state.length), header.removeFromRight ((int) (W * 0.27f)), juce::Justification::centredRight, true);
    if (state.section.isNotEmpty() || state.nextSection.isNotEmpty())
    {
        auto sec = header;
        g.setColour (ui::accent);
        g.setFont (ui::font (unit * 0.85f, true));
        g.drawText (state.section, sec.removeFromTop (sec.getHeight() / 2), juce::Justification::centred, true);
        if (state.nextSection.isNotEmpty())
        {
            g.setColour (dim);
            g.setFont (ui::font (unit * 0.6f));
            g.drawText (tr ("luego: ") + state.nextSection, sec, juce::Justification::centred, true);
        }
    }

    // --- Centro: acorde actual (enorme) y siguiente ---
    auto centre = area.withTrimmedTop ((int) (H * 0.15f)).withTrimmedBottom ((int) (H * 0.43f));
    if (! state.hasAnalysis)
    {
        g.setColour (dim);
        g.setFont (ui::font (unit * 0.8f));
        g.drawText (tr ("Sin análisis: pulsa «Analizar (IA)» para ver los acordes"), centre, juce::Justification::centred);
    }
    else
    {
        const bool hasNext = state.nextChord.isNotEmpty();
        auto nowArea = hasNext ? centre.removeFromLeft ((int) (W * 0.58f)) : centre;
        g.setColour (state.playing ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f));
        g.setFont (ui::font (unit * 3.6f, true));
        g.drawText (state.chord.isEmpty() ? "-" : state.chord, nowArea, juce::Justification::centred, true);
        if (hasNext)
        {
            auto nextArea = centre;
            auto label = nextArea.removeFromTop ((int) (unit * 0.9f));
            g.setColour (dim);
            g.setFont (ui::font (unit * 0.6f));
            g.drawText (tr ("SIGUIENTE"), label, juce::Justification::centred);
            g.setColour (ui::accent);
            g.setFont (ui::font (unit * 2.2f, true));
            g.drawText (state.nextChord, nextArea, juce::Justification::centred, true);
        }
    }

    // --- Nota de texto: letrero amarillo mientras dura; antes de empezar, aviso tenue de la próxima ---
    if (state.note.isNotEmpty() || state.nextNote.isNotEmpty())
    {
        const auto yellow = juce::Colour (0xffffd54f);
        auto band = area.withTop ((int) (H * 0.57f)).withBottom ((int) (H * 0.67f)).reduced ((int) (unit * 0.5f), 0);
        if (state.note.isNotEmpty())
        {
            g.setColour (yellow);
            g.fillRoundedRectangle (band.toFloat(), unit * 0.25f);
            g.setColour (juce::Colours::black);
            g.setFont (ui::font (unit * 0.9f, true));
            g.drawFittedText (state.note, band.reduced ((int) (unit * 0.4f), 2), juce::Justification::centred, 2, 0.7f);
        }
        else
        {
            g.setColour (yellow.withAlpha (0.65f));
            g.setFont (ui::font (unit * 0.6f));
            g.drawFittedText (tr ("Próxima nota") + (state.nextNoteIn >= 0.0 ? juce::String::formatted (" (en %.0f s)", state.nextNoteIn) : juce::String())
                                  + ": " + state.nextNote, band, juce::Justification::centred, 2, 0.7f);
        }
    }

    // --- Pie: compás y pulso a la izquierda, tempo y tonalidad a la derecha ---
    auto footer = area.withTop ((int) (H * 0.68f)).withBottom ((int) (H * 0.90f)).reduced ((int) (unit * 0.5f), (int) (unit * 0.2f));
    auto right = footer.removeFromRight ((int) (W * 0.26f));
    g.setColour (juce::Colours::white);
    g.setFont (ui::font (unit * 1.1f, true));
    if (state.bpm > 0.0)
        g.drawText (juce::String::formatted ("%.0f BPM", state.bpm), right.removeFromTop (right.getHeight() / 2), juce::Justification::centredRight);
    g.setColour (dim);
    g.setFont (ui::font (unit * 0.8f));
    g.drawText (state.key, right, juce::Justification::centredRight);

    if (state.bar > 0)
    {
        auto barArea = footer.removeFromLeft ((int) (W * 0.36f));
        g.setColour (juce::Colours::white);
        g.setFont (ui::font (unit * 1.1f, true));
        g.drawText (tr ("COMPÁS ") + juce::String (state.bar), barArea.removeFromTop (barArea.getHeight() / 2), juce::Justification::centredLeft, true);
        g.setColour (dim);
        g.setFont (ui::font (unit * 0.8f));
        g.drawText (tr ("tiempo ") + juce::String (state.beat) + " / " + juce::String (state.beatsInBar), barArea, juce::Justification::centredLeft);

        // Pulso: un punto por tiempo del compás; el actual lleno (el 1 en color de acento) y una barra de avance
        const int n = juce::jlimit (1, 12, state.beatsInBar);
        const float dot = juce::jmin (unit * 1.1f, (float) footer.getWidth() / (float) (n * 2));
        const float gap = dot * 0.6f;
        const float total = n * dot + (n - 1) * gap;
        float x = (float) footer.getX() + ((float) footer.getWidth() - total) * 0.5f;
        const float y = (float) footer.getCentreY() - dot * 0.5f - unit * 0.2f;
        for (int i = 1; i <= n; ++i)
        {
            const bool current = i == state.beat;
            const auto colour = i == 1 ? ui::accent : juce::Colours::white;
            g.setColour (current ? colour : colour.withAlpha (0.25f));
            if (current)
                g.fillEllipse (x, y, dot, dot);
            else
                g.drawEllipse (x + 1.0f, y + 1.0f, dot - 2.0f, dot - 2.0f, 2.0f);
            x += dot + gap;
        }
        const float barY = y + dot + unit * 0.35f;
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle ((float) footer.getX() + ((float) footer.getWidth() - total) * 0.5f, barY, total, unit * 0.12f, unit * 0.06f);
        g.setColour (ui::accent);
        g.fillRoundedRectangle ((float) footer.getX() + ((float) footer.getWidth() - total) * 0.5f, barY,
                                total * (float) juce::jlimit (0.0, 1.0, ((state.beat - 1) + state.beatProgress) / (double) n), unit * 0.12f, unit * 0.06f);
    }

    if (! state.playing)
    {
        g.setColour (dim);
        g.setFont (ui::font (unit * 0.7f));
        g.drawText (state.nextSong.isNotEmpty() ? tr ("Detenido  ·  siguiente canción: ") + state.nextSong : tr ("Detenido"),
                    area.withTop ((int) (H * 0.90f)), juce::Justification::centred, true);
    }
}

//==============================================================================
StageWindow::StageWindow (std::function<void()> close)
    : DocumentWindow (tr ("Secuencias: guía de escenario"), juce::Colour (0xff0b0e11), DocumentWindow::allButtons),
      onClose (std::move (close))
{
    setUsingNativeTitleBar (true);
    stageView = new StageView();
    setContentOwned (stageView, true);
    setResizable (true, true);
    setResizeLimits (480, 300, 8192, 8192);
    centreWithSize (1024, 600);
    stageView->onToggleFullScreen = [this] { toggleFullScreen(); };
}

void StageWindow::toggleFullScreen()
{
    auto& desktop = juce::Desktop::getInstance();
    desktop.setKioskModeComponent (desktop.getKioskModeComponent() == this ? nullptr : this, false);
}

void StageWindow::closeButtonPressed()
{
    if (juce::Desktop::getInstance().getKioskModeComponent() == this)
        juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
    setVisible (false);
    if (onClose)
        onClose();
}

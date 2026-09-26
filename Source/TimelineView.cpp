#include "TimelineView.h"
#include "UiUtils.h"
#include <cmath>

//==============================================================================
// Regla: fila de marcadores, fila de secciones de tempo, fila de compases, fila de tiempo y
// fila de acordes. Clic = saltar, arrastre = recorrer; sobre un marcador, clic = ir, arrastre =
// moverlo, clic derecho = menú; sobre una sección de tempo, clic = menú de la sección.
class TimelineView::Ruler : public juce::Component
{
public:
    static constexpr int rowMarkers = 0, rowTempo = 16, rowNotes = 32, rowBars = 46, rowTime = 61, rowChords = 76;

    explicit Ruler (TimelineView& o) : owner (o)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    int tempoBandAt (int x) const
    {
        const double t = owner.xToTime (x - headerWidth);
        for (int i = (int) owner.tempoBands.size(); --i >= 0;)
            if (t >= owner.tempoBands[(size_t) i].start)
                return i;
        return owner.tempoBands.empty() ? -1 : 0;
    }

    static bool inTempoRow (int y) { return y >= rowTempo && y < rowNotes; }

    int noteAt (int x) const
    {
        for (int i = (int) owner.notes.size(); --i >= 0;)
            if (std::abs (x - (headerWidth + owner.timeToX (owner.notes[(size_t) i].seconds))) <= 8)
                return i;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool hand = owner.song != nullptr && e.x >= headerWidth && inTempoRow (e.y) && tempoBandAt (e.x) >= 0;
        setMouseCursor (hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    int markerAt (int x) const
    {
        for (int i = (int) owner.markers.size(); --i >= 0;)
        {
            const int mx = headerWidth + owner.timeToX (owner.markers[(size_t) i].seconds);
            if (std::abs (x - mx) <= 7)
                return i;
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        moved = false;
        dragging = -1;
        if (owner.song == nullptr || e.x < headerWidth)
            return;
        if (inTempoRow (e.y))
        {
            const int band = tempoBandAt (e.x);
            if (band >= 0 && owner.onTempoBandClicked)
                owner.onTempoBandClicked (band, juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (e.x - headerWidth)));
            return;
        }
        if (e.mods.isPopupMenu() && e.y >= rowNotes && e.y < rowBars)
        {
            if (owner.onNoteClicked)
                owner.onNoteClicked (noteAt (e.x), juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (e.x - headerWidth)));
            return;
        }
        if (e.mods.isPopupMenu() && e.y >= rowChords && ! owner.analysis.isEmpty())
        {
            const double t = juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (e.x - headerWidth));
            if (owner.onChordClicked)
                owner.onChordClicked (owner.analysis.chordAt (t), t);
            return;
        }
        if (e.mods.isPopupMenu() && e.y >= rowBars && e.y < rowTime && ! owner.analysis.beats.empty())
        {
            // Clic derecho sobre un tiempo detectado (a menos de 8 px): menú del tiempo
            const int beat = owner.analysis.nearestBeat (owner.xToTime (e.x - headerWidth), 8.0 / owner.pixelsPerSecond);
            if (beat >= 0 && owner.onBeatClicked)
                owner.onBeatClicked (beat);
            return;
        }
        const int m = markerAt (e.x);
        if (m >= 0 && e.mods.isPopupMenu())
        {
            if (owner.onMarkerClicked) owner.onMarkerClicked (m, true);
            return;
        }
        if (m >= 0)
        {
            dragging = m;
            dragTime = owner.markers[(size_t) m].seconds;
            return;
        }
        owner.seekAt (e.x - headerWidth, false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (owner.song == nullptr)
            return;
        if (dragging >= 0)
        {
            moved = moved || std::abs (e.getDistanceFromDragStartX()) > 3;
            dragTime = juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (e.x - headerWidth));
            repaint();
            return;
        }
        owner.seekAt (e.x - headerWidth, true);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0)
            return;
        const int m = dragging;
        dragging = -1;
        if (moved)
        {
            if (owner.onMarkerMoved) owner.onMarkerMoved (m, dragTime);
        }
        else if (owner.onMarkerClicked)
            owner.onMarkerClicked (m, false);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::panel);
        g.setColour (ui::background);
        g.fillRect (0, 0, headerWidth, getHeight());
        if (owner.song == nullptr)
            return;

        const auto content = getLocalBounds().withTrimmedLeft (headerWidth);
        g.reduceClipRegion (content);
        const double pps = owner.pixelsPerSecond;
        const double t0 = owner.viewStart, t1 = owner.xToTime (content.getWidth());
        auto x = [&] (double t) { return (float) (headerWidth + owner.timeToX (t)); };

        const auto& an = owner.analysis;

        // Secciones de tempo: una banda por sección con su BPM ("orig -> reproducción" si está estirada)
        for (size_t i = 0; i < owner.tempoBands.size(); ++i)
        {
            const auto& band = owner.tempoBands[i];
            if (band.end < t0 || band.start > t1) continue;
            const float a = juce::jmax ((float) headerWidth, x (band.start)), b = juce::jmin ((float) getWidth(), x (band.end));
            if (b - a < 1.0f) continue;
            const bool stretched = std::abs (band.playBpm - band.origBpm) >= 0.05 || band.transpose != 0;
            g.setColour (stretched ? ui::accent.withAlpha (0.35f) : (i % 2 == 0 ? juce::Colour (0xff2f3b47) : juce::Colour (0xff28333e)));
            g.fillRect (a, (float) rowTempo + 1.0f, b - a, (float) (rowBars - rowTempo - 2));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillRect (x (band.start), (float) rowTempo, 1.0f, (float) (rowBars - rowTempo));
            if (b - a >= 40.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (stretched ? 1.0f : 0.85f));
                g.setFont (ui::font (11.0f, true));
                auto text = std::abs (band.playBpm - band.origBpm) >= 0.05 ? juce::String::formatted ("%.1f -> %.1f BPM", band.origBpm, band.playBpm)
                                                                          : juce::String::formatted ("%.1f BPM", band.origBpm);
                if (band.transpose != 0)
                    text += juce::String::formatted ("  %+d st", band.transpose);
                g.drawText (text, (int) a + 5, rowTempo, (int) (b - a) - 8, rowBars - rowTempo, juce::Justification::centredLeft, true);
            }
        }

        // Notas de texto: banderita amarilla y texto hasta la siguiente nota
        for (size_t i = 0; i < owner.notes.size(); ++i)
        {
            const auto& note = owner.notes[i];
            const double nextT = i + 1 < owner.notes.size() ? owner.notes[i + 1].seconds : t1 + 1.0;
            if (note.seconds > t1 || nextT < t0) continue;
            const float nx = x (note.seconds);
            const auto yellow = juce::Colour (0xffffd54f);
            g.setColour (yellow);
            juce::Path flag;
            flag.addTriangle (nx, (float) rowNotes + 2.0f, nx + 6.0f, (float) rowNotes + 7.0f, nx, (float) rowNotes + 12.0f);
            g.fillPath (flag);
            g.fillRect (nx, (float) rowNotes, 1.0f, (float) (rowBars - rowNotes));
            g.setFont (ui::font (10.0f, true));
            g.drawText (note.text.upToFirstOccurrenceOf ("\n", false, false), (int) nx + 8, rowNotes, (int) juce::jmax (0.0f, juce::jmin (x (nextT), (float) getWidth()) - nx - 10.0f),
                        rowBars - rowNotes, juce::Justification::centredLeft, true);
        }

        if (! an.beats.empty())
        {
            // Compases y tiempos detectados por el análisis. Muy alejado, solo cada k compases.
            int barNumber = 0;
            const double barPx = an.bpm > 0.0 ? (240.0 / an.bpm) * pps : 1.0e9;
            const int barStep = juce::jmax (1, (int) std::ceil (6.0 / juce::jmax (0.1, barPx)));
            const bool showBeats = an.bpm > 0.0 && (60.0 / an.bpm) * pps >= 7.0;
            for (auto& b : an.beats)
            {
                if (b.beatInBar == 1) ++barNumber;
                if (b.seconds < t0 - 1.0) continue;
                if (b.seconds > t1) break;
                if (b.beatInBar == 1 && (barNumber - 1) % barStep == 0)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.45f));
                    g.fillRect (x (b.seconds), (float) rowBars, 1.0f, (float) (rowTime - rowBars));
                    if (barPx * barStep >= 34.0)
                    {
                        g.setFont (ui::font (10.0f));
                        g.setColour (juce::Colours::white.withAlpha (0.8f));
                        g.drawText (juce::String (barNumber), (int) x (b.seconds) + 3, rowBars, 40, rowTime - rowBars, juce::Justification::centredLeft);
                    }
                }
                else if (b.beatInBar != 1 && showBeats)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.15f));
                    g.fillRect (x (b.seconds), (float) (rowTime - 5), 1.0f, 5.0f);
                }
            }
        }
        else if (owner.bpm > 0.0)
        {
            // Compases y tiempos (4/4) de la rejilla fija, a partir del inicio del click
            const double beat = 60.0 / owner.bpm, bar = beat * 4.0;
            const int labelEvery = (int) std::ceil (56.0 / (bar * pps));   // etiquetas a >= 56 px
            const double firstBar = std::floor ((t0 - owner.clickOffset) / bar);
            for (double b = firstBar; ; b += 1.0)
            {
                const double t = owner.clickOffset + b * bar;
                if (t > t1) break;
                if (t < 0.0) continue;
                const int number = (int) b + 1;
                const bool labelled = labelEvery > 0 && ((int) b % labelEvery) == 0;
                g.setColour (juce::Colours::white.withAlpha (labelled ? 0.5f : 0.25f));
                g.fillRect (x (t), (float) rowBars, 1.0f, (float) (rowTime - rowBars));
                if (labelled && number >= 1)
                {
                    g.setFont (ui::font (10.0f));
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.drawText (juce::String (number), (int) x (t) + 3, rowBars, 40, rowTime - rowBars, juce::Justification::centredLeft);
                }
                if (beat * pps >= 7.0)
                    for (int k = 1; k < 4; ++k)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.15f));
                        g.fillRect (x (t + k * beat), (float) (rowTime - 5), 1.0f, 5.0f);
                    }
            }
        }

        // Tiempo en m:ss
        {
            double step = 1.0;
            for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
            {
                step = s;
                if (s * pps >= 64.0) break;
            }
            g.setFont (ui::font (10.0f));
            for (double t = std::floor (t0 / step) * step; t <= t1; t += step)
            {
                if (t < 0.0) continue;
                g.setColour (juce::Colours::grey);
                g.fillRect (x (t), (float) rowTime, 1.0f, (float) (getHeight() - rowTime));
                g.drawText (ui::formatTime (t), (int) x (t) + 3, rowTime, 60, getHeight() - rowTime, juce::Justification::centredLeft);
            }
        }

        // Acordes detectados: bloques con nombre; el que suena, resaltado
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRect (content.getX(), rowChords, content.getWidth(), getHeight() - rowChords);
            const int current = an.chordAt (owner.position);
            for (int i = 0; i < (int) an.chords.size(); ++i)
            {
                const auto& c = an.chords[(size_t) i];
                if (c.end < t0 || c.start > t1 || c.name == "N") continue;
                const float a = x (c.start), b = x (c.end);
                if (b - a < 1.0f) continue;
                g.setColour (i == current ? ui::accent.withAlpha (0.55f) : (i % 2 == 0 ? juce::Colour (0xff3a4652) : juce::Colour (0xff31404c)));
                g.fillRoundedRectangle (a + 0.5f, (float) rowChords + 2.0f, juce::jmax (1.0f, b - a - 1.0f), (float) (getHeight() - rowChords - 4), 3.0f);
                if (b - a >= 18.0f)
                {
                    g.setColour (juce::Colours::white);
                    g.setFont (ui::font (12.0f, true));
                    g.drawText (c.name, (int) a + 3, rowChords, (int) (b - a) - 4, getHeight() - rowChords, juce::Justification::centredLeft, false);
                }
            }
        }

        // Marcadores: bandera, nombre y nivelado del tramo hasta el siguiente marcador
        for (int i = 0; i < (int) owner.markers.size(); ++i)
        {
            const auto& m = owner.markers[(size_t) i];
            const double t = (dragging == i) ? dragTime : m.seconds;
            const double nextT = i + 1 < (int) owner.markers.size() ? owner.markers[(size_t) i + 1].seconds : owner.lengthSeconds();
            const float mx = x (t);
            const float next = x (nextT);
            if (mx > (float) getWidth() || next < (float) headerWidth) continue;
            const auto colour = dragging == i ? juce::Colours::white : ui::accent;
            g.setColour (colour);
            juce::Path flag;
            flag.addTriangle (mx - 5.0f, (float) rowMarkers, mx + 5.0f, (float) rowMarkers, mx, (float) rowMarkers + 8.0f);
            g.fillPath (flag);
            g.fillRect (mx, (float) rowMarkers, 1.0f, (float) rowChords);
            g.setFont (ui::font (11.0f, true));
            juce::String text = (i < 9 ? juce::String (i + 1) + " " : juce::String()) + m.name;
            if (m.lufs > unmeasuredDb + 1.0)
            {
                text += juce::String::formatted ("  %.0f LUFS", m.lufs);
                if (owner.levelingEnabled && std::abs (m.gainDb) >= 0.05)
                    text += juce::String::formatted (" (%+.1f dB)", m.gainDb);
            }
            g.drawText (text, (int) mx + 7, rowMarkers, (int) juce::jmax (0.0f, next - mx - 10.0f), 15, juce::Justification::centredLeft, true);
        }
    }

private:
    TimelineView& owner;
    int dragging = -1;
    double dragTime = 0.0;
    bool moved = false;
};

//==============================================================================
// Una pista: cabecera con color, nombre, M y S; forma de onda cacheada en una imagen que solo se
// vuelve a dibujar cuando cambian el zoom, el desplazamiento o el tamaño.
class TimelineView::Lane : public juce::Component
{
public:
    Lane (TimelineView& o, LoadedTrack& t, int i) : owner (o), track (t), index (i), colour (ui::trackColour (i))
    {
        name.setText (track.name, juce::dontSendNotification);
        name.setFont (ui::font (13.0f, true));
        name.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (name);
        mute.setButtonText ("M");
        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
        mute.onClick = [this] { if (owner.onMute) owner.onMute (index, mute.getToggleState()); };
        addAndMakeVisible (mute);
        solo.setButtonText ("S");
        solo.setClickingTogglesState (true);
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colours::gold.darker (0.2f));
        solo.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        solo.onClick = [this] { if (owner.onSolo) owner.onSolo (index, solo.getToggleState()); };
        addAndMakeVisible (solo);
        refreshState();
        ui::disableFocus (*this);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void refreshState()
    {
        if (mute.getToggleState() != track.muted.load()) mute.setToggleState (track.muted.load(), juce::dontSendNotification);
        if (solo.getToggleState() != track.solo.load())  solo.setToggleState (track.solo.load(), juce::dontSendNotification);
    }

    void invalidate()
    {
        imageValid = false;
        repaint();
    }

    void resized() override
    {
        auto h = getLocalBounds().withWidth (headerWidth).reduced (6, 4);
        h.removeFromLeft (6);
        auto buttons = h.removeFromBottom (juce::jmin (20, h.getHeight() / 2));
        mute.setBounds (buttons.removeFromLeft (26));
        buttons.removeFromLeft (4);
        solo.setBounds (buttons.removeFromLeft (26));
        name.setBounds (h);
        invalidate();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth || owner.song == nullptr)
            return;
        const double t = juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (e.x - headerWidth));
        if (e.mods.isPopupMenu())
        {
            if (owner.onLaneMenu) owner.onLaneMenu (t, index);
            return;
        }
        if (e.mods.isShiftDown() && ! owner.clips.empty())
        {
            // Shift + arrastre: mover el tramo de audio que está bajo el mouse (todos los stems)
            owner.clipDragging = true;
            owner.clipDragFrom = t;
            owner.clipDragDelta = 0.0;
            owner.clipDragLane = index;
            owner.repaintOverlay();
            return;
        }
        owner.seekAt (e.x - headerWidth, false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth + 0 && ! owner.clipDragging)
            return;
        if (owner.song == nullptr)
            return;
        if (owner.clipDragging)
        {
            owner.clipDragDelta = owner.xToTime (e.x - headerWidth) - owner.clipDragFrom;
            owner.repaintOverlay();
            return;
        }
        owner.seekAt (juce::jmax (0, e.x - headerWidth), true);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! owner.clipDragging)
            return;
        owner.clipDragging = false;
        const double delta = owner.clipDragDelta;
        owner.clipDragDelta = 0.0;
        owner.repaintOverlay();
        if (std::abs (delta) > 1.0e-4 && owner.onClipDragged)
            owner.onClipDragged (owner.clipDragFrom, delta, owner.clipDragLane);
    }

    void paint (juce::Graphics& g) override
    {
        // Cabecera
        g.setColour (index % 2 == 0 ? ui::panel : ui::panelAlt);
        g.fillRect (0, 0, headerWidth, getHeight());
        g.setColour (colour);
        g.fillRect (0, 0, 4, getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (headerWidth - 1, 0, 1, getHeight());

        // Forma de onda
        if (! imageValid)
            renderImage();
        g.drawImageAt (image, headerWidth, 0);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (headerWidth, getHeight() - 1, getWidth() - headerWidth, 1);
    }

    LoadedTrack& getTrack() { return track; }

private:
    void renderImage()
    {
        const int w = juce::jmax (1, getWidth() - headerWidth), h = juce::jmax (1, getHeight());
        image = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (image);
        g.fillAll (index % 2 == 0 ? juce::Colour (0xff181c21) : juce::Colour (0xff1b2026));

        const auto& wf = track.waveform;
        const double sr = owner.sampleRate;
        const float midL = h * 0.25f, midR = h * 0.75f;
        // Escalado visual al pico de la pista (hasta 8x): el nivel real se ve en los medidores
        const float half = juce::jmax (1.0f, h * 0.25f - 2.0f) * juce::jlimit (1.0f, 8.0f, 1.0f / juce::jmax (0.001f, wf.peak));
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (0, (int) midL, w, 1);
        g.fillRect (0, (int) midR, w, 1);

        for (int x = 0; x < w; ++x)
        {
            const auto s0 = (juce::int64) (owner.xToTime (x) * sr);
            auto s1 = (juce::int64) (owner.xToTime (x + 1) * sr);
            if (s1 <= s0) s1 = s0 + 1;
            float mnL, mxL, mnR, mxR;
            if (! wf.rangeMinMax (s0, s1, mnL, mxL, mnR, mxR))
                continue;
            // Con nivelado, la onda se dibuja con la ganancia del tramo (como sonará)
            const float lg = owner.levelGainAt ? owner.levelGainAt (track.stemIndex, owner.xToTime (x)) : 1.0f;
            auto column = [&] (float mid, float mn, float mx)
            {
                float top = mid - juce::jlimit (-1.0f, 1.0f, mx * lg) * half;
                float bottom = mid - juce::jlimit (-1.0f, 1.0f, mn * lg) * half;
                if (bottom - top < 1.0f) { top -= 0.5f; bottom = top + 1.0f; }
                g.setColour (colour.withAlpha (0.75f));
                g.fillRect ((float) x, top, 1.0f, bottom - top);
            };
            column (midL, mnL, mxL);
            column (midR, mnR, mxR);
        }
        imageValid = true;
    }

    TimelineView& owner;
    LoadedTrack& track;
    int index;
    juce::Colour colour;
    juce::Label name;
    juce::TextButton mute, solo;
    juce::Image image;
    bool imageValid = false;
};

//==============================================================================
// Capa superior transparente: cabezal y región del loop. No recibe el mouse.
class TimelineView::Overlay : public juce::Component
{
public:
    explicit Overlay (TimelineView& o) : owner (o)
    {
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        if (owner.song == nullptr)
            return;
        g.reduceClipRegion (getLocalBounds().withTrimmedLeft (headerWidth));
        if (owner.loopEnd > owner.loopStart)
        {
            const int a = headerWidth + owner.timeToX (owner.loopStart), b = headerWidth + owner.timeToX (owner.loopEnd);
            g.setColour (ui::accent.withAlpha (0.12f));
            g.fillRect (a, 0, juce::jmax (1, b - a), getHeight());
            g.setColour (ui::accent.withAlpha (0.5f));
            g.fillRect (a, 0, 1, getHeight());
            g.fillRect (b, 0, 1, getHeight());
        }
        // Tramos del arreglo: bordes y huecos; durante Shift + arrastre, el tramo desplazado
        if (! owner.clips.empty())
        {
            double lastEnd = 0.0;
            for (auto& c : owner.clips)
            {
                if (c.start > lastEnd + 1.0e-6)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    g.fillRect (headerWidth + owner.timeToX (lastEnd), 0, juce::jmax (1, owner.timeToX (c.start) - owner.timeToX (lastEnd)), getHeight());
                }
                lastEnd = juce::jmax (lastEnd, c.end);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.fillRect (headerWidth + owner.timeToX (c.start), 0, 1, getHeight());
                g.fillRect (headerWidth + owner.timeToX (c.end), 0, 1, getHeight());
            }
            if (owner.clipDragging)
                for (auto& c : owner.clips)
                    if (owner.clipDragFrom >= c.start && owner.clipDragFrom < c.end)
                    {
                        const int a = headerWidth + owner.timeToX (c.start + owner.clipDragDelta), b = headerWidth + owner.timeToX (c.end + owner.clipDragDelta);
                        g.setColour (ui::accent.withAlpha (0.2f));
                        g.fillRect (a, 0, juce::jmax (1, b - a), getHeight());
                        g.setColour (ui::accent);
                        g.fillRect (a, 0, 1, getHeight());
                        g.fillRect (b, 0, 1, getHeight());
                        g.setFont (ui::font (12.0f, true));
                        g.setColour (juce::Colours::white);
                        g.drawText (juce::String::formatted ("%+.0f ms", owner.clipDragDelta * 1000.0), a + 4, 2, 120, 16, juce::Justification::centredLeft);
                        break;
                    }
        }
        const int px = headerWidth + owner.timeToX (owner.position);
        g.setColour (ui::playhead);
        g.fillRect (px, 0, 2, getHeight());
        juce::Path head;
        head.addTriangle ((float) px - 5.0f, 0.0f, (float) px + 7.0f, 0.0f, (float) px + 1.0f, 8.0f);
        g.fillPath (head);
    }

private:
    TimelineView& owner;
};

//==============================================================================
TimelineView::TimelineView()
{
    ruler = std::make_unique<Ruler> (*this);
    overlay = std::make_unique<Overlay> (*this);
    addAndMakeVisible (*ruler);
    laneView.setViewedComponent (&laneHolder, false);
    laneView.setScrollBarsShown (true, false, false, false);
    laneView.setScrollBarThickness (gutter);
    addAndMakeVisible (laneView);
    addAndMakeVisible (*overlay);
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
    addAndMakeVisible (scrollBar);
    emptyLabel.setText (tr ("Sin canción cargada"), juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    emptyLabel.setFont (ui::font (16.0f));
    addAndMakeVisible (emptyLabel);
    setWantsKeyboardFocus (false);
    ui::disableFocus (*this);
}

TimelineView::~TimelineView()
{
    scrollBar.removeListener (this);
}

double TimelineView::lengthSeconds() const
{
    return song != nullptr ? (double) song->length / sampleRate : 0.0;
}

double TimelineView::minPixelsPerSecond() const
{
    const double len = lengthSeconds();
    return len > 0.0 ? (double) contentWidth() / len : 1.0;
}

void TimelineView::setSong (std::shared_ptr<LoadedSong> s, double sr)
{
    lanes.clear();   // antes de soltar la canción: las pistas referencian sus LoadedTrack
    song = std::move (s);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    if (song != nullptr)
    {
        int i = 0;
        for (auto& t : song->tracks)
            laneHolder.addAndMakeVisible (lanes.add (new Lane (*this, *t, i++)));
    }
    overlay->toFront (false);
    emptyLabel.setVisible (song == nullptr);
    position = 0.0;
    loopStart = loopEnd = 0.0;
    resized();
    zoomToFit();
}

void TimelineView::setLoadingText (const juce::String& text)
{
    emptyLabel.setText (text, juce::dontSendNotification);
}

void TimelineView::setMarkers (const std::vector<SongMarker>& m, double newBpm, double newOffset)
{
    markers = m;
    bpm = newBpm;
    clickOffset = newOffset;
    ruler->repaint();
}

void TimelineView::setAnalysis (const Analysis& a)
{
    analysis = a;
    ruler->repaint();
}

void TimelineView::setTempoBands (const std::vector<TempoBand>& bands)
{
    tempoBands = bands;
    ruler->repaint();
}

void TimelineView::refreshWaveforms()
{
    invalidateLanes();
}

void TimelineView::setNotes (const std::vector<NoteView>& n)
{
    notes = n;
    ruler->repaint();
}

void TimelineView::setClips (const std::vector<ClipView>& c)
{
    clips = c;
    overlay->repaint();
}

void TimelineView::repaintOverlay()
{
    overlay->repaint();
}

void TimelineView::setLevelingEnabled (bool enabled)
{
    if (levelingEnabled != enabled)
    {
        levelingEnabled = enabled;
        ruler->repaint();
    }
}

void TimelineView::setPosition (double seconds)
{
    const int oldX = headerWidth + timeToX (position);
    position = seconds;
    if (song == nullptr)
        return;
    const int x = timeToX (position);
    if (playing && (x < 0 || x > contentWidth() - 30))
    {
        setView (position - visibleSeconds() * 0.1, pixelsPerSecond);   // seguir al cabezal
        return;
    }
    const int newX = headerWidth + x;
    overlay->repaint (oldX - 8, 0, 16, overlay->getHeight());
    overlay->repaint (newX - 8, 0, 16, overlay->getHeight());
    if (! analysis.chords.empty() && analysis.chordAt (seconds) != lastChord)
    {
        lastChord = analysis.chordAt (seconds);
        ruler->repaint (headerWidth, 46, ruler->getWidth() - headerWidth, ruler->getHeight() - 46);
    }
}

void TimelineView::setPlaying (bool p)
{
    playing = p;
}

void TimelineView::setLoop (double start, double end)
{
    if (std::abs (start - loopStart) < 1.0e-6 && std::abs (end - loopEnd) < 1.0e-6)
        return;
    loopStart = start;
    loopEnd = end;
    overlay->repaint();
}

void TimelineView::refreshTrackStates()
{
    for (auto* lane : lanes)
        lane->refreshState();
}

void TimelineView::zoomToFit()
{
    setView (0.0, minPixelsPerSecond());
}

void TimelineView::setView (double start, double pps)
{
    const double len = lengthSeconds();
    pps = juce::jlimit (minPixelsPerSecond(), 4000.0, pps);
    const double visible = contentWidth() / pps;
    start = juce::jlimit (0.0, juce::jmax (0.0, len - visible), start);
    if (std::abs (start - viewStart) < 1.0e-9 && std::abs (pps - pixelsPerSecond) < 1.0e-9)
        return;
    viewStart = start;
    pixelsPerSecond = pps;
    invalidateLanes();
    ruler->repaint();
    overlay->repaint();
    updateScrollBar();
}

void TimelineView::invalidateLanes()
{
    for (auto* lane : lanes)
        lane->invalidate();
}

void TimelineView::updateScrollBar()
{
    scrollBar.setRangeLimits (0.0, juce::jmax (0.001, lengthSeconds()), juce::dontSendNotification);
    scrollBar.setCurrentRange (viewStart, visibleSeconds(), juce::dontSendNotification);
}

void TimelineView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    setView (newRangeStart, pixelsPerSecond);
}

void TimelineView::seekAt (int contentX, bool scrubbing)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (scrubbing && now - lastScrubMs < 40)
        return;   // al arrastrar, como mucho 25 saltos por segundo (cada salto lleva su fundido)
    lastScrubMs = now;
    const double t = juce::jlimit (0.0, lengthSeconds(), xToTime (contentX));
    if (onSeek)
        onSeek (t);
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (song == nullptr)
        return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const int cx = juce::jmax (0, e.x - headerWidth);
        const double anchor = xToTime (cx);
        const double factor = std::pow (1.5, juce::jlimit (-1.0, 1.0, w.deltaY * 6.0));
        const double pps = juce::jlimit (minPixelsPerSecond(), 4000.0, pixelsPerSecond * factor);
        setView (anchor - cx / pps, pps);
    }
    else
    {
        const double delta = (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY);
        setView (viewStart - delta * visibleSeconds() * 0.6, pixelsPerSecond);
    }
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);
}

void TimelineView::resized()
{
    auto r = getLocalBounds();
    const int laneWidth = headerWidth + contentWidth();
    ruler->setBounds (r.removeFromTop (rulerHeight).withWidth (laneWidth));
    auto scroll = r.removeFromBottom (scrollHeight);
    scrollBar.setBounds (scroll.withTrimmedLeft (headerWidth).withWidth (contentWidth()));
    emptyLabel.setBounds (r);
    laneView.setBounds (r);

    const int n = lanes.size();
    const int laneH = n > 0 ? juce::jlimit (minLaneHeight, maxLaneHeight, r.getHeight() / n) : 0;
    laneHolder.setSize (laneWidth, juce::jmax (1, n * laneH));
    for (int i = 0; i < n; ++i)
        lanes[i]->setBounds (0, i * laneH, laneWidth, laneH);
    overlay->setBounds (0, 0, laneWidth, getHeight() - scrollHeight);
    overlay->toFront (false);

    // Con otro ancho cambia el zoom mínimo: mantener la vista válida
    setView (viewStart, pixelsPerSecond);
    invalidateLanes();
    updateScrollBar();
}

#include "TimelineView.h"
#include "MidiTracks.h"
#include "UiUtils.h"
#include <algorithm>
#include <array>
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
        trig.setButtonText ("T");
        trig.setClickingTogglesState (true);
        trig.setColour (juce::TextButton::buttonOnColourId, juce::Colours::orange.darker (0.2f));
        trig.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        trig.onClick = [this] { if (owner.onTrigger) owner.onTrigger (index, trig.getToggleState()); };
        addAndMakeVisible (trig);
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

    void setTriggerState (bool on)
    {
        if (trig.getToggleState() != on)
            trig.setToggleState (on, juce::dontSendNotification);
    }

    void setMarks (std::vector<TriggerMark> m)
    {
        marks = std::move (m);
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
        buttons.removeFromLeft (4);
        trig.setBounds (buttons.removeFromLeft (26));
        name.setBounds (h);
        invalidate();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth)
        {
            if (e.mods.isPopupMenu() && owner.onLaneHeaderMenu)
                owner.onLaneHeaderMenu (index);   // menú del trigger de la pista
            return;
        }
        if (owner.song == nullptr)
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

        // Golpes detectados (triggers): marcas naranjas al pie del carril, tan altas como fuerte fue el golpe
        if (! marks.empty())
        {
            g.setColour (juce::Colours::orange);
            const double t0 = owner.viewStart, t1 = owner.xToTime (getWidth() - headerWidth);
            const int maxH = juce::jmax (6, getHeight() / 3);
            for (auto& m : marks)
            {
                if (m.seconds < t0) continue;
                if (m.seconds > t1) break;
                const int x = headerWidth + owner.timeToX (m.seconds);
                const int h = 3 + (int) ((float) (maxH - 3) * juce::jlimit (0.0f, 1.0f, m.velocity));
                g.fillRect (x, getHeight() - 1 - h, 2, h);
            }
        }
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
    juce::TextButton mute, solo, trig;
    juce::Image image;
    bool imageValid = false;
    std::vector<TriggerMark> marks;
};

//==============================================================================
// Pista MIDI: los golpes congelados de un trigger como notas editables, una fila por sonido, bajo el
// carril de su pista de origen. Cabecera: nombre (clic derecho = menú de la pista) con M, y una fila por
// sonido con su nombre y lo que dispara (clic = elegir sus golpes, clic derecho = menú de la fila).
// Contenido: clic en un golpe = elegirlo (Ctrl o Shift: sumarlo o quitarlo), arrastre = mover los
// elegidos (tiempo y fila), Alt + arrastre vertical = velocidad, arrastre en un hueco = rectángulo de
// selección, clic en un hueco = soltar la selección y saltar, doble clic en un hueco = golpe nuevo.
// Se dibuja en una imagen en caché (setBufferedToImage): el cabezal repinta 30 veces por segundo su franja en la
// capa de encima y eso solo copia la imagen; la imagen se rehace únicamente con los repaint() de la propia pista
// (vista, datos, selección o gesto; los que no tocan la cabecera repintan solo el contenido).
class TimelineView::MidiLane : public juce::Component
{
public:
    static constexpr int titleHeight = 22, rowHeight = 16, minHeight = 44, maxHeight = 220, hitWidth = 5;

    static juce::Colour midiColour() { return juce::Colour (0xff9ccc65); }

    MidiLane (TimelineView& o, int i) : owner (o), index (i)
    {
        noteRows.fill (-1);
        mute.setButtonText ("M");
        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
        mute.onClick = [this] { if (owner.onMidiMute) owner.onMidiMute (index, mute.getToggleState()); };
        addAndMakeVisible (mute);
        ui::disableFocus (*this);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setOpaque (true);            // paint() cubre todo su rectángulo
        setBufferedToImage (true);
    }

    int getIndex() const                           { return index; }
    int getAfterTrack() const                      { return data.afterTrack; }
    int preferredHeight() const                    { return juce::jlimit (minHeight, maxHeight, titleHeight + rowHeight * (int) data.pads.size()); }
    const std::vector<int>& getSelection() const   { return selection; }
    bool hasSelection() const                      { return ! selection.empty(); }

    // Datos nuevos: conserva los golpes elegidos que sigan existiendo (ver remapSelection; newRender = la
    // canción se volvió a renderizar y los tiempos pudieron cambiar todos a la vez). Devuelve true si cambió el
    // alto o el lugar de la pista (hay que volver a ubicar los carriles).
    bool setData (const MidiLaneView& v, bool newRender)
    {
        const bool relayout = v.pads.size() != data.pads.size() || v.afterTrack != data.afterTrack;
        const bool hitsChanged = ! std::equal (v.hits.begin(), v.hits.end(), data.hits.begin(), data.hits.end(),
                                               [] (const MidiLaneView::Hit& a, const MidiLaneView::Hit& b)
                                               { return std::abs (a.seconds - b.seconds) <= 1.0e-9 && a.note == b.note && a.velocity == b.velocity; });
        const bool vSorted = std::is_sorted (v.hits.begin(), v.hits.end(),
                                             [] (const MidiLaneView::Hit& a, const MidiLaneView::Hit& b) { return a.seconds < b.seconds; });
        if (hitsChanged)
        {
            selection = remapSelection (v, vSorted, newRender);
            if (gesture != Gesture::none)
            {
                // El gesto en curso guarda índices que ya no valen: se abandona sin avisar
                gesture = Gesture::none;
                anchor = -1;
                moveDelta = 0.0;
                rowDelta = velocityDelta = 0;
                bandMoved = false;
            }
        }
        data = v;
        sorted = vSorted;
        noteRows.fill (-1);
        for (int r = (int) data.pads.size(); --r >= 0;)   // de abajo hacia arriba: si dos filas comparten nota, manda la primera
            if (juce::isPositiveAndBelow (data.pads[(size_t) r].note, 128))
                noteRows[(size_t) data.pads[(size_t) r].note] = r;
        mute.setToggleState (data.muted, juce::dontSendNotification);
        repaint();
        return relayout;
    }

    void repaintContent()   { repaint (headerWidth, 0, juce::jmax (0, getWidth() - headerWidth), getHeight()); }

    // Reemplaza la selección (ordenada, sin repetidos ni índices inválidos); si queda algo elegido, las
    // demás pistas MIDI sueltan la suya
    void setSelection (std::vector<int> s)
    {
        const int count = (int) data.hits.size();
        s.erase (std::remove_if (s.begin(), s.end(), [count] (int i) { return i < 0 || i >= count; }), s.end());
        std::sort (s.begin(), s.end());
        s.erase (std::unique (s.begin(), s.end()), s.end());
        if (s != selection)
        {
            selection = std::move (s);
            repaintContent();
        }
        if (! selection.empty())
            owner.clearMidiSelectionExcept (this);
    }

    void resized() override
    {
        mute.setBounds (headerWidth - 6 - 26, 2, 26, titleHeight - 4);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool onHit = e.x >= headerWidth && hitAt (e.position) >= 0;
        const bool onPlay = e.y >= titleHeight && rowAtY (e.y) >= 0 && isOnPlayButton (e.x);
        setMouseCursor (onHit ? (e.mods.isAltDown() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor)
                              : onPlay ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        gesture = Gesture::none;
        bandMoved = false;
        if (e.x < headerWidth)
        {
            headerMouseDown (e);
            return;
        }
        if (owner.song == nullptr)
            return;
        const int track = index;
        const int row = rowAtY (e.y);
        const int hit = hitAt (e.position);
        const bool toggle = e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isShiftDown();

        if (e.mods.isPopupMenu())
        {
            // Sobre un golpe que no estaba elegido, primero queda elegido solo él
            if (hit >= 0 && ! isSelected (hit))
                setSelection ({ hit });
            const auto hits = selection;
            const double t = timeAt (e.x);
            if (owner.onMidiMenu)
                owner.onMidiMenu (track, hits, juce::jmax (-1, row), t);
            return;
        }

        if (hit < 0)
        {
            if (e.getNumberOfClicks() == 2 && row >= 0)
            {
                // Doble clic en un hueco: golpe nuevo en esa fila (el primer clic ya saltó el cabezal)
                const int note = data.pads[(size_t) row].note;
                double t = timeAt (e.x);
                if (owner.snapMidiTime)
                    t = juce::jlimit (0.0, owner.lengthSeconds(), owner.snapMidiTime (track, t, note));
                if (owner.onMidiAdd)
                    owner.onMidiAdd (track, t, note);
                return;
            }
            // Hueco: rectángulo de selección si se arrastra; si no, al soltar se suelta la selección y se salta
            gesture = Gesture::band;
            bandAdd = toggle;
            bandBase = toggle ? selection : std::vector<int>();
            band = {};
            return;
        }

        anchor = hit;
        // Dónde se tomó el golpe: el arrastre sigue al tiempo bajo el mouse aunque la vista se desplace o cambie el zoom
        grabOffset = data.hits[(size_t) hit].seconds - owner.xToTime (e.x - headerWidth);
        grabY = e.y;
        if (e.mods.isAltDown())
        {
            if (! isSelected (hit))
                setSelection ({ hit });
            gesture = Gesture::velocity;
            velocityDelta = 0;
            return;
        }
        if (toggle)
        {
            auto s = selection;
            if (isSelected (hit))
                s.erase (std::find (s.begin(), s.end(), hit));
            else
                s.push_back (hit);
            setSelection (std::move (s));
            clickSelectsOnly = false;
            gesture = isSelected (hit) ? Gesture::pendingMove : Gesture::none;
            return;
        }
        // Clic sobre un golpe ya elegido: arrastrarlo mueve todos los elegidos; soltarlo sin mover deja solo ese
        clickSelectsOnly = isSelected (hit);
        if (! isSelected (hit))
            setSelection ({ hit });
        gesture = Gesture::pendingMove;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (gesture == Gesture::pendingMove)
        {
            if (e.getDistanceFromDragStart() < 4)
                return;
            beginMove();
        }
        if (gesture == Gesture::moving)
            updateMove (e);
        else if (gesture == Gesture::velocity)
        {
            const int d = juce::jlimit (-126, 126, e.getMouseDownY() - e.y);   // arriba sube, 1 por píxel
            if (d != velocityDelta)
            {
                velocityDelta = d;
                repaintContent();
            }
        }
        else if (gesture == Gesture::band)
            updateBand (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // Se deja todo en orden antes de avisar: el dueño puede reconstruir las pistas MIDI desde el callback
        const auto done = gesture;
        gesture = Gesture::none;
        const int track = index;
        if (done == Gesture::moving)
        {
            // El golpe que se arrastró va primero: el dueño convierte el desplazamiento con su tiempo (el que se ajustó)
            auto hits = selection;
            if (const auto it = std::find (hits.begin(), hits.end(), anchor); it != hits.end())
                std::rotate (hits.begin(), it, it + 1);
            const double delta = moveDelta;
            const int rd = rowDelta;
            moveDelta = 0.0;
            rowDelta = 0;
            repaintContent();
            if ((std::abs (delta) > 1.0e-6 || rd != 0) && ! hits.empty() && owner.onMidiMove)
                owner.onMidiMove (track, hits, delta, rd);
        }
        else if (done == Gesture::pendingMove)
        {
            if (clickSelectsOnly)
                setSelection ({ anchor });
        }
        else if (done == Gesture::velocity)
        {
            const auto hits = selection;
            const int delta = velocityDelta;
            velocityDelta = 0;
            repaintContent();
            if (delta != 0 && ! hits.empty() && owner.onMidiVelocity)
                owner.onMidiVelocity (track, hits, delta);
        }
        else if (done == Gesture::band)
        {
            if (bandMoved)
            {
                bandMoved = false;
                repaintContent();
            }
            else
            {
                // Clic sin arrastrar en un hueco: soltar la selección (salvo con Ctrl o Shift) y saltar ahí
                if (! bandAdd)
                    owner.clearMidiSelectionExcept (nullptr);
                owner.seekAt (e.getMouseDownX() - headerWidth, false);
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth(), h = getHeight();
        const int rows = (int) data.pads.size();
        const float rh = rowH();

        // Cabecera: título con M y una fila por sonido (solo si hay que redibujarla: mide texto)
        if (g.getClipBounds().getX() < headerWidth)
        {
            g.setColour (ui::panel);
            g.fillRect (0, 0, headerWidth, h);
            g.setColour (ui::panelAlt);
            g.fillRect (0, 0, headerWidth, titleHeight);
            g.setColour (juce::Colour (0xff22282f));
            for (int r = 1; r < rows; r += 2)
                g.fillRect (0.0f, rowTop (r), (float) headerWidth, rh);
            g.setColour (midiColour().withAlpha (data.muted ? 0.4f : 1.0f));
            g.fillRect (0, 0, 4, h);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRect (headerWidth - 1, 0, 1, h);
            g.setColour (juce::Colours::white.withAlpha (data.muted ? 0.45f : 0.95f));
            g.setFont (ui::font (12.0f, true));
            g.drawText (data.name, 10, 0, juce::jmax (0, mute.getX() - 14), titleHeight, juce::Justification::centredLeft, true);
            if (rh >= 6.0f)   // con decenas de filas no caben letras legibles
                for (int r = 0; r < rows; ++r)
                    paintPadLabel (g, r, rh);
        }

        // Contenido: franja del título, filas alternadas, rejilla, golpes y lo que se está arrastrando
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (headerWidth, 0, w - headerWidth, h);
            g.setColour (juce::Colour (0xff13171b));
            g.fillRect (headerWidth, 0, w - headerWidth, titleHeight);
            g.setColour (juce::Colour (0xff181c21));
            g.fillRect (headerWidth, titleHeight, w - headerWidth, h - titleHeight);
            g.setColour (juce::Colour (0xff1c2127));
            for (int r = 1; r < rows; r += 2)
                g.fillRect ((float) headerWidth, rowTop (r), (float) (w - headerWidth), rh);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (headerWidth, titleHeight - 1, w - headerWidth, 1);
            paintGrid (g);
            paintHits (g);

            g.setFont (ui::font (11.0f));
            g.setColour (juce::Colours::grey);
            if (rows == 0)
                g.drawText (tr ("Sin filas: clic derecho en el nombre de la pista para agregar un sonido"),
                            headerWidth + 8, titleHeight, w - headerWidth - 16, h - titleHeight, juce::Justification::centredLeft, true);
            else if (data.hits.empty())
                g.drawText (tr ("Sin golpes: doble clic en una fila para agregar uno"),
                            headerWidth + 8, 0, w - headerWidth - 16, titleHeight, juce::Justification::centredLeft, true);

            if (gesture == Gesture::band && bandMoved && ! band.isEmpty())
            {
                g.setColour (ui::accent.withAlpha (0.15f));
                g.fillRect (band);
                g.setColour (ui::accent.withAlpha (0.8f));
                g.drawRect (band, 1);
            }
            paintDragInfo (g);
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (0, h - 1, w, 1);
    }

private:
    enum class Gesture { none, pendingMove, moving, velocity, band };

    // --- Geometría: fila -1 = franja del título (golpes cuya nota no tiene fila) ---
    float rowH() const
    {
        const int rows = (int) data.pads.size();
        return rows > 0 ? juce::jlimit (1.0f, (float) rowHeight, (float) (getHeight() - titleHeight) / (float) rows) : (float) rowHeight;
    }

    float rowTop (int row) const   { return row < 0 ? 4.0f : (float) titleHeight + (float) row * rowH(); }
    // Botón "Escuchar" a la izquierda del nombre de cada fila (solo si las filas tienen alto para él)
    static constexpr int playLeft = 7, playRight = 18;
    bool hasPlayButton() const              { return rowH() >= 11.0f; }
    bool isOnPlayButton (int x) const       { return hasPlayButton() && x >= playLeft - 2 && x < playRight + 2; }
    float rowSpan (int row) const  { return row < 0 ? (float) (titleHeight - 8) : rowH(); }

    int noteRow (int note) const   { return juce::isPositiveAndBelow (note, 128) ? noteRows[(size_t) note] : -1; }

    // Fila bajo esa altura: -1 = franja del título, -2 = debajo de las filas
    int rowAtY (int y) const
    {
        if (y < titleHeight)
            return -1;
        const int row = (int) std::floor ((float) (y - titleHeight) / rowH());
        return row < (int) data.pads.size() ? row : -2;
    }

    double timeAt (int x) const   { return juce::jlimit (0.0, owner.lengthSeconds(), owner.xToTime (x - headerWidth)); }

    juce::Rectangle<float> hitBounds (double seconds, int row) const
    {
        const float x = (float) (headerWidth + owner.timeToX (seconds));
        return { x, rowTop (row) + 1.0f, (float) hitWidth, juce::jmax (2.0f, rowSpan (row) - 2.0f) };
    }

    int movedRow (int row) const   { return row < 0 ? row : juce::jlimit (0, (int) data.pads.size() - 1, row + rowDelta); }

    bool isSelected (int hit) const   { return std::binary_search (selection.begin(), selection.end(), hit); }

    // La selección en los datos nuevos. Cada golpe elegido se busca por su instante y su nota: una edición o un
    // Ctrl+Z pueden reordenar los índices, y un golpe que ya no está (se movió o se deshizo) suelta la
    // selección. Tras un render nuevo (tempo, arreglo) todos los tiempos cambian a la vez: si los golpes son
    // los mismos (misma cantidad, notas y fuerzas en el mismo orden), vale el índice.
    std::vector<int> remapSelection (const MidiLaneView& v, bool vSorted, bool newRender) const
    {
        std::vector<int> out;
        if (selection.empty())
            return out;
        constexpr double eps = 1.0e-9;
        const auto& hits = v.hits;
        const int count = (int) hits.size();
        std::vector<bool> taken ((size_t) count, false);
        bool all = true;
        for (int i : selection)
        {
            if (! juce::isPositiveAndBelow (i, (int) data.hits.size()))
            {
                all = false;
                break;
            }
            const auto& old = data.hits[(size_t) i];
            int j = 0;
            if (vSorted)
                j = (int) (std::lower_bound (hits.begin(), hits.end(), old.seconds - eps,
                                             [] (const MidiLaneView::Hit& h, double t) { return h.seconds < t; }) - hits.begin());
            int match = -1;
            for (; j < count; ++j)
            {
                const auto& h = hits[(size_t) j];
                if (vSorted && h.seconds > old.seconds + eps)
                    break;
                if (! taken[(size_t) j] && h.note == old.note && std::abs (h.seconds - old.seconds) <= eps)
                {
                    match = j;
                    break;
                }
            }
            if (match < 0)
            {
                all = false;
                break;
            }
            taken[(size_t) match] = true;
            out.push_back (match);
        }
        if (all)
        {
            std::sort (out.begin(), out.end());
            return out;
        }
        const bool sameHits = std::equal (hits.begin(), hits.end(), data.hits.begin(), data.hits.end(),
                                          [] (const MidiLaneView::Hit& a, const MidiLaneView::Hit& b)
                                          { return a.note == b.note && a.velocity == b.velocity; });
        if (newRender && sameHits)
        {
            out.clear();
            for (int i : selection)
                if (juce::isPositiveAndBelow (i, count))
                    out.push_back (i);
            return out;
        }
        return {};
    }

    // Índices [primero, último) de los golpes con tiempo en [from, to] (todos si no vinieron ordenados)
    std::pair<int, int> hitRange (double from, double to) const
    {
        const auto& hits = data.hits;
        if (! sorted)
            return { 0, (int) hits.size() };
        const auto lo = std::lower_bound (hits.begin(), hits.end(), from,
                                          [] (const MidiLaneView::Hit& hit, double t) { return hit.seconds < t; });
        const auto hi = std::upper_bound (lo, hits.end(), to,
                                          [] (double t, const MidiLaneView::Hit& hit) { return t < hit.seconds; });
        return { (int) (lo - hits.begin()), (int) (hi - hits.begin()) };
    }

    // Golpe bajo el mouse (con 2 px de tolerancia a cada lado), o -1; de varios, el más cercano
    int hitAt (juce::Point<float> p) const
    {
        if (p.x < (float) headerWidth)
            return -1;
        const double pps = owner.pixelsPerSecond;
        const double t = owner.xToTime ((int) p.x - headerWidth);
        const auto range = hitRange (t - (hitWidth + 3) / pps, t + 3.0 / pps);
        int best = -1;
        float bestDistance = 1.0e9f;
        for (int i = range.first; i < range.second; ++i)
        {
            const auto& hit = data.hits[(size_t) i];
            const auto r = hitBounds (hit.seconds, noteRow (hit.note)).expanded (2.0f, 0.0f);
            if (! r.contains (p))
                continue;
            const float d = std::abs (p.x - r.getCentreX());
            if (d < bestDistance || (d <= bestDistance && isSelected (i)))
            {
                best = i;
                bestDistance = d;
            }
        }
        return best;
    }

    juce::String padLabel (int row) const
    {
        if (! juce::isPositiveAndBelow (row, (int) data.pads.size()))
            return {};
        const auto& pad = data.pads[(size_t) row];
        return pad.name.isNotEmpty() ? pad.name : miditrack::drumNoteName (pad.note);
    }

    // Opacidad de los golpes de una fila: pista o fila silenciadas, atenuadas
    float rowAlpha (int row) const
    {
        float alpha = data.muted ? 0.45f : 1.0f;
        if (row >= 0 && data.pads[(size_t) row].muted)
            alpha *= 0.35f;
        return alpha;
    }

    // --- Gestos ---
    void headerMouseDown (const juce::MouseEvent& e)
    {
        const int track = index;
        if (e.y < titleHeight)
        {
            if (e.mods.isPopupMenu() && owner.onMidiHeaderMenu)
                owner.onMidiHeaderMenu (track);
            return;
        }
        const int row = rowAtY (e.y);
        if (row < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            if (owner.onMidiPadMenu)
                owner.onMidiPadMenu (track, row);
            return;
        }
        // Clic en el nombre de una fila: elegir todos sus golpes (con Ctrl o Shift, sumarlos)
        if (isOnPlayButton (e.x))
        {
            if (owner.onMidiAudition)
                owner.onMidiAudition (track, row);
            return;
        }
        const bool add = e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isShiftDown();
        std::vector<int> s = add ? selection : std::vector<int>();
        for (int i = 0; i < (int) data.hits.size(); ++i)
            if (noteRow (data.hits[(size_t) i].note) == row)
                s.push_back (i);
        setSelection (std::move (s));
    }

    void beginMove()
    {
        selMinTime = 1.0e12;
        selMaxTime = 0.0;
        selMinRow = (int) data.pads.size();
        selMaxRow = -1;
        for (int i : selection)
        {
            const auto& hit = data.hits[(size_t) i];
            selMinTime = juce::jmin (selMinTime, hit.seconds);
            selMaxTime = juce::jmax (selMaxTime, hit.seconds);
            const int row = noteRow (hit.note);
            if (row >= 0)
            {
                selMinRow = juce::jmin (selMinRow, row);
                selMaxRow = juce::jmax (selMaxRow, row);
            }
        }
        moveDelta = 0.0;
        rowDelta = 0;
        gesture = Gesture::moving;
    }

    void updateMove (const juce::MouseEvent& e)
    {
        if (! juce::isPositiveAndBelow (anchor, (int) data.hits.size()) || selection.empty())
        {
            gesture = Gesture::none;
            repaintContent();
            return;
        }
        const auto& hit = data.hits[(size_t) anchor];
        const int rows = (int) data.pads.size();
        const float rh = rowH();

        // Filas: una por cada alto de fila, sin sacar a ningún golpe elegido de las filas que existen
        int rd = 0;
        if (rows > 0 && selMaxRow >= 0 && selMaxRow < rows)
        {
            rd = (int) std::floor ((float) (e.y - titleHeight) / rh) - (int) std::floor ((float) (grabY - titleHeight) / rh);
            rd = juce::jlimit (-selMinRow, rows - 1 - selMaxRow, rd);
        }

        // Tiempo: el golpe arrastrado va al tiempo bajo el mouse, con el ajuste del dueño; los demás lo siguen
        // (ninguno antes de 0 ni después del final). Con menos de 4 px de lado es solo un cambio de fila: el
        // tiempo no se toca (el ajuste lo correría aunque el mouse no se haya movido de lado).
        double delta = 0.0;
        const double raw = owner.xToTime (e.x - headerWidth) + grabOffset;
        if (std::abs (raw - hit.seconds) * owner.pixelsPerSecond >= 4.0)
        {
            const int anchorRow = noteRow (hit.note);
            const int targetNote = anchorRow >= 0 ? data.pads[(size_t) juce::jlimit (0, rows - 1, anchorRow + rd)].note : hit.note;
            const double target = owner.snapMidiTime ? owner.snapMidiTime (index, raw, targetNote) : raw;
            delta = juce::jlimit (juce::jmin (0.0, -selMinTime), juce::jmax (0.0, owner.lengthSeconds() - selMaxTime), target - hit.seconds);
        }
        if (std::abs (delta - moveDelta) > 1.0e-9 || rd != rowDelta)
        {
            moveDelta = delta;
            rowDelta = rd;
            repaintContent();
        }
    }

    void updateBand (const juce::MouseEvent& e)
    {
        if (! bandMoved && e.getDistanceFromDragStart() < 4)
            return;
        if (! bandMoved && ! bandAdd)
            owner.clearMidiSelectionExcept (this);   // rectángulo nuevo: nada queda elegido en las demás pistas
        bandMoved = true;
        band = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (getLocalBounds().withTrimmedLeft (headerWidth));
        std::vector<int> inside = bandBase;
        const auto area = band.toFloat();
        const auto range = hitRange (owner.xToTime (band.getX() - headerWidth) - (hitWidth + 1) / owner.pixelsPerSecond,
                                     owner.xToTime (band.getRight() - headerWidth));
        for (int i = range.first; i < range.second; ++i)
        {
            const auto& hit = data.hits[(size_t) i];
            if (hitBounds (hit.seconds, noteRow (hit.note)).intersects (area))
                inside.push_back (i);
        }
        setSelection (std::move (inside));
        repaintContent();
    }

    // --- Dibujo ---
    void paintPadLabel (juce::Graphics& g, int row, float rh) const
    {
        const auto& pad = data.pads[(size_t) row];
        const float alpha = (pad.muted || data.muted) ? 0.35f : 0.9f;
        const int y = (int) rowTop (row), height = juce::jmax (1, (int) rh);
        const bool play = hasPlayButton();
        const int left = play ? playRight + 3 : 10, right = headerWidth - 5, width = right - left;
        if (play)
        {
            // Botón "Escuchar": un triángulo (sin símbolos Unicode)
            const float cy = (float) y + rh * 0.5f;
            juce::Path triangle;
            triangle.addTriangle ((float) playLeft + 1.0f, cy - 4.0f, (float) playLeft + 1.0f, cy + 4.0f, (float) playLeft + 8.0f, cy);
            g.setColour (midiColour().withAlpha (pad.sound.isEmpty() ? 0.3f : alpha));
            g.fillPath (triangle);
        }
        // Con muchas filas (más de 12 en el alto máximo) la letra se achica para no montarse con la vecina, y
        // bajo 11 px de fila el sonido no se muestra
        const float nameSize = juce::jlimit (6.0f, 11.0f, rh - 1.0f);
        const auto nameFont = ui::font (nameSize, true), soundFont = ui::font (juce::jmin (9.0f, nameSize - 1.0f));
        const bool showSound = rh >= 11.0f && pad.sound.isNotEmpty();

        // El sonido va a la derecha en letra chica; si el nombre no cabe, le cede espacio (hasta 36 px)
        juce::String name = padLabel (row);
        const int nameWidth = juce::GlyphArrangement::getStringWidthInt (nameFont, name);
        int soundWidth = ! showSound ? 0 : juce::jmin (60, juce::GlyphArrangement::getStringWidthInt (soundFont, pad.sound) + 2);
        if (nameWidth + 6 + soundWidth > width)
            soundWidth = juce::jmin (soundWidth, juce::jmax (36, width - 6 - nameWidth));
        const int nameArea = width - soundWidth - (soundWidth > 0 ? 6 : 0);

        // Si el nombre no cabe, el de la nota: el de batería General MIDI o, si tampoco cabe, C1, D#2...
        if (nameWidth > nameArea)
        {
            name = miditrack::drumNoteName (pad.note);
            if (juce::GlyphArrangement::getStringWidthInt (nameFont, name) > nameArea)
                name = juce::MidiMessage::getMidiNoteName (pad.note, true, true, 3);
        }
        g.setFont (nameFont);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawText (name, left, y, nameArea, height, juce::Justification::centredLeft, true);
        if (soundWidth > 0)
        {
            g.setFont (soundFont);
            g.setColour (juce::Colours::grey.withAlpha (alpha));
            g.drawText (pad.sound, right - soundWidth, y, soundWidth, height, juce::Justification::centredRight, true);
        }
    }

    // Tiempos detectados (el primero de cada compás más marcado) o la rejilla fija del click, como en la regla
    void paintGrid (juce::Graphics& g) const
    {
        const int h = getHeight();
        const double pps = owner.pixelsPerSecond;
        const double t0 = owner.viewStart, t1 = owner.xToTime (getWidth() - headerWidth);
        auto line = [&] (double t, float alpha)
        {
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.fillRect (headerWidth + owner.timeToX (t), 0, 1, h);
        };
        const auto& an = owner.analysis;
        if (! an.beats.empty())
        {
            int barNumber = 0;
            const double barPx = an.bpm > 0.0 ? (240.0 / an.bpm) * pps : 1.0e9;
            const int barStep = juce::jmax (1, (int) std::ceil (6.0 / juce::jmax (0.1, barPx)));
            const bool showBeats = an.bpm > 0.0 && (60.0 / an.bpm) * pps >= 8.0;
            for (auto& b : an.beats)
            {
                if (b.beatInBar == 1) ++barNumber;
                if (b.seconds < t0 - 0.05) continue;
                if (b.seconds > t1) break;
                if (b.beatInBar == 1)
                {
                    if ((barNumber - 1) % barStep == 0)
                        line (b.seconds, 0.14f);
                }
                else if (showBeats)
                    line (b.seconds, 0.05f);
            }
        }
        else if (owner.bpm > 0.0)
        {
            const double beat = 60.0 / owner.bpm, bar = beat * 4.0;
            const int barStep = juce::jmax (1, (int) std::ceil (6.0 / (bar * pps)));
            const bool showBeats = beat * pps >= 8.0;
            for (double b = std::floor ((t0 - owner.clickOffset) / bar); ; b += 1.0)
            {
                const double t = owner.clickOffset + b * bar;
                if (t > t1) break;
                if (t < 0.0) continue;
                const auto number = (juce::int64) b;
                if ((number % barStep + barStep) % barStep == 0)
                    line (t, 0.14f);
                if (showBeats)
                    for (int k = 1; k < 4; ++k)
                        line (t + k * beat, 0.05f);
            }
        }
    }

    // Un golpe: rectángulo con opacidad según la velocidad, rayita de altura proporcional y borde si está elegido
    static void drawHit (juce::Graphics& g, juce::Rectangle<float> r, int velocity, float alpha, bool selected, bool orphan)
    {
        const float v = (float) juce::jlimit (1, 127, velocity) / 127.0f;
        const auto base = orphan ? juce::Colours::grey : midiColour();
        g.setColour (base.withAlpha ((0.3f + 0.7f * v) * alpha));
        g.fillRect (r);
        const float lineHeight = juce::jmax (1.0f, r.getHeight() * v);
        g.setColour (base.brighter (0.7f).withAlpha (alpha));
        g.fillRect (r.getX(), r.getBottom() - lineHeight, 1.0f, lineHeight);
        if (selected)
        {
            g.setColour (ui::accent.withAlpha (juce::jmax (0.6f, alpha)));
            g.drawRect (r.expanded (1.0f, 0.0f), 1.0f);
        }
    }

    void paintHits (juce::Graphics& g) const
    {
        // Solo los golpes que tocan la zona que se redibuja (con su borde de elegido)
        const double pps = owner.pixelsPerSecond;
        const auto clip = g.getClipBounds();
        const auto range = hitRange (owner.xToTime (clip.getX() - headerWidth) - (hitWidth + 3) / pps,
                                     owner.xToTime (clip.getRight() - headerWidth) + 2.0 / pps);
        const bool moving = gesture == Gesture::moving, changingVelocity = gesture == Gesture::velocity;
        for (int i = range.first; i < range.second; ++i)
        {
            const auto& hit = data.hits[(size_t) i];
            const int row = noteRow (hit.note);
            const bool selected = isSelected (i);
            int velocity = hit.velocity;
            float alpha = rowAlpha (row);
            if (selected && moving)
                alpha *= 0.3f;   // su lugar de antes, mientras se arrastra
            if (selected && changingVelocity)
                velocity = juce::jlimit (1, 127, velocity + velocityDelta);
            drawHit (g, hitBounds (hit.seconds, row), velocity, alpha, selected && ! moving, row < 0);
        }
        // Vista previa del arrastre: los elegidos desplazados, semitransparentes
        if (moving)
            for (int i : selection)
            {
                const auto& hit = data.hits[(size_t) i];
                const int row = movedRow (noteRow (hit.note));
                drawHit (g, hitBounds (juce::jmax (0.0, hit.seconds + moveDelta), row), hit.velocity, 0.6f * rowAlpha (row), true, row < 0);
            }
    }

    // Durante un arrastre: desplazamiento (y fila de destino) o velocidad, junto al golpe arrastrado
    void paintDragInfo (juce::Graphics& g) const
    {
        if (! juce::isPositiveAndBelow (anchor, (int) data.hits.size()))
            return;
        const auto& hit = data.hits[(size_t) anchor];
        double t = hit.seconds;
        juce::String text;
        if (gesture == Gesture::moving)
        {
            t = juce::jmax (0.0, t + moveDelta);
            text = juce::String::formatted ("%+.0f ms", moveDelta * 1000.0);
            const int row = noteRow (hit.note);
            if (rowDelta != 0 && row >= 0)
                text += "  -> " + padLabel (movedRow (row));
        }
        else if (gesture == Gesture::velocity)
            text = selection.size() == 1 ? "Velocidad " + juce::String (juce::jlimit (1, 127, hit.velocity + velocityDelta))
                                         : juce::String::formatted ("Velocidad %+d", velocityDelta);
        else
            return;
        g.setFont (ui::font (11.0f, true));
        g.setColour (juce::Colours::white);
        g.drawText (text, headerWidth + owner.timeToX (t) + hitWidth + 4, 0, 240, titleHeight, juce::Justification::centredLeft, false);
    }

    TimelineView& owner;
    int index;
    MidiLaneView data;
    bool sorted = true;
    std::array<int, 128> noteRows {};   // fila de cada nota MIDI (-1 = ninguna)
    juce::TextButton mute;
    std::vector<int> selection;         // índices de golpes elegidos, ordenados

    Gesture gesture = Gesture::none;
    int anchor = -1;                    // golpe bajo el mouse al empezar el gesto
    bool clickSelectsOnly = false;
    double moveDelta = 0.0, selMinTime = 0.0, selMaxTime = 0.0;
    double grabOffset = 0.0;            // tiempo del golpe tomado menos el tiempo bajo el mouse al tomarlo
    int grabY = 0;                      // altura del mouse al tomarlo (fila de partida)
    int rowDelta = 0, selMinRow = 0, selMaxRow = -1;
    int velocityDelta = 0;
    bool bandAdd = false, bandMoved = false;
    std::vector<int> bandBase;
    juce::Rectangle<int> band;
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
    if (song == nullptr)
        clearMidiSelectionExcept (nullptr);   // otra canción: los índices elegidos ya no significan nada
    midiNewRender = true;                     // las próximas pistas MIDI pueden traer todos los tiempos cambiados
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
    const bool gridChanged = std::abs (newBpm - bpm) > 1.0e-9 || std::abs (newOffset - clickOffset) > 1.0e-9;
    markers = m;
    bpm = newBpm;
    clickOffset = newOffset;
    ruler->repaint();
    if (gridChanged && analysis.beats.empty())
        for (auto* lane : midiLanes)
            lane->repaintContent();   // rejilla fija de fondo
}

void TimelineView::setAnalysis (const Analysis& a)
{
    analysis = a;
    ruler->repaint();
    for (auto* lane : midiLanes)
        lane->repaintContent();   // tiempos detectados de fondo
}

void TimelineView::setMidiLanes (const std::vector<MidiLaneView>& views)
{
    const bool newRender = midiNewRender;
    midiNewRender = false;
    bool relayout = false;
    if ((int) views.size() != midiLanes.size())
    {
        // Otra cantidad de pistas: se arman de nuevo (y se pierde la selección)
        midiLanes.clear();
        for (size_t i = 0; i < views.size(); ++i)
        {
            auto* lane = midiLanes.add (new MidiLane (*this, (int) i));
            lane->setData (views[i], false);
            laneHolder.addAndMakeVisible (lane);
        }
        relayout = true;
    }
    else
    {
        for (size_t i = 0; i < views.size(); ++i)
            relayout = midiLanes[(int) i]->setData (views[i], newRender) || relayout;
    }
    if (relayout)
        layoutLanes();
}

void TimelineView::selectMidiHits (int midiTrack, const std::vector<int>& hits)
{
    if (juce::isPositiveAndBelow (midiTrack, midiLanes.size()))
        midiLanes[midiTrack]->setSelection (hits);
}

std::vector<int> TimelineView::getSelectedMidiHits (int midiTrack) const
{
    if (! juce::isPositiveAndBelow (midiTrack, midiLanes.size()))
        return {};
    return midiLanes[midiTrack]->getSelection();
}

bool TimelineView::deleteSelectedMidiHits()
{
    for (auto* lane : midiLanes)
        if (lane->isShowing() && lane->hasSelection())   // nunca golpes que no se ven (modo en vivo, sin canción)
        {
            const int track = lane->getIndex();
            const auto hits = lane->getSelection();
            lane->setSelection ({});   // los índices dejan de valer tras borrar
            if (onMidiDelete)
                onMidiDelete (track, hits);
            return true;
        }
    return false;
}

void TimelineView::clearMidiSelectionExcept (const MidiLane* keep)
{
    for (auto* lane : midiLanes)
        if (lane != keep && lane->hasSelection())
            lane->setSelection ({});
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
    int x = timeToX (position);
    if (playing && (x < 0 || x > contentWidth() - 30))
    {
        const double before = viewStart;
        setView (position - visibleSeconds() * 0.1, pixelsPerSecond);   // seguir al cabezal
        if (std::abs (viewStart - before) > 1.0e-9)
            return;   // la vista se movió: ya se repintó todo
        // La vista ya muestra el final de la canción y no puede avanzar: el cabezal sigue moviéndose en ella
        x = timeToX (position);
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

void TimelineView::setTriggerStates (const std::vector<bool>& perTrack)
{
    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->setTriggerState (i < (int) perTrack.size() && perTrack[(size_t) i]);
}

void TimelineView::setTriggerMarks (const std::vector<std::vector<TriggerMark>>& perTrack)
{
    for (int i = 0; i < lanes.size(); ++i)
        lanes[i]->setMarks (i < (int) perTrack.size() ? perTrack[(size_t) i] : std::vector<TriggerMark>());
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
    for (auto* lane : midiLanes)
        lane->repaintContent();   // la cabecera no depende de la vista
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
    layoutLanes();
    overlay->setBounds (0, 0, laneWidth, getHeight() - scrollHeight);
    overlay->toFront (false);

    // Con otro ancho cambia el zoom mínimo: mantener la vista válida
    setView (viewStart, pixelsPerSecond);
    invalidateLanes();
    updateScrollBar();
}

// Carriles de audio en orden y, bajo cada uno, sus pistas MIDI (las que no tienen carril, al final). Las
// pistas MIDI tienen su alto propio (título + una fila por sonido); los carriles de audio se reparten el resto.
void TimelineView::layoutLanes()
{
    const int laneWidth = headerWidth + contentWidth();
    const bool showMidi = song != nullptr;
    const int n = lanes.size();
    int midiHeight = 0;
    for (auto* lane : midiLanes)
    {
        lane->setVisible (showMidi);
        if (showMidi)
            midiHeight += lane->preferredHeight();
    }
    const int laneH = n > 0 ? juce::jlimit (minLaneHeight, maxLaneHeight, (laneView.getHeight() - midiHeight) / n) : 0;

    int y = 0;
    auto placeMidiAfter = [&] (int track)
    {
        if (! showMidi)
            return;
        for (auto* lane : midiLanes)
        {
            const int after = juce::isPositiveAndBelow (lane->getAfterTrack(), n) ? lane->getAfterTrack() : -1;
            if (after != track)
                continue;
            const int laneHeight = lane->preferredHeight();
            lane->setBounds (0, y, laneWidth, laneHeight);
            y += laneHeight;
        }
    };
    for (int i = 0; i < n; ++i)
    {
        lanes[i]->setBounds (0, y, laneWidth, laneH);
        y += laneH;
        placeMidiAfter (i);
    }
    placeMidiAfter (-1);
    laneHolder.setSize (laneWidth, juce::jmax (1, y));
}

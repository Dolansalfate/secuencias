#include "TempoTrack.h"
#include <algorithm>
#include <cmath>

TempoTrackView::TempoTrackView (int headerWidth) : header (headerWidth)
{
    mode.onClick = [this] { if (onModeChanged) onModeChanged (! stretches); };
    mode.setColour (juce::TextButton::buttonColourId, ui::panelAlt);
    addAndMakeVisible (mode);
    editorBtn.setButtonText ("Editor");
    editorBtn.setColour (juce::TextButton::buttonColourId, ui::panelAlt);
    editorBtn.onClick = [this] { if (onOpenEditor) onOpenEditor(); };
    addAndMakeVisible (editorBtn);
    setStretches (true);
    // Cursores de Dibujar y Borrar (los demás gestos usan los del sistema)
    for (int i = 0; i < ui::numEditTools; ++i)
    {
        juce::Image img (juce::Image::ARGB, 28, 28, true);
        {
            juce::Graphics g (img);
            ui::drawToolIcon (g, (ui::EditTool) i, { 2.0f, 2.0f, 24.0f, 24.0f }, juce::Colours::white, juce::Colours::black);
        }
        const bool pencil = (ui::EditTool) i == ui::EditTool::draw;
        cursors[(size_t) i] = juce::MouseCursor (img, pencil ? 5 : 14, pencil ? 23 : 14);
    }
    ui::disableFocus (*this);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void TempoTrackView::setBands (const std::vector<TempoBandView>& b)
{
    bands = b;
    if (selected >= (int) bands.size())
        selected = -1;
    repaint();
}

void TempoTrackView::setAnalysis (const Analysis& a)
{
    analysis.beats = a.beats;
    analysis.meter = a.meter;
    runs = analysis.signatureRuns();
    repaint();
}

void TempoTrackView::setFixedGrid (double bpm, double clickOffset)
{
    fixedBpm = bpm;
    fixedOffset = clickOffset;
    repaint();
}

void TempoTrackView::setLength (double seconds)
{
    length = juce::jmax (0.0, seconds);
    repaint();
}

void TempoTrackView::setView (double startSeconds, double pixelsPerSecond)
{
    viewStart = startSeconds;
    pps = juce::jmax (1.0e-6, pixelsPerSecond);
    repaint();
}

void TempoTrackView::setPosition (double seconds)
{
    if (! showPlayhead)
    {
        position = seconds;
        return;
    }
    const int oldX = timeToX (position);
    position = seconds;
    repaint (oldX - 2, 0, 5, getHeight());
    repaint (timeToX (position) - 2, 0, 5, getHeight());
}

void TempoTrackView::setTool (ui::EditTool t)
{
    tool = t;
}

void TempoTrackView::setReadOnly (bool ro)
{
    readOnly = ro;
    mode.setEnabled (! ro);
    repaint();
}

void TempoTrackView::setStretches (bool s)
{
    stretches = s;
    mode.setButtonText (stretches ? juce::String::fromUTF8 ("Estira el audio") : juce::String::fromUTF8 ("Solo el click"));
    repaint();
}

void TempoTrackView::setCompact (bool c)
{
    compact = c;
    resized();
    repaint();
}

void TempoTrackView::setSelectedBand (int band)
{
    selected = juce::isPositiveAndBelow (band, (int) bands.size()) ? band : -1;
    repaint();
}

void TempoTrackView::resized()
{
    mode.setVisible (! compact);
    if (compact)
        editorBtn.setBounds (header - 62, 2, 56, getHeight() - 4);
    else
    {
        mode.setBounds (6, getHeight() - 24, header - 12, 20);
        editorBtn.setBounds (header - 62, sigHeight + 2, 56, 18);
    }
}

double TempoTrackView::mainBpm (const TempoBandView& b) const  { return stretches ? b.playBpm : b.origBpm; }
double TempoTrackView::otherBpm (const TempoBandView& b) const { return stretches ? b.origBpm : b.playBpm; }
const std::vector<juce::Point<double>>& TempoTrackView::mainCurve (const TempoBandView& b) const  { return stretches ? b.playCurve : b.origCurve; }
const std::vector<juce::Point<double>>& TempoTrackView::otherCurve (const TempoBandView& b) const { return stretches ? b.origCurve : b.playCurve; }

double TempoTrackView::mainBpmAt (int band, double t) const
{
    if (! juce::isPositiveAndBelow (band, (int) bands.size()))
        return 0.0;
    const auto& b = bands[(size_t) band];
    const auto& c = mainCurve (b);
    if (c.size() < 2)
        return mainBpm (b);
    if (t <= c.front().x)
        return c.front().y;
    for (size_t i = 1; i < c.size(); ++i)
        if (t <= c[i].x)
        {
            const double span = c[i].x - c[i - 1].x;
            return span > 0.0 ? c[i - 1].y + (c[i].y - c[i - 1].y) * (t - c[i - 1].x) / span : c[i].y;
        }
    return c.back().y;
}

void TempoTrackView::computeRange()
{
    lo = 1.0e9;
    hi = -1.0e9;
    for (auto& b : bands)
    {
        lo = juce::jmin (lo, juce::jmin (b.origBpm, b.playBpm));
        hi = juce::jmax (hi, juce::jmax (b.origBpm, b.playBpm));
        for (auto* c : { &b.playCurve, &b.origCurve })
            for (auto& p : *c)
            {
                lo = juce::jmin (lo, p.y);
                hi = juce::jmax (hi, p.y);
            }
    }
    if (lo > hi)
    {
        lo = fixedBpm > 0.0 ? fixedBpm : 120.0;
        hi = lo;
    }
    const double pad = juce::jmax (8.0, (hi - lo) * 0.3);
    lo = juce::jmax (10.0, lo - pad);
    hi += pad;
}

float TempoTrackView::yOf (double v) const
{
    return (float) graphBottom() - (float) ((v - lo) / juce::jmax (1.0, hi - lo)) * (float) (graphBottom() - graphTop());
}

double TempoTrackView::bpmAtY (int y) const
{
    return lo + (double) (graphBottom() - y) / juce::jmax (1.0, (double) (graphBottom() - graphTop())) * (hi - lo);
}

std::vector<TempoTrackView::SigMark> TempoTrackView::signatureMarks() const
{
    std::vector<SigMark> marks;
    if (analysis.beats.empty())
    {
        if (fixedBpm > 0.0)
            marks.push_back ({ -1, fixedOffset, "4/4" });   // rejilla fija del click
        return marks;
    }
    for (auto& run : runs)
        if (juce::isPositiveAndBelow (run.firstBeat, (int) analysis.beats.size()))
            marks.push_back ({ run.firstBeat, analysis.beats[(size_t) run.firstBeat].seconds,
                               juce::String (run.numerator) + "/" + juce::String (run.unit) });
    return marks;
}

int TempoTrackView::signatureAt (int x) const
{
    for (auto& m : signatureMarks())
    {
        const int mx = timeToX (m.seconds);
        if (x >= mx - 4 && x <= mx + 42)
            return m.beat;
    }
    return -1;
}

int TempoTrackView::nearestDownbeat (double t) const
{
    const auto& beats = analysis.beats;
    int best = -1;
    for (int i = 0; i < (int) beats.size(); ++i)
        if (beats[(size_t) i].beatInBar == 1 && (best < 0 || std::abs (beats[(size_t) i].seconds - t) < std::abs (beats[(size_t) best].seconds - t)))
            best = i;
    return best;
}

int TempoTrackView::edgeAt (int x) const
{
    for (int i = 1; i < (int) bands.size(); ++i)
        if (std::abs (x - timeToX (bands[(size_t) i].start)) <= 4)
            return i;
    return -1;
}

int TempoTrackView::bandAt (double t) const
{
    for (int i = (int) bands.size(); --i >= 0;)
        if (t >= bands[(size_t) i].start)
            return i;
    return bands.empty() ? -1 : 0;
}

void TempoTrackView::drawCurve (juce::Graphics& g, const std::vector<juce::Point<double>>& c, float thickness, bool dashed)
{
    juce::Path p;
    for (size_t i = 0; i < c.size(); ++i)
    {
        const float x = (float) timeToX (c[i].x), y = yOf (c[i].y);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    if (! dashed)
    {
        g.strokePath (p, juce::PathStrokeType (thickness));
        return;
    }
    juce::Path d;
    const float lengths[] = { 4.0f, 4.0f };
    juce::PathStrokeType (thickness).createDashedStroke (d, p, lengths, 2);
    g.fillPath (d);
}

void TempoTrackView::paintCompact (juce::Graphics& g)
{
    const auto content = getLocalBounds().withTrimmedLeft (header);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (content);
    g.setColour (juce::Colour (0xff20262c));
    g.fillRect (content);
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto& b = bands[i];
        const int a = timeToX (b.start), z = timeToX (b.end);
        if (z < header || a > getWidth())
            continue;
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (a, 0, 1, getHeight());
        const bool ramp = ! mainCurve (b).empty() && i + 1 < bands.size();
        juce::String text = juce::String (mainBpm (b), 1);
        if (ramp)
            text += " > " + juce::String (mainBpm (bands[i + 1]), 1);
        text += " BPM";
        if (b.transpose != 0)
            text += juce::String::formatted ("  %+d st", b.transpose);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (ui::font (11.0f, true));
        g.drawText (text, a + 46, 0, juce::jmax (0, z - a - 50), getHeight(), juce::Justification::centredLeft, true);
    }
    for (auto& m : signatureMarks())
    {
        const float x = (float) timeToX (m.seconds);
        g.setColour (juce::Colour (0xffffb74d));
        juce::Path flag;
        flag.addTriangle (x, 3.0f, x + 6.0f, 7.0f, x, 11.0f);
        g.fillPath (flag);
        g.setFont (ui::font (11.0f, true));
        g.drawText (m.text, (int) x + 7, 0, 40, getHeight(), juce::Justification::centredLeft, false);
    }
}

void TempoTrackView::paint (juce::Graphics& g)
{
    g.fillAll (ui::panel);
    // Cabecera
    g.setColour (ui::background);
    g.fillRect (0, 0, header, getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (ui::font (11.0f, true));
    if (compact)
    {
        g.drawText (juce::String::fromUTF8 ("Tempo y compás"), 8, 0, header - 70, getHeight(), juce::Justification::centredLeft);
        paintCompact (g);
        return;
    }
    g.drawText (juce::String::fromUTF8 ("Compás"), 8, 0, header - 12, sigHeight, juce::Justification::centredLeft);
    g.drawText ("Tempo", 8, sigHeight, header - 12, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (0, sigHeight, getWidth(), 1);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    if (length <= 0.0)
        return;
    if (! dragging())
        computeRange();

    const auto content = getLocalBounds().withTrimmedLeft (header);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (content);
    const double t0 = viewStart, t1 = xToTime (getWidth());
    g.setColour (juce::Colour (0xff1a1f24));
    g.fillRect (content.withTrimmedTop (sigHeight + 1));

    // Sección elegida (editor de tempo)
    if (juce::isPositiveAndBelow (selected, (int) bands.size()))
    {
        const auto& b = bands[(size_t) selected];
        g.setColour (ui::accent.withAlpha (0.08f));
        g.fillRect (timeToX (b.start), sigHeight + 1, juce::jmax (1, timeToX (b.end) - timeToX (b.start)), getHeight() - sigHeight - 1);
    }

    // Líneas guía de BPM
    const double span = hi - lo;
    double step = 5.0;
    for (double c : { 2.0, 5.0, 10.0, 20.0, 50.0, 100.0 })
    {
        step = c;
        if (span / c <= juce::jmax (3.0, (graphBottom() - graphTop()) / 28.0)) break;
    }
    g.setFont (ui::font (9.0f));
    for (double v = std::ceil (lo / step) * step; v <= hi; v += step)
    {
        const float y = yOf (v);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect ((float) header, y, (float) content.getWidth(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawText (juce::String ((int) v), header + 3, (int) y - 10, 30, 10, juce::Justification::bottomLeft);
    }

    // El tempo que se detecta tiempo a tiempo (de fondo, para ver dónde se acelera o se frena la grabación)
    const auto& beats = analysis.beats;
    if (beats.size() > 6)
    {
        juce::Path curve;
        bool started = false;
        for (size_t i = 2; i + 3 < beats.size(); ++i)
        {
            if (beats[i].seconds < t0 - 2.0) continue;
            if (beats[i].seconds > t1 + 2.0) break;
            std::array<double, 5> local {};
            for (size_t k = 0; k < 5; ++k)
                local[k] = beats[i - 2 + k + 1].seconds - beats[i - 2 + k].seconds;
            std::sort (local.begin(), local.end());
            if (local[2] <= 0.0) continue;
            const float x = (float) timeToX (beats[i].seconds);
            const float y = juce::jlimit ((float) graphTop(), (float) graphBottom(), yOf (60.0 / local[2]));
            if (! started) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            started = true;
        }
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (curve, juce::PathStrokeType (1.0f));
    }

    // Las secciones: escalones (salto) o curvas (transición hacia la siguiente); el tempo que no se edita, punteado
    float lastY = -1.0f;
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto& b = bands[(size_t) i];
        const double start = (i == edgeDrag) ? previewTime : b.start;
        const double end = (i + 1 == edgeDrag) ? previewTime : b.end;
        const float xa = (float) timeToX (start), xb = (float) timeToX (end);
        const auto& curve = mainCurve (b);
        const bool ramp = curve.size() >= 2;
        const double startBpm = (i == bpmDrag) ? previewBpm : (ramp ? curve.front().y : mainBpm (b));
        const double endBpm = ramp ? curve.back().y : startBpm;
        const float y = yOf (startBpm);
        const auto& other = otherCurve (b);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        if (other.size() >= 2)
            drawCurve (g, other, 1.0f, true);
        else if (std::abs (otherBpm (b) - mainBpm (b)) >= 0.05)
            for (float x = xa; x < xb; x += 8.0f)
                g.fillRect (x, yOf (otherBpm (b)), juce::jmin (4.0f, xb - x), 1.0f);
        if (lastY >= 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (i == edgeDrag ? 1.0f : 0.5f));
            g.fillRect (xa, juce::jmin (lastY, y), i == edgeDrag ? 2.0f : 1.0f, std::abs (lastY - y) + 1.0f);
        }
        const bool hot = i == bpmDrag || i == hoverBand || i == selected;
        g.setColour (hot ? juce::Colours::white : ui::accent);
        if (ramp && i != bpmDrag)
            drawCurve (g, curve, 2.5f, false);
        else if (ramp)
            g.drawLine (xa, y, xb, yOf (endBpm), 2.5f);   // arrastrando el inicio de una transición: aproximada
        else
            g.fillRect (xa, y - 1.0f, juce::jmax (1.0f, xb - xa), 2.5f);
        lastY = yOf (endBpm);
        if (xb - xa >= 46.0f || i == bpmDrag)
        {
            juce::String text = juce::String (startBpm, 1);
            if (ramp && i != bpmDrag)
                text += " > " + juce::String (endBpm, 1);
            else if (std::abs (b.playBpm - b.origBpm) >= 0.05 && i != bpmDrag)
                text = stretches ? juce::String (b.origBpm, 1) + " -> " + juce::String (b.playBpm, 1)
                                 : juce::String (b.origBpm, 1) + juce::String::fromUTF8 (" (suena a ") + juce::String (b.playBpm, 1) + ")";
            g.setFont (ui::font (11.0f, true));
            g.setColour (juce::Colours::white);
            const float ty = y - 15.0f < (float) graphTop() ? y + 3.0f : y - 15.0f;
            g.drawText (text, (int) xa + 4, (int) ty, juce::jmax (40, (int) (xb - xa) - 6), 13, juce::Justification::centredLeft, true);
        }
    }

    // Pista de compás
    g.setColour (juce::Colour (0xff20262c));
    g.fillRect (content.getX(), 0, content.getWidth(), sigHeight);
    const auto marks = signatureMarks();
    for (size_t k = 0; k < marks.size(); ++k)
    {
        const auto& m = marks[k];
        const double at = (sigDrag >= 0 && m.beat == sigDrag) ? previewTime : m.seconds;
        if (at > t1) break;
        const double next = k + 1 < marks.size() ? marks[k + 1].seconds : t1 + 10.0;
        if (next < t0) continue;
        const float x = (float) timeToX (at);
        const bool hot = m.beat == hoverSig || m.beat == sigDrag;
        g.setColour (hot ? juce::Colours::white : juce::Colour (0xffffb74d));
        juce::Path flag;
        flag.addTriangle (x, 2.0f, x + 6.0f, 6.0f, x, 10.0f);
        g.fillPath (flag);
        g.fillRect (x, 0.0f, 1.0f, (float) sigHeight);
        g.setFont (ui::font (12.0f, true));
        g.drawText (m.text, (int) x + 8, 0, 60, sigHeight, juce::Justification::centredLeft, false);
    }

    if (showPlayhead)
    {
        g.setColour (ui::playhead);
        g.fillRect (timeToX (position), 0, 2, getHeight());
    }
}

juce::MouseCursor TempoTrackView::cursorAt (juce::Point<int> p, int& band, int& sig) const
{
    band = sig = -1;
    if (length <= 0.0 || p.x < header || readOnly || compact)
        return juce::MouseCursor::NormalCursor;
    const bool special = tool == ui::EditTool::draw || tool == ui::EditTool::erase;
    if (p.y < sigHeight)
    {
        sig = signatureAt (p.x);
        if (special)
            return cursors[(size_t) tool];
        return sig >= 0 ? juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor) : juce::MouseCursor (juce::MouseCursor::NormalCursor);
    }
    if (edgeAt (p.x) > 0)
        return tool == ui::EditTool::erase ? cursors[(size_t) tool] : juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    const double t = timeAt (p.x);
    const int b = bandAt (t);
    if (b >= 0 && std::abs (p.y - yOf (mainBpmAt (b, t))) <= 6.0f)
    {
        band = b;
        return special ? cursors[(size_t) tool] : juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    return special ? cursors[(size_t) tool] : juce::MouseCursor (juce::MouseCursor::NormalCursor);
}

void TempoTrackView::mouseMove (const juce::MouseEvent& e)
{
    int band = -1, sig = -1;
    setMouseCursor (cursorAt (e.position.toInt(), band, sig));
    if (band != hoverBand || sig != hoverSig)
    {
        hoverBand = band;
        hoverSig = sig;
        repaint();
    }
}

void TempoTrackView::mouseExit (const juce::MouseEvent&)
{
    if (hoverBand >= 0 || hoverSig >= 0)
    {
        hoverBand = hoverSig = -1;
        repaint();
    }
}

void TempoTrackView::mouseDown (const juce::MouseEvent& e)
{
    bpmDrag = edgeDrag = sigDrag = -1;
    moved = false;
    if (length <= 0.0 || e.x < header)
        return;
    const double t = timeAt (e.x);
    if (readOnly)
    {
        if (! e.mods.isPopupMenu() && onSeek)
            onSeek (t);
        return;
    }
    if (compact)
    {
        // Una fila: el clic derecho abre el menú de la sección, un clic en un compás lo edita
        const int sig = signatureAt (e.x);
        if (sig >= 0 || analysis.beats.empty())
        {
            if (std::abs (e.x - timeToX (signatureMarks().empty() ? 0.0 : signatureMarks().front().seconds)) < 46 || sig >= 0)
            {
                if (onSignatureClicked) onSignatureClicked (sig, e.mods.isPopupMenu(), t);
                return;
            }
        }
        if (e.mods.isPopupMenu() && onTempoMenu)
            onTempoMenu (bandAt (t), t);
        else if (onSeek)
            onSeek (t);
        return;
    }
    const bool erase = tool == ui::EditTool::erase;
    const bool draw = tool == ui::EditTool::draw || e.getNumberOfClicks() == 2;
    if (e.y < sigHeight)
    {
        if (analysis.beats.empty())
        {
            // Rejilla fija (sin tiempos): el dueño ofrece pasarla a tiempos para poder cambiar el compás
            if (onSignatureClicked) onSignatureClicked (-1, e.mods.isPopupMenu(), t);
            return;
        }
        const int sig = signatureAt (e.x);
        if (e.mods.isPopupMenu())
        {
            if (onSignatureClicked) onSignatureClicked (sig, true, t);
            return;
        }
        if (erase)
        {
            if (sig >= 0 && onSignatureRemove) onSignatureRemove (sig);
            return;
        }
        if (sig >= 0 && ! draw)
        {
            sigDrag = sig;
            previewTime = analysis.beats[(size_t) sig].seconds;
            return;
        }
        if (onSignatureAdd)
            if (const int down = nearestDownbeat (t); down >= 0)
                onSignatureAdd (down);
        return;
    }
    const int band = bandAt (t);
    if (e.mods.isPopupMenu())
    {
        if (band >= 0 && onTempoMenu) onTempoMenu (band, t);
        return;
    }
    if (const int edge = edgeAt (e.x); edge > 0)
    {
        if (erase)
        {
            if (onTempoRemove) onTempoRemove (edge);
            return;
        }
        edgeDrag = edge;
        previewTime = bands[(size_t) edge].start;
        repaint();
        return;
    }
    if (draw)
    {
        if (onTempoAdd) onTempoAdd (t, roundBpm (bpmAtY (e.y)));
        return;
    }
    if (band >= 0 && onBandSelected)
        onBandSelected (band);
    if (band >= 0 && std::abs (e.y - yOf (mainBpmAt (band, t))) <= 6.0f)
    {
        if (erase)
        {
            if (band > 0 && onTempoRemove) onTempoRemove (band);
            return;
        }
        bpmDrag = band;
        const auto& c = mainCurve (bands[(size_t) band]);
        previewBpm = c.size() >= 2 ? c.front().y : mainBpm (bands[(size_t) band]);
        grabBpm = previewBpm;
        grabY = e.y;
        repaint();
    }
}

void TempoTrackView::mouseDrag (const juce::MouseEvent& e)
{
    moved = moved || e.getDistanceFromDragStart() > 3;
    if (! moved)
        return;
    const double t = timeAt (e.x);
    if (bpmDrag >= 0)
    {
        // Hacia arriba, más rápido; con Shift, en pasos finos
        const double perPixel = (hi - lo) / juce::jmax (1.0f, (float) (graphBottom() - graphTop()));
        const double raw = grabBpm + (grabY - e.y) * perPixel * (e.mods.isShiftDown() ? 0.2 : 1.0);
        previewBpm = juce::jlimit (20.0, 400.0, e.mods.isShiftDown() ? std::round (raw * 10.0) / 10.0 : roundBpm (raw));
        repaint();
    }
    else if (edgeDrag > 0)
    {
        const double lo2 = bands[(size_t) edgeDrag - 1].start + 0.25;
        const double hi2 = bands[(size_t) edgeDrag].end - 0.25;
        const int down = nearestDownbeat (t);
        const double snapped = down >= 0 ? analysis.beats[(size_t) down].seconds : t;
        previewTime = juce::jlimit (lo2, juce::jmax (lo2, hi2), snapped);
        repaint();
    }
    else if (sigDrag >= 0 && ! analysis.beats.empty())
    {
        const int beat = analysis.nearestBeat (t, 1.0e9);
        if (beat >= 0)
            previewTime = analysis.beats[(size_t) beat].seconds;
        repaint();
    }
}

void TempoTrackView::mouseUp (const juce::MouseEvent& e)
{
    const int b = bpmDrag, edge = edgeDrag, sig = sigDrag;
    bpmDrag = edgeDrag = sigDrag = -1;
    repaint();
    if (b >= 0 && moved && onTempoSet)
        onTempoSet (b, previewBpm);
    else if (edge > 0 && moved && onTempoMoved)
        onTempoMoved (edge, previewTime);
    else if (sig >= 0)
    {
        if (moved && onSignatureMoved)
        {
            const int to = analysis.nearestBeat (previewTime, 0.001);
            if (to >= 0 && to != sig)
                onSignatureMoved (sig, to);
        }
        else if (! moved && onSignatureClicked)
            onSignatureClicked (sig, false, timeAt (e.x));
    }
}

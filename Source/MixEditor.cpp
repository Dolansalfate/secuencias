#include "MixEditor.h"
#include "UiUtils.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace
{
    constexpr int snapBars = 1, snapBeats = 2, snapFree = 3;   // ids del selector de ajuste de la selección
    constexpr int headerHeight = 36, statusHeight = 22, sourcesWidth = 230, panelHeight = 70;
    constexpr int narrowWidth = 900;           // editor más angosto: columna de canciones de 160 px
    constexpr int narrowSourcesWidth = 160;
    constexpr int panelTwoRowsWidth = 744;     // panel del tramo más angosto que esto: tres filas (y más alto)
    constexpr int threeRowsPanelHeight = 98;
    constexpr double onBeatTolerance = 0.1;      // un borde a menos de esto de un tiempo cae en el tiempo (el corte a la transiente lo adelanta)
    const juce::Colour waveBackground { 0xff181c21 };
    const juce::Colour offBeatColour { 0xffffa726 };   // uniones y bordes que no caen en un tiempo

    // Forma de onda de una fuente: pares (mín, máx), `binsPerSecond` pares por segundo del archivo
    struct SourcePeaks
    {
        std::vector<float> minMax;
        int binsPerSecond = 0;
        float maxAbs = 0.0f;
    };

    // Número con coma decimal, como en el resto de la interfaz ("96,3")
    juce::String decimal (double value, int places)
    {
        return juce::String (value, places).replaceCharacter ('.', ',');
    }

    // Tempo con un decimal y sin ",0" si es entero ("96,3", "100")
    juce::String bpmText (double bpm)
    {
        const double rounded = std::round (bpm * 10.0) / 10.0;
        if (std::abs (rounded - std::round (rounded)) < 1.0e-6)
            return juce::String ((int) std::lround (rounded));
        return decimal (rounded, 1);
    }

    // Ganancia con signo ("+1,5 dB", "0,0 dB")
    juce::String gainText (double db)
    {
        if (std::abs (db) < 0.05)
            return "0,0 dB";
        return (db > 0.0 ? "+" : "") + decimal (db, 1) + " dB";
    }

    // Duración en m:ss ("0:38", "4:35")
    juce::String clockText (double seconds)
    {
        const int total = (int) std::lround (juce::jmax (0.0, seconds));
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    // Número escrito a mano: acepta coma decimal y unidades alrededor ("98,5 BPM")
    double parseNumber (const juce::String& text)
    {
        return text.replaceCharacter (',', '.').retainCharacters ("0123456789.+-").getDoubleValue();
    }

    juce::String dot() { return tr (" · "); }

    // Tempo a partir de la mediana de los intervalos entre tiempos; 0 si hay menos de dos tiempos
    double medianBpm (const Analysis& a)
    {
        std::vector<double> gaps;
        for (size_t i = 1; i < a.beats.size(); ++i)
        {
            const double d = a.beats[i].seconds - a.beats[i - 1].seconds;
            if (d > 1.0e-3)
                gaps.push_back (d);
        }
        if (gaps.empty())
            return 0.0;
        const auto middle = gaps.begin() + (std::ptrdiff_t) (gaps.size() / 2);
        std::nth_element (gaps.begin(), middle, gaps.end());
        return 60.0 / *middle;
    }

    // Duración de un tiempo (para decidir cuánto detalle cabe al dibujar)
    double beatSeconds (const Analysis& a)
    {
        const double bpm = a.bpm > 0.0 ? a.bpm : medianBpm (a);
        return bpm > 0.0 ? 60.0 / bpm : 0.5;
    }

    // "c. 17-32": los compases que abarca un tramo (vacío sin análisis)
    juce::String barRange (const Analysis& a, double start, double end)
    {
        if (a.beats.empty())
            return {};
        const int first = mix::barNumberAt (a, start);
        const int last = mix::barNumberAt (a, juce::jmax (start, end - 0.1));
        if (first <= 0 || last < first)
            return {};
        return "c. " + juce::String (first) + (last > first ? "-" + juce::String (last) : juce::String());
    }

    // Tempo, compás y tonalidad de una fuente ("96,3 BPM · 4/4 · La menor")
    juce::String analysisText (const Analysis& a)
    {
        if (a.beats.empty())
            return "Sin analizar";
        const double bpm = a.bpm > 0.0 ? a.bpm : medianBpm (a);
        auto text = bpmText (bpm) + " BPM" + dot() + juce::String (juce::jmax (1, a.meter)) + "/4";
        if (a.key.isNotEmpty())
            text += dot() + a.key;
        return text;
    }

    // Una fila de controles: anchos fijos y lo que sobra, repartido entre los flexibles. Si no cabe,
    // primero cede lo que tiene un mínimo menor que su ancho (en proporción a lo que puede ceder; los
    // flexibles, hasta 0 si no se indica otro) y solo después se escala todo. comp == nullptr es un espacio.
    struct RowItem
    {
        RowItem (juce::Component* c, int w, bool flex = false, int minW = -1)
            : comp (c), width (w), minWidth (minW >= 0 ? juce::jlimit (0, w, minW) : (flex ? 0 : w)), flexible (flex) {}
        juce::Component* comp;
        int width, minWidth;
        bool flexible;
    };

    void layoutRow (juce::Rectangle<int> row, std::initializer_list<RowItem> items, int gap = 4)
    {
        int total = 0, flexCount = 0, count = 0, slack = 0;
        for (auto& item : items)
        {
            total += item.width;
            flexCount += item.flexible ? 1 : 0;
            slack += item.width - item.minWidth;
            ++count;
        }
        const int available = row.getWidth() - gap * juce::jmax (0, count - 1);
        const int over = total - available;
        const double shrink = (over > 0 && slack > 0) ? juce::jmin (1.0, (double) over / slack) : 0.0;
        const double shrunk = total - slack * shrink;
        const double scale = (shrunk > available && shrunk > 0.0) ? juce::jmax (0.0, available / shrunk) : 1.0;
        const int extra = (total < available && flexCount > 0) ? (available - total) / flexCount : 0;
        int x = row.getX();
        for (auto& item : items)
        {
            const double base = item.width - (item.width - item.minWidth) * shrink;
            const int w = (int) (base * scale) + (item.flexible ? extra : 0);
            if (item.comp != nullptr)
                item.comp->setBounds (x, row.getY(), juce::jmax (0, w), row.getHeight());
            x += w + gap;
        }
    }

    // Forma de onda en espejo entre dos instantes de la fuente, una columna por píxel, escalada al
    // pico de la fuente (por `gain`). El color lo pone quien llama.
    void drawPeaks (juce::Graphics& g, const SourcePeaks& p, double from, double to, int x0, int width,
                    float top, float height, float gain)
    {
        const int bins = (int) (p.minMax.size() / 2);
        if (p.binsPerSecond <= 0 || bins == 0 || width <= 0 || to <= from || height <= 1.0f)
            return;
        const float mid = top + height * 0.5f;
        const float half = juce::jmax (1.0f, height * 0.5f - 1.0f);
        const float scale = gain / juce::jmax (1.0e-4f, p.maxAbs);
        const double secondsPerPixel = (to - from) / width;
        const double bps = (double) p.binsPerSecond;
        for (int x = 0; x < width; ++x)
        {
            const double t0 = from + x * secondsPerPixel;
            int b0 = (int) std::floor (t0 * bps);
            int b1 = (int) std::floor ((t0 + secondsPerPixel) * bps);
            if (b1 <= b0)
                b1 = b0 + 1;
            if (b1 <= 0)
                continue;
            if (b0 >= bins)
                break;
            b0 = juce::jmax (0, b0);
            b1 = juce::jmin (bins, b1);
            float amplitude = 0.0f;
            for (int b = b0; b < b1; ++b)
                amplitude = juce::jmax (amplitude, std::abs (p.minMax[(size_t) (2 * b)]), std::abs (p.minMax[(size_t) (2 * b + 1)]));
            const float h = juce::jmax (0.5f, juce::jmin (1.0f, amplitude * scale) * half);
            g.fillRect ((float) (x0 + x), mid - h, 1.0f, 2.0f * h);
        }
    }

    // Marcas de tiempo en m:ss (regla sin análisis y regla del mix)
    void drawTimeTicks (juce::Graphics& g, double viewStart, double pixelsPerSecond, int width, int height)
    {
        double step = 1.0;
        for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
        {
            step = s;
            if (s * pixelsPerSecond >= 60.0)
                break;
        }
        g.setFont (ui::font (10.0f));
        const double end = viewStart + width / pixelsPerSecond;
        for (double t = std::floor (viewStart / step) * step; t <= end; t += step)
        {
            if (t < 0.0)
                continue;
            const float x = (float) ((t - viewStart) * pixelsPerSecond);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillRect (x, 0.0f, 1.0f, (float) height);
            g.setColour (juce::Colours::lightgrey);
            g.drawText (clockText (t), (int) x + 3, 0, 48, height, juce::Justification::centredLeft);
        }
    }

    // Cabezal: línea vertical con un triángulo arriba
    void drawHead (juce::Graphics& g, float x, int height, juce::Colour colour, float thickness)
    {
        g.setColour (colour);
        g.fillRect (x - thickness * 0.5f, 0.0f, thickness, (float) height);
        juce::Path head;
        head.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 8.0f);
        g.fillPath (head);
    }

    void drawHint (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (ui::font (14.0f));
        g.drawFittedText (text, area.reduced (24, 12), juce::Justification::centred, 4);
    }

    juce::PopupMenu::Options menuAtMouse()
    {
        const auto mouse = juce::Desktop::getMousePosition();
        return juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (mouse.x, mouse.y, 1, 1));
    }

    void styleCaption (juce::Label& label, const juce::String& text,
                       juce::Justification justification = juce::Justification::centredLeft)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (ui::font (12.0f));
        label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        label.setJustificationType (justification);
        label.setMinimumHorizontalScale (0.5f);
        label.setInterceptsMouseClicks (false, false);
    }

    // Etiqueta con un valor; editable = un clic abre el cuadro de texto
    void styleValue (juce::Label& label, bool editable)
    {
        label.setFont (ui::font (13.0f, true));
        label.setJustificationType (juce::Justification::centred);
        label.setMinimumHorizontalScale (0.7f);
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.25f));
        label.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (editable ? 0.15f : 0.0f));
        label.setColour (juce::Label::backgroundWhenEditingColourId, ui::panelAlt);
        label.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        label.setColour (juce::Label::outlineWhenEditingColourId, ui::accent);
        if (editable)
        {
            label.setEditable (true, false, false);
            label.setMouseCursor (juce::MouseCursor::IBeamCursor);
        }
    }

    void styleToggle (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.4f));
    }

    // Al abrir el editor de una etiqueta: el valor sin unidades, todo seleccionado. Se recuerda
    // para saber después si el usuario lo cambió (cerrar el editor sin tocar nada no edita).
    void beginEdit (juce::Label& label, const juce::String& value)
    {
        label.getProperties().set ("valorInicial", value);
        if (auto* editor = label.getCurrentTextEditor())
        {
            editor->setText (value, false);
            editor->selectAll();
        }
    }

    bool editUnchanged (juce::Label& label)
    {
        return label.getText().trim() == label.getProperties()["valorInicial"].toString().trim();
    }

    // Botones del panel del tramo con letra más chica ("+1 compás" cabe en 66 px)
    struct CompactButtonLook : public juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return ui::font (juce::jmin (13.0f, (float) buttonHeight * 0.55f));
        }
    };
}

//==============================================================================
struct MixEditor::Impl : private juce::ScrollBar::Listener
{
    //==========================================================================
    // Lista de canciones (fuentes): barrita de color, nombre y estado; clic = elegir, clic
    // derecho = menú de la fuente.
    class SourceListView : public juce::Component
    {
    public:
        static constexpr int rowHeight = 46;

        explicit SourceListView (Impl& i) : impl (i)
        {
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        void paint (juce::Graphics& g) override
        {
            auto* proj = impl.project();
            if (proj == nullptr)
                return;
            const int n = (int) proj->sources.size();
            if (n == 0)
            {
                g.setColour (juce::Colours::grey);
                g.setFont (ui::font (12.0f));
                g.drawFittedText (tr ("Todavía no hay canciones en este mix."), getLocalBounds().reduced (10).removeFromTop (40),
                                  juce::Justification::centred, 2);
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                const auto& src = proj->sources[(size_t) i];
                const auto row = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
                const bool selected = i == impl.selectedSource();
                g.setColour (selected ? ui::accent.withAlpha (0.2f) : (i % 2 == 0 ? ui::panel : ui::panelAlt));
                g.fillRect (row);
                if (selected)
                {
                    g.setColour (ui::accent.withAlpha (0.6f));
                    g.drawRect (row, 1);
                }
                g.setColour (ui::trackColour (i));
                g.fillRoundedRectangle ((float) row.getX() + 4.0f, (float) row.getY() + 6.0f, 4.0f, (float) rowHeight - 12.0f, 2.0f);

                auto text = row.withTrimmedLeft (14).withTrimmedRight (6);
                auto top = text.removeFromTop (26).withTrimmedTop (5);
                // Cuántos tramos del mix salen de esta canción
                const int uses = impl.segmentsUsing (i);
                if (uses > 0)
                {
                    const auto badge = top.removeFromRight (24).withSizeKeepingCentre (22, 15).toFloat();
                    g.setColour (ui::trackColour (i).withAlpha (0.35f));
                    g.fillRoundedRectangle (badge, 3.0f);
                    g.setColour (juce::Colours::white);
                    g.setFont (ui::font (10.0f, true));
                    g.drawText (juce::String (uses), badge, juce::Justification::centred);
                }
                g.setColour (juce::Colours::white);
                g.setFont (ui::font (13.0f, true));
                g.drawText (src.name, top, juce::Justification::centredLeft, true);
                g.setColour (impl.statusFor (i).isNotEmpty() ? ui::accent : juce::Colours::grey);
                g.setFont (ui::font (11.0f));
                g.drawText (impl.sourceStateText (i), text.removeFromTop (16), juce::Justification::centredLeft, true);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* proj = impl.project();
            if (proj == nullptr)
                return;
            const int row = e.y / rowHeight;
            if (row < 0 || row >= (int) proj->sources.size())
                return;
            impl.sourceWasLast = true;
            impl.chooseSource (row, true);
            if (e.mods.isPopupMenu())
                impl.sourceMenu (row);
        }

    private:
        Impl& impl;
    };

    //==========================================================================
    // Vista de la fuente elegida: regla con los compases, forma de onda (imagen cacheada), tramos
    // ya usados, selección y cabezales. Arrastrar = seleccionar (ajustado a compases, tiempos o
    // libre); arrastrar un borde de la selección (o su asa en la regla) = mover solo ese borde;
    // Shift + clic = llevar ahí el borde más cercano; clic = cabezal de la vista (desde donde
    // escucha "Escuchar"), doble clic = escuchar, clic derecho = menú (inicio o fin del tramo aquí).
    class SourceWaveView : public juce::Component
    {
    public:
        static constexpr int rulerHeight = 18;
        static constexpr double maxPixelsPerSecond = 1500.0;

        explicit SourceWaveView (Impl& i) : impl (i)
        {
            setOpaque (true);   // pinta todo su fondo: el cabezal a 30 Hz no repinta el editor de debajo
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        double length() const              { return juce::jmax (0.5, impl.sourceLength (impl.selectedSource())); }
        double minPixelsPerSecond() const  { return juce::jmax (1, getWidth()) / length(); }
        double visibleSeconds() const      { return juce::jmax (1, getWidth()) / pixelsPerSecond; }
        float timeToX (double t) const     { return (float) ((t - viewStart) * pixelsPerSecond); }
        double xToTime (int x) const       { return viewStart + x / pixelsPerSecond; }
        double timeAt (int x) const        { return juce::jlimit (0.0, juce::jmax (0.0, impl.sourceLength (impl.selectedSource())), xToTime (x)); }

        void setView (double start, double pps)
        {
            const double minPps = minPixelsPerSecond();
            pps = juce::jlimit (minPps, juce::jmax (minPps, maxPixelsPerSecond), pps);
            start = juce::jlimit (0.0, juce::jmax (0.0, length() - juce::jmax (1, getWidth()) / pps), start);
            whole = pps <= minPps * 1.0001;
            if (std::abs (start - viewStart) > 1.0e-9 || std::abs (pps - pixelsPerSecond) > 1.0e-9)
            {
                viewStart = start;
                pixelsPerSecond = pps;
                invalidate();
            }
            impl.updateSourceScroll();
        }

        void fit() { setView (0.0, minPixelsPerSecond()); }

        // El largo de la fuente cambió (o se conoció recién con la forma de onda): si se veía entera,
        // se sigue viendo entera; si no, se conserva el zoom dentro de los límites nuevos
        void lengthChanged()
        {
            if (whole)
                fit();
            else
                setView (viewStart, pixelsPerSecond);
        }

        // Que se vea [from, to]: desplaza o, si no cabe, aleja
        void ensureVisible (double from, double to)
        {
            const double visible = visibleSeconds();
            if (from >= viewStart && to <= viewStart + visible)
                return;
            if (to - from > visible * 0.9)
            {
                const double span = (to - from) * 1.15;
                setView (from - (span - (to - from)) * 0.5, juce::jmax (1, getWidth()) / span);
            }
            else
                setView (from - visible * 0.1, pixelsPerSecond);
        }

        void follow (double t)
        {
            const float x = timeToX (t);
            if (x < 0.0f || x > (float) getWidth() - 30.0f)
                setView (t - visibleSeconds() * 0.1, pixelsPerSecond);
        }

        void invalidate()
        {
            imageValid = false;
            repaint();
        }

        void repaintAt (double t)
        {
            const int x = (int) timeToX (t);
            repaint (x - 8, 0, 17, getHeight());
        }

        void resized() override
        {
            lengthChanged();
            invalidate();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (waveBackground);
            auto* proj = impl.project();
            if (proj == nullptr)
                return;
            if (proj->sources.empty())
            {
                drawHint (g, getLocalBounds(), tr ("Agrega las canciones originales con «+ Canción». Se analizan solas (tiempos y compases) y después eliges los tramos."));
                return;
            }
            auto* src = impl.currentSource();
            if (src == nullptr)
            {
                drawHint (g, getLocalBounds(), tr ("Elige una canción de la lista para ver sus compases y elegir un tramo."));
                return;
            }
            const int source = impl.selectedSource();
            const auto colour = ui::trackColour (source);
            const int w = getWidth(), h = getHeight();
            const float waveTop = (float) rulerHeight, waveHeight = (float) (h - rulerHeight);

            g.setColour (ui::panelAlt);
            g.fillRect (0, 0, w, rulerHeight);

            if (! imageValid)
                renderImage();
            g.drawImageAt (image, 0, rulerHeight);
            if (impl.peaksFor (source) == nullptr)
            {
                g.setColour (juce::Colours::grey);
                g.setFont (ui::font (13.0f));
                g.drawText ("Calculando la forma de onda...", 0, rulerHeight, w, h - rulerHeight, juce::Justification::centred);
            }

            // Tramos de esta canción ya usados en el mix, con su número de orden
            for (int i = 0; i < (int) proj->segments.size(); ++i)
            {
                const auto& seg = proj->segments[(size_t) i];
                if (seg.source != source)
                    continue;
                const float a = timeToX (seg.start), b = timeToX (seg.end);
                if (b < 0.0f || a > (float) w)
                    continue;
                const bool chosen = i == impl.selectedSegment();
                g.setColour (colour.withAlpha (chosen ? 0.26f : 0.13f));
                g.fillRect (a, waveTop, b - a, waveHeight);
                g.setColour (colour.withAlpha (0.7f));
                g.fillRect (a, waveTop, 1.0f, waveHeight);
                g.fillRect (b - 1.0f, waveTop, 1.0f, waveHeight);
                const auto badge = juce::Rectangle<float> (juce::jmax (a, 0.0f) + 3.0f, waveTop + 3.0f, 26.0f, 15.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (badge, 3.0f);
                g.setColour (juce::Colours::black);
                g.setFont (ui::font (11.0f, true));
                g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
            }

            drawGrid (g, src->analysis);

            // Selección, con un asa en la regla en cada borde (se arrastran para mover solo ese borde)
            if (impl.hasSelection())
            {
                const float a = timeToX (impl.selStart), b = timeToX (impl.selEnd);
                g.setColour (ui::accent.withAlpha (0.2f));
                g.fillRect (a, 0.0f, b - a, (float) h);
                for (int edge = 0; edge < 2; ++edge)
                {
                    const float x = edge == 0 ? a : b;
                    const bool hot = edge == hoverEdge || edge == draggingEdge;
                    g.setColour (hot ? ui::accent.brighter (0.4f) : ui::accent);
                    g.fillRect (x - (hot ? 1.5f : 1.0f), 0.0f, hot ? 3.0f : 2.0f, (float) h);
                    // Asa: una pestaña hacia adentro de la selección, con dos rayitas
                    const float tab = (float) handleWidth;
                    g.fillRoundedRectangle (edge == 0 ? x - 1.0f : x - tab + 1.0f, 1.0f, tab, (float) rulerHeight - 2.0f, 2.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.55f));
                    const float gx = edge == 0 ? x + tab * 0.5f - 1.0f : x - tab * 0.5f + 1.0f;
                    g.fillRect (gx - 2.0f, 5.0f, 1.0f, (float) rulerHeight - 10.0f);
                    g.fillRect (gx + 1.0f, 5.0f, 1.0f, (float) rulerHeight - 10.0f);
                }
            }

            // Cabezal de la vista (desde donde escucha "Escuchar") y el de reproducción
            drawHead (g, timeToX (impl.sourceCursor), h, juce::Colours::white.withAlpha (0.75f), 1.0f);
            if (impl.playKind == 1 && impl.playingSource == source)
                drawHead (g, timeToX (impl.playSeconds), h, ui::playhead, 2.0f);
        }

        // Borde de la selección bajo el mouse: 0 = inicio, 1 = fin, -1 = ninguno. Sobre la línea (a 6 px) o sobre
        // su asa en la regla; si los dos están cerca, el más próximo
        int edgeAt (juce::Point<int> p) const
        {
            if (! impl.hasSelection())
                return -1;
            const float a = timeToX (impl.selStart), b = timeToX (impl.selEnd), x = (float) p.x;
            const bool inRuler = p.y < rulerHeight;
            const float da = inRuler && x >= a - 3.0f && x <= a + (float) handleWidth + 1.0f ? 0.0f : std::abs (x - a);
            const float db = inRuler && x <= b + 3.0f && x >= b - (float) handleWidth - 1.0f ? 0.0f : std::abs (x - b);
            if (da > 6.0f && db > 6.0f)
                return -1;
            if (da <= 6.0f && db <= 6.0f)
                return x < (a + b) * 0.5f ? 0 : 1;   // selección angosta: manda la mitad donde se hizo clic
            return da <= db ? 0 : 1;
        }

        void setHoverEdge (int edge)
        {
            if (edge == hoverEdge)
                return;
            hoverEdge = edge;
            setMouseCursor (edge >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setHoverEdge (impl.currentSource() != nullptr ? edgeAt (e.getPosition()) : -1);
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (draggingEdge < 0)
                setHoverEdge (-1);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (impl.currentSource() == nullptr)
                return;
            impl.sourceWasLast = true;
            popupDown = e.mods.isPopupMenu();
            dragging = false;
            draggingEdge = -1;
            if (popupDown)
            {
                impl.sourceWaveMenu (timeAt (e.x), 8.0 / pixelsPerSecond);
                return;
            }
            dragAnchor = timeAt (e.x);
            if (! e.mods.isShiftDown())
                draggingEdge = edgeAt (e.getPosition());   // se toma un borde: el otro queda donde está
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (popupDown || impl.currentSource() == nullptr)
                return;
            if (! dragging && std::abs (e.getDistanceFromDragStartX()) < (draggingEdge >= 0 ? 2 : 4))
                return;
            dragging = true;
            const double t = timeAt (e.x);
            if (draggingEdge >= 0)
                impl.dragEdge (draggingEdge == 0, t);
            else
                impl.dragSelection (juce::jmin (dragAnchor, t), juce::jmax (dragAnchor, t));
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (popupDown || impl.currentSource() == nullptr)
                return;
            const int edge = draggingEdge;
            draggingEdge = -1;
            if (dragging)
            {
                dragging = false;
                if (edge >= 0)
                    impl.finishEdge (edge == 0);
                else
                    impl.finishSelection();
                setHoverEdge (edgeAt (e.getPosition()));
                return;
            }
            if (e.mods.isShiftDown())
            {
                impl.extendSelectionTo (timeAt (e.x));   // el borde más cercano va al clic
                return;
            }
            impl.setSourceCursor (timeAt (e.x));
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || impl.currentSource() == nullptr)
                return;
            impl.setSourceCursor (timeAt (e.x));
            impl.playSource();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (impl.currentSource() == nullptr)
                return;
            if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            {
                const double anchor = xToTime (e.x);
                const double factor = std::pow (1.5, juce::jlimit (-1.0, 1.0, (double) wheel.deltaY * 6.0));
                const double minPps = minPixelsPerSecond();
                const double pps = juce::jlimit (minPps, juce::jmax (minPps, maxPixelsPerSecond), pixelsPerSecond * factor);
                setView (anchor - e.x / pps, pps);
            }
            else
            {
                const double delta = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
                setView (viewStart - delta * visibleSeconds() * 0.6, pixelsPerSecond);
            }
        }

        double viewStart = 0.0, pixelsPerSecond = 10.0;

    private:
        // Compases (línea y número, cada 1, 2, 4, 8... según el espacio) y marcas cortas de los
        // tiempos; sin análisis, el tiempo en m:ss
        void drawGrid (juce::Graphics& g, const Analysis& a)
        {
            const int w = getWidth(), h = getHeight();
            if (a.beats.empty())
            {
                drawTimeTicks (g, viewStart, pixelsPerSecond, w, rulerHeight);
                return;
            }
            const double t0 = viewStart - 0.5, t1 = xToTime (w) + 0.5;
            const double barPx = beatSeconds (a) * juce::jmax (1, a.meter) * pixelsPerSecond;
            int step = 1;
            while (barPx * step < 28.0 && step < (1 << 16))
                step *= 2;
            const bool showBeats = beatSeconds (a) * pixelsPerSecond >= 6.0;
            int bar = 0;
            g.setFont (ui::font (10.0f, true));
            for (const auto& beat : a.beats)
            {
                if (beat.beatInBar == 1)
                    ++bar;
                if (beat.seconds < t0)
                    continue;
                if (beat.seconds > t1)
                    break;
                const float x = timeToX (beat.seconds);
                if (beat.beatInBar == 1 && bar > 0)
                {
                    const bool labelled = (bar - 1) % step == 0;
                    if (! labelled && barPx < 4.0)
                        continue;
                    g.setColour (juce::Colours::white.withAlpha (labelled ? 0.28f : 0.1f));
                    g.fillRect (x, 0.0f, 1.0f, (float) h);
                    if (labelled)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.9f));
                        g.drawText (juce::String (bar), (int) x + 3, 0, 44, rulerHeight, juce::Justification::centredLeft);
                    }
                }
                else if (showBeats)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    g.fillRect (x, (float) rulerHeight - 6.0f, 1.0f, 6.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.05f));
                    g.fillRect (x, (float) rulerHeight, 1.0f, (float) (h - rulerHeight));
                }
            }
        }

        void renderImage()
        {
            const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight() - rulerHeight);
            image = juce::Image (juce::Image::ARGB, w, h, true);
            imageValid = true;
            juce::Graphics ig (image);
            ig.setColour (juce::Colours::white.withAlpha (0.05f));
            ig.fillRect (0, h / 2, w, 1);
            const int source = impl.selectedSource();
            if (auto* pk = impl.peaksFor (source))
            {
                ig.setColour (ui::trackColour (source).withAlpha (0.8f));
                drawPeaks (ig, *pk, viewStart, viewStart + w / pixelsPerSecond, 0, w, 3.0f, (float) h - 6.0f, 1.0f);
            }
        }

        Impl& impl;
        juce::Image image;
        bool imageValid = false;
        bool whole = true;             // se ve la canción entera (se mantiene al cambiar el tamaño)
        bool dragging = false, popupDown = false;
        double dragAnchor = 0.0;
        static constexpr int handleWidth = 9;   // asa de cada borde de la selección, en la regla
        int hoverEdge = -1, draggingEdge = -1;  // borde bajo el mouse y el que se arrastra (0 inicio, 1 fin)
    };

    //==========================================================================
    // Línea del mix: los tramos de mix::layout uno tras otro (ancho proporcional a su duración en
    // el mix) con la onda de su fuente comprimida, uniones, fundidos, el elegido con borde de
    // acento y los cabezales. Clic en un tramo = elegirlo; clic en la regla o el fondo = cabezal.
    class MixLineView : public juce::Component
    {
    public:
        static constexpr int rulerHeight = 16, textHeight = 32;
        static constexpr float blockPad = 4.0f;
        static constexpr double maxPixelsPerSecond = 400.0;

        explicit MixLineView (Impl& i) : impl (i)
        {
            setOpaque (true);
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        double length() const              { return juce::jmax (1.0, impl.mixLength); }
        double minPixelsPerSecond() const  { return juce::jmax (1, getWidth()) / length(); }
        double visibleSeconds() const      { return juce::jmax (1, getWidth()) / pixelsPerSecond; }
        float timeToX (double t) const     { return (float) ((t - viewStart) * pixelsPerSecond); }
        double xToTime (int x) const       { return viewStart + x / pixelsPerSecond; }
        double timeAt (int x) const        { return juce::jlimit (0.0, juce::jmax (0.0, impl.mixLength), xToTime (x)); }

        void setView (double start, double pps)
        {
            const double minPps = minPixelsPerSecond();
            pps = juce::jlimit (minPps, juce::jmax (minPps, maxPixelsPerSecond), pps);
            start = juce::jlimit (0.0, juce::jmax (0.0, length() - juce::jmax (1, getWidth()) / pps), start);
            whole = pps <= minPps * 1.0001;
            if (std::abs (start - viewStart) > 1.0e-9 || std::abs (pps - pixelsPerSecond) > 1.0e-9)
            {
                viewStart = start;
                pixelsPerSecond = pps;
                invalidate();
            }
            impl.updateMixScroll();
        }

        void fit() { setView (0.0, minPixelsPerSecond()); }

        // Cambiaron los tramos: se ajusta al ancho si se estaba viendo entero
        void layoutChanged()
        {
            if (whole)
                fit();
            else
                setView (viewStart, pixelsPerSecond);
            invalidate();
        }

        void ensureVisible (double from, double to)
        {
            const double visible = visibleSeconds();
            if (whole || (from >= viewStart && to <= viewStart + visible))
                return;
            setView (from - visible * 0.1, pixelsPerSecond);
        }

        void follow (double t)
        {
            if (whole)
                return;
            const float x = timeToX (t);
            if (x < 0.0f || x > (float) getWidth() - 30.0f)
                setView (t - visibleSeconds() * 0.1, pixelsPerSecond);
        }

        void invalidate()
        {
            imageValid = false;
            repaint();
        }

        void repaintAt (double t)
        {
            const int x = (int) timeToX (t);
            repaint (x - 8, 0, 17, getHeight());
        }

        int segmentAt (double t) const
        {
            const auto& places = impl.placements;
            for (int i = 0; i < (int) places.size(); ++i)
            {
                const auto& p = places[(size_t) i];
                if (p.outEnd > p.outStart && t >= p.outStart && t < p.outEnd)
                    return i;
            }
            return -1;
        }

        void resized() override
        {
            if (whole)
                fit();
            else
                setView (viewStart, pixelsPerSecond);
            invalidate();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (waveBackground);
            auto* proj = impl.project();
            if (proj == nullptr)
                return;
            if (proj->segments.empty())
            {
                drawHint (g, getLocalBounds(), tr ("Elige un tramo en la canción de arriba (arrastra sobre la onda) y pulsa «Agregar al mix». "
                                                   "Los tramos suenan uno tras otro y cada unión cae justo en el tiempo."));
                return;
            }
            const int w = getWidth(), h = getHeight();
            g.setColour (ui::panelAlt);
            g.fillRect (0, 0, w, rulerHeight);
            drawTimeTicks (g, viewStart, pixelsPerSecond, w, rulerHeight);

            if (! imageValid)
                renderImage();
            g.drawImageAt (image, 0, rulerHeight);

            const float top = (float) rulerHeight + blockPad, bottom = (float) h - blockPad;
            const auto& places = impl.placements;
            const int count = juce::jmin ((int) places.size(), (int) proj->segments.size());
            for (int i = 0; i < count; ++i)
            {
                const auto& p = places[(size_t) i];
                const auto& seg = proj->segments[(size_t) i];
                const float a = timeToX (p.outStart), b = timeToX (p.outEnd);
                if (p.outEnd <= p.outStart || b < 0.0f || a > (float) w)
                    continue;
                const auto colour = ui::trackColour (seg.source);
                g.setColour (colour.withAlpha (0.7f));
                g.drawRect (juce::Rectangle<float> (a, top, b - a, bottom - top), 1.0f);
                // Fundido cruzado: rampa desde donde el tramo empieza a entrar hasta la unión
                if (i > 0 && seg.fadeBeats > 0.0 && p.fadeIn > 0.0)
                {
                    const float fx = timeToX (p.outStart - p.fadeIn);
                    juce::Path ramp;
                    ramp.addTriangle (fx, bottom, a, top + (float) textHeight, a, bottom);
                    g.setColour (colour.withAlpha (0.3f));
                    g.fillPath (ramp);
                    g.setColour (colour);
                    g.strokePath (ramp, juce::PathStrokeType (1.0f));
                }
                if (b - a >= 24.0f)
                {
                    const auto line = juce::Rectangle<float> (juce::jmax (a, 0.0f) + 5.0f, top + 2.0f, b - juce::jmax (a, 0.0f) - 9.0f, 15.0f);
                    g.setColour (juce::Colours::white);
                    g.setFont (ui::font (12.0f, true));
                    g.drawText (impl.segmentTitle (i), line, juce::Justification::centredLeft, true);
                    g.setColour (impl.joinOffBeat (i) ? offBeatColour : juce::Colours::lightgrey);
                    g.setFont (ui::font (11.0f));
                    g.drawText (impl.segmentDetail (i), line.translated (0.0f, 15.0f), juce::Justification::centredLeft, true);
                }
            }

            // Uniones
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            for (int i = 1; i < count; ++i)
                if (! impl.joinOffBeat (i))
                    g.fillRect (timeToX (places[(size_t) i].outStart), (float) rulerHeight, 1.0f, (float) (h - rulerHeight));

            // Tramo elegido
            const int chosen = impl.selectedSegment();
            if (chosen >= 0 && chosen < count)
            {
                const auto& p = places[(size_t) chosen];
                const auto block = juce::Rectangle<float> (timeToX (p.outStart), top, timeToX (p.outEnd) - timeToX (p.outStart), bottom - top);
                g.setColour (ui::accent.withAlpha (0.1f));
                g.fillRect (block);
                g.setColour (ui::accent);
                g.drawRect (block.expanded (1.0f), 2.0f);
            }

            // La unión que no cae en un tiempo (un borde en el inicio o el final de la canción, o elegido
            // "Libre"): en naranja, con una marca arriba y encima del borde del tramo elegido
            g.setColour (offBeatColour);
            for (int i = 1; i < count; ++i)
                if (impl.joinOffBeat (i))
                {
                    const float x = timeToX (places[(size_t) i].outStart);
                    g.fillRect (x - 1.0f, (float) rulerHeight, 2.0f, (float) (h - rulerHeight));
                    juce::Path mark;
                    mark.addTriangle (x - 6.0f, (float) rulerHeight, x + 6.0f, (float) rulerHeight, x, (float) rulerHeight + 9.0f);
                    g.fillPath (mark);
                }

            drawHead (g, timeToX (impl.mixCursor), h, juce::Colours::white.withAlpha (0.75f), 1.0f);
            if (impl.playKind == 2)
                drawHead (g, timeToX (impl.playSeconds), h, ui::playhead, 2.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* proj = impl.project();
            rulerDrag = false;
            if (proj == nullptr || proj->segments.empty())
                return;
            impl.sourceWasLast = false;
            const double t = timeAt (e.x);
            const int segment = e.y >= rulerHeight ? segmentAt (xToTime (e.x)) : -1;
            if (e.mods.isPopupMenu())
            {
                if (segment >= 0)
                {
                    impl.chooseSegment (segment);
                    impl.segmentMenu (segment, t);
                }
                return;
            }
            if (segment >= 0)
                impl.chooseSegment (segment);
            else
            {
                rulerDrag = true;
                impl.setMixCursor (t);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (rulerDrag)
                impl.setMixCursor (timeAt (e.x));
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto* proj = impl.project();
            if (e.mods.isPopupMenu() || proj == nullptr || proj->segments.empty())
                return;
            const double t = timeAt (e.x);
            impl.setMixCursor (t);
            impl.playMix (t);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto* proj = impl.project();
            if (proj == nullptr || proj->segments.empty())
                return;
            if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            {
                const double anchor = xToTime (e.x);
                const double factor = std::pow (1.5, juce::jlimit (-1.0, 1.0, (double) wheel.deltaY * 6.0));
                const double minPps = minPixelsPerSecond();
                const double pps = juce::jlimit (minPps, juce::jmax (minPps, maxPixelsPerSecond), pixelsPerSecond * factor);
                setView (anchor - e.x / pps, pps);
            }
            else if (! whole)
            {
                const double delta = std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
                setView (viewStart - delta * visibleSeconds() * 0.6, pixelsPerSecond);
            }
        }

        double viewStart = 0.0, pixelsPerSecond = 10.0;

    private:
        // Fondos de los tramos y sus ondas (lo que cuesta dibujar); bordes y textos van en paint
        void renderImage()
        {
            const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight() - rulerHeight);
            image = juce::Image (juce::Image::ARGB, w, h, true);
            imageValid = true;
            auto* proj = impl.project();
            if (proj == nullptr)
                return;
            juce::Graphics ig (image);
            const float top = blockPad, bottom = (float) h - blockPad;
            const auto& places = impl.placements;
            const int count = juce::jmin ((int) places.size(), (int) proj->segments.size());
            for (int i = 0; i < count; ++i)
            {
                const auto& p = places[(size_t) i];
                const auto& seg = proj->segments[(size_t) i];
                if (p.outEnd <= p.outStart)
                    continue;
                const float a = timeToX (p.outStart), b = timeToX (p.outEnd);
                if (b < 0.0f || a > (float) w)
                    continue;
                const auto colour = ui::trackColour (seg.source);
                ig.setColour (colour.withAlpha (0.15f));
                ig.fillRect (a, top, b - a, bottom - top);
                const auto* pk = impl.peaksFor (seg.source);
                const int xa = juce::jmax (0, (int) std::ceil (a)), xb = juce::jmin (w, (int) std::floor (b));
                if (pk == nullptr || xb <= xa)
                    continue;
                // Cada píxel del bloque corresponde a un instante de la fuente (razón constante)
                const double ratio = p.ratio > 1.0e-6 ? p.ratio : 1.0;
                const double from = p.srcStart + (xToTime (xa) - p.outStart) / ratio;
                const double to = p.srcStart + (xToTime (xb) - p.outStart) / ratio;
                const float waveTop = bottom - top > 90.0f ? top + (float) textHeight : top;
                ig.setColour (colour.withAlpha (0.7f));
                drawPeaks (ig, *pk, from, to, xa, xb - xa, waveTop + 2.0f, bottom - waveTop - 4.0f,
                           juce::Decibels::decibelsToGain (seg.gainDb));
            }
        }

        Impl& impl;
        juce::Image image;
        bool imageValid = false;
        bool whole = true;
        bool rulerDrag = false;
    };

    //==========================================================================
    explicit Impl (MixEditor&);
    ~Impl() override;

    // --- Accesos ---
    MixProject* project() const     { return owner.project; }
    int selectedSource() const      { return owner.selectedSource; }
    int selectedSegment() const     { return owner.selectedSegment; }
    bool validSource (int i) const  { return project() != nullptr && i >= 0 && i < (int) project()->sources.size(); }
    MixSource* currentSource() const
    {
        return validSource (owner.selectedSource) ? &project()->sources[(size_t) owner.selectedSource] : nullptr;
    }
    MixSegment* currentSegment() const
    {
        auto* proj = project();
        return proj != nullptr && owner.selectedSegment >= 0 && owner.selectedSegment < (int) proj->segments.size()
                 ? &proj->segments[(size_t) owner.selectedSegment] : nullptr;
    }
    bool hasSelection() const       { return selEnd > selStart + 0.02; }
    const SourcePeaks* peaksFor (int source) const;
    juce::String statusFor (int source) const;
    juce::String sourceStateText (int source) const;
    double sourceLength (int source) const;
    int segmentsUsing (int source) const;
    int segmentMeter (const MixSegment&) const;
    juce::String segmentTitle (int index) const;
    juce::String segmentDetail (int index) const;
    bool edgeOnBeat (int source, double seconds) const;
    bool joinOffBeat (int index) const;
    bool selectionOffBeat() const;
    bool sourceHasBars() const;                 // la fuente elegida tiene compases (análisis con primeros tiempos)
    int selectionFirstBar() const;              // compases de la selección, como los muestra la cabecera
    int selectionLastBar() const;
    juce::String selectionSuffix() const;       // "· 16 compases · 0:38" (y si un borde no cae en un tiempo, cuál)
    juce::String mixSummary (bool withTempo) const;

    // --- Estado general ---
    void projectChanged();
    void refreshFromProject();
    void recomputeLayout();
    void changed();
    void updateControls();
    void updateSourceButtons();
    void updatePanel();
    void updateListSize();
    void revealSourceRow (int index);
    void updateSourceScroll();
    void updateMixScroll();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void layout();
    void paintFrame (juce::Graphics&);
    void setPeaks (int source, std::vector<float> minMax, int binsPerSecond);
    void setStatusOf (int source, const juce::String& text);
    void setPlayback (int kind, int source, double seconds, bool playingNow);
    bool handleKey (const juce::KeyPress&);

    // --- Fuentes y selección ---
    void chooseSource (int index, bool fitView);
    void sourceMenu (int index);
    void sourceMenuResult (const juce::String& file, int result);
    void sourceWaveMenu (double seconds, double tolerance);
    int sourceIndexOf (const juce::String& file) const;
    double snapTime (double seconds) const;
    double refineEdge (int source, double seconds) const;
    void dragSelection (double from, double to);
    void finishSelection();
    void dragEdge (bool startEdge, double seconds);       // mueve solo ese borde (ajustado), sin cruzar el otro
    void finishEdge (bool startEdge);                     // al soltar: ajusta ese borde a la transiente
    void setEdgeAt (bool startEdge, double seconds);      // menú "Inicio / Fin del tramo aquí"
    void extendSelectionTo (double seconds);              // Shift + clic: el borde más cercano va ahí
    void applySelection (double start, double end, bool refineStart, bool refineEnd);
    void updateSelectionFields();                         // los campos de la cabecera ("Compases [8] a [16]")
    void selectionFieldEdited (bool startField);
    void setSourceCursor (double seconds);
    void playSource();

    // --- Tramos ---
    void chooseSegment (int index);
    void segmentMenu (int index, double mixSeconds);
    void addSelectionToMix();
    void replaceSegmentWithSelection();
    void syncSelectionToSegment();
    void moveSegment (int index, int delta);
    void duplicateSegment (int index);
    void removeSegment (int index);
    void nudgeEdge (bool startEdge, int direction);
    void setMixCursor (double seconds);
    void playMix (double from);
    void playJoin();
    void stop();

    // --- Ediciones desde las etiquetas y selectores ---
    void renameFromLabel();
    void mixTempoEdited();
    void segmentTempoEdited();
    void segmentGainEdited();
    void segmentLabelEdited();
    void transposeBy (int semitones);
    void fadeChosen();

    MixEditor& owner;
    CompactButtonLook compactLook;          // antes que los botones que lo usan (se destruye después)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // los menús asíncronos comprueban que el editor sigue vivo

    // Cachés por archivo de la fuente (quitar una fuente reindexa las demás, el archivo no cambia)
    std::map<juce::String, SourcePeaks> peaks;
    std::map<juce::String, juce::String> statuses;
    juce::File shownFolder;
    juce::String selectedSourceFile;
    int knownSourceCount = 0;

    std::vector<MixPlacement> placements;   // mix::layout del proyecto (uno por tramo)
    std::vector<bool> offBeatJoins;         // por tramo: su unión con el anterior no cae en un tiempo (joinOffBeat)
    double mixLength = 0.0;
    double selStart = 0.0, selEnd = 0.0;    // selección en segundos de la fuente elegida (vacía si selEnd <= selStart)
    double sourceCursor = 0.0, mixCursor = 0.0;
    int snapMode = snapBars;
    int playKind = 0, playingSource = -1;
    double playSeconds = 0.0;
    bool isPlaying = false;
    bool sourceWasLast = false;             // lo último que se usó fue la vista de la fuente (para Espacio)
    bool busy = false;
    juce::Rectangle<int> headerArea, leftArea, sourceArea, sourceInfoArea, mixArea, mixInfoArea, panelArea;
    int selectionFieldsLeft = 0;            // x donde empiezan los campos de la selección (el estado se dibuja antes)

    // Cabecera
    juce::Label mixCaption, nameEdit, bpmCaption, bpmEdit;
    juce::TextButton keepTemposBtn, createBtn, closeBtn;
    // Canciones
    juce::Label sourcesCaption;
    juce::TextButton addSourceBtn;
    SourceListView sourceList { *this };
    juce::Viewport sourceViewport;
    // Vista de la fuente
    juce::ComboBox snapBox;
    juce::TextButton playSourceBtn, stopSourceBtn, addToMixBtn, replaceBtn;
    // Selección escrita a mano en la cabecera de la fuente: "Compases [8] a [16] · 9 compases · 0:21" (o los
    // tiempos m:ss sin análisis)
    juce::Label selCaption, selFromEdit, selToCaption, selToEdit, selInfo;
    SourceWaveView sourceWave { *this };
    juce::ScrollBar sourceScroll { false };
    // Línea del mix
    juce::TextButton playMixBtn, playJoinBtn, stopMixBtn, clickBtn, fitMixBtn;
    MixLineView mixLine { *this };
    juce::ScrollBar mixScroll { false };
    // Panel del tramo elegido
    juce::Label segCaption;
    juce::TextButton moveLeftBtn, moveRightBtn, removeSegBtn;
    juce::Label startCaption, endCaption;
    juce::TextButton startEarlierBtn, startLaterBtn, endEarlierBtn, endLaterBtn;
    juce::Label tempoCaption, segTempoEdit, toneCaption, toneValue, gainCaption, gainEdit, fadeCaption, nameCaption, labelEdit;
    juce::TextButton toneDownBtn, toneUpBtn;
    juce::ComboBox fadeBox;
    // Pie
    juce::Label statusLabel;
};

//==============================================================================
MixEditor::Impl::Impl (MixEditor& o) : owner (o)
{
    // --- Cabecera ---
    styleCaption (mixCaption, "Mix:");
    mixCaption.setFont (ui::font (14.0f, true));
    mixCaption.setColour (juce::Label::textColourId, juce::Colours::white);
    styleValue (nameEdit, true);
    nameEdit.setFont (ui::font (15.0f, true));
    nameEdit.setJustificationType (juce::Justification::centredLeft);
    nameEdit.onEditorShow = [this] { if (auto* proj = project()) beginEdit (nameEdit, proj->name); };
    nameEdit.onTextChange = [this] { renameFromLabel(); };
    styleCaption (bpmCaption, "Tempo del mix", juce::Justification::centredRight);
    styleValue (bpmEdit, true);
    bpmEdit.onEditorShow = [this] { if (auto* proj = project()) beginEdit (bpmEdit, bpmText (proj->effectiveBpm())); };
    bpmEdit.onTextChange = [this] { mixTempoEdited(); };
    styleToggle (keepTemposBtn, "Cada tramo a su tempo");
    keepTemposBtn.onClick = [this]
    {
        if (auto* proj = project())
        {
            proj->keepTempos = keepTemposBtn.getToggleState();
            changed();
        }
    };
    createBtn.setButtonText (tr ("Crear canción..."));
    createBtn.setColour (juce::TextButton::buttonColourId, ui::accent.darker (0.6f));
    createBtn.onClick = [this] { if (owner.onCreateSong) owner.onCreateSong(); };
    closeBtn.setButtonText ("Cerrar");
    closeBtn.onClick = [this] { if (owner.onClose) owner.onClose(); };

    // --- Canciones ---
    styleCaption (sourcesCaption, "Canciones");
    sourcesCaption.setFont (ui::font (14.0f, true));
    sourcesCaption.setColour (juce::Label::textColourId, juce::Colours::white);
    addSourceBtn.setButtonText (tr ("+ Canción"));
    addSourceBtn.onClick = [this] { if (owner.onAddSources) owner.onAddSources(); };
    sourceViewport.setViewedComponent (&sourceList, false);
    sourceViewport.setScrollBarsShown (true, false);
    sourceViewport.setScrollBarThickness (10);

    // --- Vista de la fuente ---
    snapBox.addItem ("A compases", snapBars);
    snapBox.addItem ("A tiempos", snapBeats);
    snapBox.addItem ("Libre", snapFree);
    snapBox.setSelectedId (snapBars, juce::dontSendNotification);
    snapBox.onChange = [this] { snapMode = juce::jlimit (snapBars, snapFree, snapBox.getSelectedId()); };
    playSourceBtn.setButtonText ("Escuchar");
    playSourceBtn.onClick = [this] { playSource(); };
    stopSourceBtn.setButtonText ("Stop");
    stopSourceBtn.onClick = [this] { stop(); };
    addToMixBtn.setButtonText ("Agregar al mix");
    addToMixBtn.setColour (juce::TextButton::buttonColourId, ui::accent.darker (0.6f));
    addToMixBtn.onClick = [this] { addSelectionToMix(); };
    replaceBtn.setButtonText ("Reemplazar el tramo");
    replaceBtn.onClick = [this] { replaceSegmentWithSelection(); };
    // Selección escrita a mano: un clic en el número lo edita (Enter acepta, Esc cancela)
    styleCaption (selCaption, "Compases", juce::Justification::centredRight);
    styleCaption (selToCaption, "a", juce::Justification::centred);
    styleCaption (selInfo, {});
    selInfo.setMinimumHorizontalScale (1.0f);
    for (auto* field : { &selFromEdit, &selToEdit })
    {
        styleValue (*field, true);
        field->setFont (ui::font (12.0f, true));
    }
    selFromEdit.onEditorShow = [this] { beginEdit (selFromEdit, selFromEdit.getText()); };
    selToEdit.onEditorShow = [this] { beginEdit (selToEdit, selToEdit.getText()); };
    selFromEdit.onTextChange = [this] { selectionFieldEdited (true); };
    selToEdit.onTextChange = [this] { selectionFieldEdited (false); };
    sourceScroll.setAutoHide (false);
    sourceScroll.addListener (this);

    // --- Línea del mix ---
    playMixBtn.setButtonText ("Escuchar el mix");
    playMixBtn.onClick = [this] { sourceWasLast = false; playMix (mixCursor); };
    playJoinBtn.setButtonText (tr ("Escuchar la unión"));
    playJoinBtn.onClick = [this] { playJoin(); };
    stopMixBtn.setButtonText ("Stop");
    stopMixBtn.onClick = [this] { stop(); };
    styleToggle (clickBtn, "Click");
    clickBtn.onClick = [this] { if (owner.onClickToggled) owner.onClickToggled (clickBtn.getToggleState()); };
    fitMixBtn.setButtonText ("Ajustar");
    fitMixBtn.onClick = [this] { mixLine.fit(); };
    mixScroll.setAutoHide (false);
    mixScroll.addListener (this);

    // --- Panel del tramo ---
    styleCaption (segCaption, {});
    segCaption.setFont (ui::font (13.0f, true));
    segCaption.setColour (juce::Label::textColourId, juce::Colours::white);
    moveLeftBtn.setButtonText ("<");
    moveLeftBtn.onClick = [this] { moveSegment (selectedSegment(), -1); };
    moveRightBtn.setButtonText (">");
    moveRightBtn.onClick = [this] { moveSegment (selectedSegment(), 1); };
    removeSegBtn.setButtonText ("Quitar");
    removeSegBtn.onClick = [this] { removeSegment (selectedSegment()); };
    styleCaption (startCaption, "Inicio", juce::Justification::centredRight);
    styleCaption (endCaption, "Fin", juce::Justification::centredRight);
    startEarlierBtn.setButtonText (tr ("-1 compás"));
    startEarlierBtn.onClick = [this] { nudgeEdge (true, -1); };
    startLaterBtn.setButtonText (tr ("+1 compás"));
    startLaterBtn.onClick = [this] { nudgeEdge (true, 1); };
    endEarlierBtn.setButtonText (tr ("-1 compás"));
    endEarlierBtn.onClick = [this] { nudgeEdge (false, -1); };
    endLaterBtn.setButtonText (tr ("+1 compás"));
    endLaterBtn.onClick = [this] { nudgeEdge (false, 1); };

    styleCaption (tempoCaption, "Tempo", juce::Justification::centredRight);
    styleValue (segTempoEdit, true);
    segTempoEdit.onEditorShow = [this]
    {
        if (auto* seg = currentSegment())
            beginEdit (segTempoEdit, seg->playBpm > 0.0 ? bpmText (seg->playBpm) : juce::String());
    };
    segTempoEdit.onTextChange = [this] { segmentTempoEdited(); };
    styleCaption (toneCaption, "Tono", juce::Justification::centredRight);
    toneDownBtn.setButtonText ("-");
    toneDownBtn.onClick = [this] { transposeBy (-1); };
    styleValue (toneValue, false);
    toneUpBtn.setButtonText ("+");
    toneUpBtn.onClick = [this] { transposeBy (1); };
    styleCaption (gainCaption, "Ganancia", juce::Justification::centredRight);
    styleValue (gainEdit, true);
    gainEdit.onEditorShow = [this]
    {
        if (auto* seg = currentSegment())
            beginEdit (gainEdit, decimal (seg->gainDb, 1));
    };
    gainEdit.onTextChange = [this] { segmentGainEdited(); };
    styleCaption (fadeCaption, "Fundido", juce::Justification::centredRight);
    fadeBox.addItem ("Corte", 1);
    fadeBox.addItem ("1 tiempo", 2);
    fadeBox.addItem ("2 tiempos", 3);
    fadeBox.addItem (tr ("1 compás"), 4);
    fadeBox.addItem ("2 compases", 5);
    fadeBox.onChange = [this] { fadeChosen(); };
    styleCaption (nameCaption, "Nombre", juce::Justification::centredRight);
    styleValue (labelEdit, true);
    labelEdit.setJustificationType (juce::Justification::centredLeft);
    labelEdit.onEditorShow = [this]
    {
        auto* seg = currentSegment();
        if (seg != nullptr && validSource (seg->source))
            beginEdit (labelEdit, seg->label.isNotEmpty() ? seg->label : project()->sources[(size_t) seg->source].name);
    };
    labelEdit.onTextChange = [this] { segmentLabelEdited(); };

    for (auto* b : std::initializer_list<juce::TextButton*> { &moveLeftBtn, &moveRightBtn, &removeSegBtn, &startEarlierBtn,
                                                               &startLaterBtn, &endEarlierBtn, &endLaterBtn, &toneDownBtn, &toneUpBtn })
        b->setLookAndFeel (&compactLook);

    statusLabel.setFont (ui::font (12.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setInterceptsMouseClicks (false, false);

    for (auto* c : std::initializer_list<juce::Component*> {
             &mixCaption, &nameEdit, &bpmCaption, &bpmEdit, &keepTemposBtn, &createBtn, &closeBtn,
             &sourcesCaption, &addSourceBtn, &sourceViewport,
             &snapBox, &playSourceBtn, &stopSourceBtn, &addToMixBtn, &replaceBtn, &sourceWave, &sourceScroll,
             &selCaption, &selFromEdit, &selToCaption, &selToEdit, &selInfo,
             &playMixBtn, &playJoinBtn, &stopMixBtn, &clickBtn, &fitMixBtn, &mixLine, &mixScroll,
             &segCaption, &moveLeftBtn, &moveRightBtn, &removeSegBtn, &startCaption, &startEarlierBtn, &startLaterBtn,
             &endCaption, &endEarlierBtn, &endLaterBtn, &tempoCaption, &segTempoEdit, &toneCaption, &toneDownBtn,
             &toneValue, &toneUpBtn, &gainCaption, &gainEdit, &fadeCaption, &fadeBox, &nameCaption, &labelEdit,
             &statusLabel })
        owner.addAndMakeVisible (c);
}

MixEditor::Impl::~Impl()
{
    sourceScroll.removeListener (this);
    mixScroll.removeListener (this);
    for (auto* b : std::initializer_list<juce::TextButton*> { &moveLeftBtn, &moveRightBtn, &removeSegBtn, &startEarlierBtn,
                                                               &startLaterBtn, &endEarlierBtn, &endLaterBtn, &toneDownBtn, &toneUpBtn })
        b->setLookAndFeel (nullptr);
}

//==============================================================================
// Accesos y textos

const SourcePeaks* MixEditor::Impl::peaksFor (int source) const
{
    if (! validSource (source))
        return nullptr;
    const auto it = peaks.find (project()->sources[(size_t) source].fileName);
    return it != peaks.end() && it->second.binsPerSecond > 0 ? &it->second : nullptr;
}

juce::String MixEditor::Impl::statusFor (int source) const
{
    if (! validSource (source))
        return {};
    const auto it = statuses.find (project()->sources[(size_t) source].fileName);
    return it != statuses.end() ? it->second : juce::String();
}

juce::String MixEditor::Impl::sourceStateText (int source) const
{
    if (! validSource (source))
        return {};
    const auto status = statusFor (source);
    return status.isNotEmpty() ? status : analysisText (project()->sources[(size_t) source].analysis);
}

double MixEditor::Impl::sourceLength (int source) const
{
    if (! validSource (source))
        return 0.0;
    const double len = project()->sources[(size_t) source].length;
    if (len > 0.0)
        return len;
    if (auto* pk = peaksFor (source))
        return (double) (pk->minMax.size() / 2) / pk->binsPerSecond;
    return 0.0;
}

int MixEditor::Impl::segmentsUsing (int source) const
{
    auto* proj = project();
    if (proj == nullptr)
        return 0;
    return (int) std::count_if (proj->segments.begin(), proj->segments.end(),
                                [source] (const MixSegment& s) { return s.source == source; });
}

int MixEditor::Impl::segmentMeter (const MixSegment& seg) const
{
    if (! validSource (seg.source))
        return 4;
    const auto& a = project()->sources[(size_t) seg.source].analysis;
    return a.beats.empty() ? 4 : juce::jlimit (1, 7, a.meter);
}

// "3. Coro · c. 17-32"
juce::String MixEditor::Impl::segmentTitle (int index) const
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size())
        return {};
    const auto& seg = proj->segments[(size_t) index];
    if (! validSource (seg.source))
        return juce::String (index + 1) + ". ?";
    const auto& src = proj->sources[(size_t) seg.source];
    auto text = juce::String (index + 1) + ". " + (seg.label.isNotEmpty() ? seg.label : src.name);
    const auto bars = barRange (src.analysis, seg.start, seg.end);
    if (bars.isNotEmpty())
        text += dot() + bars;
    return text;
}

// "96,3 -> 100 BPM  +2 st  -3,0 dB"
juce::String MixEditor::Impl::segmentDetail (int index) const
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size() || index >= (int) placements.size())
        return {};
    const auto& seg = proj->segments[(size_t) index];
    const auto& p = placements[(size_t) index];
    juce::String text;
    if (! validSource (seg.source) || proj->sources[(size_t) seg.source].analysis.beats.empty())
        text = "Sin analizar";
    else if (std::abs (p.ratio - 1.0) > 1.0e-4)
        text = bpmText (p.srcBpm) + " -> " + bpmText (p.playBpm) + " BPM";
    else
        text = bpmText (p.srcBpm) + " BPM";
    if (seg.transpose != 0)
        text += juce::String::formatted ("  %+d st", seg.transpose);
    if (std::abs (seg.gainDb) >= 0.05f)
        text += "  " + gainText (seg.gainDb);
    if (joinOffBeat (index))
        text += tr ("  unión fuera de tiempo");
    return text;
}

// Un borde de tramo o de la selección cae en un tiempo detectado (sin análisis no hay rejilla que
// respetar: cuenta como que sí)
bool MixEditor::Impl::edgeOnBeat (int source, double seconds) const
{
    if (! validSource (source))
        return true;
    const auto& a = project()->sources[(size_t) source].analysis;
    if (a.beats.empty())
        return true;
    return std::abs (mix::snapToBeat (a, seconds, false) - seconds) <= onBeatTolerance;
}

// La unión del tramo `index` con el anterior no cae en la rejilla: el fin del anterior o el inicio de
// este no está en un tiempo (quedaron en el final o el inicio de la canción, o se eligieron "Libre").
// Se calcula con la ubicación (recomputeLayout), no en cada repintado del cabezal.
bool MixEditor::Impl::joinOffBeat (int index) const
{
    return index > 0 && index < (int) offBeatJoins.size() && offBeatJoins[(size_t) index];
}

bool MixEditor::Impl::selectionOffBeat() const
{
    return hasSelection() && (! edgeOnBeat (selectedSource(), selStart) || ! edgeOnBeat (selectedSource(), selEnd));
}

bool MixEditor::Impl::sourceHasBars() const
{
    auto* src = currentSource();
    return src != nullptr && mix::barCount (src->analysis) > 0;
}

// El compás donde empieza la selección (0 = antes del primero) y el último que toca. El inicio se mira 0,1 s
// más adelante: el corte ajustado a la transiente queda unos ms antes del primer tiempo del compás
int MixEditor::Impl::selectionFirstBar() const
{
    auto* src = currentSource();
    return src != nullptr ? mix::barNumberAt (src->analysis, juce::jmin (selStart + 0.1, selEnd)) : 0;
}

int MixEditor::Impl::selectionLastBar() const
{
    auto* src = currentSource();
    return src != nullptr ? mix::barNumberAt (src->analysis, juce::jmax (selStart, selEnd - 0.1)) : 0;
}

// Lo que sigue a los campos: "· 16 compases · 0:38" (y si un borde no cae en un tiempo, cuál)
juce::String MixEditor::Impl::selectionSuffix() const
{
    auto* src = currentSource();
    if (src == nullptr)
        return {};
    if (! hasSelection())
        return tr ("· escribe o arrastra sobre la onda");
    juce::String text;
    if (sourceHasBars())
    {
        const int bars = mix::barsBetween (src->analysis, selStart, selEnd);
        text = dot().trimStart() + juce::String (bars) + (bars == 1 ? tr (" compás") : juce::String (" compases")) + dot();
    }
    else
        text = dot().trimStart();
    text += clockText (selEnd - selStart);
    const bool startOff = ! edgeOnBeat (selectedSource(), selStart);
    const bool endOff = ! edgeOnBeat (selectedSource(), selEnd);
    if (startOff || endOff)
        text += dot() + (startOff && endOff ? "inicio y fin" : (startOff ? "inicio" : "fin")) + juce::String (" fuera de tiempo");
    return text;
}

// "12 tramos · 4:35 · 100 BPM" (sin el tempo si no cabe)
juce::String MixEditor::Impl::mixSummary (bool withTempo) const
{
    auto* proj = project();
    if (proj == nullptr)
        return {};
    const int n = (int) proj->segments.size();
    if (n == 0)
        return "Sin tramos";
    auto text = juce::String (n) + (n == 1 ? " tramo" : " tramos") + dot() + clockText (mixLength);
    if (withTempo)
        text += dot() + (proj->keepTempos ? juce::String ("cada tramo a su tempo") : bpmText (proj->effectiveBpm()) + " BPM");
    return text;
}

//==============================================================================
// Estado general

void MixEditor::Impl::projectChanged()
{
    auto* proj = project();
    const auto folder = proj != nullptr ? proj->folder : juce::File();
    if (folder != shownFolder)
    {
        // Otro mix: las formas de onda y estados guardados eran de otras fuentes
        peaks.clear();
        statuses.clear();
        shownFolder = folder;
    }
    selectedSourceFile = {};
    selStart = selEnd = 0.0;
    sourceCursor = mixCursor = 0.0;
    sourceWasLast = false;
    playKind = 0;
    playingSource = -1;
    isPlaying = false;
    knownSourceCount = proj != nullptr ? (int) proj->sources.size() : 0;
    recomputeLayout();
    mixLine.fit();
    mixLine.invalidate();
    if (proj != nullptr && ! proj->sources.empty())
        chooseSource (0, true);
    else
        sourceWave.invalidate();
    updateControls();
    owner.repaint();
}

void MixEditor::Impl::refreshFromProject()
{
    auto* proj = project();
    if (proj == nullptr)
    {
        updateControls();
        owner.repaint();
        return;
    }

    // Cachés de fuentes que ya no están
    std::set<juce::String> files;
    for (auto& s : proj->sources)
        files.insert (s.fileName);
    for (auto it = peaks.begin(); it != peaks.end();)
        it = files.count (it->first) > 0 ? std::next (it) : peaks.erase (it);
    for (auto it = statuses.begin(); it != statuses.end();)
        it = files.count (it->first) > 0 ? std::next (it) : statuses.erase (it);

    // La fuente elegida se sigue por su archivo (quitar una fuente reindexa las demás)
    const int n = (int) proj->sources.size();
    int index = selectedSourceFile.isNotEmpty() ? sourceIndexOf (selectedSourceFile) : -1;
    const bool sameSource = index >= 0;
    if (index < 0 && n > 0)
        index = n > knownSourceCount ? juce::jlimit (0, n - 1, knownSourceCount)            // la primera agregada
                                     : juce::jlimit (0, n - 1, owner.selectedSource);
    // Si se quitó una fuente, los índices de los tramos cambiaron: no se sigue ninguno
    if (n < knownSourceCount)
        owner.selectedSegment = -1;
    if (owner.selectedSegment >= (int) proj->segments.size())
        owner.selectedSegment = (int) proj->segments.size() - 1;
    knownSourceCount = n;

    if (sameSource)
    {
        owner.selectedSource = index;
        const double len = sourceLength (index);
        if (len > 0.0)
        {
            selStart = juce::jlimit (0.0, len, selStart);
            selEnd = juce::jlimit (0.0, len, selEnd);
            sourceCursor = juce::jlimit (0.0, len, sourceCursor);
        }
    }
    else
    {
        owner.selectedSource = -2;   // fuerza el cambio en chooseSource
        chooseSource (index, true);
    }

    recomputeLayout();
    mixLine.layoutChanged();
    sourceWave.lengthChanged();
    sourceWave.invalidate();
    updateControls();
    owner.repaint();
}

// Ubicación de los tramos, largo del mix y uniones fuera de tiempo; el cabezal del mix no queda más
// allá del final
void MixEditor::Impl::recomputeLayout()
{
    auto* proj = project();
    offBeatJoins.clear();
    if (proj == nullptr)
    {
        placements.clear();
        mixLength = 0.0;
    }
    else
    {
        placements = mix::layout (*proj);
        mixLength = placements.empty() ? 0.0 : placements.back().outEnd;
        offBeatJoins.assign (proj->segments.size(), false);
        for (size_t i = 1; i < proj->segments.size(); ++i)
        {
            const auto& before = proj->segments[i - 1];
            const auto& seg = proj->segments[i];
            offBeatJoins[i] = ! edgeOnBeat (before.source, before.end) || ! edgeOnBeat (seg.source, seg.start);
        }
    }
    mixCursor = juce::jlimit (0.0, juce::jmax (0.0, mixLength), mixCursor);
}

// Una edición hecha aquí: recalcular, redibujar y avisar al dueño
void MixEditor::Impl::changed()
{
    recomputeLayout();
    mixLine.layoutChanged();
    sourceWave.repaint();
    sourceList.repaint();
    updateControls();
    if (owner.onChanged)
        owner.onChanged();
}

void MixEditor::Impl::updateControls()
{
    auto* proj = project();
    for (auto* c : owner.getChildren())
        c->setVisible (proj != nullptr);
    if (proj == nullptr)
    {
        owner.repaint();
        return;
    }

    if (! nameEdit.isBeingEdited())
        nameEdit.setText (proj->name, juce::dontSendNotification);
    if (! bpmEdit.isBeingEdited())
        bpmEdit.setText (bpmText (proj->effectiveBpm()) + " BPM", juce::dontSendNotification);
    bpmEdit.setColour (juce::Label::textColourId, proj->keepTempos ? juce::Colours::grey : juce::Colours::white);
    keepTemposBtn.setToggleState (proj->keepTempos, juce::dontSendNotification);
    createBtn.setEnabled (! proj->segments.empty() && ! busy);

    updateListSize();
    sourceList.repaint();
    updateSourceButtons();

    const bool hasSegments = ! proj->segments.empty();
    playMixBtn.setEnabled (hasSegments);
    playJoinBtn.setEnabled (currentSegment() != nullptr);
    fitMixBtn.setEnabled (hasSegments);
    updatePanel();
    owner.repaint (mixInfoArea);
}

void MixEditor::Impl::updateSourceButtons()
{
    const bool hasSource = currentSource() != nullptr;
    snapBox.setEnabled (hasSource);
    playSourceBtn.setEnabled (hasSource);
    addToMixBtn.setEnabled (hasSource && hasSelection());
    auto* seg = currentSegment();
    replaceBtn.setEnabled (hasSource && hasSelection() && seg != nullptr && seg->source == selectedSource()
                           && (std::abs (seg->start - selStart) > 1.0e-6 || std::abs (seg->end - selEnd) > 1.0e-6));
    updateSelectionFields();
    owner.repaint (sourceInfoArea);
}

// Campos de la selección, a la derecha de la cabecera de la fuente. Con compases: "Compases [8] a [16]"; sin
// análisis: "Desde [1:05] a [1:32]". Vacíos sin selección (se puede escribir igual). Se ubican según el texto.
void MixEditor::Impl::updateSelectionFields()
{
    auto* src = currentSource();
    const bool show = src != nullptr && ! sourceInfoArea.isEmpty();
    for (auto* c : std::initializer_list<juce::Component*> { &selCaption, &selFromEdit, &selToCaption, &selToEdit, &selInfo })
        c->setVisible (show);
    if (! show)
    {
        selectionFieldsLeft = sourceInfoArea.getRight();
        return;
    }
    const bool bars = sourceHasBars();
    const bool any = hasSelection();
    selCaption.setText (bars ? "Compases" : "Desde", juce::dontSendNotification);
    if (! selFromEdit.isBeingEdited())
        selFromEdit.setText (! any ? juce::String() : bars ? juce::String (selectionFirstBar()) : clockText (selStart),
                             juce::dontSendNotification);
    if (! selToEdit.isBeingEdited())
        selToEdit.setText (! any ? juce::String() : bars ? juce::String (selectionLastBar()) : clockText (selEnd),
                           juce::dontSendNotification);
    const auto suffix = selectionSuffix();
    selInfo.setText (suffix, juce::dontSendNotification);
    selInfo.setColour (juce::Label::textColourId, ! any ? juce::Colours::grey
                                                        : (selectionOffBeat() ? offBeatColour : juce::Colours::white));
    selInfo.setFont (ui::font (12.0f, any));

    // De derecha a izquierda: texto, campo final, "a", campo inicial, "Compases"
    auto row = sourceInfoArea;
    const int fieldWidth = bars ? 40 : 52;
    const int infoWidth = juce::jmin (row.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (selInfo.getFont(), suffix) + 10);
    selInfo.setBounds (row.removeFromRight (infoWidth));
    selToEdit.setBounds (row.removeFromRight (fieldWidth).reduced (0, 1));
    selToCaption.setBounds (row.removeFromRight (16));
    selFromEdit.setBounds (row.removeFromRight (fieldWidth).reduced (0, 1));
    selCaption.setBounds (row.removeFromRight (bars ? 62 : 44));
    selectionFieldsLeft = row.getRight();
}

// Un campo de la selección se editó: compases (enteros; 0 = desde el inicio de la canción) o tiempos (m:ss o
// segundos). Si el inicio queda después del fin (o al revés), el otro borde lo acompaña; sin selección, el campo
// escrito da las dos puntas (un compás). Con compases los bordes se ajustan a la transiente, como al arrastrar.
void MixEditor::Impl::selectionFieldEdited (bool startField)
{
    auto& field = startField ? selFromEdit : selToEdit;
    auto* src = currentSource();
    const auto text = field.getText().trim();
    if (src == nullptr || text.isEmpty() || editUnchanged (field))
    {
        updateSourceButtons();
        return;
    }
    const double len = sourceLength (selectedSource());
    if (sourceHasBars())
    {
        if (! text.containsOnly ("0123456789"))
        {
            updateSourceButtons();
            return;
        }
        const int value = text.getIntValue();
        int first = hasSelection() ? selectionFirstBar() : value;
        int last = hasSelection() ? selectionLastBar() : value;
        if (startField)
            first = value, last = juce::jmax (last, value);
        else
            last = value, first = juce::jmin (first, value);
        double a = 0.0, b = 0.0;
        if (mix::barRange (src->analysis, len, first, last, a, b))
            applySelection (a, b, true, true);
        else
            updateSourceButtons();
        return;
    }
    // Tiempo: "1:05", "1:05,5" o "65"
    double seconds = 0.0;
    const auto clean = text.replaceCharacter (',', '.');
    if (clean.containsChar (':'))
        seconds = clean.upToFirstOccurrenceOf (":", false, false).getIntValue() * 60.0
                + clean.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
    else
        seconds = clean.getDoubleValue();
    if (! clean.containsOnly ("0123456789.:") || ! std::isfinite (seconds))
    {
        updateSourceButtons();
        return;
    }
    seconds = juce::jlimit (0.0, len, seconds);
    double a = hasSelection() ? selStart : seconds, b = hasSelection() ? selEnd : seconds;
    if (startField)
        a = seconds, b = juce::jmax (b, juce::jmin (len, seconds + 1.0));
    else
        b = seconds, a = juce::jmin (a, juce::jmax (0.0, seconds - 1.0));
    applySelection (a, b, false, false);
}

void MixEditor::Impl::updatePanel()
{
    auto* proj = project();
    auto* seg = currentSegment();
    const bool on = proj != nullptr && seg != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &segCaption, &moveLeftBtn, &moveRightBtn, &removeSegBtn,
                                                              &startCaption, &startEarlierBtn, &startLaterBtn, &endCaption,
                                                              &endEarlierBtn, &endLaterBtn, &tempoCaption, &segTempoEdit,
                                                              &toneCaption, &toneDownBtn, &toneValue, &toneUpBtn, &gainCaption,
                                                              &gainEdit, &fadeCaption, &fadeBox, &nameCaption, &labelEdit })
        c->setEnabled (on);
    if (! on)
    {
        segCaption.setText ("Sin tramo", juce::dontSendNotification);
        for (auto* l : std::initializer_list<juce::Label*> { &segTempoEdit, &toneValue, &gainEdit, &labelEdit })
            l->setText ({}, juce::dontSendNotification);
        fadeBox.setSelectedId (0, juce::dontSendNotification);
        return;
    }

    const int index = selectedSegment(), n = (int) proj->segments.size();
    segCaption.setText ("Tramo " + juce::String (index + 1) + " de " + juce::String (n), juce::dontSendNotification);
    moveLeftBtn.setEnabled (index > 0);
    moveRightBtn.setEnabled (index + 1 < n);
    const bool analyzed = validSource (seg->source) && ! proj->sources[(size_t) seg->source].analysis.beats.empty();
    for (auto* b : std::initializer_list<juce::Button*> { &startEarlierBtn, &startLaterBtn, &endEarlierBtn, &endLaterBtn })
        b->setEnabled (analyzed);

    if (! segTempoEdit.isBeingEdited())
        segTempoEdit.setText (seg->playBpm > 0.0 ? bpmText (seg->playBpm) + " BPM"
                                                 : juce::String (proj->keepTempos ? "original" : "del mix"),
                              juce::dontSendNotification);
    segTempoEdit.setColour (juce::Label::textColourId, seg->playBpm > 0.0 ? ui::accent : juce::Colours::white);
    toneValue.setText (seg->transpose == 0 ? juce::String ("0") : juce::String::formatted ("%+d st", seg->transpose),
                       juce::dontSendNotification);
    toneValue.setColour (juce::Label::textColourId, seg->transpose != 0 ? ui::accent : juce::Colours::white);
    toneDownBtn.setEnabled (seg->transpose > -12);
    toneUpBtn.setEnabled (seg->transpose < 12);
    if (! gainEdit.isBeingEdited())
        gainEdit.setText (gainText (seg->gainDb), juce::dontSendNotification);

    // Fundido: el valor que coincida con una opción; si no (escrito a mano en mix.json), su texto
    const int meter = segmentMeter (*seg);
    const double values[] = { 0.0, 1.0, 2.0, (double) meter, 2.0 * meter };
    int fadeId = 0;
    for (int k = 0; k < 5; ++k)
        if (std::abs (seg->fadeBeats - values[k]) < 1.0e-6)
        {
            fadeId = k + 1;
            break;
        }
    if (fadeId > 0)
        fadeBox.setSelectedId (fadeId, juce::dontSendNotification);
    else
        fadeBox.setText (decimal (seg->fadeBeats, 1) + " tiempos", juce::dontSendNotification);
    fadeBox.setEnabled (index > 0);   // el primer tramo no tiene unión con uno anterior
    fadeCaption.setEnabled (index > 0);

    // Nombre: vacío = el de la canción (se muestra en gris)
    if (! labelEdit.isBeingEdited())
    {
        const bool own = seg->label.isNotEmpty() || ! validSource (seg->source);
        labelEdit.setText (own ? seg->label : proj->sources[(size_t) seg->source].name, juce::dontSendNotification);
        labelEdit.setColour (juce::Label::textColourId, own ? juce::Colours::white : juce::Colours::grey);
    }
}

void MixEditor::Impl::updateListSize()
{
    auto* proj = project();
    const int n = proj != nullptr ? (int) proj->sources.size() : 0;
    const int w = juce::jmax (10, sourceViewport.getWidth() - sourceViewport.getScrollBarThickness());
    sourceList.setSize (w, juce::jmax (sourceViewport.getHeight(), n * SourceListView::rowHeight));
}

void MixEditor::Impl::revealSourceRow (int index)
{
    if (index < 0)
        return;
    const int y = index * SourceListView::rowHeight;
    const auto view = sourceViewport.getViewArea();
    if (y < view.getY())
        sourceViewport.setViewPosition (0, y);
    else if (y + SourceListView::rowHeight > view.getBottom())
        sourceViewport.setViewPosition (0, y + SourceListView::rowHeight - view.getHeight());
}

void MixEditor::Impl::updateSourceScroll()
{
    sourceScroll.setRangeLimits (0.0, sourceWave.length(), juce::dontSendNotification);
    sourceScroll.setCurrentRange (sourceWave.viewStart, sourceWave.visibleSeconds(), juce::dontSendNotification);
}

void MixEditor::Impl::updateMixScroll()
{
    mixScroll.setRangeLimits (0.0, mixLine.length(), juce::dontSendNotification);
    mixScroll.setCurrentRange (mixLine.viewStart, mixLine.visibleSeconds(), juce::dontSendNotification);
}

void MixEditor::Impl::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &sourceScroll)
        sourceWave.setView (newRangeStart, sourceWave.pixelsPerSecond);
    else if (bar == &mixScroll)
        mixLine.setView (newRangeStart, mixLine.pixelsPerSecond);
}

// Reparto: cabecera arriba (36 px), estado al pie (22 px), canciones a la izquierda (230 px) y, a la
// derecha, la vista de la fuente (56 % del alto que queda), la línea del mix (el resto) y el panel del
// tramo elegido (70 px, dos filas). En ventanas angostas (la mínima de la app deja unos 700 px) la
// columna de canciones baja a 160 px y el panel pasa a tres filas, así ningún control queda ilegible;
// en las filas ceden primero el nombre del mix, los espacios y la franja del resumen.
void MixEditor::Impl::layout()
{
    const bool narrow = owner.getWidth() < narrowWidth;
    auto r = owner.getLocalBounds();
    headerArea = r.removeFromTop (headerHeight);
    layoutRow (headerArea.reduced (10, 5), { { &mixCaption, 36 }, { &nameEdit, 220, false, 100 }, { nullptr, 12, false, 4 },
                                             { &bpmCaption, 94, false, 84 }, { &bpmEdit, 86, false, 76 },
                                             { &keepTemposBtn, 168, false, 150 }, { nullptr, 12, true },
                                             { &createBtn, 128, false, 120 }, { &closeBtn, 76, false, 64 } });
    statusLabel.setBounds (r.removeFromBottom (statusHeight).reduced (12, 0));
    r.reduce (6, 6);

    leftArea = r.removeFromLeft (narrow ? narrowSourcesWidth : juce::jlimit (180, sourcesWidth, (int) (owner.getWidth() * 0.22)));
    r.removeFromLeft (6);
    {
        auto left = leftArea.reduced (6);
        layoutRow (left.removeFromTop (26), { { &sourcesCaption, 90, true, 70 }, { &addSourceBtn, 96, false, 76 } });
        left.removeFromTop (6);
        sourceViewport.setBounds (left);
        updateListSize();
    }

    const bool threeRows = r.getWidth() - 16 < panelTwoRowsWidth;
    panelArea = r.removeFromBottom (threeRows ? threeRowsPanelHeight : panelHeight);
    r.removeFromBottom (6);
    sourceArea = r.removeFromTop ((int) std::round ((r.getHeight() - 6) * 0.56));
    r.removeFromTop (6);
    mixArea = r;

    {
        auto s = sourceArea.reduced (8, 6);
        sourceInfoArea = s.removeFromTop (20);
        s.removeFromTop (4);
        layoutRow (s.removeFromTop (26), { { &snapBox, 118, false, 104 }, { nullptr, 4 }, { &playSourceBtn, 80, false, 72 },
                                           { &stopSourceBtn, 56, false, 48 }, { nullptr, 10, true },
                                           { &addToMixBtn, 120, false, 108 }, { &replaceBtn, 150, false, 132 } });
        s.removeFromTop (6);
        sourceScroll.setBounds (s.removeFromBottom (12));
        sourceWave.setBounds (s);
        updateSelectionFields();
    }
    {
        auto m = mixArea.reduced (8, 6);
        auto bar = m.removeFromTop (26);
        layoutRow (bar, { { nullptr, 160, true, 110 }, { &playMixBtn, 116, false, 104 }, { &playJoinBtn, 128, false, 116 },
                          { &stopMixBtn, 56, false, 48 }, { &clickBtn, 60, false, 52 }, { &fitMixBtn, 70, false, 62 } });
        mixInfoArea = bar.withRight (juce::jmax (bar.getX(), playMixBtn.getX() - 8));
        m.removeFromTop (6);
        mixScroll.setBounds (m.removeFromBottom (12));
        mixLine.setBounds (m);
    }

    auto p = panelArea.reduced (8, 6);
    if (! threeRows)
    {
        auto row1 = p.removeFromTop (26);
        p.removeFromTop (juce::jmax (0, p.getHeight() - 26));
        layoutRow (row1, { { &segCaption, 80 }, { &moveLeftBtn, 26 }, { &moveRightBtn, 26 }, { &removeSegBtn, 56 }, { nullptr, 6 },
                           { &startCaption, 38 }, { &startEarlierBtn, 66 }, { &startLaterBtn, 66 }, { nullptr, 6 },
                           { &endCaption, 26 }, { &endEarlierBtn, 66 }, { &endLaterBtn, 66 }, { nullptr, 6 },
                           { &nameCaption, 48 }, { &labelEdit, 70, true, 60 } }, 3);
        layoutRow (p, { { &tempoCaption, 46 }, { &segTempoEdit, 80 }, { nullptr, 6 },
                        { &toneCaption, 36 }, { &toneDownBtn, 26 }, { &toneValue, 46 }, { &toneUpBtn, 26 }, { nullptr, 6 },
                        { &gainCaption, 60 }, { &gainEdit, 70 }, { nullptr, 6 },
                        { &fadeCaption, 54 }, { &fadeBox, 110 }, { nullptr, 0, true } }, 3);
        return;
    }
    // Tres filas: tramo y nombre; cortes y fundido; tempo, tono y ganancia
    const int rowHeight = 26, rowGap = juce::jmax (0, (p.getHeight() - 3 * rowHeight) / 2);
    auto row1 = p.removeFromTop (rowHeight);
    p.removeFromTop (rowGap);
    auto row2 = p.removeFromTop (rowHeight);
    p.removeFromTop (rowGap);
    auto row3 = p.removeFromTop (rowHeight);
    layoutRow (row1, { { &segCaption, 84, false, 70 }, { &moveLeftBtn, 26 }, { &moveRightBtn, 26 }, { &removeSegBtn, 56 },
                       { nullptr, 10, false, 4 }, { &nameCaption, 48 }, { &labelEdit, 120, true, 90 } }, 3);
    layoutRow (row2, { { &startCaption, 38, false, 34 }, { &startEarlierBtn, 66 }, { &startLaterBtn, 66 }, { nullptr, 6, false, 2 },
                       { &endCaption, 26, false, 22 }, { &endEarlierBtn, 66 }, { &endLaterBtn, 66 }, { nullptr, 6, false, 2 },
                       { &fadeCaption, 54, false, 48 }, { &fadeBox, 110, false, 96 }, { nullptr, 0, true } }, 3);
    layoutRow (row3, { { &tempoCaption, 46 }, { &segTempoEdit, 80 }, { nullptr, 6 },
                       { &toneCaption, 36 }, { &toneDownBtn, 26 }, { &toneValue, 46 }, { &toneUpBtn, 26 }, { nullptr, 6 },
                       { &gainCaption, 60 }, { &gainEdit, 70 }, { nullptr, 0, true } }, 3);
}

void MixEditor::Impl::paintFrame (juce::Graphics& g)
{
    g.setColour (ui::panel);
    g.fillRect (headerArea);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
    for (auto area : { leftArea, sourceArea, mixArea, panelArea })
    {
        g.setColour (ui::panel);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
    }

    // Fuente elegida: nombre y estado a la izquierda, la selección a la derecha (los textos solo si
    // hay que repintar esa franja)
    auto* src = currentSource();
    if (src != nullptr && g.clipRegionIntersects (sourceInfoArea))
    {
        auto info = sourceInfoArea;
        const auto nameFont = ui::font (14.0f, true);
        const int nameWidth = juce::jmin (info.getWidth() / 3, juce::GlyphArrangement::getStringWidthInt (nameFont, src->name) + 12);
        g.setFont (nameFont);
        g.setColour (ui::trackColour (selectedSource()));
        g.drawText (src->name, info.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);

        // La selección son componentes (campos editables) a la derecha: el estado se dibuja antes
        info.setRight (juce::jmax (info.getX(), juce::jmin (info.getRight(), selectionFieldsLeft - 6)));

        g.setFont (ui::font (12.0f));
        g.setColour (statusFor (selectedSource()).isNotEmpty() ? ui::accent : juce::Colours::lightgrey);
        g.drawText (sourceStateText (selectedSource()), info.reduced (4, 0), juce::Justification::centredLeft, true);
    }

    // Resumen del mix (en ventanas angostas, sin el tempo, que también está en la cabecera)
    if (! g.clipRegionIntersects (mixInfoArea))
        return;
    const auto summaryFont = ui::font (13.0f, true);
    auto summary = mixSummary (true);
    if (juce::GlyphArrangement::getStringWidthInt (summaryFont, summary) > mixInfoArea.getWidth())
        summary = mixSummary (false);
    g.setFont (summaryFont);
    g.setColour (juce::Colours::white);
    g.drawFittedText (summary, mixInfoArea, juce::Justification::centredLeft, 1, 0.8f);
}

void MixEditor::Impl::setPeaks (int source, std::vector<float> minMax, int binsPerSecond)
{
    if (! validSource (source))
        return;
    SourcePeaks p;
    p.minMax = std::move (minMax);
    p.binsPerSecond = binsPerSecond;
    for (auto v : p.minMax)
        p.maxAbs = juce::jmax (p.maxAbs, std::abs (v));
    peaks[project()->sources[(size_t) source].fileName] = std::move (p);
    if (source == selectedSource())
    {
        // El largo pudo quedar conocido recién ahora (fuente sin largo guardado): si se veía entera,
        // se sigue viendo entera
        sourceWave.lengthChanged();
        sourceWave.invalidate();
    }
    if (segmentsUsing (source) > 0)
        mixLine.invalidate();
}

// El dueño lo llama en cada tick mientras analiza: solo se repinta si el texto cambió, y solo la
// fila de esa fuente (y la barra de la vista si es la elegida)
void MixEditor::Impl::setStatusOf (int source, const juce::String& text)
{
    if (! validSource (source))
        return;
    const auto& file = project()->sources[(size_t) source].fileName;
    const auto it = statuses.find (file);
    if ((it != statuses.end() ? it->second : juce::String()) == text)
        return;
    if (text.isEmpty())
        statuses.erase (file);
    else
        statuses[file] = text;
    sourceList.repaint (0, source * SourceListView::rowHeight, sourceList.getWidth(), SourceListView::rowHeight);
    if (source == selectedSource())
        owner.repaint (sourceInfoArea);
}

// Llamado unas 30 veces por segundo: solo se repintan las franjas de los cabezales
void MixEditor::Impl::setPlayback (int kind, int source, double seconds, bool playingNow)
{
    if (kind == playKind && source == playingSource && playingNow == isPlaying && std::abs (seconds - playSeconds) < 1.0e-9)
        return;
    const bool wasSource = playKind == 1 && playingSource == selectedSource();
    const bool wasMix = playKind == 2;
    const double old = playSeconds;
    playKind = kind;
    playingSource = source;
    playSeconds = seconds;
    isPlaying = playingNow;
    if (wasSource)
        sourceWave.repaintAt (old);
    if (wasMix)
        mixLine.repaintAt (old);
    if (kind == 1 && source == selectedSource())
    {
        if (playingNow)
            sourceWave.follow (seconds);
        sourceWave.repaintAt (seconds);
    }
    else if (kind == 2)
    {
        if (playingNow)
            mixLine.follow (seconds);
        mixLine.repaintAt (seconds);
    }
}

bool MixEditor::Impl::handleKey (const juce::KeyPress& key)
{
    auto* proj = project();
    if (proj == nullptr)
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey)
    {
        if (isPlaying)
            stop();
        else if (sourceWasLast && currentSource() != nullptr)
            playSource();
        else if (! proj->segments.empty())
            playMix (mixCursor);
        else if (currentSource() != nullptr)
            playSource();
        else
            return false;
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        if (! isPlaying)
            return false;
        stop();
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (currentSegment() == nullptr)
            return false;
        removeSegment (selectedSegment());
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const int n = (int) proj->segments.size();
        if (n == 0)
            return false;
        const bool left = code == juce::KeyPress::leftKey;
        const int current = selectedSegment();
        const int target = current < 0 ? (left ? n - 1 : 0) : juce::jlimit (0, n - 1, current + (left ? -1 : 1));
        sourceWasLast = false;
        chooseSegment (target);
        return true;
    }
    return false;
}

//==============================================================================
// Fuentes y selección

void MixEditor::Impl::chooseSource (int index, bool fitView)
{
    if (! validSource (index))
        index = -1;
    const bool switched = index != owner.selectedSource;
    owner.selectedSource = index;
    selectedSourceFile = index >= 0 ? project()->sources[(size_t) index].fileName : juce::String();
    if (switched)
    {
        selStart = selEnd = 0.0;
        sourceCursor = 0.0;
    }
    if (switched || fitView)
        sourceWave.fit();
    sourceWave.invalidate();
    revealSourceRow (index);
    updateControls();
}

int MixEditor::Impl::sourceIndexOf (const juce::String& file) const
{
    if (auto* proj = project())
        for (int i = 0; i < (int) proj->sources.size(); ++i)
            if (proj->sources[(size_t) i].fileName == file)
                return i;
    return -1;
}

void MixEditor::Impl::sourceMenu (int index)
{
    if (! validSource (index))
        return;
    const auto& src = project()->sources[(size_t) index];
    const bool analyzed = src.analysis.beats.size() >= 2;
    juce::PopupMenu m;
    m.addSectionHeader (src.name);
    m.addItem (1, "Analizar de nuevo");
    m.addSeparator();
    m.addItem (2, "Tiempos a la mitad", analyzed);
    m.addItem (3, "Tiempos al doble", analyzed);
    m.addItem (4, "Compases de 3 tiempos", analyzed, analyzed && src.analysis.meter == 3);
    m.addItem (5, "Compases de 4 tiempos", analyzed, analyzed && src.analysis.meter == 4);
    m.addSeparator();
    m.addItem (6, "Quitar del mix");
    // El menú es asíncrono: la fuente se vuelve a buscar por su archivo al elegir
    std::weak_ptr<bool> guard = alive;
    auto* self = this;
    const auto file = src.fileName;
    m.showMenuAsync (menuAtMouse(), [guard, self, file] (int result)
    {
        if (guard.expired() || result == 0)
            return;
        self->sourceMenuResult (file, result);
    });
}

void MixEditor::Impl::sourceMenuResult (const juce::String& file, int result)
{
    const int index = sourceIndexOf (file);
    if (index < 0)
        return;
    if (result == 1)
    {
        if (owner.onAnalyzeSource)
            owner.onAnalyzeSource (index);
        return;
    }
    if (result == 6)
    {
        if (owner.onRemoveSource)
            owner.onRemoveSource (index);
        return;
    }
    auto& a = project()->sources[(size_t) index].analysis;
    const int n = (int) a.beats.size();
    if (n < 2)
        return;
    if (result == 2)
        a.halveBeats (0, n, juce::jlimit (1, 7, a.meter));
    else if (result == 3)
        a.doubleBeats (0, n, juce::jlimit (1, 7, a.meter));
    else if (result == 4 || result == 5)
        a.renumberBeats (0, result == 4 ? 3 : 4, n, a.firstDownbeatIn (0, n));
    else
        return;
    const double bpm = medianBpm (a);
    if (bpm > 0.0)
        a.bpm = bpm;
    changed();
}

void MixEditor::Impl::sourceWaveMenu (double seconds, double tolerance)
{
    auto* src = currentSource();
    if (src == nullptr)
        return;
    const int beat = src->analysis.nearestBeat (seconds, tolerance);
    juce::PopupMenu m;
    if (beat >= 0)
        m.addItem (1, tr ("Este tiempo es el 1 del compás"), true, src->analysis.beats[(size_t) beat].beatInBar == 1);
    m.addItem (2, tr ("Escuchar desde aquí"));
    m.addSeparator();
    m.addItem (5, tr ("Inicio del tramo aquí"));
    m.addItem (6, tr ("Fin del tramo aquí"));
    m.addItem (3, tr ("Agregar la selección al mix"), hasSelection());
    m.addSeparator();
    m.addItem (4, tr ("Ver la canción entera"));
    std::weak_ptr<bool> guard = alive;
    auto* self = this;
    const auto file = src->fileName;
    m.showMenuAsync (menuAtMouse(), [guard, self, file, seconds, tolerance] (int result)
    {
        if (guard.expired() || result == 0)
            return;
        auto& impl = *self;
        const int index = impl.sourceIndexOf (file);
        if (index < 0 || index != impl.selectedSource())
            return;
        if (result == 1)
        {
            auto& a = impl.project()->sources[(size_t) index].analysis;
            const int b = a.nearestBeat (seconds, tolerance);
            if (b < 0)
                return;
            a.renumberBeats (0, a.meter >= 2 ? juce::jmin (a.meter, 7) : 4, (int) a.beats.size(), b);
            impl.sourceWave.repaint();
            impl.changed();
        }
        else if (result == 2)
        {
            impl.setSourceCursor (seconds);
            impl.playSource();
        }
        else if (result == 3)
            impl.addSelectionToMix();
        else if (result == 4)
            impl.sourceWave.fit();
        else if (result == 5 || result == 6)
            impl.setEdgeAt (result == 5, seconds);
    });
}

// Ajuste de un borde de la selección: al primer tiempo de compás (mix::snapToBeat), al tiempo o libre.
// El inicio y el final de la canción también atraen cuando están más cerca que el tiempo más cercano:
// sin eso, con "A compases" no se podría tomar la cola del último tramo del mix (el acorde final suele
// caer justo en el último primer tiempo de compás) ni lo que precede al primer tiempo en el primero.
// Ese borde no cae en un tiempo: la selección lo dice en naranja y, si el tramo queda en medio del mix,
// la línea del mix marca la unión como fuera de tiempo (joinOffBeat).
double MixEditor::Impl::snapTime (double seconds) const
{
    auto* src = currentSource();
    if (src == nullptr)
        return seconds;
    const double len = juce::jmax (0.0, sourceLength (selectedSource()));
    const double t = juce::jlimit (0.0, len, seconds);
    if (snapMode == snapFree || src->analysis.beats.empty())
        return t;
    const double snapped = mix::snapToBeat (src->analysis, t, snapMode == snapBars);
    const double distance = std::abs (t - snapped);
    if (len - t < distance)
        return len;
    if (t < distance)
        return 0.0;
    return snapped;
}

// Corte justo antes del golpe (lo hace el dueño con la transiente); los extremos no se tocan
double MixEditor::Impl::refineEdge (int source, double seconds) const
{
    if (! validSource (source) || ! owner.refineCut)
        return seconds;
    const double len = sourceLength (source);
    if (seconds <= 0.001 || seconds >= len - 0.001)
        return seconds;
    const double refined = owner.refineCut (source, seconds);
    return std::abs (refined - seconds) <= 0.1 ? juce::jlimit (0.0, len, refined) : seconds;
}

void MixEditor::Impl::dragSelection (double from, double to)
{
    selStart = snapTime (from);
    selEnd = snapTime (to);
    if (selEnd < selStart)
        std::swap (selStart, selEnd);
    sourceWave.repaint();
    updateSourceButtons();
}

void MixEditor::Impl::finishSelection()
{
    auto* src = currentSource();
    if (src == nullptr)
        return;
    if (! hasSelection())
    {
        selStart = selEnd = 0.0;
    }
    else
    {
        if (snapMode != snapFree && ! src->analysis.beats.empty())
        {
            const double a = refineEdge (selectedSource(), selStart);
            const double b = refineEdge (selectedSource(), selEnd);
            if (b > a + 0.05)
            {
                selStart = a;
                selEnd = b;
            }
        }
        sourceCursor = selStart;
    }
    sourceWave.repaint();
    updateSourceButtons();
}

// Arrastre de un solo borde: el otro queda donde está (aunque esté ajustado a la transiente). El borde ajustado
// no pasa al otro lado: si el tiempo ajustado lo cruzaría, se queda en el último lugar válido.
void MixEditor::Impl::dragEdge (bool startEdge, double seconds)
{
    if (currentSource() == nullptr || ! hasSelection())
        return;
    const double t = snapTime (seconds);
    constexpr double minWidth = 0.05;
    if (startEdge && t < selEnd - minWidth)
        selStart = t;
    else if (! startEdge && t > selStart + minWidth)
        selEnd = t;
    else
        return;
    sourceWave.repaint();
    updateSourceButtons();
}

void MixEditor::Impl::finishEdge (bool startEdge)
{
    applySelection (selStart, selEnd, startEdge, ! startEdge);
}

// Menú "Inicio del tramo aquí" / "Fin del tramo aquí": ese borde va al tiempo ajustado; sin selección, el tramo va
// de ahí al final de la canción (o del inicio hasta ahí) y se termina con el otro borde
void MixEditor::Impl::setEdgeAt (bool startEdge, double seconds)
{
    if (currentSource() == nullptr)
        return;
    const double len = sourceLength (selectedSource());
    const double t = snapTime (seconds);
    double a = hasSelection() ? selStart : 0.0, b = hasSelection() ? selEnd : len;
    if (startEdge)
    {
        a = t;
        if (b <= a + 0.05)
            b = len;
    }
    else
    {
        b = t;
        if (b <= a + 0.05)
            a = 0.0;
    }
    applySelection (a, b, startEdge, ! startEdge);
}

// Shift + clic: el borde más cercano al clic va ahí; sin selección, el tramo va del cabezal de la vista al clic
void MixEditor::Impl::extendSelectionTo (double seconds)
{
    if (currentSource() == nullptr)
        return;
    const double t = snapTime (seconds);
    if (! hasSelection())
    {
        const double c = snapTime (sourceCursor);
        if (std::abs (t - c) > 0.05)
            applySelection (juce::jmin (c, t), juce::jmax (c, t), true, true);
        return;
    }
    const bool startEdge = std::abs (t - selStart) < std::abs (t - selEnd);
    if (startEdge ? t >= selEnd - 0.05 : t <= selStart + 0.05)
        return;
    applySelection (startEdge ? t : selStart, startEdge ? selEnd : t, startEdge, ! startEdge);
}

// Pone la selección [start, end] y ajusta a la transiente los bordes pedidos (con compases y sin el modo
// "Libre", como al soltar un arrastre); el cabezal de la vista va al inicio y la vista la muestra
void MixEditor::Impl::applySelection (double start, double end, bool refineStart, bool refineEnd)
{
    auto* src = currentSource();
    if (src == nullptr)
        return;
    const double len = sourceLength (selectedSource());
    start = juce::jlimit (0.0, len, start);
    end = juce::jlimit (0.0, len, end);
    if (end < start)
        std::swap (start, end);
    if (end <= start + 0.02)
    {
        updateSourceButtons();
        return;
    }
    if (snapMode != snapFree && ! src->analysis.beats.empty())
    {
        const double a = refineStart ? refineEdge (selectedSource(), start) : start;
        const double b = refineEnd ? refineEdge (selectedSource(), end) : end;
        if (b > a + 0.05)
        {
            start = a;
            end = b;
        }
    }
    selStart = start;
    selEnd = end;
    setSourceCursor (selStart);
    sourceWave.ensureVisible (selStart, selEnd);
    sourceWave.repaint();
    updateSourceButtons();
}

void MixEditor::Impl::setSourceCursor (double seconds)
{
    const double old = sourceCursor;
    sourceCursor = seconds;
    sourceWave.repaintAt (old);
    sourceWave.repaintAt (seconds);
}

void MixEditor::Impl::playSource()
{
    sourceWasLast = true;
    if (currentSource() != nullptr && owner.onPlaySource)
        owner.onPlaySource (selectedSource(), sourceCursor);
}

//==============================================================================
// Tramos

void MixEditor::Impl::chooseSegment (int index)
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size())
    {
        owner.selectedSegment = -1;
        mixLine.repaint();
        sourceWave.repaint();
        updateControls();
        return;
    }
    owner.selectedSegment = index;
    const auto seg = proj->segments[(size_t) index];
    if (validSource (seg.source))
    {
        // Se muestra su canción con la selección = el tramo
        if (seg.source != selectedSource())
            chooseSource (seg.source, true);
        selStart = seg.start;
        selEnd = seg.end;
        sourceCursor = seg.start;
        sourceWave.ensureVisible (seg.start, seg.end);
    }
    if (index < (int) placements.size())
        mixLine.ensureVisible (placements[(size_t) index].outStart, placements[(size_t) index].outEnd);
    mixLine.repaint();
    sourceWave.repaint();
    updateControls();
}

void MixEditor::Impl::segmentMenu (int index, double mixSeconds)
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size())
        return;
    const int n = (int) proj->segments.size();
    juce::PopupMenu m;
    m.addSectionHeader (segmentTitle (index));
    m.addItem (1, "Mover a la izquierda", index > 0);
    m.addItem (2, "Mover a la derecha", index + 1 < n);
    m.addItem (3, "Duplicar");
    m.addSeparator();
    m.addItem (5, tr ("Escuchar desde aquí"));
    m.addItem (6, tr ("Escuchar la unión"));
    m.addSeparator();
    m.addItem (4, "Quitar");
    std::weak_ptr<bool> guard = alive;
    auto* self = this;
    m.showMenuAsync (menuAtMouse(), [guard, self, index, mixSeconds] (int result)
    {
        if (guard.expired() || result == 0)
            return;
        auto& impl = *self;
        auto* current = impl.project();
        if (current == nullptr || index >= (int) current->segments.size())
            return;
        switch (result)
        {
            case 1: impl.moveSegment (index, -1); break;
            case 2: impl.moveSegment (index, 1); break;
            case 3: impl.duplicateSegment (index); break;
            case 4: impl.removeSegment (index); break;
            case 5: impl.setMixCursor (mixSeconds); impl.playMix (mixSeconds); break;
            case 6: impl.chooseSegment (index); impl.playJoin(); break;
            default: break;
        }
    });
}

// Agrega la selección como tramo nuevo después del tramo elegido (o al final) y lo elige
void MixEditor::Impl::addSelectionToMix()
{
    auto* proj = project();
    if (proj == nullptr || currentSource() == nullptr || ! hasSelection())
        return;
    MixSegment seg;
    seg.source = selectedSource();
    seg.start = selStart;
    seg.end = selEnd;
    const int n = (int) proj->segments.size();
    const int at = (selectedSegment() >= 0 && selectedSegment() < n) ? selectedSegment() + 1 : n;
    proj->segments.insert (proj->segments.begin() + at, seg);
    owner.selectedSegment = at;
    changed();
    if (at < (int) placements.size())
        mixLine.ensureVisible (placements[(size_t) at].outStart, placements[(size_t) at].outEnd);
}

void MixEditor::Impl::replaceSegmentWithSelection()
{
    auto* seg = currentSegment();
    if (seg == nullptr || seg->source != selectedSource() || ! hasSelection())
        return;
    seg->start = selStart;
    seg->end = selEnd;
    changed();
}

void MixEditor::Impl::syncSelectionToSegment()
{
    auto* seg = currentSegment();
    if (seg != nullptr && seg->source == selectedSource())
    {
        selStart = seg->start;
        selEnd = seg->end;
        sourceWave.repaint();
    }
}

void MixEditor::Impl::moveSegment (int index, int delta)
{
    auto* proj = project();
    if (proj == nullptr)
        return;
    const int n = (int) proj->segments.size(), target = index + delta;
    if (index < 0 || index >= n || target < 0 || target >= n)
        return;
    std::swap (proj->segments[(size_t) index], proj->segments[(size_t) target]);
    owner.selectedSegment = target;
    changed();
    if (target < (int) placements.size())
        mixLine.ensureVisible (placements[(size_t) target].outStart, placements[(size_t) target].outEnd);
}

void MixEditor::Impl::duplicateSegment (int index)
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size())
        return;
    const auto copy = proj->segments[(size_t) index];
    proj->segments.insert (proj->segments.begin() + index + 1, copy);
    owner.selectedSegment = index + 1;
    changed();
}

void MixEditor::Impl::removeSegment (int index)
{
    auto* proj = project();
    if (proj == nullptr || index < 0 || index >= (int) proj->segments.size())
        return;
    proj->segments.erase (proj->segments.begin() + index);
    const int n = (int) proj->segments.size();
    owner.selectedSegment = n == 0 ? -1 : juce::jmin (index, n - 1);
    changed();
}

// Inicio o fin del tramo elegido un compás antes o después (y a la transiente). Pasado el primer o
// el último primer tiempo de compás, el borde va al inicio o al final de la canción (como en snapTime:
// es la única forma de llegar a la cola con compases; la línea del mix avisa si esa unión queda fuera
// de tiempo).
void MixEditor::Impl::nudgeEdge (bool startEdge, int direction)
{
    auto* seg = currentSegment();
    if (seg == nullptr || ! validSource (seg->source))
        return;
    const auto& a = project()->sources[(size_t) seg->source].analysis;
    if (a.beats.empty())
        return;
    const double len = sourceLength (seg->source);
    const double current = startEdge ? seg->start : seg->end;
    // El primer tiempo de compás del borde (el corte suele quedar unos ms antes, en la transiente)
    double nominal = mix::snapToBeat (a, current, true);
    if (std::abs (nominal - current) > 0.15)
        nominal = current;
    double t = direction < 0 ? mix::previousDownbeat (a, nominal) : mix::nextDownbeat (a, nominal);
    if (std::abs (t - nominal) > 1.0e-9)
        t = refineEdge (seg->source, t);
    else if (direction < 0 && current > 0.01)
        t = 0.0;
    else if (direction > 0 && current < len - 0.01)
        t = len;
    else
        return;
    t = juce::jlimit (0.0, juce::jmax (0.0, len), t);
    if (startEdge ? (t >= seg->end - 0.05) : (t <= seg->start + 0.05))
        return;   // nunca inicio >= fin
    if (startEdge)
        seg->start = t;
    else
        seg->end = t;
    syncSelectionToSegment();
    changed();
}

void MixEditor::Impl::setMixCursor (double seconds)
{
    const double old = mixCursor;
    mixCursor = seconds;
    mixLine.repaintAt (old);
    mixLine.repaintAt (seconds);
}

// Desde ese instante del mix; desde el final (o más allá) no sonaría nada: vuelve al principio
void MixEditor::Impl::playMix (double from)
{
    auto* proj = project();
    sourceWasLast = false;
    if (proj == nullptr || proj->segments.empty() || ! owner.onPlayMix)
        return;
    if (from >= mixLength - 0.05)
        from = 0.0;
    owner.onPlayMix (juce::jmax (0.0, from));
}

// Desde 4 s antes de la unión del tramo elegido con el anterior
void MixEditor::Impl::playJoin()
{
    const int index = selectedSegment();
    if (currentSegment() == nullptr || index >= (int) placements.size())
        return;
    playMix (juce::jmax (0.0, placements[(size_t) index].outStart - 4.0));
}

void MixEditor::Impl::stop()
{
    if (owner.onStop)
        owner.onStop();
}

//==============================================================================
// Ediciones desde las etiquetas y selectores

void MixEditor::Impl::renameFromLabel()
{
    auto* proj = project();
    if (proj == nullptr)
        return;
    const auto text = nameEdit.getText().trim();
    if (text.isEmpty() || text == proj->name)
    {
        nameEdit.setText (proj->name, juce::dontSendNotification);
        return;
    }
    if (owner.onRename)
        owner.onRename (text);
}

// Un número 20..400 pone el tempo del mix (y apaga "Cada tramo a su tempo")
void MixEditor::Impl::mixTempoEdited()
{
    auto* proj = project();
    if (proj == nullptr)
        return;
    const double value = parseNumber (bpmEdit.getText());
    if (! editUnchanged (bpmEdit) && value >= 20.0 && value <= 400.0)
    {
        proj->bpm = value;
        proj->keepTempos = false;
        changed();
        return;
    }
    updateControls();   // vuelve a mostrar el tempo actual
}

// Vacío o 0 = el tempo del mix; si no, 20..400
void MixEditor::Impl::segmentTempoEdited()
{
    auto* seg = currentSegment();
    if (seg == nullptr || editUnchanged (segTempoEdit))
    {
        updatePanel();
        return;
    }
    const auto text = segTempoEdit.getText().trim();
    const double value = parseNumber (text);
    if (text.isEmpty() || std::abs (value) < 1.0e-9)
        seg->playBpm = 0.0;
    else if (value >= 20.0 && value <= 400.0)
        seg->playBpm = value;
    else
    {
        updatePanel();
        return;
    }
    changed();
}

void MixEditor::Impl::segmentGainEdited()
{
    auto* seg = currentSegment();
    if (seg == nullptr || editUnchanged (gainEdit))
    {
        updatePanel();
        return;
    }
    seg->gainDb = (float) juce::jlimit (-24.0, 12.0, parseNumber (gainEdit.getText()));
    changed();
}

// Nombre del tramo (marcador en la canción); el de la canción o vacío = sin nombre propio
void MixEditor::Impl::segmentLabelEdited()
{
    auto* seg = currentSegment();
    if (seg == nullptr || ! validSource (seg->source) || editUnchanged (labelEdit))
    {
        updatePanel();
        return;
    }
    const auto text = labelEdit.getText().trim();
    seg->label = text == project()->sources[(size_t) seg->source].name ? juce::String() : text;
    changed();
}

void MixEditor::Impl::transposeBy (int semitones)
{
    auto* seg = currentSegment();
    if (seg == nullptr)
        return;
    const int value = juce::jlimit (-12, 12, seg->transpose + semitones);
    if (value == seg->transpose)
        return;
    seg->transpose = value;
    changed();
}

// Corte, 1 tiempo, 2 tiempos, 1 compás o 2 compases (con el compás de la canción del tramo)
void MixEditor::Impl::fadeChosen()
{
    auto* seg = currentSegment();
    const int id = fadeBox.getSelectedId();
    if (seg == nullptr || id < 1 || id > 5)
        return;
    const int meter = segmentMeter (*seg);
    const double values[] = { 0.0, 1.0, 2.0, (double) meter, 2.0 * meter };
    seg->fadeBeats = values[id - 1];
    changed();
}

//==============================================================================
MixEditor::MixEditor() : impl (std::make_unique<Impl> (*this))
{
    impl->updateControls();
    ui::disableFocus (*this);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

MixEditor::~MixEditor() = default;

void MixEditor::setProject (MixProject* p)
{
    project = p;
    selectedSource = -1;
    selectedSegment = -1;
    impl->projectChanged();
    // El dueño guarda si el click suena al escuchar, y ese estado sobrevive a un editor anterior: se
    // le avisa el del botón (apagado en un editor nuevo) para que lo que suena coincida con lo que se ve
    if (onClickToggled)
        onClickToggled (impl->clickBtn.getToggleState());
}

void MixEditor::refresh()
{
    impl->refreshFromProject();
}

void MixEditor::setSourcePeaks (int source, std::vector<float> minMaxPairs, int binsPerSecond)
{
    impl->setPeaks (source, std::move (minMaxPairs), binsPerSecond);
}

void MixEditor::setSourceStatus (int source, const juce::String& status)
{
    impl->setStatusOf (source, status);
}

void MixEditor::setPlayback (int kind, int source, double seconds, bool playing)
{
    impl->setPlayback (kind, source, seconds, playing);
}

void MixEditor::setStatus (const juce::String& text)
{
    impl->statusLabel.setText (text, juce::dontSendNotification);
}

void MixEditor::setBusy (bool rendering)
{
    if (impl->busy == rendering)
        return;
    impl->busy = rendering;
    impl->updateControls();
}

void MixEditor::selectSource (int index)
{
    impl->chooseSource (index, true);
}

void MixEditor::selectSegment (int index)
{
    impl->chooseSegment (index);
}

void MixEditor::selectBars (int firstBar, int lastBar)
{
    auto* src = impl->currentSource();
    double a = 0.0, b = 0.0;
    if (src != nullptr && mix::barRange (src->analysis, impl->sourceLength (selectedSource), firstBar, lastBar, a, b))
        impl->applySelection (a, b, true, true);
}

bool MixEditor::handleKey (const juce::KeyPress& key)
{
    return impl->handleKey (key);
}

void MixEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::background);
    if (project == nullptr)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (ui::font (18.0f));
        g.drawText ("Elige o crea un mix", getLocalBounds(), juce::Justification::centred);
        return;
    }
    impl->paintFrame (g);
}

void MixEditor::resized()
{
    impl->layout();
}

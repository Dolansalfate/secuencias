#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Colores, fuentes y ayudas compartidas por todos los componentes de la interfaz.
namespace ui
{
    inline const juce::Colour accent     { 0xff4fc3f7 };
    inline const juce::Colour background { 0xff15191d };
    inline const juce::Colour panel      { 0xff1e2329 };
    inline const juce::Colour panelAlt   { 0xff262c33 };
    inline const juce::Colour playhead   { 0xffff5252 };

    inline juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Color de cada pista en la vista de arreglo y el mezclador (paleta cíclica)
    inline juce::Colour trackColour (int index)
    {
        static const juce::uint32 palette[] = { 0xff4fc3f7, 0xffffb74d, 0xff81c784, 0xffe57373,
                                                0xffba68c8, 0xff4db6ac, 0xfffff176, 0xff90a4ae };
        return juce::Colour (palette[(size_t) juce::jmax (0, index) % (sizeof (palette) / sizeof (palette[0]))]);
    }

    inline juce::String formatTime (double s)
    {
        s = juce::jmax (0.0, s);
        const int m = (int) (s / 60.0);
        const double sec = s - m * 60.0;
        return juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4);
    }

    // Herramientas de edición de la vista de arreglo, como la caja de herramientas de Cubase: Objeto (elegir, mover y
    // recortar tramos), Rango, Cortar (tijera), Unir, Borrar, Zoom, Silenciar, Dibujar, Reproducir, Warp (llevar un
    // tiempo de la rejilla al audio) y Color
    enum class EditTool { select, range, cut, glue, erase, zoom, mute, draw, play, warp, colour };
    constexpr int numEditTools = 11;

    // Ícono de una herramienta dibujado con Path (sin símbolos Unicode), centrado en `r`; con `outline`, un borde
    // alrededor (para el cursor, que debe verse sobre cualquier fondo)
    inline void drawToolIcon (juce::Graphics& g, EditTool tool, juce::Rectangle<float> r, juce::Colour colour,
                              juce::Colour outline = {})
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        const auto toBox = juce::AffineTransform::scale (s).translated (r.getCentreX() - s * 0.5f, r.getCentreY() - s * 0.5f);
        juce::Path fill, stroke;
        float strokeWidth = 0.09f;   // en unidades del cuadrado de lado 1
        constexpr float pi = juce::MathConstants<float>::pi;
        switch (tool)
        {
            case EditTool::select:   // flecha del puntero
                fill.startNewSubPath (0.24f, 0.04f);
                fill.lineTo (0.24f, 0.84f);
                fill.lineTo (0.42f, 0.66f);
                fill.lineTo (0.56f, 0.96f);
                fill.lineTo (0.70f, 0.90f);
                fill.lineTo (0.56f, 0.61f);
                fill.lineTo (0.80f, 0.61f);
                fill.closeSubPath();
                break;
            case EditTool::range:    // un rango: corchetes a los lados y bordes punteados
                stroke.startNewSubPath (0.10f, 0.18f); stroke.lineTo (0.10f, 0.82f);
                stroke.startNewSubPath (0.90f, 0.18f); stroke.lineTo (0.90f, 0.82f);
                for (float x = 0.20f; x < 0.82f; x += 0.2f)
                {
                    stroke.startNewSubPath (x, 0.24f); stroke.lineTo (x + 0.1f, 0.24f);
                    stroke.startNewSubPath (x, 0.76f); stroke.lineTo (x + 0.1f, 0.76f);
                }
                fill.addRectangle (0.10f, 0.40f, 0.80f, 0.20f);
                break;
            case EditTool::cut:      // tijera con las hojas hacia arriba
                stroke.startNewSubPath (0.33f, 0.70f); stroke.lineTo (0.62f, 0.04f);
                stroke.startNewSubPath (0.67f, 0.70f); stroke.lineTo (0.38f, 0.04f);
                stroke.addEllipse (0.12f, 0.70f, 0.24f, 0.24f);
                stroke.addEllipse (0.64f, 0.70f, 0.24f, 0.24f);
                break;
            case EditTool::glue:     // dos eslabones: unir
                stroke.addRoundedRectangle (0.04f, 0.34f, 0.54f, 0.32f, 0.14f);
                stroke.addRoundedRectangle (0.42f, 0.34f, 0.54f, 0.32f, 0.14f);
                break;
            case EditTool::erase:    // goma de borrar, inclinada
            {
                juce::Path body;
                body.addRoundedRectangle (0.12f, 0.34f, 0.76f, 0.32f, 0.05f);
                juce::Path tip;
                tip.addRectangle (0.12f, 0.34f, 0.30f, 0.32f);
                const auto rot = juce::AffineTransform::rotation (-pi / 4.0f, 0.5f, 0.5f);
                body.applyTransform (rot);
                tip.applyTransform (rot);
                stroke.addPath (body);
                fill.addPath (tip);
                strokeWidth = 0.07f;
                break;
            }
            case EditTool::zoom:     // lupa
                stroke.addEllipse (0.10f, 0.10f, 0.56f, 0.56f);
                stroke.startNewSubPath (0.62f, 0.62f); stroke.lineTo (0.90f, 0.90f);
                strokeWidth = 0.11f;
                break;
            case EditTool::mute:     // una X
                stroke.startNewSubPath (0.18f, 0.18f); stroke.lineTo (0.82f, 0.82f);
                stroke.startNewSubPath (0.82f, 0.18f); stroke.lineTo (0.18f, 0.82f);
                strokeWidth = 0.14f;
                break;
            case EditTool::draw:     // lápiz con la punta abajo a la izquierda
            {
                juce::Path pencil;
                pencil.addRectangle (0.30f, 0.40f, 0.62f, 0.20f);
                pencil.addTriangle (0.06f, 0.50f, 0.30f, 0.40f, 0.30f, 0.60f);
                pencil.applyTransform (juce::AffineTransform::rotation (-pi / 4.0f, 0.5f, 0.5f));
                fill.addPath (pencil);
                break;
            }
            case EditTool::play:     // parlante
                fill.addRectangle (0.08f, 0.38f, 0.16f, 0.24f);
                fill.startNewSubPath (0.24f, 0.38f);
                fill.lineTo (0.50f, 0.14f);
                fill.lineTo (0.50f, 0.86f);
                fill.lineTo (0.24f, 0.62f);
                fill.closeSubPath();
                stroke.addCentredArc (0.52f, 0.50f, 0.18f, 0.18f, 0.0f, pi * 0.22f, pi * 0.78f, true);
                stroke.addCentredArc (0.52f, 0.50f, 0.36f, 0.36f, 0.0f, pi * 0.25f, pi * 0.75f, true);
                strokeWidth = 0.07f;
                break;
            case EditTool::warp:     // una línea de la rejilla que se lleva a un lado o al otro
                stroke.startNewSubPath (0.50f, 0.06f); stroke.lineTo (0.50f, 0.94f);
                fill.addArrow ({ 0.40f, 0.50f, 0.02f, 0.50f }, 0.08f, 0.30f, 0.20f);
                fill.addArrow ({ 0.60f, 0.50f, 0.98f, 0.50f }, 0.08f, 0.30f, 0.20f);
                strokeWidth = 0.08f;
                break;
            case EditTool::colour:   // gota de pintura
                fill.addEllipse (0.24f, 0.40f, 0.52f, 0.52f);
                fill.addTriangle (0.50f, 0.04f, 0.27f, 0.56f, 0.73f, 0.56f);
                break;
        }
        fill.applyTransform (toBox);
        stroke.applyTransform (toBox);
        const juce::PathStrokeType strokeType (strokeWidth * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (! outline.isTransparent())
        {
            g.setColour (outline);
            if (! fill.isEmpty())
                g.strokePath (fill, juce::PathStrokeType (s * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (! stroke.isEmpty())
                g.strokePath (stroke, juce::PathStrokeType (strokeWidth * s + s * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (colour);
        g.fillPath (fill);
        if (! stroke.isEmpty())
            g.strokePath (stroke, strokeType);
    }

    // Botón de una herramienta de edición: ícono y texto; encendido mientras es la herramienta activa. Se agrupan
    // pegados (setConnectedEdges), como la caja de herramientas de un DAW.
    class ToolButton : public juce::Button
    {
    public:
        ToolButton (EditTool t, const juce::String& label) : juce::Button (label), tool (t)
        {
            setWantsKeyboardFocus (false);
            setMouseClickGrabsKeyboardFocus (false);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            const bool left = ! isConnectedOnLeft(), right = ! isConnectedOnRight();
            juce::Path box;
            box.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.0f, 4.0f, left, right, left, right);
            auto base = on ? accent.darker (0.3f) : findColour (juce::TextButton::buttonColourId);
            if (over || down)
                base = base.brighter (down ? 0.2f : 0.1f);
            g.setColour (base);
            g.fillPath (box);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.strokePath (box, juce::PathStrokeType (1.0f));
            const auto ink = on ? juce::Colours::white : juce::Colours::white.withAlpha (isEnabled() ? 0.85f : 0.35f);
            const int side = juce::jmax (8, getHeight() - 14);
            if (getButtonText().isEmpty())
            {
                // Solo el ícono, como en la caja de herramientas de Cubase (el nombre va en la ayuda emergente)
                drawToolIcon (g, tool, getLocalBounds().toFloat().withSizeKeepingCentre ((float) side, (float) side), ink);
                return;
            }
            auto content = getLocalBounds().reduced (9, 0);
            drawToolIcon (g, tool, content.removeFromLeft (side).toFloat().withSizeKeepingCentre ((float) side, (float) side), ink);
            g.setColour (ink);
            g.setFont (font (14.0f, on));
            g.drawText (getButtonText(), content.withTrimmedLeft (6), juce::Justification::centredLeft, true);
        }

        EditTool getTool() const { return tool; }

    private:
        EditTool tool;
    };

    // Ningún hijo debe quedarse con el foco: todas las teclas van a MainComponent (ver CLAUDE.md 5.5)
    inline void disableFocus (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            child->setMouseClickGrabsKeyboardFocus (false);
            disableFocus (*child);
        }
    }
}

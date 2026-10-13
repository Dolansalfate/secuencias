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

    // Herramientas de edición de la vista de arreglo (como en Cubase): selección (clic = ir a ese punto), cortar
    // (la tijera: clic = corte) y mover (arrastrar = mover el tramo)
    enum class EditTool { select, cut, move };

    // Ícono de una herramienta dibujado con Path (sin símbolos Unicode), centrado en `r`; con `outline`, un borde
    // alrededor (para el cursor, que debe verse sobre cualquier fondo)
    inline void drawToolIcon (juce::Graphics& g, EditTool tool, juce::Rectangle<float> r, juce::Colour colour,
                              juce::Colour outline = {})
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        const auto toBox = juce::AffineTransform::scale (s).translated (r.getCentreX() - s * 0.5f, r.getCentreY() - s * 0.5f);
        juce::Path fill, stroke;
        float strokeWidth = 0.0f;   // en unidades del cuadrado de lado 1
        if (tool == EditTool::select)
        {
            // Flecha del puntero
            fill.startNewSubPath (0.24f, 0.04f);
            fill.lineTo (0.24f, 0.84f);
            fill.lineTo (0.42f, 0.66f);
            fill.lineTo (0.56f, 0.96f);
            fill.lineTo (0.70f, 0.90f);
            fill.lineTo (0.56f, 0.61f);
            fill.lineTo (0.80f, 0.61f);
            fill.closeSubPath();
        }
        else if (tool == EditTool::cut)
        {
            // Tijera con las hojas hacia arriba: se cruzan bajo las puntas y terminan en dos anillos
            stroke.startNewSubPath (0.33f, 0.70f);
            stroke.lineTo (0.62f, 0.04f);
            stroke.startNewSubPath (0.67f, 0.70f);
            stroke.lineTo (0.38f, 0.04f);
            stroke.addEllipse (0.12f, 0.70f, 0.24f, 0.24f);
            stroke.addEllipse (0.64f, 0.70f, 0.24f, 0.24f);
            strokeWidth = 0.09f;
        }
        else
        {
            // Mover: flecha doble horizontal sobre un tramo
            fill.addArrow ({ 0.5f, 0.32f, 0.02f, 0.32f }, 0.11f, 0.34f, 0.24f);
            fill.addArrow ({ 0.5f, 0.32f, 0.98f, 0.32f }, 0.11f, 0.34f, 0.24f);
            fill.addRoundedRectangle (0.16f, 0.62f, 0.68f, 0.28f, 0.05f);
        }
        fill.applyTransform (toBox);
        stroke.applyTransform (toBox);
        const juce::PathStrokeType strokeType (strokeWidth * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (! outline.isTransparent())
        {
            g.setColour (outline);
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
            auto content = getLocalBounds().reduced (9, 0);
            const int side = juce::jmax (8, getHeight() - 14);
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

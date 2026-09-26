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

#pragma once
#include <juce_core/juce_core.h>

// Nombres de notas, acordes y tonalidades: enarmonías (Re# = Mib) y escritura con sostenidos
// o con bemoles. madmom escribe los acordes siempre con sostenidos (A#, C#, D#...) y las
// tonalidades con la armadura habitual (Eb major, F# major...); aquí se reescriben según la
// preferencia de la canción.
namespace music
{
    // Clase de altura (0 = Do/C ... 11 = Si/B) de una nota en letras ("C#", "Db") o en solfeo
    // ("Do#", "Reb", "Sol"); -1 si no se reconoce. Devuelve en `rest` lo que sigue al nombre.
    int pitchClass (const juce::String& note, juce::String* rest = nullptr);

    // "Eb" / "D#" o, en solfeo, "Mib" / "Re#"
    juce::String noteName (int pitchClass, bool flats, bool solfege);

    // Acorde en letras ("A#m7", "N") reescrito con sostenidos o bemoles ("Bbm7")
    juce::String spellChord (const juce::String& chord, bool flats);

    // Tonalidad ("Mib mayor", "D# major", "Eb minor") reescrita en solfeo con la escritura
    // pedida ("Re# mayor" / "Mib mayor"); vacío si no se reconoce
    juce::String spellKey (const juce::String& key, bool flats);

    // Convención habitual: tonalidades cuya armadura lleva bemoles (Fa, Sib, Mib, Lab, Reb
    // mayor; Re, Sol, Do, Fa, Sib, Mib menor)
    bool keyPrefersFlats (const juce::String& key);

    // Tonalidad canónica para guardar ("Eb major", "C# minor"), con la escritura de madmom
    juce::String canonicalKey (int pitchClass, bool minor);

    // true si la tonalidad es menor ("menor" o "minor")
    bool isMinorKey (const juce::String& key);
}

#include "Music.h"

namespace music
{
    namespace
    {
        const char* const sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const char* const flatNames[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const char* const sharpSolfa[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const char* const flatSolfa[]  = { "Do", "Reb", "Re", "Mib", "Mi", "Fa", "Solb", "Sol", "Lab", "La", "Sib", "Si" };
        const char* const solfaBase[]  = { "Sol", "Do", "Re", "Mi", "Fa", "La", "Si" };   // "Sol" antes que "Si"
        const int solfaPc[]            = { 7, 0, 2, 4, 5, 9, 11 };
    }

    int pitchClass (const juce::String& noteIn, juce::String* rest)
    {
        const auto note = noteIn.trim();
        int pc = -1;
        int used = 0;
        for (size_t i = 0; i < sizeof (solfaBase) / sizeof (solfaBase[0]); ++i)
            if (note.startsWithIgnoreCase (solfaBase[i]))
            {
                pc = solfaPc[i];
                used = (int) juce::String (solfaBase[i]).length();
                break;
            }
        if (pc < 0 && note.isNotEmpty())
        {
            const auto letter = (juce::juce_wchar) juce::CharacterFunctions::toUpperCase (note[0]);
            static const int letterPc[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
            if (letter >= 'A' && letter <= 'G')
            {
                pc = letterPc[letter - 'A'];
                used = 1;
            }
        }
        if (pc < 0)
            return -1;
        if (note.length() > used)
        {
            const auto acc = note[used];
            if (acc == '#') { pc = (pc + 1) % 12; ++used; }
            else if (acc == 'b') { pc = (pc + 11) % 12; ++used; }
        }
        if (rest != nullptr)
            *rest = note.substring (used);
        return pc;
    }

    juce::String noteName (int pc, bool flats, bool solfege)
    {
        pc = ((pc % 12) + 12) % 12;
        if (solfege)
            return flats ? flatSolfa[pc] : sharpSolfa[pc];
        return flats ? flatNames[pc] : sharpNames[pc];
    }

    juce::String spellChord (const juce::String& chord, bool flats)
    {
        if (chord.isEmpty() || chord == "N")
            return chord;
        juce::String suffix;
        const int pc = pitchClass (chord, &suffix);
        if (pc < 0)
            return chord;
        return noteName (pc, flats, false) + suffix;
    }

    bool isMinorKey (const juce::String& key)
    {
        const auto k = key.toLowerCase();
        return k.contains ("menor") || k.contains ("minor") || k.contains ("min");
    }

    juce::String spellKey (const juce::String& key, bool flats)
    {
        const int pc = pitchClass (key.upToFirstOccurrenceOf (" ", false, false));
        if (pc < 0)
            return {};
        return noteName (pc, flats, true) + (isMinorKey (key) ? " menor" : " mayor");
    }

    bool keyPrefersFlats (const juce::String& key)
    {
        const int pc = pitchClass (key.upToFirstOccurrenceOf (" ", false, false));
        if (pc < 0)
            return false;
        if (isMinorKey (key))
            return pc == 2 || pc == 7 || pc == 0 || pc == 5 || pc == 10;   // Re Sol Do Fa Sib menor (Re# menor va con Fa# mayor)
        return pc == 5 || pc == 10 || pc == 3 || pc == 8 || pc == 1;                  // Fa Sib Mib Lab Reb mayor
    }

    juce::String canonicalKey (int pc, bool minor)
    {
        // Misma escritura que madmom: bemoles en las armaduras con bemoles, sostenidos en el resto
        const juce::String probe = noteName (pc, false, false) + (minor ? " minor" : " major");
        return noteName (pc, keyPrefersFlats (probe), false) + (minor ? " minor" : " major");
    }

    juce::String transposeChord (const juce::String& chord, int semitones)
    {
        const auto c = chord.trim();
        if (semitones % 12 == 0 || c.isEmpty() || c == "N")
            return chord;
        auto shift = [semitones] (int pc) { return ((pc + semitones) % 12 + 12) % 12; };
        const auto main = c.upToFirstOccurrenceOf ("/", false, false);
        const auto bass = c.containsChar ('/') ? c.fromFirstOccurrenceOf ("/", false, false) : juce::String();
        juce::String rest;
        const int pc = pitchClass (main, &rest);
        if (pc < 0)
            return chord;
        auto out = noteName (shift (pc), false, false) + rest;
        if (bass.isNotEmpty())
        {
            juce::String bassRest;
            const int bpc = pitchClass (bass, &bassRest);
            out += "/" + (bpc < 0 ? bass : noteName (shift (bpc), false, false) + bassRest);
        }
        return out;
    }

    juce::String transposeKey (const juce::String& key, int semitones)
    {
        if (semitones % 12 == 0 || key.trim().isEmpty())
            return key;
        const int pc = pitchClass (key.trim());
        if (pc < 0)
            return key;
        const auto canonical = canonicalKey (((pc + semitones) % 12 + 12) % 12, isMinorKey (key));
        const auto spelled = spellKey (canonical, keyPrefersFlats (canonical));
        return spelled.isNotEmpty() ? spelled : key;
    }
}

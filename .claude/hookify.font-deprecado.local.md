---
name: warn-font-deprecado
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: (Source|Tests)/[^/]+\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (juce::Font\s*\(\s*[\d.]+f?\s*[,)])
---
**`juce::Font (float)` está deprecado en JUCE 8.** Usa `juce::Font (juce::FontOptions (h, estilo))` o el helper
`ui::font (h, bold)`.

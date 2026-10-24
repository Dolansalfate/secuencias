---
name: warn-ruta-absoluta-con-barra
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: (Source|Tests)/[^/]+\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (startsWith\s*\(\s*"/"\s*\))
---
**Ruta absoluta comprobada con `startsWith ("/")`**: en Windows empiezan con `C:\`. Usa
`juce::File::isAbsolutePath` (CLAUDE.md, sección 6).

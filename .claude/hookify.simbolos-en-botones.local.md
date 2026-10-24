---
name: warn-simbolos-unicode
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: Source/[^/]+\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (?:"[^"\n]*[\u2190-\u21FF\u2300-\u23FF\u25A0-\u25FF\u2600-\u27BF][^"\n]*")
---
**Flechas, figuras o símbolos Unicode en un texto de la interfaz** (CLAUDE.md, sección 6).

Muchas fuentes de Ubuntu no los tienen y se ven como cuadrados (▶ ■ ✓ ←...). Usa texto ("Play", "<<") o
dibújalos con `Path` (`ui::drawTransportIcon`, `ui::drawToolIcon`).

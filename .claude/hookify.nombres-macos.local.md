---
name: warn-nombre-que-choca-con-macos
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: Source/[^/]+\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (?m)^(?:struct|class|enum|typedef|using)\s+(?:class\s+)?(?:Marker|Rect|Point|Comment|Fixed|Style|Cell)\b
---
**Nombre global que choca con los SDK de macOS** (`Marker`, `Rect`, `Point`, `Comment`, `Fixed`, `Style`, `Cell`
vienen de Carbon/CoreServices). Dale un nombre propio (`SongMarker`, `ClipRect`...) o mételo en un namespace: la
compilación de Mac solo se ve en GitHub Actions (CLAUDE.md, sección 6).

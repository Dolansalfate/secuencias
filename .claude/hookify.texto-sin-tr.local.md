---
name: warn-texto-sin-tr
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: (Source|Tests)/[^/]+\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (?m)^(?!\s*//)(?!\s*\*)(?!\s*")(?!.*std::cout)(?:(?!//)[^\n])*?(?<!tr \()(?<!tr\()(?<!fromUTF8 \()(?<!fromUTF8\()(?<!\\)(?:"(?:[^"\\\n]|\\.)*[^\x00-\x7F](?:[^"\\\n]|\\.)*")
---
**Texto con tildes, ñ o símbolos fuera de `tr()`** (CLAUDE.md, sección 6).

En JUCE, un `const char*` no ASCII que se convierte a `juce::String` falla en Debug (assert `juce_String.cpp:327`) y en
Release se ve mal ("baterÃ­a"). Pasa con `juce::String ("ñ")`, `"ñ" + texto`, listas `{ "ñ", ... }` (StringArray) y
parámetros `juce::String`. **Arreglo:** `tr ("...")` o `juce::String::fromUTF8 ("...")`.

No falla (puedes ignorar el aviso): `texto + "ñ"` con `texto` ya `juce::String` (se lee como UTF-8),
`tr (cond ? "ñ" : "o")`, un `const char*` que después pasa por `tr()`, o `std::cout`.

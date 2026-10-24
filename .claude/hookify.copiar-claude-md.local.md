---
name: warn-copiar-claude-md
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: Secuencias/Secuencias/CLAUDE\.md$
---
**CLAUDE.md cambió**: al terminar, cópialo idéntico a `/home/ubuntu/Documents/GitHub/daw/CLAUDE.md`
(`cp CLAUDE.md /home/ubuntu/Documents/GitHub/daw/CLAUDE.md && cmp ...`).

---
name: warn-hilo-de-audio
enabled: true
event: file
conditions:
  - field: file_path
    operator: regex_match
    pattern: Source/AudioEngine\.(cpp|h)$
  - field: content
    operator: regex_match
    pattern: (\bnew\s+[A-Za-z_]|push_back\s*\(|emplace_back\s*\(|\bDBG\s*\(|\bjuce::String\b|\bScopedLock\b|std::mutex|\.resize\s*\()
---
**Ojo con el hilo de audio** (CLAUDE.md, 5.1 y 6). Si este código corre dentro de
`audioDeviceIOCallbackWithContext` / `renderChunk`: nada de `new`, `push_back`, `resize`, `juce::String`, `DBG`,
archivos ni locks bloqueantes; solo atómicos y `SpinLock::ScopedTryLockType`, con buffers prealocados. Si es código
de la UI o del hilo de carga (`loadSong`, `setSong`...), ignora el aviso.

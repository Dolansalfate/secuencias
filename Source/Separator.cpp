#include "Separator.h"

Separator::Separator() : juce::Thread ("Demucs") {}

Separator::~Separator()
{
    cancel();
    stopThread (20000);
    if (workDir.isDirectory())
        workDir.deleteRecursively();
}

bool Separator::start (const juce::File& input, const juce::String& pythonPath,
                       const juce::String& modelName, const juce::String& name)
{
    if (isThreadRunning() || getState() == State::running)
        return false;

    reset();
    inputFile = input;
    python = pythonPath;
    model = modelName;
    songName = name;
    progress = -1.0f;
    setStatus (State::running, tr ("Iniciando..."));
    startThread();
    return true;
}

void Separator::cancel()
{
    signalThreadShouldExit();
    const juce::ScopedLock sl (processLock);
    if (process != nullptr)
        process->kill();
}

void Separator::reset()
{
    if (isThreadRunning())
        return;
    if (workDir.isDirectory())
        workDir.deleteRecursively();
    workDir = juce::File();
    resultFolder = juce::File();
    progress = 0.0f;
    setStatus (State::idle, {});
}

juce::String Separator::getMessage() const
{
    const juce::ScopedLock sl (messageLock);
    return message;
}

void Separator::setStatus (State s, const juce::String& msg)
{
    {
        const juce::ScopedLock sl (messageLock);
        message = msg;
    }
    state = (int) s;
}

void Separator::parseProgress (const juce::String& text)
{
    // Demucs/tqdm imprime cosas como " 43%|#####   | ..."
    const int idx = text.lastIndexOfChar ('%');
    if (idx <= 0)
        return;
    int startIdx = idx;
    while (startIdx > 0 && juce::CharacterFunctions::isDigit (text[startIdx - 1]))
        --startIdx;
    if (startIdx < idx)
        progress = (float) juce::jlimit (0, 100, text.substring (startIdx, idx).getIntValue()) / 100.0f;
}

void Separator::run()
{
    workDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("secuencias_separacion")
                  .getNonexistentChildFile ("trabajo", "", false);
    workDir.createDirectory();

    // 1) Convertimos la canción a WAV (así Demucs no necesita ffmpeg)
    setStatus (State::running, "Preparando audio...");
    const auto wavFile = workDir.getChildFile ("input.wav");
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inputFile));
        if (reader == nullptr)
        {
            setStatus (State::failed, tr ("No se pudo leer el archivo de audio."));
            return;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream());
        if (stream == nullptr)
        {
            setStatus (State::failed, tr ("No se pudo escribir el archivo temporal."));
            return;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), reader->sampleRate,
                                 juce::jmin (2u, reader->numChannels), 24, {}, 0));
        if (writer == nullptr)
        {
            setStatus (State::failed, tr ("No se pudo crear el WAV temporal."));
            return;
        }
        stream.release();  // ahora es del writer
        writer->writeFromAudioReader (*reader, 0, -1);
    }

    if (threadShouldExit())
    {
        setStatus (State::failed, "Cancelado.");
        return;
    }

    // 2) Ejecutamos Demucs
    setStatus (State::running, tr ("Separando con IA... (la primera vez descarga el modelo)"));
    const auto outDir = workDir.getChildFile ("out");
    juce::StringArray args { python, "-u", "-m", "demucs", "-n", model,
                             "-o", outDir.getFullPathName(), wavFile.getFullPathName() };

    juce::ChildProcess proc;
    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        setStatus (State::failed, tr ("No se pudo ejecutar Python:\n") + python
                                  + tr ("\n\nRevisa la ruta en «Ajustes IA»."));
        return;
    }

    {
        const juce::ScopedLock sl (processLock);
        process = &proc;
    }

    juce::String log;
    char buffer[2048];
    for (;;)
    {
        const int n = proc.readProcessOutput (buffer, (int) sizeof (buffer));
        if (n <= 0)
        {
            if (! proc.isRunning() || threadShouldExit())
                break;
            juce::Thread::sleep (50);
            continue;
        }

        // Solo ASCII: la barra de progreso trae caracteres que no nos interesan
        std::string ascii;
        for (int i = 0; i < n; ++i)
        {
            const auto c = (unsigned char) buffer[i];
            if (c == '\r' || c == '\n')      ascii += '\n';
            else if (c >= 32 && c < 127)     ascii += (char) c;
        }
        const juce::String chunk (ascii);
        log += chunk;
        if (log.length() > 20000)
            log = log.getLastCharacters (8000);
        parseProgress (chunk);
    }

    {
        const juce::ScopedLock sl (processLock);
        process = nullptr;
    }

    if (threadShouldExit())
    {
        proc.kill();
        setStatus (State::failed, "Cancelado.");
        return;
    }

    proc.waitForProcessToFinish (5000);
    const auto exitCode = proc.getExitCode();
    resultFolder = outDir.getChildFile (model).getChildFile ("input");

    if (exitCode != 0 || Library::audioFilesIn (resultFolder).isEmpty())
    {
        juce::StringArray lines;
        lines.addLines (log.trim());
        lines.removeEmptyStrings();
        while (lines.size() > 12)
            lines.remove (0);
        setStatus (State::failed, tr ("Demucs terminó con error (código ") + juce::String ((int) exitCode)
                                  + "):\n\n" + lines.joinIntoString ("\n"));
        return;
    }

    progress = 1.0f;
    setStatus (State::done, tr ("Separación lista."));
}

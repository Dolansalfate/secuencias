#include "Separator.h"
#include <cstdlib>

namespace
{
    // Modelo de voces de audio-separator: Mel-Band Roformer de Kimberley Jensen, el de mejor SDR de
    // voces (12.6) en la lista de audio-separator 0.47. Pesa 913 MB y usa casi 4 GB de VRAM.
    const char* const roformerModel = "vocals_mel_band_roformer.ckpt";

    juce::File roformerModelDir()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".cache/audio-separator-models");
    }

    juce::File audioSeparatorExe (const juce::String& pythonPath)
    {
       #if JUCE_WINDOWS
        return juce::File (pythonPath).getSiblingFile ("audio-separator.exe");   // junto a python.exe, en Scripts del venv
       #else
        return juce::File (pythonPath).getSiblingFile ("audio-separator");       // junto a python, en bin del venv
       #endif
    }

    // audio-separator exige ffmpeg en el PATH aunque reciba WAV
    bool ffmpegInPath()
    {
       #if JUCE_WINDOWS
        const juce::String separator = ";", exe = "ffmpeg.exe";
       #else
        const juce::String separator = ":", exe = "ffmpeg";
       #endif
        juce::StringArray dirs;
        dirs.addTokens (juce::String (std::getenv ("PATH") != nullptr ? std::getenv ("PATH") : ""), separator, "\"");
        dirs.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/bin").getFullPathName());
        for (auto& d : dirs)
            if (d.isNotEmpty() && juce::File::isAbsolutePath (d) && juce::File (d).getChildFile (exe).existsAsFile())
                return true;
        return false;
    }
}

Separator::Separator() : juce::Thread ("Demucs") {}

Separator::~Separator()
{
    cancel();
    stopThread (20000);
    if (workDir.isDirectory())
        workDir.deleteRecursively();
}

//==============================================================================
int Separator::modelsInBag (const juce::String& model)
{
    return model == "htdemucs_ft" ? 4 : 1;
}

std::vector<SeparationStage> Separator::planFor (const SeparationOptions& o)
{
    std::vector<SeparationStage> plan;
    const int shifts = o.quality >= 2 ? 3 : 1;
    const double overlap = o.quality >= 2 ? 0.5 : 0.25;
    // Con overlap 0.5 hay 1.5 veces más trozos que con 0.25
    const double overlapCost = 0.75 / (1.0 - overlap);

    auto demucs = [&] (const juce::String& model)
    {
        SeparationStage s;
        s.tool = "demucs";
        s.model = model;
        s.shifts = shifts;
        s.overlap = overlap;
        s.passes = modelsInBag (model) * shifts;
        s.weight = (double) s.passes * overlapCost;
        return s;
    };

    if (o.roformerVocals)
    {
        SeparationStage r;
        r.tool = "roformer";
        r.model = roformerModel;
        r.passes = 1;
        r.weight = 6.0;   // medido en una GTX 1050 Ti: 2x tiempo real, contra 12x de una pasada de htdemucs
        plan.push_back (r);
    }

    if (o.quality == 0)
        plan.push_back (demucs (o.stems >= 6 ? "htdemucs_6s" : "htdemucs"));
    else
    {
        plan.push_back (demucs ("htdemucs_ft"));       // voces, batería y bajo afinados
        if (o.stems >= 6)
            plan.push_back (demucs ("htdemucs_6s"));   // guitarra y piano (no existe versión afinada)
    }
    return plan;
}

double Separator::relativeCost (const SeparationOptions& o)
{
    double cost = 0.0;
    for (auto& s : planFor (o))
        cost += s.weight;
    return cost;
}

juce::String Separator::describe (const SeparationOptions& o)
{
    juce::StringArray tools;
    for (auto& s : planFor (o))
        tools.add (s.tool == "roformer" ? juce::String ("BS-Roformer") : s.model);
    const juce::String quality = o.quality == 0 ? juce::String ("normal") : o.quality == 1 ? juce::String ("alta") : tr ("máxima");
    juce::String text = juce::String (o.stems) + " pistas, calidad " + quality + ": " + tools.joinIntoString (" + ");
    if (o.quality >= 2)
        text += " (3 pasadas promediadas)";
    return text;
}

bool Separator::isRoformerAvailable (const juce::String& pythonPath)
{
    return juce::File::isAbsolutePath (pythonPath) && audioSeparatorExe (pythonPath).existsAsFile() && ffmpegInPath();
}

//==============================================================================
bool Separator::start (const juce::File& input, const juce::String& pythonPath,
                       const SeparationOptions& opts, const juce::String& name)
{
    if (isThreadRunning() || getState() == State::running)
        return false;

    reset();
    inputFile = input;
    python = pythonPath;
    options = opts;
    if (options.roformerVocals && ! isRoformerAvailable (python))
        options.roformerVocals = false;
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
    if (getState() == State::running)
        return;
    // El estado final se publica justo antes de que run() termine: esperar al hilo evita
    // que un reset() se ignore y la UI vuelva a procesar el mismo resultado.
    waitForThreadToExit (5000);
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

//==============================================================================
int Separator::parsePercent (const juce::String& text)
{
    // Demucs/tqdm imprime cosas como " 43%|#####   | 3/7 [00:05<00:07, ...]"
    const int idx = text.indexOf ("%|");
    if (idx <= 0)
        return -1;
    int startIdx = idx;
    while (startIdx > 0 && juce::CharacterFunctions::isDigit (text[startIdx - 1]))
        --startIdx;
    if (startIdx == idx)
        return -1;
    return juce::jlimit (0, 100, text.substring (startIdx, idx).getIntValue());
}

void Separator::handleOutputLine (const std::string& line)
{
    const juce::String text (line);
    // Las barras de descarga de modelos (primera vez) también son tqdm; se distinguen por la velocidad en B/s
    const int pct = text.contains ("B/s") ? -1 : parsePercent (text);
    if (pct < 0)
    {
        // Solo las líneas que no son barra de progreso sirven para el mensaje de error
        log.add (text);
        while (log.size() > 40)
            log.remove (0);
        return;
    }

    if (lastPercent >= 0 && pct < lastPercent - 50)
        ++pass;   // tqdm volvió a empezar: siguiente modelo de la bolsa o siguiente pasada
    lastPercent = pct;
    const double inStage = juce::jlimit (0.0, 1.0, ((double) pass + (double) pct / 100.0) / (double) passes);
    progress = (float) juce::jlimit (0.0, 1.0, (stageOffset + stageWeight * inStage) / totalWeight);
}

//==============================================================================
bool Separator::convertToWav (const juce::File& in, const juce::File& out, double targetRate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (in));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = tr ("No se pudo leer el archivo de audio.");
        return false;
    }

    const int channels = (int) juce::jmin (2u, reader->numChannels);
    const int srcLen = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> src (channels, srcLen + 64);   // colchón para el interpolador
    src.clear();
    reader->read (&src, 0, srcLen, 0, true, channels > 1);

    juce::AudioBuffer<float> outBuf;
    if (std::abs (reader->sampleRate - targetRate) < 1.0)
    {
        outBuf = std::move (src);
        outBuf.setSize (channels, srcLen, true, false, true);
    }
    else
    {
        const double ratio = reader->sampleRate / targetRate;
        const int outLen = (int) ((double) srcLen / ratio);
        outBuf.setSize (channels, outLen);
        for (int ch = 0; ch < channels; ++ch)
        {
            juce::WindowedSincInterpolator interp;   // calidad alta; corre fuera de línea
            interp.process (ratio, src.getReadPointer (ch), outBuf.getWritePointer (ch), outLen);
        }
    }

    juce::WavAudioFormat wav;
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    if (stream == nullptr)
    {
        error = tr ("No se pudo escribir el archivo temporal.");
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), targetRate, (unsigned) channels, 24, {}, 0));
    if (writer == nullptr)
    {
        error = tr ("No se pudo crear el WAV temporal.");
        return false;
    }
    stream.release();   // ahora es del writer
    writer->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
    return true;
}

bool Separator::writeResidual (const juce::File& mix, const juce::Array<juce::File>& stems,
                               const juce::File& out, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> mixReader (formats.createReaderFor (mix));
    if (mixReader == nullptr)
    {
        error = tr ("No se pudo leer la mezcla para calcular la pista de otros.");
        return false;
    }
    const int channels = (int) mixReader->numChannels;
    const auto length = mixReader->lengthInSamples;

    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
    for (auto& f : stems)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || std::abs (r->sampleRate - mixReader->sampleRate) > 1.0)
        {
            error = tr ("Stem ilegible o con otra frecuencia: ") + f.getFileName();
            return false;
        }
        readers.push_back (std::move (r));
    }

    juce::WavAudioFormat wav;
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr
        ? wav.createWriterFor (stream.get(), mixReader->sampleRate, (unsigned) channels, 24, {}, 0) : nullptr);
    if (writer == nullptr)
    {
        error = tr ("No se pudo escribir la pista de otros.");
        return false;
    }
    stream.release();

    // Por bloques, así una canción larga no duplica su tamaño en RAM
    constexpr int blockSize = 1 << 16;
    juce::AudioBuffer<float> acc (channels, blockSize), tmp (channels, blockSize);
    for (juce::int64 pos = 0; pos < length; pos += blockSize)
    {
        const int n = (int) juce::jmin ((juce::int64) blockSize, length - pos);
        acc.clear();
        mixReader->read (&acc, 0, n, pos, true, true);
        for (auto& r : readers)
        {
            tmp.clear();
            const int avail = (int) juce::jlimit ((juce::int64) 0, (juce::int64) n, r->lengthInSamples - pos);
            if (avail > 0)
                r->read (&tmp, 0, avail, pos, true, true);
            for (int ch = 0; ch < channels; ++ch)
                acc.addFrom (ch, 0, tmp, juce::jmin (ch, tmp.getNumChannels() - 1), 0, n, -1.0f);
        }
        writer->writeFromAudioSampleBuffer (acc, 0, n);
    }
    return true;
}

//==============================================================================
bool Separator::runProcess (const juce::StringArray& args, int stagePasses)
{
    juce::ChildProcess proc;
    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        setStatus (State::failed, tr ("No se pudo ejecutar:\n") + args[0] + tr ("\n\nRevisa la ruta en «Ajustes IA»."));
        return false;
    }
    {
        const juce::ScopedLock sl (processLock);
        process = &proc;
    }

    passes = juce::jmax (1, stagePasses);
    pass = 0;
    lastPercent = -1;

    // Se lee byte a byte: readProcessOutput bloquea hasta llenar el buffer que se le pasa, y con
    // un buffer grande la barra de progreso llegaría en trozos de varios segundos.
    std::string line;
    for (;;)
    {
        char c = 0;
        const int n = proc.readProcessOutput (&c, 1);
        if (n <= 0)
        {
            if (! proc.isRunning() || threadShouldExit())
                break;
            juce::Thread::sleep (20);
            continue;
        }
        if (c == '\r' || c == '\n')
        {
            if (! line.empty())
                handleOutputLine (line);
            line.clear();
        }
        else if ((unsigned char) c >= 32 && (unsigned char) c < 127)   // solo ASCII
            line += c;
    }
    if (! line.empty())
        handleOutputLine (line);

    {
        const juce::ScopedLock sl (processLock);
        process = nullptr;
    }

    if (threadShouldExit())
    {
        proc.kill();
        setStatus (State::cancelled, "Cancelado.");
        return false;
    }

    proc.waitForProcessToFinish (5000);
    const auto exitCode = proc.getExitCode();
    if (exitCode != 0)
    {
        juce::StringArray lines (log);
        lines.trim();
        lines.removeEmptyStrings();
        while (lines.size() > 12)
            lines.remove (0);
        setStatus (State::failed, args[0].fromLastOccurrenceOf ("/", false, false)
                                  + tr (" terminó con error (código ") + juce::String ((int) exitCode)
                                  + "):\n\n" + lines.joinIntoString ("\n"));
        return false;
    }
    return true;
}

bool Separator::runStage (const SeparationStage& stage, const juce::File& input, const juce::File& outDir, juce::File& resultDir)
{
    outDir.createDirectory();
    log.clear();

    if (stage.tool == "roformer")
    {
        setStatus (State::running, tr ("Separando voces con BS-Roformer... (la primera vez descarga el modelo)"));
        // --normalization 1.0: sin esto escala la mezcla a 0.9 de pico y las voces ya no restan exacto.
        // --mdxc_batch_size 1: el modelo usa casi 4 GB de VRAM; con lotes mayores se queda sin memoria.
        juce::StringArray args { audioSeparatorExe (python).getFullPathName(), input.getFullPathName(),
                                 "--model_filename", stage.model,
                                 "--model_file_dir", roformerModelDir().getFullPathName(),
                                 "--output_dir", outDir.getFullPathName(),
                                 "--output_format", "WAV",
                                 "--normalization", "1.0",
                                 "--mdxc_batch_size", "1" };
        if (! runProcess (args, stage.passes))
            return false;
        resultDir = outDir;
        return true;
    }

    setStatus (State::running, tr ("Separando con ") + stage.model + tr ("... (la primera vez descarga el modelo)"));
    juce::StringArray args { python, "-u", "-m", "demucs", "-n", stage.model,
                             "--shifts", juce::String (stage.shifts),
                             "--overlap", juce::String (stage.overlap, 2),
                             "--int24",
                             "-o", outDir.getFullPathName(), input.getFullPathName() };
    if (! runProcess (args, stage.passes))
        return false;
    resultDir = outDir.getChildFile (stage.model).getChildFile (input.getFileNameWithoutExtension());
    if (Library::audioFilesIn (resultDir).isEmpty())
    {
        juce::StringArray lines (log);
        lines.trim();
        lines.removeEmptyStrings();
        while (lines.size() > 12)
            lines.remove (0);
        setStatus (State::failed, tr ("Demucs no dejó stems en ") + resultDir.getFullPathName() + "\n\n" + lines.joinIntoString ("\n"));
        return false;
    }
    return true;
}

void Separator::run()
{
    workDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("secuencias_separacion")
                  .getNonexistentChildFile ("trabajo", "", false);
    workDir.createDirectory();

    // 1) Mezcla a WAV 44,1 kHz y 24 bits: la frecuencia de los modelos, así Demucs no remuestrea y
    //    sus stems quedan alineados muestra a muestra con la mezcla (necesario para el residuo).
    setStatus (State::running, "Preparando audio...");
    const auto mixFile = workDir.getChildFile ("input.wav");
    juce::String error;
    if (! convertToWav (inputFile, mixFile, demucsSampleRate, error))
    {
        setStatus (State::failed, error);
        return;
    }
    if (threadShouldExit())
    {
        setStatus (State::cancelled, "Cancelado.");
        return;
    }

    // 2) Etapas
    const auto plan = planFor (options);
    totalWeight = 0.0;
    for (auto& s : plan)
        totalWeight += s.weight;
    stageOffset = 0.0;

    juce::File vocals, drums, bass, guitar, piano;
    juce::File demucsInput = mixFile;
    int stageIndex = 0;
    for (auto& stage : plan)
    {
        stageWeight = stage.weight;
        progress = (float) (stageOffset / totalWeight);
        juce::File resultDir;
        if (! runStage (stage, demucsInput, workDir.getChildFile ("etapa" + juce::String (++stageIndex)), resultDir))
            return;
        stageOffset += stage.weight;

        if (stage.tool == "roformer")
        {
            juce::File instrumental;
            for (auto& f : Library::audioFilesIn (resultDir))
            {
                // audio-separator nombra "<entrada>_(vocals)_<modelo>.wav" y "(other)" o "(instrumental)" según el modelo
                if (f.getFileName().containsIgnoreCase ("(vocals)"))
                    vocals = f;
                else if (f.getFileName().containsIgnoreCase ("(instrumental)") || f.getFileName().containsIgnoreCase ("(other)"))
                    instrumental = f;
            }
            if (! vocals.existsAsFile() || ! instrumental.existsAsFile())
            {
                setStatus (State::failed, tr ("BS-Roformer no dejó las pistas de voces e instrumental."));
                return;
            }
            // Demucs sigue sobre el instrumental (sin voces); su carpeta de salida lleva el nombre del archivo
            demucsInput = workDir.getChildFile ("instrumental").getChildFile ("input.wav");
            demucsInput.getParentDirectory().createDirectory();
            instrumental.moveFileTo (demucsInput);
        }
        else
        {
            // De cada modelo de Demucs se toman las pistas que mejor hace; "otros" se recalcula al final
            const bool sixStems = stage.model == "htdemucs_6s";
            if (! vocals.existsAsFile()) vocals = resultDir.getChildFile ("vocals.wav");
            if (! drums.existsAsFile())  drums  = resultDir.getChildFile ("drums.wav");
            if (! bass.existsAsFile())   bass   = resultDir.getChildFile ("bass.wav");
            if (sixStems)
            {
                guitar = resultDir.getChildFile ("guitar.wav");
                piano  = resultDir.getChildFile ("piano.wav");
            }
        }
    }

    // 3) Carpeta final: mover las pistas elegidas y calcular "otros" = mezcla - resto
    setStatus (State::running, tr ("Armando las pistas..."));
    resultFolder = workDir.getChildFile ("resultado");
    resultFolder.createDirectory();
    juce::Array<juce::File> chosen;
    const std::pair<juce::File*, const char*> picks[] = { { &vocals, "vocals.wav" }, { &drums, "drums.wav" },
                                                          { &bass, "bass.wav" }, { &guitar, "guitar.wav" },
                                                          { &piano, "piano.wav" } };
    for (const auto& pick : picks)
    {
        if (! pick.first->existsAsFile())
            continue;
        const auto dest = resultFolder.getChildFile (pick.second);
        if (! pick.first->moveFileTo (dest))
        {
            setStatus (State::failed, tr ("No se pudo mover ") + pick.first->getFileName());
            return;
        }
        chosen.add (dest);
    }
    if (chosen.isEmpty())
    {
        setStatus (State::failed, tr ("La separación no produjo pistas."));
        return;
    }
    if (! writeResidual (mixFile, chosen, resultFolder.getChildFile ("other.wav"), error))
    {
        setStatus (State::failed, error);
        return;
    }

    progress = 1.0f;
    setStatus (State::done, tr ("Separación lista."));
}

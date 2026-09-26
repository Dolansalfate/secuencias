#include "Analyzer.h"

// Script de análisis. Se escribe en la carpeta temporal y se ejecuta con el Python del análisis
// (venv con madmom). Imprime "progreso=NN" por stdout y deja el resultado en JSON.
static const char* const analyzerScript = R"PY(
import argparse, json, sys, warnings
warnings.filterwarnings("ignore")
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("--mezcla", required=True)
ap.add_argument("--armonico", required=True)
ap.add_argument("--salida", required=True)
args = ap.parse_args()

def progreso(p):
    print("progreso=%d" % p, flush=True)

from madmom.features.downbeats import RNNDownBeatProcessor, DBNDownBeatTrackingProcessor
from madmom.audio.chroma import DeepChromaProcessor
from madmom.features.chords import DeepChromaChordRecognitionProcessor
from madmom.features.key import CNNKeyRecognitionProcessor, key_prediction_to_label

progreso(2)
# 1) Tiempos y primeros tiempos de compás (red neuronal + decodificador DBN), compases de 3 o 4
act = RNNDownBeatProcessor()(args.mezcla)
progreso(40)
beats = DBNDownBeatTrackingProcessor(beats_per_bar=[3, 4], fps=100)(act)
progreso(50)

# 2) Acordes sobre la mezcla sin batería (cromas profundos + CRF)
chroma = DeepChromaProcessor()(args.armonico)
progreso(75)
chords = DeepChromaChordRecognitionProcessor()(chroma)
progreso(90)

# 3) Tonalidad
key = key_prediction_to_label(CNNKeyRecognitionProcessor()(args.mezcla))
progreso(97)

beat_list = [[round(float(t), 4), int(n)] for t, n in beats]
meter = int(max((n for _, n in beat_list), default=4))
intervals = [b - a for (a, _), (b, _) in zip(beat_list, beat_list[1:]) if b > a]
bpm = round(60.0 / float(np.median(intervals)), 2) if intervals else 0.0
chord_list = [[round(float(s), 3), round(float(e), 3), str(l)] for s, e, l in chords]

with open(args.salida, "w") as f:
    json.dump({"bpm": bpm, "meter": meter, "key": str(key), "beats": beat_list, "chords": chord_list}, f)
progreso(100)
)PY";

Analyzer::Analyzer() : juce::Thread ("Analisis") {}

Analyzer::~Analyzer()
{
    cancel();
    stopThread (20000);
    if (workDir.isDirectory())
        workDir.deleteRecursively();
}

const char* Analyzer::pythonScript()
{
    return analyzerScript;
}

//==============================================================================
bool Analyzer::isAvailable (const juce::String& pythonPath)
{
    return pythonPath.startsWithChar ('/') && juce::File (pythonPath).existsAsFile();
}

bool Analyzer::isDrumsTrack (const juce::String& name, const juce::String& fileName)
{
    const auto n = name.toLowerCase(), f = fileName.toLowerCase();
    return f.startsWith ("drums") || n.startsWith (tr ("bater").toLowerCase()) || n.startsWith ("drum")
        || n.startsWith ("perc") || f.startsWith ("perc");
}

juce::String Analyzer::displayChord (const juce::String& label)
{
    if (label.isEmpty() || label == "N")
        return "N";
    const auto root = label.upToFirstOccurrenceOf (":", false, false);
    const auto quality = label.fromFirstOccurrenceOf (":", false, false);
    if (quality == "maj" || quality.isEmpty()) return root;
    if (quality == "min")  return root + "m";
    if (quality == "7")    return root + "7";
    if (quality == "maj7") return root + "maj7";
    if (quality == "min7") return root + "m7";
    if (quality == "dim")  return root + "dim";
    if (quality == "aug")  return root + "aug";
    return root + quality;
}

juce::String Analyzer::displayKey (const juce::String& key)
{
    static const char* const notes[]   = { "C", "C#", "Db", "D", "D#", "Eb", "E", "F", "F#", "Gb", "G", "G#", "Ab", "A", "A#", "Bb", "B" };
    static const char* const spanish[] = { "Do", "Do#", "Reb", "Re", "Re#", "Mib", "Mi", "Fa", "Fa#", "Solb", "Sol", "Sol#", "Lab", "La", "La#", "Sib", "Si" };
    const auto note = key.upToFirstOccurrenceOf (" ", false, false).trim();
    const auto mode = key.fromFirstOccurrenceOf (" ", false, false).trim().toLowerCase();
    juce::String name = note;
    for (size_t i = 0; i < sizeof (notes) / sizeof (notes[0]); ++i)
        if (note == notes[i])
            name = spanish[i];
    if (name.isEmpty())
        return {};
    return name + (mode.startsWith ("min") ? " menor" : " mayor");
}

Analysis Analyzer::parseResult (const juce::var& json, juce::String& error)
{
    Analysis a;
    if (! json.isObject())
    {
        error = tr ("El resultado del análisis no es válido.");
        return a;
    }
    a.bpm = juce::jmax (0.0, (double) json.getProperty ("bpm", 0.0));
    a.meter = juce::jlimit (2, 7, (int) json.getProperty ("meter", 4));
    a.key = displayKey (json.getProperty ("key", "").toString());
    if (auto* arr = json.getProperty ("beats", juce::var()).getArray())
        for (auto& b : *arr)
            if (auto* pair = b.getArray(); pair != nullptr && pair->size() >= 2)
                a.beats.push_back ({ juce::jmax (0.0, (double) (*pair)[0]), juce::jlimit (1, 7, (int) (*pair)[1]) });
    if (auto* arr = json.getProperty ("chords", juce::var()).getArray())
        for (auto& c : *arr)
            if (auto* t = c.getArray(); t != nullptr && t->size() >= 3)
            {
                const auto name = displayChord ((*t)[2].toString());
                const double s = juce::jmax (0.0, (double) (*t)[0]), e = juce::jmax (0.0, (double) (*t)[1]);
                // Unir acordes iguales consecutivos
                if (! a.chords.empty() && a.chords.back().name == name && std::abs (a.chords.back().end - s) < 0.05)
                    a.chords.back().end = e;
                else if (e > s)
                    a.chords.push_back ({ s, e, name });
            }
    if (a.beats.empty() && a.chords.empty())
        error = tr ("El análisis no encontró tiempos ni acordes.");
    return a;
}

bool Analyzer::writeMix (const LoadedSong& s, const std::vector<bool>& include, double sr,
                         const juce::File& out, juce::String& error)
{
    const int n = (int) s.length;
    if (n <= 0)
    {
        error = tr ("La canción está vacía.");
        return false;
    }
    juce::AudioBuffer<float> mix (2, n);
    mix.clear();
    int used = 0;
    for (size_t i = 0; i < s.tracks.size(); ++i)
    {
        if (i < include.size() && ! include[i])
            continue;
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, s.tracks[i]->buffer, ch, 0, n);
        ++used;
    }
    if (used == 0)
    {
        error = tr ("No hay pistas para analizar.");
        return false;
    }
    const float peak = mix.getMagnitude (0, n);
    if (peak > 0.9f)
        mix.applyGain (0.9f / peak);

    juce::WavAudioFormat wav;
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer (stream != nullptr ? wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0) : nullptr);
    if (writer == nullptr)
    {
        error = tr ("No se pudo escribir la mezcla temporal.");
        return false;
    }
    stream.release();
    writer->writeFromAudioSampleBuffer (mix, 0, n);
    return true;
}

//==============================================================================
bool Analyzer::start (std::shared_ptr<LoadedSong> s, double sr, std::vector<bool> isDrums, const juce::String& pythonPath)
{
    if (isThreadRunning() || getState() == State::running || s == nullptr)
        return false;
    reset();
    song = std::move (s);
    sampleRate = sr;
    drums = std::move (isDrums);
    python = pythonPath;
    progress = -1.0f;
    setStatus (State::running, tr ("Preparando mezclas..."));
    startThread();
    return true;
}

void Analyzer::cancel()
{
    signalThreadShouldExit();
    const juce::ScopedLock sl (processLock);
    if (process != nullptr)
        process->kill();
}

void Analyzer::reset()
{
    if (getState() == State::running)
        return;
    waitForThreadToExit (5000);
    if (workDir.isDirectory())
        workDir.deleteRecursively();
    workDir = juce::File();
    song = nullptr;
    progress = 0.0f;
    setStatus (State::idle, {});
}

juce::String Analyzer::getMessage() const
{
    const juce::ScopedLock sl (messageLock);
    return message;
}

void Analyzer::setStatus (State s, const juce::String& msg)
{
    {
        const juce::ScopedLock sl (messageLock);
        message = msg;
    }
    state = (int) s;
}

void Analyzer::run()
{
    workDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("secuencias_analisis")
                  .getNonexistentChildFile ("trabajo", "", false);
    workDir.createDirectory();

    // 1) Mezclas: completa (tiempos, tonalidad) y sin batería (acordes)
    juce::String error;
    std::vector<bool> all (song->tracks.size(), true), harmonic (song->tracks.size(), true);
    for (size_t i = 0; i < harmonic.size() && i < drums.size(); ++i)
        harmonic[i] = ! drums[i];
    if (std::none_of (harmonic.begin(), harmonic.end(), [] (bool b) { return b; }))
        harmonic = all;   // solo hay batería: se analiza lo que haya
    const auto mixFile = workDir.getChildFile ("mezcla.wav"), harmonicFile = workDir.getChildFile ("armonico.wav");
    if (! writeMix (*song, all, sampleRate, mixFile, error) || ! writeMix (*song, harmonic, sampleRate, harmonicFile, error))
    {
        setStatus (State::failed, error);
        return;
    }
    song = nullptr;   // ya no hace falta retener el audio
    if (threadShouldExit())
    {
        setStatus (State::cancelled, "Cancelado.");
        return;
    }

    // 2) Script
    const auto script = workDir.getChildFile ("analizar.py");
    script.replaceWithText (analyzerScript);
    const auto resultFile = workDir.getChildFile ("resultado.json");
    setStatus (State::running, tr ("Analizando tempo, compases y acordes... (la primera vez tarda más)"));

    juce::ChildProcess proc;
    juce::StringArray args { python, "-u", script.getFullPathName(), "--mezcla", mixFile.getFullPathName(),
                             "--armonico", harmonicFile.getFullPathName(), "--salida", resultFile.getFullPathName() };
    if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        setStatus (State::failed, tr ("No se pudo ejecutar el Python del análisis:\n") + python);
        return;
    }
    {
        const juce::ScopedLock sl (processLock);
        process = &proc;
    }

    juce::StringArray log;
    std::string line;
    for (;;)
    {
        char c = 0;
        const int n = proc.readProcessOutput (&c, 1);   // byte a byte: ver Separator
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
            {
                const juce::String text (line);
                if (text.startsWith ("progreso="))
                    progress = (float) juce::jlimit (0, 100, text.fromFirstOccurrenceOf ("=", false, false).getIntValue()) / 100.0f;
                else
                {
                    log.add (text);
                    while (log.size() > 40)
                        log.remove (0);
                }
            }
            line.clear();
        }
        else if ((unsigned char) c >= 32 && (unsigned char) c < 127)
            line += c;
    }
    {
        const juce::ScopedLock sl (processLock);
        process = nullptr;
    }
    if (threadShouldExit())
    {
        proc.kill();
        setStatus (State::cancelled, "Cancelado.");
        return;
    }
    proc.waitForProcessToFinish (5000);

    // 3) Resultado
    const auto json = juce::JSON::parse (resultFile);
    if (proc.getExitCode() != 0 || ! json.isObject())
    {
        juce::StringArray lines (log);
        lines.trim();
        lines.removeEmptyStrings();
        while (lines.size() > 12)
            lines.remove (0);
        setStatus (State::failed, tr ("El análisis terminó con error (código ") + juce::String ((int) proc.getExitCode())
                                  + "):\n\n" + lines.joinIntoString ("\n"));
        return;
    }
    result = parseResult (json, error);
    if (error.isNotEmpty())
    {
        setStatus (State::failed, error);
        return;
    }
    progress = 1.0f;
    setStatus (State::done, tr ("Análisis listo."));
}

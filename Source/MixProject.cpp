#include "MixProject.h"
#include "Arrangement.h"
#include "Loudness.h"
#include "Music.h"
#include "Stretcher.h"
#include "TempoMap.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace
{
    constexpr double beatTolerance = 0.05;      // un tiempo a menos de esto de un borde cuenta como "en el borde"
    constexpr double stretchContext = 0.5;      // audio de más a cada lado al estirar (el estirador necesita contexto)
    constexpr double halfPi = juce::MathConstants<double>::halfPi;
    constexpr double maxSeconds = 4.0 * 3600.0; // tope de una fuente, de un tramo y del mix (un mix.json editado a mano)
    constexpr double coherentOffset = 0.00025;  // desfase máximo (s de la fuente) para tratar un fundido como del mismo audio

    // Número finito dentro de [lo, hi] (NaN o infinito = `fallback`)
    double finiteIn (double value, double lo, double hi, double fallback)
    {
        return std::isfinite (value) ? juce::jlimit (lo, hi, value) : fallback;
    }

    const MixSource* sourceAt (const MixProject& project, int index)
    {
        return index >= 0 && index < (int) project.sources.size() ? &project.sources[(size_t) index] : nullptr;
    }

    // 0 = sin tempo; si no, entre 20 y 400 BPM
    double clampBpm (double bpm)
    {
        return bpm > 0.0 ? juce::jlimit (20.0, 400.0, bpm) : 0.0;
    }

    // Tramo acotado a su fuente (si se conoce su largo); end >= start
    void clampedRange (const MixSource& source, const MixSegment& segment, double& start, double& end)
    {
        start = juce::jmax (0.0, segment.start);
        end = segment.end;
        if (source.length > 0.0)
        {
            start = juce::jmin (start, source.length);
            end = juce::jmin (end, source.length);
        }
        end = juce::jmax (end, start);
    }

    juce::var toVar (const MixSource& s)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", s.name);
        o->setProperty ("file", s.fileName);
        o->setProperty ("length", s.length);
        if (! s.analysis.isEmpty() || s.analysis.bpm > 0.0 || s.analysis.key.isNotEmpty())
            o->setProperty ("analysis", Library::analysisToVar (s.analysis));
        return juce::var (o);
    }

    juce::var toVar (const MixSegment& s)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("source", s.source);
        o->setProperty ("start", s.start);
        o->setProperty ("end", s.end);
        o->setProperty ("label", s.label);
        o->setProperty ("playBpm", s.playBpm);
        o->setProperty ("transpose", s.transpose);
        o->setProperty ("gainDb", (double) s.gainDb);
        o->setProperty ("fadeBeats", s.fadeBeats);
        o->setProperty ("id", s.id);
        if (s.silenceBars > 0)
            o->setProperty ("silenceBars", s.silenceBars);
        // Lo nuevo solo si cambia algo: un mix.json de antes conserva su huella (renderSignature) y no se rearma
        if (s.overlapBars > 0)
            o->setProperty ("overlapBars", s.overlapBars);
        if (s.overlapBlend != MixSegment::blendFadeOut)
            o->setProperty ("overlapBlend", s.overlapBlend);
        juce::Array<juce::var> stems;
        for (auto& st : s.stems)
            if (! st.isDefault() && st.stem.isNotEmpty())
            {
                auto* so = new juce::DynamicObject();
                so->setProperty ("stem", st.stem);
                if (st.muted)
                    so->setProperty ("muted", true);
                if (st.leadBars != 0)
                    so->setProperty ("leadBars", st.leadBars);
                if (st.tailBars != 0)
                    so->setProperty ("tailBars", st.tailBars);
                if (st.shiftBeats != 0)
                    so->setProperty ("shiftBeats", st.shiftBeats);
                stems.add (juce::var (so));
            }
        if (! stems.isEmpty())
            o->setProperty ("stems", stems);
        return juce::var (o);
    }

    // reader.read en `dest` (estéreo). Si la lectura falla, lo que no se pudo leer queda en cero, como
    // en el resto de la app: un MP3 sin cabecera Xing tiene un largo estimado que puede pasar del
    // audio real, y leer esa cola devuelve false (el lector ya puso ceros). Devuelve si leyó entero.
    bool readBlock (juce::AudioFormatReader& reader, juce::AudioBuffer<float>& dest, int destStart, int numSamples, juce::int64 fileStart)
    {
        if (reader.read (&dest, destStart, numSamples, fileStart, true, true))
            return true;
        // Al fallar, JUCE no convierte los enteros a float ni duplica el canal mono
        if (! reader.usesFloatingPointData)
            for (int ch = 0; ch < dest.getNumChannels(); ++ch)
            {
                float* d = dest.getWritePointer (ch, destStart);
                juce::FloatVectorOperations::convertFixedToFloat (d, reinterpret_cast<const int*> (d), 1.0f / (float) 0x7fffffff, numSamples);
            }
        if (reader.numChannels == 1 && dest.getNumChannels() > 1)
            dest.copyFrom (1, destStart, dest, 0, destStart, numSamples);
        return false;
    }

    // Lee muestras del archivo desde `fileStart` (puede ser negativo o pasar del final: ceros) en `dest` (estéreo)
    void readRaw (juce::AudioFormatReader& reader, juce::AudioBuffer<float>& dest, juce::int64 fileStart)
    {
        dest.clear();
        const juce::int64 from = juce::jmax ((juce::int64) 0, fileStart);
        const juce::int64 to = juce::jmin (fileStart + dest.getNumSamples(), reader.lengthInSamples);
        if (to > from)
            readBlock (reader, dest, (int) (from - fileStart), (int) (to - from), from);
    }

    // Unión entre dos tramos seguidos de la misma fuente que continúan el mismo audio (sin salto en la
    // fuente y con casi el mismo estiramiento, con el mismo tono): lo que suena en el fundido es el
    // mismo audio en los dos, así que las ganancias deben sumar 1 (seno² y coseno²). Con seno y coseno
    // (igual potencia, lo justo entre canciones distintas, sin correlación) la unión subiría 3 dB.
    // El desfase entre ambos en el fundido es a lo más el salto en la fuente más F·|1/r1 - 1/r2|.
    bool isCoherentJoin (const MixSegment& out, const MixPlacement& pOut, const MixSegment& in, const MixPlacement& pIn)
    {
        if (out.source != in.source || out.transpose != in.transpose || pIn.fadeIn < mix::edgeFadeSeconds
            || pOut.ratio <= 0.0 || pIn.ratio <= 0.0)
            return false;
        const double offset = std::abs (pIn.srcStart - pOut.srcEnd) + pIn.fadeIn * std::abs (1.0 / pIn.ratio - 1.0 / pOut.ratio);
        return offset < coherentOffset;
    }
}

//==============================================================================
const MixStemSettings* MixSegment::stemSettings (const juce::String& stemFile) const
{
    for (auto& st : stems)
        if (st.stem == stemFile)
            return &st;
    // Una parte de la batería ("drums_kick.wav") sigue los ajustes de la pista de la que salió ("drums.wav")
    for (auto& st : stems)
        if (const auto base = st.stem.upToLastOccurrenceOf (".", false, false); base.isNotEmpty() && stemFile.startsWith (base + "_"))
            return &st;
    return nullptr;
}

MixStemSettings& MixSegment::stemSettingsFor (const juce::String& stemFile)
{
    for (auto& st : stems)
        if (st.stem == stemFile)
            return st;
    MixStemSettings st;
    st.stem = stemFile;
    stems.push_back (st);
    return stems.back();
}

bool MixSegment::hasStemSettings() const
{
    return std::any_of (stems.begin(), stems.end(), [] (const MixStemSettings& st) { return ! st.isDefault(); });
}

juce::File MixProject::sourceFile (int index) const
{
    if (index < 0 || index >= (int) sources.size() || sources[(size_t) index].fileName.isEmpty())
        return {};
    return sourcesFolder().getChildFile (sources[(size_t) index].fileName);
}

double MixProject::effectiveBpm() const
{
    if (bpm > 0.0)
        return clampBpm (bpm);
    for (auto& seg : segments)
        if (auto* s = sourceAt (*this, seg.source))
        {
            double start = 0.0, end = 0.0;
            clampedRange (*s, seg, start, end);
            double detected = mix::segmentBpm (s->analysis, start, end);
            if (detected <= 0.0)
                detected = s->analysis.bpm;
            if (detected > 0.0)
                return clampBpm (detected);
            break;   // el primer tramo no tiene tempo detectado
        }
    return 120.0;
}

bool MixProject::save() const
{
    if (folder.getFullPathName().isEmpty() || ! folder.createDirectory().wasOk())
        return false;
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("bpm", bpm);
    obj->setProperty ("keepTempos", keepTempos);
    obj->setProperty ("levelLufs", levelLufs);
    obj->setProperty ("nextSegmentId", nextSegmentId);
    if (stemsKey.isNotEmpty())
        obj->setProperty ("stemsKey", stemsKey);
    juce::Array<juce::var> srcs, segs;
    for (auto& s : sources)
        srcs.add (toVar (s));
    for (auto& s : segments)
        segs.add (toVar (s));
    obj->setProperty ("sources", srcs);
    obj->setProperty ("segments", segs);
    return folder.getChildFile ("mix.json").replaceWithText (juce::JSON::toString (juce::var (obj)));
}

bool MixProject::load (const juce::File& mixFolder, MixProject& out)
{
    const auto file = mixFolder.getChildFile ("mix.json");
    if (! file.existsAsFile())
        return false;
    const auto v = juce::JSON::parse (file.loadFileAsString());
    if (! v.isObject())
        return false;

    MixProject p;
    p.folder = mixFolder;
    p.name = v.getProperty ("name", "").toString().trim();
    if (p.name.isEmpty())
        p.name = mixFolder.getFileName();
    p.bpm = clampBpm (finiteIn ((double) v.getProperty ("bpm", 0.0), 0.0, 400.0, 0.0));
    p.keepTempos = (bool) v.getProperty ("keepTempos", false);
    p.levelLufs = finiteIn ((double) v.getProperty ("levelLufs", 0.0), -30.0, 0.0, 0.0);
    p.nextSegmentId = (int) finiteIn ((double) v.getProperty ("nextSegmentId", 1), 1.0, 1.0e9, 1.0);
    if (p.levelLufs > -6.0)
        p.levelLufs = 0.0;   // fuera de lo razonable (o 0): sin nivelar
    p.stemsKey = v.getProperty ("stemsKey", "").toString().trim();

    if (auto* arr = v.getProperty ("sources", juce::var()).getArray())
        for (auto& sv : *arr)
        {
            MixSource s;
            // Solo un nombre de archivo dentro de fuentes/ (un mix.json editado a mano no debe salir de ahí)
            s.fileName = juce::File::createLegalFileName (sv.getProperty ("file", "").toString());
            if (s.fileName == "." || s.fileName == "..")
                s.fileName.clear();
            s.name = sv.getProperty ("name", "").toString();
            if (s.name.trim().isEmpty())
                s.name = s.fileName.upToLastOccurrenceOf (".", false, false);
            s.length = finiteIn ((double) sv.getProperty ("length", 0.0), 0.0, maxSeconds, 0.0);
            s.analysis = Library::analysisFromVar (sv.getProperty ("analysis", juce::var()));
            p.sources.push_back (s);
        }

    // Sin largo (0 = desconocido: nada acotaría los tramos) se mide el archivo
    {
        std::unique_ptr<juce::AudioFormatManager> formats;
        for (int i = 0; i < (int) p.sources.size(); ++i)
        {
            auto& s = p.sources[(size_t) i];
            if (s.length > 0.0 || ! p.sourceFile (i).existsAsFile())
                continue;
            if (formats == nullptr)
            {
                formats = std::make_unique<juce::AudioFormatManager>();
                formats->registerBasicFormats();
            }
            std::unique_ptr<juce::AudioFormatReader> reader (formats->createReaderFor (p.sourceFile (i)));
            if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
                s.length = juce::jmin (maxSeconds, (double) reader->lengthInSamples / reader->sampleRate);
        }
    }

    if (auto* arr = v.getProperty ("segments", juce::var()).getArray())
        for (auto& sv : *arr)
        {
            MixSegment s;
            s.source = (int) finiteIn ((double) sv.getProperty ("source", -1), -1.0, 1.0e6, -1.0);
            if (s.source < 0 || s.source >= (int) p.sources.size())
                continue;   // fuente inexistente
            // Dentro de la fuente (si se conoce su largo) y nunca más de maxSeconds
            const double limit = p.sources[(size_t) s.source].length > 0.0 ? p.sources[(size_t) s.source].length : maxSeconds;
            s.start = finiteIn ((double) sv.getProperty ("start", 0.0), 0.0, limit, 0.0);
            s.end = finiteIn ((double) sv.getProperty ("end", 0.0), 0.0, limit, 0.0);
            if (! (s.end > s.start))
                continue;
            s.label = sv.getProperty ("label", "").toString();
            s.playBpm = clampBpm (finiteIn ((double) sv.getProperty ("playBpm", 0.0), 0.0, 400.0, 0.0));
            s.transpose = (int) std::lround (finiteIn ((double) sv.getProperty ("transpose", 0), -12.0, 12.0, 0.0));
            s.gainDb = (float) finiteIn ((double) sv.getProperty ("gainDb", 0.0), -24.0, 12.0, 0.0);
            s.fadeBeats = finiteIn ((double) sv.getProperty ("fadeBeats", 0.0), 0.0, 16.0, 0.0);
            s.id = (int) finiteIn ((double) sv.getProperty ("id", 0), 0.0, 1.0e9, 0.0);
            s.silenceBars = (int) finiteIn ((double) sv.getProperty ("silenceBars", 0), 0.0, (double) MixSegment::maxSilenceBars, 0.0);
            s.overlapBars = (int) finiteIn ((double) sv.getProperty ("overlapBars", 0), 0.0, (double) MixSegment::maxOverlapBars, 0.0);
            s.overlapBlend = (int) finiteIn ((double) sv.getProperty ("overlapBlend", 0), 0.0, 2.0, 0.0);
            if (auto* stems = sv.getProperty ("stems", juce::var()).getArray())
                for (auto& stv : *stems)
                {
                    MixStemSettings st;
                    st.stem = juce::File::createLegalFileName (stv.getProperty ("stem", "").toString());
                    if (st.stem.isEmpty() || st.stem == "." || st.stem == ".."
                        || std::any_of (s.stems.begin(), s.stems.end(), [&st] (const MixStemSettings& o) { return o.stem == st.stem; }))
                        continue;
                    constexpr double bars = MixStemSettings::maxBars, shift = MixStemSettings::maxShift;
                    st.muted = (bool) stv.getProperty ("muted", false);
                    st.leadBars = (int) finiteIn ((double) stv.getProperty ("leadBars", 0), 0.0, bars, 0.0);
                    st.tailBars = (int) finiteIn ((double) stv.getProperty ("tailBars", 0), 0.0, bars, 0.0);
                    st.shiftBeats = (int) std::lround (finiteIn ((double) stv.getProperty ("shiftBeats", 0), -shift, shift, 0.0));
                    if (! st.isDefault())
                        s.stems.push_back (st);
                }
            p.segments.push_back (s);
        }
    p.ensureSegmentIds();   // mix.json de antes (sin ids): en orden, siempre los mismos

    out = std::move (p);
    return true;
}

void MixProject::ensureSegmentIds()
{
    for (auto& s : segments)
        nextSegmentId = juce::jmax (nextSegmentId, s.id + 1);
    std::set<int> seen;
    for (auto& s : segments)
    {
        if (s.id <= 0 || seen.count (s.id) > 0)
            s.id = nextSegmentId++;
        seen.insert (s.id);
    }
}

std::vector<int> MixProject::usedSources() const
{
    std::vector<int> used;
    for (auto& s : segments)
        if (s.source >= 0 && s.source < (int) sources.size() && std::find (used.begin(), used.end(), s.source) == used.end())
            used.push_back (s.source);
    return used;
}

juce::File MixProject::stemsFolderFor (const juce::File& mixFolder, const juce::String& sourceFileName, const juce::String& optionsKey)
{
    auto base = juce::File::createLegalFileName (sourceFileName);
    if (base.isEmpty() || base == "." || base == "..")
        base = "fuente";
    auto options = juce::File::createLegalFileName (optionsKey.replaceCharacter ('|', '-'));
    if (options.isEmpty() || options == "." || options == "..")
        options = "pistas";
    return mixFolder.getChildFile ("pistas").getChildFile (base).getChildFile (options);
}

juce::File MixProject::stemsFolder (int source, const juce::String& optionsKey) const
{
    if (source < 0 || source >= (int) sources.size())
        return {};
    return stemsFolderFor (folder, sources[(size_t) source].fileName, optionsKey);
}

juce::StringArray MixProject::cachedStems (int source, const juce::String& optionsKey) const
{
    const auto dir = stemsFolder (source, optionsKey);
    const auto info = juce::JSON::parse (dir.getChildFile ("pistas.json").loadFileAsString());
    juce::StringArray files;
    if (! info.isObject() || info.getProperty ("options", "").toString() != optionsKey)
        return {};
    if (auto* arr = info.getProperty ("files", juce::var()).getArray())
        for (auto& f : *arr)
        {
            const auto stemFile = juce::File::createLegalFileName (f.toString());
            if (stemFile.isEmpty() || ! dir.getChildFile (stemFile).existsAsFile())
                return {};   // falta un archivo: hay que separar de nuevo
            files.add (stemFile);
        }
    return files;
}

bool MixProject::storeStems (const juce::File& mixFolder, const juce::String& sourceFileName, const juce::File& resultFolder,
                             const juce::String& optionsKey)
{
    const auto dir = stemsFolderFor (mixFolder, sourceFileName, optionsKey);
    dir.deleteRecursively();
    if (! dir.createDirectory().wasOk())
        return false;
    juce::Array<juce::var> files;
    for (const auto& f : resultFolder.findChildFiles (juce::File::findFiles, false))
        if (Library::isAudioFile (f) && f.moveFileTo (dir.getChildFile (f.getFileName())))
            files.add (f.getFileName());
    if (files.isEmpty())
        return false;
    auto* o = new juce::DynamicObject();
    o->setProperty ("options", optionsKey);
    o->setProperty ("source", sourceFileName);
    o->setProperty ("files", files);
    return dir.getChildFile ("pistas.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

juce::String MixProject::bestCachedStemsKey (const juce::String& preferred) const
{
    auto used = usedSources();
    if (used.empty())
        for (int i = 0; i < (int) sources.size(); ++i)
            used.push_back (i);
    std::map<juce::String, int> coverage;
    for (int i : used)
    {
        const auto all = stemsFolderFor (folder, sources[(size_t) i].fileName, {}).getParentDirectory();
        for (const auto& dir : all.findChildFiles (juce::File::findDirectories, false))
        {
            const auto key = juce::JSON::parse (dir.getChildFile ("pistas.json").loadFileAsString()).getProperty ("options", "").toString();
            if (key.isNotEmpty() && ! cachedStems (i, key).isEmpty())
                ++coverage[key];
        }
    }
    juce::String best;
    int bestCount = 0;
    for (auto& [key, count] : coverage)
        if (count > bestCount || (count == bestCount && key == preferred))
        {
            best = key;
            bestCount = count;
        }
    return best;
}

juce::StringArray MixProject::list (const juce::File& mixesRoot)
{
    juce::StringArray names;
    if (! mixesRoot.isDirectory())
        return names;
    for (const auto& d : mixesRoot.findChildFiles (juce::File::findDirectories, false))
        if (d.getChildFile ("mix.json").existsAsFile())
            names.add (d.getFileName());
    names.sort (true);
    return names;
}

std::vector<MixProject::ListEntry> MixProject::entries (const juce::File& mixesRoot)
{
    std::vector<ListEntry> out;
    for (auto& folderName : list (mixesRoot))
    {
        const auto folder = mixesRoot.getChildFile (folderName);
        auto name = juce::JSON::parse (folder.getChildFile ("mix.json")).getProperty ("name", "").toString().trim();
        out.push_back ({ folder, name.isEmpty() ? folderName : name });
    }
    std::stable_sort (out.begin(), out.end(), [] (const ListEntry& a, const ListEntry& b)
    {
        const int c = a.name.compareIgnoreCase (b.name);
        return c != 0 ? c < 0 : a.folder.getFileName().compareIgnoreCase (b.folder.getFileName()) < 0;
    });
    return out;
}

juce::File MixProject::create (const juce::File& mixesRoot, const juce::String& nameIn)
{
    auto display = nameIn.trim();
    if (display.isEmpty())
        display = "Mix";
    const auto legal = Library::legalFolderName (display, "Mix");
    if (! mixesRoot.createDirectory().wasOk())
        return {};
    const auto dir = mixesRoot.getNonexistentChildFile (legal, "", false);
    if (! dir.createDirectory().wasOk())
        return {};
    MixProject p;
    p.name = display;
    p.folder = dir;
    p.levelLufs = defaultLevelLufs;
    if (! p.save())
    {
        dir.deleteRecursively();
        return {};
    }
    return dir;
}

int MixProject::addSource (const juce::File& audioFile, juce::AudioFormatManager& formats)
{
    double seconds = 0.0;
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return -1;
        seconds = (double) reader->lengthInSamples / reader->sampleRate;
    }
    const auto dir = sourcesFolder();
    if (folder.getFullPathName().isEmpty() || ! dir.createDirectory().wasOk())
        return -1;
    auto base = juce::File::createLegalFileName (audioFile.getFileNameWithoutExtension()).trim().trimCharactersAtStart (".");
    if (base.isEmpty())
        base = "fuente";
    base = base.substring (0, 100).trim();
    const auto dest = dir.getNonexistentChildFile (base, audioFile.getFileExtension(), false);
    if (! audioFile.copyFileTo (dest))
        return -1;

    MixSource s;
    s.name = audioFile.getFileNameWithoutExtension();
    s.fileName = dest.getFileName();
    s.length = seconds;
    sources.push_back (s);
    return (int) sources.size() - 1;
}

void MixProject::removeSource (int index)
{
    if (index < 0 || index >= (int) sources.size())
        return;
    const auto file = sourceFile (index);
    const auto fileName = sources[(size_t) index].fileName;
    sources.erase (sources.begin() + index);
    segments.erase (std::remove_if (segments.begin(), segments.end(), [index] (const MixSegment& s) { return s.source == index; }),
                    segments.end());
    for (auto& s : segments)
        if (s.source > index)
            --s.source;
    // El archivo (y sus pistas separadas) se borra solo si ninguna otra fuente lo usa
    const bool shared = std::any_of (sources.begin(), sources.end(), [&fileName] (const MixSource& s) { return s.fileName == fileName; });
    if (! shared && file.existsAsFile() && file.isAChildOf (sourcesFolder()))
        file.deleteFile();
    if (const auto stems = stemsFolderFor (folder, fileName, {}).getParentDirectory(); ! shared && stems.isDirectory() && stems.isAChildOf (folder))
        stems.deleteRecursively();   // todas sus separaciones
}

//==============================================================================
namespace mix
{
    double segmentBpm (const Analysis& a, double start, double end)
    {
        int first = -1, last = -1;
        for (int i = 0; i < (int) a.beats.size(); ++i)
        {
            const double t = a.beats[(size_t) i].seconds;
            if (t >= start - beatTolerance && t <= end + beatTolerance)
            {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        if (first < 0 || last <= first)
            return 0.0;
        const double span = a.beats[(size_t) last].seconds - a.beats[(size_t) first].seconds;
        return span > 1.0e-6 ? 60.0 * (double) (last - first) / span : 0.0;
    }

    double snapToBeat (const Analysis& a, double seconds, bool downbeatsOnly)
    {
        const bool onlyDownbeats = downbeatsOnly
            && std::any_of (a.beats.begin(), a.beats.end(), [] (const Beat& b) { return b.beatInBar == 1; });
        double best = seconds, bestDistance = std::numeric_limits<double>::max();
        for (auto& b : a.beats)
            if (! onlyDownbeats || b.beatInBar == 1)
                if (const double d = std::abs (b.seconds - seconds); d < bestDistance)
                {
                    bestDistance = d;
                    best = b.seconds;
                }
        return best;
    }

    double previousDownbeat (const Analysis& a, double seconds)
    {
        double found = seconds;
        for (auto& b : a.beats)
            if (b.beatInBar == 1 && b.seconds < seconds - beatTolerance)
                found = b.seconds;
        return found;
    }

    double nextDownbeat (const Analysis& a, double seconds)
    {
        for (auto& b : a.beats)
            if (b.beatInBar == 1 && b.seconds > seconds + beatTolerance)
                return b.seconds;
        return seconds;
    }

    int barNumberAt (const Analysis& a, double seconds)
    {
        int bars = 0;
        for (auto& b : a.beats)
            if (b.beatInBar == 1 && b.seconds <= seconds + beatTolerance)
                ++bars;
        return bars;
    }

    int barsBetween (const Analysis& a, double start, double end)
    {
        int bars = 0;
        for (auto& b : a.beats)
            if (b.beatInBar == 1 && b.seconds >= start - beatTolerance && b.seconds < end - beatTolerance)
                ++bars;
        return bars;
    }

    int barCount (const Analysis& a)
    {
        int bars = 0;
        for (auto& b : a.beats)
            bars += b.beatInBar == 1 ? 1 : 0;
        return bars;
    }

    bool barRange (const Analysis& a, double length, int first, int last, double& start, double& end)
    {
        std::vector<double> downbeats;
        for (auto& b : a.beats)
            if (b.beatInBar == 1)
                downbeats.push_back (b.seconds);
        const int n = (int) downbeats.size();
        if (n == 0)
            return false;
        if (first > last)
            std::swap (first, last);
        first = juce::jlimit (0, n, first);
        last = juce::jlimit (juce::jmax (1, first), n, last);
        start = first == 0 ? 0.0 : downbeats[(size_t) first - 1];
        end = last < n ? downbeats[(size_t) last] : juce::jmax (length, downbeats[(size_t) n - 1]);
        return end > start;
    }

    int meterOf (const MixSource& source)
    {
        const int meter = source.analysis.meter;
        return meter >= 2 && meter <= 12 ? meter : 4;
    }

    std::vector<MixPlacement> layout (const MixProject& project)
    {
        std::vector<MixPlacement> out (project.segments.size());
        const double mixBpm = project.effectiveBpm();
        double position = 0.0;
        int previous = -1;   // último tramo que suena
        for (size_t i = 0; i < project.segments.size(); ++i)
        {
            const auto& seg = project.segments[i];
            auto& p = out[i];
            p.outStart = p.outEnd = position;
            p.srcStart = seg.start;
            p.srcEnd = seg.end;
            const auto* s = sourceAt (project, seg.source);
            if (s == nullptr)
                continue;   // fuente inexistente: no suena
            clampedRange (*s, seg, p.srcStart, p.srcEnd);

            const double detected = segmentBpm (s->analysis, p.srcStart, p.srcEnd);
            if (seg.playBpm > 0.0)
                p.playBpm = clampBpm (seg.playBpm);
            else
                p.playBpm = project.keepTempos && detected > 0.0 ? detected : mixBpm;
            p.srcBpm = detected > 0.0 ? detected : p.playBpm;   // sin tempo detectado no se estira
            p.ratio = p.srcBpm / p.playBpm;
            if (p.srcEnd <= p.srcStart)
                continue;   // largo 0: no suena

            // Compases de silencio antes: el tramo empieza ese lapso después del anterior (a su tempo y su compás)
            p.barSeconds = meterOf (*s) * 60.0 / p.playBpm;
            p.silence = seg.silenceBars > 0 ? juce::jlimit (0, MixSegment::maxSilenceBars, seg.silenceBars) * p.barSeconds : 0.0;
            const double duration = (p.srcEnd - p.srcStart) * p.ratio;
            if (previous >= 0 && p.silence <= 0.0 && seg.overlapBars > 0)
            {
                // Solape: los compases pedidos que caben, menos que la duración de cada uno (los inicios siguen en orden)
                const auto& prev = out[(size_t) previous];
                const double room = juce::jmin (prev.outEnd - prev.outStart, duration) - 0.01;
                const int bars = juce::jmin (seg.overlapBars, MixSegment::maxOverlapBars, (int) std::floor (room / p.barSeconds));
                p.overlap = bars > 0 ? bars * p.barSeconds : 0.0;
            }
            p.outStart = position + p.silence - p.overlap;
            p.outEnd = p.outStart + duration;
            if (previous >= 0 && p.silence <= 0.0)
            {
                auto& prev = out[(size_t) previous];
                if (p.overlap > 0.0)
                {
                    // Solape: el que entra empieza con su propio audio (sin audio previo); el anterior se apaga a lo
                    // largo del solape (o al final, con "los dos completos")
                    const int blend = juce::jlimit (0, 2, seg.overlapBlend);
                    p.riseIn = blend == MixSegment::blendCrossfade ? p.overlap : 0.0;
                    prev.fadeOut = blend == MixSegment::blendBoth ? juce::jmin (cutFadeSeconds, 0.5 * (prev.outEnd - prev.outStart)) : p.overlap;
                }
                else
                {
                    // Fundido cruzado en la unión: el tramo que entra empieza a sonar `fadeIn` antes (con
                    // el audio que precede a su inicio en la fuente) y el anterior se apaga en ese lapso
                    double fade = seg.fadeBeats > 0.0 ? seg.fadeBeats * 60.0 / p.playBpm : cutFadeSeconds;
                    fade = juce::jmin (fade, p.srcStart * p.ratio);
                    fade = juce::jmin (fade, 0.5 * duration, 0.5 * (prev.outEnd - prev.outStart));
                    p.fadeIn = juce::jmax (0.0, fade);
                    prev.fadeOut = p.fadeIn;
                }
            }
            p.fadeOut = juce::jmin (cutFadeSeconds, 0.5 * duration);   // si viene otro tramo, lo corrige
            position = p.outEnd;
            previous = (int) i;
        }
        return out;
    }

    double length (const MixProject& project)
    {
        const auto places = layout (project);
        return places.empty() ? 0.0 : places.back().outEnd;
    }

    void describeSong (const MixProject& project, SongInfo& out)
    {
        const auto places = layout (project);
        out.name = project.name;

        Analysis analysis;
        struct PlacedBeat
        {
            Beat beat;
            size_t segment;   // índice del tramo del que viene (en orden de reproducción)
            double minGap;    // medio tiempo a su tempo en el mix: en una unión, más cerca que esto es un duplicado
        };
        std::vector<PlacedBeat> beats;
        std::vector<Chord> chords;
        std::vector<TempoRegion> regions;
        std::vector<SongMarker> markers;
        // Dónde empieza el tramo siguiente que suena (con su silencio): los tiempos y acordes de cada tramo llegan hasta
        // ahí (en un solape, desde donde entra el siguiente mandan los suyos)
        std::vector<double> nextStart (places.size(), std::numeric_limits<double>::max());
        {
            double following = std::numeric_limits<double>::max();
            for (size_t k = places.size(); k-- > 0;)
            {
                nextStart[k] = following;
                if (places[k].outEnd > places[k].outStart)
                    following = places[k].outStart - places[k].silence;
            }
        }
        int first = -1;
        for (size_t i = 0; i < places.size(); ++i)
        {
            const auto& p = places[i];
            const auto& seg = project.segments[i];
            if (p.outEnd <= p.outStart)
                continue;
            const auto& src = project.sources[(size_t) seg.source];
            const auto& an = src.analysis;
            if (first < 0)
            {
                first = (int) i;
                analysis.key = music::transposeKey (an.key, seg.transpose);
                analysis.meter = an.meter;
                analysis.bpm = p.playBpm;
            }
            // Compases de silencio antes del tramo: el click sigue con su tempo y su compás (cuenta para entrar)
            if (p.silence > 0.0)
            {
                const int meter = meterOf (src);
                const int count = juce::jlimit (0, MixSegment::maxSilenceBars, seg.silenceBars) * meter;
                const double beat = 60.0 / juce::jmax (1.0, p.playBpm);
                for (int k = 0; k < count; ++k)
                    beats.push_back ({ Beat { p.outStart - p.silence + k * beat, k % meter + 1 }, i, 30.0 / juce::jmax (1.0, p.playBpm) });
            }
            for (auto& b : an.beats)
                if (b.seconds >= p.srcStart - beatTolerance && b.seconds < p.srcEnd - beatTolerance)
                {
                    // Un tiempo apenas antes del corte (que se ajustó a la transiente) es el del inicio del
                    // tramo: va en la unión; si no, el primer "1" del mix quedaría en negativo y se perdería
                    const double t = b.seconds < p.srcStart ? p.outStart : toMix (p, b.seconds);
                    if (t >= 0.0 && t < nextStart[i])
                        beats.push_back ({ Beat { t, b.beatInBar }, i, 30.0 / juce::jmax (1.0, p.playBpm) });
                }
            for (auto& c : an.chords)
            {
                const double from = juce::jmax (c.start, p.srcStart), to = juce::jmin (c.end, p.srcEnd);
                if (to <= from)
                    continue;
                Chord mc { juce::jmax (0.0, toMix (p, from)), juce::jmin (toMix (p, to), nextStart[i]), music::transposeChord (c.name, seg.transpose) };
                if (mc.end > mc.start)
                    chords.push_back (mc);
            }
            TempoRegion r;
            r.start = p.outStart - p.silence;   // el silencio va al tempo del tramo que entra
            r.origBpm = p.playBpm;
            r.playBpm = 0.0;
            regions.push_back (r);
            SongMarker m;
            m.name = seg.label.trim().isNotEmpty() ? seg.label.trim() : src.name;
            m.seconds = p.outStart;
            m.fromMix = true;
            markers.push_back (m);
        }

        // Tiempos ordenados. Dentro de un tramo, de dos a menos de 30 ms queda el primero. En una unión
        // (tiempos de tramos distintos) dos a menos de medio tiempo son el mismo pulso: manda el del
        // tramo que entra, porque su número en el compás es el que siguen los tiempos que vienen.
        std::stable_sort (beats.begin(), beats.end(), [] (const PlacedBeat& x, const PlacedBeat& y) { return x.beat.seconds < y.beat.seconds; });
        std::vector<PlacedBeat> kept;
        for (auto& b : beats)
        {
            if (kept.empty())
            {
                kept.push_back (b);
                continue;
            }
            auto& last = kept.back();
            const double gap = b.beat.seconds - last.beat.seconds;
            const bool join = b.segment != last.segment;
            if (gap >= (join ? juce::jmax (0.03, juce::jmin (b.minGap, last.minGap)) : 0.03))
                kept.push_back (b);
            else if (b.segment > last.segment)
                last = b;
        }
        for (auto& b : kept)
            analysis.beats.push_back (b.beat);
        // Acordes ordenados; los iguales que se tocan en una unión quedan en uno
        std::stable_sort (chords.begin(), chords.end(), [] (const Chord& x, const Chord& y) { return x.start < y.start; });
        for (auto& c : chords)
        {
            if (! analysis.chords.empty() && analysis.chords.back().name == c.name
                && std::abs (c.start - analysis.chords.back().end) < 0.05)
                analysis.chords.back().end = juce::jmax (analysis.chords.back().end, c.end);
            else
                analysis.chords.push_back (c);
        }
        out.analysis = analysis;

        out.tempoRegions = regions;
        if (! out.tempoRegions.empty())
        {
            out.tempoRegions.front().start = 0.0;
            out.sortTempoRegions();
            out.mergeEqualTempoRegions();
        }
        out.markers = markers;
        out.sortMarkers();

        if (first >= 0)
            out.bpm = juce::jlimit (20.0, 400.0, places[(size_t) first].playBpm);
        out.clickOffset = analysis.beats.empty() ? 0.0 : analysis.beats.front().seconds;
        out.clips.clear();
        out.notes.clear();
        out.playBpm = 0.0;
        out.transpose = 0;
        // Nivelado sin medir
        out.loudnessLufs = out.truePeakDb = unmeasuredDb;
        out.songGainDb = 0.0;
        out.levelingEnabled = false;
        out.headGainDb = 0.0;
        out.headLufs = out.headTruePeakDb = unmeasuredDb;
        out.headStemGainsDb.clear();
        out.headStemLufs.clear();
        out.stemSongLufs.clear();
        out.fitStemArrays();
    }

    bool readRange (const juce::File& file, double startSeconds, double endSeconds, double sampleRate,
                    juce::AudioFormatManager& formats, juce::AudioBuffer<float>& out)
    {
        // Rangos absurdos (no finitos, o más muestras de las que caben en un int) no se leen
        constexpr double maxInt = (double) std::numeric_limits<int>::max();
        if (! std::isfinite (startSeconds) || ! std::isfinite (endSeconds) || ! (sampleRate > 0.0)
            || std::abs (startSeconds) > 1.0e7 || std::abs (endSeconds) > 1.0e7
            || (endSeconds - startSeconds) * sampleRate > maxInt)
            return false;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || (endSeconds - startSeconds) * reader->sampleRate + 1000.0 > maxInt)
            return false;
        const int outLen = (int) juce::jmax ((juce::int64) 0, (juce::int64) std::llround ((endSeconds - startSeconds) * sampleRate));
        out.setSize (2, outLen);
        out.clear();
        if (outLen == 0)
            return true;

        const double fileRate = reader->sampleRate;
        if (std::abs (fileRate - sampleRate) < 1.0e-3)
        {
            readRaw (*reader, out, (juce::int64) std::llround (startSeconds * fileRate));
            return true;
        }

        // Remuestreo con sinc enventanado. El interpolador de JUCE responde con `latency` muestras de
        // entrada de retraso y empieza con su historia en cero: se le entregan `context` muestras
        // anteriores al inicio y una primera salida descartable cuyo avance (`phase`) deja la
        // siguiente salida exactamente en `startSeconds`, fracción de muestra incluida.
        constexpr int context = 100;   // la mitad del núcleo del sinc
        const double latency = (double) juce::WindowedSincInterpolator::getBaseLatency();
        const double ratio = fileRate / sampleRate;   // muestras de entrada por muestra de salida
        const double exact = startSeconds * fileRate;
        const double base = std::floor (exact);
        const double phase = (exact - base) + context + latency;
        const int inLen = (int) std::ceil (1.0 + phase + (double) outLen * ratio) + 4;
        juce::AudioBuffer<float> in (2, inLen);
        readRaw (*reader, in, (juce::int64) base - context);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::WindowedSincInterpolator interp;
            float discard = 0.0f;
            const float* src = in.getReadPointer (ch);
            const int used = interp.process (phase, src, &discard, 1, inLen, 0);
            interp.process (ratio, src + used, out.getWritePointer (ch), outLen, inLen - used, 0);
        }
        return true;
    }

    double refineToOnset (const juce::File& file, double seconds, juce::AudioFormatManager& formats)
    {
        double rate = renderSampleRate;
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
                return seconds;
            if (reader->sampleRate > 0.0)
                rate = reader->sampleRate;
        }
        constexpr double span = 0.06, window = 0.04;
        // A la frecuencia del archivo readRange no remuestrea y empieza en la muestra redondeada
        const double from = (double) std::llround ((seconds - span) * rate) / rate;
        juce::AudioBuffer<float> buffer;
        if (! readRange (file, from, from + 2.0 * span, rate, formats, buffer))
            return seconds;
        const double onset = arrangement::findOnset (buffer, rate, seconds - from, window);
        return onset >= 0.0 ? juce::jmax (0.0, from + onset) : seconds;
    }

    std::vector<float> computePeaks (const juce::File& file, juce::AudioFormatManager& formats, int binsPerSecond, double& lengthSeconds,
                                     const std::function<bool()>& shouldAbort)
    {
        lengthSeconds = 0.0;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || binsPerSecond <= 0)
            return {};
        const double rate = reader->sampleRate;
        const juce::int64 total = reader->lengthInSamples;
        lengthSeconds = (double) total / rate;
        auto binOf = [rate, binsPerSecond] (juce::int64 sample) { return (juce::int64) std::floor ((double) sample * binsPerSecond / rate); };
        const juce::int64 bins = binOf (total - 1) + 1;
        std::vector<float> lows ((size_t) bins, std::numeric_limits<float>::max());
        std::vector<float> highs ((size_t) bins, std::numeric_limits<float>::lowest());

        constexpr int blockSize = 65536;
        juce::AudioBuffer<float> block (2, blockSize);
        for (juce::int64 pos = 0; pos < total; pos += blockSize)
        {
            if (shouldAbort && shouldAbort())
                return {};
            const int n = (int) juce::jmin ((juce::int64) blockSize, total - pos);
            block.clear();
            // Si falla (la cola de un MP3 con el largo estimado de más), lo leído vale y el resto
            // queda en silencio; no se sigue leyendo más allá
            const bool complete = readBlock (*reader, block, 0, n, pos);
            const float* l = block.getReadPointer (0);
            const float* r = block.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                const auto bin = (size_t) binOf (pos + i);
                const float lo = juce::jmin (l[i], r[i]), hi = juce::jmax (l[i], r[i]);
                lows[bin] = juce::jmin (lows[bin], lo);
                highs[bin] = juce::jmax (highs[bin], hi);
            }
            if (! complete)
                break;
        }

        std::vector<float> out ((size_t) bins * 2, 0.0f);
        for (size_t b = 0; b < (size_t) bins; ++b)
            if (lows[b] <= highs[b])
            {
                out[b * 2] = lows[b];
                out[b * 2 + 1] = highs[b];
            }
        return out;
    }

    namespace
    {
        // Cómo suena una capa de un tramo (su fuente entera o una de sus pistas): qué ventana de la fuente lee y cómo
        // entra y sale. Por omisión, la del tramo (con sus fundidos).
        struct LayerWindow
        {
            double lead = 0.0;       // segundos del mix antes de outStart en que ya suena entera (pista que entra antes)
            double tail = 0.0;       // segundos del mix después de outEnd en que sigue (pista que sigue)
            double tailFade = 0.0;   // fundido al final de la cola
            double shift = 0.0;      // segundos del mix que se corre el audio (positivo = más tarde)
        };

        // Una pista de un tramo que se arma con sus pistas (ajustes por pista)
        struct StemLayer
        {
            juce::String file;
            bool muted = false;
            LayerWindow window;
        };

        // Lo que comparten el render del mix y el de sus pistas: dónde cae cada tramo que suena y cómo entra y sale
        struct SegmentPlan
        {
            int index = -1;                       // en project.segments
            MixPlacement place;
            bool crossfade = false, squareIn = false, squareOut = false;
            double pre = 0.0, fadeOut = 0.0;      // audio previo del fundido de entrada; fundido de salida
            double rise = 0.0;                    // solape en fundido cruzado: sube (seno) en [outStart, outStart + rise]
            int m0 = 0, m1 = 0;                   // muestras del mix que ocupa
            int reach0 = 0, reach1 = 0;           // las que ocupa con las pistas que entran antes o siguen
            int limit = 0;                        // muestras del mix
            juce::File stemsFolder;               // con ajustes por pista y la fuente separada: sus pistas, que lo arman
            std::vector<StemLayer> stems;         // (vacío = se arma con la fuente)
        };

        // Muestras del mix donde suena más de una capa (fundido cruzado, solape, pista que entra antes o que sigue)
        struct LayerRegion
        {
            int start = 0, end = 0;
            std::vector<int> owners;              // tramos (índices del proyecto) a los que se atribuye lo que haga el limitador
        };

        struct RenderPlan
        {
            std::vector<SegmentPlan> segments;    // solo los que suenan, en orden
            std::vector<LayerRegion> layers;      // ordenadas y separadas (con lo que tarda el limitador en volver)
            int totalSamples = 0;
            int count = 0;                        // tramos del proyecto (progreso y `levels`)
        };

        int limiterLookaheadSamples (double sampleRate) { return juce::jmax (1, (int) std::lround (limiterLookahead * sampleRate)); }
        int limiterReleaseSamples (double sampleRate)   { return juce::jmax (1, (int) std::lround (limiterRelease * sampleRate)); }

        // Muestras del mix que ocupa una capa
        void layerRange (const SegmentPlan& sp, const LayerWindow& w, double sampleRate, int& r0, int& r1)
        {
            r0 = w.lead > 0.0 ? juce::jlimit (0, sp.limit, (int) std::floor ((sp.place.outStart - w.lead) * sampleRate)) : sp.m0;
            r1 = w.tail > 0.0 ? juce::jlimit (r0, sp.limit, (int) std::ceil ((sp.place.outEnd + w.tail) * sampleRate)) : sp.m1;
        }

        // Ventana de una pista según sus ajustes en el tramo (compases con su compás y su tempo)
        LayerWindow windowFor (const MixStemSettings* st, const MixPlacement& p)
        {
            LayerWindow w;
            if (st == nullptr)
                return w;
            const double beat = 60.0 / juce::jmax (1.0, p.playBpm);
            w.lead = juce::jlimit (0, MixStemSettings::maxBars, st->leadBars) * p.barSeconds;
            w.tail = juce::jlimit (0, MixStemSettings::maxBars, st->tailBars) * p.barSeconds;
            w.tailFade = juce::jmin (beat, w.tail);
            w.shift = juce::jlimit (-MixStemSettings::maxShift, MixStemSettings::maxShift, st->shiftBeats) * beat;
            return w;
        }

        // `stemsKey`: las pistas separadas con que se arman los tramos con ajustes por pista (vacía = ninguno se arma así)
        bool makePlan (const MixProject& project, double sampleRate, const juce::String& stemsKey, RenderPlan& plan, juce::String& error)
        {
            plan = RenderPlan();
            const auto places = layout (project);
            plan.count = (int) places.size();
            double totalSeconds = places.empty() ? 0.0 : places.back().outEnd;
            if (! (totalSeconds > 0.0) || ! (sampleRate > 0.0))
            {
                error = "El mix no tiene tramos.";
                return false;
            }

            // Tramos con ajustes por pista cuya fuente está separada con esas opciones: se arman con sus pistas (una pista
            // que sigue después del último tramo alarga el mix)
            std::vector<std::vector<StemLayer>> stemLayers (places.size());
            std::vector<juce::File> stemFolders (places.size());
            if (stemsKey.isNotEmpty())
                for (size_t i = 0; i < places.size(); ++i)
                {
                    const auto& seg = project.segments[i];
                    if (places[i].outEnd <= places[i].outStart || ! seg.hasStemSettings())
                        continue;
                    for (auto& file : project.cachedStems (seg.source, stemsKey))
                    {
                        const auto* st = seg.stemSettings (file);
                        StemLayer layer;
                        layer.file = file;
                        layer.muted = st != nullptr && st->muted;
                        layer.window = windowFor (st, places[i]);
                        if (! layer.muted)
                            totalSeconds = juce::jmax (totalSeconds, places[i].outEnd + layer.window.tail);
                        stemLayers[i].push_back (layer);
                    }
                    if (! stemLayers[i].empty())
                        stemFolders[i] = project.stemsFolder (seg.source, stemsKey);
                }

            if (! (totalSeconds <= maxSeconds) || ! (std::ceil (totalSeconds * sampleRate) <= (double) std::numeric_limits<int>::max()))
            {
                error = tr ("El mix dura más de 4 horas.");
                return false;
            }
            plan.totalSamples = (int) std::ceil (totalSeconds * sampleRate);

            // Uniones que continúan el mismo audio de la fuente: fundido de ganancias que suman 1
            std::vector<bool> coherentIn (places.size(), false), coherentOut (places.size(), false);
            {
                int previous = -1;
                for (size_t i = 0; i < places.size(); ++i)
                {
                    if (places[i].outEnd <= places[i].outStart)
                        continue;
                    if (previous >= 0
                        && isCoherentJoin (project.segments[(size_t) previous], places[(size_t) previous], project.segments[i], places[i]))
                        coherentIn[i] = coherentOut[(size_t) previous] = true;
                    previous = (int) i;
                }
            }

            std::vector<LayerRegion> regions;
            auto addRegion = [&regions, &plan, sampleRate] (double from, double to, int owner)
            {
                const int a = juce::jlimit (0, plan.totalSamples, (int) std::floor (from * sampleRate));
                const int b = juce::jlimit (a, plan.totalSamples, (int) std::ceil (to * sampleRate));
                if (b > a)
                    regions.push_back ({ a, b, { owner } });
            };

            // Entrada: con fundido cruzado (seno sobre el audio que precede al inicio) o, en el primer tramo o sin
            // audio previo suficiente, 2 ms lineales desde la unión (con solape en fundido cruzado, un seno a lo largo
            // del solape). Salida: coseno. En una unión que continúa el mismo audio de la fuente, seno² y coseno²
            // (suman 1: sin subida).
            bool first = true;
            for (int i = 0; i < plan.count; ++i)
            {
                const auto& p = places[(size_t) i];
                if (p.outEnd <= p.outStart)
                    continue;
                const bool isFirst = first;
                first = false;
                SegmentPlan sp;
                sp.index = i;
                sp.place = p;
                sp.crossfade = ! isFirst && p.fadeIn >= edgeFadeSeconds;
                sp.pre = sp.crossfade ? p.fadeIn : 0.0;
                sp.rise = isFirst ? 0.0 : p.riseIn;
                const double duration = p.outEnd - p.outStart;
                sp.fadeOut = juce::jmin (juce::jmax (p.fadeOut, edgeFadeSeconds), duration);
                sp.m0 = juce::jlimit (0, plan.totalSamples, (int) std::floor ((p.outStart - sp.pre) * sampleRate));
                sp.m1 = juce::jlimit (sp.m0, plan.totalSamples, (int) std::ceil (p.outEnd * sampleRate));
                if (sp.m1 <= sp.m0)
                    continue;
                sp.squareIn = coherentIn[(size_t) i];
                sp.squareOut = coherentOut[(size_t) i];
                sp.limit = plan.totalSamples;
                sp.stems = std::move (stemLayers[(size_t) i]);
                sp.stemsFolder = stemFolders[(size_t) i];
                sp.reach0 = sp.m0;
                sp.reach1 = sp.m1;
                for (auto& layer : sp.stems)
                {
                    if (layer.muted)
                        continue;
                    int r0 = 0, r1 = 0;
                    layerRange (sp, layer.window, sampleRate, r0, r1);
                    sp.reach0 = juce::jmin (sp.reach0, r0);
                    sp.reach1 = juce::jmax (sp.reach1, r1);
                    if (layer.window.lead > 0.0)
                        addRegion (p.outStart - layer.window.lead, p.outStart, i);
                    if (layer.window.tail > 0.0)
                        addRegion (p.outEnd, p.outEnd + layer.window.tail, i);
                }
                if (sp.crossfade && sp.pre > 1.5 * cutFadeSeconds)
                    addRegion (p.outStart - sp.pre, p.outStart, i);
                if (! isFirst && p.overlap > 0.0)
                    addRegion (p.outStart, p.outStart + p.overlap, i);
                plan.segments.push_back (std::move (sp));
            }

            // Zonas en orden; las que quedan más cerca de lo que tarda el limitador en volver van juntas (así sus
            // ganancias no se pisan)
            std::sort (regions.begin(), regions.end(), [] (const LayerRegion& x, const LayerRegion& y) { return x.start < y.start; });
            const int margin = limiterLookaheadSamples (sampleRate) + 5 * limiterReleaseSamples (sampleRate) + 1;
            for (auto& r : regions)
            {
                if (plan.layers.empty() || r.start > plan.layers.back().end + margin)
                {
                    plan.layers.push_back (r);
                    continue;
                }
                auto& last = plan.layers.back();
                last.end = juce::jmax (last.end, r.end);
                for (int o : r.owners)
                    if (std::find (last.owners.begin(), last.owners.end(), o) == last.owners.end())
                        last.owners.push_back (o);
            }
            return true;
        }

        // Una capa del tramo leída de `file` (su fuente o una de sus pistas), estirada y transpuesta si hace falta,
        // alineada con el mix: aligned[j] es la muestra r0 + j (ceros donde el archivo no tiene audio). Con la ventana
        // por omisión, r0 = m0 y el largo es m1 - m0. false si no se pudo leer (con `error`) o se abortó (sin error).
        bool readLayer (const MixProject& project, const SegmentPlan& sp, const LayerWindow& w, const juce::File& file,
                        const juce::String& what, double sampleRate, juce::AudioFormatManager& formats,
                        const std::function<bool()>& shouldAbort, juce::AudioBuffer<float>& aligned, int& r0, juce::String& error)
        {
            const auto& p = sp.place;
            const auto& seg = project.segments[(size_t) sp.index];
            int r1 = 0;
            layerRange (sp, w, sampleRate, r0, r1);
            // audio[offset + j] es la muestra r0 + j del mix, que corresponde al instante
            // srcStart + ((r0 + j) / sampleRate - outStart - shift) / ratio de la fuente
            juce::AudioBuffer<float> audio;
            juce::int64 offset = 0;
            const bool stretch = std::abs (p.ratio - 1.0) > 1.0e-6 || seg.transpose != 0;
            if (! stretch)
            {
                const double from = p.srcStart + ((double) r0 / sampleRate - p.outStart - w.shift);
                if (! readRange (file, from, from + (double) (r1 - r0) / sampleRate, sampleRate, formats, audio))
                {
                    error = tr ("No se pudo leer «") + what + tr ("»");
                    return false;
                }
            }
            else
            {
                // Lo que usa la capa más contexto, empezando en una muestra exacta; el instante x del recorte cae en
                // x · ratio de lo estirado
                const double lead = w.lead > 0.0 ? w.lead : sp.pre;
                const double from = std::floor ((p.srcStart - (lead + w.shift) / p.ratio - stretchContext) * sampleRate) / sampleRate;
                juce::AudioBuffer<float> clip;
                if (! readRange (file, from, p.srcEnd + (w.tail - w.shift) / p.ratio + stretchContext, sampleRate, formats, clip))
                {
                    error = tr ("No se pudo leer «") + what + tr ("»");
                    return false;
                }
                SongInfo tmp;
                TempoRegion region;
                region.start = 0.0;
                region.origBpm = p.srcBpm;
                region.playBpm = p.playBpm;
                region.transpose = juce::jlimit (-12, 12, seg.transpose);
                tmp.tempoRegions = { region };
                const auto map = TimeMap::build (tmp, (double) clip.getNumSamples() / sampleRate);
                audio = stretcher::renderBuffer (clip, map, sampleRate, shouldAbort);
                if (audio.getNumSamples() == 0)
                    return false;   // abortado
                offset = std::llround (((p.srcStart - from) * p.ratio + (double) r0 / sampleRate - p.outStart - w.shift) * sampleRate);
            }
            aligned.setSize (2, r1 - r0);
            aligned.clear();
            const int channels = audio.getNumChannels();
            if (channels <= 0)
                return true;
            const float* inL = audio.getReadPointer (0);
            const float* inR = audio.getReadPointer (juce::jmin (1, channels - 1));
            const juce::int64 available = audio.getNumSamples();
            float* outL = aligned.getWritePointer (0);
            float* outR = aligned.getWritePointer (1);
            for (int j = 0; j < r1 - r0; ++j)
            {
                const juce::int64 k = offset + j;
                if (k >= 0 && k < available)
                {
                    outL[j] = inL[k];
                    outR[j] = inR[k];
                }
            }
            return true;
        }

        // Ganancia del tramo (la propia y, con nivelado, la que lo lleva al objetivo) y, si hizo falta, el limitador
        // por muestra desde m0 en `limiter`. `aligned` es su fuente original (ver render).
        float levelSegment (const MixProject& project, const SegmentPlan& sp, const juce::AudioBuffer<float>& aligned,
                            double sampleRate, std::vector<float>& limiter, MixLevel* level)
        {
            const auto& seg = project.segments[(size_t) sp.index];
            const float userDb = juce::jlimit (-24.0f, 12.0f, seg.gainDb);
            limiter.clear();
            if (! (project.levelLufs < 0.0))
                return juce::Decibels::decibelsToGain (userDb);
            // Su cuerpo, desde la unión: lo que se mide (sin el audio previo del fundido de entrada)
            const int length = sp.m1 - sp.m0;
            const int b0 = juce::jlimit (0, length, (int) std::ceil (sp.place.outStart * sampleRate) - sp.m0);
            auto bodyLufs = [&] (const std::vector<float>* lim, float g)
            {
                juce::AudioBuffer<float> body (2, juce::jmax (1, length - b0));
                body.clear();
                for (int ch = 0; ch < 2; ++ch)
                    for (int j = b0; j < length; ++j)
                        body.setSample (ch, j - b0, aligned.getSample (ch, j) * g * (lim != nullptr ? (*lim)[(size_t) j] : 1.0f));
                return loudness::measure (body, sampleRate, {}, false).whole.lufs;
            };
            const double measured = bodyLufs (nullptr, 1.0f);
            if (! (measured > loudness::unknown + 1.0))
                return juce::Decibels::decibelsToGain (userDb);
            // Ganancia hasta el objetivo (tope ±12 dB); el limitador corta solo los picos que pasarían del techo y,
            // como eso baja algo la sonoridad, se vuelve a medir y a corregir (hasta tres pasadas)
            const double target = project.levelLufs;
            const int lookahead = juce::jmax (1, (int) std::lround (limiterLookahead * sampleRate));
            const int release = juce::jmax (1, (int) std::lround (limiterRelease * sampleRate));
            const float ceiling = juce::Decibels::decibelsToGain ((float) limiterCeilingDb);
            double levelDb = juce::jlimit (-maxLevelDb, maxLevelDb, target - measured);
            double after = measured + levelDb, reduction = 0.0;
            for (int pass = 0;; ++pass)
            {
                // El limitador siempre es el de la ganancia final (cada corrección vuelve a pasar por aquí)
                const float total = juce::Decibels::decibelsToGain ((float) levelDb + userDb);
                reduction = loudness::limiterGains (aligned, total, ceiling, lookahead, release, limiter);
                if (reduction > -0.01)
                {
                    limiter.clear();   // no hizo falta
                    after = measured + levelDb;
                    break;
                }
                after = bodyLufs (&limiter, total) - userDb;
                const double missing = target - after;
                if (pass == 2 || std::abs (missing) < 0.15 || levelDb >= maxLevelDb - 1.0e-6)
                    break;
                levelDb = juce::jlimit (-maxLevelDb, maxLevelDb, levelDb + missing);
            }
            if (level != nullptr)
            {
                level->gainDb = levelDb;
                level->lufs = after;
                level->limiterDb = reduction;
                level->belowTarget = after < target - 0.5;
            }
            return juce::Decibels::decibelsToGain ((float) levelDb + userDb);
        }

        // Suma una capa del tramo en `out` (desde la muestra r0) con sus fundidos, su ganancia y el limitador del tramo
        // (que va de m0 a m1: fuera de ahí, 1). Una pista que entra antes sube en 10 ms al empezar y una que sigue se
        // apaga en su último tiempo; en lugar de los fundidos del tramo.
        void addSegment (juce::AudioBuffer<float>& out, const SegmentPlan& sp, const LayerWindow& w, const juce::AudioBuffer<float>& aligned,
                         int r0, float gain, const std::vector<float>& limiter, double sampleRate)
        {
            const auto& p = sp.place;
            const double fadeOutStart = p.outEnd - sp.fadeOut;
            const double leadStart = p.outStart - w.lead;
            const double tailFadeStart = p.outEnd + w.tail - w.tailFade;
            const float* inL = aligned.getReadPointer (0);
            const float* inR = aligned.getReadPointer (1);
            float* outL = out.getWritePointer (0);
            float* outR = out.getWritePointer (1);
            const int n = juce::jmin (aligned.getNumSamples(), out.getNumSamples() - r0);
            for (int j = 0; j < n; ++j)
            {
                const int m = r0 + j;
                const double t = (double) m / sampleRate;
                double g;
                if (w.lead > 0.0)
                    g = std::sin (halfPi * juce::jlimit (0.0, 1.0, (t - leadStart) / cutFadeSeconds));
                else if (! sp.crossfade)
                    g = sp.rise > 0.0 ? std::sin (halfPi * juce::jlimit (0.0, 1.0, (t - p.outStart) / sp.rise))
                                      : juce::jlimit (0.0, 1.0, (t - p.outStart) / edgeFadeSeconds);
                else if (t >= p.outStart)
                    g = 1.0;
                else
                {
                    g = std::sin (halfPi * juce::jlimit (0.0, 1.0, (t - (p.outStart - sp.pre)) / sp.pre));
                    if (sp.squareIn)
                        g *= g;
                }
                if (w.tail > 0.0)
                {
                    if (t > tailFadeStart && w.tailFade > 0.0)
                        g *= std::cos (halfPi * juce::jlimit (0.0, 1.0, (t - tailFadeStart) / w.tailFade));
                }
                else if (t > fadeOutStart)
                {
                    const double c = std::cos (halfPi * juce::jlimit (0.0, 1.0, (t - fadeOutStart) / sp.fadeOut));
                    g *= sp.squareOut ? c * c : c;
                }
                const int k = m - sp.m0;
                const float lim = k >= 0 && k < (int) limiter.size() ? limiter[(size_t) k] : 1.0f;
                const float v = gain * (float) g * lim;
                outL[m] += inL[j] * v;
                outR[m] += inR[j] * v;
            }
        }

        // La ganancia del tramo y su limitador (ver levelSegment; sin nivelado, solo su ganancia propia) y, si `sum` no es
        // nullptr, el tramo entero sumado ahí: su fuente o, si se arma con sus pistas, las que suenan con sus ventanas.
        // Lee la fuente solo si hace falta.
        bool prepareSegment (const MixProject& project, const SegmentPlan& sp, double sampleRate, juce::AudioFormatManager& formats,
                             const std::function<bool()>& shouldAbort, juce::AudioBuffer<float>* sum, float& gain,
                             std::vector<float>& limiter, MixLevel* level, juce::String& error)
        {
            const auto& seg = project.segments[(size_t) sp.index];
            const auto& src = project.sources[(size_t) seg.source];
            gain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 12.0f, seg.gainDb));
            limiter.clear();
            const bool fromStems = ! sp.stems.empty();
            const bool leveled = project.levelLufs < 0.0;
            if (leveled || (sum != nullptr && ! fromStems))
            {
                const auto file = project.sourceFile (seg.source);
                if (! file.existsAsFile())
                {
                    error = tr ("No se encuentra la fuente «") + src.name + tr ("»");
                    return false;
                }
                juce::AudioBuffer<float> aligned;
                int r0 = 0;
                if (! readLayer (project, sp, {}, file, src.name, sampleRate, formats, shouldAbort, aligned, r0, error))
                    return false;
                // Con nivelado, medido siempre en la fuente original (la suma de sus pistas): igual para todas
                if (leveled)
                    gain = levelSegment (project, sp, aligned, sampleRate, limiter, level);
                if (sum != nullptr && ! fromStems)
                    addSegment (*sum, sp, {}, aligned, r0, gain, limiter, sampleRate);
            }
            if (sum != nullptr && fromStems)
                for (auto& layer : sp.stems)
                {
                    if (layer.muted)
                        continue;
                    if (shouldAbort && shouldAbort())
                        return false;
                    juce::AudioBuffer<float> aligned;
                    int r0 = 0;
                    if (! readLayer (project, sp, layer.window, sp.stemsFolder.getChildFile (layer.file), layer.file, sampleRate, formats,
                                     shouldAbort, aligned, r0, error))
                        return false;
                    addSegment (*sum, sp, layer.window, aligned, r0, gain, limiter, sampleRate);
                }
            return true;
        }

        // Limitador de las zonas donde suenan varias capas, calculado sobre la suma (fuera de la zona no cuenta: ahí rige el
        // limitador de cada tramo): la ganancia baja la anticipación antes de cada pico y vuelve en 5 · release después
        struct LayerGains
        {
            int start = 0;
            std::vector<float> gains;
        };

        std::vector<LayerGains> layerLimiter (const RenderPlan& plan, const juce::AudioBuffer<float>& sum, double sampleRate,
                                              std::vector<MixLevel>* levels)
        {
            std::vector<LayerGains> out;
            const int lookahead = limiterLookaheadSamples (sampleRate), release = limiterReleaseSamples (sampleRate);
            const float ceiling = juce::Decibels::decibelsToGain ((float) limiterCeilingDb);
            for (auto& r : plan.layers)
            {
                const int a = juce::jmax (0, r.start - lookahead - 1);
                const int b = juce::jmin (juce::jmin (plan.totalSamples, sum.getNumSamples()), r.end + 5 * release);
                if (b <= a || r.end <= r.start || r.end > sum.getNumSamples())
                    continue;
                juce::AudioBuffer<float> slice (2, b - a);
                slice.clear();
                for (int ch = 0; ch < 2; ++ch)
                    slice.copyFrom (ch, r.start - a, sum, juce::jmin (ch, sum.getNumChannels() - 1), r.start, r.end - r.start);
                LayerGains lg;
                lg.start = a;
                const double reduction = loudness::limiterGains (slice, 1.0f, ceiling, lookahead, release, lg.gains);
                if (reduction > -0.01)
                    continue;
                if (levels != nullptr)
                    for (int o : r.owners)
                        if (juce::isPositiveAndBelow (o, (int) levels->size()))
                            (*levels)[(size_t) o].joinLimiterDb = juce::jmin ((*levels)[(size_t) o].joinLimiterDb, reduction);
                out.push_back (std::move (lg));
            }
            return out;
        }

        void applyLayerGains (juce::AudioBuffer<float>& buffer, const std::vector<LayerGains>& gains)
        {
            for (auto& lg : gains)
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                {
                    float* d = buffer.getWritePointer (ch);
                    const int n = juce::jmin ((int) lg.gains.size(), buffer.getNumSamples() - lg.start);
                    for (int j = 0; j < n; ++j)
                        d[lg.start + j] *= lg.gains[(size_t) j];
                }
        }
    }

    juce::AudioBuffer<float> render (const MixProject& project, double sampleRate, juce::AudioFormatManager& formats,
                                     const std::function<bool()>& shouldAbort,
                                     const std::function<void (float)>& progress, juce::String& error,
                                     std::vector<MixLevel>* levels)
    {
        error.clear();
        RenderPlan plan;
        const bool planned = makePlan (project, sampleRate, project.stemsKey, plan, error);
        if (levels != nullptr)
            levels->assign ((size_t) plan.count, MixLevel());
        if (! planned)
            return {};
        juce::AudioBuffer<float> result (2, plan.totalSamples);
        result.clear();
        size_t next = 0;
        for (int i = 0; i < plan.count; ++i)
        {
            if (shouldAbort && shouldAbort())
                return {};
            auto report = [&] { if (progress) progress ((float) (i + 1) / (float) plan.count); };
            if (next >= plan.segments.size() || plan.segments[next].index != i)
            {
                report();   // no suena
                continue;
            }
            const auto& sp = plan.segments[next++];
            float gain = 1.0f;
            std::vector<float> limiter;
            if (! prepareSegment (project, sp, sampleRate, formats, shouldAbort, &result, gain, limiter,
                                  levels != nullptr ? &(*levels)[(size_t) i] : nullptr, error))
                return {};
            report();
        }
        if (shouldAbort && shouldAbort())
            return {};
        applyLayerGains (result, layerLimiter (plan, result, sampleRate, levels));
        return result;
    }

    bool renderStems (const MixProject& project, const juce::String& optionsKey, const juce::StringArray& stemFiles, double sampleRate,
                      juce::AudioFormatManager& formats,
                      const std::function<bool()>& shouldAbort, const std::function<void (float)>& progress, juce::String& error,
                      const std::function<bool (const juce::String&, const juce::AudioBuffer<float>&)>& onStem,
                      std::vector<MixLevel>* levels)
    {
        error.clear();
        RenderPlan plan;
        const bool planned = makePlan (project, sampleRate, optionsKey, plan, error);
        if (levels != nullptr)
            levels->assign ((size_t) plan.count, MixLevel());
        if (! planned)
            return false;
        const int steps = juce::jmax (1, (int) plan.segments.size() * (1 + stemFiles.size()));
        int done = 0;
        auto report = [&] { ++done; if (progress) progress ((float) done / (float) steps); };

        // 1) La ganancia de cada tramo (con nivelado, medida en su fuente original, que es la suma de sus pistas): la
        //    misma ganancia y el mismo limitador van después a cada pista, así las pistas siguen sumando el mix. Si hay
        //    zonas donde suenan varios tramos, la suma de los que las tocan: su limitador va igual a todas las pistas.
        std::vector<float> gains (plan.segments.size(), 1.0f);
        std::vector<std::vector<float>> limiters (plan.segments.size());
        juce::AudioBuffer<float> sum;
        if (! plan.layers.empty())
        {
            sum.setSize (2, plan.totalSamples);
            sum.clear();
        }
        for (size_t s = 0; s < plan.segments.size(); ++s)
        {
            if (shouldAbort && shouldAbort())
                return false;
            const auto& sp = plan.segments[s];
            const bool touches = std::any_of (plan.layers.begin(), plan.layers.end(),
                                              [&sp] (const LayerRegion& r) { return r.start < sp.reach1 && r.end > sp.reach0; });
            if (! prepareSegment (project, sp, sampleRate, formats, shouldAbort, touches ? &sum : nullptr, gains[s], limiters[s],
                                  levels != nullptr ? &(*levels)[(size_t) sp.index] : nullptr, error))
                return false;
            report();
        }
        const auto layerGains = plan.layers.empty() ? std::vector<LayerGains>() : layerLimiter (plan, sum, sampleRate, levels);
        sum = juce::AudioBuffer<float>();

        // 2) Cada pista: el mix armado con esa pista de cada fuente (una fuente que no la tenga, en silencio), con sus
        //    ajustes en cada tramo
        for (auto& stem : stemFiles)
        {
            juce::AudioBuffer<float> out (2, plan.totalSamples);
            out.clear();
            for (size_t s = 0; s < plan.segments.size(); ++s)
            {
                if (shouldAbort && shouldAbort())
                    return false;
                const auto& sp = plan.segments[s];
                const auto file = project.stemsFolder (project.segments[(size_t) sp.index].source, optionsKey).getChildFile (stem);
                LayerWindow window;
                bool muted = false;
                for (auto& layer : sp.stems)
                    if (layer.file == stem)
                    {
                        window = layer.window;
                        muted = layer.muted;
                        break;
                    }
                if (file.existsAsFile() && ! muted)
                {
                    juce::AudioBuffer<float> aligned;
                    int r0 = 0;
                    if (! readLayer (project, sp, window, file, stem, sampleRate, formats, shouldAbort, aligned, r0, error))
                        return false;
                    addSegment (out, sp, window, aligned, r0, gains[s], limiters[s], sampleRate);
                }
                report();
            }
            applyLayerGains (out, layerGains);
            if (! onStem (stem, out))
            {
                if (error.isEmpty())
                    error = tr ("No se pudo guardar la pista «") + stem + tr ("»");
                return false;
            }
        }
        return true;
    }

    std::vector<MixPiece> pieces (const MixProject& project)
    {
        std::vector<MixPiece> out;
        const auto places = layout (project);
        for (size_t i = 0; i < places.size(); ++i)
        {
            const auto& p = places[i];
            if (p.outEnd <= p.outStart)
                continue;
            MixPiece piece;
            piece.id = project.segments[i].id;
            piece.outStart = p.outStart;
            piece.outEnd = p.outEnd;
            piece.srcStart = p.srcStart;
            piece.srcEnd = p.srcEnd;
            piece.ratio = p.ratio > 0.0 ? p.ratio : 1.0;
            const int source = project.segments[i].source;
            if (source >= 0 && source < (int) project.sources.size())
                piece.source = project.sources[(size_t) source].fileName;
            out.push_back (piece);
        }
        return out;
    }

    juce::String renderSignature (const MixProject& project)
    {
        // Todo lo que cambia el audio o lo que trae la canción (tiempos, acordes, marcadores); no el nombre del mix
        auto* o = new juce::DynamicObject();
        o->setProperty ("bpm", project.bpm);
        o->setProperty ("keepTempos", project.keepTempos);
        o->setProperty ("levelLufs", project.levelLufs);
        juce::Array<juce::var> segs;
        for (auto& seg : project.segments)
        {
            auto v = toVar (seg);
            if (auto* so = v.getDynamicObject(); so != nullptr && seg.source >= 0 && seg.source < (int) project.sources.size())
            {
                const auto& src = project.sources[(size_t) seg.source];
                so->setProperty ("file", src.fileName);
                so->setProperty ("name", src.name);
                so->setProperty ("length", src.length);
            }
            segs.add (v);
        }
        o->setProperty ("segments", segs);
        juce::Array<juce::var> analyses;
        for (int i : project.usedSources())
            analyses.add (Library::analysisToVar (project.sources[(size_t) i].analysis));
        o->setProperty ("analyses", analyses);
        // Con ajustes por pista, la mezcla sin separar se arma con las pistas del mix (si están): cambia al separarlas
        if (std::any_of (project.segments.begin(), project.segments.end(), [] (const MixSegment& s) { return s.hasStemSettings(); }))
        {
            o->setProperty ("stemsKey", project.stemsKey);
            juce::Array<juce::var> ready;
            for (int i : project.usedSources())
                ready.add (! project.cachedStems (i, project.stemsKey).isEmpty());
            o->setProperty ("stemsReady", ready);
        }
        return juce::String::toHexString ((juce::int64) juce::JSON::toString (juce::var (o), true).hashCode64());
    }

    bool remapTime (const std::vector<MixPiece>& from, const std::vector<MixPiece>& to, double seconds, double& out)
    {
        const MixPiece* old = nullptr;
        for (auto& p : from)
            if (p.outEnd > p.outStart && seconds >= p.outStart - 1.0e-6 && seconds < p.outEnd)
                old = &p;   // en un solape manda el que entra (el último)
        if (old == nullptr && ! from.empty() && std::abs (seconds - from.back().outEnd) < 1.0e-6)
            old = &from.back();   // justo en el final
        if (old == nullptr)
        {
            // En el silencio antes de un tramo: va con ese tramo, a la misma distancia antes de su inicio (si ese
            // silencio nuevo alcanza)
            for (size_t j = 0; j < from.size(); ++j)
            {
                const double gapStart = j == 0 ? 0.0 : from[j - 1].outEnd;
                if (seconds < gapStart - 1.0e-6 || seconds >= from[j].outStart)
                    continue;
                const double before = from[j].outStart - seconds;
                for (size_t k = 0; k < to.size(); ++k)
                    if (to[k].id == from[j].id && (to[k].source.isEmpty() || from[j].source.isEmpty() || to[k].source == from[j].source))
                    {
                        const double newGapStart = k == 0 ? 0.0 : to[k - 1].outEnd;
                        out = to[k].outStart - before;
                        return out >= newGapStart - 1.0e-6;
                    }
                return false;
            }
            return false;
        }
        const double src = old->srcStart + (seconds - old->outStart) / old->ratio;
        for (auto& p : to)
            if (p.id == old->id && (p.source.isEmpty() || old->source.isEmpty() || p.source == old->source))
            {
                if (src < p.srcStart - 0.01 || src > p.srcEnd + 0.01)
                    return false;   // ese pedazo del tramo ya no está en el mix
                out = juce::jlimit (p.outStart, p.outEnd, p.outStart + (src - p.srcStart) * p.ratio);
                return true;
            }
        return false;   // el tramo se quitó
    }

    void applyRebuild (SongInfo& song, const SongInfo& generated, const std::vector<MixPiece>& from, const std::vector<MixPiece>& to)
    {
        // Cada sección nueva conserva el tempo y el tono de reproducción que tenía la vieja de su tramo (se busca con el
        // mismo instante de la fuente, llevado hacia atrás)
        auto regions = generated.tempoRegions;
        for (auto& r : regions)
            if (double old = 0.0; remapTime (to, from, r.start + 0.01, old))
                if (const int k = song.tempoRegionAt (old); k >= 0)
                {
                    r.playBpm = song.tempoRegions[(size_t) k].playBpm;
                    r.transpose = song.tempoRegions[(size_t) k].transpose;
                }
        song.analysis = generated.analysis;
        song.tempoRegions = regions;
        song.bpm = generated.bpm;
        song.clickOffset = generated.clickOffset;
        song.clips.clear();
        song.trackClips.clear();

        // Marcadores: los del mix se rehacen; los propios van con su tramo
        std::vector<SongMarker> markers;
        for (auto& m : song.markers)
            if (double t = 0.0; ! m.fromMix && remapTime (from, to, m.seconds, t))
            {
                auto moved = m;
                moved.seconds = t;
                markers.push_back (moved);
            }
        for (auto m : generated.markers)
        {
            // Donde ya hay un marcador propio (por ejemplo, el de un tramo renombrado a mano), queda el propio
            const bool taken = std::any_of (markers.begin(), markers.end(), [&m] (const SongMarker& own)
                                            { return ! own.fromMix && std::abs (own.seconds - m.seconds) < 0.05; });
            if (taken)
                continue;
            m.fromMix = true;
            markers.push_back (m);
        }
        for (auto& m : markers)
        {
            m.gainDb = 0.0;
            m.lufs = m.truePeakDb = unmeasuredDb;
            m.stemGainsDb.clear();
            m.stemLufs.clear();
        }
        song.markers = std::move (markers);
        song.sortMarkers();

        // Notas y golpes MIDI van con su tramo (los de un pedazo que ya no está se quitan)
        std::vector<SongNote> notes;
        for (auto& n : song.notes)
            if (double t = 0.0; remapTime (from, to, n.seconds, t))
            {
                auto moved = n;
                moved.seconds = t;
                notes.push_back (moved);
            }
        song.notes = std::move (notes);
        song.sortNotes();
        for (auto& mt : song.midiTracks)
        {
            std::vector<MidiHit> hits;
            for (auto& h : mt.hits)
                if (double t = 0.0; remapTime (from, to, h.seconds, t))
                {
                    auto moved = h;
                    moved.seconds = t;
                    hits.push_back (moved);
                }
            mt.hits = std::move (hits);
            mt.sortHits();
        }

        // El audio cambió: el nivelado se vuelve a medir
        song.loudnessLufs = song.truePeakDb = unmeasuredDb;
        song.headGainDb = 0.0;
        song.headLufs = song.headTruePeakDb = unmeasuredDb;
        song.headStemGainsDb.clear();
        song.headStemLufs.clear();
        song.stemSongLufs.clear();
        song.fitStemArrays();
        song.mixLink.pieces = to;
    }
}

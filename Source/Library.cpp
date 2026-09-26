#include "Library.h"
#include "TempoMap.h"
#include <algorithm>
#include <cmath>

void SongInfo::sortMarkers()
{
    std::sort (markers.begin(), markers.end(),
               [] (const SongMarker& a, const SongMarker& b) { return a.seconds < b.seconds; });
}

void SongInfo::fitStemArrays()
{
    const size_t n = stems.size();
    auto fit = [n] (std::vector<double>& v, double fill) { v.resize (n, fill); };
    fit (headStemGainsDb, 0.0);
    fit (headStemLufs, unmeasuredDb);
    fit (stemSongLufs, unmeasuredDb);
    for (auto& m : markers)
    {
        fit (m.stemGainsDb, 0.0);
        fit (m.stemLufs, unmeasuredDb);
    }
}

void SongInfo::sortTempoRegions()
{
    std::stable_sort (tempoRegions.begin(), tempoRegions.end(),
                      [] (const TempoRegion& a, const TempoRegion& b) { return a.start < b.start; });
    if (! tempoRegions.empty() && tempoRegions.front().start > 1.0e-6)
        tempoRegions.insert (tempoRegions.begin(), TempoRegion { 0.0, tempoRegions.front().origBpm, 0.0 });
}

void SongInfo::mergeEqualTempoRegions()
{
    for (int i = (int) tempoRegions.size(); --i >= 1;)
    {
        const auto& a = tempoRegions[(size_t) i - 1];
        const auto& b = tempoRegions[(size_t) i];
        if (std::abs (a.origBpm - b.origBpm) < 0.05 && std::abs (a.playBpm - b.playBpm) < 0.05)
            tempoRegions.erase (tempoRegions.begin() + i);
    }
}

int SongInfo::tempoRegionAt (double seconds) const
{
    int found = -1;
    for (int i = 0; i < (int) tempoRegions.size(); ++i)
        if (tempoRegions[(size_t) i].start <= seconds + 1.0e-9)
            found = i;
    return found;
}

double Analysis::bpmBetween (double from, double to) const
{
    std::vector<double> intervals;
    for (size_t i = 1; i < beats.size(); ++i)
        if (beats[i].seconds > from && beats[i].seconds <= to && beats[i].seconds > beats[i - 1].seconds)
            intervals.push_back (beats[i].seconds - beats[i - 1].seconds);
    if (intervals.empty())
        return 0.0;
    std::sort (intervals.begin(), intervals.end());
    return 60.0 / intervals[intervals.size() / 2];   // mediana: robusta a un tiempo mal detectado
}

int Analysis::chordAt (double seconds) const
{
    for (int i = 0; i < (int) chords.size(); ++i)
        if (seconds >= chords[(size_t) i].start && seconds < chords[(size_t) i].end)
            return i;
    return -1;
}

int Analysis::downbeatAfter (double seconds) const
{
    for (int i = 0; i < (int) beats.size(); ++i)
        if (beats[(size_t) i].beatInBar == 1 && beats[(size_t) i].seconds >= seconds)
            return i;
    return -1;
}

int Analysis::nearestBeat (double seconds, double maxDistance) const
{
    int best = -1;
    for (int i = 0; i < (int) beats.size(); ++i)
    {
        const double d = std::abs (beats[(size_t) i].seconds - seconds);
        if (d <= maxDistance && (best < 0 || d < std::abs (beats[(size_t) best].seconds - seconds)))
            best = i;
    }
    return best;
}

void Analysis::renumberBeats (int from, int meterIn, int toExclusive, int anchor)
{
    const int m = juce::jlimit (1, 7, meterIn);
    const int to = juce::jlimit (0, (int) beats.size(), toExclusive);
    if (anchor < 0)
        anchor = from;
    for (int i = juce::jmax (0, from); i < to; ++i)
        beats[(size_t) i].beatInBar = (((i - anchor) % m) + m) % m + 1;
    meter = 1;
    for (auto& b : beats)
        meter = juce::jmax (meter, b.beatInBar);
}

int Analysis::firstBeatAtOrAfter (double seconds) const
{
    for (int i = 0; i < (int) beats.size(); ++i)
        if (beats[(size_t) i].seconds >= seconds - 1.0e-6)
            return i;
    return (int) beats.size();
}

int Analysis::firstDownbeatIn (int from, int toExclusive) const
{
    for (int i = juce::jmax (0, from); i < juce::jmin (toExclusive, (int) beats.size()); ++i)
        if (beats[(size_t) i].beatInBar == 1)
            return i;
    return from;
}

void Analysis::halveBeats (int from, int toExclusive, int meterIn)
{
    const int to = juce::jlimit (0, (int) beats.size(), toExclusive);
    from = juce::jlimit (0, to, from);
    if (to - from < 2)
        return;
    // Se conservan los tiempos con la paridad del primer tiempo de compás del tramo
    const int anchor = firstDownbeatIn (from, to);
    std::vector<Beat> kept;
    for (int i = from; i < to; ++i)
        if (((i - anchor) % 2 + 2) % 2 == 0)
            kept.push_back (beats[(size_t) i]);
    const int newAnchor = from + (anchor - from) / 2;   // el ancla entre los conservados
    beats.erase (beats.begin() + from, beats.begin() + to);
    beats.insert (beats.begin() + from, kept.begin(), kept.end());
    renumberBeats (from, meterIn, from + (int) kept.size(), newAnchor);
}

void Analysis::doubleBeats (int from, int toExclusive, int meterIn)
{
    const int to = juce::jlimit (0, (int) beats.size(), toExclusive);
    from = juce::jlimit (0, to, from);
    if (to - from < 2)
        return;
    const int anchor = firstDownbeatIn (from, to);
    std::vector<Beat> out;
    for (int i = from; i < to; ++i)
    {
        out.push_back (beats[(size_t) i]);
        if (i + 1 < to)
            out.push_back ({ 0.5 * (beats[(size_t) i].seconds + beats[(size_t) i + 1].seconds), 1 });
    }
    beats.erase (beats.begin() + from, beats.begin() + to);
    beats.insert (beats.begin() + from, out.begin(), out.end());
    renumberBeats (from, meterIn, from + (int) out.size(), from + 2 * (anchor - from));
}

double Analysis::evenGrid (double fromSeconds, double toSeconds, int count, int meterIn, bool absorbRemainder)
{
    if (count < 1 || toSeconds <= fromSeconds)
        return 0.0;
    const double interval = (toSeconds - fromSeconds) / count;
    replaceBeatsWithGrid (fromSeconds, toSeconds, 60.0 / interval, meterIn);
    const int m = juce::jlimit (1, 7, meterIn);
    const int from = firstBeatAtOrAfter (fromSeconds);
    const int remainder = count % m;
    if (absorbRemainder && remainder > 0 && remainder * 2 < m && count > m && m + remainder <= 7)
        for (int k = 0; k < remainder; ++k)
            beats[(size_t) (from + count - remainder + k)].beatInBar = m + 1 + k;
    meter = 1;
    for (auto& b : beats)
        meter = juce::jmax (meter, b.beatInBar);
    return 60.0 / interval;
}

void Analysis::shortBar (int index, int beatsInBar, int meterAfter, int toExclusive)
{
    const int n = juce::jlimit (1, 7, beatsInBar);
    const int to = juce::jlimit (0, (int) beats.size(), toExclusive);
    if (index < 0 || index >= to)
        return;
    renumberBeats (index, n, juce::jmin (to, index + n));
    if (index + n < to)
        renumberBeats (index + n, meterAfter, to);
}

void Analysis::replaceBeatsWithGrid (double fromSeconds, double toSeconds, double gridBpm, int meterIn)
{
    if (gridBpm <= 0.0 || toSeconds <= fromSeconds)
        return;
    const int from = firstBeatAtOrAfter (fromSeconds);
    const int to = firstBeatAtOrAfter (toSeconds);
    std::vector<Beat> grid;
    const double step = 60.0 / gridBpm;
    for (double t = fromSeconds; t < toSeconds - 1.0e-6; t += step)
        grid.push_back ({ t, 1 });
    beats.erase (beats.begin() + from, beats.begin() + to);
    beats.insert (beats.begin() + from, grid.begin(), grid.end());
    renumberBeats (from, meterIn, from + (int) grid.size());
}

static juce::String displayNameFor (const juce::String& base)
{
    const auto b = base.toLowerCase();
    if (b == "vocals") return tr ("Voces");
    if (b == "drums")  return tr ("Batería");
    if (b == "bass")   return "Bajo";
    if (b == "other")  return "Otros";
    if (b == "guitar") return "Guitarra";
    if (b == "piano")  return "Piano";
    return base;
}

juce::File Library::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Secuencias");
}

Library::Library (const juce::File& rootFolder) : root (rootFolder)
{
    root.createDirectory();
}

// JUCE decodifica AAC/m4a con CoreAudio (macOS) y Media Foundation (Windows); en Linux no hay decodificador.
static const char* const audioExtensions =
   #if JUCE_MAC || JUCE_WINDOWS
    "wav;aif;aiff;flac;mp3;ogg;m4a";
   #else
    "wav;aif;aiff;flac;mp3;ogg";
   #endif

bool Library::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension (audioExtensions);
}

juce::String Library::audioFilePatterns()
{
    juce::StringArray exts;
    exts.addTokens (audioExtensions, ";", "");
    for (auto& e : exts)
        e = "*." + e;
    return exts.joinIntoString (";");
}

juce::String Library::audioFormatsDescription()
{
    return juce::String (audioExtensions).replace (";", ", ");
}

juce::Array<juce::File> Library::audioFilesIn (const juce::File& folder)
{
    juce::Array<juce::File> result;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false))
        if (isAudioFile (f))
            result.add (f);
    result.sort();
    return result;
}

SongInfo Library::readSong (const juce::File& folder) const
{
    SongInfo s;
    s.folder = folder;
    s.name = folder.getFileName();

    // Los valores se acotan: un song.json editado a mano o corrupto no debe poder
    // sacar al motor de rango (por ejemplo un par de salida negativo).
    const auto json = juce::JSON::parse (folder.getChildFile ("song.json"));
    if (json.isObject())
    {
        s.name            = json.getProperty ("name", s.name).toString();
        s.bpm             = juce::jlimit (20.0, 400.0, (double) json.getProperty ("bpm", 120.0));
        s.clickOffset     = juce::jmax (0.0, (double) json.getProperty ("clickOffset", 0.0));
        s.clickEnabled    = (bool) json.getProperty ("clickEnabled", false);
        s.clickGainDb     = juce::jlimit (-60.0f, 12.0f, (float) (double) json.getProperty ("clickGainDb", -6.0));
        s.clickOutputPair = juce::jmax (0, (int) json.getProperty ("clickOutputPair", 0));
        s.masterGainDb    = juce::jlimit (-60.0f, 12.0f, (float) (double) json.getProperty ("masterGainDb", 0.0));
        s.loudnessLufs    = (double) json.getProperty ("loudnessLufs", unmeasuredDb);
        s.truePeakDb      = (double) json.getProperty ("truePeakDb", unmeasuredDb);
        s.songGainDb      = juce::jlimit (-40.0, 40.0, (double) json.getProperty ("songGainDb", 0.0));
        s.levelingEnabled = (bool) json.getProperty ("levelingEnabled", false);
        s.headGainDb      = juce::jlimit (-40.0, 40.0, (double) json.getProperty ("headGainDb", 0.0));
        s.headLufs        = (double) json.getProperty ("headLufs", unmeasuredDb);
        s.headTruePeakDb  = (double) json.getProperty ("headTruePeakDb", unmeasuredDb);
        s.playBpm         = juce::jlimit (0.0, 400.0, (double) json.getProperty ("playBpm", 0.0));
        s.transpose       = juce::jlimit (-12, 12, (int) json.getProperty ("transpose", 0));
        s.spelling        = juce::jlimit (0, 2, (int) json.getProperty ("spelling", 0));
        s.keyOverride     = json.getProperty ("keyOverride", "").toString();

        const auto analysis = json.getProperty ("analysis", juce::var());
        if (analysis.isObject())
        {
            auto& a = s.analysis;
            a.bpm   = juce::jmax (0.0, (double) analysis.getProperty ("bpm", 0.0));
            a.meter = juce::jlimit (2, 7, (int) analysis.getProperty ("meter", 4));
            a.key   = analysis.getProperty ("key", "").toString();
            if (auto* arr = analysis.getProperty ("beats", juce::var()).getArray())
                for (auto& b : *arr)
                    if (auto* pair = b.getArray(); pair != nullptr && pair->size() >= 2)
                        a.beats.push_back ({ juce::jmax (0.0, (double) (*pair)[0]), juce::jlimit (1, 7, (int) (*pair)[1]) });
            if (auto* arr = analysis.getProperty ("chords", juce::var()).getArray())
                for (auto& c : *arr)
                    if (auto* t = c.getArray(); t != nullptr && t->size() >= 3)
                        a.chords.push_back ({ juce::jmax (0.0, (double) (*t)[0]), juce::jmax (0.0, (double) (*t)[1]), (*t)[2].toString() });
            std::sort (a.beats.begin(), a.beats.end(), [] (const Beat& x, const Beat& y) { return x.seconds < y.seconds; });
            std::sort (a.chords.begin(), a.chords.end(), [] (const Chord& x, const Chord& y) { return x.start < y.start; });
        }

        const auto markers = json.getProperty ("markers", juce::var());
        if (auto* arr = markers.getArray())
            for (auto& m : *arr)
            {
                SongMarker mk;
                mk.name       = m.getProperty ("name", "").toString();
                mk.seconds    = juce::jmax (0.0, (double) m.getProperty ("seconds", 0.0));
                mk.gainDb     = juce::jlimit (-40.0, 40.0, (double) m.getProperty ("gainDb", 0.0));
                mk.lufs       = (double) m.getProperty ("lufs", unmeasuredDb);
                mk.truePeakDb = (double) m.getProperty ("truePeakDb", unmeasuredDb);
                if (auto* g = m.getProperty ("stemGainsDb", juce::var()).getArray())
                    for (auto& v : *g) mk.stemGainsDb.push_back (juce::jlimit (-40.0, 40.0, (double) v));
                if (auto* l = m.getProperty ("stemLufs", juce::var()).getArray())
                    for (auto& v : *l) mk.stemLufs.push_back ((double) v);
                s.markers.push_back (mk);
            }
        if (auto* g = json.getProperty ("headStemGainsDb", juce::var()).getArray())
            for (auto& v : *g) s.headStemGainsDb.push_back (juce::jlimit (-40.0, 40.0, (double) v));
        if (auto* l = json.getProperty ("headStemLufs", juce::var()).getArray())
            for (auto& v : *l) s.headStemLufs.push_back ((double) v);
        if (auto* l = json.getProperty ("stemSongLufs", juce::var()).getArray())
            for (auto& v : *l) s.stemSongLufs.push_back ((double) v);

        const auto regions = json.getProperty ("tempoRegions", juce::var());
        if (auto* arr = regions.getArray())
            for (auto& r : *arr)
            {
                TempoRegion reg;
                reg.start   = juce::jmax (0.0, (double) r.getProperty ("start", 0.0));
                reg.origBpm = juce::jlimit (20.0, 400.0, (double) r.getProperty ("bpm", s.bpm));
                reg.playBpm = juce::jlimit (0.0, 400.0, (double) r.getProperty ("playBpm", 0.0));
                s.tempoRegions.push_back (reg);
            }
        s.sortTempoRegions();   // ordena y garantiza una sección que empieza en 0

        const auto clips = json.getProperty ("clips", juce::var());
        if (auto* arr = clips.getArray())
            for (auto& c : *arr)
            {
                Clip clip;
                clip.srcStart = juce::jmax (0.0, (double) c.getProperty ("start", 0.0));
                clip.srcEnd   = juce::jmax (clip.srcStart, (double) c.getProperty ("end", 0.0));
                clip.position = juce::jmax (0.0, (double) c.getProperty ("at", 0.0));
                if (clip.srcEnd > clip.srcStart + 1.0e-6)
                    s.clips.push_back (clip);
            }
        std::stable_sort (s.clips.begin(), s.clips.end(), [] (const Clip& a, const Clip& b) { return a.position < b.position; });
        // Canciones analizadas antes de existir las secciones de tempo: se detectan de los tiempos guardados
        if (s.tempoRegions.empty() && s.analysis.beats.size() >= 2)
            s.tempoRegions = detectTempoRegions (s.analysis, s.bpm);

        const auto stems = json.getProperty ("stems", juce::var());
        if (auto* arr = stems.getArray())
            for (auto& st : *arr)
            {
                StemInfo si;
                si.fileName   = st.getProperty ("file", "").toString();
                si.name       = st.getProperty ("name", si.fileName).toString();
                si.gainDb     = juce::jlimit (-60.0f, 12.0f, (float) (double) st.getProperty ("gainDb", 0.0));
                si.muted      = (bool) st.getProperty ("muted", false);
                si.outputPair = juce::jmax (0, (int) st.getProperty ("outputPair", 0));
                if (si.fileName.isNotEmpty() && isAudioFile (folder.getChildFile (si.fileName)))
                    s.stems.push_back (si);
            }
    }

    // Archivos de audio nuevos que no estén en song.json
    for (auto& f : audioFilesIn (folder))
    {
        const bool known = std::any_of (s.stems.begin(), s.stems.end(),
                                        [&] (const StemInfo& si) { return si.fileName == f.getFileName(); });
        if (! known)
        {
            StemInfo si;
            si.fileName = f.getFileName();
            si.name = displayNameFor (f.getFileNameWithoutExtension());
            s.stems.push_back (si);
        }
    }

    s.sortMarkers();
    s.fitStemArrays();
    return s;
}

bool Library::saveSong (const SongInfo& s) const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", s.name);
    obj->setProperty ("bpm", s.bpm);
    obj->setProperty ("clickOffset", s.clickOffset);
    obj->setProperty ("clickEnabled", s.clickEnabled);
    obj->setProperty ("clickGainDb", (double) s.clickGainDb);
    obj->setProperty ("clickOutputPair", s.clickOutputPair);
    obj->setProperty ("masterGainDb", (double) s.masterGainDb);
    obj->setProperty ("loudnessLufs", s.loudnessLufs);
    obj->setProperty ("truePeakDb", s.truePeakDb);
    obj->setProperty ("songGainDb", s.songGainDb);
    obj->setProperty ("levelingEnabled", s.levelingEnabled);
    obj->setProperty ("headGainDb", s.headGainDb);
    obj->setProperty ("headLufs", s.headLufs);
    obj->setProperty ("headTruePeakDb", s.headTruePeakDb);
    {
        juce::Array<juce::var> hg, hl, sl;
        for (auto v : s.headStemGainsDb) hg.add (v);
        for (auto v : s.headStemLufs) hl.add (v);
        for (auto v : s.stemSongLufs) sl.add (v);
        obj->setProperty ("headStemGainsDb", hg);
        obj->setProperty ("headStemLufs", hl);
        obj->setProperty ("stemSongLufs", sl);
    }
    obj->setProperty ("playBpm", s.playBpm);
    obj->setProperty ("transpose", s.transpose);
    obj->setProperty ("spelling", s.spelling);
    obj->setProperty ("keyOverride", s.keyOverride);

    if (! s.analysis.isEmpty())
    {
        auto* an = new juce::DynamicObject();
        an->setProperty ("bpm", s.analysis.bpm);
        an->setProperty ("meter", s.analysis.meter);
        an->setProperty ("key", s.analysis.key);
        juce::Array<juce::var> beats, chords;
        for (auto& b : s.analysis.beats)
            beats.add (juce::Array<juce::var> { b.seconds, b.beatInBar });
        for (auto& c : s.analysis.chords)
            chords.add (juce::Array<juce::var> { c.start, c.end, c.name });
        an->setProperty ("beats", beats);
        an->setProperty ("chords", chords);
        obj->setProperty ("analysis", juce::var (an));
    }

    juce::Array<juce::var> markers;
    for (auto& m : s.markers)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", m.name);
        mo->setProperty ("seconds", m.seconds);
        mo->setProperty ("gainDb", m.gainDb);
        mo->setProperty ("lufs", m.lufs);
        mo->setProperty ("truePeakDb", m.truePeakDb);
        juce::Array<juce::var> sg, sl;
        for (auto v : m.stemGainsDb) sg.add (v);
        for (auto v : m.stemLufs) sl.add (v);
        mo->setProperty ("stemGainsDb", sg);
        mo->setProperty ("stemLufs", sl);
        markers.add (juce::var (mo));
    }
    obj->setProperty ("markers", markers);

    if (! s.clips.empty())
    {
        juce::Array<juce::var> clips;
        for (auto& c : s.clips)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("start", c.srcStart);
            co->setProperty ("end", c.srcEnd);
            co->setProperty ("at", c.position);
            clips.add (juce::var (co));
        }
        obj->setProperty ("clips", clips);
    }

    if (! s.tempoRegions.empty())
    {
        juce::Array<juce::var> regions;
        for (auto& r : s.tempoRegions)
        {
            auto* ro = new juce::DynamicObject();
            ro->setProperty ("start", r.start);
            ro->setProperty ("bpm", r.origBpm);
            ro->setProperty ("playBpm", r.playBpm);
            regions.add (juce::var (ro));
        }
        obj->setProperty ("tempoRegions", regions);
    }

    juce::Array<juce::var> stems;
    for (auto& st : s.stems)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("name", st.name);
        so->setProperty ("file", st.fileName);
        so->setProperty ("gainDb", (double) st.gainDb);
        so->setProperty ("muted", st.muted);
        so->setProperty ("outputPair", st.outputPair);
        stems.add (juce::var (so));
    }
    obj->setProperty ("stems", stems);

    return s.folder.getChildFile ("song.json").replaceWithText (juce::JSON::toString (juce::var (obj)));
}

void Library::saveSetlist() const
{
    juce::Array<juce::var> order;
    for (auto& s : songs)
        order.add (s.folder.getFileName());

    const auto file = root.getChildFile ("setlist.json");
    const auto text = juce::JSON::toString (juce::var (order));
    if (! file.existsAsFile() || file.loadFileAsString() != text)   // no reescribir si no cambió
        file.replaceWithText (text);
}

void Library::load()
{
    songs.clear();
    juce::StringArray added;

    // Solo cuentan las carpetas con audio: una carpeta con song.json pero sin stems no es una canción
    const auto order = juce::JSON::parse (root.getChildFile ("setlist.json"));
    if (auto* arr = order.getArray())
        for (auto& v : *arr)
        {
            auto folder = root.getChildFile (v.toString());
            if (folder.isDirectory() && ! added.contains (folder.getFileName()) && ! audioFilesIn (folder).isEmpty())
            {
                songs.push_back (readSong (folder));
                added.add (folder.getFileName());
            }
        }

    auto dirs = root.findChildFiles (juce::File::findDirectories, false);
    dirs.sort();
    for (auto& d : dirs)
        if (! added.contains (d.getFileName()) && ! audioFilesIn (d).isEmpty())
            songs.push_back (readSong (d));

    saveSetlist();
}

int Library::importStemFiles (const juce::Array<juce::File>& files, const juce::String& songName, bool moveFiles)
{
    auto name = songName.trim().isEmpty() ? tr ("Nueva canción") : songName.trim();
    auto folder = root.getNonexistentChildFile (juce::File::createLegalFileName (name), "", false);
    if (! folder.createDirectory())
        return -1;

    int copied = 0;
    for (auto& f : files)
    {
        if (! isAudioFile (f))
            continue;
        const auto dest = folder.getChildFile (f.getFileName());
        // moveFileTo hace rename (instantáneo en el mismo sistema de archivos) y si no puede, copia y borra
        if (moveFiles ? f.moveFileTo (dest) : f.copyFileTo (dest))
            ++copied;
    }

    if (copied == 0)
    {
        folder.deleteRecursively();
        return -1;
    }

    auto song = readSong (folder);
    song.name = name;
    saveSong (song);
    songs.push_back (song);
    saveSetlist();
    return (int) songs.size() - 1;
}

int Library::importStemFolder (const juce::File& folder, const juce::String& songName, bool moveFiles)
{
    return importStemFiles (audioFilesIn (folder), songName, moveFiles);
}

bool Library::isProjectFolder (const juce::File& folder)
{
    return folder.isDirectory() && folder.getChildFile ("song.json").existsAsFile() && ! audioFilesIn (folder).isEmpty();
}

int Library::importProject (const juce::File& folder)
{
    if (! isProjectFolder (folder))
        return -1;
    const auto dest = root.getNonexistentChildFile (juce::File::createLegalFileName (folder.getFileName()), "", false);
    if (! folder.copyDirectoryTo (dest))
    {
        dest.deleteRecursively();
        return -1;
    }
    auto song = readSong (dest);
    saveSong (song);
    songs.push_back (song);
    saveSetlist();
    return (int) songs.size() - 1;
}

bool Library::replaceStems (int index, const juce::File& resultFolder)
{
    if (! juce::isPositiveAndBelow (index, (int) songs.size()) || audioFilesIn (resultFolder).isEmpty())
        return false;
    const auto folder = songs[(size_t) index].folder;
    const auto originals = folder.getChildFile ("original");
    if (! originals.createDirectory())
        return false;
    for (auto& f : audioFilesIn (folder))
        if (! f.moveFileTo (originals.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false)))
            return false;
    for (auto& f : audioFilesIn (resultFolder))
        if (! f.moveFileTo (folder.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false)))
            return false;
    auto song = readSong (folder);   // conserva nombre, marcadores, análisis, tempo, cortes; los stems nuevos entran traducidos
    song.headStemGainsDb.clear();
    song.headStemLufs.clear();
    song.stemSongLufs.clear();
    song.headLufs = unmeasuredDb;    // el nivelado se vuelve a medir con las pistas nuevas
    for (auto& m : song.markers)
    {
        m.stemGainsDb.clear();
        m.stemLufs.clear();
        m.lufs = unmeasuredDb;
    }
    song.fitStemArrays();
    songs[(size_t) index] = song;
    saveSong (song);
    return true;
}

bool Library::exportSong (int index, const juce::File& destination) const
{
    if (! juce::isPositiveAndBelow (index, (int) songs.size()) || ! destination.isDirectory())
        return false;
    const auto& folder = songs[(size_t) index].folder;
    const auto dest = destination.getNonexistentChildFile (folder.getFileName(), "", false);
    return folder.copyDirectoryTo (dest);
}

bool Library::exportAll (const juce::File& destination) const
{
    if (! destination.isDirectory())
        return false;
    bool ok = true;
    for (int i = 0; i < (int) songs.size(); ++i)
        ok = exportSong (i, destination) && ok;
    const auto setlistFile = root.getChildFile ("setlist.json");
    if (setlistFile.existsAsFile())
        ok = setlistFile.copyFileTo (destination.getChildFile ("setlist.json")) && ok;
    return ok;
}

static bool sendToTrash (const juce::File& folder)
{
   #if JUCE_LINUX
    // "gio trash" usa la papelera estándar de Ubuntu/GNOME (se puede restaurar desde Archivos)
    juce::ChildProcess gio;
    if (gio.start (juce::StringArray { "gio", "trash", folder.getFullPathName() })
        && gio.waitForProcessToFinish (15000) && gio.getExitCode() == 0 && ! folder.exists())
        return true;
   #endif
    return folder.moveToTrash();
}

bool Library::moveToTrash (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) songs.size()))
        return false;
    if (! sendToTrash (songs[(size_t) index].folder))
        return false;
    songs.erase (songs.begin() + index);
    saveSetlist();
    return true;
}

void Library::move (int index, int delta)
{
    const int other = index + delta;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())
        || ! juce::isPositiveAndBelow (other, (int) songs.size()))
        return;
    std::swap (songs[(size_t) index], songs[(size_t) other]);
    saveSetlist();
}

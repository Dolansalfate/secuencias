#include "Library.h"
#include <algorithm>

void SongInfo::sortMarkers()
{
    std::sort (markers.begin(), markers.end(),
               [] (const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
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

bool Library::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;ogg");
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

    const auto json = juce::JSON::parse (folder.getChildFile ("song.json"));
    if (json.isObject())
    {
        s.name            = json.getProperty ("name", s.name).toString();
        s.bpm             = (double) json.getProperty ("bpm", 120.0);
        s.clickOffset     = (double) json.getProperty ("clickOffset", 0.0);
        s.clickEnabled    = (bool) json.getProperty ("clickEnabled", false);
        s.clickGainDb     = (float) (double) json.getProperty ("clickGainDb", -6.0);
        s.clickOutputPair = (int) json.getProperty ("clickOutputPair", 0);

        const auto markers = json.getProperty ("markers", juce::var());
        if (auto* arr = markers.getArray())
            for (auto& m : *arr)
                s.markers.push_back ({ m.getProperty ("name", "").toString(),
                                       (double) m.getProperty ("seconds", 0.0) });

        const auto stems = json.getProperty ("stems", juce::var());
        if (auto* arr = stems.getArray())
            for (auto& st : *arr)
            {
                StemInfo si;
                si.fileName   = st.getProperty ("file", "").toString();
                si.name       = st.getProperty ("name", si.fileName).toString();
                si.gainDb     = (float) (double) st.getProperty ("gainDb", 0.0);
                si.muted      = (bool) st.getProperty ("muted", false);
                si.outputPair = (int) st.getProperty ("outputPair", 0);
                if (si.fileName.isNotEmpty() && folder.getChildFile (si.fileName).existsAsFile())
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

    juce::Array<juce::var> markers;
    for (auto& m : s.markers)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", m.name);
        mo->setProperty ("seconds", m.seconds);
        markers.add (juce::var (mo));
    }
    obj->setProperty ("markers", markers);

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
    root.getChildFile ("setlist.json").replaceWithText (juce::JSON::toString (juce::var (order)));
}

void Library::load()
{
    songs.clear();
    juce::StringArray added;

    const auto order = juce::JSON::parse (root.getChildFile ("setlist.json"));
    if (auto* arr = order.getArray())
        for (auto& v : *arr)
        {
            auto folder = root.getChildFile (v.toString());
            if (folder.isDirectory() && ! added.contains (folder.getFileName()))
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

int Library::importStemFiles (const juce::Array<juce::File>& files, const juce::String& songName)
{
    auto name = songName.trim().isEmpty() ? tr ("Nueva canción") : songName.trim();
    auto folder = root.getNonexistentChildFile (juce::File::createLegalFileName (name), "", false);
    if (! folder.createDirectory())
        return -1;

    int copied = 0;
    for (auto& f : files)
        if (isAudioFile (f) && f.copyFileTo (folder.getChildFile (f.getFileName())))
            ++copied;

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

int Library::importStemFolder (const juce::File& folder, const juce::String& songName)
{
    return importStemFiles (audioFilesIn (folder), songName);
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

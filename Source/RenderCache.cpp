#include "RenderCache.h"
#include "Triggers.h"
#include <algorithm>

namespace rendercache
{
    namespace
    {
        juce::File entryFolder (const juce::File& root, const juce::String& key)
        {
            return root.getChildFile (juce::String::toHexString (key.hashCode64()));
        }
    }

    juce::File defaultFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/Secuencias/render");
       #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Secuencias").getChildFile ("render");
       #else
        const char* xdg = std::getenv ("XDG_CACHE_HOME");
        const juce::File base = xdg != nullptr && juce::File::isAbsolutePath (xdg) ? juce::File (xdg)
                                    : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".cache");
        return base.getChildFile ("Secuencias").getChildFile ("render");
       #endif
    }

    std::shared_ptr<LoadedSong> load (const juce::File& root, const juce::String& key, const SongInfo& info, double sampleRate,
                                      juce::AudioFormatManager& formats, const std::function<bool()>& shouldAbort)
    {
        const auto dir = entryFolder (root, key);
        const auto meta = juce::JSON::parse (dir.getChildFile ("meta.json").loadFileAsString());
        if (! meta.isObject() || meta.getProperty ("key", "").toString() != key
            || std::abs ((double) meta.getProperty ("sampleRate", 0.0) - sampleRate) > 0.5)
            return nullptr;
        const auto length = (juce::int64) (double) meta.getProperty ("length", 0.0);
        auto* tracks = meta.getProperty ("tracks", juce::var()).getArray();
        if (length <= 0 || tracks == nullptr || tracks->isEmpty())
            return nullptr;
        auto song = std::make_shared<LoadedSong>();
        song->sampleRate = sampleRate;
        song->length = length;
        for (int i = 0; i < tracks->size(); ++i)
        {
            if (shouldAbort && shouldAbort())
                return nullptr;
            const auto& tv = tracks->getReference (i);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (dir.getChildFile ("pista-" + juce::String (i) + ".wav")));
            if (reader == nullptr || reader->lengthInSamples < length)
                return nullptr;
            auto t = std::make_unique<LoadedTrack>();
            t->name = tv.getProperty ("name", "").toString();
            t->stemIndex = (int) tv.getProperty ("stem", -1);
            t->songTime = (bool) tv.getProperty ("songTime", false);
            t->buffer.setSize (2, (int) length);
            t->buffer.clear();
            reader->read (&t->buffer, 0, (int) length, 0, true, true);
            if (reader->numChannels == 1)
                t->buffer.copyFrom (1, 0, t->buffer, 0, 0, (int) length);
            if (juce::isPositiveAndBelow (t->stemIndex, (int) info.stems.size()))
            {
                AudioEngine::applyStemMix (*t, info.stems[(size_t) t->stemIndex]);
                t->name = info.stems[(size_t) t->stemIndex].name;
            }
            t->smoothedGain = t->gain.load();
            t->waveform = WaveformCache::build (t->buffer);
            song->tracks.push_back (std::move (t));
        }
        dir.setLastModificationTime (juce::Time::getCurrentTime());   // recién usada: la última en borrarse
        return song;
    }

    bool store (const juce::File& root, const juce::String& key, const LoadedSong& song)
    {
        const auto dir = entryFolder (root, key);
        dir.deleteRecursively();
        if (! dir.createDirectory().wasOk())
            return false;
        juce::Array<juce::var> tracks;
        for (size_t i = 0; i < song.tracks.size(); ++i)
        {
            const auto& t = *song.tracks[i];
            if (! triggers::writeWav (t.buffer, song.sampleRate, dir.getChildFile ("pista-" + juce::String ((int) i) + ".wav"), 24))
            {
                dir.deleteRecursively();
                return false;
            }
            auto* to = new juce::DynamicObject();
            to->setProperty ("name", t.name);
            to->setProperty ("stem", t.stemIndex);
            to->setProperty ("songTime", t.songTime);
            tracks.add (juce::var (to));
        }
        auto* meta = new juce::DynamicObject();
        meta->setProperty ("key", key);
        meta->setProperty ("sampleRate", song.sampleRate);
        meta->setProperty ("length", (double) song.length);
        meta->setProperty ("tracks", tracks);
        return dir.getChildFile ("meta.json").replaceWithText (juce::JSON::toString (juce::var (meta)));
    }

    void prune (const juce::File& root, juce::int64 maxBytes)
    {
        struct Entry
        {
            juce::File dir;
            juce::Time used;
            juce::int64 bytes = 0;
        };
        std::vector<Entry> entries;
        juce::int64 total = 0;
        for (const auto& d : root.findChildFiles (juce::File::findDirectories, false))
        {
            Entry e { d, d.getLastModificationTime(), 0 };
            for (const auto& f : d.findChildFiles (juce::File::findFiles, false))
                e.bytes += f.getSize();
            total += e.bytes;
            entries.push_back (e);
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.used < b.used; });
        for (auto& e : entries)
        {
            if (total <= maxBytes)
                break;
            e.dir.deleteRecursively();
            total -= e.bytes;
        }
    }
}

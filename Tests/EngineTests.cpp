// Tests sin interfaz ni dispositivo de audio: se llama al callback del motor a mano.
// Ejecutar:  ctest --preset debug   (o ./build/debug/SecuenciasTests_artefacts/Debug/SecuenciasTests)

#include "AudioEngine.h"
#include "Library.h"
#include "Separator.h"
#include "Analyzer.h"
#include "Loudness.h"
#include "TempoMap.h"
#include "Music.h"
#include "Arrangement.h"
#include "Triggers.h"
#include "Recorder.h"
#include "Stretcher.h"
#include "MixProject.h"
#include <cmath>
#include <iostream>

static StemInfo stem (const juce::String& name, const juce::String& file)
{
    StemInfo si;
    si.name = name;
    si.fileName = file;
    return si;
}

static int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (! (cond)) { ++failures; std::cerr << "  FALLO " << __FILE__ << ":"         \
                                              << __LINE__ << "  " #cond "\n"; }       \
    } while (false)

namespace
{
    constexpr double sr = 44100.0;
    constexpr int block = 512;

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) b.getNumChannels(), 24, {}, 0));
        os.release();
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    // Senoide de 0.05 rad/muestra: cambia como máximo amp*0.05 por muestra (0.025 con amp 0.5)
    juce::AudioBuffer<float> sine (double rate, double seconds, int channels, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (channels, (int) (rate * seconds));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amp * std::sin ((float) i * 0.05f));
        return b;
    }

    void writeSine (const juce::File& f, double rate, double seconds, int channels, float amp = 0.5f)
    {
        writeWav (f, sine (rate, seconds, channels, amp), rate);
    }

    struct FakeOutputs
    {
        explicit FakeOutputs (int n) : data ((size_t) n, std::vector<float> (block)), ptrs ((size_t) n)
        {
            for (size_t i = 0; i < data.size(); ++i)
                ptrs[i] = data[i].data();
        }
        float peak (int ch) const
        {
            float p = 0;
            for (float v : data[(size_t) ch]) p = std::max (p, std::abs (v));
            return p;
        }
        std::vector<std::vector<float>> data;
        std::vector<float*> ptrs;
    };

    void render (AudioEngine& e, FakeOutputs& o, int blocks, float* peaks = nullptr)
    {
        juce::AudioIODeviceCallbackContext ctx;
        for (int b = 0; b < blocks; ++b)
        {
            e.audioDeviceIOCallbackWithContext (nullptr, 0, o.ptrs.data(), (int) o.ptrs.size(), block, ctx);
            if (peaks != nullptr)
                for (size_t ch = 0; ch < o.ptrs.size(); ++ch)
                    peaks[ch] = std::max (peaks[ch], o.peak ((int) ch));
        }
    }

    // Salto máximo entre muestras consecutivas del canal 0 durante "blocks" bloques (también entre bloques)
    float maxJump (AudioEngine& e, FakeOutputs& o, int blocks, bool* stillPlaying = nullptr)
    {
        float prev = o.data[0][block - 1], jump = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            render (e, o, 1);
            for (float v : o.data[0]) { jump = std::max (jump, std::abs (v - prev)); prev = v; }
            if (stillPlaying != nullptr && ! e.isPlaying())
                break;
        }
        return jump;
    }

    std::shared_ptr<LoadedSong> loadFolder (const juce::File& folder, juce::AudioFormatManager& formats, double rate)
    {
        SongInfo info;
        info.folder = folder;
        for (auto& f : Library::audioFilesIn (folder))
            info.stems.push_back (stem (f.getFileNameWithoutExtension(), f.getFileName()));
        return AudioEngine::loadSong (info, rate, formats, {});
    }
}

static SongMarker mk (const char* name, double seconds)
{
    SongMarker m;
    m.name = name;
    m.seconds = seconds;
    return m;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("secuencias_tests", "", false);
    auto src = tmp.getChildFile ("src");
    src.createDirectory();
    writeSine (src.getChildFile ("drums.wav"), 44100.0, 3.0, 2);
    writeSine (src.getChildFile ("vocals.wav"), 48000.0, 2.0, 1);   // mono y otra frecuencia

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::cout << "[Library] importar, guardar y recargar\n";
    Library lib (tmp.getChildFile ("lib"));
    const int idx = lib.importStemFolder (src, tr ("Canción ñandú"));
    CHECK (idx == 0);
    CHECK (lib.songs.size() == 1);
    auto& info = lib.songs[0];
    CHECK (info.name == tr ("Canción ñandú"));
    CHECK (info.stems.size() == 2);
    CHECK (info.stems[0].name == tr ("Batería"));   // nombres de Demucs traducidos
    CHECK (info.stems[1].name == "Voces");
    info.markers.push_back (mk ("Coro", 2.0));
    info.markers.push_back (mk ("Intro", 0.0));
    info.sortMarkers();
    info.stems[1].gainDb = -6.0f;
    info.stems[1].outputPair = 1;
    CHECK (lib.saveSong (info));
    {
        Library again (tmp.getChildFile ("lib"));
        again.load();
        CHECK (again.songs.size() == 1);
        CHECK (again.songs[0].markers.size() == 2);
        CHECK (again.songs[0].markers[0].name == "Intro");
        CHECK (std::abs (again.songs[0].stems[1].gainDb + 6.0f) < 0.01f);
        CHECK (again.songs[0].stems[1].outputPair == 1);
    }

    std::cout << "[Library] song.json con valores fuera de rango o campos faltantes\n";
    {
        auto root = tmp.getChildFile ("lib2");
        root.createDirectory();
        auto rara = root.getChildFile ("Rara");
        rara.createDirectory();
        writeSine (rara.getChildFile ("x.wav"), 44100.0, 0.2, 1);
        rara.getChildFile ("song.json").replaceWithText (
            R"({"name":"Rara","clickOutputPair":-1,"bpm":-5,"stems":[{"name":"X","file":"x.wav","outputPair":-1,"gainDb":99}]})");
        auto vieja = root.getChildFile ("Vieja");          // song.json vacío: todo por defecto
        vieja.createDirectory();
        writeSine (vieja.getChildFile ("y.wav"), 44100.0, 0.2, 1);
        vieja.getChildFile ("song.json").replaceWithText ("{}");
        auto vacia = root.getChildFile ("Vacia");          // en el setlist pero sin audio: no es una canción
        vacia.createDirectory();
        vacia.getChildFile ("song.json").replaceWithText (R"({"name":"Vacia"})");
        auto huerfana = root.getChildFile ("Huerfana");    // con audio pero fuera del setlist: se agrega al final
        huerfana.createDirectory();
        writeSine (huerfana.getChildFile ("z.wav"), 44100.0, 0.2, 1);
        root.getChildFile ("setlist.json").replaceWithText (R"(["Vacia","Rara","Vieja"])");

        Library l2 (root);
        l2.load();
        CHECK (l2.songs.size() == 3);
        CHECK (l2.songs[0].name == "Rara");
        CHECK (l2.songs[0].clickOutputPair == 0);          // acotado
        CHECK (l2.songs[0].stems[0].outputPair == 0);
        CHECK (l2.songs[0].stems[0].gainDb <= 12.0f);
        CHECK (l2.songs[0].bpm >= 20.0);
        CHECK (l2.songs[1].name == "Vieja");
        CHECK (std::abs (l2.songs[1].bpm - 120.0) < 1.0e-9 && l2.songs[1].stems.size() == 1 && l2.songs[1].stems[0].fileName == "y.wav");
        CHECK (l2.songs[2].folder.getFileName() == "Huerfana");
    }

    std::cout << "[TempoMap] mapa original <-> reproduccion por tramos\n";
    {
        SongInfo si;
        si.bpm = 100.0;
        // Sin marcadores ni tempo pedido: identidad
        auto m = TimeMap::build (si, 60.0);
        CHECK (m.isIdentity() && std::abs (m.playbackLength() - 60.0) < 1.0e-9 && std::abs (m.toPlayback (12.5) - 12.5) < 1.0e-9);
        // Canción entera a 50 BPM (mitad de tempo): dura el doble
        si.playBpm = 50.0;
        m = TimeMap::build (si, 60.0);
        CHECK (! m.isIdentity() && std::abs (m.playbackLength() - 120.0) < 1.0e-9);
        CHECK (std::abs (m.toPlayback (10.0) - 20.0) < 1.0e-9 && std::abs (m.toOriginal (20.0) - 10.0) < 1.0e-9);
        // Dos secciones de tempo: la segunda (desde 20 s) con tempo propio de 200 BPM: el primer tramo x2, el segundo x0.5
        si.tempoRegions = { { 0.0, 100.0, 0.0 }, { 20.0, 100.0, 200.0 } };
        m = TimeMap::build (si, 60.0);
        CHECK (m.segments().size() == 2);
        CHECK (std::abs (m.segments()[0].ratio - 2.0) < 1.0e-9 && std::abs (m.segments()[1].ratio - 0.5) < 1.0e-9);
        CHECK (std::abs (m.playbackLength() - (40.0 + 20.0)) < 1.0e-9);
        CHECK (std::abs (m.toPlayback (20.0) - 40.0) < 1.0e-9 && std::abs (m.toPlayback (40.0) - 50.0) < 1.0e-9);
        CHECK (std::abs (m.toOriginal (50.0) - 40.0) < 1.0e-9 && std::abs (m.toOriginal (m.toPlayback (33.3)) - 33.3) < 1.0e-9);
        CHECK (std::abs (effectivePlayBpm (si, si.tempoRegions[0]) - 50.0) < 1.0e-9 && std::abs (effectivePlayBpm (si, si.tempoRegions[1]) - 200.0) < 1.0e-9);
        // Secciones fuera del audio o repetidas no crean tramos (de dos con el mismo inicio manda la última)
        si.playBpm = 0.0;
        si.tempoRegions.push_back ({ 20.0, 100.0, 0.0 });
        si.tempoRegions.push_back ({ 90.0, 100.0, 0.0 });
        si.sortTempoRegions();
        CHECK (TimeMap::build (si, 60.0).segments().size() == 2 && std::abs (TimeMap::build (si, 60.0).playbackLength() - 60.0) < 1.0e-9);
        CHECK (si.tempoRegionAt (0.0) == 0 && si.tempoRegionAt (25.0) == 2 && si.tempoRegionAt (95.0) == 3);
        // Sin sección en 0, se crea una con el tempo de la primera
        si.tempoRegions = { { 10.0, 150.0, 0.0 } };
        si.sortTempoRegions();
        CHECK (si.tempoRegions.size() == 2 && std::abs (si.tempoRegions[0].start) < 1.0e-9 && std::abs (si.tempoRegions[0].origBpm - 150.0) < 1.0e-9);
        // Vecinas iguales se unen (mismo tempo original y de reproducción); distintas se conservan
        si.tempoRegions = { { 0.0, 100.0, 0.0 }, { 5.0, 100.0, 0.0 }, { 8.0, 120.0, 0.0 }, { 9.0, 120.0, 110.0 }, { 12.0, 100.0, 0.0 } };
        si.mergeEqualTempoRegions();
        CHECK (si.tempoRegions.size() == 4 && std::abs (si.tempoRegions[1].start - 8.0) < 1.0e-9 && std::abs (si.tempoRegions[2].start - 9.0) < 1.0e-9 && std::abs (si.tempoRegions[3].start - 12.0) < 1.0e-9);
        // Sin secciones pero con análisis, el tempo original es el del análisis
        si.tempoRegions.clear();
        si.analysis.bpm = 120.0;
        si.playBpm = 120.0;
        m = TimeMap::build (si, 60.0);
        CHECK (m.isIdentity());   // pedir 120 sobre 120 detectados = sin cambio, aunque bpm de la canción diga 100
    }

    std::cout << "[TempoMap] deteccion de secciones con tempo distinto\n";
    {
        // 60 s a 100 BPM, 60 s a 130 BPM y 30 s a 100 BPM, con un poco de jitter y compases de 4
        Analysis a;
        a.meter = 4;
        double t = 0.0;
        int beat = 0;
        auto add = [&] (double seconds, double bpm)
        {
            const double end = t + seconds;
            while (t < end)
            {
                a.beats.push_back ({ t + ((beat % 3) - 1) * 0.004, beat % 4 + 1 });
                t += 60.0 / bpm;
                ++beat;
            }
        };
        add (60.0, 100.0);
        const double change1 = t;
        add (60.0, 130.0);
        const double change2 = t;
        add (30.0, 100.0);
        a.bpm = 110.0;
        const auto regions = detectTempoRegions (a, 120.0);
        CHECK (regions.size() == 3);
        if (regions.size() == 3)
        {
            CHECK (std::abs (regions[0].start) < 1.0e-9 && std::abs (regions[0].origBpm - 100.0) < 1.0);
            CHECK (std::abs (regions[1].origBpm - 130.0) < 1.5 && std::abs (regions[1].start - change1) < 1.3);   // a lo más dos tiempos (ajuste al compás)
            CHECK (std::abs (regions[2].origBpm - 100.0) < 1.0 && std::abs (regions[2].start - change2) < 1.3);
            CHECK (regions[1].playBpm <= 0.0);
        }
        // Tempo constante con jitter: una sola sección con la mediana
        Analysis b;
        for (int i = 0; i < 200; ++i) b.beats.push_back ({ std::round ((i * 0.5 + ((i % 5) - 2) * 0.006) * 100.0) / 100.0, i % 4 + 1 });   // en pasos de 10 ms, como madmom
        const auto one = detectTempoRegions (b, 90.0);
        CHECK (one.size() == 1 && std::abs (one[0].origBpm - 120.0) < 0.5);
        // Pocos tiempos: una sección con el tempo del análisis o el de respaldo
        Analysis c;
        c.beats = { { 0.0, 1 }, { 0.5, 2 } };
        CHECK (detectTempoRegions (c, 90.0).size() == 1 && std::abs (detectTempoRegions (c, 90.0)[0].origBpm - 120.0) < 0.2);
        CHECK (detectTempoRegions (Analysis(), 90.0).size() == 1 && std::abs (detectTempoRegions (Analysis(), 90.0)[0].origBpm - 90.0) < 1.0e-9);
    }

    std::cout << "[Stretcher] estirar sin cambiar el tono, transponer, identidad\n";
    {
        const double rate = 48000.0;
        auto zeroCrossingsPerSecond = [] (const juce::AudioBuffer<float>& b, double fs, double from, double to)
        {
            int crossings = 0;
            const int a = (int) (from * fs), z = juce::jmin (b.getNumSamples(), (int) (to * fs));
            for (int i = a + 1; i < z; ++i)
                if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f)) ++crossings;
            return (double) crossings / (to - from);
        };
        auto tone = sine (rate, 3.0, 2);   // 0.05 rad/muestra = 382 Hz a 48 kHz -> 764 cruces por cero por segundo
        SongInfo si;
        si.bpm = 120.0;
        si.playBpm = 60.0;                 // la mitad de tempo: el doble de largo, misma altura
        auto slow = stretcher::renderBuffer (tone, TimeMap::build (si, 3.0), rate);
        CHECK (std::abs (slow.getNumSamples() - 6.0 * rate) < rate * 0.02);
        CHECK (std::abs (zeroCrossingsPerSecond (slow, rate, 1.0, 5.0) - 764.0) < 764.0 * 0.03);
        CHECK (slow.getMagnitude (0, (int) rate, (int) (4.0 * rate)) > 0.3f);   // nivel conservado
        si.playBpm = 0.0;
        si.transpose = 12;
        auto up = stretcher::renderBuffer (tone, TimeMap::build (si, 3.0), rate);   // una octava arriba, mismo largo
        CHECK (std::abs (up.getNumSamples() - 3.0 * rate) < rate * 0.02);
        CHECK (std::abs (zeroCrossingsPerSecond (up, rate, 0.5, 2.5) - 1528.0) < 1528.0 * 0.04);
        // Tono por sección: solo la segunda mitad sube una octava; la primera sigue la canción (0 st)
        si.transpose = 0;
        si.tempoRegions = { { 0.0, 120.0, 0.0 }, { 1.5, 120.0, 0.0 } };
        si.tempoRegions[1].transpose = 12;
        const auto half = TimeMap::build (si, 3.0);
        CHECK (! half.isPlain() && half.isIdentity() && half.hasPitchShift() && half.segments()[0].transpose == 0 && half.segments()[1].transpose == 12);
        CHECK (half.segmentAtPlayback (0.5)->transpose == 0 && half.segmentAtPlayback (2.0)->transpose == 12 && half.segmentAtPlayback (99.0)->transpose == 12);
        auto halfUp = stretcher::renderBuffer (tone, half, rate);
        CHECK (std::abs (zeroCrossingsPerSecond (halfUp, rate, 0.3, 1.2) - 764.0) < 764.0 * 0.05);
        CHECK (std::abs (zeroCrossingsPerSecond (halfUp, rate, 1.9, 2.8) - 1528.0) < 1528.0 * 0.05);
        si.tempoRegions.clear();
        // Mapa con dos tramos distintos: el largo total sigue el mapa
        si.tempoRegions = { { 0.0, 120.0, 0.0 }, { 1.0, 120.0, 240.0 } };
        si.playBpm = 60.0;
        const auto map = TimeMap::build (si, 3.0);     // 1 s x2 + 2 s x0.5 = 3 s
        auto mixed = stretcher::renderBuffer (tone, map, rate);
        CHECK (std::abs (mixed.getNumSamples() - map.playbackLength() * rate) < 2.0);
        // Identidad: se devuelve la misma canción
        auto song0 = std::make_shared<LoadedSong>();
        song0->length = tone.getNumSamples();
        song0->sampleRate = rate;
        auto tr = std::make_unique<LoadedTrack>();
        tr->buffer = tone;
        song0->tracks.push_back (std::move (tr));
        si.tempoRegions.clear();
        si.playBpm = 0.0;
        CHECK (stretcher::render (song0, TimeMap::build (si, 3.0), rate, {}) == song0);
        si.playBpm = 90.0;
        float lastProgress = 0.0f;
        auto rendered = stretcher::render (song0, TimeMap::build (si, 3.0), rate, {}, [&] (float p) { lastProgress = p; });
        CHECK (rendered != nullptr && rendered != song0 && rendered->tracks.size() == 1
               && std::abs ((double) rendered->length - 4.0 * rate) < rate * 0.02 && lastProgress > 0.99f);
        CHECK (rendered != nullptr && rendered->tracks[0]->waveform.numBins() > 0);
        CHECK (stretcher::render (song0, TimeMap::build (si, 3.0), rate, [] { return true; }) == nullptr);   // abortado
    }

    std::cout << "[Loudness] EBU R128: seno de 1 kHz a -23 dBFS mide -23 LUFS; pico real; tramos; puertas\n";
    {
        const double rate = 48000.0;
        const int n = (int) (rate * 6.0);
        juce::AudioBuffer<float> b (2, n);
        const float amp = juce::Decibels::decibelsToGain (-23.0f);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                double t = (double) i / rate;
                float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * t);
                if (t >= 2.0 && t < 4.0) v *= juce::Decibels::decibelsToGain (-10.0f);   // tramo 10 dB más bajo
                if (t >= 4.0) v = 0.0f;                                                 // silencio: la puerta lo descarta
                b.setSample (ch, i, v);
            }
        std::vector<loudness::Section> secs = { { 0.0, 2.0, {} }, { 2.0, 4.0, {} }, { 4.0, 6.0, {} }, { 5.0, 3.0, {} } };
        const auto r = loudness::measure (b, rate, secs);
        CHECK (std::abs (r.sections[0].result.lufs + 23.0) < 0.2);
        CHECK (std::abs (r.sections[1].result.lufs + 33.0) < 0.2);
        CHECK (r.sections[2].result.lufs <= loudness::unknown + 1.0);              // silencio
        CHECK (r.sections[3].result.lufs <= loudness::unknown + 1.0);              // tramo inválido
        CHECK (std::abs (r.sections[0].result.truePeakDb + 23.0) < 0.3);
        CHECK (r.whole.lufs > -26.0 && r.whole.lufs < -22.5);                       // la puerta relativa deja fuera el tramo bajo, casi
        CHECK (std::abs (loudness::gainToTarget (r.sections[0].result, -14.0) - 9.0) < 0.3);
        CHECK (std::abs (loudness::gainToTarget (r.sections[1].result, -14.0) - 19.0) < 0.3);
        CHECK (loudness::gainToTarget (r.sections[2].result, -14.0) < 1.0e-9);              // sin medir: 0
        CHECK (std::abs (loudness::gainToTarget ({ -30.0, -3.0 }, -14.0) - 2.0) < 1.0e-9);   // limitado por el pico: -3 dBTP -> -1
        CHECK (std::abs (loudness::gainToTarget ({ -10.0, -1.0 }, -14.0) + 4.0) < 1.0e-9);   // bajar no tiene tope
        // Otra frecuencia de muestreo: mismo resultado
        juce::AudioBuffer<float> b44 (2, 44100 * 2);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b44.getNumSamples(); ++i)
                b44.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * i / 44100.0));
        CHECK (std::abs (loudness::measure (b44, 44100.0, {}).whole.lufs + 23.0) < 0.2);
    }

    std::cout << "[Analyzer] etiquetas, resultado y utilidades del analisis\n";
    {
        CHECK (Analyzer::displayChord ("C:maj") == "C" && Analyzer::displayChord ("A:min") == "Am");
        CHECK (Analyzer::displayChord ("G:7") == "G7" && Analyzer::displayChord ("N") == "N" && Analyzer::displayChord ("") == "N");
        CHECK (Analyzer::displayKey ("A minor") == "La menor" && Analyzer::displayKey ("F# major") == "Fa# mayor");
        CHECK (Analyzer::displayKey ("") .isEmpty());
        CHECK (Analyzer::isDrumsTrack (tr ("Batería"), "drums.wav") && Analyzer::isDrumsTrack ("x", "drums.wav") && ! Analyzer::isDrumsTrack ("Bajo", "bass.wav"));

        juce::String err;
        const auto json = juce::JSON::parse (
            R"({"bpm":128.0,"meter":4,"key":"A minor","beats":[[0.5,1],[0.9688,2],[1.4375,3],[1.9063,4],[2.375,1]],)"
            R"("chords":[[0.0,0.5,"N"],[0.5,1.4,"A:min"],[1.4,2.4,"A:min"],[2.4,3.0,"F:maj"]]})");
        auto a = Analyzer::parseResult (json, err);
        CHECK (err.isEmpty());
        CHECK (a.beats.size() == 5 && a.beats[4].beatInBar == 1 && std::abs (a.bpm - 128.0) < 0.01 && a.key == "La menor");
        // "N" (sin acorde) se conserva; los acordes iguales consecutivos se unen
        CHECK (a.chords.size() == 3 && a.chords[0].name == "N" && a.chords[1].name == "Am" && std::abs (a.chords[1].end - 2.4) < 0.001);
        CHECK (a.chordAt (1.0) == 1 && a.chordAt (2.5) == 2 && a.chordAt (3.5) == -1);
        CHECK (a.downbeatAfter (0.0) == 0 && a.downbeatAfter (0.6) == 4 && a.downbeatAfter (3.0) == -1);
        CHECK (std::abs (a.bpmBetween (0.0, 3.0) - 128.0) < 1.0);
        CHECK (a.bpmBetween (0.0, 0.6) < 0.001);   // un solo tiempo: sin tempo

        // Ida y vuelta por song.json
        SongInfo si;
        si.folder = tmp.getChildFile ("an");
        si.folder.createDirectory();
        si.analysis = a;
        CHECK (lib.saveSong (si));
        Library l3 (tmp);
        writeSine (si.folder.getChildFile ("z.wav"), 44100.0, 0.2, 1);
        l3.load();
        const SongInfo* back = nullptr;
        for (auto& sng : l3.songs) if (sng.folder == si.folder) back = &sng;
        CHECK (back != nullptr && back->analysis.beats.size() == 5 && back->analysis.chords.size() == 3
               && back->analysis.chords[2].name == "F" && back->analysis.key == "La menor");
        // Canción analizada sin secciones de tempo guardadas: se detectan al cargar (una sola, con el tempo de los tiempos)
        CHECK (back != nullptr && back->tempoRegions.size() == 1 && std::abs (back->tempoRegions[0].origBpm - 128.0) < 1.0);
        // Secciones guardadas: ida y vuelta, ordenadas y con la primera en 0; escritura y tonalidad elegida
        si.tempoRegions = { { 1.5, 140.0, 150.0 }, { 0.0, 128.0, 0.0 } };
        si.tempoRegions[0].transpose = -3;
        si.spelling = 2;
        si.keyOverride = "Eb major";
        CHECK (lib.saveSong (si));
        Library l4 (tmp);
        l4.load();
        back = nullptr;
        for (auto& sng : l4.songs) if (sng.folder == si.folder) back = &sng;
        CHECK (back != nullptr && back->tempoRegions.size() == 2 && std::abs (back->tempoRegions[0].start) < 1.0e-9
               && std::abs (back->tempoRegions[1].start - 1.5) < 1.0e-9 && std::abs (back->tempoRegions[1].origBpm - 140.0) < 1.0e-9
               && std::abs (back->tempoRegions[1].playBpm - 150.0) < 1.0e-9
               && back->tempoRegions[1].transpose == -3 && back->tempoRegions[0].transpose == TempoRegion::followSong);
        CHECK (back != nullptr && back->spelling == 2 && back->keyOverride == "Eb major");
        // Notas de texto: ida y vuelta, ordenadas; las vacías no se guardan
        si.notes = { { 5.0, 0.0, "Coro" }, { 1.0, 6.0, tr ("Entra el bajo") }, { 2.0, 3.0, "" } };
        CHECK (lib.saveSong (si));
        Library l7 (tmp);
        l7.load();
        back = nullptr;
        for (auto& sng : l7.songs) if (sng.folder == si.folder) back = &sng;
        CHECK (back != nullptr && back->notes.size() == 2 && std::abs (back->notes[0].seconds - 1.0) < 1.0e-9 && back->notes[0].text == tr ("Entra el bajo")
               && std::abs (back->notes[1].duration) < 1.0e-9 && back->notes[1].text == "Coro");
        // Nivelado por pista: vectores por stem, ajustados al número de stems al leer
        si.stems.clear();
        si.stems.push_back (stem ("Z", "z.wav"));
        si.markers = { mk ("A", 0.1) };
        si.markers[0].stemGainsDb = { 3.5 };
        si.markers[0].stemLufs = { -20.0 };
        si.headStemGainsDb = { -2.0 };
        si.stemSongLufs = { -18.0 };
        CHECK (lib.saveSong (si));
        Library l5 (tmp);
        l5.load();
        back = nullptr;
        for (auto& sng : l5.songs) if (sng.folder == si.folder) back = &sng;
        CHECK (back != nullptr && back->stems.size() == 1 && back->markers.size() == 1 && back->markers[0].stemGainsDb.size() == 1
               && std::abs (back->markers[0].stemGainsDb[0] - 3.5) < 1.0e-9 && std::abs (back->markers[0].stemLufs[0] + 20.0) < 1.0e-9
               && back->headStemGainsDb.size() == 1 && std::abs (back->headStemGainsDb[0] + 2.0) < 1.0e-9
               && back->headStemLufs.size() == 1 && back->headStemLufs[0] <= unmeasuredDb + 1.0 && std::abs (back->stemSongLufs[0] + 18.0) < 1.0e-9);
        // Proyecto: exportar la carpeta y volver a importarla en otra biblioteca con todos sus ajustes
        const int idx5 = (int) (back - l5.songs.data());
        auto exportDir = tmp.getChildFile ("export");
        exportDir.createDirectory();
        CHECK (l5.exportSong (idx5, exportDir));
        const auto exported = exportDir.getChildFile (si.folder.getFileName());
        CHECK (Library::isProjectFolder (exported) && ! Library::isProjectFolder (exportDir));
        auto otherRoot = tmp.getChildFile ("otra");
        otherRoot.createDirectory();
        Library l6 (otherRoot);
        l6.load();
        const int imported = l6.importProject (exported);
        CHECK (imported == 0 && l6.songs.size() == 1 && l6.songs[0].keyOverride == "Eb major" && l6.songs[0].spelling == 2
               && l6.songs[0].markers.size() == 1 && std::abs (l6.songs[0].markers[0].stemGainsDb[0] - 3.5) < 1.0e-9
               && l6.songs[0].tempoRegions.size() == 2 && l6.songs[0].folder.getChildFile ("z.wav").existsAsFile());
        CHECK (l6.importProject (exportDir) == -1);   // no es una carpeta de canción
        auto allDir = tmp.getChildFile ("todo");
        allDir.createDirectory();
        CHECK (l6.exportAll (allDir) && allDir.getChildFile ("setlist.json").existsAsFile());

        // Resultado inválido
        auto bad = Analyzer::parseResult (juce::JSON::parse ("{}"), err);
        CHECK (bad.isEmpty() && err.isNotEmpty());
    }

    std::cout << "[Music] enarmonias: notas, acordes y tonalidades\n";
    {
        CHECK (music::pitchClass ("C") == 0 && music::pitchClass ("C#") == 1 && music::pitchClass ("Db") == 1 && music::pitchClass ("Bb") == 10 && music::pitchClass ("B") == 11);
        CHECK (music::pitchClass ("Do") == 0 && music::pitchClass ("Reb") == 1 && music::pitchClass ("Sol#") == 8 && music::pitchClass ("Si") == 11 && music::pitchClass ("Sib") == 10);
        CHECK (music::pitchClass ("H") == -1 && music::pitchClass ("") == -1);
        juce::String rest;
        CHECK (music::pitchClass ("A#m7", &rest) == 10 && rest == "m7");
        CHECK (music::noteName (3, true, false) == "Eb" && music::noteName (3, false, false) == "D#" && music::noteName (3, true, true) == "Mib" && music::noteName (-1, false, true) == "Si");
        CHECK (music::spellChord ("A#m7", true) == "Bbm7" && music::spellChord ("Bbm7", false) == "A#m7" && music::spellChord ("Am", true) == "Am");
        CHECK (music::spellChord ("N", true) == "N" && music::spellChord ("", false).isEmpty() && music::spellChord ("G#7", true) == "Ab7" && music::spellChord ("F", true) == "F");
        CHECK (music::spellKey ("Eb major", false) == "Re# mayor" && music::spellKey ("Mib mayor", true) == "Mib mayor" && music::spellKey ("D# minor", true) == "Mib menor");
        CHECK (music::spellKey ("La menor", false) == "La menor" && music::spellKey ("", true).isEmpty() && music::spellKey ("x mayor", true).isEmpty());
        CHECK (music::keyPrefersFlats ("Eb major") && music::keyPrefersFlats ("Sib mayor") && music::keyPrefersFlats ("D minor") && music::keyPrefersFlats ("Sol menor"));
        CHECK (! music::keyPrefersFlats ("D major") && ! music::keyPrefersFlats ("F# major") && ! music::keyPrefersFlats ("E minor") && ! music::keyPrefersFlats ("C major") && ! music::keyPrefersFlats (""));
        CHECK (music::canonicalKey (3, false) == "Eb major" && music::canonicalKey (6, false) == "F# major" && music::canonicalKey (3, true) == "D# minor" && music::canonicalKey (10, true) == "Bb minor");
        CHECK (music::isMinorKey ("La menor") && music::isMinorKey ("A minor") && ! music::isMinorKey ("Re mayor"));
    }

    std::cout << "[Analysis] renumerar tiempos (metrica), tiempo mas cercano\n";
    {
        Analysis a;
        for (int i = 0; i < 16; ++i) a.beats.push_back ({ i * 0.5, i % 4 + 1 });
        a.meter = 4;
        CHECK (a.nearestBeat (2.6, 0.2) == 5 && a.nearestBeat (2.75, 0.3) == 5 && a.nearestBeat (2.76, 0.3) == 6 && a.nearestBeat (2.6, 0.05) == -1);
        // El tiempo 6 (segundo del segundo compás) pasa a ser el 1 con compases de 4 hasta el 12: el compás anterior queda de 1 tiempo
        a.renumberBeats (5, 4, 12);
        CHECK (a.beats[4].beatInBar == 1 && a.beats[5].beatInBar == 1 && a.beats[6].beatInBar == 2 && a.beats[8].beatInBar == 4 && a.beats[9].beatInBar == 1 && a.beats[11].beatInBar == 3);
        CHECK (a.beats[12].beatInBar == 1 && a.beats[15].beatInBar == 4);   // fuera del rango, sin cambios
        // Compases de 3 hasta el final y el compás detectado sigue al máximo
        a.renumberBeats (0, 3, 100);
        CHECK (a.beats[0].beatInBar == 1 && a.beats[2].beatInBar == 3 && a.beats[3].beatInBar == 1 && a.beats[15].beatInBar == 1 && a.meter == 3);
        a.renumberBeats (0, 9, 16);   // se acota a 7
        CHECK (a.meter == 7 && a.beats[7].beatInBar == 1);
        // Ancla distinta del inicio: el tiempo 6 es el 1 y los anteriores se numeran hacia atrás
        a.renumberBeats (0, 4, 16, 6);
        CHECK (a.beats[6].beatInBar == 1 && a.beats[5].beatInBar == 4 && a.beats[2].beatInBar == 1 && a.beats[0].beatInBar == 3 && a.beats[10].beatInBar == 1);
        CHECK (a.firstBeatAtOrAfter (2.5) == 5 && a.firstBeatAtOrAfter (2.51) == 6 && a.firstBeatAtOrAfter (100.0) == 16 && a.firstDownbeatIn (3, 16) == 6 && a.firstDownbeatIn (7, 10) == 7);
    }

    std::cout << "[Analysis] click de una seccion: mitad, doble, rejilla fija\n";
    {
        // 8 s a 120 BPM (negras) y luego 8 s con tiempos en corcheas (madmom al doble), compases de 4
        Analysis a;
        for (int i = 0; i < 16; ++i) a.beats.push_back ({ i * 0.5, i % 4 + 1 });
        for (int i = 0; i < 32; ++i) a.beats.push_back ({ 8.0 + i * 0.25, i % 4 + 1 });
        a.meter = 4;
        CHECK (std::abs (detectedBpmBetween (a, 0.0, 8.0) - 120.0) < 0.2 && std::abs (detectedBpmBetween (a, 8.0, 16.0) - 240.0) < 0.2);
        CHECK (detectedBpmBetween (a, 100.0, 200.0) < 1.0e-9);
        // Mitad en la segunda parte: quedan 16 tiempos a 0,5 s, el primero sigue siendo el 1 del compás
        a.halveBeats (a.firstBeatAtOrAfter (8.0), (int) a.beats.size(), 4);
        CHECK (a.beats.size() == 32 && std::abs (a.beats[16].seconds - 8.0) < 1.0e-9 && std::abs (a.beats[17].seconds - 8.5) < 1.0e-9);
        CHECK (a.beats[16].beatInBar == 1 && a.beats[19].beatInBar == 4 && a.beats[20].beatInBar == 1 && a.beats[15].beatInBar == 4);
        CHECK (std::abs (detectedBpmBetween (a, 8.0, 16.0) - 120.0) < 0.2);
        // Doble en la primera parte: 31 tiempos a 0,25 s (no se añade tras el último del tramo), el compás sigue anclado al 1
        a.doubleBeats (0, 16, 4);
        CHECK (a.beats.size() == 47 && std::abs (a.beats[1].seconds - 0.25) < 1.0e-9 && a.beats[0].beatInBar == 1 && a.beats[4].beatInBar == 1 && a.beats[3].beatInBar == 4);
        CHECK (std::abs (a.beats[31].seconds - 8.0) < 1.0e-9 && a.beats[31].beatInBar == 1);   // la segunda parte no se toca
        // La paridad se conserva respecto del primer tiempo de compás: si el tramo empieza en un 3, se conserva el 1 siguiente
        Analysis b;
        for (int i = 0; i < 12; ++i) b.beats.push_back ({ i * 0.25, (i + 2) % 4 + 1 });   // el índice 2 es el 1
        b.halveBeats (0, 12, 4);
        CHECK (b.beats.size() == 6 && std::abs (b.beats[1].seconds - 0.5) < 1.0e-9 && b.beats[1].beatInBar == 1 && b.beats[0].beatInBar == 4 && b.beats[5].beatInBar == 1);
        // Rejilla fija: entre 4 y 8 s a 100 BPM, compases de 3
        Analysis c;
        for (int i = 0; i < 24; ++i) c.beats.push_back ({ i * 0.5, i % 4 + 1 });
        c.replaceBeatsWithGrid (4.0, 8.0, 100.0, 3);
        int inRange = 0;
        for (auto& bt : c.beats) if (bt.seconds >= 4.0 - 1.0e-9 && bt.seconds < 8.0 - 1.0e-9) ++inRange;
        CHECK (inRange == 7 && c.beats.size() == 8 + 7 + 8);   // 4 s / 0,6 s = 6,67 -> 7 tiempos (4,0 ... 7,6)
        CHECK (std::abs (c.beats[8].seconds - 4.0) < 1.0e-9 && std::abs (c.beats[9].seconds - 4.6) < 1.0e-9 && c.beats[8].beatInBar == 1 && c.beats[11].beatInBar == 1 && c.beats[15].beatInBar == 1);
        CHECK (std::abs (c.beats[16].seconds - 8.5) < 1.0e-9 || std::abs (c.beats[15].seconds - 8.0) < 1.0e-9);   // lo de después sigue
        c.replaceBeatsWithGrid (8.0, 4.0, 100.0, 4);   // rango vacío: sin cambios
        CHECK (c.beats.size() == 23);
        // Rejilla pareja: 5 tiempos iguales entre 4 y 7,01 s (una transición donde madmom puso corcheas):
        // el sobrante de 1 se suma al compás anterior (compás de 5) en vez de quedar como compás de 1 tiempo
        Analysis e;
        for (int i = 0; i < 24; ++i) e.beats.push_back ({ i * 0.5, i % 4 + 1 });
        const double bpm = e.evenGrid (4.0, 7.01, 5, 4);
        CHECK (std::abs (bpm - 60.0 * 5 / 3.01) < 0.01);
        int n = 0;
        for (auto& bt : e.beats) if (bt.seconds >= 4.0 - 1.0e-9 && bt.seconds < 7.01 - 1.0e-9) ++n;
        CHECK (n == 5 && std::abs (e.beats[9].seconds - (4.0 + 0.602)) < 1.0e-6 && e.beats[8].beatInBar == 1 && e.beats[11].beatInBar == 4 && e.beats[12].beatInBar == 5 && e.meter == 5);
        // Sobrante de medio compás o más: queda como compás corto (6 tiempos = 4 + 2)
        Analysis f;
        for (int i = 0; i < 24; ++i) f.beats.push_back ({ i * 0.5, i % 4 + 1 });
        f.evenGrid (4.0, 7.0, 6, 4);
        CHECK (f.beats[12].beatInBar == 1 && f.beats[13].beatInBar == 2 && f.meter == 4);
        CHECK (f.evenGrid (7.0, 4.0, 3, 4) < 1.0e-9);   // rango vacío
        // Sobrante como compás aparte: 4 + 1, el último tiempo es un 1 (el click lo acentúa)
        Analysis g;
        for (int i = 0; i < 24; ++i) g.beats.push_back ({ i * 0.5, i % 4 + 1 });
        g.evenGrid (4.0, 7.01, 5, 4, false);
        CHECK (g.beats[11].beatInBar == 4 && g.beats[12].beatInBar == 1 && g.beats[13].beatInBar == 4 && g.meter == 4);   // el 13 es el original de 7,5 s, sin tocar
        // Compás corto solo aquí: el tiempo 9 forma un compás de 1 y desde el 10 siguen compases de 4
        Analysis h;
        for (int i = 0; i < 20; ++i) h.beats.push_back ({ i * 0.5, i % 4 + 1 });
        h.shortBar (9, 1, 4, 20);
        CHECK (h.beats[8].beatInBar == 1 && h.beats[9].beatInBar == 1 && h.beats[10].beatInBar == 1 && h.beats[13].beatInBar == 4 && h.beats[14].beatInBar == 1);
        h.shortBar (2, 2, 4, 6);   // compás de 2 en los tiempos 2 y 3; del 4 al 5 se renumera, del 6 en adelante no
        CHECK (h.beats[2].beatInBar == 1 && h.beats[3].beatInBar == 2 && h.beats[4].beatInBar == 1 && h.beats[5].beatInBar == 2 && h.beats[6].beatInBar == 3);
        h.shortBar (19, 3, 4, 20);   // al final de la canción: solo el último tiempo, sin salirse
        CHECK (h.beats[19].beatInBar == 1 && h.beats.size() == 20);
    }

    std::cout << "[Arrangement] tramos: cortar, mover, eliminar, unir\n";
    {
        std::vector<Clip> clips;
        CHECK (arrangement::isIdentity (clips, 10.0) && std::abs (arrangement::lengthSeconds (clips, 10.0) - 10.0) < 1.0e-9);
        arrangement::ensureClips (clips, 10.0);
        CHECK (clips.size() == 1 && arrangement::isIdentity (clips, 10.0) && arrangement::clipAt (clips, 5.0) == 0 && arrangement::clipAt (clips, 10.0) == -1);
        CHECK (arrangement::cutAt (clips, 4.0, 10.0) && clips.size() == 2 && std::abs (clips[1].srcStart - 4.0) < 1.0e-9 && std::abs (clips[1].position - 4.0) < 1.0e-9 && std::abs (clips[0].srcEnd - 4.0) < 1.0e-9);
        CHECK (! arrangement::cutAt (clips, 4.0, 10.0) && ! arrangement::cutAt (clips, 20.0, 10.0));   // en un corte existente o fuera: nada
        CHECK (! arrangement::isIdentity (clips, 10.0) && arrangement::canJoinWithPrevious (clips, 1));
        // Mover el segundo tramo 0,5 s más tarde: hueco de 0,5 s, largo total 10,5
        arrangement::moveClip (clips, 1, 0.5, false);
        CHECK (std::abs (clips[1].position - 4.5) < 1.0e-9 && std::abs (arrangement::lengthSeconds (clips, 10.0) - 10.5) < 1.0e-9);
        CHECK (arrangement::clipAt (clips, 4.2) == -1 && arrangement::clipAt (clips, 4.6) == 1 && ! arrangement::canJoinWithPrevious (clips, 1));
        // Volver 0,5 s antes: se puede unir; más allá de 0 no se mueve
        arrangement::moveClip (clips, 1, -0.5, false);
        CHECK (arrangement::canJoinWithPrevious (clips, 1) && arrangement::joinWithPrevious (clips, 1) && clips.size() == 1 && arrangement::isIdentity (clips, 10.0));
        arrangement::moveClip (clips, 0, -3.0, false);
        CHECK (std::abs (clips[0].position) < 1.0e-9);
        // Tres tramos: mover "este y los siguientes", eliminar dejando silencio y cerrando el hueco
        arrangement::cutAt (clips, 2.0, 10.0);
        arrangement::cutAt (clips, 6.0, 10.0);
        arrangement::moveClip (clips, 1, 1.0, true);
        CHECK (clips.size() == 3 && std::abs (clips[1].position - 3.0) < 1.0e-9 && std::abs (clips[2].position - 7.0) < 1.0e-9 && std::abs (arrangement::lengthSeconds (clips, 10.0) - 11.0) < 1.0e-9);
        double from = 0.0, len = 0.0;
        arrangement::removeClip (clips, 1, false, from, len);
        CHECK (clips.size() == 2 && std::abs (clips[1].position - 7.0) < 1.0e-9 && len < 1.0e-9);   // silencio entre 2 y 7
        arrangement::cutAt (clips, 8.0, 10.0);
        arrangement::removeClip (clips, 1, true, from, len);   // el tramo [7, 8) desaparece y lo que sigue se adelanta 1 s
        CHECK (clips.size() == 2 && std::abs (from - 7.0) < 1.0e-9 && std::abs (len - 1.0) < 1.0e-9 && std::abs (clips[1].position - 7.0) < 1.0e-9 && std::abs (clips[1].srcStart - 7.0) < 1.0e-9);
        // Un tramo que se mueve por encima de otro queda "arriba" (el último manda al buscar)
        std::vector<Clip> two { { 0.0, 4.0, 0.0 }, { 4.0, 8.0, 4.0 } };
        arrangement::moveClip (two, 1, -1.0, false);
        CHECK (arrangement::clipAt (two, 3.5) == 1 && std::abs (arrangement::lengthSeconds (two, 8.0) - 7.0) < 1.0e-9);
    }

    std::cout << "[Arrangement] pegar insertando: hueco, copia y grilla\n";
    {
        std::vector<Clip> clips { { 0.0, 10.0, 0.0 } };
        // Insertar un hueco de 2 s en 4 s: el tramo se parte y la segunda mitad se corre
        arrangement::insertGap (clips, 4.0, 2.0);
        CHECK (clips.size() == 2 && std::abs (clips[0].srcEnd - 4.0) < 1.0e-9 && std::abs (clips[1].position - 6.0) < 1.0e-9 && std::abs (clips[1].srcStart - 4.0) < 1.0e-9);
        CHECK (arrangement::clipAt (clips, 5.0) == -1 && std::abs (arrangement::lengthSeconds (clips, 10.0) - 12.0) < 1.0e-9);
        // Pegar insertando el primer tramo (0-4 del original) en 6 s: lo que sigue se corre 4 s más
        const int pasted = arrangement::pasteClip (clips, clips[0], 6.0, true);
        CHECK (pasted == 1 && clips.size() == 3 && std::abs (clips[1].position - 6.0) < 1.0e-9 && std::abs (clips[1].srcEnd - 4.0) < 1.0e-9
               && std::abs (clips[2].position - 10.0) < 1.0e-9 && std::abs (arrangement::lengthSeconds (clips, 10.0) - 16.0) < 1.0e-9);
        // Pegar encima: nada se mueve, el nuevo queda "arriba"
        const int over = arrangement::pasteClip (clips, clips[0], 1.0, false);
        CHECK (over >= 0 && clips.size() == 4 && arrangement::clipAt (clips, 2.0) == over && std::abs (clips[2].position - 6.0) < 1.0e-9);
        CHECK (arrangement::pasteClip (clips, { 3.0, 3.0, 0.0 }, 1.0, true) == -1);   // tramo vacío: nada
        // Grilla: copiar un rango y pegarlo tras abrir el hueco; un acorde que atraviesa el hueco se parte
        SongInfo si;
        for (int i = 0; i < 20; ++i) si.analysis.beats.push_back ({ i * 0.5, i % 4 + 1 });   // 0 .. 9,5
        si.analysis.chords = { { 0.0, 3.0, "C" }, { 3.0, 10.0, "G" } };
        const auto slice = arrangement::copyGrid (si.analysis, 1.0, 3.0);
        CHECK (slice.beats.size() == 4 && std::abs (slice.beats[0].seconds) < 1.0e-9 && slice.beats[0].beatInBar == 3
               && slice.chords.size() == 1 && slice.chords[0].name == "C" && std::abs (slice.chords[0].end - 2.0) < 1.0e-9);
        arrangement::shiftGrid (si, 5.0, 2.0);   // hueco de 2 s en 5 s: G (3-10) se parte en 3-5 y 7-12
        CHECK (si.analysis.chords.size() == 3 && std::abs (si.analysis.chords[1].end - 5.0) < 1.0e-9 && std::abs (si.analysis.chords[2].start - 7.0) < 1.0e-9
               && std::abs (si.analysis.chords[2].end - 12.0) < 1.0e-9 && si.analysis.chords[2].name == "G");
        CHECK (si.analysis.beats.size() == 20 && std::abs (si.analysis.beats[10].seconds - 7.0) < 1.0e-9 && std::abs (si.analysis.beats[9].seconds - 4.5) < 1.0e-9);
        arrangement::pasteGrid (si.analysis, slice, 5.0);
        CHECK (si.analysis.beats.size() == 24 && std::abs (si.analysis.beats[10].seconds - 5.0) < 1.0e-9 && si.analysis.beats[10].beatInBar == 3
               && std::abs (si.analysis.beats[13].seconds - 6.5) < 1.0e-9 && std::abs (si.analysis.beats[14].seconds - 7.0) < 1.0e-9);
        CHECK (si.analysis.chords.size() == 4 && std::abs (si.analysis.chords[2].start - 5.0) < 1.0e-9 && si.analysis.chords[2].name == "C" && si.analysis.chordAt (6.0) == 2 && si.analysis.chordAt (8.0) == 3);
    }

    std::cout << "[Arrangement] render: huecos en silencio, tramos desplazados, fundidos, identidad\n";
    {
        const double rate = 48000.0;
        auto srcSong = std::make_shared<LoadedSong>();
        srcSong->sampleRate = rate;
        srcSong->length = (juce::int64) (2.0 * rate);
        auto tr1 = std::make_unique<LoadedTrack>();
        tr1->name = "A";
        tr1->buffer.setSize (2, (int) srcSong->length);
        for (int i = 0; i < (int) srcSong->length; ++i)
            for (int ch = 0; ch < 2; ++ch)
                tr1->buffer.setSample (ch, i, i < (int) rate ? 0.5f : -0.25f);   // primer segundo 0,5; segundo -0,25
        srcSong->tracks.push_back (std::move (tr1));
        std::vector<Clip> clips;
        CHECK (arrangement::render (srcSong, clips, rate) == srcSong);   // identidad: mismo puntero
        clips = { { 0.0, 1.0, 0.0 }, { 1.0, 2.0, 1.5 } };        // el segundo tramo, 0,5 s más tarde
        auto out = arrangement::render (srcSong, clips, rate);
        CHECK (out != nullptr && out != srcSong && out->tracks.size() == 1 && out->length == (juce::int64) (2.5 * rate));
        const auto& b = out->tracks[0]->buffer;
        CHECK (std::abs (b.getSample (0, (int) (0.5 * rate)) - 0.5f) < 1.0e-6f);            // dentro del primer tramo
        CHECK (std::abs (b.getSample (0, (int) (1.25 * rate))) < 1.0e-6f);                  // hueco en silencio
        CHECK (std::abs (b.getSample (1, (int) (2.0 * rate)) + 0.25f) < 1.0e-6f);           // segundo tramo desplazado
        CHECK (std::abs (b.getSample (0, (int) (1.5 * rate))) < 0.1f && std::abs (b.getSample (0, (int) (2.5 * rate) - 1)) < 0.1f);   // fundidos en los bordes
        CHECK (out->tracks[0]->waveform.numBins() > 0);
        CHECK (arrangement::render (srcSong, clips, rate, [] { return true; }) == nullptr);   // abortado
        // Solapado de 0,1 s: se suman (crossfade con los fundidos)
        clips = { { 0.0, 1.0, 0.0 }, { 0.9, 2.0, 0.9 } };
        out = arrangement::render (srcSong, clips, rate);
        CHECK (out != nullptr && out->length == (juce::int64) (2.0 * rate) && std::abs (out->tracks[0]->buffer.getSample (0, (int) (0.95 * rate)) - 1.0f) < 0.02f);
    }

    std::cout << "[Arrangement] transientes: ataque mas cercano, inicio del golpe\n";
    {
        const double rate = 48000.0;
        juce::AudioBuffer<float> b (2, (int) (3.0 * rate));
        b.clear();
        juce::Random rnd (7);
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, rnd.nextFloat() * 0.002f - 0.001f);   // piso de ruido
        auto hit = [&] (double at, float level, double decay)
        {
            const int s0 = (int) (at * rate);
            for (int i = 0; i < (int) (decay * rate) && s0 + i < b.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.addSample (ch, s0 + i, level * (float) std::exp (-i / (decay * rate * 0.3)) * (rnd.nextFloat() * 2.0f - 1.0f));
        };
        hit (1.000, 0.8f, 0.15);   // bombo fuerte
        hit (1.300, 0.3f, 0.05);   // golpe débil
        hit (2.000, 0.8f, 0.15);
        double strength = 0.0;
        const double o1 = arrangement::findOnset (b, rate, 1.02, 0.2, &strength);
        CHECK (o1 >= 0.990 && o1 <= 1.001 && strength > 20.0);           // justo antes del golpe de 1,0 s
        const double o2 = arrangement::findOnset (b, rate, 1.31, 0.2);
        CHECK (o2 >= 1.290 && o2 <= 1.301);                               // el débil, por cercanía, gana al fuerte de 1,0
        const double o3 = arrangement::findOnset (b, rate, 1.16, 0.2);
        CHECK (std::abs (o3 - 1.0) < 0.012 || std::abs (o3 - 1.3) < 0.012);   // entre los dos, alguno
        CHECK (arrangement::findOnset (b, rate, 2.6, 0.2) < 0.0);         // solo ruido: sin transiente
        CHECK (arrangement::findOnset (b, rate, 0.05, 0.02) < 0.0);        // ventana demasiado corta
        juce::AudioBuffer<float> empty;
        CHECK (arrangement::findOnset (empty, rate, 1.0, 0.2) < 0.0);
    }

    std::cout << "[Arrangement] cerrar un hueco desplaza la grilla\n";
    {
        SongInfo si;
        for (int i = 0; i < 20; ++i) si.analysis.beats.push_back ({ i * 0.5, i % 4 + 1 });          // 0 .. 9,5
        si.analysis.chords = { { 0.0, 2.0, "C" }, { 2.0, 5.0, "F" }, { 5.0, 10.0, "G" } };
        si.markers = { mk ("A", 1.0), mk ("B", 3.5), mk ("C", 8.0) };
        si.tempoRegions = { { 0.0, 120.0, 0.0 }, { 3.0, 130.0, 0.0 }, { 6.0, 140.0, 0.0 } };
        si.notes = { { 2.0, 6.0, "antes" }, { 4.0, 6.0, "dentro" }, { 7.0, 6.0, "despues" } };
        si.clickOffset = 4.0;
        arrangement::shiftGrid (si, 3.0, -2.0);   // desaparece [3, 5): lo de después se adelanta 2 s
        CHECK (si.notes.size() == 2 && si.notes[0].text == "antes" && std::abs (si.notes[1].seconds - 5.0) < 1.0e-9 && si.notes[1].text == "despues");
        CHECK (si.analysis.beats.size() == 16 && std::abs (si.analysis.beats[6].seconds - 3.0) < 1.0e-9 && std::abs (si.analysis.beats[15].seconds - 7.5) < 1.0e-9);
        CHECK (si.analysis.chords.size() == 3 && std::abs (si.analysis.chords[1].end - 3.0) < 1.0e-9 && std::abs (si.analysis.chords[2].start - 3.0) < 1.0e-9 && std::abs (si.analysis.chords[2].end - 8.0) < 1.0e-9);
        CHECK (si.markers.size() == 2 && si.markers[1].name == "C" && std::abs (si.markers[1].seconds - 6.0) < 1.0e-9);
        CHECK (si.tempoRegions.size() == 2 && std::abs (si.tempoRegions[1].start - 4.0) < 1.0e-9 && std::abs (si.tempoRegions[1].origBpm - 140.0) < 1.0e-9);
        CHECK (std::abs (si.clickOffset - 3.0) < 1.0e-9);
        // Un acorde entero dentro del hueco desaparece; abrir un hueco desplaza sin borrar
        SongInfo s2;
        s2.analysis.chords = { { 0.0, 1.0, "C" }, { 1.0, 2.0, "F" }, { 2.0, 3.0, "G" } };
        arrangement::shiftGrid (s2, 1.0, -1.0);
        CHECK (s2.analysis.chords.size() == 2 && s2.analysis.chords[1].name == "G" && std::abs (s2.analysis.chords[1].start - 1.0) < 1.0e-9);
        arrangement::shiftGrid (s2, 1.0, 0.5);
        CHECK (std::abs (s2.analysis.chords[1].start - 1.5) < 1.0e-9 && std::abs (s2.analysis.chords[0].end - 1.0) < 1.0e-9);
    }

    std::cout << "[Triggers] deteccion de golpes, bancos, cortes y ajustes guardados\n";
    {
        const double rate = 48000.0;
        juce::Random rnd (3);
        // Golpes de 60 ms (ruido con caída) en 0.5, 1.0, 1.5 y 2.0 s con amplitudes 0.9, 0.5, 0.25, 0.1 y piso de ruido
        juce::AudioBuffer<float> b (2, (int) (3.0 * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, rnd.nextFloat() * 0.0006f - 0.0003f);
        const double at[] = { 0.5, 1.0, 1.5, 2.0 };
        const float amp[] = { 0.9f, 0.5f, 0.25f, 0.1f };
        for (int h = 0; h < 4; ++h)
            for (int i = 0; i < (int) (0.06 * rate); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.addSample (ch, (int) (at[h] * rate) + i, amp[h] * (float) std::exp (-i / (0.015 * rate)) * (rnd.nextFloat() * 2.0f - 1.0f));
        const auto ev = triggers::detect (b, rate, -40.0, 1.0, 40.0);
        CHECK (ev.size() == 4);
        if (ev.size() == 4)
        {
            for (int h = 0; h < 4; ++h)
                CHECK (std::abs ((double) ev[(size_t) h].sample / rate - at[h]) < 0.004);   // a menos de 4 ms del ataque
            CHECK (ev[0].velocity > ev[1].velocity && ev[1].velocity > ev[2].velocity && ev[2].velocity > ev[3].velocity && ev[0].velocity > 0.8f && ev[3].velocity > 0.05f);
        }
        CHECK (triggers::detect (b, rate, -15.0, 1.0, 40.0).size() == 2);   // la envolvente es RMS: 0.25 de ruido queda en -18 dB, bajo el umbral
        const auto sens = triggers::detect (b, rate, -40.0, 2.0, 40.0);       // más sensible: velocidades más altas para los suaves
        CHECK (sens.size() == 4 && sens[3].velocity > ev[3].velocity);
        CHECK (triggers::detect (b, rate, -40.0, 1.0, 800.0).size() == 2);   // tiempo mínimo de 0.8 s: quedan los de 0.5 y 1.5 s
        CHECK (triggers::detect (juce::AudioBuffer<float>(), rate, -40.0, 1.0, 40.0).empty());

        // Cortar una grabación de golpes en muestras: 4 golpes, cada uno hasta el siguiente
        const auto hits = triggers::sliceHits (b, rate, -40.0, 60.0);
        CHECK (hits.size() == 4 && hits[0].getNumSamples() > (int) (0.4 * rate) && hits[0].getNumSamples() <= (int) (0.5 * rate)
               && hits[0].getMagnitude (0, 0, hits[0].getNumSamples()) > 0.5f);

        // Banco: guardar los golpes como wav, cargarlo (recorte al ataque, orden por pico) y elegir por velocidad
        auto bankDir = tmp.getChildFile ("_bancos").getChildFile ("BomboPrueba");
        bankDir.createDirectory();
        for (size_t i = 0; i < hits.size(); ++i)
            CHECK (triggers::writeWav (hits[i], rate, bankDir.getChildFile ("golpe" + juce::String ((int) i + 1) + ".wav")));
        auto bank = triggers::loadBank (bankDir, rate, formats);
        CHECK (bank != nullptr && bank->hits.size() == 4 && bank->hits[0].peak < bank->hits[3].peak && bank->hits[3].peak > 0.5f);
        CHECK (bank != nullptr && bank->hits[3].buffer.getNumSamples() > (int) (0.01 * rate) && bank->hits[3].buffer.getMagnitude (0, 0, (int) (0.006 * rate)) > 0.2f);   // el ataque quedó en los primeros ms
        CHECK (bank != nullptr && bank->preRoll == (int) (0.002 * rate));   // 2 ms antes del ataque en todas las muestras
        int last = -1;
        CHECK (bank != nullptr && bank->pick (1.0f, last) == 3 && bank->pick (1.0f, last) == 2 && bank->pick (0.0f, last) == 0 && bank->pick (0.0f, last) == 1);
        SampleBankData empty;
        CHECK (empty.pick (0.5f, last) == -1);
        CHECK (triggers::loadBank (tmp.getChildFile ("no-existe"), rate, formats) == nullptr);
        // La carpeta _bancos no es una canción y aparece en la lista de bancos
        Library lb (tmp);
        lb.load();
        bool asSong = false;
        for (auto& sng : lb.songs) asSong = asSong || sng.folder.getFileName() == "_bancos";
        CHECK (! asSong && lb.listBanks().contains ("BomboPrueba"));

        // Ajustes de trigger y songTime: ida y vuelta por song.json
        SongInfo si;
        si.folder = tmp.getChildFile ("trig");
        si.folder.createDirectory();
        writeSine (si.folder.getChildFile ("drums.wav"), 44100.0, 0.2, 1);
        si.stems.push_back (stem ("Bateria", "drums.wav"));
        si.stems[0].songTime = true;
        si.stems[0].trigger.enabled = true;
        si.stems[0].trigger.keepAudio = true;
        si.stems[0].trigger.note = 38;
        si.stems[0].trigger.sound = "banco:BomboPrueba";
        si.stems[0].trigger.thresholdDb = -24.0;
        si.stems[0].trigger.sensitivity = 1.5;
        si.stems[0].trigger.minMs = 55.0;
        si.stems[0].trigger.gainDb = -3.0f;
        si.stems[0].trigger.outputPair = 1;
        CHECK (lb.saveSong (si));
        Library lb2 (tmp);
        lb2.load();
        const SongInfo* back = nullptr;
        for (auto& sng : lb2.songs) if (sng.folder == si.folder) back = &sng;
        CHECK (back != nullptr && back->stems.size() == 1 && back->stems[0].songTime && back->stems[0].trigger.enabled
               && back->stems[0].trigger.sound == "banco:BomboPrueba" && std::abs (back->stems[0].trigger.thresholdDb + 24.0) < 1.0e-9
               && std::abs (back->stems[0].trigger.sensitivity - 1.5) < 1.0e-9 && std::abs (back->stems[0].trigger.minMs - 55.0) < 1.0e-9
               && std::abs (back->stems[0].trigger.gainDb + 3.0f) < 1.0e-6f && back->stems[0].trigger.outputPair == 1
               && back->stems[0].trigger.keepAudio && back->stems[0].trigger.note == 38);
    }

    std::cout << "[Separator] bateria en partes: plan, nombres de las partes\n";
    {
        SeparationOptions o;
        o.drumParts = true;
        const auto plan = Separator::planFor (o);
        CHECK (plan.size() == 2 && plan.back().tool == "drumsep" && plan.back().model.contains ("DrumSep"));
        CHECK (Separator::describe (o).contains ("DrumSep") && Separator::describe (o).contains (tr ("6 partes")));
        o.drumParts = false;
        CHECK (Separator::planFor (o).size() == 1);
        CHECK (Separator::drumPartFileName ("drums_(Kick)_MDX23C-DrumSep-aufr33-jarredou.wav") == "drums_kick.wav");
        CHECK (Separator::drumPartFileName ("input_(HH)_MDX23C-DrumSep-aufr33-jarredou.wav") == "drums_hh.wav");
        CHECK (Separator::drumPartFileName ("input_(crash)_x.wav") == "drums_crash.wav" && Separator::drumPartFileName ("input_(vocals)_x.wav").isEmpty());
        // Los archivos de las partes se traducen al cargar la canción
        auto folder = tmp.getChildFile ("partes");
        folder.createDirectory();
        for (auto* n : { "drums_kick", "drums_snare", "drums_toms", "drums_hh", "drums_ride", "drums_crash" })
            writeSine (folder.getChildFile (juce::String (n) + ".wav"), 44100.0, 0.1, 1);
        Library lp (tmp);
        lp.load();
        const SongInfo* partes = nullptr;
        for (auto& sng : lp.songs) if (sng.folder == folder) partes = &sng;
        juce::StringArray names;
        if (partes != nullptr) for (auto& st : partes->stems) names.add (st.name);
        CHECK (partes != nullptr && names.contains ("Bombo") && names.contains ("Caja") && names.contains ("Toms") && names.contains ("Hi-hat") && names.contains ("Ride") && names.contains ("Crash"));
    }

    std::cout << "[Separator] progreso de tqdm y modelos por bolsa\n";
    CHECK (Separator::parsePercent (" 43%|#####     | 3/7 [00:05<00:07,  1.85seconds/s]") == 43);
    CHECK (Separator::parsePercent ("100%|##########| 7/7 [00:12<00:00,  1.85seconds/s]") == 100);
    CHECK (Separator::parsePercent ("Selected model is a bag of 4 models. You will see that many progress bars per track.") == -1);
    CHECK (Separator::parsePercent ("") == -1);
    CHECK (Separator::modelsInBag ("htdemucs_ft") == 4);
    CHECK (Separator::modelsInBag ("htdemucs") == 1);

    std::cout << "[Separator] plan de etapas segun pistas y calidad\n";
    {
        SeparationOptions o;                       // 4 pistas, normal
        auto plan = Separator::planFor (o);
        CHECK (plan.size() == 1 && plan[0].model == "htdemucs" && plan[0].passes == 1 && plan[0].shifts == 1);
        o.stems = 6;
        plan = Separator::planFor (o);
        CHECK (plan.size() == 1 && plan[0].model == "htdemucs_6s");
        o.quality = 1;                             // alta: híbrido afinado + 6 pistas
        plan = Separator::planFor (o);
        CHECK (plan.size() == 2 && plan[0].model == "htdemucs_ft" && plan[0].passes == 4 && plan[1].model == "htdemucs_6s");
        o.quality = 2;                             // máxima: 3 pasadas promediadas y más solapamiento
        plan = Separator::planFor (o);
        CHECK (plan.size() == 2 && plan[0].shifts == 3 && plan[0].passes == 12 && plan[0].overlap > 0.4 && plan[1].passes == 3);
        o.roformerVocals = true;
        plan = Separator::planFor (o);
        CHECK (plan.size() == 3 && plan[0].tool == "roformer" && plan[1].tool == "demucs");
        CHECK (Separator::relativeCost (SeparationOptions {}) < Separator::relativeCost (o));
        CHECK (Separator::describe (o).contains ("htdemucs_ft") && Separator::describe (o).contains ("BS-Roformer"));
        CHECK (! Separator::isRoformerAvailable ("/no/existe/python"));
        CHECK (! Separator::isRoformerAvailable ("python3"));   // ruta relativa: nunca
    }

    std::cout << "[Separator] conversion a WAV 44.1k y residuo de la mezcla\n";
    {
        auto dir = tmp.getChildFile ("sep");
        dir.createDirectory();
        juce::String err;
        writeSine (dir.getChildFile ("src48.wav"), 48000.0, 2.0, 2);
        CHECK (Separator::convertToWav (dir.getChildFile ("src48.wav"), dir.getChildFile ("mix.wav"), 44100.0, err));
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (dir.getChildFile ("mix.wav")));
            CHECK (r != nullptr && std::abs (r->sampleRate - 44100.0) < 1.0 && r->numChannels == 2);
            CHECK (r != nullptr && std::abs ((double) r->lengthInSamples - 88200.0) <= 2.0);
            CHECK (r != nullptr && r->bitsPerSample == 24);
        }
        CHECK (! Separator::convertToWav (dir.getChildFile ("no_existe.mp3"), dir.getChildFile ("x.wav"), 44100.0, err));

        // mix = a + b + c; con stems a y b, el residuo debe ser c
        const int n = 44100;
        juce::AudioBuffer<float> a (2, n), b (2, n), c (2, n), mix (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                a.setSample (ch, i, 0.30f * std::sin ((float) i * 0.010f));
                b.setSample (ch, i, 0.20f * std::sin ((float) i * 0.037f));
                c.setSample (ch, i, 0.25f * std::sin ((float) i * 0.101f));
                mix.setSample (ch, i, a.getSample (ch, i) + b.getSample (ch, i) + c.getSample (ch, i));
            }
        writeWav (dir.getChildFile ("mix2.wav"), mix, 44100.0);
        writeWav (dir.getChildFile ("a.wav"), a, 44100.0);
        writeWav (dir.getChildFile ("b.wav"), b, 44100.0);
        CHECK (Separator::writeResidual (dir.getChildFile ("mix2.wav"), { dir.getChildFile ("a.wav"), dir.getChildFile ("b.wav") },
                                         dir.getChildFile ("other.wav"), err));
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (dir.getChildFile ("other.wav")));
            CHECK (r != nullptr && r->lengthInSamples == n);
            juce::AudioBuffer<float> got (2, n);
            if (r != nullptr) r->read (&got, 0, n, 0, true, true);
            float maxErr = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    maxErr = std::max (maxErr, std::abs (got.getSample (ch, i) - c.getSample (ch, i)));
            CHECK (maxErr < 1.0e-3f);   // solo el ruido de cuantización de 24 bits
        }
        // Un stem con otra frecuencia se rechaza
        CHECK (! Separator::writeResidual (dir.getChildFile ("mix2.wav"), { dir.getChildFile ("src48.wav") },
                                           dir.getChildFile ("other2.wav"), err));
    }

    std::cout << "[AudioEngine] carga y remuestreo\n";
    auto song = AudioEngine::loadSong (info, sr, formats, {});
    CHECK (song != nullptr);
    CHECK (song->tracks.size() == 2);
    CHECK (song->length == (juce::int64) (3.0 * sr));
    for (auto& t : song->tracks)
        CHECK (t->buffer.getNumSamples() == (int) song->length);   // todas igual de largas

    AudioEngine engine;
    engine.setSong (song);
    FakeOutputs out (4);

    std::cout << "[AudioEngine] reproduce hasta el final y se detiene\n";
    song->tracks[1]->outputPair = 0;
    engine.play();
    int blocks = 0;
    while (engine.isPlaying() && blocks < 1000) { render (engine, out, 1); ++blocks; }
    CHECK (! engine.isPlaying());
    CHECK (std::abs (blocks - (int) (3.0 * sr / block)) <= 2);

    std::cout << "[AudioEngine] stop vuelve al inicio\n";
    engine.stop();
    render (engine, out, 1);
    CHECK (engine.getPositionSeconds() < 0.001);

    std::cout << "[AudioEngine] loop mantiene la posición dentro de la región\n";
    engine.seekSeconds (0.5);
    engine.setLoop (0.5, 1.0);
    engine.play();
    for (int b = 0; b < 400; ++b)
    {
        render (engine, out, 1);
        const double p = engine.getPositionSeconds();
        CHECK (p >= 0.49 && p <= 1.001);
    }
    CHECK (engine.isPlaying());
    engine.clearLoop();

    std::cout << "[AudioEngine] ruteo por salidas, mute y solo\n";
    engine.seekSeconds (0.0);
    song->tracks[0]->outputPair = 0;
    song->tracks[1]->outputPair = 1;
    render (engine, out, 20);   // pasa el fundido del salto
    {
        float peaks[4] = {};
        render (engine, out, 20, peaks);
        CHECK (peaks[0] > 0.1f && peaks[2] > 0.1f);
    }
    song->tracks[0]->muted = true;
    render (engine, out, 2);    // rampa de ganancia
    {
        float peaks[4] = {};
        render (engine, out, 10, peaks);
        CHECK (peaks[0] < 1.0e-4f);
        CHECK (peaks[2] > 0.1f);
    }
    song->tracks[0]->muted = false;
    song->tracks[0]->solo = true;
    render (engine, out, 2);
    {
        float peaks[4] = {};
        render (engine, out, 10, peaks);
        CHECK (peaks[0] > 0.1f);
        CHECK (peaks[2] < 1.0e-4f);
    }
    song->tracks[0]->solo = false;

    std::cout << "[AudioEngine] un par de salida inválido cae a 1-2 sin salirse de rango\n";
    {
        int l = 9, r = 9;
        AudioEngine::outputPairChannels (-1, 4, l, r);
        CHECK (l == 0 && r == 1);
        AudioEngine::outputPairChannels (7, 4, l, r);
        CHECK (l == 0 && r == 1);
        AudioEngine::outputPairChannels (1, 4, l, r);
        CHECK (l == 2 && r == 3);
        AudioEngine::outputPairChannels (0, 1, l, r);
        CHECK (l == 0 && r == 0);

        song->tracks[1]->muted = true;
        song->tracks[0]->outputPair = -1;
        render (engine, out, 2);
        float peaks[4] = {};
        render (engine, out, 10, peaks);
        CHECK (peaks[0] > 0.1f && peaks[2] < 1.0e-4f);
        song->tracks[0]->outputPair = 0;
        song->tracks[1]->muted = false;
    }

    std::cout << "[AudioEngine] click en su propia salida\n";
    song->tracks[0]->muted = true;
    song->tracks[1]->muted = true;
    engine.setClick (true, 120.0, 0.0, 0.0f, 1);
    engine.seekSeconds (0.0);
    render (engine, out, 4);
    {
        float peaks[4] = {};
        render (engine, out, 100, peaks);
        CHECK (peaks[2] > 0.5f);   // click en salida 3-4
        CHECK (peaks[0] < 1.0e-4f);
    }

    std::cout << "[Recorder] alineación de la toma, compensación y banco desde una grabación\n";
    {
        juce::AudioBuffer<float> raw (1, 1000);
        for (int i = 0; i < 1000; ++i)
            raw.setSample (0, i, 0.5f);
        const auto rawF = tmp.getChildFile ("toma-cruda.wav");
        const auto outF = tmp.getChildFile ("toma-alineada.wav");
        CHECK (triggers::writeWav (raw, sr, rawF));
        CHECK (recorder::writeAligned (rawF, 500, outF, formats));
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (outF));
            CHECK (r != nullptr && r->lengthInSamples == 1500);
            juce::AudioBuffer<float> b (1, 1500);
            r->read (&b, 0, 1500, 0, true, false);
            CHECK (std::abs (b.getSample (0, 499)) < 1.0e-6f && std::abs (b.getSample (0, 500) - 0.5f) < 1.0e-3f && std::abs (b.getSample (0, 1499) - 0.5f) < 1.0e-3f);
        }
        CHECK (recorder::writeAligned (rawF, -200, outF, formats));
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (outF));
            CHECK (r != nullptr && r->lengthInSamples == 800);
        }
        CHECK (recorder::compensation (128, 256, 10.0, 48000.0) == 128 + 256 + 480);
        CHECK (recorder::compensation (0, 0, -5.0, 48000.0) == -240);

        // Grabación de tres golpes sueltos (0,2, 0,7 y 1,2 s; 50 ms cada uno; de suave a fuerte) -> banco de 3 muestras
        juce::AudioBuffer<float> rec (1, (int) (2.0 * sr));
        rec.clear();
        const float amps[3] = { 0.3f, 0.5f, 0.8f };
        const double starts[3] = { 0.2, 0.7, 1.2 };
        for (int h = 0; h < 3; ++h)
            for (int i = 0; i < (int) (0.05 * sr); ++i)
                rec.setSample (0, (int) (starts[h] * sr) + i, amps[h] * (1.0f - (float) i / (float) (0.05 * sr)) * std::sin ((float) i * 0.3f));
        const auto bankDir = tmp.getChildFile ("_bancos").getChildFile ("Grabado");
        CHECK (recorder::saveBank (rec, sr, bankDir) == 3);
        CHECK (Library::audioFilesIn (bankDir).size() == 3);
        auto bank = triggers::loadBank (bankDir, sr, formats);
        CHECK (bank != nullptr && bank->hits.size() == 3 && bank->hits[0].peak < bank->hits[1].peak && bank->hits[1].peak < bank->hits[2].peak);
    }

    std::cout << "[Arrangement] una pista en tiempo de canción (grabación) no pasa por los tramos\n";
    {
        auto srcSong = std::make_shared<LoadedSong>();
        srcSong->sampleRate = sr;
        srcSong->length = (juce::int64) sr;
        for (int k = 0; k < 2; ++k)
        {
            auto t = std::make_unique<LoadedTrack>();
            t->name = k == 0 ? "audio" : "grabacion";
            t->stemIndex = k;
            t->songTime = k == 1;
            t->buffer.setSize (2, (int) sr);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < (int) sr; ++i)
                    t->buffer.setSample (ch, i, (float) i / (float) sr);
            srcSong->tracks.push_back (std::move (t));
        }
        std::vector<Clip> swapped { { 0.5, 1.0, 0.0 }, { 0.0, 0.5, 0.5 } };   // las dos mitades intercambiadas
        auto out2 = arrangement::render (srcSong, swapped, sr);
        CHECK (out2 != nullptr && out2->tracks.size() == 2);
        const int i0 = 2000;   // lejos de los fundidos de 5 ms
        CHECK (std::abs (out2->tracks[0]->buffer.getSample (0, i0) - (float) ((int) (0.5 * sr) + i0) / (float) sr) < 1.0e-4f);   // reordenada
        CHECK (std::abs (out2->tracks[1]->buffer.getSample (0, i0) - (float) i0 / (float) sr) < 1.0e-4f);                        // tal cual
        CHECK (out2->tracks[1]->songTime);
    }

    std::cout << "[AudioEngine] sampler de triggers: dispara muestras en las posiciones exactas\n";
    {
        engine.setClick (false, 120.0, 0.0, 0.0f, 0);
        song->tracks[0]->muted = true;
        song->tracks[1]->muted = true;
        // Banco con un solo golpe: 100 ms de nivel constante 0.5
        auto bank = std::make_shared<SampleBankData>();
        bank->sampleRate = sr;
        SampleBankData::Hit hit;
        hit.buffer.setSize (2, (int) (0.1 * sr));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < hit.buffer.getNumSamples(); ++i)
                hit.buffer.setSample (ch, i, 0.5f);
        hit.peak = 0.5f;
        bank->hits.push_back (std::move (hit));
        auto set = std::make_shared<SamplerSet>();
        auto lane = std::make_unique<SamplerLane>();
        lane->name = "prueba";
        lane->bank = bank;
        lane->events = { { (juce::int64) (0.25 * sr), 1.0f }, { (juce::int64) (0.6 * sr), 0.0f } };   // fuerte y suave
        lane->control.gain = 1.0f;
        lane->control.outputPair = 0;
        set->lanes.push_back (std::move (lane));
        engine.setSamplers (set);
        engine.seekSeconds (0.0);
        engine.play();
        render (engine, out, 20);   // fundido de entrada
        float before = 0.0f, strong = 0.0f, between = 0.0f, soft = 0.0f;
        for (int b = 0; b < 200 && engine.isPlaying(); ++b)
        {
            render (engine, out, 1);
            const double t = engine.getPositionSeconds() - block / sr;
            const float pk = out.peak (0);
            if (t > 0.12 && t + block / sr < 0.24) before = std::max (before, pk);
            if (t > 0.26 && t + block / sr < 0.34) strong = std::max (strong, pk);
            if (t > 0.40 && t + block / sr < 0.58) between = std::max (between, pk);
            if (t > 0.61 && t + block / sr < 0.69) soft = std::max (soft, pk);
        }
        CHECK (before < 1.0e-4f && between < 1.0e-4f);                 // silencio fuera de los golpes
        CHECK (std::abs (strong - 0.5f) < 0.02f);                       // velocidad 1 -> ganancia 1 -> 0.5
        CHECK (std::abs (soft - 0.25f) < 0.02f);                        // velocidad 0 -> ganancia 0.5 -> 0.25
        CHECK (set->lanes[0]->control.peakL.load() > 0.4f);             // medidores de la línea
        // Pre-roll del banco: el motor adelanta las voces esas muestras, así el ataque de la muestra cae
        // exacto en el golpe. Con 10 ms de pre-roll, la voz del golpe de 0,25 s empieza a sonar en 0,24 s.
        bank->preRoll = (int) (0.01 * sr);
        engine.seekSeconds (0.0);
        render (engine, out, 10);   // fundido del salto: queda en ~0,12 s, antes del golpe
        double firstSound = -1.0;
        for (int b = 0; b < 60 && firstSound < 0.0 && engine.isPlaying(); ++b)
        {
            render (engine, out, 1);
            const double t0 = engine.getPositionSeconds() - block / sr;
            for (int k = 0; k < block; ++k)
                if (std::abs (out.data[0][(size_t) k]) > 0.4f)
                {
                    firstSound = t0 + k / sr;
                    break;
                }
        }
        CHECK (std::abs (firstSound - 0.24) < 0.002);
        if (std::abs (firstSound - 0.24) >= 0.002)
            std::cout << "  primer sonido en " << firstSound << " s\n";
        bank->preRoll = 0;
        // Mute de la línea: silencio; solo de la línea silencia las pistas
        set->lanes[0]->control.muted = true;
        engine.seekSeconds (0.2);
        render (engine, out, 20);
        float muted = 0.0f;
        for (int b = 0; b < 12; ++b) { render (engine, out, 1); muted = std::max (muted, out.peak (0)); }
        CHECK (muted < 1.0e-3f);
        // Pista reemplazada por su trigger: no suena aunque no esté muteada; al quitar el reemplazo vuelve
        song->tracks[0]->muted = false;
        song->tracks[0]->replaced = true;
        engine.seekSeconds (0.2);
        render (engine, out, 20);
        float replacedPeak = 0.0f, restored = 0.0f;
        for (int b = 0; b < 12; ++b) { render (engine, out, 1); replacedPeak = std::max (replacedPeak, out.peak (0)); }
        CHECK (replacedPeak < 1.0e-3f);
        song->tracks[0]->replaced = false;
        render (engine, out, 20);
        for (int b = 0; b < 12; ++b) { render (engine, out, 1); restored = std::max (restored, out.peak (0)); }
        CHECK (restored > 0.01f);
        song->tracks[0]->muted = true;
        set->lanes[0]->control.muted = false;
        engine.setSamplers (nullptr);
        engine.pause();
        song->tracks[0]->muted = false;
        song->tracks[1]->muted = false;
        engine.seekSeconds (0.0);
        render (engine, out, 4);
    }

#if defined (SECUENCIAS_TEST_SYNTH)
    std::cout << "[Instrumentos] carga un VST3 (sintetizador de prueba) y el motor le manda los golpes como MIDI\n";
    {
        juce::VST3PluginFormat vst3;
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, SECUENCIAS_TEST_SYNTH);
        CHECK (found.size() == 1);
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::String error;
        if (found.size() == 1)
            plugin = vst3.createInstanceFromDescription (*found[0], sr, block, error);
        CHECK (plugin != nullptr);
        if (plugin != nullptr)
        {
            CHECK (plugin->acceptsMidi() && found[0]->isInstrument);
            plugin->prepareToPlay (sr, AudioEngine::maxBlockSize);
            // Directo: una nota en la muestra 10 -> nivel = velocidad desde ahí
            juce::AudioBuffer<float> direct (2, block);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, 0.5f), 10);
            direct.clear();
            plugin->processBlock (direct, midi);
            CHECK (std::abs (direct.getSample (0, 9)) < 1.0e-6f && std::abs (direct.getSample (0, 10) - 0.5f) < 0.02f);

            // En el motor: línea de instrumento + línea del sampler que le manda golpes (0,25 s fuerte, 0,6 s suave)
            auto iset = std::make_shared<InstrumentSet>();
            auto ilane = std::make_shared<InstrumentLane>();
            ilane->id = 7;
            ilane->name = "prueba";
            ilane->control.gain = 1.0f;
            ilane->control.outputPair = 0;
            ilane->latency = plugin->getLatencySamples();
            ilane->holdSamples = (int) (0.05 * sr);
            ilane->work.setSize (2, AudioEngine::maxBlockSize);
            ilane->midi.ensureSize (4096);
            ilane->plugin = std::move (plugin);
            iset->lanes.push_back (ilane);
            engine.setInstruments (iset);
            auto set = std::make_shared<SamplerSet>();
            auto lane = std::make_unique<SamplerLane>();
            lane->instrumentId = 7;
            lane->note = 38;
            lane->events = { { (juce::int64) (0.25 * sr), 1.0f }, { (juce::int64) (0.6 * sr), 0.4f } };
            set->lanes.push_back (std::move (lane));
            engine.setSamplers (set);
            song->tracks[0]->muted = true;
            song->tracks[1]->muted = true;
            engine.seekSeconds (0.0);
            engine.play();
            render (engine, out, 20);
            float before = 0.0f, strong = 0.0f, between = 0.0f, soft = 0.0f;
            for (int b = 0; b < 200 && engine.isPlaying(); ++b)
            {
                render (engine, out, 1);
                const double t = engine.getPositionSeconds() - block / sr;
                const float pk = out.peak (0);
                if (t > 0.12 && t + block / sr < 0.24) before = std::max (before, pk);
                if (t > 0.26 && t + block / sr < 0.29) strong = std::max (strong, pk);
                if (t > 0.34 && t + block / sr < 0.58) between = std::max (between, pk);
                if (t > 0.61 && t + block / sr < 0.64) soft = std::max (soft, pk);
            }
            CHECK (before < 1.0e-4f && between < 1.0e-4f);      // 50 ms por golpe y silencio fuera
            CHECK (std::abs (strong - 1.0f) < 0.03f);            // velocidad 1 -> nota a 127 -> nivel 1
            CHECK (std::abs (soft - 0.55f) < 0.03f);             // velocidad 0,4 -> 0,25 + 0,75 x 0,4
            CHECK (ilane->control.peakL.load() > 0.9f);          // medidores del canal del instrumento
            ilane->control.muted = true;
            engine.seekSeconds (0.2);
            render (engine, out, 20);
            float muted = 0.0f;
            for (int b = 0; b < 12; ++b) { render (engine, out, 1); muted = std::max (muted, out.peak (0)); }
            CHECK (muted < 1.0e-3f);
            engine.setSamplers (nullptr);
            engine.setInstruments (nullptr);   // aquí muere el plugin (hilo de mensajes)
            engine.pause();
            song->tracks[0]->muted = false;
            song->tracks[1]->muted = false;
            engine.seekSeconds (0.0);
            render (engine, out, 4);
        }
    }
#endif

    std::cout << "[AudioEngine] grabación de la entrada: archivo, posición inicial, escucha con rampa\n";
    {
        song->tracks[0]->muted = true;
        song->tracks[1]->muted = true;
        engine.pause();
        engine.seekSeconds (0.5);
        render (engine, out, 4);
        const auto rawFile = tmp.getChildFile ("entrada-grabada.wav");
        rawFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (rawFile.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0));
        CHECK (w != nullptr);
        stream.release();
        juce::TimeSliceThread thread ("grabacion-test");
        thread.startThread();
        auto writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, 1 << 16);
        RecordSetup rs;
        rs.writer = writer.get();
        rs.inputL = 0;
        rs.inputR = -1;
        rs.monitor = true;
        rs.monitorPair = 0;
        engine.setRecorder (rs);
        CHECK (engine.isRecording());
        std::vector<float> inData ((size_t) block, 0.3f);
        const float* inPtrs[1] = { inData.data() };
        juce::AudioIODeviceCallbackContext ctx;
        float monitorPeak = 0.0f, firstSample = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            engine.audioDeviceIOCallbackWithContext (inPtrs, 1, out.ptrs.data(), (int) out.ptrs.size(), block, ctx);
            if (b == 0) firstSample = std::abs (out.data[0][0]);
            if (b >= 5) monitorPeak = std::max (monitorPeak, out.peak (0));
        }
        CHECK (std::abs ((double) engine.getRecordStartPosition() - 0.5 * sr) <= 2 * block);   // posición del transporte al empezar
        CHECK (engine.getRecordedSamples() == 10 * block && engine.getDroppedSamples() == 0);
        CHECK (engine.takeInputPeak() > 0.29f);
        CHECK (firstSample < 0.02f);                          // la escucha entra con rampa (dentro del primer bloque)
        CHECK (std::abs (monitorPeak - 0.3f) < 0.01f);        // y llega al nivel de la entrada por la salida 1-2
        engine.clearRecorder();
        CHECK (! engine.isRecording());
        float tail = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            engine.audioDeviceIOCallbackWithContext (inPtrs, 1, out.ptrs.data(), (int) out.ptrs.size(), block, ctx);
            tail = out.peak (0);
        }
        CHECK (tail < 1.0e-3f);                               // la escucha se apaga sola
        writer.reset();                                       // cierra el archivo
        thread.stopThread (2000);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rawFile));
        CHECK (reader != nullptr && reader->lengthInSamples == 10 * block);
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> rb (1, (int) reader->lengthInSamples);
            reader->read (&rb, 0, rb.getNumSamples(), 0, true, false);
            CHECK (std::abs (rb.getSample (0, 0) - 0.3f) < 1.0e-3f && std::abs (rb.getSample (0, rb.getNumSamples() - 1) - 0.3f) < 1.0e-3f);
        }
        song->tracks[0]->muted = false;
        song->tracks[1]->muted = false;
        engine.seekSeconds (0.0);
        render (engine, out, 4);
    }

    std::cout << "[AudioEngine] la curva de ganancia por tramo se aplica con rampa\n";
    {
        engine.setClick (false, 120.0, 0.0, 0.0f, 0);
        song->tracks[0]->muted = false;
        song->tracks[0]->outputPair = 0;
        song->tracks[1]->muted = true;
        auto curve = std::make_shared<GainCurve>();
        curve->positions = { 0, (juce::int64) (1.0 * sr) };
        curve->gains = { 1.0f, 0.5f };
        engine.setGainCurve (curve);
        engine.seekSeconds (0.5);
        engine.play();
        render (engine, out, 20);   // fundido de entrada y llegada a ~0.73 s
        float before = 0.0f, after = 0.0f;
        for (int b = 0; b < 120; ++b)
        {
            render (engine, out, 1);
            const double t = engine.getPositionSeconds();
            if (t < 0.98) before = std::max (before, out.peak (0));
            if (t > 1.15 && t < 1.4) after = std::max (after, out.peak (0));
        }
        CHECK (before > 0.45f);                       // antes del tramo: ganancia 1
        CHECK (after > 0.22f && after < 0.28f);       // después de la rampa de 50 ms: la mitad
        engine.setGainCurve (nullptr);
        render (engine, out, 20);
        float restored = 0.0f;
        for (int b = 0; b < 5; ++b) { render (engine, out, 1); restored = std::max (restored, out.peak (0)); }
        CHECK (restored > 0.45f);

        // Nivelado por pista: la pista 0 (stem 0) a la mitad solo en el segundo tramo, la general en 1
        auto perTrack = std::make_shared<GainCurve>();
        perTrack->positions = { 0, (juce::int64) (1.0 * sr) };
        perTrack->gains = { 1.0f, 1.0f };
        perTrack->trackGains = { { 1.0f, 0.5f } };   // solo el stem 0; los demás sin curva = 1
        song->tracks[0]->stemIndex = 0;
        engine.setGainCurve (perTrack);
        engine.seekSeconds (0.5);
        render (engine, out, 20);
        before = after = 0.0f;
        for (int b = 0; b < 120; ++b)
        {
            render (engine, out, 1);
            const double t = engine.getPositionSeconds();
            if (t < 0.98) before = std::max (before, out.peak (0));
            if (t > 1.15 && t < 1.4) after = std::max (after, out.peak (0));
        }
        CHECK (before > 0.45f && after > 0.22f && after < 0.28f);
        engine.setGainCurve (nullptr);
        song->tracks[1]->muted = false;
    }

    std::cout << "[AudioEngine] el click sigue la rejilla de tiempos detectados (y publica su pico)\n";
    {
        song->tracks[0]->muted = true;
        song->tracks[1]->muted = true;
        auto grid = std::make_shared<BeatGrid>();
        for (int i = 0; i < 6; ++i)   // tiempos irregulares: 0.30, 0.70, 1.00, 1.45, 1.80, 2.10 s
        {
            const double t[] = { 0.30, 0.70, 1.00, 1.45, 1.80, 2.10 };
            grid->positions.push_back ((juce::int64) (t[i] * sr));
            grid->beatInBar.push_back (i % 3 == 0 ? 1 : 2);
        }
        engine.setBeatGrid (grid);
        engine.setClick (true, 120.0, 0.0, 0.0f, 0);
        engine.seekSeconds (0.0);
        render (engine, out, 4);
        // Entre 0.05 s y 0.25 s no hay tiempo detectado: silencio aunque la rejilla fija de 120 BPM tendría uno en 0
        juce::int64 samplePos = (juce::int64) engine.getPositionSeconds() * 0;
        float silentPeak = 0.0f, beatPeak = 0.0f;
        for (int b = 0; b < 200 && engine.isPlaying(); ++b)
        {
            render (engine, out, 1);
            const double t = engine.getPositionSeconds() - block / sr;   // inicio del bloque recién renderizado
            const float pk = out.peak (0);
            if (t > 0.05 && t + block / sr < 0.28) silentPeak = std::max (silentPeak, pk);
            if (t <= 0.70 && t + block / sr > 0.70) beatPeak = std::max (beatPeak, pk);
        }
        (void) samplePos;
        CHECK (silentPeak < 1.0e-4f);
        CHECK (beatPeak > 0.3f);
        CHECK (engine.takeClickPeak() > 0.3f && engine.takeClickPeak() < 1.0e-6f);   // el pico del click se publica y se consume al leerlo
        engine.setBeatGrid (nullptr);
        engine.setClick (false, 120.0, 0.0, 0.0f, 0);
        song->tracks[0]->muted = false;
        song->tracks[1]->muted = false;
        engine.seekSeconds (0.0);
        render (engine, out, 4);
    }

    std::cout << "[AudioEngine] el salto hace fundido (sin clics)\n";
    engine.setClick (false, 120.0, 0.0, 0.0f, 0);
    song->tracks[0]->muted = false;
    song->tracks[0]->outputPair = 0;
    render (engine, out, 4);
    {
        engine.seekSeconds (1.7);
        CHECK (maxJump (engine, out, 4) < 0.06f);   // la senoide de prueba cambia como máximo ~0.025 por muestra
    }

    std::cout << "[AudioEngine] el cierre del loop hace crossfade (sin clics)\n";
    {
        // Loop cuyo final cae en un pico (+0.5) y cuyo inicio en un valle (-0.5): el peor caso
        int ls = 22050, le = 44100;
        while (std::sin ((float) ls * 0.05f) > -0.999f) ++ls;
        while (std::sin ((float) (le - 1) * 0.05f) < 0.999f) ++le;
        engine.seekSeconds ((ls + 0.5) / sr);
        engine.setLoop ((ls + 0.5) / sr, (le + 0.5) / sr);
        render (engine, out, 20);
        CHECK (maxJump (engine, out, 400) < 0.06f);
        const double p = engine.getPositionSeconds();
        CHECK (p >= (ls - 1) / sr && p <= (le + 1) / sr);
        CHECK (engine.isPlaying());
        engine.clearLoop();
    }

    std::cout << "[AudioEngine] cache de forma de onda\n";
    {
        const auto& wf = song->tracks[0]->waveform;
        CHECK (wf.numBins() == ((int) song->length + WaveformCache::binSize - 1) / WaveformCache::binSize);
        float mnL, mxL, mnR, mxR;
        CHECK (wf.rangeMinMax (0, song->length, mnL, mxL, mnR, mxR));
        CHECK (mxL > 0.45f && mnL < -0.45f && mxR > 0.45f && mnR < -0.45f);   // senoide de amplitud 0.5
        CHECK (wf.peak > 0.45f && wf.peak <= 0.5f);
        CHECK (wf.rangeMinMax (0, 10, mnL, mxL, mnR, mxR) && mxL <= 0.5f && mnL >= -0.5f);
        CHECK (! wf.rangeMinMax (song->length + 1000, song->length + 2000, mnL, mxL, mnR, mxR));   // fuera del audio
        CHECK (! wf.rangeMinMax (100, 100, mnL, mxL, mnR, mxR));                                    // rango vacío
    }

    std::cout << "[AudioEngine] medidores por canal, de salida y fader maestro\n";
    {
        engine.seekSeconds (0.5);
        render (engine, out, 20);
        song->tracks[0]->peakL.exchange (0.0f);
        song->tracks[0]->peakR.exchange (0.0f);
        engine.takeOutputPeak (0);
        render (engine, out, 5);
        const float pL = song->tracks[0]->peakL.exchange (0.0f), pR = song->tracks[0]->peakR.exchange (0.0f);
        CHECK (pL > 0.4f && pR > 0.4f && std::abs (pL - pR) < 0.01f);
        CHECK (song->tracks[0]->rmsL.load() > 0.2f && song->tracks[0]->rmsL.load() < pL);
        const float outPeak = engine.takeOutputPeak (0);
        CHECK (outPeak > 0.4f);
        CHECK (engine.takeOutputPeak (0) < 1.0e-9f);          // ya se leyó
        CHECK (engine.getOutputRms (0) > 0.2f && engine.getOutputRms (0) < 0.5f);
        CHECK (engine.takeOutputPeak (99) < 1.0e-9f && engine.getOutputRms (-1) < 1.0e-9f);

        engine.setMasterGain (0.5f);
        render (engine, out, 2);                              // rampa
        engine.takeOutputPeak (0);
        render (engine, out, 5);
        const float halved = engine.takeOutputPeak (0);
        CHECK (halved > 0.2f && halved < 0.3f);
        engine.setMasterGain (1.0f);
        render (engine, out, 2);
    }

    std::cout << "[AudioEngine] pausa: el fundido llega a silencio en un bloque\n";
    {
        CHECK (! engine.isSilent());
        engine.pause();
        render (engine, out, 1);
        CHECK (engine.isSilent());
        CHECK (std::abs (out.data[0][block - 1]) < 1.0e-9f);
        engine.play();
        render (engine, out, 1);
        CHECK (! engine.isSilent());
    }

    std::cout << "[AudioEngine] fundido al final de una canción que no termina en silencio\n";
    {
        auto folder = tmp.getChildFile ("fin");
        folder.createDirectory();
        juce::AudioBuffer<float> b (2, (int) sr);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, 0.45f);   // valor constante: el stem termina "cortado", no en silencio
        writeWav (folder.getChildFile ("a.wav"), b, sr);

        AudioEngine e2;
        auto s2 = loadFolder (folder, formats, sr);
        CHECK (s2 != nullptr && s2->tracks.size() == 1);
        e2.setSong (s2);
        FakeOutputs o2 (2);
        e2.play();
        bool playing = true;
        CHECK (maxJump (e2, o2, 200, &playing) < 0.06f);
        CHECK (! e2.isPlaying());
        e2.setSong (nullptr);
    }

    std::cout << "[Mix] armar mixes: tiempos, ubicación, render, canción y disco\n";
    {
        // Clicks de 1 ms (rampa que cae) en tiempos conocidos
        auto clickTrack = [] (double rate, double seconds, int channels, const std::vector<double>& times, float amp)
        {
            juce::AudioBuffer<float> b (channels, (int) std::llround (rate * seconds));
            b.clear();
            const int len = (int) std::llround (rate * 0.001);
            for (double t : times)
            {
                const int at = (int) std::llround (t * rate);
                for (int i = 0; i < len && at + i < b.getNumSamples(); ++i)
                    for (int ch = 0; ch < channels; ++ch)
                        b.setSample (ch, at + i, amp * (1.0f - (float) i / (float) len));
            }
            return b;
        };
        // Tiempos regulares: n tiempos desde `first` cada `step` s; `downbeatAt` = índice del primer 1
        auto regularBeats = [] (double first, double step, int n, int downbeatAt)
        {
            std::vector<Beat> beats;
            for (int k = 0; k < n; ++k)
                beats.push_back ({ first + step * k, ((k - downbeatAt) % 4 + 4) % 4 + 1 });
            return beats;
        };
        auto beatTimes = [] (const std::vector<Beat>& beats)
        {
            std::vector<double> times;
            for (auto& bt : beats)
                times.push_back (bt.seconds);
            return times;
        };
        // Posición (s) del máximo absoluto del canal 0 en [around - window, around + window]
        auto peakNear = [] (const juce::AudioBuffer<float>& b, double rate, double around, double window)
        {
            const int from = juce::jmax (0, (int) ((around - window) * rate));
            const int to = juce::jmin (b.getNumSamples(), (int) ((around + window) * rate));
            int best = from;
            float bestValue = -1.0f;
            for (int i = from; i < to; ++i)
                if (std::abs (b.getSample (0, i)) > bestValue)
                {
                    bestValue = std::abs (b.getSample (0, i));
                    best = i;
                }
            return (double) best / rate;
        };
        auto approx = [] (double x, double y, double tol = 1.0e-9) { return std::abs (x - y) < tol; };

        // Fuente A: 44,1 kHz estéreo, 120 BPM (tiempos en 0,25 + 0,5 k; 1 en 0,25 y 2,25)
        // Fuente B: 48 kHz mono, 100 BPM (tiempos en 0,3 + 0,6 k; 1 en 0,9 y 3,3)
        // Fuente C: 44,1 kHz estéreo, nivel constante 0,5 (sin analizar)
        const auto mixFiles = tmp.getChildFile ("mix-originales");
        mixFiles.createDirectory();
        Analysis anA, anB;
        anA.beats = regularBeats (0.25, 0.5, 8, 0);
        anA.chords = { { 0.0, 1.25, "Am" }, { 1.25, 4.0, "C" } };
        anA.key = "La menor";
        anA.meter = 4;
        anA.bpm = 120.0;
        anB.beats = regularBeats (0.3, 0.6, 7, 1);
        anB.chords = { { 0.0, 2.1, "A#" }, { 2.1, 4.0, "Dm" } };
        anB.key = "Sol mayor";
        anB.meter = 4;
        anB.bpm = 100.0;
        const auto fileA = mixFiles.getChildFile ("fuente-a.wav");
        const auto fileB = mixFiles.getChildFile ("fuente-b.wav");
        const auto fileC = mixFiles.getChildFile ("fuente-c.wav");
        const auto bufA = clickTrack (44100.0, 4.0, 2, beatTimes (anA.beats), 0.8f);
        writeWav (fileA, bufA, 44100.0);
        writeWav (fileB, clickTrack (48000.0, 4.0, 1, beatTimes (anB.beats), 0.8f), 48000.0);
        {
            juce::AudioBuffer<float> dc (2, (int) (44100.0 * 4.0));
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
            writeWav (fileC, dc, 44100.0);
        }

        std::cout << "  tiempos: tempo de un tramo, ajuste a tiempos y compases\n";
        CHECK (approx (mix::segmentBpm (anA, 0.25, 2.25), 120.0));
        CHECK (approx (mix::segmentBpm (anB, 0.9, 3.3), 100.0));
        CHECK (approx (mix::segmentBpm (anA, 0.24, 2.24), 120.0));     // cortes un poco antes del golpe
        CHECK (approx (mix::segmentBpm (anA, 0.25, 0.26), 0.0));             // un solo tiempo
        CHECK (approx (mix::segmentBpm (Analysis(), 0.0, 10.0), 0.0));
        {
            Analysis jitter;
            jitter.beats = { { 0.0, 1 }, { 0.51, 2 }, { 0.99, 3 }, { 1.52, 4 } };
            CHECK (approx (mix::segmentBpm (jitter, 0.0, 1.52), 60.0 * 3.0 / 1.52));
        }
        CHECK (approx (mix::snapToBeat (anA, 1.1, false), 1.25));
        CHECK (approx (mix::snapToBeat (anA, 1.1, true), 0.25));
        CHECK (approx (mix::snapToBeat (Analysis(), 1.1, true), 1.1));
        CHECK (approx (mix::previousDownbeat (anA, 2.25), 0.25));
        CHECK (approx (mix::previousDownbeat (anA, 2.26), 0.25));      // a menos de 0,05 s no cuenta
        CHECK (approx (mix::previousDownbeat (anA, 2.4), 2.25));
        CHECK (approx (mix::previousDownbeat (anA, 0.2), 0.2));        // no hay
        CHECK (approx (mix::nextDownbeat (anA, 0.25), 2.25));
        CHECK (approx (mix::nextDownbeat (anA, 2.25), 2.25));          // no hay otro
        CHECK (mix::barNumberAt (anA, 0.1) == 0);
        CHECK (mix::barNumberAt (anA, 0.22) == 1);
        CHECK (mix::barNumberAt (anA, 2.3) == 2);
        CHECK (mix::barNumberAt (Analysis(), 2.3) == 0);
        CHECK (mix::barsBetween (anA, 0.25, 4.25) == 2);
        CHECK (mix::barsBetween (anA, 0.24, 2.24) == 1);
        CHECK (mix::barsBetween (anB, 0.89, 3.29) == 1);

        std::cout << "  proyecto: crear, fuentes copiadas, quitar\n";
        const auto mixRoot = tmp.getChildFile ("lib-mixes").getChildFile (Library::mixesFolderName());
        const auto projFolder = MixProject::create (mixRoot, "Mix de prueba");
        CHECK (projFolder.isDirectory() && projFolder.getChildFile ("mix.json").existsAsFile());
        MixProject proj;
        CHECK (MixProject::load (projFolder, proj));
        CHECK (proj.name == "Mix de prueba" && proj.sources.empty() && proj.segments.empty() && proj.folder == projFolder);
        CHECK (proj.addSource (fileA, formats) == 0);
        CHECK (proj.addSource (fileB, formats) == 1);
        CHECK (proj.addSource (fileC, formats) == 2);
        CHECK (proj.sources.size() == 3 && proj.sources[0].name == "fuente-a" && proj.sources[0].fileName == "fuente-a.wav");
        CHECK (proj.sourceFile (0).existsAsFile() && proj.sourceFile (0).isAChildOf (proj.sourcesFolder()));
        CHECK (std::abs (proj.sources[0].length - 4.0) < 1.0e-6 && std::abs (proj.sources[1].length - 4.0) < 1.0e-6);
        CHECK (proj.sourceFile (7) == juce::File() && proj.sourceFile (-1) == juce::File());
        {
            const auto notAudio = mixFiles.getChildFile ("texto.wav");
            notAudio.replaceWithText ("no es audio");
            CHECK (proj.addSource (notAudio, formats) == -1);
            CHECK (proj.addSource (mixFiles.getChildFile ("no-existe.wav"), formats) == -1);
            CHECK (proj.sources.size() == 3);
        }
        proj.sources[0].analysis = anA;
        proj.sources[1].analysis = anB;

        std::cout << "  ubicación: uniones en la rejilla, razón por tramo, fundidos\n";
        proj.bpm = 100.0;
        MixSegment segA;
        segA.source = 0;
        segA.start = 0.25;
        segA.end = 2.25;
        MixSegment segB;
        segB.source = 1;
        segB.start = 0.9;
        segB.end = 3.3;
        proj.segments = { segA, segB };
        {
            auto pl = mix::layout (proj);
            CHECK (pl.size() == 2);
            CHECK (approx (pl[0].outStart, 0.0) && approx (pl[0].outEnd, 2.4) && approx (pl[0].ratio, 1.2));
            CHECK (approx (pl[0].srcBpm, 120.0) && approx (pl[0].playBpm, 100.0) && approx (pl[0].srcStart, 0.25) && approx (pl[0].srcEnd, 2.25));
            CHECK (approx (pl[1].outStart, 2.4) && approx (pl[1].outEnd, 4.8) && approx (pl[1].ratio, 1.0));
            CHECK (approx (pl[0].fadeIn, 0.0) && approx (pl[0].fadeOut, mix::cutFadeSeconds));
            CHECK (approx (pl[1].fadeIn, mix::cutFadeSeconds) && approx (pl[1].fadeOut, mix::cutFadeSeconds));
            CHECK (approx (mix::length (proj), 4.8));
            // La unión cae un tiempo (al tempo del mix) después del último tiempo del tramo anterior
            CHECK (approx (pl[1].outStart - mix::toMix (pl[0], 1.75), 0.6));
            CHECK (approx (mix::toMix (pl[1], 1.5), 3.0));
        }
        {
            // Fundido musical de un tiempo; acotado al audio previo en la fuente
            auto p2 = proj;
            p2.segments[1].fadeBeats = 1.0;
            auto pl = mix::layout (p2);
            CHECK (approx (pl[1].fadeIn, 0.6) && approx (pl[0].fadeOut, 0.6));
            p2.segments[1].start = 0.3;   // solo hay 0,3 s antes
            pl = mix::layout (p2);
            CHECK (approx (pl[1].fadeIn, 0.3) && approx (pl[0].fadeOut, 0.3) && approx (pl[1].outEnd, 2.4 + 3.0));
            p2.segments[1].start = 0.0;   // nada antes: sin fundido cruzado
            pl = mix::layout (p2);
            CHECK (approx (pl[1].fadeIn, 0.0) && approx (pl[0].fadeOut, 0.0));
            // Acotado a la mitad del tramo más corto
            p2.segments[1] = segB;
            p2.segments[1].start = 2.1;   // dura 1,2 s: el fundido no pasa de 0,6 s
            p2.segments[1].fadeBeats = 16.0;
            pl = mix::layout (p2);
            CHECK (approx (pl[1].fadeIn, 0.6) && approx (pl[0].fadeOut, 0.6));
        }
        {
            // Tempo propio, cada tramo a su tempo, tempo del mix sin fijar, tramo sin analizar, fuente inexistente
            auto p2 = proj;
            p2.segments[0].playBpm = 60.0;
            auto pl = mix::layout (p2);
            CHECK (approx (pl[0].ratio, 2.0) && approx (pl[0].outEnd, 4.0) && approx (pl[1].outStart, 4.0) && approx (pl[1].ratio, 1.0));
            p2.segments[0].playBpm = 0.0;
            p2.keepTempos = true;
            pl = mix::layout (p2);
            CHECK (approx (pl[0].ratio, 1.0) && approx (pl[0].playBpm, 120.0) && approx (pl[1].ratio, 1.0) && approx (pl[1].playBpm, 100.0));
            CHECK (approx (pl[0].outEnd, 2.0) && approx (pl[1].outEnd, 4.4));
            p2.keepTempos = false;
            p2.bpm = 0.0;
            CHECK (approx (p2.effectiveBpm(), 120.0));   // el del primer tramo
            pl = mix::layout (p2);
            CHECK (approx (pl[0].ratio, 1.0) && approx (pl[1].ratio, 100.0 / 120.0) && approx (pl[1].playBpm, 120.0));
            MixSegment segC;
            segC.source = 2;
            segC.start = 1.0;
            segC.end = 2.0;
            p2.segments = { segC };
            CHECK (approx (p2.effectiveBpm(), 120.0));   // sin análisis: 120
            p2.bpm = 90.0;
            pl = mix::layout (p2);
            CHECK (approx (pl[0].ratio, 1.0) && approx (pl[0].srcBpm, 90.0) && approx (pl[0].playBpm, 90.0) && approx (pl[0].outEnd, 1.0));
            MixSegment missing;
            missing.source = 9;
            missing.start = 0.0;
            missing.end = 1.0;
            MixSegment beyond = segC;
            beyond.start = 3.5;
            beyond.end = 9.0;   // se acota al largo de la fuente
            p2.segments = { segC, missing, beyond };
            pl = mix::layout (p2);
            CHECK (pl.size() == 3 && approx (pl[1].outStart, 1.0) && approx (pl[1].outEnd, 1.0));
            CHECK (approx (pl[2].outStart, 1.0) && approx (pl[2].srcEnd, 4.0) && approx (pl[2].outEnd, 1.5));
            CHECK (approx (pl[2].fadeIn, mix::cutFadeSeconds) && approx (pl[0].fadeOut, mix::cutFadeSeconds));
            CHECK (approx (mix::length (p2), 1.5));
            CHECK (approx (mix::length (MixProject()), 0.0) && mix::layout (MixProject()).empty());
        }

        std::cout << "  render: clicks donde dice la ubicación, unión exacta, copia directa, fundidos\n";
        {
            float lastProgress = 0.0f;
            juce::String err;
            auto rendered = mix::render (proj, mix::renderSampleRate, formats, {}, [&] (float p) { lastProgress = p; }, err);
            CHECK (err.isEmpty() && rendered.getNumChannels() == 2);
            CHECK (rendered.getNumSamples() == (int) std::ceil (4.8 * mix::renderSampleRate));
            CHECK (lastProgress > 0.99f);
            const auto pl = mix::layout (proj);
            double lastA = 0.0, firstB = 0.0;
            for (auto& bt : anA.beats)
                if (bt.seconds >= 0.25 - 0.05 && bt.seconds < 2.25 - 0.05)
                {
                    const double expected = mix::toMix (pl[0], bt.seconds);
                    const double found = peakNear (rendered, mix::renderSampleRate, expected, 0.03);
                    CHECK (std::abs (found - expected) < 0.002);
                    lastA = found;
                }
            for (auto& bt : anB.beats)
                if (bt.seconds >= 0.9 - 0.05 && bt.seconds < 3.3 - 0.05)
                {
                    const double expected = mix::toMix (pl[1], bt.seconds);
                    const double found = peakNear (rendered, mix::renderSampleRate, expected, 0.03);
                    CHECK (std::abs (found - expected) < 0.002);
                    if (firstB <= 0.0)
                        firstB = found;
                }
            CHECK (std::abs ((firstB - lastA) - 0.6) < 0.002);   // la unión queda a un tiempo exacto
            CHECK (rendered.getMagnitude (0, (int) (4.25 * mix::renderSampleRate), (int) (0.5 * mix::renderSampleRate)) < 1.0e-3f);
        }
        {
            // Sin estirar (tempo del mix = el del primer tramo): copia exacta de las muestras, también en
            // la unión (dos tramos seguidos de la misma fuente: el fundido suma 1)
            auto p2 = proj;
            p2.bpm = 0.0;
            MixSegment second = segA;
            second.start = 2.25;
            second.end = 3.75;
            p2.segments = { segA, second };
            juce::String err;
            auto rendered = mix::render (p2, mix::renderSampleRate, formats, {}, {}, err);
            CHECK (rendered.getNumSamples() == (int) std::ceil (3.5 * mix::renderSampleRate));
            float worst = 0.0f;
            const int offsetA = 11025;   // 0,25 s
            for (int m = 100; m < (int) (3.49 * mix::renderSampleRate); ++m)
                for (int ch = 0; ch < 2; ++ch)
                    worst = juce::jmax (worst, std::abs (rendered.getSample (ch, m) - bufA.getSample (ch, m + offsetA)));
            CHECK (worst < 1.0e-6f);
        }
        {
            // Dos tramos seguidos de la misma fuente (el mismo audio a los dos lados de la unión): el
            // fundido no sube el nivel (con seno y coseno subiría hasta 3 dB), con corte o fundido musical.
            // Con un salto en la fuente ya no es el mismo audio: igual potencia.
            MixProject p2 = proj;
            p2.bpm = 0.0;
            MixSegment first;
            first.source = 2;
            first.start = 1.0;
            first.end = 2.0;
            MixSegment next = first;
            next.start = 2.0;
            next.end = 2.5;
            const double rate = mix::renderSampleRate;
            for (double fadeBeats : { 0.0, 1.0 })
            {
                next.fadeBeats = fadeBeats;
                p2.segments = { first, next };
                const auto pl = mix::layout (p2);
                CHECK (approx (pl[1].fadeIn, fadeBeats > 0.0 ? 0.25 : mix::cutFadeSeconds));
                juce::String err;
                const auto rendered = mix::render (p2, rate, formats, {}, {}, err);
                CHECK (err.isEmpty() && rendered.getNumSamples() == (int) std::ceil (1.5 * rate));
                float worst = 0.0f;
                for (int m = (int) (0.01 * rate); m < (int) (1.48 * rate); ++m)
                    for (int ch = 0; ch < 2; ++ch)
                        worst = juce::jmax (worst, std::abs (rendered.getSample (ch, m) - 0.5f));
                CHECK (worst < 1.0e-5f);
            }
            next.fadeBeats = 0.0;
            next.start = 2.01;
            p2.segments = { first, next };
            juce::String err;
            const auto rendered = mix::render (p2, rate, formats, {}, {}, err);
            const int mid = (int) std::llround (0.995 * rate);
            const double x = ((double) mid / rate - 0.99) / 0.01;
            const double expected = 0.5 * (std::cos (x * juce::MathConstants<double>::halfPi) + std::sin (x * juce::MathConstants<double>::halfPi));
            CHECK (std::abs (rendered.getSample (0, mid) - (float) expected) < 1.0e-4f && expected > 0.7);
        }
        {
            // Fundidos con una fuente de nivel constante
            MixProject p2 = proj;
            p2.bpm = 0.0;
            MixSegment first;
            first.source = 2;
            first.start = 1.0;
            first.end = 2.0;
            MixSegment second = first;
            second.start = 3.0;
            second.end = 3.5;
            second.gainDb = juce::Decibels::gainToDecibels (0.5f);
            p2.segments = { first, second };
            juce::String err;
            auto rendered = mix::render (p2, mix::renderSampleRate, formats, {}, {}, err);
            const double rate = mix::renderSampleRate;
            auto at = [&] (double t) { return rendered.getSample (0, (int) std::llround (t * rate)); };
            auto timeOf = [&] (double t) { return (double) std::llround (t * rate) / rate; };
            CHECK (rendered.getNumSamples() == (int) std::ceil (1.5 * rate));
            CHECK (std::abs (rendered.getSample (0, 0)) < 1.0e-9f);                                   // silencio antes del primer tramo
            CHECK (std::abs (at (0.001) - 0.5f * (float) (timeOf (0.001) / 0.002)) < 1.0e-3f);   // entra con 2 ms lineales
            CHECK (std::abs (at (0.5) - 0.5f) < 1.0e-4f);
            const double xt = timeOf (0.995);   // mitad del fundido cruzado de 10 ms antes de la unión
            const double x = (xt - 0.99) / 0.01;
            const double expected = 0.5 * std::cos (x * juce::MathConstants<double>::halfPi) + 0.25 * std::sin (x * juce::MathConstants<double>::halfPi);
            CHECK (std::abs (at (0.995) - (float) expected) < 2.0e-3f);
            CHECK (std::abs (at (0.985) - 0.5f) < 1.0e-4f);
            CHECK (std::abs (at (1.2) - 0.25f) < 1.0e-4f);                               // ganancia del segundo tramo
            CHECK (std::abs (rendered.getSample (1, (int) (1.2 * rate)) - 0.25f) < 1.0e-4f);
            CHECK (std::abs (rendered.getSample (0, rendered.getNumSamples() - 1)) < 0.01f);   // termina en silencio
            const double yt = timeOf (1.495);
            CHECK (std::abs (at (1.495) - 0.25f * (float) std::cos ((yt - 1.49) / 0.01 * juce::MathConstants<double>::halfPi)) < 2.0e-3f);
            // Sin audio previo en la fuente: el que entra lo hace con 2 ms lineales desde la unión
            p2.segments[1].start = 0.0;
            p2.segments[1].end = 0.5;
            p2.segments[1].fadeBeats = 1.0;
            rendered = mix::render (p2, rate, formats, {}, {}, err);
            CHECK (std::abs (at (0.985) - 0.5f) < 1.0e-4f);
            CHECK (std::abs (at (1.001) - 0.25f * (float) ((timeOf (1.001) - 1.0) / 0.002)) < 2.0e-3f);
            CHECK (std::abs (at (0.9995) - 0.5f * (float) std::cos ((timeOf (0.9995) - 0.998) / 0.002 * juce::MathConstants<double>::halfPi)) < 2.0e-3f);
            CHECK (std::abs (at (1.1) - 0.25f) < 1.0e-4f);
        }
        {
            // Transponer (sin estirar) pasa por el estirador y conserva el largo
            auto p2 = proj;
            p2.bpm = 0.0;
            MixSegment up = segA;
            up.end = 1.25;
            up.transpose = 2;
            p2.segments = { up };
            juce::String err;
            auto rendered = mix::render (p2, mix::renderSampleRate, formats, {}, {}, err);
            CHECK (err.isEmpty() && rendered.getNumSamples() == (int) std::ceil (1.0 * mix::renderSampleRate));
            CHECK (rendered.getMagnitude (0, rendered.getNumSamples()) > 0.1f);
            // Abortar vacía el resultado sin error: antes del primer tramo y dentro del estirador (la
            // primera consulta es la del tramo; las siguientes, las del estirador, una por bloque)
            err = "x";
            auto aborted = mix::render (p2, mix::renderSampleRate, formats, [] { return true; }, {}, err);
            CHECK (aborted.getNumSamples() == 0 && err.isEmpty());
            int abortCalls = 0;
            err = "x";
            aborted = mix::render (p2, mix::renderSampleRate, formats, [&abortCalls] { return ++abortCalls >= 3; }, {}, err);
            CHECK (aborted.getNumSamples() == 0 && err.isEmpty() && abortCalls == 3);
            // Una fuente que falta es un error
            p2.sources[0].fileName = "no-existe.wav";
            auto failed = mix::render (p2, mix::renderSampleRate, formats, {}, {}, err);
            CHECK (failed.getNumSamples() == 0 && err.contains ("fuente-a"));
            p2.segments.clear();
            CHECK (mix::render (p2, mix::renderSampleRate, formats, {}, {}, err).getNumSamples() == 0 && err.isNotEmpty());
        }

        {
            // Fundido cruzado hacia un tramo ESTIRADO: primero la fuente de nivel constante (sin estirar)
            // y después A (120 BPM) a 100 BPM con 1,5 tiempos de fundido (0,9 s). En [J - F, J] ya suena
            // el audio de A que precede a su inicio (con el seno) sobre el anterior que se apaga (coseno)
            auto p2 = proj;   // 100 BPM
            MixSegment level;
            level.source = 2;
            level.start = 1.0;
            level.end = 3.0;
            MixSegment stretched = segA;
            stretched.start = 1.25;
            stretched.end = 3.25;
            stretched.fadeBeats = 1.5;
            p2.segments = { level, stretched };
            const auto pl = mix::layout (p2);
            CHECK (approx (pl[0].outEnd, 2.0) && approx (pl[1].ratio, 1.2) && approx (pl[1].fadeIn, 0.9)
                   && approx (pl[0].fadeOut, 0.9) && approx (pl[1].outEnd, 4.4));
            const double rate = mix::renderSampleRate;
            const double halfPi = juce::MathConstants<double>::halfPi;
            juce::String err;
            const auto rendered = mix::render (p2, rate, formats, {}, {}, err);
            CHECK (err.isEmpty() && rendered.getNumSamples() == (int) std::ceil (4.4 * rate));
            // Lo que aporta A = el mix menos el tramo constante (0,5 con 2 ms de entrada y el coseno de salida)
            juce::AudioBuffer<float> incoming (1, rendered.getNumSamples());
            for (int m = 0; m < rendered.getNumSamples(); ++m)
            {
                const double t = (double) m / rate;
                double g = 0.0;
                if (m < (int) std::ceil (2.0 * rate))
                    g = juce::jlimit (0.0, 1.0, t / mix::edgeFadeSeconds) * (t > 1.1 ? std::cos (halfPi * juce::jlimit (0.0, 1.0, (t - 1.1) / 0.9)) : 1.0);
                incoming.setSample (0, m, rendered.getSample (0, m) - (float) (0.5 * g));
            }
            CHECK (incoming.getMagnitude (0, 0, (int) (1.09 * rate)) < 1.0e-5f);   // A no entra antes de J - F
            // El click de 0,75 s de A (medio tiempo antes de su inicio) cae en J - 0,6 con ganancia seno(30°) = 0,5
            const double pre = mix::toMix (pl[1], 0.75);
            CHECK (approx (pre, 1.4, 1.0e-9));
            const double foundPre = peakNear (incoming, rate, pre, 0.03);
            CHECK (std::abs (foundPre - pre) < 0.002);
            const float preLevel = std::abs (incoming.getSample (0, (int) std::llround (foundPre * rate)));
            // Y los clicks de A desde la unión, donde dice toMix (la unión cae en su tiempo de 1,25 s)
            float meanLevel = 0.0f;
            for (double beat : { 1.25, 1.75, 2.25, 2.75 })
            {
                const double expected = mix::toMix (pl[1], beat);
                const double found = peakNear (incoming, rate, expected, 0.03);
                CHECK (std::abs (found - expected) < 0.002);
                meanLevel += 0.25f * std::abs (incoming.getSample (0, (int) std::llround (found * rate)));
            }
            CHECK (approx (mix::toMix (pl[1], 1.25), 2.0, 1.0e-9));
            CHECK (preLevel > 0.2f * meanLevel && preLevel < 0.8f * meanLevel);   // suena, con la ganancia reducida (0,38 aquí)
        }

        std::cout << "  canción del mix: tiempos, acordes, tonalidad, secciones de tempo, marcadores\n";
        {
            auto p2 = proj;
            p2.segments[0].start = 0.24;   // cortes un poco antes del golpe, como con la transiente
            p2.segments[0].end = 2.24;
            p2.segments[1].start = 0.89;
            p2.segments[1].end = 3.29;
            p2.segments[1].transpose = 2;
            p2.segments[1].label = "Coro";
            SongInfo described;
            described.clips = { Clip { 0.0, 1.0, 0.0 } };
            described.notes = { SongNote { 1.0, 2.0, "nota" } };
            described.playBpm = 90.0;
            described.transpose = 3;
            described.levelingEnabled = true;
            mix::describeSong (p2, described);
            const auto& an = described.analysis;
            CHECK (described.name == "Mix de prueba");
            CHECK (an.beats.size() == 8);
            if (an.beats.size() == 8)
            {
                CHECK (approx (an.beats[0].seconds, 0.012, 1.0e-6) && an.beats[0].beatInBar == 1);
                CHECK (approx (an.beats[3].seconds, 1.812, 1.0e-6) && an.beats[3].beatInBar == 4);
                CHECK (approx (an.beats[4].seconds, 2.41, 1.0e-6) && an.beats[4].beatInBar == 1);
                CHECK (approx (an.beats[7].seconds, 4.21, 1.0e-6) && an.beats[7].beatInBar == 4);
            }
            CHECK (an.chords.size() == 3);
            if (an.chords.size() == 3)
            {
                CHECK (an.chords[0].name == "Am" && approx (an.chords[0].start, 0.0));
                CHECK (approx (an.chords[0].end, (1.25 - 0.24) * 1.2, 1.0e-6));
                CHECK (an.chords[1].name == "C" && approx (an.chords[1].start, (1.25 - 0.24) * 1.2, 1.0e-6) && approx (an.chords[1].end, 2.4 + (2.1 - 0.89), 1.0e-6));
                CHECK (an.chords[2].name == "Em" && approx (an.chords[2].end, 4.8, 1.0e-6));
            }
            CHECK (an.key == "La menor" && an.meter == 4 && approx (an.bpm, 100.0));
            CHECK (described.tempoRegions.size() == 1 && approx (described.tempoRegions[0].start, 0.0)
                   && approx (described.tempoRegions[0].origBpm, 100.0) && approx (described.tempoRegions[0].playBpm, 0.0));
            CHECK (described.markers.size() == 2);
            if (described.markers.size() == 2)
            {
                CHECK (described.markers[0].name == "fuente-a" && approx (described.markers[0].seconds, 0.0));
                CHECK (described.markers[1].name == "Coro" && approx (described.markers[1].seconds, 2.4));
            }
            CHECK (approx (described.bpm, 100.0) && approx (described.clickOffset, 0.012, 1.0e-6));
            CHECK (described.clips.empty() && described.notes.empty() && approx (described.playBpm, 0.0) && described.transpose == 0);
            CHECK (! described.levelingEnabled && approx (described.loudnessLufs, unmeasuredDb));
            // Primer tramo transpuesto: tonalidad transpuesta; tempos distintos: dos secciones
            p2.segments[0].transpose = 2;
            p2.segments[1].playBpm = 90.0;
            SongInfo other;
            mix::describeSong (p2, other);
            CHECK (other.analysis.key == "Si menor" && other.analysis.chords.size() == 4 && other.analysis.chords[0].name == "Bm");
            CHECK (other.tempoRegions.size() == 2 && approx (other.tempoRegions[1].start, 2.4) && approx (other.tempoRegions[1].origBpm, 90.0));
            CHECK (other.markers.size() == 2 && approx (other.markers[1].seconds, 2.4));
        }
        {
            // Dos tiempos a menos de 30 ms en una unión (cortes libres): queda el del tramo que entra,
            // cuyo número en el compás es el que siguen los tiempos que vienen
            auto p3 = proj;
            p3.bpm = 120.0;
            p3.segments = { segA, segB };
            p3.segments[0].end = 2.801;     // su 2 de 2,75 cae 51 ms antes de la unión
            p3.segments[1].start = 0.949;   // su 1 de 0,9 cae 41 ms antes (razón 100/120): después del 2
            SongInfo joined;
            mix::describeSong (p3, joined);
            const auto& bts = joined.analysis.beats;
            CHECK (bts.size() == 9);
            if (bts.size() == 9)
            {
                CHECK (approx (bts[4].seconds, 2.0, 1.0e-6) && bts[4].beatInBar == 1);
                CHECK (approx (bts[5].seconds, 2.551, 1.0e-6) && bts[5].beatInBar == 1);   // pegado a la unión
                CHECK (bts[6].beatInBar == 2 && bts[7].beatInBar == 3 && bts[8].beatInBar == 4);
            }
            // Al revés: el del tramo que entra cae antes que el del que sale
            p3.bpm = 100.0;
            p3.segments = { segB, segA };
            p3.segments[0].start = 0.3;
            p3.segments[0].end = 2.151;     // su 3 de 2,1 cae 51 ms antes de la unión
            p3.segments[1].start = 0.299;   // su 1 de 0,25 cae 59 ms antes (razón 1,2): antes del 3
            SongInfo reversed;
            mix::describeSong (p3, reversed);
            const auto& rb = reversed.analysis.beats;
            CHECK (rb.size() == 7);
            if (rb.size() == 7)
            {
                CHECK (approx (rb[2].seconds, 1.2, 1.0e-6) && rb[2].beatInBar == 2);
                CHECK (approx (rb[3].seconds, 1.851, 1.0e-6) && rb[3].beatInBar == 1);   // pegado a la unión
                CHECK (rb[4].beatInBar == 2);
            }
            // Primer tramo cortado poco después de un tiempo (el corte ajustado a la transiente): ese tiempo
            // es el primero del tramo y va en 0, con su número en el compás
            p3 = proj;
            p3.segments[0].start = 0.26;
            p3.segments[0].end = 2.26;
            SongInfo early;
            mix::describeSong (p3, early);
            CHECK (early.analysis.beats.size() == 8);
            if (! early.analysis.beats.empty())
                CHECK (approx (early.analysis.beats.front().seconds, 0.0, 1.0e-9) && early.analysis.beats.front().beatInBar == 1);
            CHECK (approx (early.clickOffset, 0.0, 1.0e-9));
        }

        std::cout << "  disco: guardar y leer, valores fuera de rango\n";
        {
            proj.keepTempos = true;
            proj.segments[1].label = tr ("Canción ñandú");
            proj.segments[1].playBpm = 98.5;
            proj.segments[1].transpose = -3;
            proj.segments[1].gainDb = -4.5f;
            proj.segments[1].fadeBeats = 2.0;
            CHECK (proj.save());
            MixProject back;
            CHECK (MixProject::load (projFolder, back));
            CHECK (back.name == proj.name && approx (back.bpm, 100.0) && back.keepTempos && back.folder == projFolder);
            CHECK (back.sources.size() == 3 && back.sources[1].name == "fuente-b" && back.sources[1].fileName == "fuente-b.wav"
                   && approx (back.sources[1].length, proj.sources[1].length));
            CHECK (back.sources[1].analysis.beats.size() == anB.beats.size() && back.sources[1].analysis.key == "Sol mayor"
                   && back.sources[1].analysis.chords.size() == 2 && back.sources[2].analysis.isEmpty());
            CHECK (back.segments.size() == 2);
            if (back.segments.size() == 2)
            {
                const auto& s1 = back.segments[1];
                CHECK (s1.source == 1 && approx (s1.start, 0.9) && approx (s1.end, 3.3) && s1.label == tr ("Canción ñandú"));
                CHECK (approx (s1.playBpm, 98.5) && s1.transpose == -3 && std::abs (s1.gainDb + 4.5f) < 1.0e-6f && approx (s1.fadeBeats, 2.0));
            }
            CHECK (! MixProject::load (tmp.getChildFile ("no-hay-mix"), back));

            const auto bad = mixRoot.getChildFile ("fuera-de-rango");
            bad.createDirectory();
            bad.getChildFile ("mix.json").replaceWithText (R"({ "name": "X", "bpm": 5, "sources": [ { "name": "a", "file": "../../a.wav", "length": -3 } ],
                "segments": [ { "source": 0, "start": 1, "end": 2, "transpose": 30, "gainDb": -100, "fadeBeats": 99, "playBpm": 900 },
                              { "source": 3, "start": 1, "end": 2 },
                              { "source": 0, "start": 2, "end": 1 },
                              { "source": 0, "start": -1, "end": 2, "playBpm": -4 },
                              { "source": 0, "start": 0, "end": 1e6 } ] })");
            MixProject clamped;
            CHECK (MixProject::load (bad, clamped));
            CHECK (approx (clamped.bpm, 20.0) && clamped.sources.size() == 1 && approx (clamped.sources[0].length, 0.0));
            CHECK (! clamped.sources[0].fileName.containsChar ('/') && clamped.sourceFile (0).isAChildOf (clamped.sourcesFolder()));
            CHECK (clamped.segments.size() == 3);
            if (clamped.segments.size() == 3)
            {
                CHECK (clamped.segments[0].transpose == 12 && approx (clamped.segments[0].gainDb, -24.0) && approx (clamped.segments[0].fadeBeats, 16.0)
                       && approx (clamped.segments[0].playBpm, 400.0));
                CHECK (approx (clamped.segments[1].start, 0.0) && approx (clamped.segments[1].playBpm, 0.0));
                // Largo de la fuente desconocido (no hay archivo): el tramo se acota a 4 horas
                CHECK (approx (clamped.segments[2].end, 4.0 * 3600.0));
            }
            // Y un mix de más de 4 horas no se renderiza (ni reserva la memoria)
            {
                juce::String err;
                const auto huge = mix::render (clamped, mix::renderSampleRate, formats, {}, {}, err);
                CHECK (huge.getNumSamples() == 0 && err.contains ("4 horas"));
            }
            // Sin largo en mix.json se mide el archivo; los tramos se acotan a la fuente
            {
                const auto noLength = mixRoot.getChildFile ("sin-largo");
                noLength.getChildFile ("fuentes").createDirectory();
                CHECK (fileA.copyFileTo (noLength.getChildFile ("fuentes").getChildFile ("fuente-a.wav")));
                noLength.getChildFile ("mix.json").replaceWithText (R"({ "name": "Y", "sources": [ { "name": "a", "file": "fuente-a.wav" } ],
                    "segments": [ { "source": 0, "start": 3.5, "end": 9 }, { "source": 0, "start": 5, "end": 9 } ] })");
                MixProject measured;
                CHECK (MixProject::load (noLength, measured));
                CHECK (measured.sources.size() == 1 && approx (measured.sources[0].length, 4.0, 1.0e-6));
                CHECK (measured.segments.size() == 1);
                if (measured.segments.size() == 1)
                    CHECK (approx (measured.segments[0].start, 3.5) && approx (measured.segments[0].end, 4.0, 1.0e-6));
                noLength.deleteRecursively();
            }
            bad.getChildFile ("mix.json").replaceWithText ("esto no es json");
            CHECK (! MixProject::load (bad, clamped));
            bad.deleteRecursively();
        }

        std::cout << "  quitar una fuente: sus tramos se van, los demás se reindexan, se borra su archivo\n";
        {
            auto p2 = proj;
            CHECK (p2.addSource (fileA, formats) == 3);
            CHECK (p2.sources[3].fileName != p2.sources[0].fileName && p2.sourceFile (3).existsAsFile());   // no pisa la otra copia
            MixSegment s3 = segA;
            s3.source = 3;
            MixSegment s2 = segA;
            s2.source = 2;
            p2.segments = { segA, segB, s3, s2, segB };
            const auto removedFile = p2.sourceFile (0);
            p2.removeSource (0);
            CHECK (p2.sources.size() == 3 && p2.sources[0].name == "fuente-b");
            CHECK (p2.segments.size() == 4);
            if (p2.segments.size() == 4)
                CHECK (p2.segments[0].source == 0 && p2.segments[1].source == 2 && p2.segments[2].source == 1 && p2.segments[3].source == 0);
            CHECK (! removedFile.exists() && p2.sourceFile (0).existsAsFile() && p2.sourceFile (2).existsAsFile());
            p2.removeSource (7);   // no existe: nada
            CHECK (p2.sources.size() == 3);
        }

        std::cout << "  lista de mixes\n";
        {
            const auto root2 = tmp.getChildFile ("lista-mixes");
            const auto beta = MixProject::create (root2, "beta");
            const auto alfa = MixProject::create (root2, "Alfa");
            const auto alfa2 = MixProject::create (root2, "Alfa");
            const auto odd = MixProject::create (root2, "../a:b");
            CHECK (beta.isDirectory() && alfa.isDirectory() && alfa2.isDirectory() && alfa2 != alfa);
            CHECK (odd.isDirectory() && odd.getParentDirectory() == root2);
            root2.getChildFile ("sin-mix").createDirectory();
            const auto names = MixProject::list (root2);
            CHECK (odd.getFileName() == "ab" && alfa2.getFileName() == "Alfa2");
            CHECK (names == juce::StringArray ({ "ab", "Alfa", "Alfa2", "beta" }));   // sin distinguir mayúsculas
            MixProject loaded;
            CHECK (MixProject::load (alfa2, loaded) && loaded.name == "Alfa");
            CHECK (MixProject::list (tmp.getChildFile ("no-existe")).isEmpty());
        }

        std::cout << "  lectura de un rango: remuestreo sin corrimiento, ceros fuera del audio\n";
        {
            juce::AudioBuffer<float> part;
            CHECK (mix::readRange (fileB, 1.2, 2.4, 44100.0, formats, part));
            CHECK (part.getNumChannels() == 2 && part.getNumSamples() == (int) std::llround (1.2 * 44100.0));
            CHECK (std::abs (peakNear (part, 44100.0, 0.3, 0.05) - 0.3) < 0.001);      // click de 1,5 s
            CHECK (std::abs (peakNear (part, 44100.0, 0.9, 0.05) - 0.9) < 0.001);      // click de 2,1 s
            CHECK (part.getMagnitude (0, (int) (0.6 * 44100.0), (int) (0.25 * 44100.0)) < 0.05f);   // entre los dos, casi nada
            CHECK (mix::readRange (fileB, 0.5, 1.6, 44100.0, formats, part));
            CHECK (part.getNumChannels() == 2 && part.getNumSamples() == (int) std::llround (1.1 * 44100.0));
            CHECK (std::abs (peakNear (part, 44100.0, 0.4, 0.05) - 0.4) < 0.001);      // click de 0,9 s
            CHECK (std::abs (peakNear (part, 44100.0, 1.0, 0.05) - 1.0) < 0.001);      // click de 1,5 s
            bool same = true;
            for (int i = 0; i < part.getNumSamples(); ++i)
                same = same && std::abs (part.getSample (0, i) - part.getSample (1, i)) < 1.0e-9f;
            CHECK (same);   // mono duplicado
            CHECK (mix::readRange (fileB, -1.0, 0.5, 44100.0, formats, part));
            CHECK (part.getNumSamples() == (int) std::llround (1.5 * 44100.0));
            CHECK (part.getMagnitude (0, (int) (0.95 * 44100.0)) < 1.0e-6f);
            CHECK (std::abs (peakNear (part, 44100.0, 1.3, 0.05) - 1.3) < 0.001);
            CHECK (mix::readRange (fileB, 3.95, 5.0, 44100.0, formats, part));
            CHECK (part.getMagnitude (0, (int) (0.06 * 44100.0), part.getNumSamples() - (int) (0.06 * 44100.0)) < 1.0e-6f);
            // Misma frecuencia: muestras exactas
            CHECK (mix::readRange (fileA, 0.25, 0.35, 44100.0, formats, part));
            CHECK (part.getNumSamples() == 4410 && std::abs (part.getSample (0, 0) - bufA.getSample (0, 11025)) < 1.0e-6f
                   && std::abs (part.getSample (1, 20) - bufA.getSample (1, 11045)) < 1.0e-6f && part.getSample (0, 0) > 0.7f);
            CHECK (! mix::readRange (mixFiles.getChildFile ("no-existe.wav"), 0.0, 1.0, 44100.0, formats, part));
        }

        std::cout << "  lectura que falla en la cola (MP3 con el largo estimado de más): silencio, sin error\n";
        {
            // Imita a MP3Reader: dice durar 2 s, pero después de 1,5 s no puede decodificar más y
            // la lectura devuelve false (con ceros en lo que falta)
            struct TailReader : juce::AudioFormatReader
            {
                explicit TailReader (juce::InputStream* sourceStream) : juce::AudioFormatReader (sourceStream, "Cola")
                {
                    sampleRate = 44100.0;
                    bitsPerSample = 32;
                    lengthInSamples = 88200;
                    numChannels = 1;
                    usesFloatingPointData = true;
                }
                bool readSamples (int* const* destChannels, int numDestChannels, int destOffset,
                                  juce::int64 firstSample, int sampleCount) override
                {
                    constexpr juce::int64 decodable = 66150;
                    for (int chIndex = 0; chIndex < numDestChannels; ++chIndex)
                        if (destChannels[chIndex] != nullptr)
                        {
                            float* dest = reinterpret_cast<float*> (destChannels[chIndex]) + destOffset;
                            for (int n = 0; n < sampleCount; ++n)
                                dest[n] = firstSample + n < decodable ? 0.5f : 0.0f;
                        }
                    return firstSample + sampleCount <= decodable;
                }
            };
            struct TailFormat : juce::AudioFormat
            {
                TailFormat() : juce::AudioFormat (juce::String ("Cola"), juce::StringArray { ".cola" }) {}
                juce::Array<int> getPossibleSampleRates() override { return { 44100 }; }
                juce::Array<int> getPossibleBitDepths() override { return { 32 }; }
                bool canDoStereo() override { return false; }
                bool canDoMono() override { return true; }
                juce::AudioFormatReader* createReaderFor (juce::InputStream* sourceStream, bool) override { return new TailReader (sourceStream); }
                using juce::AudioFormat::createWriterFor;
                juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int,
                                                          const juce::StringPairArray&, int) override { return nullptr; }
            };
            juce::AudioFormatManager tailFormats;
            tailFormats.registerFormat (new TailFormat(), true);
            const auto tailFile = mixFiles.getChildFile ("con-cola.cola");
            tailFile.replaceWithText ("x");

            juce::AudioBuffer<float> part;
            CHECK (mix::readRange (tailFile, 1.0, 2.0, 44100.0, tailFormats, part) && part.getNumSamples() == 44100);
            if (part.getNumSamples() == 44100)
            {
                CHECK (std::abs (part.getSample (0, 100) - 0.5f) < 1.0e-6f && std::abs (part.getSample (1, 100) - 0.5f) < 1.0e-6f);   // mono duplicado
                CHECK (part.getMagnitude (0, 22050, 22050) < 1.0e-9f && part.getMagnitude (1, 22050, 22050) < 1.0e-9f);
            }
            CHECK (mix::readRange (tailFile, 1.0, 2.0, 48000.0, tailFormats, part) && part.getNumSamples() == 48000);   // remuestreado
            double len = 0.0;
            const auto peaks = mix::computePeaks (tailFile, tailFormats, 10, len);
            CHECK (approx (len, 2.0) && peaks.size() == 40);
            if (peaks.size() == 40)
                CHECK (std::abs (peaks[14 * 2 + 1] - 0.5f) < 1.0e-6f && std::abs (peaks[15 * 2 + 1]) < 1.0e-9f && std::abs (peaks[19 * 2 + 1]) < 1.0e-9f);

            // Un tramo que llega a la cola se renderiza (la cola es silencio)
            MixProject tailMix;
            tailMix.folder = tmp.getChildFile ("mix-cola");
            CHECK (tailMix.addSource (tailFile, tailFormats) == 0);
            MixSegment tail;
            tail.source = 0;
            tail.start = 1.0;
            tail.end = 2.0;
            tailMix.segments = { tail };
            juce::String err;
            const auto rendered = mix::render (tailMix, mix::renderSampleRate, tailFormats, {}, {}, err);
            CHECK (err.isEmpty() && rendered.getNumSamples() == 44100);
            if (rendered.getNumSamples() == 44100)
                CHECK (std::abs (rendered.getSample (1, 11025) - 0.5f) < 1.0e-6f && rendered.getMagnitude (0, 23000, 20000) < 1.0e-9f);
        }

        std::cout << "  transiente, forma de onda\n";
        {
            const double r1 = mix::refineToOnset (fileA, 0.78, formats);
            const double r2 = mix::refineToOnset (fileA, 0.72, formats);
            CHECK (r1 <= 0.7502 && r1 > 0.745);
            CHECK (r2 <= 0.7502 && r2 > 0.745);
            CHECK (approx (mix::refineToOnset (fileA, 1.0, formats), 1.0));   // nada a ±60 ms
            CHECK (approx (mix::refineToOnset (mixFiles.getChildFile ("no-existe.wav"), 1.0, formats), 1.0));
            double len = 0.0;
            const auto peaks = mix::computePeaks (fileA, formats, 100, len);
            CHECK (std::abs (len - 4.0) < 1.0e-6 && peaks.size() == 800);
            if (peaks.size() == 800)
            {
                const float top = *std::max_element (peaks.begin(), peaks.end());
                CHECK (std::abs (top - 0.8f) < 1.0e-3f);
                CHECK (std::abs (peaks[25 * 2 + 1] - 0.8f) < 1.0e-3f && std::abs (peaks[30 * 2 + 1]) < 1.0e-9f && std::abs (peaks[30 * 2]) < 1.0e-9f);
            }
            const auto peaksB = mix::computePeaks (fileB, formats, 50, len);
            CHECK (std::abs (len - 4.0) < 1.0e-6 && peaksB.size() == 400);
            CHECK (mix::computePeaks (mixFiles.getChildFile ("no-existe.wav"), formats, 100, len).empty() && approx (len, 0.0));
        }

        std::cout << "  transponer acordes y tonalidades, análisis en JSON, la biblioteca ignora _mixes\n";
        {
            CHECK (music::transposeChord ("A#m7", 1) == "Bm7");
            CHECK (music::transposeChord ("C/E", 2) == "D/F#");
            CHECK (music::transposeChord ("N", 5) == "N");
            CHECK (music::transposeChord ("B", -1) == "A#");
            CHECK (music::transposeKey (tr ("La menor"), 2) == "Si menor");
            CHECK (music::transposeKey ("Mib mayor", -1) == "Re mayor");
            CHECK (music::transposeKey ("Mib mayor", 0) == "Mib mayor");

            Analysis an;
            an.beats = { { 0.5, 1 }, { 1.0, 2 }, { 1.5, 3 } };
            an.chords = { { 0.0, 1.2, "Am" }, { 1.2, 2.0, "G7" } };
            an.key = "Mib mayor";
            an.meter = 3;
            an.bpm = 97.5;
            const auto back = Library::analysisFromVar (juce::JSON::parse (juce::JSON::toString (Library::analysisToVar (an))));
            CHECK (back.beats.size() == 3 && approx (back.beats[1].seconds, 1.0) && back.beats[1].beatInBar == 2);
            CHECK (back.chords.size() == 2 && back.chords[1].name == "G7" && approx (back.chords[1].start, 1.2) && approx (back.chords[1].end, 2.0));
            CHECK (back.key == "Mib mayor" && back.meter == 3 && approx (back.bpm, 97.5));
            CHECK (Library::analysisFromVar (juce::var()).isEmpty());

            const auto libRoot = tmp.getChildFile ("lib-con-mixes");
            Library withMixes (libRoot);
            libRoot.getChildFile ("Cancion").createDirectory();
            withMixes.mixesFolder().createDirectory();
            writeSine (libRoot.getChildFile ("Cancion").getChildFile ("a.wav"), 44100.0, 0.5, 1);
            writeSine (withMixes.mixesFolder().getChildFile ("suelto.wav"), 44100.0, 0.5, 1);   // audio directo en _mixes
            MixProject::create (withMixes.mixesFolder(), "Un mix");
            withMixes.load();
            CHECK (withMixes.songs.size() == 1 && withMixes.songs[0].folder.getFileName() == "Cancion");
        }
    }

    std::cout << "[Mix] un corte ajustado a la transiente justo después del tiempo conserva el 1 del compás\n";
    {
        MixProject late;
        late.folder = tmp.getChildFile ("mix-corte-tarde");
        MixSource lateSrc;
        lateSrc.name = "A";
        lateSrc.fileName = "a.wav";
        lateSrc.length = 10.0;
        for (int k = 0; k < 16; ++k)
            lateSrc.analysis.beats.push_back ({ 1.0 + 0.5 * k, k % 4 + 1 });
        lateSrc.analysis.bpm = 120.0;
        late.sources.push_back (lateSrc);
        MixSegment lateSeg;
        lateSeg.source = 0;
        lateSeg.start = 1.002;   // 2 ms después del tiempo 1 (lo que deja refineToOnset)
        lateSeg.end = 5.0;
        late.segments.push_back (lateSeg);
        late.segments.push_back (lateSeg);   // el mismo tramo dos veces: la unión no duplica el 1
        SongInfo lateSong;
        mix::describeSong (late, lateSong);
        const auto& lb = lateSong.analysis.beats;
        CHECK (! lb.empty() && std::abs (lb.front().seconds) < 1.0e-9 && lb.front().beatInBar == 1);
        CHECK (std::abs (lateSong.clickOffset) < 1.0e-9);
        CHECK (lb.size() == 16);
        bool evenSpacing = lb.size() == 16;
        for (size_t k = 1; k < lb.size(); ++k)
            evenSpacing = evenSpacing && std::abs (lb[k].seconds - lb[k - 1].seconds - 0.5) < 0.003;
        CHECK (evenSpacing);
        CHECK (lb.size() == 16 && lb[8].beatInBar == 1 && lb[7].beatInBar == 4);
    }

    engine.setSong (nullptr);
    tmp.deleteRecursively();

    if (failures == 0)
        std::cout << "\nTodos los tests pasaron.\n";
    else
        std::cout << "\n" << failures << " fallo(s).\n";
    return failures == 0 ? 0 : 1;
}

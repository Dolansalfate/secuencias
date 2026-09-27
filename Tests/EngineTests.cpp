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
#include "Stretcher.h"
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
               && std::abs (back->stems[0].trigger.gainDb + 3.0f) < 1.0e-6f && back->stems[0].trigger.outputPair == 1);
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
        // Mute de la línea: silencio; solo de la línea silencia las pistas
        set->lanes[0]->control.muted = true;
        engine.seekSeconds (0.2);
        render (engine, out, 20);
        float muted = 0.0f;
        for (int b = 0; b < 12; ++b) { render (engine, out, 1); muted = std::max (muted, out.peak (0)); }
        CHECK (muted < 1.0e-3f);
        set->lanes[0]->control.muted = false;
        engine.setSamplers (nullptr);
        engine.pause();
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

    engine.setSong (nullptr);
    tmp.deleteRecursively();

    if (failures == 0)
        std::cout << "\nTodos los tests pasaron.\n";
    else
        std::cout << "\n" << failures << " fallo(s).\n";
    return failures == 0 ? 0 : 1;
}

// Tests sin interfaz ni dispositivo de audio: se llama al callback del motor a mano.
// Ejecutar:  ctest --preset debug   (o ./build/debug/SecuenciasTests_artefacts/Debug/SecuenciasTests)

#include "AudioEngine.h"
#include "Library.h"
#include <cmath>
#include <iostream>

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

    void writeSine (const juce::File& f, double rate, double seconds, int channels, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (channels, (int) (rate * seconds));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amp * std::sin ((float) i * 0.05f));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) channels, 24, {}, 0));
        os.release();
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
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
    info.markers.push_back ({ "Coro", 2.0 });
    info.markers.push_back ({ "Intro", 0.0 });
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

    std::cout << "[AudioEngine] el salto hace fundido (sin clics)\n";
    engine.setClick (false, 120.0, 0.0, 0.0f, 0);
    song->tracks[0]->muted = false;
    song->tracks[0]->outputPair = 0;
    render (engine, out, 4);
    {
        engine.seekSeconds (1.7);
        float maxJump = 0.0f, prev = out.data[0][block - 1];
        juce::AudioIODeviceCallbackContext ctx;
        for (int b = 0; b < 4; ++b)
        {
            engine.audioDeviceIOCallbackWithContext (nullptr, 0, out.ptrs.data(), 4, block, ctx);
            for (float v : out.data[0]) { maxJump = std::max (maxJump, std::abs (v - prev)); prev = v; }
        }
        CHECK (maxJump < 0.06f);   // la senoide de prueba cambia como máximo ~0.025 por muestra
    }

    engine.setSong (nullptr);
    tmp.deleteRecursively();

    if (failures == 0)
        std::cout << "\nTodos los tests pasaron.\n";
    else
        std::cout << "\n" << failures << " fallo(s).\n";
    return failures == 0 ? 0 : 1;
}

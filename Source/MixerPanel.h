#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Meters.h"

// Etiqueta que avisa al hacer clic (la casilla del trigger de cada canal abre un menú)
class ClickLabel : public juce::Label
{
public:
    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
};

// Canal del mezclador: nombre, medidor estéreo, fader en dB, nivelado, trigger, mute, solo y salida.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (std::shared_ptr<void> keepAlive, LoadedTrack&, juce::Colour);

    std::function<void()> onChanged;   // cambió algo que se guarda en song.json (ganancia, mute, salida)
    std::function<void (double db)> onLevelEdited;   // se escribió un valor en la casilla de nivelado
    std::function<void()> onTriggerClicked;          // clic en la casilla del trigger (abre el menú)

    // Casilla del trigger: qué suena en esta pista. mode: 0 = "Audio" (suena por sí misma), 1 = solo el
    // sonido del trigger, 2 = ambas, 3 = trigger sin sonido asignado (aviso).
    void setTriggerLabel (const juce::String& text, int mode);
    void setTriggerBoxVisible (bool);   // los canales del sampler no la tienen

    juce::ComboBox& outputBox() { return outBox; }
    LoadedTrack& getTrack()     { return track; }

    // Casilla de nivelado por pista: ganancia (dB) del tramo donde está el cabezal; sin nivelado, "--"
    void setLevelGain (double db, bool enabled);

    void tick();   // lee los medidores y sincroniza los botones con el estado de la pista
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 106;

private:
    void changed() { if (onChanged) onChanged(); }

    std::shared_ptr<void> keepAlive;    // mantiene viva la pista (la canción o el conjunto de samplers)
    LoadedTrack& track;
    juce::Colour colour;
    juce::Label nameLabel;
    LevelMeter meter { 2 };
    juce::Slider fader;
    juce::Label levelBox;              // nivelado del tramo (editable con un clic)
    double shownLevelDb = 0.0;
    bool levelEnabled = false;
    ClickLabel trigBox;                // trigger: "Audio", "Trig: banco", "Trig+Audio: banco"
    bool trigBoxVisible = true;
    juce::TextButton muteBtn, soloBtn;
    juce::ComboBox outBox;
};

// Canal del click: encendido, fader en dB, medidor y salida. Es el mismo click de la fila de
// controles (BPM, inicio, volumen, salida); aquí solo cambia dónde se ve y se mueve.
class ClickStrip : public juce::Component
{
public:
    ClickStrip();

    std::function<void (bool enabled, float gainDb, int outputPair)> onChanged;

    juce::ComboBox& outputBox() { return outBox; }
    void setState (bool enabled, float gainDb, int outputPair);   // desde la canción, sin avisar
    void setLevel (float peak);
    void tick() { meter.tick(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 106;

private:
    void changed();

    juce::Label nameLabel;
    LevelMeter meter { 1 };
    juce::Slider fader;
    juce::TextButton onBtn;
    juce::ComboBox outBox;
    int currentPair = 0;
};

// Canal maestro: fader de salida general y medidor de las salidas 1-2 con escala.
class MasterStrip : public juce::Component
{
public:
    MasterStrip();

    std::function<void (float gainDb)> onGainChanged;
    void setGainDb (float db);
    void setLevels (float peakL, float peakR, float rmsL, float rmsR);
    void tick() { meter.tick(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 120;

private:
    juce::Label label;
    LevelMeter meter { 2, true };
    juce::Slider fader;
};

// Panel del mezclador: los canales de la canción cargada, con desplazamiento horizontal, y el maestro.
class MixerPanel : public juce::Component
{
public:
    MixerPanel();

    std::function<void (juce::ComboBox&, int selectedPair)> fillOutputBox;   // lo pone MainComponent
    std::function<void()> onChanged;
    std::function<void (float gainDb)> onMasterGainChanged;

    std::function<void (int stemIndex, double db)> onLevelEdited;   // casilla de nivelado de un canal
    std::function<void (int track)> onTriggerClicked;               // casilla del trigger de un canal (índice de pista)
    std::function<void (bool enabled, float gainDb, int outputPair)> onClickChanged;   // canal del click

    void setClickState (bool enabled, float gainDb, int outputPair) { click.setState (enabled, gainDb, outputPair); }
    void setClickVisible (bool visible);

    void setSong (std::shared_ptr<LoadedSong>);   // nullptr = sin canales
    void setSamplers (std::shared_ptr<SamplerSet>);   // canales de los triggers (sampler), después de las pistas
    void refreshOutputs();                        // cambió el dispositivo: rehacer los ComboBox de salida
    // Nivelado por pista del tramo actual: dB por stemIndex (vacío o sin nivelado = "--")
    void setLevelGains (const std::vector<double>& dbPerStem, bool enabled);
    void setTriggerLabel (int track, const juce::String& text, int mode);   // casilla del trigger de esa pista
    void setMasterGainDb (float db) { master.setGainDb (db); }
    void tick (AudioEngine&);

    void resized() override;

private:
    juce::Viewport view;
    juce::Component holder;
    juce::OwnedArray<ChannelStrip> strips, samplerStrips;
    ClickStrip click;
    MasterStrip master;
};

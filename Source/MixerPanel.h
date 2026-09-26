#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Meters.h"

// Canal del mezclador: nombre, medidor estéreo, fader en dB, mute, solo y salida.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (std::shared_ptr<LoadedSong>, LoadedTrack&, juce::Colour);

    std::function<void()> onChanged;   // cambió algo que se guarda en song.json (ganancia, mute, salida)
    std::function<void (double db)> onLevelEdited;   // se escribió un valor en la casilla de nivelado

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

    std::shared_ptr<LoadedSong> song;   // mantiene viva la pista
    LoadedTrack& track;
    juce::Colour colour;
    juce::Label nameLabel;
    LevelMeter meter { 2 };
    juce::Slider fader;
    juce::Label levelBox;              // nivelado del tramo (editable con un clic)
    double shownLevelDb = 0.0;
    bool levelEnabled = false;
    juce::TextButton muteBtn, soloBtn;
    juce::ComboBox outBox;
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

    void setSong (std::shared_ptr<LoadedSong>);   // nullptr = sin canales
    void refreshOutputs();                        // cambió el dispositivo: rehacer los ComboBox de salida
    // Nivelado por pista del tramo actual: dB por stemIndex (vacío o sin nivelado = "--")
    void setLevelGains (const std::vector<double>& dbPerStem, bool enabled);
    void setMasterGainDb (float db) { master.setGainDb (db); }
    void tick (AudioEngine&);

    void resized() override;

private:
    juce::Viewport view;
    juce::Component holder;
    juce::OwnedArray<ChannelStrip> strips;
    MasterStrip master;
};

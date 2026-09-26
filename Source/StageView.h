#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Guía de escenario: ventana aparte para una segunda pantalla, pensada para leerse de lejos.
// Acorde actual y siguiente grandes al centro, compás y tiempo con puntos que marcan el pulso,
// tempo, tonalidad, sección actual y siguiente, título y tiempo. La alimenta el Timer de
// MainComponent (`setState`) y no toca el motor. Doble clic o F11 = pantalla completa; las demás
// teclas (pedal) se reenvían a la ventana principal.
struct StageState
{
    juce::String song, section, nextSection, chord, nextChord, key, nextSong;
    int bar = 0, beat = 0, beatsInBar = 4;
    double bpm = 0.0, position = 0.0, length = 0.0;
    double beatProgress = 0.0;   // 0..1 dentro del tiempo actual
    bool playing = false, hasAnalysis = false, hasSong = false;
};

class StageView : public juce::Component
{
public:
    StageView();

    void setState (const StageState&);

    std::function<bool (const juce::KeyPress&)> onKey;   // teclas reenviadas a la ventana principal
    std::function<void()> onToggleFullScreen;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    StageState state;
};

class StageWindow : public juce::DocumentWindow
{
public:
    explicit StageWindow (std::function<void()> onClose);

    StageView& view() { return *stageView; }
    void toggleFullScreen();
    void closeButtonPressed() override;

private:
    StageView* stageView = nullptr;
    std::function<void()> onClose;
};

#pragma once
#include "TempoTrack.h"

// Editor de la pista de tempo en una ventana aparte (como el de Cubase, Ctrl+T): la pista de compás y la de tempo en
// grande, con su propio zoom y desplazamiento, una regla de compases y una barra con lo que se edita de la sección
// elegida: su tempo (escrito), la transición hacia la siguiente (salto o curva) y botones para agregar un cambio en el
// cabezal o quitar el de la sección. No toca la canción: avisa con los callbacks de la pista y los suyos.
class TempoEditor : public juce::Component,
                    private juce::ScrollBar::Listener
{
public:
    TempoEditor();

    TempoTrackView& track() { return view; }
    void setBands (const std::vector<TempoBandView>&);
    void setAnalysis (const Analysis&);
    void setFixedGrid (double bpm, double clickOffset);
    void setLength (double seconds);
    void setPosition (double seconds);
    void setStretches (bool);
    void setReadOnly (bool);
    void selectBandAt (double seconds);   // elige la sección que suena en ese instante (al abrir)

    std::function<void (int band, int curve)> onCurveSet;        // transición de la sección hacia la siguiente
    std::function<bool (const juce::KeyPress&)> onKey;           // las teclas que no usa: a la ventana principal

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    class BarRuler;
    void setView (double start, double pps);
    void zoomToFit();
    void refreshToolbar();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    double minPps() const;
    int contentWidth() const { return juce::jmax (1, getWidth() - headerWidth); }

    static constexpr int headerWidth = 120, toolbarHeight = 40, rulerHeight = 26, scrollHeight = 12;
    TempoTrackView view { headerWidth };
    std::unique_ptr<BarRuler> ruler;
    std::vector<TempoBandView> bands;
    Analysis analysis;
    double length = 0.0, viewStart = 0.0, pps = 40.0, position = 0.0;
    bool stretches = true, readOnly = false;
    juce::Label sectionLabel, bpmLabel, curveLabel;
    juce::TextEditor bpmEdit;
    juce::ComboBox curveBox;
    juce::TextButton addBtn, removeBtn, drawBtn, fitBtn, zoomOutBtn, zoomInBtn;
    juce::ScrollBar scroll { false };
};

class TempoEditorWindow : public juce::DocumentWindow
{
public:
    explicit TempoEditorWindow (std::function<void()> onClose);
    TempoEditor& editor() { return *content; }
    void closeButtonPressed() override;

private:
    TempoEditor* content = nullptr;
    std::function<void()> onClose;
};

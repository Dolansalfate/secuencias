#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library.h"
#include "UiUtils.h"
#include <array>
#include <functional>
#include <vector>

// Una sección de tempo para dibujar, en tiempo de reproducción
struct TempoBandView
{
    double start = 0.0, end = 0.0;
    double origBpm = 0.0, playBpm = 0.0;   // al inicio de la sección
    int transpose = 0;                     // semitonos efectivos de la sección
    int curve = 0, gridCurve = 0;          // transiciones hacia la siguiente (TempoRegion::Curve)
    // Con una transición: (segundos de reproducción, BPM) a lo largo de la sección
    std::vector<juce::Point<double>> playCurve, origCurve;
};

// Pista de compás (arriba: los cambios de compás, 4/4, 17/4, 7/8...) y pista de tempo (abajo: el tempo de cada sección
// con sus transiciones, y de fondo el tempo que se detecta tiempo a tiempo), como las pistas globales de Cubase. La
// usan la vista de arreglo (sobre los carriles, con su zoom) y el editor de tempo en una ventana aparte (con el suyo).
// Arrastrar un tempo lo cambia; arrastrar un cambio lo corre; doble clic (o Dibujar) agrega uno; Borrar lo quita; clic
// derecho abre el menú de la sección. En la de compás: clic edita el cambio, arrastrarlo lo lleva a otro tiempo.
// Compacta (una fila): los compases y el tempo de cada sección en texto.
class TempoTrackView : public juce::Component
{
public:
    explicit TempoTrackView (int headerWidth);

    void setBands (const std::vector<TempoBandView>&);
    void setAnalysis (const Analysis&);                  // tiempos en tiempo de reproducción (índices = los del análisis)
    void setFixedGrid (double bpm, double clickOffset);  // sin tiempos: la rejilla fija del click (4/4)
    void setLength (double seconds);
    void setView (double startSeconds, double pixelsPerSecond);
    void setPosition (double seconds);                   // cabezal (solo si showPlayhead)
    void setTool (ui::EditTool);
    void setReadOnly (bool);
    void setStretches (bool);                            // el tempo que se edita: el de reproducción o el de la grabación
    void setCompact (bool);
    void setSelectedBand (int);
    int getSelectedBand() const { return selected; }
    void setShowsPlayhead (bool show)                    { showPlayhead = show; repaint(); }
    void setShowsEditorButton (bool show)                { editorBtn.setVisible (show); }
    double bpmAtY (int y) const;                         // para el editor (el valor bajo el mouse)

    std::function<void (double seconds)> onSeek;
    std::function<void (int band, double bpm)> onTempoSet;
    std::function<void (int band, double seconds)> onTempoMoved;
    std::function<void (double seconds, double bpm)> onTempoAdd;
    std::function<void (int band)> onTempoRemove;
    std::function<void (int band, double seconds)> onTempoMenu;      // clic derecho
    std::function<void (int band)> onBandSelected;
    std::function<void (int beat, bool popup, double seconds)> onSignatureClicked;
    std::function<void (int beat)> onSignatureAdd;
    std::function<void (int fromBeat, int toBeat)> onSignatureMoved;
    std::function<void (int beat)> onSignatureRemove;
    std::function<void (bool stretches)> onModeChanged;
    std::function<void()> onOpenEditor;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int sigHeight = 20, compactHeight = 22;

private:
    struct SigMark
    {
        int beat = -1;      // primer tiempo del cambio (-1: rejilla fija, sin tiempos)
        double seconds = 0.0;
        juce::String text;
    };
    std::vector<SigMark> signatureMarks() const;
    int signatureAt (int x) const;
    int nearestDownbeat (double t) const;
    juce::MouseCursor cursorAt (juce::Point<int>, int& band, int& sig) const;
    int edgeAt (int x) const;
    int bandAt (double t) const;
    double mainBpm (const TempoBandView&) const;
    double otherBpm (const TempoBandView&) const;
    const std::vector<juce::Point<double>>& mainCurve (const TempoBandView&) const;
    const std::vector<juce::Point<double>>& otherCurve (const TempoBandView&) const;
    double mainBpmAt (int band, double t) const;   // con su curva
    static double roundBpm (double v)          { return std::round (v * 2.0) / 2.0; }   // de medio en medio BPM
    int timeToX (double t) const               { return header + (int) std::lround ((t - viewStart) * pps); }
    double xToTime (int x) const               { return viewStart + (x - header) / pps; }
    double timeAt (int x) const                { return juce::jlimit (0.0, length, xToTime (x)); }
    int graphTop() const                       { return sigHeight + 6; }
    int graphBottom() const                    { return getHeight() - 6; }
    bool dragging() const                      { return bpmDrag >= 0 || edgeDrag >= 0 || sigDrag >= 0; }
    void computeRange();
    float yOf (double v) const;
    void paintCompact (juce::Graphics&);
    void drawCurve (juce::Graphics&, const std::vector<juce::Point<double>>&, float thickness, bool dashed);

    const int header;
    std::vector<TempoBandView> bands;
    Analysis analysis;
    std::vector<Analysis::SignatureRun> runs;
    double fixedBpm = 0.0, fixedOffset = 0.0, length = 0.0;
    double viewStart = 0.0, pps = 40.0, position = 0.0;
    ui::EditTool tool = ui::EditTool::select;
    bool readOnly = false, stretches = true, compact = false, showPlayhead = false;
    int selected = -1;
    std::array<juce::MouseCursor, (size_t) ui::numEditTools> cursors;
    juce::TextButton mode, editorBtn;
    double lo = 100.0, hi = 140.0;
    int bpmDrag = -1, edgeDrag = -1, sigDrag = -1, hoverBand = -1, hoverSig = -1;
    double previewBpm = 0.0, grabBpm = 0.0, previewTime = 0.0;
    int grabY = 0;
    bool moved = false;
};

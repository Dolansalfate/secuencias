#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Library.h"

// Vista de arreglo: regla con marcadores, compases y tiempo; una pista horizontal con forma de
// onda por stem (cabecera con nombre, mute y solo); cabezal; loop sombreado; zoom con
// Ctrl+rueda y desplazamiento con la rueda o la barra. Nunca toca el motor: recibe la posición
// desde el Timer de MainComponent y avisa por callbacks.
class TimelineView : public juce::Component,
                     private juce::ScrollBar::Listener
{
public:
    TimelineView();
    ~TimelineView() override;

    void setSong (std::shared_ptr<LoadedSong>, double sampleRate);   // nullptr = sin canción
    void setLoadingText (const juce::String&);                          // texto cuando no hay canción
    void setMarkers (const std::vector<SongMarker>&, double bpm, double clickOffset);
    // Secciones de tempo en tiempo de reproducción, para la fila de tempo de la regla
    struct TempoBand
    {
        double start = 0.0, end = 0.0;
        double origBpm = 0.0, playBpm = 0.0;   // distintos = sección estirada
    };
    void setTempoBands (const std::vector<TempoBand>&);
    // Tramos del arreglo (audio) en tiempo de reproducción: bordes sobre los carriles y huecos sombreados
    struct ClipView
    {
        double start = 0.0, end = 0.0;
    };
    void setClips (const std::vector<ClipView>&);
    void setAnalysis (const Analysis&);   // tiempos y acordes detectados (vacío = rejilla fija por BPM)
    void setLevelingEnabled (bool);       // mostrar la ganancia de nivelado de cada tramo
    void setPosition (double seconds);
    void setPlaying (bool);
    void setLoop (double startSeconds, double endSeconds);   // end <= start = sin loop
    void refreshTrackStates();                                // mute y solo desde los atómicos de cada pista
    void zoomToFit();

    std::function<void (double seconds)> onSeek;
    std::function<void (int index, bool popup)> onMarkerClicked;
    std::function<void (int index, double seconds)> onMarkerMoved;
    std::function<void (int index, double seconds)> onTempoBandClicked;   // clic en la fila de tempo
    std::function<void (int beatIndex)> onBeatClicked;                     // clic derecho en un tiempo de la fila de compases
    std::function<void (int chordIndex, double seconds)> onChordClicked;   // clic derecho en la fila de acordes (-1 = sin acorde ahí)
    std::function<void (double seconds, int lane)> onLaneMenu;                        // clic derecho sobre un carril (menú del audio)
    std::function<void (double seconds, double deltaSeconds, int lane)> onClipDragged; // Shift + arrastre sobre un carril: mover el tramo
    std::function<void (int track, bool state)> onMute, onSolo;
    // Ganancia de nivelado (lineal) de un stem en un instante de reproducción, para dibujar la onda como sonará
    std::function<float (int stemIndex, double seconds)> levelGainAt;
    void refreshWaveforms();   // cambió el nivelado: volver a dibujar las ondas

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int headerWidth = 130, rulerHeight = 82, scrollHeight = 12;
    static constexpr int gutter = 10;              // reserva para la barra vertical de los carriles
    static constexpr int minLaneHeight = 44, maxLaneHeight = 120;

private:
    class Ruler;
    class Lane;
    class Overlay;

    double lengthSeconds() const;
    int contentWidth() const        { return juce::jmax (1, getWidth() - headerWidth - gutter); }
    double visibleSeconds() const   { return contentWidth() / pixelsPerSecond; }
    int timeToX (double t) const    { return (int) std::lround ((t - viewStart) * pixelsPerSecond); }   // 0 = borde izquierdo del contenido
    double xToTime (int x) const    { return viewStart + x / pixelsPerSecond; }
    double minPixelsPerSecond() const;
    void setView (double start, double pps);
    void invalidateLanes();
    void updateScrollBar();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void seekAt (int contentX, bool scrubbing);
    void repaintOverlay();

    std::shared_ptr<LoadedSong> song;
    double sampleRate = 44100.0;
    std::vector<SongMarker> markers;
    std::vector<TempoBand> tempoBands;
    std::vector<ClipView> clips;
    bool clipDragging = false;                     // Shift + arrastre en curso sobre un tramo
    double clipDragFrom = 0.0, clipDragDelta = 0.0;
    int clipDragLane = 0;
    Analysis analysis;
    bool levelingEnabled = false;
    double bpm = 120.0, clickOffset = 0.0;
    double position = 0.0, loopStart = 0.0, loopEnd = 0.0;
    bool playing = false;
    double viewStart = 0.0, pixelsPerSecond = 40.0;
    juce::uint32 lastScrubMs = 0;
    int lastChord = -1;

    std::unique_ptr<Ruler> ruler;
    juce::Viewport laneView;                       // desplazamiento vertical cuando hay muchas pistas
    juce::Component laneHolder;
    juce::OwnedArray<Lane> lanes;
    std::unique_ptr<Overlay> overlay;
    juce::ScrollBar scrollBar { false };
    juce::Label emptyLabel;
};

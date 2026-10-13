#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "Library.h"
#include "UiUtils.h"
#include "TempoTrack.h"
#include <array>
#include <map>

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
    // Secciones de tempo en tiempo de reproducción, para la pista de tempo
    using TempoBand = TempoBandView;
    void setTempoBands (const std::vector<TempoBand>&);
    // Notas de texto (en tiempo de reproducción), banderitas en su fila de la regla
    struct NoteView
    {
        double seconds = 0.0;
        juce::String text;
    };
    void setNotes (const std::vector<NoteView>&);
    // Tramos del arreglo (audio) en tiempo de reproducción: bordes sobre los carriles y huecos sombreados
    struct ClipView
    {
        double start = 0.0, end = 0.0;
        bool muted = false;            // silenciado (herramienta Silenciar): onda gris
        juce::uint32 colour = 0;       // color propio (herramienta Color); 0 = el de la pista
        double fadeIn = 0.0, fadeOut = 0.0;   // fundidos (segundos de reproducción)
        float gainDb = 0.0f;           // ganancia del tramo
    };
    void setClips (const std::vector<ClipView>&);
    // Pistas con su propio arreglo (cortadas o movidas solas), en el orden de los carriles, con sus tramos en tiempo de
    // reproducción. Si hay alguna, cada carril dibuja sus bordes y huecos (los suyos o los de la canción) en vez de la
    // capa de encima.
    struct LaneClips
    {
        bool own = false;
        std::vector<ClipView> clips;
    };
    void setLaneClips (const std::vector<LaneClips>&);
    void setAnalysis (const Analysis&);   // tiempos y acordes detectados (vacío = rejilla fija por BPM)
    void setLevelingEnabled (bool);       // mostrar la ganancia de nivelado de cada tramo
    void setPosition (double seconds);
    void setPlaying (bool);
    void setLoop (double startSeconds, double endSeconds);   // end <= start = sin loop
    void refreshTrackStates();                                // mute y solo desde los atómicos de cada pista
    void setTriggerStates (const std::vector<bool>& perTrack);            // botón T de cada carril
    struct TriggerMark
    {
        double seconds = 0.0;
        float velocity = 1.0f;
    };
    void setTriggerMarks (const std::vector<std::vector<TriggerMark>>& perTrack);   // golpes detectados (tiempo de reproducción)

    // Pistas MIDI (golpes congelados y editables, una fila por sonido), en tiempo de reproducción. Cada una
    // va bajo el carril de su pista de origen (afterTrack = índice de pista en la canción cargada; -1 = al final).
    struct MidiLaneView
    {
        juce::String name;
        int afterTrack = -1;
        bool muted = false;
        struct Pad
        {
            juce::String name;
            int note = 36;
            juce::String sound;        // texto corto del sonido ("Bombo grabado", "Addictive Drums", "sin sonido")
            bool muted = false;
        };
        std::vector<Pad> pads;
        struct Hit
        {
            double seconds = 0.0;
            int note = 36;
            int velocity = 100;        // 1..127
        };
        std::vector<Hit> hits;         // mismo orden (e índices) que MidiTrack::hits
    };
    void setMidiLanes (const std::vector<MidiLaneView>&);   // conserva la selección de cada pista si sus índices siguen existiendo
    void selectMidiHits (int midiTrack, const std::vector<int>& hits);   // selección tras una edición (reemplaza la anterior)
    std::vector<int> getSelectedMidiHits (int midiTrack) const;
    bool deleteSelectedMidiHits();   // Supr: avisa onMidiDelete de la pista con selección; false si no había ninguna

    void zoomToFit();
    void zoomToRange (double start, double end);
    // Modo reproducción: sin edición (sin menús, sin mover marcadores ni tramos, sin tocar las pistas MIDI); clic en
    // la regla o en un carril sigue saltando, y un clic en un marcador va a él
    void setReadOnly (bool);
    bool isReadOnly() const { return readOnly; }
    // Herramienta de los carriles, como la caja de herramientas de Cubase (ui::EditTool). En todas, Shift alcanza a
    // todas las pistas (el tramo de la canción) en vez de solo la del carril, y el clic derecho abre el menú del tramo.
    // - Objeto: clic elige el tramo (Ctrl suma o quita), arrastrar mueve los elegidos, arrastrar un borde lo recorta,
    //   clic en un hueco lleva el cabezal; Alt + clic corta.
    // - Rango: arrastrar marca un rango en los carriles que se recorren; clic lo quita y lleva el cabezal.
    // - Cortar: una guía muestra dónde caerá el corte; clic corta. Unir, Borrar, Silenciar y Color: clic en un tramo.
    // - Zoom: clic acerca, Alt + clic aleja, arrastrar muestra ese tramo. Reproducir: suena mientras se mantiene.
    // - Warp: arrastrar una línea de la rejilla la lleva a donde está el golpe en el audio. Dibujar: en la pista de
    //   tempo, de compás o MIDI agrega un cambio o un golpe.
    void setTool (ui::EditTool);
    ui::EditTool getTool() const { return tool; }
    void showCutGuide (double seconds, int lane, bool allTracks);   // la guía de la tijera (captura); seconds < 0 la quita
    void dragForCapture (int lane, double fromSeconds, double toSeconds, juce::ModifierKeys);   // un arrastre sobre el carril
    void clickForCapture (int lane, double seconds, juce::ModifierKeys);                         // un clic sobre el carril

    // Tramos elegidos con Objeto: de la pista de un carril, o de toda la canción (lane = -1); tiempo de reproducción
    struct ClipPick
    {
        int lane = -1;
        double start = 0.0, end = 0.0;
    };
    void setClipSelection (std::vector<ClipPick>);
    const std::vector<ClipPick>& getClipSelection() const { return clipSelection; }
    // Rango marcado con la herramienta Rango, en los carriles firstLane..lastLane (-1 = todos)
    struct Range
    {
        double start = 0.0, end = 0.0;
        int firstLane = -1, lastLane = -1;
        bool isEmpty() const { return end <= start; }
        bool allLanes() const { return firstLane < 0; }
        bool hasLane (int lane) const { return allLanes() || (lane >= firstLane && lane <= lastLane); }
    };
    void setRange (Range);
    Range getRange() const { return range; }

    // Pista de compás y pista de tempo (las pistas globales de Cubase), sobre los carriles: desplegada (el gráfico) o
    // plegada en una fila (los compases y el tempo de cada sección en texto)
    void setTempoTrackVisible (bool expanded);
    bool isTempoTrackVisible() const { return tempoTrackVisible; }
    void setTempoStretches (bool);   // arrastrar el tempo estira el audio (true) o solo mueve el click (false)
    bool tempoStretchesAudio() const { return tempoStretches; }

    std::function<void (double seconds)> onSeek;
    std::function<void (int index, bool popup)> onMarkerClicked;
    std::function<void (int index, double seconds)> onMarkerMoved;
    std::function<void (int index, double seconds)> onTempoBandClicked;   // clic en la fila de tempo
    std::function<void (int beatIndex)> onBeatClicked;                     // clic derecho en un tiempo de la fila de compases
    std::function<void (int noteIndex, double seconds)> onNoteClicked;     // clic derecho en la fila de notas (-1 = hueco)
    std::function<void (int chordIndex, double seconds)> onChordClicked;   // clic derecho en la fila de acordes (-1 = sin acorde ahí)
    std::function<void (double seconds, int lane)> onLaneMenu;                        // clic derecho sobre un carril (menú del audio)
    // Tijera: dónde caería un corte en ese instante del carril (según el modo de corte; tiempo de reproducción) y el
    // corte en sí (seconds = donde se hizo clic, sin ajustar)
    std::function<double (double seconds, int lane)> snapCut;
    std::function<void (double seconds, int lane, bool allTracks)> onCut;
    // Objeto: los elegidos se arrastraron `delta` (primary = el que se tomó, grab = el instante donde se tomó); un borde
    // de un tramo se llevó a `edge`
    std::function<void (std::vector<ClipPick> picks, int primary, double grabSeconds, double deltaSeconds)> onPicksMoved;
    std::function<void (ClipPick clip, bool startEdge, double edgeSeconds)> onClipTrimmed;
    // Asas del tramo con Objeto: el largo de un fundido (segundos de reproducción) y la ganancia (dB)
    std::function<void (ClipPick clip, bool fadeIn, double seconds)> onClipFade;
    std::function<void (ClipPick clip, double gainDb)> onClipGain;
    // Unir, Borrar, Silenciar, Color y Dibujar (en un carril de audio): clic en ese instante del carril
    std::function<void (ui::EditTool, double seconds, int lane, bool allTracks)> onClipTool;
    std::function<void (double seconds, int lane)> onRangeMenu;         // clic derecho dentro del rango
    std::function<void (double seconds)> onPlayToolDown;                // Reproducir: desde ahí mientras se mantiene
    std::function<void()> onPlayToolUp;
    std::function<void (int beat, double seconds)> onBeatWarped;        // Warp: el tiempo `beat` va a ese instante
    // Pista de tempo (índices de setTempoBands) y de compás (índices de los tiempos del análisis)
    std::function<void (bool visible)> onTempoTrackToggled;
    std::function<void()> onOpenTempoEditor;                            // botón "Editor" de la pista de tempo
    std::function<void (bool stretches)> onTempoModeChanged;
    std::function<void (int band, double bpm)> onTempoSet;              // arrastre vertical de una sección
    std::function<void (int band, double seconds)> onTempoMoved;        // arrastre del cambio de tempo (inicio de la sección)
    std::function<void (double seconds, double bpm)> onTempoAdd;        // doble clic o Dibujar
    std::function<void (int band)> onTempoRemove;                       // Borrar sobre un cambio de tempo
    std::function<void (int beat, bool popup, double seconds)> onSignatureClicked;   // un cambio de compás (-1 = ninguno ahí)
    std::function<void (int beat)> onSignatureAdd;                      // doble clic o Dibujar: el primer tiempo del compás
    std::function<void (int fromBeat, int toBeat)> onSignatureMoved;
    std::function<void (int beat)> onSignatureRemove;                   // Borrar sobre un cambio de compás
    std::function<void (int track, bool state)> onMute, onSolo, onTrigger;
    std::function<void (int track)> onLaneHeaderMenu;   // clic derecho en la cabecera de un carril (trigger)
    // Pistas MIDI (índices de golpe = índices en MidiLaneView::hits; tiempos de reproducción)
    std::function<void (int midiTrack, double seconds, int note)> onMidiAdd;                                   // doble clic en un hueco de una fila
    std::function<void (int midiTrack, std::vector<int> hits, double deltaSeconds, int rowDelta)> onMidiMove;  // arrastre de golpes elegidos (el arrastrado primero)
    std::function<void (int midiTrack, std::vector<int> hits, int velocityDelta)> onMidiVelocity;              // Alt + arrastre vertical
    std::function<void (int midiTrack, std::vector<int> hits)> onMidiDelete;                                   // Supr o menú
    std::function<void (int midiTrack, std::vector<int> hits, int padRow, double seconds)> onMidiMenu;       // clic derecho en el contenido
    std::function<void (int midiTrack, int padRow)> onMidiPadMenu;                                            // clic derecho en el nombre de una fila
    std::function<void (int midiTrack, int padRow)> onMidiAudition;                                           // botón "Escuchar" de una fila
    std::function<void (int midiTrack)> onMidiHeaderMenu;                                                     // clic derecho en el título de la pista MIDI
    std::function<void (int midiTrack, bool muted)> onMidiMute;                                               // botón M de la pista MIDI
    // Ajuste de un golpe que se agrega o se mueve (el dueño aplica el modo de corte); sin callback, libre
    std::function<double (int midiTrack, double seconds, int note)> snapMidiTime;
    // Ganancia de nivelado (lineal) de un stem en un instante de reproducción, para dibujar la onda como sonará
    std::function<float (int stemIndex, double seconds)> levelGainAt;
    void refreshWaveforms();   // cambió el nivelado: volver a dibujar las ondas

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int headerWidth = 130, rulerHeight = 80, scrollHeight = 12, tempoLaneHeight = 78;
    static constexpr int gutter = 10;              // reserva para la barra vertical de los carriles
    static constexpr int minLaneHeight = 44, maxLaneHeight = 120;

private:
    class Ruler;
    class Lane;
    class MidiLane;
    class Overlay;

    void layoutLanes();                                       // carriles de audio y pistas MIDI dentro de laneHolder
    void clearMidiSelectionExcept (const MidiLane* keep);     // la selección de golpes vive en una sola pista MIDI
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
    std::vector<NoteView> notes;
    std::vector<ClipView> clips;
    std::vector<LaneClips> laneClips;              // por carril (setLaneClips)
    bool readOnly = false;
    ui::EditTool tool = ui::EditTool::select;
    std::array<juce::MouseCursor, (size_t) ui::numEditTools> toolCursors;   // uno por herramienta (se crean con la vista)
    double cutGuide = -1.0;                        // guía de la tijera en tiempo de reproducción (-1 = sin guía)
    int cutGuideLane = -1;
    bool cutGuideAll = false;                      // Shift: el corte alcanza a todas las pistas
    void setCutGuide (double seconds, int lane, bool allTracks);
    bool anyOwnLane() const;
    // Tramos de un carril (los suyos o los de la canción) o de la canción; sin cortes, uno con toda la canción
    std::vector<ClipView> clipList (int lane, bool song) const;
    static int clipIndexAt (const std::vector<ClipView>&, double seconds);
    juce::Rectangle<int> laneArea (int firstLane, int lastLane) const;   // en coordenadas de la capa de encima
    int laneIndexAtY (int yInView) const;                                 // carril bajo esa altura (de la vista), o -1
    int beatNear (double seconds, int pixels) const;                      // tiempo del análisis a menos de esos px, o -1
    // Gestos de las herramientas sobre un carril (contentX: desde el borde del contenido)
    void toolMouseDown (int lane, int contentX, juce::ModifierKeys, int yInLane = 100);
    void toolMouseDrag (int lane, juce::Point<int> inView, int dragDistanceX);
    void toolMouseUp (int contentX);
    juce::MouseCursor toolHover (int lane, int contentX, juce::ModifierKeys, int yInLane = 100);
    // Asa bajo el mouse (arriba en el tramo): 1 fundido de entrada, 2 de salida, 3 ganancia; 0 ninguna
    int handleAt (const std::vector<ClipView>&, int contentX, int yInLane, int& clipIndex) const;
    void toolHoverExit();
    int edgeNear (const std::vector<ClipView>&, int contentX, bool& startEdge) const;   // tramo con un borde a <= 5 px
    std::vector<ClipPick> clipSelection;
    Range range;
    // Gesto en curso con una herramienta sobre un carril
    enum class Gesture { none, seek, move, trim, range, zoom, warp, play, fade, gain };
    Gesture gesture = Gesture::none;
    int gestureLane = -1, gesturePrimary = -1, gestureBeat = -1;
    double gestureFrom = 0.0, gestureNow = 0.0;
    bool gestureMoved = false, gestureAll = false, trimStartEdge = false;
    ClipPick trimTarget;
    ClipView handleClip;                           // el tramo cuya asa se arrastra
    bool fadeIsIn = true;
    int gainGrabY = 0;
    double previewValue = 0.0;                     // largo del fundido o dB mientras se arrastra
    int hoverBeat = -1;                            // Warp: la línea de la rejilla bajo el mouse
    // Alto propio de cada carril (arrastrando el borde de abajo de su cabecera), por nombre de pista; 0 = automático.
    // Zoom vertical de la onda (Alt + rueda), para todos.
    std::map<juce::String, int> laneHeights;
    float waveZoom = 1.0f;
    int resizingLane = -1, resizeGrabY = 0, resizeStartH = 0;
public:
    void laneResizeBegin (int lane, int yInView);
    void laneResizeDrag (int yInView);
    void laneResizeEnd();
    void laneResizeReset (int lane);
private:
    // Pista de compás y de tempo
    std::unique_ptr<TempoTrackView> tempoLane;
    juce::TextButton tempoToggle;
    bool tempoTrackVisible = true, tempoStretches = true;
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
    juce::OwnedArray<MidiLane> midiLanes;          // una por MidiLaneView, en el orden de setMidiLanes
    bool midiNewRender = false;                    // setSong desde el último setMidiLanes: los tiempos pudieron cambiar todos
    std::unique_ptr<Overlay> overlay;
    juce::ScrollBar scrollBar { false };
    juce::Label emptyLabel;
};

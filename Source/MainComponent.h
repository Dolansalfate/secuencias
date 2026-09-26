#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "Library.h"
#include "Separator.h"
#include "Analyzer.h"
#include "Loudness.h"
#include "TempoMap.h"
#include "Stretcher.h"
#include "Arrangement.h"
#include "TimelineView.h"
#include "MixerPanel.h"
#include "FilePicker.h"

class MarkerButton;

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ListBoxModel,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void mouseUp (const juce::MouseEvent&) override;   // clic en la etiqueta de tonalidad

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

    // Cierra la app; si hay una canción sonando, pide confirmación primero.
    void requestQuit();
    // Carga la canción de esa fila del setlist (como un clic en la lista).
    void selectSong (int row) { if (juce::isPositiveAndBelow (row, (int) library.songs.size())) setlist.selectRow (row); }
    bool isLoading() const    { return currentIndex >= 0 && currentSong == nullptr; }
    bool isAnalyzing() const  { return analyzer.getState() == Analyzer::State::running; }
    bool isLeveling() const   { return levelPool.getNumJobs() > 0; }
    void analyzeCurrentSong() { startAnalysis(); }
    void levelCurrentSong()   { measureAndLevel(); }
    bool isRendering() const  { return rendering || tempoDirtySince != 0; }
    void editForCapture (double cutSeconds, double moveMs);   // corta los stems y desplaza el tramo que sigue (herramienta de captura)
    void setCutModeForCapture (int mode)                      { cutModeBox.setSelectedId (juce::jlimit (1, 3, mode), juce::dontSendNotification); }
    void separateCurrentSong();                               // separa la canción seleccionada y reemplaza sus pistas por los stems
    void openImportPickerForCapture()                         { chooseStems(); }
    void setSeparationOptionsForCapture (int stems, int quality)   // sin guardar en los ajustes
    {
        if (stems > 0)   stemsBox.setSelectedId (juce::jlimit (1, 2, stems), juce::dontSendNotification);
        if (quality > 0) qualityBox.setSelectedId (juce::jlimit (1, 3, quality), juce::dontSendNotification);
    }
    bool isSeparating() const                                 { return separator.getState() == Separator::State::running; }
    void setTempoForCapture (double playBpm, int transpose)
    {
        if (auto* info = currentInfo())
        {
            info->playBpm = std::abs (playBpm - originalBpm()) < 0.05 ? 0.0 : playBpm;   // igual que el control de tempo
            info->transpose = transpose;
            tempoControlsFromSong();
            requestRender();
        }
    }

private:
    // ListBoxModel (setlist)
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Canciones
    SongInfo* currentInfo();
    void loadSongAt (int index);
    void songLoaded (std::shared_ptr<LoadedSong> rendered, std::shared_ptr<LoadedSong> source, std::shared_ptr<LoadedSong> arranged,
                     int generation, const juce::String& error);
    void saveCurrentMix();
    void markSongDirty();
    void unloadSong();
    void nextSong (int delta);
    void songMenu (int row);
    void refreshSetlist();
    void restoreSetlistSelection();
    void afterFadeOut (std::function<void()> action, int waitedMs = 0);

    // Vista de arreglo, mezclador, marcadores, click, loop
    void syncTimelineMarkers();
    void setLiveMode (bool);
    void rebuildMarkers();
    void clickControlsFromSong();
    void applyClick();   // manda el click de la canción actual al motor
    void pushClick();    // applyClick + guardar
    void updateLoopRegion();
    void sectionAt (double seconds, double& start, double& end, juce::String& name) const;
    void jumpToMarker (int index);
    void addMarkerHere();
    void markerMenu (int index);
    void tempoBandMenu (int index, double playbackSeconds);   // menú de una sección de tempo (fila de la regla)
    void chordMenu (int index, double playbackSeconds);       // menú de un acorde de la regla (-1 = hueco)
    void sectionRange (const SongInfo&, int region, double& startSeconds, double& endSeconds) const;   // límites de una sección en tiempo original
    void ensureTempoRegions (SongInfo&) const;                 // sin secciones: crea la única, con el tempo original
    void fillOutputBox (juce::ComboBox&, int selectedPair);
    int numOutputPairs() const;

    // Análisis musical
    void startAnalysis();
    void applyAnalysis (const Analysis&);
    void updateBeatGrid();
    void showChordAndKey (double positionSeconds);
    void keyMenu();                    // enarmonía (sostenidos o bemoles) y tonalidad a mano
    void beatMenu (int beatIndex);     // métrica: primer tiempo de compás, compases de N tiempos, quitar o insertar un tiempo
    bool useFlats() const;             // escritura de notas de la canción actual
    juce::String effectiveKey() const; // tonalidad elegida o detectada, sin reescribir
    void analysisEdited();             // guarda y refresca regla, click y cabecera tras editar tiempos
    juce::String analysisPythonPath() const;

    // Tempo y tono (fase 3): el motor reproduce una versión renderizada de la canción original
    void rebuildTimeMap();                         // mapa de la versión que está sonando
    std::vector<SongMarker> mappedMarkers() const;     // marcadores en tiempo de reproducción
    Analysis mappedAnalysis() const;               // tiempos y acordes en tiempo de reproducción
    std::vector<TimelineView::TempoBand> mappedTempoBands() const;   // secciones de tempo en tiempo de reproducción
    double originalBpm() const;
    // Tempo original y de reproducción de la sección que contiene ese instante (segundos originales)
    void tempoAt (double originalSeconds, double& origBpm, double& playBpm) const;
    void tempoControlsFromSong();
    void requestRender();                          // con retardo, desde el Timer
    void renderTempo();
    void songRendered (std::shared_ptr<LoadedSong> rendered, std::shared_ptr<LoadedSong> arranged, TimeMap, int transpose,
                       std::vector<Clip> clips, int generation);
    double songLengthSeconds() const;              // largo de la línea de tiempo de la canción (arreglo, sin estirar)

    // Arreglo (fase 4): cortar, mover, eliminar y unir tramos de audio; deshacer
    std::vector<TimelineView::ClipView> mappedClips() const;
    void clipMenu (double playbackSeconds, int lane);
    void clipDragged (double playbackSeconds, double deltaSeconds, int lane);
    int cutMode() const { return cutModeBox.getSelectedId(); }          // 1 libre, 2 a la rejilla, 3 a la transiente
    double nearestGridTime (double songSeconds) const;                   // tiempo detectado (o de la rejilla fija) más cercano
    double onsetNear (double songSeconds, int lane, double window) const; // transiente en la pista del carril (o en la mezcla); -1 si no hay
    double snapCutTime (double songSeconds, int lane, juce::String& how) const;   // según el modo de corte
    void arrangementEdited();                      // guarda, refresca la vista y pide el render
    void pushUndo();                               // copia de la canción antes de una edición (grilla o audio)
    void undoLastEdit();
    void refreshFromInfo();                        // vuelve a aplicar SongInfo a la UI y al motor tras deshacer

    // Nivelado de sonoridad (EBU R128)
    // Medición por stem: sonoridad de toda la canción y de cada tramo, y las ganancias por tramo propuestas
    struct StemLevel
    {
        int stemIndex = -1;
        double whole = unmeasuredDb;
        std::vector<double> sections, gainsDb;
    };
    void measureAndLevel();                        // mide la canción actual por tramos (por stem y la suma) y aplica las ganancias
    int sectionIndexAt (double songSeconds) const; // 0 = antes del primer marcador, i + 1 = tramo del marcador i
    std::vector<double> sectionStemGains (const SongInfo&, int section) const;   // dB por stem del tramo
    float levelGainAt (int stemIndex, double playbackSeconds) const;             // ganancia lineal total (general x pista)
    void stemLevelEdited (int stemIndex, double db);                             // casilla del mezclador
    void remeasureIfLeveling();                    // tras cambiar marcadores: si el nivelado está activo, vuelve a medir
    static bool needsLevelMeasure (const SongInfo&);   // hay tramos sin medir
    void applyLeveling (const loudness::Result&, const std::vector<StemLevel>&);
    void recomputeSectionGains (SongInfo&) const;  // a partir de las mediciones guardadas y el objetivo
    void updateGainCurve();
    void levelSetlist();                           // mide todas las canciones y las empareja
    double levelTargetLufs() const;

    // Importar / separar / ajustes
    void chooseStems();
    void chooseSongToSeparate();
    // Selector de archivos: nativo en macOS; en Linux y Windows, un panel dentro de la ventana (FilePicker)
    void pickFiles (const juce::String& title, int browserFlags, const juce::File& startDir, const juce::String& patterns,
                    std::function<void (const juce::Array<juce::File>&)> onDone);
    void importStems (const juce::Array<juce::File>&);
    void startSeparation (const juce::File&);
    void showAudioSettings();
    void showAiSettings();
    juce::File installerScript() const;   // scripts/instalar-ia.sh: en el bundle (macOS), /opt/secuencias, ~/.local/share o el árbol de desarrollo
    void launchInstaller();               // abre una terminal con el instalador de los motores de IA
    juce::String pythonPath() const;
    SeparationOptions selectedOptions() const;
    void askText (const juce::String& title, const juce::String& initial,
                  std::function<void (const juce::String&)> onOk);

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    mutable juce::ApplicationProperties props;
    AudioEngine engine;
    Library library;
    Separator separator;
    Analyzer analyzer;

    std::shared_ptr<LoadedSong> currentSong;   // lo que suena (renderizado si hay tempo o tono)
    std::shared_ptr<LoadedSong> sourceSong;    // los archivos originales, para volver a renderizar
    std::shared_ptr<LoadedSong> arrangedSong;  // el arreglo (tramos colocados), en la línea de tiempo de la canción
    std::vector<Clip> renderedClips;           // tramos con los que se renderizó lo que suena
    std::vector<SongInfo> undoStack;           // estados anteriores de la canción (hasta 30)
    TimeMap timeMap;                     // original <-> reproducción de lo que suena
    bool renumberToEnd = false;          // el menú del tiempo renumera hasta el final de la canción (no solo de la sección)
    int renderedTranspose = 0;
    int renderGeneration = 0;
    bool rendering = false;
    juce::int64 tempoDirtySince = 0;
    juce::File loadedFolder;             // carpeta de la canción que está en currentSong
    int currentIndex = -1;
    std::atomic<int> loadGeneration { 0 };
    juce::int64 songDirtySince = 0;      // 0 = nada pendiente de guardar
    double sepProgress = 0.0;
    double lastLength = -1.0;
    double loopStartSec = 0.0, loopEndSec = 0.0;
    bool liveMode = false;               // oculta la vista de arreglo y agranda la información
    juce::Array<int> heldKeys;           // teclas pulsadas, para ignorar la autorrepetición

    // --- UI ---
    juce::TextButton importBtn, separateBtn, audioBtn, aiBtn, cancelSepBtn;
    juce::ComboBox stemsBox, qualityBox;
    juce::ProgressBar sepBar { sepProgress };
    juce::Label sepLabel;

    juce::Label setlistTitle;
    juce::TextButton levelSetlistBtn;
    juce::ListBox setlist;

    juce::Label songTitle, nextLabel, sectionLabel, timeLabel, chordLabel, keyLabel;
    juce::TextButton prevBtn, playBtn, stopBtn, nextBtn, liveBtn, zoomFitBtn;
    juce::ToggleButton loopBtn;
    juce::Slider positionSlider;

    juce::TextButton addMarkerBtn;
    juce::ComboBox cutModeBox;                 // modo de corte y de encaje al mover tramos
    juce::OwnedArray<MarkerButton> markerButtons;

    juce::ToggleButton clickBtn, levelBtn;
    juce::TextButton analyzeBtn;
    juce::ComboBox targetBox;

    juce::Label tempoLabel, transposeLabel, tempoInfoLabel;
    juce::Slider tempoSlider, transposeSlider;
    juce::TextButton tempoOrigBtn;
    juce::Label bpmLabel, offsetLabel, clickVolLabel;
    juce::Slider bpmSlider, offsetSlider, clickGainSlider;
    juce::ComboBox clickOutBox;

    TimelineView timeline;
    MixerPanel mixer;

    juce::Label helpLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<FilePicker> picker;
    juce::File separationTarget;               // carpeta de la canción cuyas pistas reemplazará la separación en curso (vacío = canción nueva)

    std::atomic<bool> abortJobs { false };
    juce::ThreadPool loaderPool { 1 };   // último: se destruye primero
    juce::ThreadPool levelPool { 1 };    // mediciones de sonoridad (no bloquean la carga de canciones)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

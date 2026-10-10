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
#include "StageView.h"
#include "Recorder.h"
#include "Instruments.h"
#include "MixProject.h"
#include "MixEditor.h"
#include "MidiTracks.h"
#include <map>
#include <set>

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
    // Corta los stems y desplaza el tramo que sigue (herramienta de captura); con lane >= 0, solo esa pista
    void editForCapture (double cutSeconds, double moveMs, int lane = -1);
    void setCutModeForCapture (int mode)                      { cutModeBox.setSelectedId (juce::jlimit (1, 3, mode), juce::dontSendNotification); }
    void separateCurrentSong();                               // separa la canción seleccionada y reemplaza sus pistas por los stems
    void separateDrumsOfCurrentSong();                        // parte la batería de la canción seleccionada en bombo, caja, toms, hi-hat, ride y crash
    void setDrumPartsForCapture (bool on)                     { drumPartsBtn.setToggleState (on, juce::dontSendNotification); }
    void setTriggerForCapture (const juce::String& bank);     // activa el trigger de la batería con ese banco (herramienta de captura)
    void setInstrumentForCapture (const juce::String& path);  // carga ese plugin en el rack y lo pone como trigger de la batería (captura)
    bool isLoadingInstruments() const { return rack != nullptr && rack->isLoading(); }
    // Herramienta de captura: abre (o crea) el mix `name` en _mixes, le agrega esas fuentes si no las
    // tiene, y opcionalmente arma tramos de prueba (8 compases de cada fuente desde su 3.er compás) y
    // crea la canción (createMode 1 = separar con IA, 2 = sin separar)
    void openMixForCapture (const juce::String& name, const juce::StringArray& sourceFiles, bool autoSegments, int createMode,
                            int selectFirstBar = -1, int selectLastBar = -1, bool stemsPanel = false, bool separate = false);
    bool isMixBusy() const;   // la captura espera a que termine lo que pidió del mix
    void createMidiTrackForCapture();   // pista MIDI de la batería, con una fila de conga y los golpes de 5 a 10 s en ella
    void openImportPickerForCapture()                         { chooseStems(); }
    void showStage (bool show);                               // ventana de guía de escenario (segunda pantalla)
    juce::Image stageSnapshot();                              // captura de la guía (herramienta de captura)
    double positionForCapture() const                         { return engine.getPositionSeconds(); }
    void seekForCapture (double seconds)                      { engine.seekSeconds (seconds); }
    void updateStage (double playbackSeconds);                // alimenta la guía de escenario desde el Timer
    void duplicateForCapture (double seconds);                // duplica el tramo que contiene ese instante (herramienta de captura)
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
                     std::shared_ptr<SamplerSet> samplers, int generation, const juce::String& error);
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
    // Notas de texto (guía de escenario)
    void addNoteHere();
    void editNote (int index, double songSeconds);      // index -1 = nueva en songSeconds
    void noteMenu (int index, double playbackSeconds);
    void activeNote (double playbackSeconds, juce::String& now, juce::String& next, double& nextIn) const;
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
    void currentChords (double playbackSeconds, juce::String& now, juce::String& next) const;   // acorde actual y siguiente, escritos según la canción
    void barAndBeat (double playbackSeconds, int& bar, int& beat, int& beatsInBar, double& progress) const;   // compás y tiempo en curso
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
                       std::vector<Clip> clips, std::vector<TrackClips> trackClips, std::shared_ptr<SamplerSet> samplers, int generation);

    // Triggers (punto 1): golpes de las pistas que disparan bancos de muestras
    struct BankCache
    {
        juce::CriticalSection lock;
        std::map<juce::String, std::shared_ptr<SampleBankData>> banks;   // "nombre@sr"
    };
    static std::shared_ptr<SamplerSet> buildSamplers (LoadedSong& rendered, const SongInfo&, double sampleRate,
                                                      const juce::File& banksFolder, juce::AudioFormatManager&, BankCache&);
    static std::vector<TriggerSettings> triggersOf (const SongInfo&);
    static juce::String triggerLabelFor (const TriggerSettings&, bool hasSound, const juce::String& instrumentName, int& mode);   // casilla del mezclador
    void refreshReplaced();   // LoadedTrack::replaced según el modo del trigger y si su sonido (banco o instrumento) existe
    void applySamplers (std::shared_ptr<SamplerSet>);   // al motor, al mezclador y a las marcas de la regla
    void syncTriggerMarks();
    void triggerMenu (int track);
    void setTriggerEnabled (int track, bool on);
    void importBankFor (int track);
    void triggerSettingsDialog (int track);

    // Pistas MIDI: golpes congelados y editables (una fila por sonido), en SongInfo::midiTracks
    void applyMidiTracks();                                   // a la vista, al motor y al mezclador (al instante)
    void pullMidiControls();                                  // fader, mute y salida de los canales de las filas a SongInfo
    // La fila de `info` de la que salió una línea de midiSamplers: misma canción, pista (índice y archivo de origen), fila
    // y nota; nullptr si ya no existe (otra canción, fila quitada, nota cambiada)
    MidiPad* padForLane (SongInfo& info, const SamplerLane& lane) const;
    bool arrangementReady();                                  // el arreglo que suena es el de SongInfo (si no, avisa y false)
    static bool isMidiSource (const SongInfo&, const StemInfo&);   // de este stem salió una pista MIDI (reemplaza su trigger en vivo)
    void midiEdited (int midiTrack, const std::vector<int>& selection);   // guardar, aplicar y conservar la selección
    void createMidiTrack (int track);                         // desde el menú del trigger de una pista
    void midiHeaderMenu (int midiTrack);
    void midiHeaderMenuAddPad (int midiTrack);
    void midiPadMenu (int midiTrack, int padIndex);
    // "Escuchar": un golpe de la fila (o del trigger del stem) ya, aunque esté detenido; avisa qué sonó o por qué no
    void auditionMidiPad (int midiTrack, int padIndex);
    void auditionTrigger (int stem);
    void midiHitsMenu (int midiTrack, std::vector<int> hits, int row, double playbackSeconds);
    double snapMidiTime (int midiTrack, double playbackSeconds, int note);   // según el modo de corte
    int trackIndexForFile (const juce::String& fileName);     // pista cargada con ese archivo, o -1
    std::shared_ptr<SampleBankData> bankFor (const juce::String& bankName);   // de la caché, o se carga
    juce::String soundLabel (const juce::String& sound);
    void importBank (std::function<void (const juce::String& bankName)> onDone);

    // Grabación de entradas (punto 2): una pista nueva alineada con la canción, o golpes para un banco
    struct Recording
    {
        std::unique_ptr<juce::TimeSliceThread> thread;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        juce::File tempFile;         // toma cruda (sin alinear)
        juce::File songFolder;       // canción a la que se agrega la pista
        juce::String name;           // nombre de la pista nueva o del banco
        bool toBank = false;         // destino: banco de muestras; si no, pista nueva de la canción
        int bankTrack = -1;          // pista cuyo trigger usará el banco nuevo (-1 = ninguna)
        double extraOffsetMs = 0.0;  // compensación manual además de la latencia del dispositivo
        bool active = false;
        juce::uint32 startedAt = 0;
    };
    void recordDialog (bool toBank, int bankTrack);      // entrada, nombre, escucha, compensación
    bool ensureInputsEnabled (juce::String& problem);    // activa las dos primeras entradas si no hay ninguna
    void startRecording (int inputL, int inputR, bool toBank, int bankTrack, const juce::String& name, int monitorPair, double extraOffsetMs);
    void stopRecording (bool discard);                   // cierra la toma y la guarda (alineada o como banco) en el hilo de carga
    void recordingFinished (bool ok, const juce::String& message, const juce::String& fileName, const juce::String& name,
                            bool toBank, int bankTrack, const juce::String& bankName, const juce::File& songFolder);

    // Armar mix (antes de separar): sección que reemplaza la vista de la canción mientras está abierta
    void mixMenu();                                   // botón "Armar mix": nuevo, abrir, borrar, cerrar
    void openMix (const juce::File& folder);
    void closeMix (bool reloadSong);                  // reloadSong: vuelve a cargar la canción que estaba
    void mixEdited();                                 // el editor cambió el proyecto: guardar, deshacer, render viejo
    void undoMixEdit();
    void addMixSources();
    void addMixSourceFiles (const juce::Array<juce::File>&);   // copia en segundo plano y agrega (también al arrastrar)
    void removeMixSource (int source);
    int mixSourceIndex (const juce::String& fileName) const;
    void queueMixAnalysis (const juce::String& fileName);
    void startNextMixAnalysis();
    void mixSourceLoadedForAnalysis (std::shared_ptr<LoadedSong>, const juce::String& fileName);
    void loadMixPeaks (const juce::String& fileName);
    void playMixSource (int source, double from);
    void playMix (double from);
    void setMixPreview (std::shared_ptr<LoadedSong>, int kind, const juce::String& sourceFile, const Analysis& grid, double from,
                        int request, int version);   // version: la del render del mix (kind 2) o -1
    void dropMixPreview();                            // descarta lo que había para escuchar (p. ej. cambió la frecuencia)
    void applyMixPreviewGrid (const Analysis&);
    void renderMix (std::function<void (bool ok)> then);   // renderiza si el render quedó viejo
    void stopMixPreview();
    void createSongFromMix();
    void startMixSong (bool separate, const juce::String& name);
    void applyMixSongInfo (int songIndex, const SongInfo& meta);
    // Canción ligada a un mix: se separan (una vez) las canciones originales que falten, en cola, y después se arman
    // sus pistas con los tramos del mix (mix::renderStems). Al cargar una canción ligada cuyo mix cambió, sus pistas
    // se vuelven a armar (sin separar de nuevo) y lo suyo se reubica por tramo (mix::applyRebuild).
    void continueMixSongJob();
    void mixSongJobFailed();
    // "Separar canciones" del armado de mix: separa las canciones que usa (las que falten, con las opciones de la barra
    // de arriba) para los ajustes por pista de sus tramos, con la misma cola (MixSongJob::separateOnly)
    void separateMixSources();
    void noteMixStems (const juce::File& mixFolder, const juce::String& key);   // el mix usa esas pistas si no tenía otras
    SeparationOptions effectiveOptions (SeparationOptions) const;
    struct MixSongBuild
    {
        juce::File mixFolder;
        juce::File songFolder;          // canción a actualizar; vacío = crear una nueva
        juce::String name;              // nombre de la canción nueva
        bool separated = true;
        juce::String options;           // separationKey
    };
    void buildMixSong (const MixSongBuild&);
    struct MixSongResult
    {
        MixSongBuild build;
        bool ok = false;
        juce::String error;
        SongInfo meta;                  // describeSong de la versión armada
        std::vector<MixPiece> pieces;
        juce::String signature;
        juce::File tempFolder;          // las pistas armadas (se mueven a la canción)
        bool missingStems = false;      // a una canción del mix le faltan sus pistas (se separa en la próxima carga)
    };
    void mixSongBuilt (const MixSongResult&);
    void applyMixUpdate (const MixSongResult&, bool reloadIfCurrent);
    int songIndexForFolder (const juce::File&) const;
    bool linkedSongNeedsRebuild (int index);   // true: hay que volver a armarla antes de cargarla (ver loadSongAt)
    void openLinkedMix (int index);
    void unlinkSong (int index);
    // Menú de tramos en una canción ligada (abrir su mix o editar aquí, que la desliga tras preguntar y llama a
    // `afterUnlink`); true si lo mostró
    bool linkedSongMenu (const SongInfo&, std::function<void()> afterUnlink = {});
    void confirmUnlink (int index, std::function<void()> afterUnlink);
    static juce::String separationKey (const SeparationOptions&);
    static SeparationOptions optionsFromKey (const juce::String&);
    bool startSeparationWith (const juce::File& file, const juce::String& songName, const SeparationOptions&);
    void updateMixTimer();
    void mixCaptureStep();
    struct MixSnapshot
    {
        double bpm = 0.0;
        bool keepTempos = false;
        std::vector<MixSegment> segments;
        std::vector<Analysis> analyses;   // una por fuente (el editor puede editar los tiempos)
    };
    MixSnapshot mixSnapshot() const;

    // Instrumentos VST3/AU (punto 3): rack global; menú del botón "Instrumentos" y búsqueda de plugins
    void instrumentsMenu();
    void scanInstruments (const juce::FileSearchPath& extraFolders);
    double songLengthSeconds() const;              // largo de la línea de tiempo de la canción (arreglo, sin estirar)

    // Arreglo (fase 4): cortar, mover, eliminar y unir tramos de audio; deshacer
    std::vector<TimelineView::ClipView> mappedClips() const;
    std::vector<TimelineView::LaneClips> mappedLaneClips() const;   // pistas con arreglo propio, por carril
    const StemInfo* stemForLane (int lane) const;                   // la pista del carril (lo que suena), o nullptr
    static juce::StringArray stemFilesOf (const SongInfo&);         // archivos de los stems por índice (render del arreglo)
    void clipMenu (double playbackSeconds, int lane);
    // Una edición de una sola pista (ids 21 a 29 del menú del carril) en el instante `songSeconds`
    void trackClipAction (int action, double songSeconds, const juce::String& stemFile);
    // Portapapeles de tramos: el tramo copiado con la grilla (tiempos y acordes) de su rango y el tempo de su sección
    struct ClipClipboard
    {
        bool valid = false;
        juce::File folder;                  // canción de la que se copió (solo se pega en ella: los tramos son de su audio)
        Clip clip;
        arrangement::GridSlice grid;
        std::vector<TempoRegion> regions;   // secciones de tempo del rango, con inicio relativo al tramo
        juce::String stem;                  // copiado de una sola pista (se pega solo en ella); vacío = de toda la canción
        std::vector<TrackClips> trackParts; // de toda la canción: lo que sonaba en las pistas con arreglo propio (relativo)
    };
    void copyClip (int index);
    void pasteClipboard (double songSeconds, bool insert);   // insert: abre espacio (audio y grilla); si no, superpone
    void duplicateClip (int index);                          // copia y pega insertando justo después del tramo
    void clipDragged (double playbackSeconds, double deltaSeconds, int lane, bool trackOnly);
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
    bool startSeparation (const juce::File&, const juce::String& songName);   // false si no pudo empezar (avisa)
    void pickFiles (const juce::String& title, int browserFlags, const juce::File& startDir, const juce::String& patterns,
                    std::function<void (const juce::Array<juce::File>&)> onDone);
    void importStems (const juce::Array<juce::File>&);
    void startSeparation (const juce::File&);
    void showAudioSettings();
    void showAiSettings();
    juce::File installerScript() const;   // scripts/instalar-ia.sh: en el bundle (macOS), /opt/secuencias, ~/.local/share o el árbol de desarrollo
    void launchInstaller (bool withRoformer = false);   // abre una terminal con el instalador de los motores de IA
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
    std::vector<TrackClips> renderedTrackClips;   // y los arreglos propios de las pistas
    juce::File midiSamplersFolder;             // canción para la que se armaron las líneas de midiSamplers
    std::shared_ptr<SamplerSet> samplers;      // triggers de lo que suena
    std::shared_ptr<SamplerSet> midiSamplers;  // filas con sonido de las pistas MIDI de lo que suena
    std::vector<TriggerSettings> renderedTriggers;
    BankCache bankCache;
    Recording recording;
    std::unique_ptr<InstrumentRack> rack;      // se crea tras abrir el dispositivo (necesita la frecuencia)

    // Armar mix
    std::unique_ptr<MixProject> mixProject;
    std::unique_ptr<MixEditor> mixEditor;
    Analyzer mixAnalyzer;                          // tiempos y compases de las fuentes (aparte del de la canción)
    std::vector<juce::String> mixAnalysisQueue;    // archivos de fuentes por analizar
    juce::String mixAnalyzingFile;                 // la que se está leyendo o analizando
    juce::String mixAnalyzerFile;                  // para la que se arrancó mixAnalyzer (vacío mientras solo se lee): sus estados finales son de ella
    std::atomic<int> mixGeneration { 0 };          // cambia al abrir o cerrar un mix: invalida los trabajos viejos
    int mixVersion = 0, mixRenderedVersion = -1;   // ediciones del proyecto y versión del último render
    bool mixRendering = false;
    int mixCopyJobs = 0;                           // copias de fuentes en curso (la captura las espera)
    SongInfo mixRenderedInfo;                      // describeSong del último render (tiempos para el click)
    std::shared_ptr<LoadedSong> mixPreviewSong;    // lo que suena en el motor mientras el mix está abierto
    int mixPreviewKind = 0, mixPreviewVersion = -1;   // 0 nada, 1 una fuente, 2 el mix
    juce::String mixPreviewSourceFile;             // la fuente que suena (por archivo: los índices cambian al quitar)
    int mixPlayRequest = 0;                        // cada pedido de escucha y cada Stop lo cambian: una carga vieja no arranca sola
    int mixWantedPlay = 0;                         // pedido de escuchar el mix que llegó mientras se preparaba (suena al terminar)
    double mixWantedFrom = 0.0;
    bool mixClick = false;
    juce::File mixReturnFolder;                    // canción que estaba cargada al abrir el mix (por carpeta: el setlist puede cambiar)
    std::vector<MixSnapshot> mixUndo;
    MixSnapshot mixLastSnapshot;
    // Fuentes de un mix que se separan en cola para una canción ligada (crearla o actualizarla)
    struct MixSongJob
    {
        bool active = false;
        MixSongBuild build;
        SeparationOptions options;
        juce::StringArray pending;                 // archivos de fuentes/ por separar
        juce::String current;                      // el que se está separando
        int total = 0;
        juce::String signature;                    // actualizando: la versión del mix (si falla, no se reintenta sola)
        bool separateOnly = false;                 // solo separar (para los ajustes por pista del mix): no se arma canción
    } mixSongJob;
    int mixSongBuilds = 0;                         // pistas de canciones ligadas armándose (mixSongPool)
    std::set<juce::String> mixSongBuildingFolders; // canciones que se están actualizando (no se encola otra igual)
    std::unique_ptr<MixSongResult> pendingMixUpdate;   // actualización lista para la canción que suena (al detenerla)
    std::set<juce::String> failedMixRebuilds;      // carpeta|huella que no se pudo armar (no se reintenta en bucle)
    struct MixCapture
    {
        bool active = false, autoSegments = false, segmentsDone = false, createStarted = false, selectionDone = true;
        bool renderStarted = false;   // con --mixtramos también se prepara el mix (se ve el nivelado de cada tramo)
        bool stemsPanel = false, stemsPanelShown = false;   // --mixpistas: "Pistas del tramo" del tramo elegido
        bool separate = false, separateStarted = false;     // --mixseparar: "Separar canciones (IA)"
        int createMode = 0, selFirst = -1, selLast = -1;
    } mixCapture;
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
    juce::TextButton importBtn, separateBtn, mixBtn, audioBtn, aiBtn, instrumentsBtn, cancelSepBtn;
    juce::ComboBox stemsBox, qualityBox;
    juce::ToggleButton drumPartsBtn;           // "Batería en partes" al separar
    juce::ProgressBar sepBar { sepProgress };
    juce::Label sepLabel;

    juce::Label setlistTitle;
    juce::TextButton levelSetlistBtn;
    juce::ListBox setlist;

    juce::Label songTitle, nextLabel, sectionLabel, timeLabel, chordLabel, keyLabel;
    juce::TextButton prevBtn, playBtn, stopBtn, nextBtn, recordBtn, liveBtn, zoomFitBtn;
    juce::ToggleButton loopBtn;
    juce::Slider positionSlider;

    juce::TextButton addMarkerBtn, addNoteBtn;
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
    std::unique_ptr<StageWindow> stageWindow;
    juce::TextButton stageBtn;
    juce::File separationTarget;               // carpeta de la canción cuyas pistas reemplazará la separación en curso (vacío = canción nueva)
    int separationStemIndex = -1;              // >= 0: solo esa pista (la batería) se reemplaza por el resultado
    ClipClipboard clipboard;

    std::atomic<bool> abortJobs { false };
    juce::ThreadPool loaderPool { 1 };   // último: se destruye primero
    juce::ThreadPool levelPool { 1 };    // mediciones de sonoridad (no bloquean la carga de canciones)
    juce::ThreadPool mixPool { 1 };      // mix: copias de fuentes, formas de onda y lecturas para analizar (no demoran la escucha)
    juce::ThreadPool mixSongPool { 1 };  // pistas de canciones ligadas a un mix (no demoran la carga de otras canciones)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

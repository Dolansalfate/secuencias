#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class SecuenciasApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }   // la define CMake

    // Una sola instancia en uso normal. Con --captura se permite otra: es una herramienta de
    // desarrollo que abre la ventana, la renderiza a un PNG y sale.
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameters().contains ("--captura"); }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow>();

        // --captura=/ruta/imagen.png [--cancion=N] [--vivo] [--analizar]: carga la canción N del setlist
        // (desde 1), espera a que termine de cargar (y de analizar, si se pide), guarda la ventana como PNG y sale
        // Banderas exactas: "--separar" no debe coincidir con "--separarbateria"
        juce::StringArray tokens;
        tokens.addTokens (commandLine, " ", "\"");
        auto hasFlag = [&tokens] (const char* flag) { return tokens.contains (flag); };
        if (commandLine.contains ("--captura="))
        {
            const auto path = commandLine.fromFirstOccurrenceOf ("--captura=", false, false)
                                         .upToFirstOccurrenceOf (" ", false, false).unquoted();
            const bool live = hasFlag ("--vivo");
            captureAnalyze = hasFlag ("--analizar");
            captureLevel = hasFlag ("--nivelar");
            captureSeparate = hasFlag ("--separar");
            captureSelector = hasFlag ("--selector");
            captureDrumParts = hasFlag ("--bateria");
            captureSeparateDrums = hasFlag ("--separarbateria");
            if (commandLine.contains ("--trigger="))
                captureTriggerBank = commandLine.fromFirstOccurrenceOf ("--trigger=", false, false).upToFirstOccurrenceOf (" ", false, false).unquoted();
            if (commandLine.contains ("--instrumento="))
                captureInstrument = commandLine.fromFirstOccurrenceOf ("--instrumento=", false, false).upToFirstOccurrenceOf (" ", false, false).unquoted();
            // Armar mix: --mix=<nombre> [--mixfuentes=/a.wav,/b.wav] [--mixtramos] [--mixcancion=1|2]
            if (commandLine.contains ("--mix="))
                captureMix = commandLine.fromFirstOccurrenceOf ("--mix=", false, false).upToFirstOccurrenceOf (" ", false, false).unquoted();
            if (commandLine.contains ("--mixfuentes="))
                captureMixSources.addTokens (commandLine.fromFirstOccurrenceOf ("--mixfuentes=", false, false)
                                                        .upToFirstOccurrenceOf (" ", false, false).unquoted(), ",", "");
            captureMixSegments = hasFlag ("--mixtramos");
            captureMidiTrack = hasFlag ("--pistamidi");
            if (commandLine.contains ("--mixcancion="))
                captureMixCreate = commandLine.fromFirstOccurrenceOf ("--mixcancion=", false, false).getIntValue();
            if (commandLine.contains ("--posicion="))
                capturePosition = commandLine.fromFirstOccurrenceOf ("--posicion=", false, false).getDoubleValue();
            if (commandLine.contains ("--escenario="))
                captureStageFile = juce::File (commandLine.fromFirstOccurrenceOf ("--escenario=", false, false)
                                                   .upToFirstOccurrenceOf (" ", false, false).unquoted());
            if (commandLine.contains ("--calidad="))
                captureQuality = commandLine.fromFirstOccurrenceOf ("--calidad=", false, false).getIntValue();
            if (commandLine.contains ("--pistas="))
                captureStems = commandLine.fromFirstOccurrenceOf ("--pistas=", false, false).getIntValue();
            if (commandLine.contains ("--tempo="))
                captureTempo = commandLine.fromFirstOccurrenceOf ("--tempo=", false, false).getDoubleValue();
            if (commandLine.contains ("--tono="))
                captureTranspose = commandLine.fromFirstOccurrenceOf ("--tono=", false, false).getIntValue();
            if (commandLine.contains ("--corte="))
                captureCut = commandLine.fromFirstOccurrenceOf ("--corte=", false, false).getDoubleValue();
            if (commandLine.contains ("--desplazar="))
                captureMoveMs = commandLine.fromFirstOccurrenceOf ("--desplazar=", false, false).getDoubleValue();
            if (commandLine.contains ("--duplicar="))
                captureDuplicate = commandLine.fromFirstOccurrenceOf ("--duplicar=", false, false).getDoubleValue();
            if (commandLine.contains ("--modocorte="))
                captureCutMode = commandLine.fromFirstOccurrenceOf ("--modocorte=", false, false).getIntValue();
            if (commandLine.contains ("--cancion="))
                if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    mc->selectSong (commandLine.fromFirstOccurrenceOf ("--cancion=", false, false).getIntValue() - 1);
            captureFile = juce::File (path);
            captureLive = live;
            captureTicks = 0;
            captureTimer.startTimer (250);
        }
    }

    void shutdown() override                       { mainWindow = nullptr; }
    void systemRequestedQuit() override            { quit(); }

    // Espera a que la canción termine de cargar (más un segundo para las formas de onda), con tope de 2 minutos
    void captureTick()
    {
        auto* mc = mainWindow != nullptr ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent()) : nullptr;
        if (mc != nullptr && ! mc->isLoading() && ! captureAnalysisStarted)
        {
            captureAnalysisStarted = true;
            if (captureAnalyze) mc->analyzeCurrentSong();
            if (captureLevel)   mc->levelCurrentSong();
            if (captureQuality > 0 || captureStems > 0) mc->setSeparationOptionsForCapture (captureStems, captureQuality);
            if (captureDrumParts) mc->setDrumPartsForCapture (true);
            if (captureTriggerBank.isNotEmpty()) mc->setTriggerForCapture (captureTriggerBank);
            if (captureInstrument.isNotEmpty()) mc->setInstrumentForCapture (captureInstrument);
            if (captureMix.isNotEmpty()) mc->openMixForCapture (captureMix, captureMixSources, captureMixSegments, captureMixCreate);
            if (captureMidiTrack) mc->createMidiTrackForCapture();
            if (captureSeparate) mc->separateCurrentSong();
            if (captureSeparateDrums) mc->separateDrumsOfCurrentSong();
            if (captureSelector) mc->openImportPickerForCapture();   // el selector de archivos dentro de la ventana
            if (capturePosition > 0.0) mc->seekForCapture (capturePosition);
            if (captureStageFile != juce::File()) mc->showStage (true);
            if (captureTempo > 0.0 || captureTranspose != 0) mc->setTempoForCapture (captureTempo, captureTranspose);
            if (captureCutMode > 0) mc->setCutModeForCapture (captureCutMode);
            if (captureCut > 0.0) mc->editForCapture (captureCut, captureMoveMs);
            if (captureDuplicate > 0.0) mc->duplicateForCapture (captureDuplicate);
        }
        const bool ready = mc == nullptr || (! mc->isLoading() && captureAnalysisStarted && ! mc->isAnalyzing() && ! mc->isLeveling()
                                             && ! mc->isRendering() && ! mc->isSeparating() && ! mc->isLoadingInstruments()
                                             && ! mc->isMixBusy());
        if ((ready && ++captureTicks >= 4) || captureTicks++ > 2400)   // tope: 10 min (los renders largos)
        {
            captureTimer.stopTimer();
            if (mainWindow != nullptr)
            {
                mainWindow->saveSnapshot (captureFile, captureLive);
                if (captureStageFile != juce::File())
                    if (auto* mc2 = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    {
                        mc2->updateStage (mc2->positionForCapture());
                        const auto image = mc2->stageSnapshot();
                        captureStageFile.deleteFile();
                        if (auto stream = captureStageFile.createOutputStream())
                            juce::PNGImageFormat().writeImageToStream (image, *stream);
                    }
            }
            quit();
        }
        else if (! ready)
            captureTicks = 0;
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow ("Secuencias", juce::Colour (0xff15191d), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1000, 640, 5000, 4000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* c = getContentComponent())
                c->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            // Con una canción sonando, MainComponent pide confirmación antes de salir
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                mc->requestQuit();
            else
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        void saveSnapshot (const juce::File& file, bool liveMode)
        {
            auto* content = getContentComponent();
            if (content == nullptr)
                return;
            if (liveMode)
                content->keyPressed (juce::KeyPress (juce::KeyPress::F11Key));
            const auto image = content->createComponentSnapshot (content->getLocalBounds());
            file.deleteFile();
            if (auto stream = file.createOutputStream())
                juce::PNGImageFormat().writeImageToStream (image, *stream);
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    struct CaptureTimer : public juce::Timer
    {
        explicit CaptureTimer (SecuenciasApp& a) : app (a) {}
        void timerCallback() override { app.captureTick(); }
        SecuenciasApp& app;
    };

    std::unique_ptr<MainWindow> mainWindow;
    CaptureTimer captureTimer { *this };
    juce::File captureFile, captureStageFile;
    juce::String captureTriggerBank, captureInstrument, captureMix;
    juce::StringArray captureMixSources;
    bool captureMixSegments = false, captureMidiTrack = false;
    int captureMixCreate = 0;
    bool captureLive = false, captureAnalyze = false, captureLevel = false, captureSeparate = false, captureSelector = false, captureAnalysisStarted = false;
    bool captureDrumParts = false, captureSeparateDrums = false;
    double captureTempo = 0.0, captureCut = 0.0, captureMoveMs = 0.0, captureDuplicate = 0.0, capturePosition = 0.0;
    int captureTranspose = 0, captureCutMode = 0, captureQuality = 0, captureStems = 0;
    int captureTicks = 0;
};

START_JUCE_APPLICATION (SecuenciasApp)

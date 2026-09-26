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
        if (commandLine.contains ("--captura="))
        {
            const auto path = commandLine.fromFirstOccurrenceOf ("--captura=", false, false)
                                         .upToFirstOccurrenceOf (" ", false, false).unquoted();
            const bool live = commandLine.contains ("--vivo");
            captureAnalyze = commandLine.contains ("--analizar");
            captureLevel = commandLine.contains ("--nivelar");
            captureSeparate = commandLine.contains ("--separar");
            captureSelector = commandLine.contains ("--selector");
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
            if (captureSeparate) mc->separateCurrentSong();
            if (captureSelector) mc->openImportPickerForCapture();   // el selector de archivos dentro de la ventana
            if (captureTempo > 0.0 || captureTranspose != 0) mc->setTempoForCapture (captureTempo, captureTranspose);
            if (captureCutMode > 0) mc->setCutModeForCapture (captureCutMode);
            if (captureCut > 0.0) mc->editForCapture (captureCut, captureMoveMs);
            if (captureDuplicate > 0.0) mc->duplicateForCapture (captureDuplicate);
        }
        const bool ready = mc == nullptr || (! mc->isLoading() && captureAnalysisStarted && ! mc->isAnalyzing() && ! mc->isLeveling()
                                             && ! mc->isRendering() && ! mc->isSeparating());
        if ((ready && ++captureTicks >= 4) || captureTicks++ > 2400)   // tope: 10 min (los renders largos)
        {
            captureTimer.stopTimer();
            if (mainWindow != nullptr)
                mainWindow->saveSnapshot (captureFile, captureLive);
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
    juce::File captureFile;
    bool captureLive = false, captureAnalyze = false, captureLevel = false, captureSeparate = false, captureSelector = false, captureAnalysisStarted = false;
    double captureTempo = 0.0, captureCut = 0.0, captureMoveMs = 0.0, captureDuplicate = 0.0;
    int captureTranspose = 0, captureCutMode = 0, captureQuality = 0, captureStems = 0;
    int captureTicks = 0;
};

START_JUCE_APPLICATION (SecuenciasApp)

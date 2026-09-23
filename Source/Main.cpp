#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class SecuenciasApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Secuencias"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { mainWindow = std::make_unique<MainWindow>(); }
    void shutdown() override                       { mainWindow = nullptr; }
    void systemRequestedQuit() override            { quit(); }

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

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SecuenciasApp)

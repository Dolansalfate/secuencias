#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Selector de archivos dentro de la propia ventana: un panel sobre la vista con un
// FileBrowserComponent y botones Aceptar / Cancelar. No abre otra ventana, así que no depende
// del gestor de ventanas (en GNOME, las ventanas nuevas de JUCE y de zenity se abrían detrás de
// la principal por la prevención de robo de foco y, al ser modales, la app parecía colgada).
class FilePicker : public juce::Component,
                   private juce::FileBrowserListener
{
public:
    // flags: los de juce::FileBrowserComponent (openMode, canSelectFiles, canSelectDirectories,
    // canSelectMultipleItems). patterns: "*.wav;*.flac" (vacío = todos). onDone recibe la
    // selección (vacía si se canceló); con canSelectDirectories y nada seleccionado, la carpeta abierta.
    FilePicker (const juce::String& title, int flags, const juce::File& initialDirectory,
                const juce::String& patterns, std::function<void (const juce::Array<juce::File>&)> onDone);
    ~FilePicker() override;

    void accept();
    void cancel();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}   // absorbe los clics fuera del panel

private:
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}
    juce::Array<juce::File> results() const;
    void finish (bool ok);

    int flags;
    juce::WildcardFileFilter filter;
    juce::FileBrowserComponent browser;
    juce::Label titleLabel;
    juce::TextButton okBtn, cancelBtn;
    std::function<void (const juce::Array<juce::File>&)> onDone;
    bool finished = false;
};

#include "FilePicker.h"
#include "UiUtils.h"
#include "Library.h"

FilePicker::FilePicker (const juce::String& title, int f, const juce::File& initialDirectory,
                        const juce::String& patterns, std::function<void (const juce::Array<juce::File>&)> done)
    : flags (f),
      filter (patterns.isEmpty() ? juce::String ("*") : patterns, "*", "Archivos"),
      browser (f, initialDirectory.isDirectory() ? initialDirectory : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
               &filter, nullptr),
      onDone (std::move (done))
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setFont (ui::font (16.0f, true));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);
    browser.addListener (this);
    addAndMakeVisible (browser);
    okBtn.setButtonText ("Aceptar");
    okBtn.onClick = [this] { accept(); };
    okBtn.setColour (juce::TextButton::buttonColourId, ui::accent.darker (0.5f));
    addAndMakeVisible (okBtn);
    cancelBtn.setButtonText ("Cancelar");
    cancelBtn.onClick = [this] { cancel(); };
    addAndMakeVisible (cancelBtn);
    setWantsKeyboardFocus (true);
}

FilePicker::~FilePicker()
{
    browser.removeListener (this);
}

void FilePicker::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto panel = browser.getBounds().getUnion (titleLabel.getBounds()).getUnion (okBtn.getBounds()).expanded (12);
    g.setColour (ui::panel);
    g.fillRoundedRectangle (panel.toFloat(), 8.0f);
    g.setColour (ui::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (panel.toFloat(), 8.0f, 1.0f);
}

void FilePicker::resized()
{
    auto r = getLocalBounds();
    const int w = juce::jmin (r.getWidth() - 40, 900), h = juce::jmin (r.getHeight() - 40, 620);
    auto panel = juce::Rectangle<int> (w, h).withCentre (r.getCentre()).reduced (12);
    titleLabel.setBounds (panel.removeFromTop (28));
    panel.removeFromTop (6);
    auto buttons = panel.removeFromBottom (30);
    cancelBtn.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    okBtn.setBounds (buttons.removeFromRight (110));
    panel.removeFromBottom (8);
    browser.setBounds (panel);
}

bool FilePicker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { cancel(); return true; }
    if (key == juce::KeyPress::returnKey) { accept(); return true; }
    return false;
}

void FilePicker::fileDoubleClicked (const juce::File& file)
{
    if (file.existsAsFile() && (flags & juce::FileBrowserComponent::canSelectFiles) != 0)
        accept();   // doble clic en un archivo = elegirlo; en una carpeta, el navegador entra en ella
}

juce::Array<juce::File> FilePicker::results() const
{
    juce::Array<juce::File> out;
    for (int i = 0; i < browser.getNumSelectedFiles(); ++i)
    {
        const auto f = browser.getSelectedFile (i);
        const bool dir = f.isDirectory(), fileOk = f.existsAsFile() && (flags & juce::FileBrowserComponent::canSelectFiles) != 0;
        const bool dirOk = dir && (flags & juce::FileBrowserComponent::canSelectDirectories) != 0;
        if (fileOk || dirOk)
            out.add (f);
    }
    if (out.isEmpty() && (flags & juce::FileBrowserComponent::canSelectDirectories) != 0 && browser.getRoot().isDirectory())
        out.add (browser.getRoot());   // nada marcado: la carpeta abierta
    return out;
}

void FilePicker::accept()
{
    // Con archivos y carpetas permitidos, "Aceptar" sin marcar nada dentro de una carpeta de stems = esa carpeta
    finish (true);
}

void FilePicker::cancel()
{
    finish (false);
}

void FilePicker::finish (bool ok)
{
    if (finished)
        return;
    finished = true;
    const auto files = ok ? results() : juce::Array<juce::File>();
    if (onDone)
        onDone (files);   // quien nos creó suele destruirnos aquí: no tocar miembros después
}

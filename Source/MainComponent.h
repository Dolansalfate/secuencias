#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "Library.h"
#include "Separator.h"

class ChannelStrip;
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

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

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
    void songLoaded (std::shared_ptr<LoadedSong>, int generation);
    void saveCurrentMix();
    void unloadSong();
    void nextSong (int delta);
    void songMenu (int row);
    void refreshSetlist();

    // Mezclador, marcadores, click, loop
    void rebuildStrips();
    void rebuildMarkers();
    void clickControlsFromSong();
    void pushClick();
    void updateLoopRegion();
    void sectionAt (double seconds, double& start, double& end, juce::String& name) const;
    void jumpToMarker (int index);
    void addMarkerHere();
    void markerMenu (int index);
    void fillOutputBox (juce::ComboBox&, int selectedPair);
    int numOutputPairs() const;

    // Importar / separar / ajustes
    void chooseStems();
    void chooseSongToSeparate();
    void importStems (const juce::Array<juce::File>&);
    void startSeparation (const juce::File&);
    void showAudioSettings();
    void showAiSettings();
    juce::String pythonPath() const;
    juce::String selectedModel() const;
    void askText (const juce::String& title, const juce::String& initial,
                  std::function<void (const juce::String&)> onOk);
    void disableFocus (juce::Component&);

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    mutable juce::ApplicationProperties props;
    AudioEngine engine;
    Library library;
    Separator separator;

    std::shared_ptr<LoadedSong> currentSong;
    int currentIndex = -1;
    std::atomic<int> loadGeneration { 0 };
    double sepProgress = 0.0;
    double lastLength = -1.0;

    // --- UI ---
    juce::TextButton importBtn, separateBtn, audioBtn, aiBtn, cancelSepBtn;
    juce::ComboBox modelBox;
    juce::ProgressBar sepBar { sepProgress };
    juce::Label sepLabel;

    juce::Label setlistTitle;
    juce::ListBox setlist;

    juce::Label songTitle, nextLabel, sectionLabel, timeLabel;
    juce::TextButton prevBtn, playBtn, stopBtn, nextBtn;
    juce::ToggleButton loopBtn;
    juce::Slider positionSlider;

    juce::TextButton addMarkerBtn;
    juce::OwnedArray<MarkerButton> markerButtons;

    juce::ToggleButton clickBtn;
    juce::Label bpmLabel, offsetLabel, clickVolLabel;
    juce::Slider bpmSlider, offsetSlider, clickGainSlider;
    juce::ComboBox clickOutBox;

    juce::Viewport mixerView;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;

    juce::Label helpLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::ThreadPool loaderPool { 1 };   // último: se destruye primero

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

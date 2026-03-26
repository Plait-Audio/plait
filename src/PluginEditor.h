#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "ISODesign.h"
#include "PluginProcessor.h"
#include "OnsetDetector.h"
#include "MidiExporter.h"
#include "MidiSettingsComponent.h"
#include "LicenseDialog.h"
#include "AudioSettingsComponent.h"
#include "DrumMap.h"

// ── Volume slider — Option+Click resets to 0 dB ──────────────────────────────
class VolumeSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isAltDown())
            setValue (0.0, juce::sendNotification);
        else
            juce::Slider::mouseDown (e);
    }
};

// ── Editor ────────────────────────────────────────────────────────────────────
class ISODrumsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::ChangeListener,
                                      public juce::FileDragAndDropTarget,
                                      private juce::Timer
{
public:
    explicit ISODrumsAudioProcessorEditor(ISODrumsAudioProcessor&);
    ~ISODrumsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ChangeListener — thumbnail updates
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Mouse (drag stems out to DAW)
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    // ---- Worker thread ----
    class SeparationThread : public juce::Thread
    {
    public:
        SeparationThread(ISODrumsAudioProcessor& p, std::function<void()> onDone)
            : juce::Thread("SeparationWorker"), processor_(p), onDone_(std::move(onDone)) {}

        void run() override;

    private:
        ISODrumsAudioProcessor& processor_;
        std::function<void()>   onDone_;
    };

    // ---- Core ----
    ISOLookAndFeel          lookAndFeel_;
    ISODrumsAudioProcessor& audioProcessor_;
    SeparationThread        separationThread_;

    // ---- Audio I/O helpers ----
    juce::AudioFormatManager formatManager_;

    std::unique_ptr<juce::AudioFormatReaderSource> inputSource_;
    std::unique_ptr<juce::MemoryAudioSource>       kickSource_, snareSource_,
                                                    tomsSource_, hihatSource_, cymbalsSource_;

    juce::File tempDir_;
    juce::String inputFileName_;
    juce::File   currentFile_;

    // ---- Waveform display ----
    juce::AudioThumbnailCache thumbnailCache_ { 6 };
    juce::AudioThumbnail      thumbInput_    { 512, formatManager_, thumbnailCache_ };
    juce::AudioThumbnail      thumbKick_     { 512, formatManager_, thumbnailCache_ };
    juce::AudioThumbnail      thumbSnare_    { 512, formatManager_, thumbnailCache_ };
    juce::AudioThumbnail      thumbToms_     { 512, formatManager_, thumbnailCache_ };
    juce::AudioThumbnail      thumbHihat_    { 512, formatManager_, thumbnailCache_ };
    juce::AudioThumbnail      thumbCymbals_  { 512, formatManager_, thumbnailCache_ };

    // ---- UI components ----

    // Header
    juce::TextButton loadButton_     { "Load" };
    juce::TextButton settingsButton_ { "" };

    // Volume slider
    VolumeSlider volumeSlider_;
    juce::Label  volumeLabel_;

    // Per-stem: [0]=input, [1]=kick, [2]=snare, [3]=toms, [4]=hihat, [5]=cymbals
    static constexpr int kNumRows = 6;
    juce::TextButton soloButtons_[kNumRows];
    juce::TextButton saveButtons_[kNumRows];

    // Toolbar
    juce::TextButton separateButton_  { "Separate" };
    juce::TextButton exportWavsButton_{ "WAV" };
    juce::TextButton exportMidiButton_{ "MIDI" };

    // Progress
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_ { progressValue_ };

    // State
    bool fileLoaded_      = false;
    bool stemsDone_       = false;
    int  soloStemIndex_   = -1;

    // ---- Layout helpers (computed in resized()) ----
    juce::Rectangle<int> rowBounds_[kNumRows];
    juce::Rectangle<int> waveformBounds_[kNumRows];
    juce::Rectangle<int> toolbarBounds_;
    juce::Rectangle<int> footerBounds_;

    // ---- Helpers ----
    void loadFile(const juce::File& file);
    void startSeparation();
    void onSeparationComplete();
    void updateStemThumbnails();
    void attachStemSources();
    void setSolo(int stemIndex);
    void exportWavs();
    void showMidiDialog();
    void exportMidi(const MidiExportSettings& settings, const OnsetParams& params);
    void showLicenseDialog();
    void showExportLimitMessage(bool isWav);

    void paintWaveformRow(juce::Graphics& g,
                          juce::Rectangle<int> bounds,
                          const juce::String& label,
                          juce::AudioThumbnail& thumb,
                          juce::Colour waveColour,
                          bool active,
                          bool draggable) const;

    juce::File writeStemToTempFile(int rowIndex);
    bool rowHasAudio(int rowIndex) const;
    int  hitTestWaveformRow(juce::Point<int> pos) const;

    int dragSourceRow_ = -1;
    bool isDraggingOut_ = false;

    void timerCallback() override;

    static const juce::Colour kRowColours[kNumRows];
    static const char*        kRowLabels[kNumRows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ISODrumsAudioProcessorEditor)
};

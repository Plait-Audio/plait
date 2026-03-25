#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

#include "ISODesign.h"
#include "MidiExporter.h"
#include "OnsetDetector.h"

class MidiSettingsComponent : public juce::Component
{
public:
    using ExportCallback = std::function<void(const MidiExportSettings&, const OnsetParams&)>;

    explicit MidiSettingsComponent(ExportCallback onExport, double detectedBpm = 120.0);
    ~MidiSettingsComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth  = 420;
    static constexpr int kHeight = 500;

private:
    ISOLookAndFeel   lookAndFeel_;
    ExportCallback   exportCallback_;
    double           detectedBpm_;

    // ── Tempo ──
    juce::Label      bpmLabel_       { {}, "TEMPO" };
    juce::TextEditor bpmField_;
    juce::Label      bpmDetectedTag_;   // shows "DETECTED" badge

    // ── Sensitivity ──
    juce::Label  sensitivityLabel_   { {}, "ONSET SENSITIVITY" };
    juce::Slider sensitivitySlider_;
    juce::Label  sensitivityValue_;

    // ── Options ──
    juce::Label        optionsLabel_     { {}, "OPTIONS" };
    juce::ToggleButton quantizeToggle_   { "Quantize to grid" };
    juce::ComboBox     quantizeGrid_;
    juce::ToggleButton separateTracksToggle_ { "One track per stem" };

    // ── Note Map ──
    juce::Label      noteMapLabel_  { {}, "NOTE MAP" };
    juce::Label      noteRowLabels_[5];
    juce::TextEditor noteFields_[5];

    // ── Buttons ──
    juce::TextButton cancelButton_ { "Cancel" };
    juce::TextButton exportButton_ { "Export MIDI" };

    static const char* kStemNames[5];
    static const int   kDefaultNotes[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSettingsComponent)
};

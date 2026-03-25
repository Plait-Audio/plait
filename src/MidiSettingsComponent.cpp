#include "MidiSettingsComponent.h"
#include "DrumMap.h"

const char* MidiSettingsComponent::kStemNames[5]    = { "Kick", "Snare", "Toms", "Hi-Hat", "Cymbals" };
const int   MidiSettingsComponent::kDefaultNotes[5] = {
    DrumMap::DEFAULT_KICK, DrumMap::DEFAULT_SNARE, DrumMap::DEFAULT_TOMS,
    DrumMap::DEFAULT_HIHAT, DrumMap::DEFAULT_CYMBALS
};

using namespace ISOPalette;

// ── Constructor ───────────────────────────────────────────────────────────────

MidiSettingsComponent::MidiSettingsComponent(ExportCallback onExport, double detectedBpm)
    : exportCallback_(std::move(onExport)), detectedBpm_(detectedBpm)
{
    setLookAndFeel(&lookAndFeel_);
    setSize(kWidth, kHeight);

    // ── Tempo ──────────────────────────────────────────────────────────────────
    bpmLabel_.setColour(juce::Label::textColourId, TextDim);
    addAndMakeVisible(bpmLabel_);

    bpmField_.setText(juce::String(juce::roundToInt(detectedBpm_)), juce::dontSendNotification);
    bpmField_.setInputRestrictions(6, "0123456789.");
    bpmField_.setJustification(juce::Justification::centred);
    bpmField_.setFont(ISOLookAndFeel::font(18.f, true));
    addAndMakeVisible(bpmField_);

    bpmDetectedTag_.setText("DETECTED", juce::dontSendNotification);
    bpmDetectedTag_.setColour(juce::Label::textColourId, Accent.withAlpha(0.85f));
    bpmDetectedTag_.setFont(ISOLookAndFeel::font(8.5f, true));
    bpmDetectedTag_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(bpmDetectedTag_);

    // ── Sensitivity ────────────────────────────────────────────────────────────
    sensitivityLabel_.setColour(juce::Label::textColourId, TextDim);
    addAndMakeVisible(sensitivityLabel_);

    sensitivitySlider_.setRange(-60.0, 0.0, 0.5);
    sensitivitySlider_.setValue(-30.0);
    sensitivitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    sensitivitySlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sensitivitySlider_.setColour(juce::Slider::thumbColourId,          Accent);
    sensitivitySlider_.setColour(juce::Slider::trackColourId,          Border);
    sensitivitySlider_.setColour(juce::Slider::backgroundColourId,     Surface);
    sensitivitySlider_.onValueChange = [this]
    {
        sensitivityValue_.setText(juce::String(sensitivitySlider_.getValue(), 1) + " dB",
                                  juce::dontSendNotification);
    };
    addAndMakeVisible(sensitivitySlider_);

    sensitivityValue_.setText("-30.0 dB", juce::dontSendNotification);
    sensitivityValue_.setColour(juce::Label::textColourId, Text);
    sensitivityValue_.setFont(ISOLookAndFeel::font(12.f));
    sensitivityValue_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(sensitivityValue_);

    // ── Options ────────────────────────────────────────────────────────────────
    optionsLabel_.setColour(juce::Label::textColourId, TextDim);
    addAndMakeVisible(optionsLabel_);

    quantizeGrid_.addItem("1/4",  1);
    quantizeGrid_.addItem("1/8",  2);
    quantizeGrid_.addItem("1/16", 3);
    quantizeGrid_.addItem("1/32", 4);
    quantizeGrid_.setSelectedId(3, juce::dontSendNotification);
    quantizeGrid_.setEnabled(false);
    quantizeToggle_.onStateChange = [this] {
        quantizeGrid_.setEnabled(quantizeToggle_.getToggleState());
    };
    addAndMakeVisible(quantizeToggle_);
    addAndMakeVisible(quantizeGrid_);

    separateTracksToggle_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(separateTracksToggle_);

    // ── Note Map ───────────────────────────────────────────────────────────────
    noteMapLabel_.setColour(juce::Label::textColourId, TextDim);
    addAndMakeVisible(noteMapLabel_);

    for (int i = 0; i < 5; ++i)
    {
        noteRowLabels_[i].setText(kStemNames[i], juce::dontSendNotification);
        noteRowLabels_[i].setColour(juce::Label::textColourId, Text.withAlpha(0.8f));
        noteRowLabels_[i].setFont(ISOLookAndFeel::font(11.f));
        noteFields_[i].setText(juce::String(kDefaultNotes[i]), juce::dontSendNotification);
        noteFields_[i].setInputRestrictions(3, "0123456789");
        noteFields_[i].setJustification(juce::Justification::centred);
        addAndMakeVisible(noteRowLabels_[i]);
        addAndMakeVisible(noteFields_[i]);
    }

    // ── Buttons ────────────────────────────────────────────────────────────────
    cancelButton_.setColour(juce::TextButton::buttonColourId, Surface);
    cancelButton_.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton_);

    exportButton_.setColour(juce::TextButton::buttonColourId, Accent);
    exportButton_.onClick = [this]
    {
        MidiExportSettings settings;
        settings.bpm          = bpmField_.getText().getDoubleValue();
        settings.quantize     = quantizeToggle_.getToggleState();
        settings.quantizeGrid = [this]() -> int {
            switch (quantizeGrid_.getSelectedId())
            {
                case 1: return 4;   case 2: return 8;
                case 4: return 32;  default: return 16;
            }
        }();
        settings.separateTracks = separateTracksToggle_.getToggleState();

        OnsetParams params;
        params.sensitivityDb = (float)sensitivitySlider_.getValue();

        if (exportCallback_) exportCallback_(settings, params);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(exportButton_);
}

MidiSettingsComponent::~MidiSettingsComponent()
{
    setLookAndFeel(nullptr);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void MidiSettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(Bg);

    // Section dividers
    g.setColour(Border);
    const int divs[] = { 110, 186, 268 };
    for (int y : divs)
        g.fillRect(juce::Rectangle<int>(20, y, kWidth - 40, 1));
}

// ── Layout ────────────────────────────────────────────────────────────────────

void MidiSettingsComponent::resized()
{
    constexpr int kPad  = 20;
    constexpr int kGap  = 10;
    auto area = getLocalBounds().reduced(kPad);

    // ── TEMPO section ─────────────────────────────────────────────────────────
    bpmLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    auto tempoRow = area.removeFromTop(50);
    bpmField_.setBounds(tempoRow.removeFromLeft(80));
    tempoRow.removeFromLeft(10);
    auto tagArea = tempoRow.removeFromTop(18);
    bpmDetectedTag_.setBounds(tagArea);

    area.removeFromTop(kGap + 4);   // spacing above divider

    // ── SENSITIVITY section ───────────────────────────────────────────────────
    area.removeFromTop(kGap);
    sensitivityLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);
    auto sensRow = area.removeFromTop(28);
    sensitivityValue_.setBounds(sensRow.removeFromRight(72));
    sensitivitySlider_.setBounds(sensRow);
    area.removeFromTop(kGap);

    // ── OPTIONS section ───────────────────────────────────────────────────────
    area.removeFromTop(kGap);
    optionsLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    auto qRow = area.removeFromTop(26);
    quantizeToggle_.setBounds(qRow.removeFromLeft(160));
    quantizeGrid_.setBounds(qRow.removeFromLeft(80));
    area.removeFromTop(6);
    separateTracksToggle_.setBounds(area.removeFromTop(26));
    area.removeFromTop(kGap);

    // ── NOTE MAP section ──────────────────────────────────────────────────────
    area.removeFromTop(kGap);
    noteMapLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);

    // 2-column grid: Kick / Snare / Toms in left col, Hi-Hat / Cymbals in right
    constexpr int kCellW = (kWidth - kPad * 2) / 2 - 5;
    constexpr int kFieldW = 46;
    constexpr int kLabelW = kCellW - kFieldW - 8;
    const int noteRowH = 30;

    struct NoteCell { int col; };  // 0 = left, 1 = right
    const int cols[] = { 0, 1, 0, 1, 0 };  // Kick(L) Snare(R) Toms(L) Hi-Hat(R) Cymbals(L)

    int leftY  = area.getY();
    int rightY = area.getY();

    for (int i = 0; i < 5; ++i)
    {
        const bool isLeft = (cols[i] == 0);
        int x   = isLeft ? area.getX() : area.getX() + kCellW + 10;
        int& y  = isLeft ? leftY : rightY;

        noteRowLabels_[i].setBounds(x, y, kLabelW, noteRowH);
        noteFields_[i].setBounds(x + kLabelW + 8, y + (noteRowH - 26) / 2, kFieldW, 26);
        y += noteRowH + 4;
    }

    area.setY(juce::jmax(leftY, rightY) + kGap);
    area = getLocalBounds().reduced(kPad);
    area.removeFromBottom(kPad);

    // ── Action buttons ────────────────────────────────────────────────────────
    auto btnRow = area.removeFromBottom(38);
    cancelButton_.setBounds(btnRow.removeFromLeft(100));
    btnRow.removeFromLeft(10);
    exportButton_.setBounds(btnRow.removeFromLeft(140));
}

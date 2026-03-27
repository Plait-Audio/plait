#include "PluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <map>

// ---- Colour / label tables ----

const juce::Colour ISODrumsAudioProcessorEditor::kRowColours[kNumRows] = {
    juce::Colour(0xffffffff),   // Input   — white
    juce::Colour(0xffc45c5c),   // Kick    — Plait red
    juce::Colour(0xffc98a4a),   // Snare   — Plait amber
    juce::Colour(0xff6b9e6b),   // Toms    — Plait green
    juce::Colour(0xff5c8ec4),   // Hi-Hat  — Plait blue
    juce::Colour(0xff8a6ec9),   // Cymbals — Plait violet
};

const char* ISODrumsAudioProcessorEditor::kRowLabels[kNumRows] = {
    "Input", "Kick", "Snare", "Toms", "Hats", "Cymbals"
};

static const juce::Colour kBg      { ISOPalette::Bg      };
static const juce::Colour kSurface { ISOPalette::Surface };
static const juce::Colour kRowBg   { ISOPalette::RowBg   };
static const juce::Colour kBorder  { ISOPalette::Border  };
static const juce::Colour kAccent  { ISOPalette::Accent  };
static const juce::Colour kMuted   { ISOPalette::Muted   };
static const juce::Colour kText    { ISOPalette::Text    };

// ---- BPM estimation from detected drum hits ----
static double estimateBpm(const std::vector<DrumHit>& hits, double sampleRate)
{
    if (hits.size() < 4 || sampleRate < 1.0) return 120.0;

    std::vector<double> pos;
    pos.reserve(hits.size());
    for (auto& h : hits) pos.push_back(h.timeSec);
    std::sort(pos.begin(), pos.end());

    std::map<int, int> votes;
    for (size_t i = 1; i < pos.size(); ++i)
    {
        double ioi = pos[i] - pos[i - 1];
        juce::ignoreUnused(sampleRate);
        for (int mult : {1, 2, 4})
        {
            double period = ioi * mult;
            if (period < 0.25 || period > 2.0) continue;
            int bpm = juce::roundToInt(60.0 / period);
            if (bpm >= 60 && bpm <= 240) votes[bpm]++;
        }
    }

    if (votes.empty()) return 120.0;

    int best = 120, bestV = 0;
    for (auto& [bpm, v] : votes)
        if (v > bestV) { bestV = v; best = bpm; }
    return best;
}

// ============================================================================
// SeparationThread
// ============================================================================

void ISODrumsAudioProcessorEditor::SeparationThread::run()
{
    auto& p = processor_;

    p.separationRunning.store(true);
    p.separationProgress.store(0.0f);

    juce::AudioBuffer<float> inputCopy;
    double sampleRate = 44100.0;
    {
        juce::ScopedLock sl(p.resultLock);
        inputCopy   = p.inputBuffer;
        sampleRate  = p.inputSampleRate;
    }

    if (threadShouldExit()) { p.separationRunning.store(false); return; }

    const float exponent = p.maskExponent.load();
    auto result = p.getEngine().separate(inputCopy, sampleRate, &p.separationProgress, exponent);
    inputCopy = juce::AudioBuffer<float>();

    if (threadShouldExit()) { p.separationRunning.store(false); return; }

    OnsetDetector detector;
    std::vector<DrumHit> hits;

    struct StemInfo { const juce::AudioBuffer<float>* buf; int note; float prog; };
    StemInfo stems[5] = {
        { &result.kick,    DrumMap::DEFAULT_KICK,    0.92f },
        { &result.snare,   DrumMap::DEFAULT_SNARE,   0.94f },
        { &result.toms,    DrumMap::DEFAULT_TOMS,    0.96f },
        { &result.hihat,   DrumMap::DEFAULT_HIHAT,   0.97f },
        { &result.cymbals, DrumMap::DEFAULT_CYMBALS, 0.99f },
    };

    for (auto& s : stems)
    {
        if (threadShouldExit()) { p.separationRunning.store(false); return; }
        auto stemHits = detector.detect(*s.buf, sampleRate, s.note);
        hits.insert(hits.end(), stemHits.begin(), stemHits.end());
        p.separationProgress.store(s.prog);
    }

    {
        juce::ScopedLock sl(p.resultLock);
        p.separationResult = std::move(result);
        p.allHits          = std::move(hits);
        p.detectedBpm      = estimateBpm(p.allHits, sampleRate);
    }

    p.separationProgress.store(1.0f);
    p.separationRunning.store(false);

    juce::MessageManager::callAsync(onDone_);
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ISODrumsAudioProcessorEditor::ISODrumsAudioProcessorEditor(ISODrumsAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor_(p),
      separationThread_(p, [this]() { onSeparationComplete(); })
{
    setLookAndFeel(&lookAndFeel_);
    formatManager_.registerBasicFormats();

    tempDir_ = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                           .getChildFile("ISODrumsTemp");
    tempDir_.createDirectory();

    thumbInput_  .addChangeListener(this);
    thumbKick_   .addChangeListener(this);
    thumbSnare_  .addChangeListener(this);
    thumbToms_   .addChangeListener(this);
    thumbHihat_  .addChangeListener(this);
    thumbCymbals_.addChangeListener(this);

    // ---- Load button (gold bg, black text, sentence-case) ----
    loadButton_.setButtonText("Load Track");
    loadButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    loadButton_.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
    loadButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    loadButton_.setTooltip("Load a drum track (WAV, AIFF, FLAC, MP3)");
    loadButton_.onClick = [this]
    {
        juce::FileChooser chooser("Load audio file", {}, "*.wav;*.aiff;*.aif;*.flac;*.mp3");
        if (chooser.browseForFileToOpen())
            loadFile(chooser.getResult());
    };
    addAndMakeVisible(loadButton_);

    // ---- Settings button (gear icon drawn in paint) ----
    settingsButton_.setButtonText("");
    settingsButton_.setTooltip("Settings");
    settingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    settingsButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    settingsButton_.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem(1, "Audio Settings...");
        m.addItem(2, "License...");
        m.addSeparator();
        m.addItem(3, "Clear All", fileLoaded_);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton_),
            [this](int result)
            {
                if (result == 1)
                {
                    if (auto* holder = juce::StandalonePluginHolder::getInstance())
                    {
                        juce::DialogWindow::LaunchOptions o;
                        o.content.setOwned (new AudioSettingsComponent (holder->deviceManager));
                        o.dialogTitle                  = "Audio Settings";
                        o.dialogBackgroundColour       = ISOPalette::Darkest;
                        o.escapeKeyTriggersCloseButton = true;
                        o.useNativeTitleBar            = true;
                        o.resizable                    = false;
                        o.launchAsync();
                    }
                }
                else if (result == 2)
                {
                    showLicenseDialog();
                }
                else if (result == 3)
                {
                    clearAll();
                }
            });
    };
    addAndMakeVisible(settingsButton_);

    // ---- Volume slider ----
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setTooltip("Master volume (Option+Click to reset to 0 dB)");
    volumeSlider_.setRange(-60.0, 6.0, 0.1);
    volumeSlider_.setValue(0.0);
    volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    volumeSlider_.setColour(juce::Slider::trackColourId, kBorder);
    volumeSlider_.setColour(juce::Slider::thumbColourId, kText);
    volumeSlider_.setColour(juce::Slider::textBoxTextColourId, kMuted);
    volumeSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    volumeSlider_.setTextValueSuffix(" dB");
    volumeSlider_.onValueChange = [this]
    {
        float gain = juce::Decibels::decibelsToGain((float)volumeSlider_.getValue());
        audioProcessor_.outputGain.store(gain);
    };
    addAndMakeVisible(volumeSlider_);

    // ---- Per-row solo buttons ----
    for (int i = 0; i < kNumRows; ++i)
    {
        soloButtons_[i].setButtonText("");
        soloButtons_[i].setTooltip("Solo / play this stem (click again to pause)");
        soloButtons_[i].setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        soloButtons_[i].setEnabled(false);
        const int idx = i;
        soloButtons_[i].onClick = [this, idx]
        {
            if (soloStemIndex_ == idx)
                setSolo(-1);
            else
                setSolo(idx);
        };
        addAndMakeVisible(soloButtons_[i]);
    }

    // ---- Per-row save buttons (dropdown: WAV or MIDI) ----
    for (int i = 0; i < kNumRows; ++i)
    {
        saveButtons_[i].setButtonText("");
        saveButtons_[i].setTooltip(i == 0 ? "Solo / play the input track" : "Export this stem (WAV or MIDI)");
        saveButtons_[i].setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        saveButtons_[i].setEnabled(false);
        const int idx = i;
        saveButtons_[i].onClick = [this, idx]
        {
            if (idx == 0)
            {
                if (soloStemIndex_ == 0) setSolo(-1);
                else setSolo(0);
                return;
            }

            juce::PopupMenu m;
            m.addItem(1, "Export WAV");
            m.addItem(2, "Export MIDI");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&saveButtons_[idx]),
                [this, idx](int result)
                {
                    if (result == 1)
                    {
                        if (!audioProcessor_.getLicenseManager().canExportWav())
                        {
                            showExportLimitMessage(true);
                            return;
                        }

                        const int stemIdx = idx - 1;
                        juce::FileChooser chooser("Save stem WAV", {},  "*.wav");
                        if (!chooser.browseForFileToSave(true)) return;

                        const juce::AudioBuffer<float>* buf = nullptr;
                        {
                            juce::ScopedLock sl(audioProcessor_.resultLock);
                            switch (stemIdx) {
                                case 0: buf = &audioProcessor_.separationResult.kick;    break;
                                case 1: buf = &audioProcessor_.separationResult.snare;   break;
                                case 2: buf = &audioProcessor_.separationResult.toms;    break;
                                case 3: buf = &audioProcessor_.separationResult.hihat;   break;
                                case 4: buf = &audioProcessor_.separationResult.cymbals; break;
                            }
                            if (buf == nullptr || buf->getNumSamples() == 0) return;

                            juce::WavAudioFormat fmt;
                            auto outFile = chooser.getResult().withFileExtension("wav");
                            outFile.deleteFile();
                            auto stream = std::make_unique<juce::FileOutputStream>(outFile);
                            if (!stream->openedOk()) return;
                            auto writer = std::unique_ptr<juce::AudioFormatWriter>(
                                fmt.createWriterFor(stream.release(),
                                                    audioProcessor_.inputSampleRate,
                                                    static_cast<unsigned int>(buf->getNumChannels()),
                                                    16, {}, 0));
                            if (writer)
                                writer->writeFromAudioSampleBuffer(*buf, 0, buf->getNumSamples());
                        }
                    }
                    else if (result == 2)
                    {
                        showMidiDialog();
                    }
                });
        };
        addAndMakeVisible(saveButtons_[i]);
    }

    // ---- Toolbar ----
    separateButton_.setButtonText("Separate");
    separateButton_.setTooltip("Run AI stem separation on the loaded track");
    separateButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    separateButton_.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
    separateButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    separateButton_.setEnabled(false);
    separateButton_.onClick = [this] { startSeparation(); };
    addAndMakeVisible(separateButton_);

    exportWavsButton_.setButtonText("WAV");
    exportWavsButton_.setTooltip("Export all separated stems as WAV files");
    exportWavsButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    exportWavsButton_.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
    exportWavsButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    exportWavsButton_.setEnabled(false);
    exportWavsButton_.onClick = [this] { exportWavs(); };
    addAndMakeVisible(exportWavsButton_);

    exportMidiButton_.setButtonText("MIDI");
    exportMidiButton_.setTooltip("Export drum hits as a MIDI file");
    exportMidiButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    exportMidiButton_.setColour(juce::TextButton::textColourOnId,  juce::Colours::black);
    exportMidiButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    exportMidiButton_.setEnabled(false);
    exportMidiButton_.onClick = [this] { showMidiDialog(); };
    addAndMakeVisible(exportMidiButton_);

    progressBar_.setColour(juce::ProgressBar::foregroundColourId, kAccent);
    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    isolationSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    isolationSlider_.setTooltip("Adjust isolation strength (higher = sharper separation, may add artifacts)");
    isolationSlider_.setRange(0.5, 2.0, 0.05);
    isolationSlider_.setValue(1.0);
    isolationSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 20);
    isolationSlider_.setColour(juce::Slider::trackColourId, kBorder);
    isolationSlider_.setColour(juce::Slider::thumbColourId, kAccent);
    isolationSlider_.setColour(juce::Slider::textBoxTextColourId, kMuted);
    isolationSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    isolationSlider_.onValueChange = [this]
    {
        audioProcessor_.maskExponent.store((float)isolationSlider_.getValue());
    };
    addAndMakeVisible(isolationSlider_);

    isolationLabel_.setText("Isolation", juce::dontSendNotification);
    isolationLabel_.setFont(ISOLookAndFeel::font(10.f));
    isolationLabel_.setColour(juce::Label::textColourId, kMuted);
    isolationLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(isolationLabel_);

    setResizable(true, true);
    setResizeLimits(720, 520, 1920, 1400);
    setSize(960, 700);
    startTimerHz(24);
}

ISODrumsAudioProcessorEditor::~ISODrumsAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
    separationThread_.stopThread(3000);

    thumbInput_  .removeChangeListener(this);
    thumbKick_   .removeChangeListener(this);
    thumbSnare_  .removeChangeListener(this);
    thumbToms_   .removeChangeListener(this);
    thumbHihat_  .removeChangeListener(this);
    thumbCymbals_.removeChangeListener(this);

    audioProcessor_.activeTransport.store(nullptr);
    audioProcessor_.transportInput  .setSource(nullptr);
    audioProcessor_.transportKick   .setSource(nullptr);
    audioProcessor_.transportSnare  .setSource(nullptr);
    audioProcessor_.transportToms   .setSource(nullptr);
    audioProcessor_.transportHihat  .setSource(nullptr);
    audioProcessor_.transportCymbals.setSource(nullptr);
}

// ============================================================================
// Layout
// ============================================================================

void ISODrumsAudioProcessorEditor::resized()
{
    constexpr int kPadH        = 50;
    constexpr int kHeaderH     = 36;
    constexpr int kRowGap      = 2;
    constexpr int kRowToolGap  = 16;
    constexpr int kToolbarH    = 34;
    constexpr int kFooterH     = 28;
    constexpr int kFooterZoneH = 80;
    constexpr int kIconCol     = 28;
    constexpr int kColorStrip  = 3;
    constexpr int kIconGap     = 2;
    constexpr int kBtnMinW     = 90;
    constexpr int kBtnGap      = 10;

    auto content = getLocalBounds();
    content.removeFromLeft(kPadH);
    content.removeFromRight(kPadH);

    // Footer zone at bottom — content will be vertically centered within
    auto footerZone = content.removeFromBottom(kFooterZoneH);
    footerBounds_ = footerZone.withSizeKeepingCentre(footerZone.getWidth(), kFooterH);

    // Toolbar
    toolbarBounds_ = content.removeFromBottom(kToolbarH);
    content.removeFromBottom(kRowToolGap);

    // Header with balanced vertical centering
    const int availableForRowsAndGaps = content.getHeight() - kHeaderH;
    const int balancedGap = juce::jlimit(15, 40, availableForRowsAndGaps / 12);

    auto header = content.removeFromTop(balancedGap + kHeaderH);
    header.removeFromTop(balancedGap);
    content.removeFromTop(balancedGap);

    // --- Header layout ---
    // ISO Drums icon (painted): reserve space at far right
    const int isoIconW = (int)(35.f * (425.f / 476.f));
    const int isoLeftX = getWidth() - kPadH - isoIconW;
    header.removeFromRight(isoIconW);

    // Settings zone + load button from the right
    constexpr int kSettingsZone = 48;
    header.removeFromRight(kSettingsZone);
    constexpr int kLoadW = 105;
    loadButton_.setBounds(header.removeFromRight(kLoadW).reduced(0, 2));

    // Settings button: centered between load right and ISO icon left
    {
        int midX = (loadButton_.getRight() + isoLeftX) / 2;
        constexpr int kSettSz = 28;
        settingsButton_.setBounds(
            juce::Rectangle<int>(midX - kSettSz / 2,
                                 header.getY() + (header.getHeight() - kSettSz) / 2,
                                 kSettSz, kSettSz));
    }

    // Volume slider
    {
        const int leftEdge  = header.getX() + 210;
        const int rightEdge = header.getRight();
        auto volArea = juce::Rectangle<int>(leftEdge, header.getY(),
                                            rightEdge - leftEdge, header.getHeight());
        volArea = volArea.withSizeKeepingCentre(juce::jmin(160, volArea.getWidth()), 22);
        volArea.setCentre(volArea.getCentreX(), header.getCentreY());
        volumeSlider_.setBounds(volArea);
    }

    // --- Toolbar layout ---
    {
        auto tb = toolbarBounds_;
        separateButton_  .setBounds(tb.removeFromLeft(kBtnMinW));
        tb.removeFromLeft(kBtnGap);
        exportWavsButton_.setBounds(tb.removeFromLeft(kBtnMinW));
        tb.removeFromLeft(kBtnGap);
        exportMidiButton_.setBounds(tb.removeFromLeft(kBtnMinW));

        // Isolation slider (right side of toolbar)
        const int isoLabelW = 52;
        const int isoSliderW = juce::jmin(120, tb.getWidth() / 3);
        auto isoArea = tb.removeFromRight(isoSliderW);
        isoArea = isoArea.withSizeKeepingCentre(isoSliderW, 22);
        isolationSlider_.setBounds(isoArea);
        auto isoLabelArea = tb.removeFromRight(isoLabelW);
        isoLabelArea = isoLabelArea.withSizeKeepingCentre(isoLabelW, 20);
        isolationLabel_.setBounds(isoLabelArea);

        // Progress bar: fills the middle
        auto progressArea = tb;
        progressArea.removeFromLeft(20);
        progressArea.removeFromLeft(36);
        progressArea.removeFromRight(8);
        progressBar_.setBounds(progressArea.reduced(0, 4));
    }

    // --- Waveform rows ---
    const int totalRowGaps = (kNumRows - 1) * kRowGap;
    const int rowH = (content.getHeight() - totalRowGaps) / kNumRows;

    for (int i = 0; i < kNumRows; ++i)
    {
        auto row = content.removeFromTop(rowH);
        rowBounds_[i] = row;

        auto iconCol = row.removeFromLeft(kIconCol);
        iconCol.reduce(0, 2);
        auto topIcon = iconCol.removeFromTop((iconCol.getHeight() - kIconGap) / 2);
        iconCol.removeFromTop(kIconGap);
        auto botIcon = iconCol;
        soloButtons_[i].setBounds(topIcon);
        saveButtons_[i].setBounds(botIcon);

        row.removeFromLeft(kColorStrip + 4);
        waveformBounds_[i] = row;

        if (i < kNumRows - 1)
            content.removeFromTop(kRowGap);
    }
}

// ============================================================================
// Paint
// ============================================================================

static std::unique_ptr<juce::Drawable>& getCachedSVG(const char* data, int size)
{
    static std::map<const char*, std::unique_ptr<juce::Drawable>> cache;
    auto it = cache.find(data);
    if (it == cache.end())
    {
        auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(data, size));
        auto drawable = xml ? juce::Drawable::createFromSVG(*xml) : nullptr;
        it = cache.emplace(data, std::move(drawable)).first;
    }
    return it->second;
}

static juce::Image& getCachedImage(const char* data, int size)
{
    static std::map<const char*, juce::Image> cache;
    auto it = cache.find(data);
    if (it == cache.end())
        it = cache.emplace(data, juce::ImageCache::getFromMemory(data, size)).first;
    return it->second;
}

static void drawSVGInRect(juce::Graphics& g, const char* data, int size,
                           juce::Rectangle<float> rect, float opacity = 1.f)
{
    auto& svg = getCachedSVG(data, size);
    if (svg)
    {
        svg->setTransformToFit(rect, juce::RectanglePlacement::centred);
        svg->draw(g, opacity);
    }
}

static void drawImageInRect(juce::Graphics& g, const char* data, int size,
                             juce::Rectangle<float> rect, float opacity = 1.f)
{
    auto& img = getCachedImage(data, size);
    if (img.isValid())
    {
        g.setOpacity(opacity);
        g.drawImage(img,
                    (int)rect.getX(), (int)rect.getY(),
                    (int)rect.getWidth(), (int)rect.getHeight(),
                    0, 0, img.getWidth(), img.getHeight());
        g.setOpacity(1.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void ISODrumsAudioProcessorEditor::paint(juce::Graphics& g)
{
    constexpr int kPadH = 50;

    g.fillAll(kBg);

    // ── Footer ────────────────────────────────────────────────────────────────
    {
        auto fb = footerBounds_;

        const float plaitH = 31.f;
        drawImageInRect(g, BinaryData::logoplaiticonwhite_png,
                        BinaryData::logoplaiticonwhite_pngSize,
                        juce::Rectangle<float>((float)fb.getX(),
                            fb.getY() + (fb.getHeight() - plaitH) * 0.5f,
                            plaitH, plaitH), 1.f);

        const float svgH = 26.f;
        const float svgW = svgH * (273.f / 40.f);
        drawSVGInRect(g, BinaryData::designedbyplait_svg,
                      BinaryData::designedbyplait_svgSize,
                      juce::Rectangle<float>(
                          (float)fb.getRight() - svgW,
                          fb.getY() + (fb.getHeight() - svgH) * 0.5f,
                          svgW, svgH), 1.f);
    }

    // ── Toolbar: progress status (only visible during/after separation) ─────
    {
        auto pbarBounds = progressBar_.getBounds();
        const bool separating = audioProcessor_.separationRunning.load();

        if (stemsDone_)
        {
            g.setColour(juce::Colour(0xff44cc66));
            g.setFont(ISOLookAndFeel::font(10.f));
            g.drawText("Done", pbarBounds, juce::Justification::centred);
        }
        else if (separating || progressValue_ > 0.01)
        {
            int pct = juce::roundToInt(progressValue_ * 100.0);
            g.setColour(ISOPalette::MutedLt);
            g.setFont(ISOLookAndFeel::font(10.f));
            g.drawText("Processing " + juce::String(pct) + "%",
                       pbarBounds, juce::Justification::centred);
        }
    }

    // ── Header ────────────────────────────────────────────────────────────────
    // Use loadButton_ position to derive the header Y since resized() places it
    const float headerY = (float)loadButton_.getY() - 2.f;
    const float headerH = (float)loadButton_.getHeight() + 4.f;
    const float contentLeft = (float)kPadH;
    const float contentRight = (float)(getWidth() - kPadH);

    // ISO DRUMS wordmark SVG
    {
        const float wmH = 23.f;
        const float wmW = wmH * (265.f / 32.f);
        const float wmX = contentLeft;
        const float wmY = headerY + (headerH - wmH) * 0.5f - 1.f;
        drawSVGInRect(g, BinaryData::logoisodrumswordmarkwhite_svg,
                      BinaryData::logoisodrumswordmarkwhite_svgSize,
                      juce::Rectangle<float>(wmX, wmY, wmW, wmH), 1.f);

#ifndef ISO_VERSION_STRING
#define ISO_VERSION_STRING "0.1.0"
#endif
        g.setColour(kMuted);
        g.setFont(ISOLookAndFeel::font(10.f));
        g.drawText("v " ISO_VERSION_STRING,
                   juce::Rectangle<float>(wmX + wmW + 6.f, wmY, 50.f, wmH),
                   juce::Justification::bottomLeft);
    }

    // ISO Drums icon
    {
        const float iconH = 35.f;
        const float iconW = iconH * (425.f / 476.f);
        drawImageInRect(g, BinaryData::logoisodrumsiconwhite_png,
                        BinaryData::logoisodrumsiconwhite_pngSize,
                        juce::Rectangle<float>(
                            contentRight - iconW,
                            headerY + (headerH - iconH) * 0.5f,
                            iconW, iconH), 1.f);
    }

    // Settings gear icon (20% smaller than previous ~24px → ~19px)
    {
        auto b = settingsButton_.getBounds().toFloat();
        const float iconSz = 19.f;
        auto iconRect = juce::Rectangle<float>(
            b.getCentreX() - iconSz * 0.5f,
            b.getCentreY() - iconSz * 0.5f,
            iconSz, iconSz);
        drawSVGInRect(g, BinaryData::settingsicon_svg,
                      BinaryData::settingsicon_svgSize,
                      iconRect, 0.8f);
    }

    // Trial / License — plain text, no pill
    {
        auto& lm = audioProcessor_.getLicenseManager();
        const auto st = lm.getState();

        const float textY = headerY;
        const float textH = headerH;
        const float textRight = (float)loadButton_.getX() - 12.f;

        g.setFont(ISOLookAndFeel::font(11.f, true));

        switch (st)
        {
            case LicenseState::Trial:
            {
                int daysLeft = lm.trialDaysRemaining();
                juce::String days = juce::String(daysLeft) + (daysLeft == 1 ? " Day" : " Days");

                g.setFont(ISOLookAndFeel::font(11.f, true));
                float trialW = g.getCurrentFont().getStringWidthFloat("Trial");
                g.setFont(ISOLookAndFeel::font(11.f));
                float daysW  = g.getCurrentFont().getStringWidthFloat(days);

                const float sep  = 8.f;
                const float barW = 6.f;
                float totalW = trialW + barW + sep + daysW;
                float tx = textRight - totalW;

                g.setColour(kAccent);
                g.setFont(ISOLookAndFeel::font(11.f, true));
                g.drawText("Trial", juce::Rectangle<float>(tx, textY, trialW, textH),
                           juce::Justification::centredLeft);
                tx += trialW + sep * 0.5f;

                g.setColour(kAccent.withAlpha(0.5f));
                g.drawText("|", juce::Rectangle<float>(tx, textY, barW, textH),
                           juce::Justification::centred);
                tx += barW + sep * 0.5f;

                g.setColour(kText);
                g.setFont(ISOLookAndFeel::font(11.f));
                g.drawText(days, juce::Rectangle<float>(tx, textY, daysW, textH),
                           juce::Justification::centredLeft);
                break;
            }
            case LicenseState::TrialExpired:
                g.setColour(juce::Colour(0xffff4444));
                g.drawText("Trial Expired", juce::Rectangle<float>(textRight - 120.f, textY, 120.f, textH),
                           juce::Justification::centredRight);
                break;
            case LicenseState::Licensed:
                g.setColour(juce::Colour(0xff44cc66));
                g.drawText("Licensed", juce::Rectangle<float>(textRight - 80.f, textY, 80.f, textH),
                           juce::Justification::centredRight);
                break;
            case LicenseState::LicenseCheckNeeded:
                g.setColour(kAccent);
                g.drawText("Check License", juce::Rectangle<float>(textRight - 120.f, textY, 120.f, textH),
                           juce::Justification::centredRight);
                break;
        }
    }

    // Volume icon (SVG)
    {
        auto slBounds = volumeSlider_.getBounds();
        const float iconSz = 16.f;
        drawSVGInRect(g, BinaryData::volumeloud_svg, BinaryData::volumeloud_svgSize,
                      juce::Rectangle<float>(
                          (float)slBounds.getX() - iconSz - 4.f,
                          (float)slBounds.getCentreY() - iconSz * 0.5f,
                          iconSz, iconSz), 0.6f);
    }

    // ── Waveform rows ─────────────────────────────────────────────────────────
    juce::AudioThumbnail* thumbs[kNumRows] = {
        &thumbInput_, &thumbKick_, &thumbSnare_,
        &thumbToms_,  &thumbHihat_, &thumbCymbals_
    };
    for (int i = 0; i < kNumRows; ++i)
    {
        const bool active    = (soloStemIndex_ == i);
        const bool draggable = rowHasAudio(i);
        const double ph = (soloStemIndex_ >= 0 && rowHasAudio(i)) ? playheadPos_ : -1.0;
        paintWaveformRow(g, rowBounds_[i], kRowLabels[i], *thumbs[i],
                         kRowColours[i], active, draggable, ph);
    }

    // ── Solo/Save icons ───────────────────────────────────────────────────────
    for (int i = 0; i < kNumRows; ++i)
    {
        const bool hasStem = (i == 0) ? fileLoaded_ : stemsDone_;
        const float iconAlpha = hasStem ? 0.75f : 0.25f;

        auto soloBounds = soloButtons_[i].getBounds().toFloat().reduced(5.f);
        drawSVGInRect(g, BinaryData::soloicon_svg, BinaryData::soloicon_svgSize,
                      soloBounds, (soloStemIndex_ == i) ? 1.f : iconAlpha);

        auto saveBounds = saveButtons_[i].getBounds().toFloat().reduced(5.f);
        drawSVGInRect(g, BinaryData::saveicon_svg, BinaryData::saveicon_svgSize,
                      saveBounds, iconAlpha);

        // Paint gap between solo/save with app background color
        int gapY = soloButtons_[i].getBottom();
        int gapH = saveButtons_[i].getY() - gapY;
        if (gapH > 0)
        {
            g.setColour(kBg);
            g.fillRect(rowBounds_[i].getX(), gapY,
                       28, gapH);
        }
    }
}

void ISODrumsAudioProcessorEditor::paintWaveformRow(juce::Graphics& g,
                                                     juce::Rectangle<int> bounds,
                                                     const juce::String& label,
                                                     juce::AudioThumbnail& thumb,
                                                     juce::Colour waveColour,
                                                     bool active,
                                                     bool /*draggable*/,
                                                     double playheadNorm) const
{
    constexpr int kIconCol    = 28;
    constexpr int kColorStrip = 3;
    constexpr float kRadius   = 6.f;

    // Determine which row this is for selective rounding
    int rowIdx = -1;
    for (int i = 0; i < kNumRows; ++i)
        if (rowBounds_[i] == bounds) { rowIdx = i; break; }

    const bool isFirst = (rowIdx == 0);
    const bool isLast  = (rowIdx == kNumRows - 1);

    // Unified background — only round top corners on first row, bottom on last
    {
        auto r = bounds.toFloat();
        juce::Path bg;
        if (isFirst && isLast)
            bg.addRoundedRectangle(r, kRadius);
        else if (isFirst)
            bg.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                   kRadius, kRadius, true, true, false, false);
        else if (isLast)
            bg.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                   kRadius, kRadius, false, false, true, true);
        else
            bg.addRectangle(r);

        g.setColour(active ? ISOPalette::Hover : ISOPalette::RowBg);
        g.fillPath(bg);
    }

    // Color strip — full height of trackbar
    const float stripX = (float)(bounds.getX() + kIconCol + 2);
    g.setColour(waveColour.withAlpha(active ? 0.9f : 0.6f));
    g.fillRect(stripX, (float)bounds.getY(),
               (float)kColorStrip, (float)bounds.getHeight());

    // Waveform area
    auto waveArea = bounds;
    waveArea.removeFromLeft(kIconCol + kColorStrip + 4);

    auto waveR = waveArea.reduced(10, 6);

    if (thumb.getTotalLength() > 0.0)
    {
        g.setColour(waveColour.withAlpha(active ? 0.75f : 0.45f));
        thumb.drawChannels(g, waveR, 0.0, thumb.getTotalLength(), 1.0f);

        g.setColour(waveColour.withAlpha(0.12f));
        g.fillRect(waveR.getX(), waveR.getCentreY(), waveR.getWidth(), 1);
    }
    else
    {
        g.setColour(ISOPalette::MutedLt.withAlpha(0.3f));
        g.setFont(ISOLookAndFeel::font(10.f, false));
        g.drawText("NO AUDIO", waveR, juce::Justification::centred);
    }

    // Track label — on top of waveform, top-left
    {
        const float labelX = (float)waveArea.getX() + 10.f;
        const float labelY = (float)waveArea.getY() + 5.f;
        g.setColour(juce::Colours::white);
        g.setFont(ISOLookAndFeel::font(12.f, false));
        g.drawText(label, juce::Rectangle<float>(labelX, labelY, 80.f, 16.f),
                   juce::Justification::centredLeft);
    }

    // Playhead
    if (playheadNorm >= 0.0 && playheadNorm <= 1.0 && thumb.getTotalLength() > 0.0)
    {
        const float phX = (float)waveR.getX() + (float)waveR.getWidth() * (float)playheadNorm;
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.fillRect(phX, (float)waveR.getY(), 1.5f, (float)waveR.getHeight());
    }
}

// ============================================================================
// File loading
// ============================================================================

void ISODrumsAudioProcessorEditor::loadFile(const juce::File& file)
{
    auto* reader = formatManager_.createReaderFor(file);
    if (reader == nullptr) return;

    // ── Stop any active playback ──────────────────────────────────────────────
    if (auto* t = audioProcessor_.activeTransport.load())
    {
        t->stop();
        t->setPosition(0.0);
    }
    audioProcessor_.activeTransport.store(nullptr);
    soloStemIndex_ = -1;

    // ── Clear previous stem thumbnails and sources ────────────────────────────
    thumbKick_.clear();
    thumbSnare_.clear();
    thumbToms_.clear();
    thumbHihat_.clear();
    thumbCymbals_.clear();

    audioProcessor_.transportKick.setSource(nullptr);
    audioProcessor_.transportSnare.setSource(nullptr);
    audioProcessor_.transportToms.setSource(nullptr);
    audioProcessor_.transportHihat.setSource(nullptr);
    audioProcessor_.transportCymbals.setSource(nullptr);
    kickSource_.reset();
    snareSource_.reset();
    tomsSource_.reset();
    hihatSource_.reset();
    cymbalsSource_.reset();

    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        audioProcessor_.separationResult = SeparationResult();
        audioProcessor_.allHits.clear();
    }

    // ── Load the new file ─────────────────────────────────────────────────────
    currentFile_    = file;
    inputFileName_  = file.getFileNameWithoutExtension();

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buf(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&buf, 0, numSamples, 0, true, true);

    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        audioProcessor_.inputBuffer     = std::move(buf);
        audioProcessor_.inputSampleRate = reader->sampleRate;
    }

    audioProcessor_.transportInput.setSource(nullptr);
    inputSource_.reset();

    inputSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    audioProcessor_.transportInput.setSource(
        inputSource_.get(), 0, nullptr,
        reader->sampleRate, static_cast<int>(reader->numChannels));

    thumbInput_.setSource(new juce::FileInputSource(file));

    fileLoaded_ = true;
    stemsDone_  = false;
    progressValue_ = 0.0;
    audioProcessor_.separationProgress.store(0.0f);

    separateButton_.setEnabled(audioProcessor_.getEngine().isReady());
    soloButtons_[0].setEnabled(true);
    for (int i = 1; i < kNumRows; ++i)
    {
        soloButtons_[i].setEnabled(false);
        saveButtons_[i].setEnabled(false);
    }

    repaint();
}

// ============================================================================
// Clear All
// ============================================================================

void ISODrumsAudioProcessorEditor::clearAll()
{
    if (audioProcessor_.separationRunning.load()) return;

    if (auto* t = audioProcessor_.activeTransport.load())
    {
        t->stop();
        t->setPosition(0.0);
    }
    audioProcessor_.activeTransport.store(nullptr);
    soloStemIndex_ = -1;
    playheadPos_ = 0.0;

    thumbInput_.clear();
    thumbKick_.clear();
    thumbSnare_.clear();
    thumbToms_.clear();
    thumbHihat_.clear();
    thumbCymbals_.clear();

    audioProcessor_.transportInput.setSource(nullptr);
    audioProcessor_.transportKick.setSource(nullptr);
    audioProcessor_.transportSnare.setSource(nullptr);
    audioProcessor_.transportToms.setSource(nullptr);
    audioProcessor_.transportHihat.setSource(nullptr);
    audioProcessor_.transportCymbals.setSource(nullptr);
    inputSource_.reset();
    kickSource_.reset();
    snareSource_.reset();
    tomsSource_.reset();
    hihatSource_.reset();
    cymbalsSource_.reset();

    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        audioProcessor_.separationResult = SeparationResult();
        audioProcessor_.allHits.clear();
        audioProcessor_.inputBuffer = juce::AudioBuffer<float>();
    }

    currentFile_ = juce::File();
    inputFileName_ = juce::String();
    fileLoaded_ = false;
    stemsDone_ = false;
    progressValue_ = 0.0;
    audioProcessor_.separationProgress.store(0.0f);

    separateButton_.setEnabled(false);
    exportWavsButton_.setEnabled(false);
    exportMidiButton_.setEnabled(false);
    for (int i = 0; i < kNumRows; ++i)
    {
        soloButtons_[i].setEnabled(false);
        saveButtons_[i].setEnabled(false);
    }

    repaint();
}

// ============================================================================
// Separation
// ============================================================================

void ISODrumsAudioProcessorEditor::startSeparation()
{
    if (audioProcessor_.separationRunning.load()) return;

    separateButton_.setEnabled(false);
    exportWavsButton_.setEnabled(false);
    exportMidiButton_.setEnabled(false);
    progressValue_ = 0.0;

    setSolo(-1);
    separationThread_.startThread();
}

void ISODrumsAudioProcessorEditor::onSeparationComplete()
{
    progressValue_ = 1.0;  // guarantee 100% is shown before stems appear

    updateStemThumbnails();
    attachStemSources();

    stemsDone_ = true;
    separateButton_.setEnabled(true);
    exportWavsButton_.setEnabled(true);
    exportMidiButton_.setEnabled(true);

    for (int i = 0; i < kNumRows; ++i)
    {
        soloButtons_[i].setEnabled(true);
        saveButtons_[i].setEnabled(true);
    }

    repaint();
}

void ISODrumsAudioProcessorEditor::updateStemThumbnails()
{
    juce::ScopedLock sl(audioProcessor_.resultLock);
    const double sr = audioProcessor_.inputSampleRate;
    auto& r = audioProcessor_.separationResult;

    auto setThumb = [&](juce::AudioThumbnail& t, const juce::AudioBuffer<float>& b)
    {
        t.reset(b.getNumChannels(), sr, b.getNumSamples());
        t.addBlock(0, b, 0, b.getNumSamples());
    };
    setThumb(thumbKick_,    r.kick);
    setThumb(thumbSnare_,   r.snare);
    setThumb(thumbToms_,    r.toms);
    setThumb(thumbHihat_,   r.hihat);
    setThumb(thumbCymbals_, r.cymbals);
}

void ISODrumsAudioProcessorEditor::attachStemSources()
{
    audioProcessor_.transportKick   .setSource(nullptr);
    audioProcessor_.transportSnare  .setSource(nullptr);
    audioProcessor_.transportToms   .setSource(nullptr);
    audioProcessor_.transportHihat  .setSource(nullptr);
    audioProcessor_.transportCymbals.setSource(nullptr);
    kickSource_.reset();
    snareSource_.reset();
    tomsSource_.reset();
    hihatSource_.reset();
    cymbalsSource_.reset();

    juce::ScopedLock sl(audioProcessor_.resultLock);
    const double sr = audioProcessor_.inputSampleRate;
    auto& r = audioProcessor_.separationResult;

    auto attach = [&](std::unique_ptr<juce::MemoryAudioSource>& src,
                      juce::AudioTransportSource& transport,
                      juce::AudioBuffer<float>& buf)
    {
        src = std::make_unique<juce::MemoryAudioSource>(buf, true, false);
        transport.setSource(src.get(), 0, nullptr, sr, buf.getNumChannels());
    };

    attach(kickSource_,    audioProcessor_.transportKick,    r.kick);
    attach(snareSource_,   audioProcessor_.transportSnare,   r.snare);
    attach(tomsSource_,    audioProcessor_.transportToms,    r.toms);
    attach(hihatSource_,   audioProcessor_.transportHihat,   r.hihat);
    attach(cymbalsSource_, audioProcessor_.transportCymbals, r.cymbals);
}

// ============================================================================
// Playback
// ============================================================================

void ISODrumsAudioProcessorEditor::setSolo(int stemIndex)
{
    juce::AudioTransportSource* targets[kNumRows] = {
        &audioProcessor_.transportInput,
        &audioProcessor_.transportKick,
        &audioProcessor_.transportSnare,
        &audioProcessor_.transportToms,
        &audioProcessor_.transportHihat,
        &audioProcessor_.transportCymbals,
    };

    auto* current = audioProcessor_.activeTransport.load();

    // Re-clicking the active stem toggles play/pause
    if (stemIndex == soloStemIndex_ && current != nullptr)
    {
        if (current->isPlaying())
        {
            current->stop();
        }
        else
        {
            current->start();
        }
        repaint();
        return;
    }

    // Switching to a different stem or deselecting
    if (current != nullptr)
        current->stop();
    audioProcessor_.activeTransport.store(nullptr);

    soloStemIndex_ = stemIndex;
    playheadPos_ = 0.0;

    if (stemIndex < 0)
    {
        repaint();
        return;
    }

    auto* chosen = targets[stemIndex];
    chosen->setPosition(0.0);
    chosen->start();
    audioProcessor_.activeTransport.store(chosen);

    repaint();
}

// ============================================================================
// Export
// ============================================================================

void ISODrumsAudioProcessorEditor::showExportLimitMessage(bool /*isWav*/)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Trial Expired",
        "Your 30-day trial has expired.\n\n"
        "Open Settings to activate ISO Drums.");
}

void ISODrumsAudioProcessorEditor::exportWavs()
{
    if (!audioProcessor_.getLicenseManager().canExportWav())
    {
        showExportLimitMessage(true);
        return;
    }

    juce::FileChooser chooser("Choose output folder", juce::File::getSpecialLocation(
                              juce::File::userMusicDirectory));
    if (!chooser.browseForDirectory()) return;

    const auto dir = chooser.getResult();
    const juce::String base = inputFileName_.isEmpty() ? "iso_drums" : inputFileName_;

    juce::ScopedLock sl(audioProcessor_.resultLock);
    const double sr = audioProcessor_.inputSampleRate;
    auto& r = audioProcessor_.separationResult;

    struct { const char* suffix; const juce::AudioBuffer<float>* buf; } stems[5] = {
        { "_kick",    &r.kick    },
        { "_snare",   &r.snare   },
        { "_toms",    &r.toms    },
        { "_hihat",   &r.hihat   },
        { "_cymbals", &r.cymbals },
    };

    juce::WavAudioFormat fmt;
    for (auto& s : stems)
    {
        if (s.buf->getNumSamples() == 0) continue;
        auto outFile = dir.getChildFile(base + s.suffix + ".wav");
        outFile.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream>(outFile);
        if (!stream->openedOk()) continue;
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(
            fmt.createWriterFor(stream.release(), sr,
                                static_cast<unsigned int>(s.buf->getNumChannels()),
                                16, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(*s.buf, 0, s.buf->getNumSamples());
    }

    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Export complete", "WAV stems saved to:\n" + dir.getFullPathName());
}

void ISODrumsAudioProcessorEditor::showLicenseDialog()
{
    auto* content = new LicenseDialog(audioProcessor_.getLicenseManager());

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle             = "License";
    opts.dialogBackgroundColour  = ISOPalette::Dark;
    opts.componentToCentreAround = this;
    opts.useNativeTitleBar       = true;
    opts.resizable               = false;
    opts.launchAsync();
}

void ISODrumsAudioProcessorEditor::showMidiDialog()
{
    const double bpm = audioProcessor_.detectedBpm;
    auto* content = new MidiSettingsComponent([this](const MidiExportSettings& s,
                                                     const OnsetParams& p)
    {
        exportMidi(s, p);
    }, bpm);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle              = "Export MIDI";
    opts.dialogBackgroundColour   = ISOPalette::Dark;
    opts.componentToCentreAround  = this;
    opts.useNativeTitleBar        = true;
    opts.resizable                = false;
    opts.launchAsync();
}

void ISODrumsAudioProcessorEditor::exportMidi(const MidiExportSettings& settings,
                                               const OnsetParams& /*params*/)
{
    if (!audioProcessor_.getLicenseManager().canExportMidi())
    {
        showExportLimitMessage(false);
        return;
    }

    juce::FileChooser chooser("Save MIDI file", {}, "*.mid");
    if (!chooser.browseForFileToSave(true)) return;

    std::vector<DrumHit> hitsCopy;
    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        hitsCopy = audioProcessor_.allHits;
    }

    if (hitsCopy.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "No hits", "No drum hits detected. Run separation first.");
        return;
    }

    MidiExporter exporter;
    const auto outFile = chooser.getResult().withFileExtension("mid");
    const std::vector<int> stemNotes = {
        DrumMap::DEFAULT_KICK, DrumMap::DEFAULT_SNARE, DrumMap::DEFAULT_TOMS,
        DrumMap::DEFAULT_HIHAT, DrumMap::DEFAULT_CYMBALS
    };
    if (exporter.exportToFile(hitsCopy, outFile, settings, stemNotes))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "Export complete", "MIDI saved to:\n" + outFile.getFullPathName());
    else
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Export failed", "Could not write MIDI file.");
}

// ============================================================================
// Timer (progress poll + repaint)
// ============================================================================

void ISODrumsAudioProcessorEditor::timerCallback()
{
    progressValue_ = static_cast<double>(audioProcessor_.separationProgress.load());

    bool showProgress = audioProcessor_.separationRunning.load() || (progressValue_ > 0.01 && !stemsDone_);
    progressBar_.setVisible(showProgress);

    if (auto* t = audioProcessor_.activeTransport.load())
    {
        double len = t->getLengthInSeconds();
        if (len > 0.0)
            playheadPos_ = t->getCurrentPosition() / len;

        if (t->hasStreamFinished())
        {
            t->stop();
            t->setPosition(0.0);
            audioProcessor_.activeTransport.store(nullptr);
            soloStemIndex_ = -1;
            playheadPos_ = 0.0;
        }
    }

    repaint();
}

// ============================================================================
// Drag stems out to DAW
// ============================================================================

bool ISODrumsAudioProcessorEditor::rowHasAudio(int rowIndex) const
{
    if (rowIndex == 0) return fileLoaded_;
    return stemsDone_;
}

int ISODrumsAudioProcessorEditor::hitTestWaveformRow(juce::Point<int> pos) const
{
    for (int i = 0; i < kNumRows; ++i)
        if (waveformBounds_[i].contains(pos))
            return i;
    return -1;
}

juce::File ISODrumsAudioProcessorEditor::writeStemToTempFile(int rowIndex)
{
    static const char* suffixes[] = { "", "_kick", "_snare", "_toms", "_hihat", "_cymbals" };
    juce::String baseName = inputFileName_.isEmpty() ? "iso_drums" : inputFileName_;
    auto outFile = tempDir_.getChildFile(baseName + suffixes[rowIndex] + ".wav");

    const juce::AudioBuffer<float>* buf = nullptr;
    double sr = 44100.0;

    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        sr = audioProcessor_.inputSampleRate;
        switch (rowIndex)
        {
            case 0: buf = &audioProcessor_.inputBuffer;           break;
            case 1: buf = &audioProcessor_.separationResult.kick;    break;
            case 2: buf = &audioProcessor_.separationResult.snare;   break;
            case 3: buf = &audioProcessor_.separationResult.toms;    break;
            case 4: buf = &audioProcessor_.separationResult.hihat;   break;
            case 5: buf = &audioProcessor_.separationResult.cymbals; break;
        }
        if (buf == nullptr || buf->getNumSamples() == 0)
            return {};

        outFile.deleteFile();
        juce::WavAudioFormat fmt;
        auto stream = std::make_unique<juce::FileOutputStream>(outFile);
        if (!stream->openedOk()) return {};

        auto writer = std::unique_ptr<juce::AudioFormatWriter>(
            fmt.createWriterFor(stream.release(), sr,
                                static_cast<unsigned int>(buf->getNumChannels()),
                                24, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer(*buf, 0, buf->getNumSamples());
    }

    return outFile;
}

void ISODrumsAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    dragSourceRow_ = -1;
    int row = hitTestWaveformRow(e.getPosition());

    // Click-to-seek: if an active transport is playing and the click is on a
    // waveform row that has audio, compute the normalized position and seek.
    if (row >= 0 && rowHasAudio(row) && soloStemIndex_ >= 0)
    {
        constexpr int kIconCol    = 28;
        constexpr int kColorStrip = 3;

        auto waveArea = waveformBounds_[row];
        waveArea.removeFromLeft(kIconCol + kColorStrip + 4);
        auto waveR = waveArea.reduced(10, 6);

        if (waveR.contains(e.getPosition()))
        {
            double norm = (double)(e.getPosition().x - waveR.getX()) / (double)waveR.getWidth();
            norm = juce::jlimit(0.0, 1.0, norm);

            if (auto* t = audioProcessor_.activeTransport.load())
            {
                t->setPosition(norm * t->getLengthInSeconds());
                playheadPos_ = norm;
                repaint();
                return;
            }
        }
    }

    if (row >= 0 && rowHasAudio(row))
        dragSourceRow_ = row;
}

void ISODrumsAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSourceRow_ < 0) return;
    if (e.getDistanceFromDragStart() < 8) return;

    int row = dragSourceRow_;
    dragSourceRow_ = -1;

    isDraggingOut_ = true;

    if (row == 0 && currentFile_.existsAsFile())
    {
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            { currentFile_.getFullPathName() }, false, this);
    }
    else
    {
        auto tempFile = writeStemToTempFile(row);
        if (tempFile != juce::File())
        {
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                { tempFile.getFullPathName() }, false, this);
        }
    }

    juce::Timer::callAfterDelay(500, [this] { isDraggingOut_ = false; });
}

void ISODrumsAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    int row = hitTestWaveformRow(e.getPosition());
    if (row >= 0 && rowHasAudio(row))
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

// ============================================================================
// ChangeListener / drag-drop (into app)
// ============================================================================

void ISODrumsAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint();
}

bool ISODrumsAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif" ||
            ext == ".flac" || ext == ".mp3")
            return true;
    }
    return false;
}

void ISODrumsAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.isEmpty() || isDraggingOut_) return;

    juce::File dropped(files[0]);

    if (currentFile_.existsAsFile() && dropped == currentFile_)
        return;

    if (tempDir_.isDirectory() && dropped.getParentDirectory() == tempDir_)
        return;

    loadFile(dropped);
}

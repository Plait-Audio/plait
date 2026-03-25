#include "PluginEditor.h"

// ---- Colour / label tables ----

const juce::Colour ISODrumsAudioProcessorEditor::kRowColours[kNumRows] = {
    juce::Colour(0xffe8e4df),   // Input   — warm white (inactive)
    juce::Colour(0xffc45c5c),   // Kick    — Plait red
    juce::Colour(0xffc98a4a),   // Snare   — Plait amber
    juce::Colour(0xff6b9e6b),   // Toms    — Plait green
    juce::Colour(0xff5c8ec4),   // Hi-Hat  — Plait blue
    juce::Colour(0xff8a6ec9),   // Cymbals — Plait violet
};

const char* ISODrumsAudioProcessorEditor::kRowLabels[kNumRows] = {
    "Input", "Kick", "Snare", "Toms", "Hi-Hat", "Cymbals"
};

// ---- Colours (aliased from ISOPalette for local convenience) ----
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
        // Consider the raw interval and its 2× / 4× multiples (8th / quarter / half)
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

    // Step 1 — copy input buffer under lock, then separate off the lock
    juce::AudioBuffer<float> inputCopy;
    double sampleRate = 44100.0;
    {
        juce::ScopedLock sl(p.resultLock);
        inputCopy   = p.inputBuffer;
        sampleRate  = p.inputSampleRate;
    }

    if (threadShouldExit()) { p.separationRunning.store(false); return; }

    auto result = p.getEngine().separate(inputCopy, sampleRate, &p.separationProgress);

    // Release the input copy now — stems are in result, raw audio no longer needed
    inputCopy = juce::AudioBuffer<float>();

    if (threadShouldExit()) { p.separationRunning.store(false); return; }

    // Step 2 — onset detection on each stem
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

    // Step 3 — store results under lock; estimate BPM while we have the data
    {
        juce::ScopedLock sl(p.resultLock);
        p.separationResult = std::move(result);
        p.allHits          = std::move(hits);
        p.detectedBpm      = estimateBpm(p.allHits, sampleRate);
    }

    p.separationProgress.store(1.0f);
    p.separationRunning.store(false);

    // Notify UI on message thread
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

    // Temp folder for WAV exports
    tempDir_ = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                           .getChildFile("ISODrumsTemp");
    tempDir_.createDirectory();

    // Thumbnails — register as change listeners so waveforms repaint
    thumbInput_  .addChangeListener(this);
    thumbKick_   .addChangeListener(this);
    thumbSnare_  .addChangeListener(this);
    thumbToms_   .addChangeListener(this);
    thumbHihat_  .addChangeListener(this);
    thumbCymbals_.addChangeListener(this);

    // ---- Header buttons ----
    loadButton_.setButtonText("Load");
    loadButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    loadButton_.onClick = [this]
    {
        juce::FileChooser chooser("Load audio file", {}, "*.wav;*.aiff;*.aif;*.flac;*.mp3");
        if (chooser.browseForFileToOpen())
            loadFile(chooser.getResult());
    };
    addAndMakeVisible(loadButton_);

    licenseButton_.setButtonText("License");
    licenseButton_.setColour(juce::TextButton::buttonColourId, kSurface);
    licenseButton_.onClick = [this] { showLicenseDialog(); };
    addAndMakeVisible(licenseButton_);

    // ---- Per-row play buttons ----
    for (int i = 0; i < kNumRows; ++i)
    {
        playButtons_[i].setButtonText(i == 0 ? "Play" : "Solo");
        playButtons_[i].setColour(juce::TextButton::buttonColourId, kSurface);
        playButtons_[i].setEnabled(false);
        const int idx = i;
        playButtons_[i].onClick = [this, idx]
        {
            if (soloStemIndex_ == idx)
                setSolo(-1);
            else
                setSolo(idx);
        };
        addAndMakeVisible(playButtons_[i]);
    }

    // ---- Per-stem save buttons ----
    for (int i = 0; i < 5; ++i)
    {
        saveButtons_[i].setButtonText("Save");
        saveButtons_[i].setColour(juce::TextButton::buttonColourId, kSurface);
        saveButtons_[i].setEnabled(false);
        const int idx = i;
        saveButtons_[i].onClick = [this, idx]
        {
            if (!audioProcessor_.getLicenseManager().canExportWav())
            {
                showExportLimitMessage(true);
                return;
            }

            // Stem order: kick=0, snare=1, toms=2, hihat=3, cymbals=4
            static const char* suffixes[5] = { "_kick", "_snare", "_toms", "_hihat", "_cymbals" };
            juce::FileChooser chooser("Save stem WAV", {},  "*.wav");
            if (!chooser.browseForFileToSave(true)) return;

            const juce::AudioBuffer<float>* buf = nullptr;
            {
                juce::ScopedLock sl(audioProcessor_.resultLock);
                switch (idx) {
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
        };
        addAndMakeVisible(saveButtons_[i]);
    }

    // ---- Toolbar ----
    separateButton_.setButtonText("Separate");
    separateButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    separateButton_.setEnabled(false);
    separateButton_.onClick = [this] { startSeparation(); };
    addAndMakeVisible(separateButton_);

    exportWavsButton_.setButtonText("Export WAVs");
    exportWavsButton_.setColour(juce::TextButton::buttonColourId, kSurface);
    exportWavsButton_.setEnabled(false);
    exportWavsButton_.onClick = [this] { exportWavs(); };
    addAndMakeVisible(exportWavsButton_);

    exportMidiButton_.setButtonText("Export MIDI");
    exportMidiButton_.setColour(juce::TextButton::buttonColourId, kSurface);
    exportMidiButton_.setEnabled(false);
    exportMidiButton_.onClick = [this] { showMidiDialog(); };
    addAndMakeVisible(exportMidiButton_);

    progressBar_.setColour(juce::ProgressBar::foregroundColourId, kAccent);
    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    setSize(960, 590);
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

    // Detach all transport sources before our unique_ptrs are destroyed
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
    constexpr int kHeaderH   = 50;
    constexpr int kRowH      = 72;
    constexpr int kToolbarH  = 55;
    constexpr int kStatusH   = 28;
    constexpr int kPad       = 8;
    constexpr int kLabelW    = 75;
    constexpr int kBtnW      = 58;
    constexpr int kBtnGap    = 4;

    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop(kHeaderH);
    loadButton_   .setBounds(header.removeFromRight(80) .reduced(kPad));
    licenseButton_.setBounds(header.removeFromRight(100).reduced(kPad));

    // Waveform rows
    for (int i = 0; i < kNumRows; ++i)
    {
        auto row = area.removeFromTop(kRowH);
        rowBounds_[i] = row;

        // Buttons on right
        auto btnArea = row.removeFromRight(kBtnW * 2 + kBtnGap * 3);
        btnArea.reduce(0, kPad);
        playButtons_[i].setBounds(btnArea.removeFromTop(btnArea.getHeight() / 2).reduced(kBtnGap / 2));
        if (i > 0)  // stems have a Save button
            saveButtons_[i - 1].setBounds(btnArea.reduced(kBtnGap / 2));

        // Waveform
        row.removeFromLeft(kPad);
        waveformBounds_[i] = row.reduced(0, kPad);
    }

    // Status
    auto statusRow = area.removeFromBottom(kStatusH);
    juce::ignoreUnused(statusRow);

    // Toolbar
    toolbarBounds_ = area.removeFromBottom(kToolbarH);
    auto toolbar = toolbarBounds_.reduced(kPad);
    separateButton_  .setBounds(toolbar.removeFromLeft(120));
    toolbar.removeFromLeft(kPad);
    exportWavsButton_.setBounds(toolbar.removeFromLeft(120));
    toolbar.removeFromLeft(kPad);
    exportMidiButton_.setBounds(toolbar.removeFromLeft(140));

    progressBar_.setBounds(toolbarBounds_.withTrimmedLeft(420).reduced(kPad));
}

// ============================================================================
// Paint
// ============================================================================

void ISODrumsAudioProcessorEditor::paint(juce::Graphics& g)
{
    // ── Body ──────────────────────────────────────────────────────────────────
    g.fillAll(kBg);

    // ── Toolbar surface + top separator ───────────────────────────────────────
    g.setColour(kSurface);
    g.fillRect(toolbarBounds_);
    g.setColour(kBorder);
    g.fillRect(toolbarBounds_.withHeight(1));

    // ── Header separator ──────────────────────────────────────────────────────
    g.fillRect(juce::Rectangle<int>(0, 50, getWidth(), 1));

    // ── ISO Drums horizontal logo (embedded PNG) ──────────────────────────────
    {
        static const juce::Image logoImg = juce::ImageCache::getFromMemory(
            BinaryData::logoisodrumshorizwhite_png,
            BinaryData::logoisodrumshorizwhite_pngSize);

        if (logoImg.isValid())
        {
            // Draw at fixed height of 22px, width proportional
            const float logoH = 22.f;
            const float logoW = logoH * (float)logoImg.getWidth() / (float)logoImg.getHeight();
            const float logoX = 14.f;
            const float logoY = (50.f - logoH) * 0.5f;
            g.setOpacity(0.92f);
            g.drawImage(logoImg,
                        (int)logoX, (int)logoY, (int)logoW, (int)logoH,
                        0, 0, logoImg.getWidth(), logoImg.getHeight());
            g.setOpacity(1.0f);
        }
        else
        {
            // Fallback: hand-drawn mark if image didn't load
            g.setColour(kText);
            g.setFont(ISOLookAndFeel::font(14.f, true));
            g.drawText("ISO DRUMS", juce::Rectangle<int>(14, 0, 160, 50),
                       juce::Justification::centredLeft);
        }
    }

    // ── License status chip ───────────────────────────────────────────────────
    auto& lm = audioProcessor_.getLicenseManager();
    const auto licState = lm.getState();
    juce::String licText;
    juce::Colour licDot;
    switch (licState)
    {
        case LicenseState::Trial:
            licText = juce::String(lm.trialDaysRemaining()) + "D TRIAL"
                    + "   " + juce::String(lm.wavExportsRemaining()) + "W  "
                    + juce::String(lm.midiExportsRemaining()) + "M";
            licDot = juce::Colour(0xffffaa33);
            break;
        case LicenseState::TrialExpired:
            licText = "TRIAL EXPIRED";
            licDot  = juce::Colour(0xffff4444);
            break;
        case LicenseState::Licensed:
            licText = "LICENSED";
            licDot  = juce::Colour(0xff44cc66);
            break;
        case LicenseState::LicenseCheckNeeded:
            licText = "CHECK LICENSE";
            licDot  = juce::Colour(0xffffaa33);
            break;
    }

    // Chip: sits between title and the right-side buttons
    {
        g.setFont(ISOLookAndFeel::font(9.5f, true));
        const float textW = g.getCurrentFont().getStringWidthFloat(licText);
        const float chipW = textW + 28.0f;
        const float chipH = 20.0f;
        const float chipX = 200.0f;
        const float chipY = (50.0f - chipH) * 0.5f;

        juce::Rectangle<float> chip(chipX, chipY, chipW, chipH);
        g.setColour(kSurface);
        g.fillRoundedRectangle(chip, 3.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(chip.reduced(0.5f), 3.0f, 1.0f);

        // Status LED dot
        g.setColour(licDot);
        const float dotR = 3.5f;
        g.fillEllipse(chipX + 9.0f - dotR, chip.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);

        // Text
        g.setColour(kText.withAlpha(0.78f));
        g.drawText(licText,
                   juce::Rectangle<float>(chipX + 19.0f, chipY, chipW - 22.0f, chipH),
                   juce::Justification::centredLeft);
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
        paintWaveformRow(g, rowBounds_[i], kRowLabels[i], *thumbs[i],
                         kRowColours[i], active, draggable);
    }
}

void ISODrumsAudioProcessorEditor::paintWaveformRow(juce::Graphics& g,
                                                     juce::Rectangle<int> bounds,
                                                     const juce::String& label,
                                                     juce::AudioThumbnail& thumb,
                                                     juce::Colour waveColour,
                                                     bool active,
                                                     bool draggable) const
{
    // ── Row body ──────────────────────────────────────────────────────────────
    g.setColour(active ? kRowBg.brighter(0.07f) : kRowBg);
    g.fillRect(bounds.reduced(0, 1));

    // ── Colored left accent strip (3 px) ──────────────────────────────────────
    auto strip = bounds.removeFromLeft(3);
    g.setColour(waveColour.withAlpha(active ? 0.95f : 0.55f));
    g.fillRect(strip);

    // ── Skip button area on right (128 px = 2 × 58 + 3 × 4) ─────────────────
    bounds.removeFromRight(128);

    // ── Label panel ───────────────────────────────────────────────────────────
    auto labelPanel = bounds.removeFromLeft(78);
    g.setColour(kSurface);
    g.fillRect(labelPanel);

    // Right edge separator
    g.setColour(kBorder);
    g.fillRect(juce::Rectangle<int>(labelPanel.getRight(), labelPanel.getY(), 1, labelPanel.getHeight()));

    // LED indicator dot
    const float dotDiam = 6.0f;
    auto dotCol = waveColour.withAlpha(active ? 1.0f : 0.5f);
    auto dotArea = labelPanel.removeFromLeft(22);
    const auto dotCentre = dotArea.getCentre().toFloat();
    g.setColour(dotCol);
    g.fillEllipse(dotCentre.x - dotDiam * 0.5f, dotCentre.y - dotDiam * 0.5f,
                  dotDiam, dotDiam);
    // Glow ring
    g.setColour(dotCol.withAlpha(active ? 0.22f : 0.10f));
    g.fillEllipse(dotCentre.x - dotDiam, dotCentre.y - dotDiam,
                  dotDiam * 2.0f, dotDiam * 2.0f);

    // Label text (ALL CAPS, Inter)
    g.setColour(active ? kText : kText.withAlpha(0.72f));
    g.setFont(ISOLookAndFeel::font(11.f, true));
    g.drawText(label.toUpperCase(), labelPanel.reduced(0, 4),
               juce::Justification::centredLeft);

    // ── Waveform display (inset "screen") ─────────────────────────────────────
    auto waveOuter = bounds.reduced(5, 6);
    g.setColour(kBg);
    g.fillRoundedRectangle(waveOuter.toFloat(), 2.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(waveOuter.toFloat().reduced(0.5f), 2.0f, 1.0f);
    auto waveR = waveOuter.reduced(3, 3);

    if (thumb.getTotalLength() > 0.0)
    {
        g.setColour(waveColour.withAlpha(active ? 0.92f : 0.62f));
        thumb.drawChannels(g, waveR, 0.0, thumb.getTotalLength(), 1.0f);

        if (draggable)
        {
            g.setColour(kMuted.withAlpha(0.65f));
            g.setFont(ISOLookAndFeel::font(8.5f, true));
            auto hintR = waveR.withHeight(12).withY(waveR.getBottom() - 12);
            g.drawText("DRAG TO DAW", hintR.withTrimmedLeft(hintR.getWidth() - 82),
                       juce::Justification::centredRight);
        }
    }
    else
    {
        g.setColour(kMuted.withAlpha(0.45f));
        g.setFont(ISOLookAndFeel::font(10.5f, true));
        g.drawText("NO AUDIO", waveR, juce::Justification::centred);
    }
}

// ============================================================================
// File loading
// ============================================================================

void ISODrumsAudioProcessorEditor::loadFile(const juce::File& file)
{
    auto* reader = formatManager_.createReaderFor(file);
    if (reader == nullptr) return;

    currentFile_    = file;
    inputFileName_  = file.getFileNameWithoutExtension();

    // Load full buffer into memory
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buf(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&buf, 0, numSamples, 0, true, true);

    {
        juce::ScopedLock sl(audioProcessor_.resultLock);
        audioProcessor_.inputBuffer     = std::move(buf);
        audioProcessor_.inputSampleRate = reader->sampleRate;
    }

    // Detach old source before resetting unique_ptr
    audioProcessor_.activeTransport.store(nullptr);
    audioProcessor_.transportInput.setSource(nullptr);
    inputSource_.reset();

    // New playback source
    inputSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    audioProcessor_.transportInput.setSource(
        inputSource_.get(), 0, nullptr,
        reader->sampleRate, static_cast<int>(reader->numChannels));

    // Waveform thumbnail
    thumbInput_.setSource(new juce::FileInputSource(file));

    fileLoaded_ = true;
    stemsDone_  = false;
    soloStemIndex_ = -1;

    separateButton_.setEnabled(audioProcessor_.getEngine().isReady());
    playButtons_[0].setEnabled(true);
    playButtons_[0].setColour(juce::TextButton::buttonColourId, kRowColours[0]);

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
    progressBar_.setVisible(true);

    setSolo(-1);
    separationThread_.startThread();
}

void ISODrumsAudioProcessorEditor::onSeparationComplete()
{
    updateStemThumbnails();
    attachStemSources();

    stemsDone_ = true;
    separateButton_.setEnabled(true);
    exportWavsButton_.setEnabled(true);
    exportWavsButton_.setColour(juce::TextButton::buttonColourId, kAccent);
    exportMidiButton_.setEnabled(true);
    exportMidiButton_.setColour(juce::TextButton::buttonColourId, kAccent);

    for (int i = 1; i < kNumRows; ++i)
    {
        playButtons_[i].setEnabled(true);
        playButtons_[i].setColour(juce::TextButton::buttonColourId, kRowColours[i]);
    }
    for (int i = 0; i < 5; ++i)
    {
        saveButtons_[i].setEnabled(true);
        saveButtons_[i].setColour(juce::TextButton::buttonColourId, kSurface);
    }

    progressBar_.setVisible(false);
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
    // Detach existing sources first
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
    // Stop whatever was playing
    if (auto* t = audioProcessor_.activeTransport.load())
    {
        t->stop();
        t->setPosition(0.0);
    }
    audioProcessor_.activeTransport.store(nullptr);

    soloStemIndex_ = stemIndex;

    if (stemIndex < 0)
    {
        repaint();
        return;
    }

    juce::AudioTransportSource* targets[kNumRows] = {
        &audioProcessor_.transportInput,
        &audioProcessor_.transportKick,
        &audioProcessor_.transportSnare,
        &audioProcessor_.transportToms,
        &audioProcessor_.transportHihat,
        &audioProcessor_.transportCymbals,
    };

    auto* chosen = targets[stemIndex];
    chosen->setPosition(0.0);
    chosen->start();
    audioProcessor_.activeTransport.store(chosen);

    repaint();
}

// ============================================================================
// Export
// ============================================================================

void ISODrumsAudioProcessorEditor::showExportLimitMessage(bool isWav)
{
    const auto& lm = audioProcessor_.getLicenseManager();
    const auto state = lm.getState();

    juce::String title, msg;
    if (state == LicenseState::TrialExpired)
    {
        title = "Trial Expired";
        msg   = "Your 14-day trial has expired.\n\n"
                "Click \"License...\" to activate ISO Drums and unlock unlimited exports.";
    }
    else
    {
        title = isWav ? "WAV Export Limit Reached" : "MIDI Export Limit Reached";
        msg   = juce::String("You have used all ")
              + (isWav ? juce::String(LicenseManager::kMaxWavExports) + " trial WAV exports."
                       : juce::String(LicenseManager::kMaxMidiExports) + " trial MIDI exports.")
              + "\n\nClick \"License...\" to activate ISO Drums and unlock unlimited exports.";
    }
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, msg);
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
    opts.dialogTitle             = "ISO Drums License";
    opts.dialogBackgroundColour  = juce::Colour(0xff1e1e38);
    opts.componentToCentreAround = this;
    opts.useNativeTitleBar       = false;
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
    opts.dialogBackgroundColour   = juce::Colour(0xff1e1e38);
    opts.componentToCentreAround  = this;
    opts.useNativeTitleBar        = false;
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
    if (audioProcessor_.separationRunning.load())
    {
        progressValue_ = static_cast<double>(audioProcessor_.separationProgress.load());
        progressBar_.repaint();
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
    if (row >= 0 && rowHasAudio(row))
        dragSourceRow_ = row;
}

void ISODrumsAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSourceRow_ < 0) return;
    if (e.getDistanceFromDragStart() < 8) return;

    int row = dragSourceRow_;
    dragSourceRow_ = -1;

    if (row == 0 && currentFile_.existsAsFile())
    {
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            { currentFile_.getFullPathName() }, false, this);
        return;
    }

    auto tempFile = writeStemToTempFile(row);
    if (tempFile != juce::File())
    {
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            { tempFile.getFullPathName() }, false, this);
    }
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
    if (files.isEmpty()) return;
    loadFile(juce::File(files[0]));
}

# ISO Drums — Build Plan

**ISO Drums** is a commercial standalone macOS (arm64) application and audio plug-in (AU / VST3) that separates a stereo drum mix into five stems (kick, snare, toms, hi-hat, cymbals) and exports the detected hits as a Standard MIDI File (SMF). It combines the neural drum demixing engine from [LARS](https://github.com/EdoardoMor/LARS) with a new onset-detection + MIDI-export pipeline.

Users purchase through a website, receive a license key and receipt by email, and can trial the full app for 14 days (capped at 5 WAV + 5 MIDI exports during trial).

> **This document is the authoritative spec.** An agent should be able to build ISO Drums end-to-end from this file. Every section marks the recommended Cursor agent tier.

---

## Token Budget & Agent Instructions

**This project is built on a limited Cursor budget. Every agent invoked on this plan MUST follow these rules to minimize wasted tokens:**

### Model selection rules

| Use this tier | When |
|---------------|------|
| **Auto / fast** | Boilerplate, scaffolding, file copies, CMake plumbing, `.gitignore`, `setup.sh`, mechanical refactors where the pattern is already shown in this doc. |
| **Sonnet 4.6** | Implementation work with clear specs: porting LARS code, MIDI export, UI wiring, threading, server API, website. The spec in this doc is detailed enough that Sonnet can execute without exploration. |
| **Opus 4.6** | Only for genuinely hard algorithmic work: onset detection tuning, edge-case robustness, and security-sensitive licensing logic. **Do not use Opus for anything that has a clear spec or pattern to follow.** |

### Token-saving practices for all agents

1. **Read this plan first, not the codebase.** This doc contains the APIs, algorithms, file layout, and known issues. Only read source files when this doc is insufficient.
2. **Write code in large, complete blocks.** Prefer one 200-line file write over 10 small edits. Each tool call costs tokens for context re-serialization.
3. **Don't re-read files you've already read this session.** Cache what you need in your working memory.
4. **Don't explore speculatively.** If the plan says "port `Utils.cpp`," read `Utils.cpp` once, write the new file, and move on. Don't grep the codebase for related patterns.
5. **Batch independent work.** If Phase 0 has 5 independent file-creation tasks, do them all in one turn with parallel tool calls.
6. **Don't re-read this plan file between turns.** It's long (~700 lines). Read it once at the start of a session, then work from memory.
7. **Fail fast.** If a build fails, read the error, fix it, and rebuild. Don't add speculative fixes for errors you haven't seen.
8. **Skip verbose commit messages and explanations.** The user values working code over narration. Keep responses short.

### Budget breakdown (estimated)

| Phase | Tier | Est. tokens | Est. on-demand cost |
|-------|------|------------|-------------------|
| 0 Scaffold | Auto | ~55k | $0.05 |
| 1 Separation port | Sonnet | ~250k | $2.50 |
| 2 Onset detection | **Opus** | ~185k | **$8.50** |
| 3 MIDI export | Sonnet | ~125k | $1.50 |
| 4 UI | Sonnet + Auto | ~310k | $3.50 |
| 5 Threading | Sonnet | ~125k | $1.50 |
| 6 LibTorch bundling | Auto | ~35k | $0.05 |
| 7 Polish | Opus + Auto | ~145k | **$5.50** |
| 8 Licensing | Opus + Sonnet | ~245k | **$8.00** |
| 9 Website | Sonnet + Auto | ~130k | $1.50 |
| Debug overhead (~40%) | mixed | ~330k | $5.00 |
| **Total** | | **~2.3M** | **~$38** |

**The biggest cost driver is Opus output tokens.** To stay under budget:
- Use Sonnet for the first pass of Phase 2 (onset detection). Escalate to Opus only for tuning the adaptive threshold and edge cases.
- Use Sonnet for Phase 8 licensing client code. Escalate to Opus only for the cryptographic signing / anti-tamper logic.
- Never use Opus for UI, MIDI file I/O, CMake, or website work.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Repository Layout](#2-repository-layout)
3. [Phase 0 — Scaffold & Build System](#3-phase-0--scaffold--build-system)
4. [Phase 1 — Port LARS Separation Engine](#4-phase-1--port-lars-separation-engine)
5. [Phase 2 — Onset Detection (DSP)](#5-phase-2--onset-detection-dsp)
6. [Phase 3 — MIDI Export](#6-phase-3--midi-export)
7. [Phase 4 — UI](#7-phase-4--ui)
8. [Phase 5 — Threading & Progress](#8-phase-5--threading--progress)
9. [Phase 6 — LibTorch Bundling & Packaging](#9-phase-6--libtorch-bundling--packaging)
10. [Phase 7 — Polish & Edge Cases](#10-phase-7--polish--edge-cases)
11. [Phase 8 — Licensing & Trial System](#11-phase-8--licensing--trial-system)
12. [Phase 9 — Website, Store & Distribution](#12-phase-9--website-store--distribution)
13. [Web App Alternative (Exploration)](#13-web-app-alternative-exploration)
14. [GM Drum Map Reference](#14-gm-drum-map-reference)
15. [Key Decisions Log](#15-key-decisions-log)
16. [Known LARS Issues to Fix During Port](#16-known-lars-issues-to-fix-during-port)

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                      ISO Drums                           │
│                                                          │
│  ┌──────────┐   ┌──────────────┐   ┌──────────────────┐ │
│  │ File I/O │──▶│  Separation  │──▶│ Onset Detection  │ │
│  │ (load    │   │  Engine      │   │ (per-stem DSP)   │ │
│  │  WAV/    │   │  (LarsNet    │   │                  │ │
│  │  AIFF/   │   │   via Torch  │   │  envelope follow │ │
│  │  drag &  │   │   Script)    │   │  peak picking    │ │
│  │  drop)   │   │              │   │  velocity est.   │ │
│  └──────────┘   └──────┬───────┘   └────────┬─────────┘ │
│                        │                     │           │
│                        ▼                     ▼           │
│              ┌──────────────┐      ┌──────────────────┐  │
│              │ WAV Export   │      │ MIDI Export       │  │
│              │ (existing    │      │ (new — SMF Type 1 │  │
│              │  LARS path)  │      │  via JUCE)        │  │
│              └──────────────┘      └──────────────────┘  │
│                                                          │
│  ┌──────────────────────────────────────────────────────┐│
│  │                     UI (JUCE)                        ││
│  │  waveform display · stem solos · progress bar        ││
│  │  "Separate" button · "Export MIDI" button            ││
│  │  MIDI settings panel (BPM, sensitivity, note map)    ││
│  └──────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────┘
```

**Data flow summary:**

1. User loads a stereo drum audio file (WAV, AIFF, FLAC, MP3).
2. STFT → five TorchScript models → ISTFT → five `at::Tensor` stem buffers (stereo float).
3. Each stem buffer is mono-summed → envelope follower → adaptive peak picking → `vector<DrumHit>`.
4. Hits are mapped to GM drum notes, velocity-scaled, and written as a Type 1 SMF (one track per stem).
5. User can also export stems as WAVs (existing LARS flow).
6. Drag-and-drop export: drag a stem waveform out of ISO Drums into a DAW track to export WAV; drag the MIDI icon out to export the .mid file directly into a DAW.

**Licensing flow:**

1. User downloads app from website (free trial, fully functional for 14 days).
2. Trial limits: 5 WAV exports + 5 MIDI exports total. Separation and playback are unlimited.
3. User purchases license on website → receives license key by email + separate receipt email.
4. User enters license key in-app → key validated against server → app unlocked permanently.

**Priority:** Get separation + MIDI export working first. GUI polish is secondary. Ship function, then skin.

---

## 2. Repository Layout

> **Agent tier: Auto**

```
ISO-Drums/
├── _ISO-Drums-Plan.md            ← this file
├── CMakeLists.txt                 ← top-level CMake
├── scripts/
│   └── bundle_libtorch_macos.sh   ← post-build LibTorch bundler (from LARS build)
├── src/
│   ├── Main.cpp                   ← (if Standalone-only) or PluginProcessor / PluginEditor
│   ├── ISOProcessor.h / .cpp      ← AudioProcessor (host integration, playback)
│   ├── ISOEditor.h / .cpp         ← AudioProcessorEditor (UI)
│   ├── SeparationEngine.h / .cpp  ← wraps TorchScript model loading + STFT/ISTFT
│   ├── OnsetDetector.h / .cpp     ← per-stem onset detection
│   ├── MidiExporter.h / .cpp      ← builds MidiFile from detected hits
│   ├── DrumHit.h                  ← struct { double timeSec; float velocity; int midiNote; }
│   ├── DrumMap.h                  ← GM note constants + user-configurable mapping
│   ├── LicenseManager.h / .cpp    ← trial countdown, export limits, key validation
│   ├── DragSource.h / .cpp        ← drag-out-of-app for WAV and MIDI (extends ClickableArea)
│   ├── Utils.h / .cpp             ← STFT helpers (cleaned-up port from LARS)
│   └── ClickableArea.h            ← waveform drag-to-export component (port from LARS)
├── Resources/
│   ├── my_scripted_module_kick.pt
│   ├── my_scripted_module_snare.pt
│   ├── my_scripted_module_toms.pt
│   ├── my_scripted_module_hihat.pt
│   ├── my_scripted_module_cymbals.pt
│   ├── my_scripted_module.pt
│   └── (UI images — ported / new branding)
├── web/                           ← marketing site + store (Phase 9, separate deploy)
│   ├── index.html                 ← landing page
│   └── ...
├── third_party/
│   ├── libtorch/                  ← CPU arm64 (downloaded, .gitignored)
│   └── JUCE/                     ← v8.0.4 (cloned, .gitignored)
└── .gitignore
```

---

## 3. Phase 0 — Scaffold & Build System

> **Agent tier: Auto**

### Tasks

1. Create `CMakeLists.txt` modeled on the LARS one, but cleaned up:
   - Project name `ISODrums`, product name `"ISO Drums"`.
   - `CMAKE_CXX_STANDARD 17`.
   - `set(CMAKE_PREFIX_PATH ...)` for LibTorch, `add_subdirectory(...)` for JUCE 8.0.4.
   - `juce_add_plugin(ISODrums ...)` with formats `AU VST3 Standalone`.
   - **`IS_SYNTH FALSE`** (this is an effect / utility, not a synth).
   - Binary data target for `.pt` models and images.
   - Link `juce::juce_audio_utils`, `"${TORCH_LIBRARIES}"`.
   - Apple post-build LibTorch bundling (copy `scripts/bundle_libtorch_macos.sh` from the LARS build verbatim, ensure Unix LF line endings).
   - Re-copy bundles to `~/Library/Audio/Plug-Ins/` after bundling (same pattern as LARS build).
2. Create `.gitignore`: `build/`, `third_party/`, `.DS_Store`, `*.user`.
3. Add a `setup.sh` that:
   - Downloads LibTorch CPU arm64 2.5.1 into `third_party/libtorch/`.
   - Shallow-clones JUCE 8.0.4 into `third_party/JUCE/`.
4. Copy `.pt` model files and required images from the LARS repo `drums_demix/Resources/` into `Resources/`.
5. Verify `cmake -B build . && cmake --build build` compiles a stub Standalone that launches.

### Acceptance

- `LARS Drum Demixing` repo at `/Users/zachmcnair/repos/apps/LARS Drum Demixing` is the source of truth for model weights and the bundle script.
- Standalone app window opens (can be empty).

---

## 4. Phase 1 — Port LARS Separation Engine

> **Agent tier: Sonnet 4.6** (moderate refactoring of existing code into cleaner abstractions)

### What to port

The core separation logic lives in three LARS files:

| LARS file | What it does | ISO Drums target |
|-----------|-------------|-----------------|
| `Utils.cpp` | `batch_stft`, `batch_istft`, `pad_stft_input`, `_stft`, `_istft` (n_fft=4096, hop=win/4, Hann window) | `Utils.h/.cpp` — clean up: proper header/impl split, remove commented-out code, use `const&` where appropriate |
| `PluginEditor.cpp` lines 624–740 | "Separate" button handler: load audio → tensor → STFT → model forward → ISTFT → per-stem tensors | `SeparationEngine::separate(juce::AudioBuffer<float>& input) → SeparationResult` |
| `PluginEditor.cpp` `InferModels()` | Forward pass through 5 TorchScript modules, squeeze, ISTFT | Fold into `SeparationEngine` |

### SeparationEngine API

```cpp
struct SeparationResult {
    juce::AudioBuffer<float> kick;     // stereo
    juce::AudioBuffer<float> snare;
    juce::AudioBuffer<float> toms;
    juce::AudioBuffer<float> hihat;
    juce::AudioBuffer<float> cymbals;
    double sampleRate;
};

class SeparationEngine {
public:
    SeparationEngine();                        // loads 5 TorchScript modules from BinaryData
    SeparationResult separate(const juce::AudioBuffer<float>& input, double sampleRate);
private:
    torch::jit::Module kickModel, snareModel, tomsModel, hihatModel, cymbalsModel;
    Utils utils;                               // STFT helper (n_fft=4096)
    void loadModels();
    at::Tensor bufferToTensor(const juce::AudioBuffer<float>& buf);
    juce::AudioBuffer<float> tensorToBuffer(const at::Tensor& t, int numSamples);
};
```

### Key cleanup from LARS

- Models are loaded from `BinaryData` via `std::stringstream` (same pattern LARS uses). Load once in the constructor, **not** re-loaded after every separation. LARS re-loads models after each run as a crash workaround — investigate and fix the root cause (likely a tensor aliasing / graph retention issue; calling `torch::NoGradGuard` or `c10::InferenceMode` should fix it).
- Remove all hardcoded Windows paths (`C:/Users/Riccardo/...`).
- Remove unused `NeuralNetwork.h/.cpp` (that's a training-time toy network, not used by the plugin).
- `Utils` STFT defaults: `n_fft=4096`, `win_length=n_fft`, `hop_length=win_length/4`, `power=1.0`, `center=true`. Keep these values; they match the trained LarsNet models.

### Acceptance

- Load a WAV → get 5 stem `AudioBuffer<float>`s back → play them and they sound correct.

---

## 5. Phase 2 — Onset Detection (DSP)

> **Agent tier: Start with Sonnet 4.6 for initial implementation. Escalate to Opus 4.6 ONLY for adaptive threshold tuning and edge-case robustness.**
>
> **Budget note:** This phase is the largest Opus cost driver. The algorithm is fully specified below — Sonnet can implement it from this spec. Use Opus only if Sonnet's initial detector produces poor results on real audio (double triggers, missed ghost notes, bad velocity curves).

This is the hardest part of the project. The separated stems are already quite clean per-instrument, so a **classical DSP approach** (envelope follower + peak picking) is the right call — no need for a second ML model.

### OnsetDetector API

```cpp
struct DrumHit {
    double timeSec;       // onset time in seconds from file start
    float velocity;       // 0.0–1.0 (maps to MIDI 1–127)
    int midiNote;         // GM drum note number
};

struct OnsetParams {
    float sensitivityDb  = -30.0f; // threshold relative to peak (dB)
    float minIntervalMs  =  30.0f; // minimum ms between triggers (anti-double-trigger)
    float envelopeAttackMs = 1.0f; // envelope follower attack
    float envelopeReleaseMs = 50.0f; // envelope follower release
};

class OnsetDetector {
public:
    std::vector<DrumHit> detect(
        const juce::AudioBuffer<float>& stemBuffer,
        double sampleRate,
        int midiNote,
        const OnsetParams& params = {}
    );
};
```

### Algorithm (per stem)

1. **Mono sum**: `(L + R) / 2`.
2. **Rectify**: `abs(sample)`.
3. **Envelope follower** (one-pole IIR):
   - `attackCoeff  = exp(-1.0 / (sampleRate * attackMs / 1000.0))`
   - `releaseCoeff = exp(-1.0 / (sampleRate * releaseMs / 1000.0))`
   - `env[n] = sample > env[n-1] ? attackCoeff * env[n-1] + (1-attackCoeff) * sample : releaseCoeff * env[n-1]`
4. **Adaptive threshold**: `threshold = peakEnvelope * db_to_linear(sensitivityDb)`. Use a sliding window (e.g. 200 ms) to compute local peak so quiet passages still trigger.
5. **Peak picking**: find local maxima of the envelope that exceed the threshold. A hit is registered at the **first sample** where the envelope crosses above threshold (onset, not peak).
6. **Minimum interval gate**: suppress any hit within `minIntervalMs` of the previous hit on the same stem.
7. **Velocity estimation**: `velocity = clamp(envelope_at_onset / local_peak, 0.0, 1.0)`.

### Stem-specific considerations

| Stem | GM Note(s) | Notes |
|------|-----------|-------|
| Kick | 36 (C1) | Cleanest stem. Default params work well. |
| Snare | 38 (D1) | May include ghost notes — let them through at low velocity. Consider 37 (side stick) for very quiet hits if feasible. |
| Toms | 45 (low), 47 (mid), 50 (high) | **V1: map all to 47 (mid tom).** V2: add pitch estimation via zero-crossing rate or autocorrelation to split into 3 toms. |
| Hi-hat | 42 (closed), 46 (open) | **V1: map all to 42 (closed).** V2: classify open vs closed by hit duration (longer decay = open). Pedal (44) is aspirational. |
| Cymbals | 49 (crash 1), 51 (ride) | **V1: map all to 49 (crash).** V2: frequency band split — ride is higher, crash is broader. |

### Testing

- Use a simple kick pattern with known timing (4-on-the-floor at 120 BPM) and verify onsets land within ±5 ms.
- Use a full drum loop and spot-check against manual onset marking.

---

## 6. Phase 3 — MIDI Export

> **Agent tier: Sonnet 4.6** (straightforward JUCE API usage, but correctness matters)

### MidiExporter API

```cpp
struct MidiExportSettings {
    double bpm             = 120.0;
    int ppq                = 480;     // pulses per quarter note
    bool quantize          = false;   // if true, snap to nearest grid
    int quantizeGrid       = 16;      // 1/16 note grid
    bool separateTracks    = true;    // true = 1 track per stem; false = all on 1 track
};

class MidiExporter {
public:
    bool exportToFile(
        const std::vector<DrumHit>& allHits,  // combined hits from all stems
        const juce::File& outputFile,
        const MidiExportSettings& settings = {}
    );

    juce::MidiFile buildMidiFile(
        const std::vector<DrumHit>& allHits,
        const MidiExportSettings& settings = {}
    );
};
```

### Implementation details

1. Create a `juce::MidiFile`, set time format to `settings.ppq`.
2. Add a tempo track (track 0): `juce::MidiMessage::tempoMetaEvent(microsPerBeat)` at tick 0.
3. For each stem (or one combined track):
   - Create a `juce::MidiMessageSequence`.
   - For each `DrumHit`:
     - Convert `timeSec` to ticks: `tick = (timeSec / 60.0) * bpm * ppq`.
     - If `quantize`, snap to nearest grid division.
     - Add `noteOn(channel=10, note, velocity_0_127)` at `tick`.
     - Add `noteOff(channel=10, note, 0)` at `tick + shortDuration` (e.g. `ppq / 8`).
   - Append sequence to MidiFile.
4. Write via `juce::MidiFile::writeTo(juce::FileOutputStream)`.

**MIDI channel 10** is the GM percussion channel (0-indexed: channel 9 in JUCE's 0-based API).

### Quantization (optional, but nice)

- Nearest grid: `quantizedTick = round(tick / gridTicks) * gridTicks` where `gridTicks = ppq * 4 / quantizeGrid`.
- Expose as a checkbox + dropdown (1/4, 1/8, 1/16, 1/32).

### Acceptance

- Export a MIDI file, open in a DAW or MIDI viewer, verify note positions match the audio.
- Round-trip: load the MIDI file in a DAW with a drum sampler → should sound rhythmically identical to the source stems.

---

## 7. Phase 4 — UI

> **Agent tier: Sonnet 4.6** (UI layout) + **Auto** (boilerplate wiring). **Never Opus for UI work.**

### Layout concept

```
┌──────────────────────────────────────────────────────────┐
│  ISO Drums                                    [Load] [▶] │
├──────────────────────────────────────────────────────────┤
│  Input ██████████████████████████████████████             │
├──────────────────────────────────────────────────────────┤
│  Kick   ██████▓▓▓░░░░░░░█████▓▓░░░░      [Solo] [Save] │
│  Snare  ░░░░████▓▓░░░░░░░░░░████▓▓░░░    [Solo] [Save] │
│  Toms   ░░░░░░░░░░░░████▓▓░░░░░░░░░░░    [Solo] [Save] │
│  HiHat  ██▓██▓██▓██▓██▓██▓██▓██▓██▓██▓   [Solo] [Save] │
│  Cymbal ░░░░░░░░████░░░░░░░░░░████░░░░   [Solo] [Save] │
├──────────────────────────────────────────────────────────┤
│  [Separate]        [Export WAVs]     [Export MIDI...]    │
├──────────────────────────────────────────────────────────┤
│  Progress: ████████████░░░░░░░ 65%                       │
└──────────────────────────────────────────────────────────┘
```

### "Export MIDI..." dialog

Clicking "Export MIDI..." opens a modal or panel with:

- **BPM** — text field, default 120. Auto-detect button (aspirational: use `aubio`-style tempo estimation on the input).
- **Sensitivity** — slider (dB), controls `OnsetParams::sensitivityDb`.
- **Quantize** — checkbox + grid dropdown (Off, 1/4, 1/8, 1/16, 1/32).
- **Tracks** — radio: "One track per stem" / "All on one track".
- **Note mapping** — 5 rows showing stem → GM note number (editable for power users).
- **[Export]** button → `FileChooser` save dialog → `.mid` file.

### Drag and drop (both into and out of the app)

**Drag IN:**
- Drag audio files onto the window to load (same as LARS `FileDragAndDropTarget`).

**Drag OUT (WAV stems):**
- Each stem waveform row is a drag source. User drags the waveform onto a DAW track → ISO Drums writes a temp WAV to `~/Music/ISODrumsTemp/` and provides the file path via `juce::DragAndDropContainer::performExternalDragDropOfFiles`. This is the same pattern LARS uses with `ClickableArea`, but extended to all stems.
- Each drag-out counts as 1 WAV export for trial limit purposes.

**Drag OUT (MIDI):**
- A MIDI icon or "MIDI" label next to the export button is also a drag source. Dragging it builds the `.mid` file into the temp directory on-the-fly and hands the path to the OS drag.
- Counts as 1 MIDI export for trial limits.

**Implementation:** Extend `ClickableArea` into `DragSource` (or just use `juce::DragAndDropContainer` on the editor + make each stem row respond to `mouseDrag`). The key JUCE API is `DragAndDropContainer::performExternalDragDropOfFiles({filePath}, ...)`.

### Branding

- Replace LARS images with ISO Drums branding. For V1, text-based header is fine; custom graphics can come later.
- Background color scheme: dark gray (#1a1a2e) with accent color (#e94560) for waveforms and active states.
- **Priority is function, not polish.** V1 GUI can be minimal — just needs to be usable. Design pass comes after core works.

### License status in UI

- Show in the bottom-left or top-right corner: "Trial: X days left (Y WAV / Z MIDI exports remaining)" or "Licensed to: user@email.com".
- "Enter License Key..." button opens a dialog with a text field + Activate button.

---

## 8. Phase 5 — Threading & Progress

> **Agent tier: Sonnet 4.6**

### Problem

LARS runs separation on the message thread and uses `MessageManager::runDispatchLoopUntil(500)` to pump the event loop during long operations. This is fragile and blocks the UI.

### Solution

Use `juce::ThreadWithProgressWindow` or a manual `juce::Thread` + `juce::ProgressBar`:

```
User clicks "Separate"
  → disable buttons
  → spawn worker thread:
      1. SeparationEngine::separate()   (progress 0–80%)
      2. OnsetDetector::detect() × 5    (progress 80–95%)
      3. Store results                   (progress 95–100%)
  → on completion (MessageManager callback):
      → enable "Export WAVs", "Export MIDI..."
      → display stem waveforms
```

### Rules

- **No Torch calls on the message thread.** All model inference and tensor ops happen on the worker.
- Post results back via `juce::MessageManager::callAsync` or `juce::AsyncUpdater`.
- Progress reporting: the worker writes to an `std::atomic<float>` that the UI polls via `timerCallback`.

---

## 9. Phase 6 — LibTorch Bundling & Packaging

> **Agent tier: Auto** (copy from LARS build, zero creative work)
>
> **Budget note:** This phase should cost near-zero. The script and CMake block exist verbatim in the LARS repo. Copy, adjust target names, done.

Identical to the LARS build. Copy:

- `scripts/bundle_libtorch_macos.sh` (ensure Unix LF).
- The CMake `APPLE` post-build block that calls the script on `ISODrums_AU`, `ISODrums_VST3`, `ISODrums_Standalone` and re-copies to `~/Library/Audio/Plug-Ins/`.

### Packaging

Create `dist/ISO-Drums-macOS-arm64.zip` containing:

- `ISO Drums.app` (Standalone)
- `ISO Drums.component` (AU)
- `ISO Drums.vst3` (VST3)
- `INSTALL.txt`

---

## 10. Phase 7 — Polish & Edge Cases

> **Agent tier: Sonnet 4.6 for the first pass. Opus 4.6 only for audio edge cases that Sonnet can't resolve (sample rate resampling correctness, STFT padding crashes). Auto for mechanical fixes.**

### Audio edge cases

- [ ] **Mono input**: detect and duplicate to stereo before separation.
- [ ] **Sample rate mismatch**: the LarsNet models expect 44100 Hz. If input is different, resample before separation (use `juce::ResamplingAudioSource` or `juce::Interpolators`), then adjust onset times accordingly.
- [ ] **Very short files** (< 1 second): ensure STFT padding doesn't crash.
- [ ] **Very long files** (> 10 minutes): monitor memory. Torch tensors for a 10-min stereo 44.1k file ≈ 200 MB × 5 stems.
- [ ] **Empty stems**: if a stem is silent (no onsets detected), still include an empty MIDI track so the file structure is consistent.

### MIDI edge cases

- [ ] **Zero-length file**: don't write a MIDI file if no onsets detected; show a message instead.
- [ ] **BPM = 0 or nonsensical**: validate input, clamp to 20–300.
- [ ] **Overlapping notes on same pitch**: shouldn't happen with minimum interval gate, but guard against it.

### Code quality (apply during all phases)

- No raw `new` — use `std::unique_ptr` / `std::make_unique`.
- No `#include "Utils.cpp"` (LARS does this in `PluginEditor.cpp` — fix it, use proper header/impl).
- Remove all commented-out code blocks from LARS.
- Consistent naming: `camelCase` for methods, `PascalCase` for classes, `UPPER_SNAKE` for constants.

---

## 11. Phase 8 — Licensing & Trial System

> **Agent tier: Sonnet 4.6 for the full implementation. Opus 4.6 ONLY for the cryptographic signing / HMAC anti-tamper logic (a single focused task, ~1–2 turns).**
>
> **Budget note:** The licensing system is "keep honest people honest" — not DRM. Sonnet can write the `LicenseManager`, server API, and machine fingerprinting. Only escalate to Opus for the state-file signing where getting the crypto wrong would mean a trivially bypassable trial. That one Opus task should be tightly scoped: "Given this state struct, produce a signed blob using HMAC-SHA256 and a hardcoded key, and a verify function."

### Business rules

| Rule | Detail |
|------|--------|
| Trial period | 14 calendar days from first launch |
| Trial export cap | 5 WAV exports + 5 MIDI exports (independent counters) |
| Trial functionality | Separation and playback are **unlimited**. Only exporting (save/drag-out) is gated. |
| Activation | User enters a license key string → app calls activation server → server validates + records machine ID → returns success/fail |
| Deactivation | User can deactivate from one machine to move to another (menu item or website self-service) |
| Offline grace | After initial online activation, allow 30 days offline before requiring a re-check |

### LicenseManager (client side)

```cpp
enum class LicenseState { Trial, TrialExpired, Licensed, LicenseCheckNeeded };

class LicenseManager {
public:
    LicenseManager();
    LicenseState getState() const;

    int trialDaysRemaining() const;
    int wavExportsRemaining() const;  // 5 during trial, unlimited when licensed
    int midiExportsRemaining() const; // 5 during trial, unlimited when licensed

    bool canExportWav();    // checks + decrements; returns false if limit hit
    bool canExportMidi();   // checks + decrements; returns false if limit hit

    // Activation
    bool activate(const juce::String& licenseKey);  // calls server, stores result
    void deactivate();

private:
    void loadState();       // from encrypted local file
    void saveState();
    juce::String machineId() const;  // hardware fingerprint

    // Persisted state
    juce::Time firstLaunchDate;
    int wavExportCount  = 0;
    int midiExportCount = 0;
    juce::String storedLicenseKey;
    juce::Time lastServerCheck;
};
```

### Local persistence

- Store trial start date, export counts, and license state in a file under `juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory) / "ISODrums"`.
- **Encrypt / sign** the file with a hardcoded app secret so casual hex-editing doesn't bypass the trial. This is not DRM — it's just "keep honest people honest."
- Use `juce::RSAKey` or a simple HMAC to sign the state blob.

### Machine fingerprint

- Combine MAC address + hostname + OS serial (via `juce::SystemStats`) → SHA-256 → truncate to 16 hex chars.
- Send as `machine_id` during activation so one key can't be shared across unlimited machines.
- Default: allow 2 machines per license.

### Activation server (backend)

A minimal REST API — can be a simple serverless function (Cloudflare Workers, Vercel Edge, or AWS Lambda):

| Endpoint | Method | Payload | Response |
|----------|--------|---------|----------|
| `/api/activate` | POST | `{ licenseKey, machineId }` | `{ success, expiresAt, message }` |
| `/api/deactivate` | POST | `{ licenseKey, machineId }` | `{ success }` |
| `/api/check` | POST | `{ licenseKey, machineId }` | `{ valid, expiresAt }` |

**Database:** a single `licenses` table:

| Column | Type |
|--------|------|
| `license_key` | string (PK) |
| `email` | string |
| `max_machines` | int (default 2) |
| `activated_machines` | JSON array of machine IDs |
| `created_at` | timestamp |
| `order_id` | string (from payment provider) |

### Key generation

- Pre-generate keys as `ISO-XXXX-XXXX-XXXX-XXXX` (alphanumeric, check-digit for typo detection).
- Keys are created when a purchase completes (webhook from payment provider → insert row → email key).

---

## 12. Phase 9 — Website, Store & Distribution

> **Agent tier: Sonnet 4.6** (website) + **Auto** (boilerplate) + manual setup for payment provider. **Never Opus for web work.**

### Website

A single-page marketing + purchase site. Can be static (Next.js/Astro on Vercel, or plain HTML on Cloudflare Pages).

**Sections:**
1. Hero — product name, one-liner ("Separate your drum stems. Export to MIDI."), demo video/GIF.
2. Features — 5 stems, MIDI export, standalone + AU/VST3, drag-to-DAW.
3. Pricing — single price, one-time purchase (no subscription). Show trial details.
4. Download — macOS arm64 `.dmg` or `.zip`. (Windows / Intel Mac as future links, greyed out.)
5. FAQ — system requirements, license terms, supported formats.

### Payment + email flow

```
User clicks "Buy Now"
  → Redirected to payment provider (Gumroad / LemonSqueezy / Stripe Checkout)
  → Payment completes
  → Webhook fires to /api/purchase
  → Server:
      1. Generates license key
      2. Stores in DB
      3. Sends license key email (via Resend, Postmark, or SES)
      4. Sends separate receipt email
  → User enters key in app → activation
```

**Recommended payment provider:** [LemonSqueezy](https://www.lemonsqueezy.com/) or [Gumroad](https://gumroad.com/) — both handle:
- Payment processing (Stripe under the hood)
- Tax / VAT compliance
- Receipt generation (they send the receipt; you send the license key separately)
- Webhook on purchase

This means you only need to build the license-key-generation webhook + the activation API, not a full e-commerce backend.

### Distribution

- Host the `.zip` / `.dmg` on the website (or a CDN / S3 bucket linked from the download button).
- The download is **free** (trial). Purchasing unlocks via key.
- No installer needed for V1 — a zip with an `INSTALL.txt` is fine. `.dmg` with drag-to-Applications is a polish step.

---

## 13. Web App Alternative (Exploration)

> **This section is for reference / future planning. The native app is the V1 path.**

### Could ISO Drums be a web app?

**Advantages:**
- No install, no signing, no LibTorch bundling headaches.
- Cross-platform for free (any browser).
- Licensing is trivial (just gate the feature behind login).
- Payment → account creation → immediate access, no key entry.

**Challenges:**
- **TorchScript in the browser:** You'd need to convert the LarsNet models to ONNX and run them via ONNX Runtime Web (WASM) or TensorFlow.js. This is a real porting effort — not trivial but doable.
- **Performance:** WASM inference on 5 U-Net models for a multi-minute stereo file will be **significantly slower** than native C++ LibTorch. Probably 3–10x slower depending on the browser and hardware.
- **Large model payloads:** ~40 MB per model × 5 = ~200 MB to download on first use. Would need lazy loading or a server-side inference API.
- **No DAW integration:** No AU/VST3. No drag-to-DAW (browser drag-and-drop to desktop is limited). Standalone-only.
- **Audio API:** Web Audio API handles playback and basic DSP fine. File I/O works via `<input type="file">` and download links.

### Hybrid: server-side inference + web UI

A middle path: the web app uploads the audio file to a backend that runs LibTorch natively, returns the stems + MIDI, and the user downloads them.

**Pros:** Fast inference (server GPU), tiny client, works on any device.
**Cons:** Server costs scale with usage, latency for upload/download, privacy concerns (users uploading unreleased music).

### Recommendation

**Ship native first** (Phases 0–9). The AU/VST3 + drag-to-DAW experience is a core differentiator that a web app can't replicate. A web app could be a V2 marketing funnel ("try it in your browser, buy the full plugin for DAW integration").

If a web app is pursued later, the onset detection and MIDI export code (Phase 2–3) can be reused as-is in a server-side C++ worker, and only the model serving needs a new deployment path.

---

## 14. GM Drum Map Reference

These are the default note assignments. Expose them in the UI as editable.

| Stem | Default GM Note | GM Name | Number |
|------|----------------|---------|--------|
| Kick | Bass Drum 1 | C1 | 36 |
| Snare | Acoustic Snare | D1 | 38 |
| Toms | Low-Mid Tom | B1 | 47 |
| Hi-Hat | Closed Hi-Hat | F#1 | 42 |
| Cymbals | Crash Cymbal 1 | C#2 | 49 |

For V2 multi-note mapping (toms → 3 notes, hi-hat → open/closed, etc.), see Phase 2 notes.

---

## 15. Key Decisions Log

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Onset detection approach | Classical DSP (envelope + peak pick) | Stems are already separated; no need for another ML model. Simpler, faster, no extra weights. |
| MIDI file format | SMF Type 1, channel 10 | Standard drum MIDI. One track per stem for easy DAW editing. |
| LibTorch version | 2.5.1 CPU arm64 | Matches existing LARS build. CPU is fine for offline batch processing. |
| JUCE version | 8.0.4 | Required for macOS 15 (Sequoia) compatibility. 7.x fails to build `juceaide`. |
| Standalone vs plugin-only | Both (AU + VST3 + Standalone) | Maximum flexibility. Standalone is simplest for users who just want to convert files. No Kontakt or host dependency. |
| IS_SYNTH | FALSE | LARS set this to TRUE incorrectly. ISO Drums is an effect/utility. |
| Toms / Hi-hat / Cymbals multi-note | V1: single note each | Ship fast. Multi-note classification is a V2 feature. |
| BPM detection | V1: manual entry | Auto-detection is a V2 feature (requires tempo estimation algorithm). |
| Native vs web app | Native first | DAW integration (AU/VST3) + drag-to-DAW are core differentiators a web app cannot match. Web app is a potential V2 marketing funnel. |
| Licensing model | One-time purchase, 14-day trial, 5-export cap | Simple, fair, no subscription fatigue. Trial is fully functional so users can evaluate quality. |
| Payment provider | LemonSqueezy or Gumroad | Handles payment, tax, receipts. We only build the license-key webhook + activation API. |
| GUI priority | Function first, skin later | Get separation + MIDI export + licensing working. Polish the UI in a later pass. |
| Drag-out to DAW | WAV and MIDI via `performExternalDragDropOfFiles` | Standard OS drag; works with Logic, Ableton, Pro Tools, Reaper, etc. Each drag counts against trial export limit. |

---

## 16. Known LARS Issues to Fix During Port

These are bugs or code smells in the LARS codebase that should be fixed in ISO Drums, not carried over.

| Issue | LARS location | Fix |
|-------|--------------|-----|
| `#include "Utils.cpp"` in a `.cpp` file | `PluginEditor.cpp:12` | Proper header/impl split. `Utils.h` declares, `Utils.cpp` defines. Add to CMake `target_sources`. |
| Models re-loaded after every separation | `PluginEditor.cpp:1576–1631` | Load once in constructor. Use `torch::NoGradGuard` / `c10::InferenceMode` during forward pass to prevent graph accumulation. |
| Raw `new` for AudioThumbnail / Cache | `PluginEditor.cpp:56–72` | Use `std::unique_ptr`. |
| Hardcoded 44100 sample rate in WAV writer | `PluginEditor.cpp:1855` | Use actual input file sample rate. |
| `MessageManager::runDispatchLoopUntil` for progress | `PluginEditor.cpp:631,703,720` | Use proper worker thread + async callback (Phase 5). |
| Hardcoded Windows paths | `PluginEditor.cpp:79–82` | Remove entirely. |
| `IS_SYNTH TRUE` | `CMakeLists.txt:50` | Set to `FALSE`. |
| `juce_box2d`, `juce_video`, `juce_osc`, etc. linked | `PluginProcessor.h:26–35` | Remove unused JUCE module includes. Only link what's needed. |
| Progress thread only increments a counter | `PluginEditor.h:257–427` | Replace with real progress reporting from worker thread. |
| `ClickableArea` has `-Wextra-semi`, `-Winconsistent-missing-override` warnings | `ClickableArea.h:14,17` | Fix override specifiers and remove extra semicolons. |

---

## Build & Run Cheat Sheet

```bash
# One-time setup
cd ISO-Drums
bash setup.sh   # downloads LibTorch + JUCE into third_party/

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build --config Release -j 8

# Artefacts
open build/ISODrums_artefacts/Release/Standalone/ISO\ Drums.app
ls ~/Library/Audio/Plug-Ins/Components/ISO\ Drums.component
ls ~/Library/Audio/Plug-Ins/VST3/ISO\ Drums.vst3

# Package
# (zip script TBD — same pattern as LARS build)
```

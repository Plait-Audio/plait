# ISO Drums — Build Plan

**ISO Drums** is a standalone macOS (arm64) application and audio plug-in (AU / VST3) that separates a stereo drum mix into five stems (kick, snare, toms, hi-hat, cymbals) and exports the detected hits as a Standard MIDI File (SMF). It combines the neural drum demixing engine from [LARS](https://github.com/EdoardoMor/LARS) with a new onset-detection + MIDI-export pipeline.

> **This document is the authoritative spec.** An agent should be able to build ISO Drums end-to-end from this file. Every section marks the recommended Cursor agent tier.

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
11. [GM Drum Map Reference](#11-gm-drum-map-reference)
12. [Key Decisions Log](#12-key-decisions-log)
13. [Known LARS Issues to Fix During Port](#13-known-lars-issues-to-fix-during-port)

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

---

## 2. Repository Layout

> **Agent tier: Auto**

```
ISO-Drums/
├── _ISO-Drums-Plan.md          ← this file
├── CMakeLists.txt               ← top-level CMake
├── scripts/
│   └── bundle_libtorch_macos.sh ← post-build LibTorch bundler (from LARS build)
├── src/
│   ├── Main.cpp                 ← (if Standalone-only) or PluginProcessor / PluginEditor
│   ├── ISOProcessor.h / .cpp    ← AudioProcessor (host integration, playback)
│   ├── ISOEditor.h / .cpp       ← AudioProcessorEditor (UI)
│   ├── SeparationEngine.h / .cpp← wraps TorchScript model loading + STFT/ISTFT
│   ├── OnsetDetector.h / .cpp   ← per-stem onset detection
│   ├── MidiExporter.h / .cpp    ← builds MidiFile from detected hits
│   ├── DrumHit.h                ← struct { double timeSec; float velocity; int midiNote; }
│   ├── DrumMap.h                ← GM note constants + user-configurable mapping
│   ├── Utils.h / .cpp           ← STFT helpers (cleaned-up port from LARS)
│   └── ClickableArea.h          ← waveform drag-to-export component (port from LARS)
├── Resources/
│   ├── my_scripted_module_kick.pt
│   ├── my_scripted_module_snare.pt
│   ├── my_scripted_module_toms.pt
│   ├── my_scripted_module_hihat.pt
│   ├── my_scripted_module_cymbals.pt
│   ├── my_scripted_module.pt
│   └── (UI images — ported / new branding)
├── third_party/
│   ├── libtorch/                ← CPU arm64 (downloaded, .gitignored)
│   └── JUCE/                   ← v8.0.4 (cloned, .gitignored)
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

> **Agent tier: Opus 4.6** (core algorithmic work — must be robust and well-tuned)

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

> **Agent tier: Sonnet 4.6** (UI layout) + **Auto** (boilerplate wiring)

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

### Drag and drop

- Drag audio files onto the window to load (same as LARS).
- Drag individual stem waveforms out to export WAV (same as LARS `ClickableArea`).

### Branding

- Replace LARS images with ISO Drums branding. For V1, text-based header is fine; custom graphics can come later.
- Background color scheme: dark gray (#1a1a2e) with accent color (#e94560) for waveforms and active states.

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

> **Agent tier: Auto** (copy from LARS build)

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

> **Agent tier: Opus 4.6** (nuanced correctness) + **Auto** (mechanical fixes)

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

## 11. GM Drum Map Reference

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

## 12. Key Decisions Log

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Onset detection approach | Classical DSP (envelope + peak pick) | Stems are already separated; no need for another ML model. Simpler, faster, no extra weights. |
| MIDI file format | SMF Type 1, channel 10 | Standard drum MIDI. One track per stem for easy DAW editing. |
| LibTorch version | 2.5.1 CPU arm64 | Matches existing LARS build. CPU is fine for offline batch processing. |
| JUCE version | 8.0.4 | Required for macOS 15 (Sequoia) compatibility. 7.x fails to build `juceaide`. |
| Standalone vs plugin-only | Both (AU + VST3 + Standalone) | Maximum flexibility. Standalone is simplest for users who just want to convert files. |
| IS_SYNTH | FALSE | LARS set this to TRUE incorrectly. ISO Drums is an effect/utility. |
| Toms / Hi-hat / Cymbals multi-note | V1: single note each | Ship fast. Multi-note classification is a V2 feature. |
| BPM detection | V1: manual entry | Auto-detection is a V2 feature (requires tempo estimation algorithm). |

---

## 13. Known LARS Issues to Fix During Port

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

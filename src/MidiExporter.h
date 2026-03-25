#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

#include "DrumHit.h"

struct MidiExportSettings
{
    double bpm          = 120.0;
    int    ppq          = 480;    // pulses per quarter note
    bool   quantize     = false;
    int    quantizeGrid = 16;     // 1/N note grid (4, 8, 16, 32)
    bool   separateTracks = true; // one track per stem vs. all on one track
};

class MidiExporter
{
public:
    // Build a MidiFile in memory from a flat list of hits (multiple stems combined).
    // stemNotes: if provided and separateTracks is true, guarantees one track per
    // note even if no hits exist for that stem (produces an empty track).
    juce::MidiFile buildMidiFile(const std::vector<DrumHit>& allHits,
                                 const MidiExportSettings& settings = {},
                                 const std::vector<int>& stemNotes = {}) const;

    // Build and write to disk. Returns true on success.
    bool exportToFile(const std::vector<DrumHit>& allHits,
                      const juce::File& outputFile,
                      const MidiExportSettings& settings = {},
                      const std::vector<int>& stemNotes = {}) const;

private:
    // Convert a time in seconds to MIDI ticks.
    static double secondsToTicks(double timeSec, double bpm, int ppq);

    // Snap a tick value to the nearest quantise grid.
    static double quantiseTick(double tick, double bpm, int ppq, int grid);

    // Scale velocity 0.0–1.0 to MIDI 1–127.
    static int velocityToMidi(float velocity);
};

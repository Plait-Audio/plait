#include "MidiExporter.h"

#include <algorithm>
#include <cmath>
#include <map>

// --------------------------------------------------------------------------
// Private helpers
// --------------------------------------------------------------------------

double MidiExporter::secondsToTicks(double timeSec, double bpm, int ppq)
{
    return (timeSec / 60.0) * bpm * static_cast<double>(ppq);
}

double MidiExporter::quantiseTick(double tick, double bpm, int ppq, int grid)
{
    // gridTicks = one quarter note divided by (grid/4)
    // e.g. grid=16 → gridTicks = ppq * 4 / 16 = ppq/4
    const double gridTicks = static_cast<double>(ppq) * 4.0 / static_cast<double>(grid);
    return std::round(tick / gridTicks) * gridTicks;
}

int MidiExporter::velocityToMidi(float velocity)
{
    // Map [0,1] → [1,127], never zero (a zero velocity is a note-off in MIDI)
    return std::clamp(static_cast<int>(std::round(velocity * 126.0f)) + 1, 1, 127);
}

// --------------------------------------------------------------------------
// Build
// --------------------------------------------------------------------------

juce::MidiFile MidiExporter::buildMidiFile(const std::vector<DrumHit>& allHits,
                                            const MidiExportSettings& settings,
                                            const std::vector<int>& stemNotes) const
{
    // Clamp BPM to a sane range
    const double bpm = std::clamp(settings.bpm, 20.0, 300.0);
    const int    ppq = std::max(1, settings.ppq);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);

    // ---- Track 0: tempo map ----
    {
        juce::MidiMessageSequence tempoTrack;
        const int microsecondsPerBeat = static_cast<int>(std::round(60'000'000.0 / bpm));
        tempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(microsecondsPerBeat), 0.0);
        midiFile.addTrack(tempoTrack);
    }

    // ---- Note duration: one 32nd note ----
    const double noteDurationTicks = static_cast<double>(ppq) / 8.0;

    // GM percussion channel is channel 10 (1-based), which is index 9 (0-based).
    // JUCE MidiMessage uses 1-based channels.
    constexpr int kPercussionChannel = 10;

    if (settings.separateTracks)
    {
        // Group hits by MIDI note so each stem gets its own track.
        std::map<int, std::vector<const DrumHit*>> byNote;

        // Seed the map with stem notes to guarantee empty tracks for silent stems
        for (int note : stemNotes)
            byNote[note]; // inserts empty vector if not present

        for (const auto& hit : allHits)
            byNote[hit.midiNote].push_back(&hit);

        for (auto& [note, noteHits] : byNote)
        {
            juce::MidiMessageSequence track;

            // Sort by time (should already be sorted, but be safe)
            std::sort(noteHits.begin(), noteHits.end(),
                      [](const DrumHit* a, const DrumHit* b)
                      { return a->timeSec < b->timeSec; });

            for (const auto* hit : noteHits)
            {
                double tick = secondsToTicks(hit->timeSec, bpm, ppq);
                if (settings.quantize)
                    tick = quantiseTick(tick, bpm, ppq, settings.quantizeGrid);

                const int vel = velocityToMidi(hit->velocity);

                track.addEvent(juce::MidiMessage::noteOn (kPercussionChannel, note, (juce::uint8)vel),
                               tick);
                track.addEvent(juce::MidiMessage::noteOff(kPercussionChannel, note, (juce::uint8)0),
                               tick + noteDurationTicks);
            }

            track.updateMatchedPairs();
            midiFile.addTrack(track);
        }
    }
    else
    {
        // All hits on a single track
        juce::MidiMessageSequence track;

        for (const auto& hit : allHits)
        {
            double tick = secondsToTicks(hit.timeSec, bpm, ppq);
            if (settings.quantize)
                tick = quantiseTick(tick, bpm, ppq, settings.quantizeGrid);

            const int vel = velocityToMidi(hit.velocity);

            track.addEvent(juce::MidiMessage::noteOn (kPercussionChannel, hit.midiNote, (juce::uint8)vel),
                           tick);
            track.addEvent(juce::MidiMessage::noteOff(kPercussionChannel, hit.midiNote, (juce::uint8)0),
                           tick + noteDurationTicks);
        }

        track.updateMatchedPairs();
        midiFile.addTrack(track);
    }

    return midiFile;
}

// --------------------------------------------------------------------------
// Export
// --------------------------------------------------------------------------

bool MidiExporter::exportToFile(const std::vector<DrumHit>& allHits,
                                 const juce::File& outputFile,
                                 const MidiExportSettings& settings,
                                 const std::vector<int>& stemNotes) const
{
    if (allHits.empty())
        return false;

    juce::MidiFile midiFile = buildMidiFile(allHits, settings, stemNotes);

    outputFile.deleteFile();
    juce::FileOutputStream stream(outputFile);

    if (!stream.openedOk())
        return false;

    return midiFile.writeTo(stream);
}

#pragma once

namespace DrumMap
{
    // GM percussion note numbers (channel 10)
    constexpr int KICK_NOTE        = 36;  // C1  — Bass Drum 1
    constexpr int SNARE_NOTE       = 38;  // D1  — Acoustic Snare
    constexpr int SIDE_STICK_NOTE  = 37;  // C#1 — Side Stick (aspirational for ghost notes)
    constexpr int TOM_LOW_NOTE     = 45;  // A1  — Low Tom
    constexpr int TOM_MID_NOTE     = 47;  // B1  — Low-Mid Tom
    constexpr int TOM_HIGH_NOTE    = 50;  // D2  — High Tom
    constexpr int HIHAT_CLOSED_NOTE = 42; // F#1 — Closed Hi-Hat
    constexpr int HIHAT_OPEN_NOTE  = 46;  // A#1 — Open Hi-Hat
    constexpr int HIHAT_PEDAL_NOTE = 44;  // G#1 — Pedal Hi-Hat
    constexpr int CRASH_NOTE       = 49;  // C#2 — Crash Cymbal 1
    constexpr int RIDE_NOTE        = 51;  // D#2 — Ride Cymbal 1

    // V1 defaults: one note per stem
    constexpr int DEFAULT_KICK    = KICK_NOTE;
    constexpr int DEFAULT_SNARE   = SNARE_NOTE;
    constexpr int DEFAULT_TOMS    = TOM_MID_NOTE;
    constexpr int DEFAULT_HIHAT   = HIHAT_CLOSED_NOTE;
    constexpr int DEFAULT_CYMBALS = CRASH_NOTE;
}

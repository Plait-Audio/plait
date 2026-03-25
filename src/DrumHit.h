#pragma once

struct DrumHit
{
    double timeSec;    // onset time in seconds from file start
    float  velocity;   // 0.0–1.0 (maps to MIDI 1–127)
    int    midiNote;   // GM drum note number
};

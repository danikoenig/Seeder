#pragma once
#include "Styles.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace seeder
{
constexpr int kStepsPerBar  = 16;
constexpr int kMaxBars      = 16;
constexpr int kMaxSteps     = kMaxBars * kStepsPerBar;
constexpr int kMaxEvents    = 4096;

struct NoteEvent
{
    int16_t step = 0;     // 0 .. steps-1 (16th notes)
    int16_t length = 1;   // in steps
    uint8_t note = 60;
    uint8_t velocity = 90;
    uint8_t layer = 0;    // Layer
};

struct Pattern
{
    std::array<NoteEvent, kMaxEvents> events {};
    int count = 0;
    int bars = 8;                  // 4, 8 or 16
    float suggestedSwing = 0.0f;
    int scale = 0;                 // ScaleId used for this pattern
    int progression[4] {};         // scale degrees of the first four chords

    int steps() const { return bars * kStepsPerBar; }
    void clear() { count = 0; }
    bool add (int step, int len, int note, int vel, Layer layer);
};

// Choices that shape the pattern besides mood and style. Packed into one int for the link.
struct Options
{
    int bars = 1;        // 0 = 4 bars, 1 = 8 bars, 2 = 16 bars
    int voicing = 0;     // 0 auto, 1 close, 2 open, 3 drop 2, 4 spread
    int chordRate = 0;   // 0 one chord per bar, 1 per half bar, 2 per two bars
    int arpPattern = 0;  // 0 auto, 1 up, 2 down, 3 up/down, 4 random, 5 converge
    int arpRate = 0;     // 0 auto, 1 eighths, 2 sixteenths
    int bassMove = 0;    // 0 auto, 1 root, 2 octaves, 3 walking
    int fill = 1;        // 0 off, 1 light, 2 strong (turnaround in the last bar)

    int numBars() const { return bars == 0 ? 4 : (bars == 2 ? 16 : 8); }

    int pack() const
    {
        return (bars & 3) | ((voicing & 7) << 2) | ((chordRate & 3) << 5) | ((arpPattern & 7) << 7)
             | ((arpRate & 3) << 10) | ((bassMove & 3) << 12) | ((fill & 3) << 14);
    }
    static Options unpack (int v)
    {
        Options o;
        o.bars = std::min (2, v & 3);
        o.voicing = std::min (4, (v >> 2) & 7);
        o.chordRate = std::min (2, (v >> 5) & 3);
        o.arpPattern = std::min (5, (v >> 7) & 7);
        o.arpRate = std::min (2, (v >> 10) & 3);
        o.bassMove = (v >> 12) & 3;
        o.fill = std::min (2, (v >> 14) & 3);
        return o;
    }
};

struct GenParams
{
    uint64_t seed       = 1;
    int      rootPc     = 0;       // 0..11, from the trigger note
    Mood     mood       = Mood::Melancholic;
    Style    style      = Style::House;
    float    complexity = 0.5f;    // 0..1  extensions, passing tones, inversions
    float    density    = 0.5f;    // 0..1  note density of melody / arp
    Options  options;
};

// Builds a pattern (chords, stabs, pad, bass, melody, counter melody, arp). Pure function of GenParams.
void generate (const GenParams& p, Pattern& out);

// Mutates a copy of 'base' for loop number 'loopIndex'. variation 0 = untouched.
void applyVariation (const Pattern& base, Pattern& out, const GenParams& p, float variation, int loopIndex);

// Seed derived from the trigger note so every key has its own, reproducible pattern.
uint64_t seedFromNote (int midiNote, int userSeed, Mood mood, Style style);
} // namespace seeder

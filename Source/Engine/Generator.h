#pragma once
#include "Styles.h"
#include <array>
#include <cstdint>

namespace seeder
{
constexpr int kBars         = 4;
constexpr int kStepsPerBar  = 16;
constexpr int kPatternSteps = kBars * kStepsPerBar;
constexpr int kMaxEvents    = 1536;

struct NoteEvent
{
    int16_t step = 0;     // 0 .. kPatternSteps-1 (16th notes)
    int16_t length = 1;   // in steps
    uint8_t note = 60;
    uint8_t velocity = 90;
    uint8_t layer = 0;    // Layer
};

struct Pattern
{
    std::array<NoteEvent, kMaxEvents> events {};
    int count = 0;
    float suggestedSwing = 0.0f;
    int scale = 0;                 // ScaleId used for this pattern
    int progression[kBars] {};     // scale degrees (0 = first) of the four chords

    void clear() { count = 0; }
    bool add (int step, int len, int note, int vel, Layer layer);
};

struct GenParams
{
    uint64_t seed       = 1;
    int      rootPc     = 0;       // 0..11, from the trigger note
    Mood     mood       = Mood::Melancholic;
    Style    style      = Style::House;
    float    complexity = 0.5f;    // 0..1  extensions, passing tones, inversions
    float    density    = 0.5f;    // 0..1  note density of melody / arp
};

// Builds a 4-bar pattern (chords, melody, arp, bass). Pure function of GenParams.
void generate (const GenParams& p, Pattern& out);

// Mutates a copy of 'base' for loop number 'loopIndex'. variation 0 = untouched.
void applyVariation (const Pattern& base, Pattern& out, const GenParams& p, float variation, int loopIndex);

// Seed derived from the trigger note so every key has its own, reproducible pattern.
uint64_t seedFromNote (int midiNote, int userSeed, Mood mood, Style style);
} // namespace seeder

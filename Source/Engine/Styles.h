#pragma once
#include "Theory.h"

namespace seeder
{
enum class Mood  { Happy = 0, Melancholic, Sad, Dark, Soulful, Jazzy, Count };
enum class Style { House = 0, DeepDub, Techno, ClassicHouse, Garage, Disco, Ambient, Count };

constexpr int kNumMoods  = (int) Mood::Count;
constexpr int kNumStyles = (int) Style::Count;

inline const char* moodName (int i)
{
    static const char* n[] { "Happy", "Melancholic", "Sad", "Dark", "Soulful", "Jazzy" };
    return n[i];
}
inline const char* styleName (int i)
{
    static const char* n[] { "House", "Deep / Dub House", "Techno", "Classic House", "Garage", "Disco", "Ambient" };
    return n[i];
}

struct MoodProfile
{
    ScaleId scales[2];
    int     progressions[4][4];   // scale degrees (0 = I/i), one chord per bar
    float   extension;            // base probability of 7th/9th chords
    float   densityMul;           // scales rhythmic activity
    int     melodyShift;          // semitone shift of melody register
};

struct StyleProfile
{
    const char* chordTemplates[3]; // 16 steps: '.' rest, 'x' hit, 'X' accent
    const char* bassTemplates[3];
    int   chordLen;                // steps, 0 = hold until next hit
    float melodyDensity;
    int   arpRate;                 // steps per arp note
    float arpActivity;             // probability the arp layer plays in a bar
    float swing;                   // suggested swing (0..0.5), applied by the host plugin
    float bassOctave;              // probability of octave jumps in bass
    float extensionMul;
};

inline const MoodProfile& moodProfile (Mood m)
{
    static const MoodProfile t[kNumMoods] = {
        // Happy
        { { ScaleId::Major, ScaleId::Mixolydian },
          { { 0, 4, 5, 3 }, { 0, 3, 4, 3 }, { 0, 5, 3, 4 }, { 0, 4, 3, 4 } }, 0.10f, 1.10f, 0 },
        // Melancholic
        { { ScaleId::Dorian, ScaleId::Aeolian },
          { { 0, 6, 5, 6 }, { 0, 3, 6, 2 }, { 0, 5, 2, 6 }, { 0, 2, 6, 3 } }, 0.35f, 0.90f, 0 },
        // Sad
        { { ScaleId::Aeolian, ScaleId::Aeolian },
          { { 0, 5, 2, 6 }, { 0, 3, 0, 4 }, { 0, 6, 5, 4 }, { 0, 5, 3, 6 } }, 0.20f, 0.65f, -5 },
        // Dark
        { { ScaleId::Phrygian, ScaleId::HarmonicMinor },
          { { 0, 1, 0, 6 }, { 0, 0, 1, 0 }, { 0, 5, 6, 0 }, { 0, 1, 6, 0 } }, 0.05f, 0.90f, -7 },
        // Soulful
        { { ScaleId::Dorian, ScaleId::Major },
          { { 0, 5, 1, 4 }, { 1, 4, 0, 5 }, { 0, 3, 1, 4 }, { 2, 5, 1, 4 } }, 0.65f, 0.95f, 0 },
        // Jazzy
        { { ScaleId::Major, ScaleId::Dorian },
          { { 1, 4, 0, 0 }, { 0, 5, 1, 4 }, { 2, 5, 1, 4 }, { 1, 4, 0, 5 } }, 0.85f, 1.00f, 0 },
    };
    return t[(int) m];
}

inline const StyleProfile& styleProfile (Style s)
{
    static const StyleProfile t[kNumStyles] = {
        // House
        { { "..x...x...x...x.", "x..x..x...x..x..", "..X...x...X...x." },
          { "x..x..x...x..x..", "..x...x...x...x.", "x.x...x.x.x...x." },
          2, 0.45f, 2, 0.60f, 0.00f, 0.25f, 1.0f },
        // Deep / Dub House
        { { "x.......x..x....", "x.....x...x.....", "..x......x......" },
          { "x..x.....x.x....", "x.....x.x.....x.", "x.......x.x....." },
          4, 0.25f, 2, 0.40f, 0.08f, 0.10f, 1.2f },
        // Techno
        { { "x.......x.......", "..x...x...x...x.", "x...x...x...x..." },
          { "..x...x...x...x.", "xxxxxxxxxxxxxxxx", "x.xxx.xxx.xxx.xx" },
          1, 0.30f, 1, 0.80f, 0.00f, 0.15f, 0.2f },
        // Classic House
        { { "x..x..x...x.x...", "x..x..x...x..x..", "x.x..x..x.x..x.." },
          { "x..x..x...x..x..", "x.x.x.x.x.x.x.x.", "x..x.x..x..x.x.." },
          2, 0.55f, 2, 0.35f, 0.02f, 0.30f, 1.2f },
        // Garage
        { { "x.....x...x.....", "..x...x....x..x.", "x..x....x..x...." },
          { "x.....x..x....x.", "x..x....x...x...", "x.....x...x..x.." },
          2, 0.40f, 2, 0.50f, 0.22f, 0.20f, 1.0f },
        // Disco
        { { "..x...x...x...x.", "x.x.x.x.x.x.x.x.", "..x..x..x..x..x." },
          { "x.x.x.x.x.x.x.x.", "x.xxx.x.x.xxx.x.", "x.x.x.xxx.x.x.x." },
          1, 0.50f, 1, 0.50f, 0.00f, 0.50f, 1.0f },
        // Ambient
        { { "X...............", "X.......x.......", "X.......X......." },
          { "x...............", "x.......x.......", "x..............." },
          0, 0.15f, 2, 0.35f, 0.00f, 0.00f, 1.5f },
    };
    return t[(int) s];
}
} // namespace seeder

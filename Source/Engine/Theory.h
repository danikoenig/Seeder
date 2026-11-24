#pragma once
#include <array>
#include <cstddef>
#include <cstdint>


namespace seeder
{
// Deterministic RNG (SplitMix64). Same seed -> same pattern on every machine.
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed = 1) : s (seed) {}

    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    float uni() { return (float) (next() >> 40) / 16777216.0f; }          // 0..1
    int   range (int lo, int hi) { return lo + (int) (next() % (uint64_t) (hi - lo + 1)); } // inclusive
    bool  chance (float p) { return uni() < p; }
};

inline uint64_t hashCombine (uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

// 7-note scales as semitone offsets from the root.
enum class ScaleId { Major, Mixolydian, Dorian, Aeolian, Phrygian, HarmonicMinor, Count };

inline const std::array<int, 7>& scaleIntervals (ScaleId id)
{
    static const std::array<std::array<int, 7>, (std::size_t) ScaleId::Count> t {{
        { 0, 2, 4, 5, 7, 9, 11 },  // Major
        { 0, 2, 4, 5, 7, 9, 10 },  // Mixolydian
        { 0, 2, 3, 5, 7, 9, 10 },  // Dorian
        { 0, 2, 3, 5, 7, 8, 10 },  // Aeolian (natural minor)
        { 0, 1, 3, 5, 7, 8, 10 },  // Phrygian
        { 0, 2, 3, 5, 7, 8, 11 },  // Harmonic minor
    }};
    return t[(std::size_t) id];
}

// Pitch (semitones above root, may exceed one octave or be negative) of scale degree d.
inline int degreeToSemitone (ScaleId id, int degree)
{
    const auto& iv = scaleIntervals (id);
    int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    int idx = degree - oct * 7;
    return oct * 12 + iv[(std::size_t) idx];
}

enum class Layer { Chords = 0, Melody, Arp, Bass, Count };
constexpr int kNumLayers = (int) Layer::Count;
} // namespace seeder

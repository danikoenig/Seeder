// Standalone engine test (no JUCE needed):
//   g++ -std=c++17 -O1 -fsanitize=address,undefined -ISource/Engine Tests/engine_test.cpp Source/Engine/Generator.cpp -o engine_test && ./engine_test
#include "Generator.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

using namespace seeder;

static int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    int combos = 0;
    for (int m = 0; m < kNumMoods; ++m)
    for (int s = 0; s < kNumStyles; ++s)
    for (int note = 24; note < 108; note += 7)
    for (float cx : { 0.0f, 0.5f, 1.0f })
    for (float dn : { 0.0f, 0.5f, 1.0f })
    {
        GenParams p;
        p.options = Options::unpack (std::abs (m * 31 + s * 7 + note + (int) (cx * 4) + (int) (dn * 8)) * 2654435761u >> 3 & 0x3fff);
        p.mood = (Mood) m; p.style = (Style) s; p.rootPc = note % 12;
        p.complexity = cx; p.density = dn;
        p.seed = seedFromNote (note, 7, p.mood, p.style);

        static Pattern a, b, v;
        generate (p, a);
        generate (p, b);

        CHECK (a.count == b.count, "determinism count %d/%d", m, s);
        CHECK (a.count > 0 && a.count < kMaxEvents, "count %d out of range (%d/%d)", a.count, m, s);
        CHECK (std::memcmp (a.events.data(), b.events.data(), sizeof (NoteEvent) * (size_t) a.count) == 0,
               "determinism data %d/%d", m, s);

        int perLayer[kNumLayers] {};
        for (int i = 0; i < a.count; ++i)
        {
            const NoteEvent& e = a.events[(size_t) i];
            CHECK (e.step >= 0 && e.step < a.steps(), "step %d", e.step);
            CHECK (e.step + e.length <= a.steps(), "length overflow step %d len %d", e.step, e.length);
            CHECK (e.note >= 12 && e.note <= 108, "note %d out of musical range (%d/%d)", e.note, m, s);
            CHECK (e.velocity >= 1 && e.velocity <= 127, "velocity %d", e.velocity);
            CHECK (e.layer < kNumLayers, "layer %d", e.layer);
            ++perLayer[e.layer];
        }
        CHECK (perLayer[(int) Layer::Chords] > 0, "no chords (%d/%d)", m, s);
        CHECK (perLayer[(int) Layer::Bass] > 0, "no bass (%d/%d)", m, s);
        CHECK (perLayer[(int) Layer::Stabs] > 0 && perLayer[(int) Layer::Pad] > 0, "no stabs or pad (%d/%d)", m, s);
        CHECK (a.bars == p.options.numBars(), "bars %d", a.bars);

        applyVariation (a, v, p, 0.0f, 3);
        CHECK (v.count == a.count, "variation 0 must not change pattern");
        applyVariation (a, v, p, 1.0f, 3);
        CHECK (v.count <= a.count, "variation must not add events");
        ++combos;
    }

    // every option on its own, on every bar length
    for (int bars = 0; bars < 3; ++bars)
    for (int ch = 0; ch < 3; ++ch)
    for (int vo = 0; vo < 5; ++vo)
    for (int ap = 0; ap < 6; ++ap)
    for (int ar = 0; ar < 3; ++ar)
    for (int bm = 0; bm < 4; ++bm)
    for (int fl = 0; fl < 3; ++fl)
    {
        GenParams p;
        p.options.bars = bars; p.options.chordRate = ch; p.options.voicing = vo;
        p.options.arpPattern = ap; p.options.arpRate = ar; p.options.bassMove = bm; p.options.fill = fl;
        CHECK (Options::unpack (p.options.pack()).pack() == p.options.pack(), "options pack round trip");
        p.mood = (Mood) ((bars * 7 + ch * 3 + vo) % kNumMoods);
        p.style = (Style) ((ap * 5 + ar + bm) % kNumStyles);
        p.complexity = 0.5f + 0.5f * (float) (fl % 2); p.density = 0.7f;
        p.seed = (uint64_t) (1 + bars * 1000 + ch * 100 + vo * 10 + ap);
        static Pattern o; generate (p, o);
        int perLayer[kNumLayers] {};
        for (int i = 0; i < o.count; ++i)
        {
            const NoteEvent& e = o.events[(size_t) i];
            CHECK (e.step >= 0 && e.step + e.length <= o.steps(), "option test: event outside pattern (step %d len %d bars %d)", e.step, e.length, o.bars);
            CHECK (e.note >= 12 && e.note <= 108, "option test: note %d", e.note);
            ++perLayer[e.layer];
        }
        for (int l = 0; l < kNumLayers; ++l)
            if (l != (int) Layer::Arp && l != (int) Layer::Counter && l != (int) Layer::Melody)
                CHECK (perLayer[l] > 0, "option test: layer %d empty", l);
        CHECK (o.count < kMaxEvents, "option test: pattern full");
        ++combos;
    }

    // all rhythm templates have 16 steps
    for (int s = 0; s < kNumStyles; ++s)
    {
        const StyleProfile& sp = styleProfile ((Style) s);
        for (int i = 0; i < 3; ++i)
        {
            CHECK (std::strlen (sp.chordTemplates[i]) == 16, "chord template %d/%d", s, i);
            CHECK (std::strlen (sp.stabTemplates[i]) == 16,  "stab template %d/%d", s, i);
            CHECK (std::strlen (sp.bassTemplates[i]) == 16,  "bass template %d/%d", s, i);
        }
    }

    // different notes must give different patterns (the "keyboard = preset bank" idea)
    GenParams p1, p2;
    p1.seed = seedFromNote (48, 0, p1.mood, p1.style);
    p2.seed = seedFromNote (50, 0, p2.mood, p2.style);
    p1.rootPc = 0; p2.rootPc = 2;
    static Pattern x, y; generate (p1, x); generate (p2, y);
    bool differ = x.count != y.count || std::memcmp (x.events.data(), y.events.data(), sizeof (NoteEvent) * (size_t) x.count) != 0;
    CHECK (differ, "different trigger notes produced identical patterns");

    std::printf ("%d combinations checked, %d failure(s)\n", combos, failures);
    return failures ? 1 : 0;
}

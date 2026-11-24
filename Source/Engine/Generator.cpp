#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace seeder
{
bool Pattern::add (int step, int len, int note, int vel, Layer layer)
{
    if (count >= kMaxEvents || step < 0 || step >= kPatternSteps) return false;
    NoteEvent& e = events[(size_t) count++];
    e.step     = (int16_t) step;
    e.length   = (int16_t) std::max (1, std::min (len, kPatternSteps - step));
    e.note     = (uint8_t) std::max (0, std::min (127, note));
    e.velocity = (uint8_t) std::max (1, std::min (127, vel));
    e.layer    = (uint8_t) layer;
    return true;
}

uint64_t seedFromNote (int midiNote, int userSeed, Mood mood, Style style)
{
    uint64_t h = 0x5EED5EEDULL;
    h = hashCombine (h, (uint64_t) midiNote);
    h = hashCombine (h, (uint64_t) userSeed);
    h = hashCombine (h, (uint64_t) mood);
    h = hashCombine (h, (uint64_t) style);
    return h;
}

namespace
{
struct Chord
{
    int notes[6];
    int count = 0;
    int degree = 0;
};

// Stacks thirds on the scale: 1-3-5 (+7, +9, +13). 'sus' swaps the third for the fourth.
Chord buildChord (ScaleId scale, int degree, int rootMidi, float ext, bool sus, bool invert, Rng& rng)
{
    Chord c;
    c.degree = degree;
    static const int stack[] { 0, 2, 4, 6, 8, 12 };
    int n = 3;
    if (rng.chance (ext))        n = 4;
    if (n == 4 && rng.chance (ext * 0.6f)) n = 5;
    if (n == 5 && rng.chance (ext * 0.35f)) n = 6;

    for (int i = 0; i < n; ++i)
    {
        int d = degree + stack[i];
        if (i == 1 && sus) d = degree + 3;
        c.notes[c.count++] = rootMidi + degreeToSemitone (scale, d);
    }

    // keep voicing around middle C
    auto mean = [&] { float s = 0; for (int i = 0; i < c.count; ++i) s += (float) c.notes[i]; return s / (float) c.count; };
    while (mean() > 67.0f) for (int i = 0; i < c.count; ++i) c.notes[i] -= 12;
    while (mean() < 55.0f) for (int i = 0; i < c.count; ++i) c.notes[i] += 12;

    if (invert && c.count > 2) { c.notes[0] += 12; std::sort (c.notes, c.notes + c.count); }
    return c;
}

bool isChordDegree (int degree, int chordDegree, int n)
{
    static const int stack[] { 0, 2, 4, 6, 8, 12 };
    int d = ((degree - chordDegree) % 7 + 7) % 7;
    for (int i = 0; i < std::min (n, 4); ++i)
        if (stack[i] % 7 == d) return true;
    return false;
}
} // namespace

void generate (const GenParams& p, Pattern& out)
{
    out.clear();
    const MoodProfile&  mood  = moodProfile (p.mood);
    const StyleProfile& style = styleProfile (p.style);
    out.suggestedSwing = style.swing;

    Rng rngMain (hashCombine (p.seed, 99));
    const ScaleId scale = mood.scales[rngMain.range (0, 1)];
    const int* prog = mood.progressions[rngMain.range (0, 3)];
    const int rootMidi = 48 + p.rootPc;
    const float ext = std::min (1.0f, (mood.extension + p.complexity * 0.5f) * style.extensionMul);

    // ---- chords + per-bar chord info -------------------------------------
    Rng rngC (hashCombine (p.seed, 1));
    const char* chordT = style.chordTemplates[rngC.range (0, 2)];
    Chord bars[kBars];

    for (int b = 0; b < kBars; ++b)
    {
        bool sus = rngC.chance (0.05f + p.complexity * 0.12f);
        bool inv = rngC.chance (p.complexity * 0.45f);
        bars[b] = buildChord (scale, prog[b], rootMidi, ext, sus, inv, rngC);

        // hit positions of this bar
        int hits[16], nh = 0;
        for (int s = 0; s < 16; ++s) if (chordT[s] != '.') hits[nh++] = s;
        for (int h = 0; h < nh; ++h)
        {
            int s = hits[h];
            int next = (h + 1 < nh) ? hits[h + 1] : 16;
            int len = style.chordLen == 0 ? (next - s) : std::min (style.chordLen, next - s);
            int vel = (chordT[s] == 'X' ? 92 : 76) + rngC.range (-6, 6);
            for (int k = 0; k < bars[b].count; ++k)
                out.add (b * 16 + s, len, bars[b].notes[k], vel - k * 2, Layer::Chords);
        }
    }

    // ---- bass --------------------------------------------------------------
    Rng rngB (hashCombine (p.seed, 4));
    const char* bassT = style.bassTemplates[rngB.range (0, 2)];
    bool altOct = (p.style == Style::Disco);
    for (int b = 0; b < kBars; ++b)
    {
        int bassRoot = rootMidi - 12 + degreeToSemitone (scale, bars[b].degree);
        while (bassRoot > 50) bassRoot -= 12;
        while (bassRoot < 28) bassRoot += 12;
        int hitIdx = 0;
        for (int s = 0; s < 16; ++s)
        {
            if (bassT[s] == '.') continue;
            int note = bassRoot;
            if (altOct) { if (hitIdx % 2 == 1) note += 12; }
            else if (rngB.chance (style.bassOctave)) note += 12;
            else if (rngB.chance (p.complexity * 0.12f)) note = rootMidi - 12 + degreeToSemitone (scale, bars[b].degree + 4);
            while (note > 55) note -= 12;
            bool rolling = (p.style == Style::Techno && bassT[1] == 'x');
            int len = rolling ? 1 : 2;
            int vel = (s % 4 == 0 ? 100 : 86) + rngB.range (-6, 6);
            out.add (b * 16 + s, len, note, vel, Layer::Bass);
            ++hitIdx;
        }
    }

    // ---- arp ---------------------------------------------------------------
    Rng rngA (hashCombine (p.seed, 3));
    const int arpMode = rngA.range (0, 4); // up, down, updown, random, converge
    const int rate = style.arpRate;
    for (int b = 0; b < kBars; ++b)
    {
        if (! rngA.chance (std::min (1.0f, style.arpActivity + p.density * 0.3f))) continue;
        int tones[12], nt = 0;
        for (int k = 0; k < bars[b].count && nt < 6; ++k) tones[nt++] = bars[b].notes[k] + 12;
        for (int k = 0; k < bars[b].count && nt < 12; ++k) tones[nt++] = bars[b].notes[k] + 24;
        std::sort (tones, tones + nt);
        int idx = 0, dir = 1;
        const float gate = 0.45f + p.density * 0.5f;
        for (int s = 0; s < 16; s += rate)
        {
            int ni = 0;
            switch (arpMode)
            {
                case 0: ni = idx % nt; break;
                case 1: ni = (nt - 1) - (idx % nt); break;
                case 2: { int cyc = std::max (1, 2 * nt - 2); int q = idx % cyc; ni = q < nt ? q : cyc - q; break; }
                case 3: ni = rngA.range (0, nt - 1); break;
                default: { int q = idx % nt; ni = (q % 2 == 0) ? q / 2 : nt - 1 - q / 2; break; }
            }
            ++idx;
            if (! rngA.chance (gate)) continue;
            int vel = (s % 4 == 0 ? 82 : 66) + rngA.range (-8, 8);
            out.add (b * 16 + s, rate, tones[std::max (0, std::min (nt - 1, ni))], vel, Layer::Arp);
        }
        (void) dir;
    }

    // ---- melody ------------------------------------------------------------
    Rng rngM (hashCombine (p.seed, 2));
    const float mDens = std::min (0.95f, style.melodyDensity * mood.densityMul * (0.4f + p.density * 1.2f));
    int deg = 7 + rngM.range (0, 2);   // start about an octave above the root
    int lastStep = -100;
    struct Hit { int step, degree; };
    Hit melodyHits[kBars * 16]; int nm = 0;

    for (int b = 0; b < kBars; ++b)
    {
        for (int s = 0; s < 16; ++s)
        {
            float strength = (s % 4 == 0) ? 0.75f : (s % 2 == 0 ? 0.40f : 0.14f);
            if (b * 16 + s - lastStep < 2) strength *= 0.25f;
            if (! rngM.chance (strength * mDens * 1.6f)) continue;

            static const int stepSet[] { -3, -2, -1, 0, 1, 2, 3 };
            float w[7] { 0.04f + p.complexity * 0.04f, 0.14f, 0.30f, 0.12f, 0.30f, 0.14f, 0.04f + p.complexity * 0.04f };
            if (deg > 12) { w[4] *= 0.3f; w[5] *= 0.3f; w[6] *= 0.3f; }
            if (deg < 5)  { w[0] *= 0.3f; w[1] *= 0.3f; w[2] *= 0.3f; }
            float tot = 0; for (float x : w) tot += x;
            float r = rngM.uni() * tot; int k = 0;
            while (k < 6 && r > w[k]) { r -= w[k]; ++k; }
            deg += stepSet[k];

            if (s % 4 == 0)   // strong beat: snap to chord tone
            {
                for (int off : { 0, 1, -1, 2, -2 })
                    if (isChordDegree (deg + off, bars[b].degree, 4)) { deg += off; break; }
            }
            melodyHits[nm++] = { b * 16 + s, deg };
            lastStep = b * 16 + s;
        }
    }
    if (nm > 0)  // end the phrase on a stable tone
    {
        int target = 7;
        int best = melodyHits[nm - 1].degree;
        for (int off : { 0, 1, -1, 2, -2, 3, -3 })
            if (((best + off) % 7 + 7) % 7 == 0 || ((best + off) % 7 + 7) % 7 == 4) { target = best + off; break; }
        melodyHits[nm - 1].degree = target;
    }
    for (int i = 0; i < nm; ++i)
    {
        int next = (i + 1 < nm) ? melodyHits[i + 1].step : kPatternSteps;
        int len = std::max (1, std::min (next - melodyHits[i].step, p.style == Style::Ambient ? 12 : 4));
        int note = rootMidi + 12 + mood.melodyShift + degreeToSemitone (scale, melodyHits[i].degree - 7);
        while (note > 90) note -= 12;
        while (note < 52) note += 12;
        int vel = (melodyHits[i].step % 4 == 0 ? 96 : 80) + rngM.range (-8, 8);
        out.add (melodyHits[i].step, len, note, vel, Layer::Melody);
    }
}

void applyVariation (const Pattern& base, Pattern& out, const GenParams& p, float variation, int loopIndex)
{
    out = base;
    if (variation <= 0.0f || loopIndex <= 0) return;
    Rng rng (hashCombine (hashCombine (p.seed, 0xA11CE), (uint64_t) loopIndex));
    const float prob = variation * 0.35f;

    int w = 0;
    for (int i = 0; i < base.count; ++i)
    {
        NoteEvent e = base.events[(size_t) i];
        const Layer layer = (Layer) e.layer;
        if (layer == Layer::Chords || ! rng.chance (prob)) { out.events[(size_t) w++] = e; continue; }

        int action = rng.range (0, 3);
        if (action == 0) continue;                                  // drop note
        if (action == 1) e.note = (uint8_t) std::min (127, (int) e.note + 12);   // octave up
        if (action == 2 && e.note > 12) e.note = (uint8_t) (e.note - 12);        // octave down
        if (action == 3) e.velocity = (uint8_t) std::max (20, std::min (127, (int) e.velocity + rng.range (-25, 15)));
        out.events[(size_t) w++] = e;
    }
    out.count = w;
}
} // namespace seeder

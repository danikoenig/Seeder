#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace seeder
{
bool Pattern::add (int step, int len, int note, int vel, Layer layer)
{
    if (count >= kMaxEvents || step < 0 || step >= steps()) return false;
    NoteEvent& e = events[(size_t) count++];
    e.step     = (int16_t) step;
    e.length   = (int16_t) std::max (1, std::min (len, steps() - step));
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
// voicing: 0 auto (inversion by chance), 1 close, 2 open, 3 drop 2, 4 spread
Chord buildChord (ScaleId scale, int degree, int rootMidi, float ext, bool sus, bool invert, int voicing, Rng& rng)
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
    std::sort (c.notes, c.notes + c.count);

    switch (voicing)
    {
        case 0: if (invert && c.count > 2) { c.notes[0] += 12; std::sort (c.notes, c.notes + c.count); } break;
        case 2: for (int i = 1; i < c.count; i += 2) c.notes[i] += 12;
                std::sort (c.notes, c.notes + c.count); break;
        case 3: if (c.count >= 3) { c.notes[c.count - 2] -= 12; std::sort (c.notes, c.notes + c.count); } break;
        case 4: c.notes[0] -= 12; if (c.count > 2) c.notes[c.count - 1] += 12;
                std::sort (c.notes, c.notes + c.count); break;
        default: break;
    }
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

// One chord degree per slot. First four chords = the progression, then the phrase is answered:
// the second group ends on a cadence, the fourth group (16 bars) brings an alternative progression.
void buildSequence (const MoodProfile& mood, int progIdx, Rng& rng, int nSlots, int* seq)
{
    const int* base = mood.progressions[progIdx];
    const int* alt  = mood.progressions[(progIdx + 1 + rng.range (0, 2)) % 4];
    const bool cadence = rng.chance (0.7f);
    for (int i = 0; i < nSlots; ++i)
    {
        const int g = i / 4, j = i % 4;
        int d = base[j];
        if (g == 1 && j == 3 && cadence) d = 4;
        if (g == 3) d = alt[j];
        seq[i] = d;
    }
}

struct Hit { int step, degree; };
} // namespace

void generate (const GenParams& p, Pattern& out)
{
    out.clear();
    const MoodProfile&  mood  = moodProfile (p.mood);
    const StyleProfile& style = styleProfile (p.style);
    const Options&      opt   = p.options;
    const int nBars = opt.numBars();
    const int steps = nBars * kStepsPerBar;
    out.bars = nBars;
    out.suggestedSwing = style.swing;

    Rng rngMain (hashCombine (p.seed, 99));
    const ScaleId scale = mood.scales[rngMain.range (0, 1)];
    const int progIdx = rngMain.range (0, 3);
    out.scale = (int) scale;

    const int slotLen = opt.chordRate == 1 ? 8 : (opt.chordRate == 2 ? 32 : 16);
    const int nSlots  = steps / slotLen;
    int seq[64];
    buildSequence (mood, progIdx, rngMain, nSlots, seq);
    for (int i = 0; i < 4; ++i) out.progression[i] = seq[std::min (i, nSlots - 1)];

    const int rootMidi = 48 + p.rootPc;
    const float ext = std::min (1.0f, (mood.extension + p.complexity * 0.5f) * style.extensionMul);

    // ---- chord per slot ------------------------------------------------------
    Rng rngC (hashCombine (p.seed, 1));
    const char* chordT = style.chordTemplates[rngC.range (0, 2)];
    Chord chords[64];
    for (int i = 0; i < nSlots; ++i)
    {
        const bool sus = rngC.chance (0.05f + p.complexity * 0.12f);
        const bool inv = rngC.chance (p.complexity * 0.45f);
        chords[i] = buildChord (scale, seq[i], rootMidi, ext, sus, inv, opt.voicing, rngC);
    }
    auto slotOf = [&] (int step) { return std::min (nSlots - 1, step / slotLen); };

    // hit positions of a 16 step template inside one slot (a slot of 8 steps uses the first half)
    auto slotHits = [&] (const char* t, int* hits)
    {
        int nh = 0;
        const int period = slotLen == 8 ? 8 : 16;
        for (int s = 0; s < slotLen; ++s) if (t[s % period] != '.') hits[nh++] = s;
        if (nh == 0) hits[nh++] = 0;
        return nh;
    };

    Rng rngV (hashCombine (p.seed, 11));
    for (int i = 0; i < nSlots; ++i)
    {
        int hits[32];
        const int nh = slotHits (chordT, hits);
        const int period = slotLen == 8 ? 8 : 16;
        for (int h = 0; h < nh; ++h)
        {
            const int s = hits[h];
            const int next = (h + 1 < nh) ? hits[h + 1] : slotLen;
            const int len = style.chordLen == 0 ? (next - s) : std::min (style.chordLen, next - s);
            const int vel = (chordT[s % period] == 'X' ? 92 : 76) + rngV.range (-6, 6);
            for (int k = 0; k < chords[i].count; ++k)
                out.add (i * slotLen + s, len, chords[i].notes[k], vel - k * 2, Layer::Chords);
        }
    }

    // ---- stabs: short chord hits on their own rhythm --------------------------
    Rng rngS (hashCombine (p.seed, 5));
    const char* stabT = style.stabTemplates[rngS.range (0, 2)];
    const int stabLen = style.chordLen >= 4 ? 2 : 1;
    for (int i = 0; i < nSlots; ++i)
    {
        int hits[32];
        const int nh = slotHits (stabT, hits);
        const int n = std::min (4, chords[i].count);
        for (int h = 0; h < nh; ++h)
        {
            const int vel = 74 + rngS.range (-8, 8);
            for (int k = 0; k < n; ++k)
            {
                int note = chords[i].notes[k] + 12;
                while (note > 84) note -= 12;
                out.add (i * slotLen + hits[h], stabLen, note, vel - k, Layer::Stabs);
            }
        }
    }

    // ---- pad: the chord held for the whole slot ---------------------------------
    Rng rngP (hashCombine (p.seed, 6));
    for (int i = 0; i < nSlots; ++i)
    {
        const int n = std::min (4, chords[i].count);
        for (int k = 0; k < n; ++k)
        {
            int note = chords[i].notes[k];
            if (k == 0) note -= 12;   // root an octave lower for body
            out.add (i * slotLen, slotLen, note, 58 + rngP.range (-4, 4), Layer::Pad);
        }
    }

    // ---- bass --------------------------------------------------------------------
    Rng rngB (hashCombine (p.seed, 4));
    const char* bassT = style.bassTemplates[rngB.range (0, 2)];
    const int move = opt.bassMove;
    const bool altOct = (move == 2) || (move == 0 && p.style == Style::Disco);
    const bool rolling = (p.style == Style::Techno && bassT[1] == 'x');
    auto bassRootFor = [&] (int degree)
    {
        int r = rootMidi - 12 + degreeToSemitone (scale, degree);
        while (r > 50) r -= 12;
        while (r < 28) r += 12;
        return r;
    };
    const bool doFill = opt.fill > 0 && rngB.chance (opt.fill == 1 ? 0.6f : 1.0f);

    for (int b = 0; b < nBars; ++b)
    {
        const bool lastBar = (b == nBars - 1);
        const bool fillHere = lastBar && doFill;
        const int nextDeg = chords[slotOf ((b * 16 + 16) % steps)].degree;
        if (move == 3)   // walking: one note per beat
        {
            for (int q = 0; q < 4; ++q)
            {
                const int s = q * 4;
                if (fillHere && s >= 12) break;
                const int deg = chords[slotOf (b * 16 + s)].degree;
                int note = bassRootFor (deg);
                if (q == 1) note = bassRootFor (deg + 2) + (bassRootFor (deg + 2) < note ? 12 : 0);
                if (q == 2) note = bassRootFor (deg + 4) + (bassRootFor (deg + 4) < note ? 12 : 0);
                if (q == 3)
                {
                    const int below = degreeToSemitone (scale, nextDeg) - degreeToSemitone (scale, nextDeg - 1);
                    note = bassRootFor (nextDeg) - below;
                }
                while (note > 55) note -= 12;
                out.add (b * 16 + s, 4, note, (q == 0 ? 100 : 86) + rngB.range (-6, 6), Layer::Bass);
            }
        }
        else
        {
            int hitIdx = 0;
            for (int s = 0; s < 16; ++s)
            {
                if (bassT[s] == '.') continue;
                if (fillHere && s >= 14) continue;
                const int deg = chords[slotOf (b * 16 + s)].degree;
                int note = bassRootFor (deg);
                if (altOct) { if (hitIdx % 2 == 1) note += 12; }
                else if (move == 0)
                {
                    if (rngB.chance (style.bassOctave)) note += 12;
                    else if (rngB.chance (p.complexity * 0.12f)) note = rootMidi - 12 + degreeToSemitone (scale, deg + 4);
                }
                while (note > 55) note -= 12;
                const int len = rolling ? 1 : 2;
                const int vel = (s % 4 == 0 ? 100 : 86) + rngB.range (-6, 6);
                out.add (b * 16 + s, len, note, vel, Layer::Bass);
                ++hitIdx;
            }
        }
        if (fillHere)   // turnaround into the first chord of the next loop
        {
            const int below = degreeToSemitone (scale, nextDeg) - degreeToSemitone (scale, nextDeg - 1);
            int a = bassRootFor (nextDeg) - below, c = bassRootFor (nextDeg) - 1;
            while (a > 55) a -= 12;
            while (c > 55) c -= 12;
            out.add (b * 16 + 14, 1, a, 84, Layer::Bass);
            out.add (b * 16 + 15, 1, c, 92, Layer::Bass);
        }
    }

    // ---- arp -----------------------------------------------------------------------
    Rng rngA (hashCombine (p.seed, 3));
    const int autoMode = rngA.range (0, 4);
    const int arpMode = opt.arpPattern == 0 ? autoMode : opt.arpPattern - 1;   // up, down, updown, random, converge
    const int rate = opt.arpRate == 0 ? style.arpRate : (opt.arpRate == 1 ? 2 : 1);
    int toneSets[64][12]; int toneCount[64];
    for (int i = 0; i < nSlots; ++i)
    {
        int nt = 0;
        for (int k = 0; k < chords[i].count && nt < 6; ++k) toneSets[i][nt++] = chords[i].notes[k] + 12;
        for (int k = 0; k < chords[i].count && nt < 12; ++k) toneSets[i][nt++] = chords[i].notes[k] + 24;
        for (int k = 0; k < nt; ++k) while (toneSets[i][k] > 96) toneSets[i][k] -= 12;
        std::sort (toneSets[i], toneSets[i] + nt);
        toneCount[i] = nt;
    }
    for (int b = 0; b < nBars; ++b)
    {
        if (! rngA.chance (std::min (1.0f, style.arpActivity + p.density * 0.3f))) continue;
        int idx = 0;
        const float gate = 0.45f + p.density * 0.5f;
        const bool lastBar = (b == nBars - 1);
        for (int s = 0; s < 16; )
        {
            const int r = (lastBar && opt.fill == 2 && s >= 8) ? 1 : rate;
            const int sl = slotOf (b * 16 + s);
            const int nt = toneCount[sl];
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
            if (rngA.chance (gate))
            {
                const int vel = (s % 4 == 0 ? 82 : 66) + rngA.range (-8, 8);
                out.add (b * 16 + s, r, toneSets[sl][std::max (0, std::min (nt - 1, ni))], vel, Layer::Arp);
            }
            s += r;
        }
    }

    // ---- melody ---------------------------------------------------------------------
    Rng rngM (hashCombine (p.seed, 2));
    const float mDens = std::min (0.95f, style.melodyDensity * mood.densityMul * (0.4f + p.density * 1.2f));
    int deg = 7 + rngM.range (0, 2);   // start about an octave above the root
    int lastStep = -100;
    Hit melodyHits[kMaxSteps]; int nm = 0;

    for (int b = 0; b < nBars; ++b)
    {
        const int g = b / 4, inG = b % 4;
        if (g > 0 && inG < 3)   // repeat the bar of an earlier group, so the phrase has a shape
        {
            const float pc = (g == 1 ? 0.6f : (g == 2 ? 0.75f : 0.4f)) * (1.0f - p.complexity * 0.3f);
            if (rngM.chance (pc))
            {
                const int src = g == 1 ? b - 4 : b - 8;
                const int n0 = nm;
                for (int i = 0; i < n0; ++i)
                    if (melodyHits[i].step / 16 == src)
                    {
                        melodyHits[nm++] = { melodyHits[i].step + (b - src) * 16, melodyHits[i].degree };
                        deg = melodyHits[i].degree;
                        lastStep = melodyHits[nm - 1].step;
                    }
                continue;
            }
        }
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
                const int cd = chords[slotOf (b * 16 + s)].degree;
                for (int off : { 0, 1, -1, 2, -2 })
                    if (isChordDegree (deg + off, cd, 4)) { deg += off; break; }
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
    bool cover[kMaxSteps] {};
    Rng rngMv (hashCombine (p.seed, 12));
    for (int i = 0; i < nm; ++i)
    {
        const int next = (i + 1 < nm) ? melodyHits[i + 1].step : steps;
        const int len = std::max (1, std::min (next - melodyHits[i].step, p.style == Style::Ambient ? 12 : 4));
        int note = rootMidi + 12 + mood.melodyShift + degreeToSemitone (scale, melodyHits[i].degree - 7);
        while (note > 90) note -= 12;
        while (note < 52) note += 12;
        const int vel = (melodyHits[i].step % 4 == 0 ? 96 : 80) + rngMv.range (-8, 8);
        out.add (melodyHits[i].step, len, note, vel, Layer::Melody);
        for (int s = melodyHits[i].step; s < melodyHits[i].step + len && s < steps; ++s) cover[s] = true;
    }

    // ---- counter melody: answers in the gaps, moving against the melody -----------------
    Rng rngK (hashCombine (p.seed, 7));
    const float cDens = std::min (0.8f, style.melodyDensity * 0.6f * mood.densityMul * (0.4f + p.density * 1.2f));
    int cdeg = 5 + rngK.range (0, 2);
    int lastC = -100, mi = 0;
    for (int s = 0; s < steps; ++s)
    {
        while (mi + 1 < nm && melodyHits[mi + 1].step <= s) ++mi;
        if (cover[s] || s - lastC < 2) continue;
        float strength = (s % 4 == 0) ? 0.55f : (s % 2 == 0 ? 0.35f : 0.12f);
        if (! rngK.chance (std::min (0.9f, strength * cDens * 5.5f))) continue;

        int dir = 0;   // direction of the melody's last move
        if (nm > 1 && melodyHits[mi].step <= s && mi > 0)
            dir = melodyHits[mi].degree > melodyHits[mi - 1].degree ? 1 : (melodyHits[mi].degree < melodyHits[mi - 1].degree ? -1 : 0);
        if (dir == 0) dir = rngK.chance (0.5f) ? 1 : -1;
        cdeg -= dir * (rngK.chance (0.6f) ? 1 : 2);
        cdeg = std::max (2, std::min (10, cdeg));
        if (s % 4 == 0)
        {
            const int cd = chords[slotOf (s)].degree;
            for (int off : { 0, 1, -1, 2, -2 })
                if (isChordDegree (cdeg + off, cd, 4)) { cdeg += off; break; }
        }
        int next = steps;
        for (int t = s + 1; t < steps; ++t) if (cover[t]) { next = t; break; }
        const int len = std::max (1, std::min ({ next - s, 2 + rngK.range (0, 2), 4 }));
        int note = rootMidi + 12 + mood.melodyShift + degreeToSemitone (scale, cdeg - 7);
        while (note > 84) note -= 12;
        while (note < 48) note += 12;
        out.add (s, len, note, 72 + rngK.range (-8, 8), Layer::Counter);
        lastC = s;
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
        if (layer == Layer::Chords || layer == Layer::Pad || layer == Layer::Stabs || ! rng.chance (prob))
        {
            out.events[(size_t) w++] = e;
            continue;
        }

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

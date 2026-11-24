#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Generator.h"

namespace seeder
{
inline const char* layerName (int l)
{
    static const char* n[] { "Chords", "Melody", "Arp", "Bass" };
    return n[juce::jlimit (0, kNumLayers - 1, l)];
}

// Writes the pattern as a standard MIDI file (4 bars, 4/4, 960 ticks per quarter).
// layerMask: bit n = include layer n. One track per included layer, all on MIDI channel 1.
inline bool writePatternMidi (const Pattern& p, int layerMask, double bpm, const juce::File& file)
{
    constexpr int ppq = 960, ticksPerStep = ppq / 4;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);

    bool any = false;
    bool first = true;
    for (int layer = 0; layer < kNumLayers; ++layer)
    {
        if (! (layerMask & (1 << layer))) continue;
        juce::MidiMessageSequence seq;
        if (first)
        {
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
            first = false;
        }
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, layerName (layer)), 0.0);   // track name

        for (int i = 0; i < p.count; ++i)
        {
            const NoteEvent& e = p.events[(size_t) i];
            if (e.layer != layer) continue;
            const double on  = (double) e.step * ticksPerStep;
            const double off = (double) (e.step + e.length) * ticksPerStep - 1.0;
            seq.addEvent (juce::MidiMessage::noteOn (1, (int) e.note, (juce::uint8) e.velocity), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, (int) e.note), off);
            any = true;
        }
        seq.addEvent (juce::MidiMessage::endOfTrack(), (double) kPatternSteps * ticksPerStep);
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    if (! any) return false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (! os.openedOk()) return false;
    mf.writeTo (os, 1);
    return true;
}
} // namespace seeder

// Headless test of the plugin's trigger / clock logic (built with -DSEEDER_BUILD_TESTS=ON).
#include "PluginProcessor.h"
#include <map>
#include <set>
#include <cstdio>

namespace
{
int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct FakeHost : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setIsPlaying (playing);
        i.setPpqPosition (ppq);
        return i;
    }
};

struct Ev { int64_t t; bool on; int note, ch; };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 512;

    SeederProcessor proc;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    FakeHost host;
    proc.setPlayHead (&host);

    auto setParam = [&] (const char* id, float v)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    setParam ("latch", 0.0f);
    setParam ("quant", 2.0f);
    setParam ("swing", 0.0f);

    std::vector<Ev> events;
    juce::AudioBuffer<float> audio (2, block);
    int64_t pos = 0;
    std::map<std::pair<int,int>, int> sounding;   // (ch,note) -> count
    int stuck = 0;

    auto run = [&] (int blocks, std::function<void (int, juce::MidiBuffer&)> feed)
    {
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m;
            feed (b, m);
            host.ppq = (double) pos / sr * host.bpm / 60.0;
            proc.processBlock (audio, m);
            for (const auto meta : m)
            {
                auto msg = meta.getMessage();
                if (msg.isNoteOn())  { events.push_back ({ pos + meta.samplePosition, true,  msg.getNoteNumber(), msg.getChannel() }); ++sounding[{ msg.getChannel(), msg.getNoteNumber() }]; }
                if (msg.isNoteOff()) { events.push_back ({ pos + meta.samplePosition, false, msg.getNoteNumber(), msg.getChannel() }); --sounding[{ msg.getChannel(), msg.getNoteNumber() }]; }
            }
            pos += block;
        }
    };

    const int blocksPerBar = (int) (sr * 2.0 / block);   // 120 bpm: 1 bar = 2 s
    // 1) idle: no output
    run (4, [] (int, juce::MidiBuffer&) {});
    CHECK (events.empty(), "idle plugin produced MIDI");

    // 2) play C3, hold for 2 bars
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 10); });
    run (blocksPerBar * 2, [] (int, juce::MidiBuffer&) {});
    CHECK (! events.empty(), "no notes after trigger");

    const double spStep = sr * 60.0 / (120.0 * 4.0);
    int64_t firstOn = -1;
    for (auto& e : events) if (e.on) { firstOn = e.t; break; }
    const double stepsFromStart = (double) firstOn / spStep;
    CHECK (std::abs (stepsFromStart - std::round (stepsFromStart)) < 0.05, "first note-on is off the 16th grid (%.3f)", stepsFromStart);
    for (auto& e : events)
        if (e.on) { double s = (double) e.t / spStep; CHECK (std::abs (s - std::round (s)) < 0.05, "note-on off grid %.3f", s); }

    std::set<int> channels;
    for (auto& e : events) channels.insert (e.ch);
    CHECK (channels.size() >= 3, "expected >= 3 layers/channels, got %d", (int) channels.size());

    // 3) switch root note to F3 mid-pattern: new pattern may only start on a bar line
    size_t before = events.size();
    const int64_t switchAt = pos;
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 53, (juce::uint8) 100), 0); });
    run (blocksPerBar * 2, [] (int, juce::MidiBuffer&) {});
    CHECK (events.size() > before, "no output after note change");

    // 4) release the key (latch off): everything must stop and nothing may hang
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (1, 53), 0);
                                           m.addEvent (juce::MidiMessage::noteOff (1, 48), 0); });
    size_t afterRelease = events.size();
    run (blocksPerBar, [] (int, juce::MidiBuffer&) {});
    int onsAfter = 0;
    for (size_t i = afterRelease; i < events.size(); ++i) onsAfter += events[i].on ? 1 : 0;
    CHECK (onsAfter == 0, "%d note-ons after release", onsAfter);
    for (auto& kv : sounding) if (kv.second != 0) ++stuck;
    CHECK (stuck == 0, "%d stuck notes after release", stuck);

    // 5) transport stop with latch on must also release everything
    setParam ("latch", 1.0f);
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); });
    run (blocksPerBar, [] (int, juce::MidiBuffer&) {});
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (1, 48), 0); });   // latch keeps it running
    run (blocksPerBar, [] (int, juce::MidiBuffer&) {});
    const size_t beforeStop = events.size();
    CHECK (beforeStop > afterRelease, "latch: pattern should keep playing after key release");
    host.playing = false;
    run (blocksPerBar, [] (int, juce::MidiBuffer&) {});
    int onsAfterStop = 0;
    for (size_t i = beforeStop; i < events.size(); ++i) onsAfterStop += events[i].on ? 1 : 0;
    CHECK (onsAfterStop == 0, "%d note-ons after transport stop", onsAfterStop);
    stuck = 0;
    for (auto& kv : sounding) if (kv.second != 0) ++stuck;
    CHECK (stuck == 0, "%d stuck notes after transport stop", stuck);
    (void) switchAt;

    std::printf ("%d events, %d failure(s)\n", (int) events.size(), failures);
    proc.releaseResources();
    return failures ? 1 : 0;
}

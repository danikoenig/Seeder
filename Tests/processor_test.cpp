// Headless test of the plugin's trigger / clock / link logic (built with -DSEEDER_BUILD_TESTS=ON).
#include "PluginProcessor.h"
#include "MidiExport.h"
#include <map>
#include <set>
#include <tuple>
#include <cstdio>

namespace
{
int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

constexpr double sr = 48000.0;
constexpr int block = 512;
constexpr int blocksPerBar = (int) (sr * 2.0 / block);   // 120 bpm: 1 bar = 2 s

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

struct Rig
{
    explicit Rig (const juce::File& linkFile) : proc (linkFile)
    {
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);
        proc.setPlayHead (&host);
        audio.setSize (2, block);
    }
    ~Rig() { proc.setPlayHead (nullptr); }

    void set (const char* id, float v)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    float get (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); }

    // one block; pos = start sample of this block (shared timeline)
    void process (int64_t pos, const juce::MidiBuffer& in)
    {
        juce::MidiBuffer m (in);
        host.ppq = (double) pos / sr * host.bpm / 60.0;
        proc.processBlock (audio, m);
        for (const auto meta : m)
        {
            auto msg = meta.getMessage();
            const Ev e { pos + meta.samplePosition, msg.isNoteOn(), msg.getNoteNumber(), msg.getChannel() };
            if (msg.isNoteOn())  { events.push_back (e); ++sounding[{ e.ch, e.note }]; }
            if (msg.isNoteOff()) { events.push_back (e); --sounding[{ e.ch, e.note }]; }
        }
    }
    int stuck() const { int n = 0; for (auto& kv : sounding) if (kv.second != 0) ++n; return n; }

    FakeHost host;
    SeederProcessor proc;
    juce::AudioBuffer<float> audio;
    std::vector<Ev> events;
    std::map<std::pair<int,int>, int> sounding;
};

juce::File tempLink (const char* name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    return f;
}

void noteOn (juce::MidiBuffer& m, int note, int sample = 10) { m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), sample); }
void noteOff (juce::MidiBuffer& m, int note, int sample = 0) { m.addEvent (juce::MidiMessage::noteOff (1, note), sample); }

// ---------------------------------------------------------------------------------
void testSingleInstance()
{
    Rig r (tempLink ("seeder-test-single.bin"));
    r.set ("latch", 0.0f);
    r.set ("quant", 2.0f);
    r.set ("swing", 0.0f);
    r.set ("link", 0.0f);

    int64_t pos = 0;
    auto run = [&] (int blocks, std::function<void (int, juce::MidiBuffer&)> feed)
    {
        for (int b = 0; b < blocks; ++b) { juce::MidiBuffer m; feed (b, m); r.process (pos, m); pos += block; }
    };
    auto idle = [] (int, juce::MidiBuffer&) {};

    run (4, idle);
    CHECK (r.events.empty(), "idle plugin produced MIDI");

    run (1, [] (int, juce::MidiBuffer& m) { noteOn (m, 48); });
    run (blocksPerBar * 2, idle);
    CHECK (! r.events.empty(), "no notes after trigger");

    const double spStep = sr * 60.0 / (120.0 * 4.0);
    for (auto& e : r.events)
        if (e.on) { double s = (double) e.t / spStep; CHECK (std::abs (s - std::round (s)) < 0.05, "note-on off grid %.3f", s); }

    std::set<int> channels;
    for (auto& e : r.events) channels.insert (e.ch);
    CHECK (channels.size() >= 3, "expected >= 3 layers/channels, got %d", (int) channels.size());

    // mute one layer: channel 1 (chords) must go silent
    r.set ("onChords", 0.0f);
    run (blocksPerBar, idle);   // let sounding notes finish
    const size_t mark = r.events.size();
    run (blocksPerBar * 2, idle);
    int chords = 0;
    for (size_t i = mark; i < r.events.size(); ++i) if (r.events[i].on && r.events[i].ch == 1) ++chords;
    CHECK (chords == 0, "muted layer still plays (%d notes)", chords);
    r.set ("onChords", 1.0f);

    // new root
    size_t before = r.events.size();
    run (1, [] (int, juce::MidiBuffer& m) { noteOn (m, 53); });
    run (blocksPerBar * 2, idle);
    CHECK (r.events.size() > before, "no output after note change");

    // release (latch off)
    run (1, [] (int, juce::MidiBuffer& m) { noteOff (m, 53); noteOff (m, 48); });
    size_t afterRelease = r.events.size();
    run (blocksPerBar, idle);
    int onsAfter = 0;
    for (size_t i = afterRelease; i < r.events.size(); ++i) onsAfter += r.events[i].on ? 1 : 0;
    CHECK (onsAfter == 0, "%d note-ons after release", onsAfter);
    CHECK (r.stuck() == 0, "%d stuck notes after release", r.stuck());

    // latch + transport stop
    r.set ("latch", 1.0f);
    run (1, [] (int, juce::MidiBuffer& m) { noteOn (m, 48); });
    run (blocksPerBar, idle);
    run (1, [] (int, juce::MidiBuffer& m) { noteOff (m, 48); });
    run (blocksPerBar, idle);
    const size_t beforeStop = r.events.size();
    CHECK (beforeStop > afterRelease, "latch: pattern should keep playing after key release");
    r.host.playing = false;
    run (blocksPerBar, idle);
    int onsAfterStop = 0;
    for (size_t i = beforeStop; i < r.events.size(); ++i) onsAfterStop += r.events[i].on ? 1 : 0;
    CHECK (onsAfterStop == 0, "%d note-ons after transport stop", onsAfterStop);
    CHECK (r.stuck() == 0, "%d stuck notes after transport stop", r.stuck());
}

// ---------------------------------------------------------------------------------
void testLink()
{
    const auto file = tempLink ("seeder-test-link.bin");
    Rig a (file), b (file), solo (tempLink ("seeder-test-solo.bin"));
    solo.set ("link", 0.0f);

    a.set ("role", 1.0f);   // chords only
    b.set ("role", 4.0f);   // bass only
    a.set ("latch", 0.0f);
    solo.set ("latch", 0.0f);

    // join: a publishes, b adopts
    a.proc.pollLink(); b.proc.pollLink(); a.proc.pollLink(); b.proc.pollLink();
    CHECK (a.proc.uiLinked.load() == 2 && b.proc.uiLinked.load() == 2, "linked count %d/%d", a.proc.uiLinked.load(), b.proc.uiLinked.load());
    CHECK (b.get ("latch") < 0.5f, "b did not adopt the group's latch setting");

    // a parameter change on one instance reaches the other
    a.set ("mood", 3.0f); a.set ("style", 2.0f); a.set ("complexity", 0.8f); a.set ("seed", 42.0f);
    const char* optIds[] { "bars", "voicing", "chordRate", "arpPattern", "arpRate", "bassMove", "fill" };
    const float optVals[] { 0.0f, 3.0f, 1.0f, 3.0f, 2.0f, 3.0f, 2.0f };
    for (int i = 0; i < 7; ++i) a.set (optIds[i], optVals[i]);
    a.set ("humanize", 0.4f);
    a.proc.pollLink(); b.proc.pollLink();
    for (int i = 0; i < 7; ++i) CHECK ((int) b.get (optIds[i]) == (int) optVals[i], "option %s not synced", optIds[i]);
    CHECK (std::abs (b.get ("humanize") - 0.4f) < 0.002f, "humanize not synced");
    a.set ("humanize", 0.0f);
    a.proc.pollLink(); b.proc.pollLink();
    CHECK ((int) b.get ("mood") == 3 && (int) b.get ("style") == 2, "mood/style not synced");
    CHECK (std::abs (b.get ("complexity") - 0.8f) < 0.002f && (int) b.get ("seed") == 42, "complexity/seed not synced");
    // and back
    b.set ("density", 0.25f);
    b.proc.pollLink(); a.proc.pollLink();
    CHECK (std::abs (a.get ("density") - 0.25f) < 0.002f, "density not synced back");

    for (int i = 0; i < 7; ++i) solo.set (optIds[i], optVals[i]);
    solo.set ("mood", 3.0f); solo.set ("style", 2.0f); solo.set ("complexity", 0.8f); solo.set ("seed", 42.0f); solo.set ("density", 0.25f);

    // key goes only to instance a; b follows through the link
    int64_t pos = 0;
    auto run = [&] (int blocks, std::function<void (int, juce::MidiBuffer&)> feed)
    {
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer m, none;
            feed (i, m);
            a.process (pos, m); solo.process (pos, m); b.process (pos, none);
            pos += block;
        }
    };
    auto idle = [] (int, juce::MidiBuffer&) {};
    run (2, idle);
    run (1, [] (int, juce::MidiBuffer& m) { noteOn (m, 50); });
    run (blocksPerBar * 3, idle);

    auto notesOf = [] (const std::vector<Ev>& ev, int ch)
    {
        std::set<std::tuple<int64_t, int, bool>> s;
        for (auto& e : ev) if (e.ch == ch) s.insert (std::make_tuple (e.t, e.note, e.on));
        return s;
    };
    const auto soloChords = notesOf (solo.events, 1);   // role ALL: chords on ch 1, bass on ch 4
    const auto soloBass   = notesOf (solo.events, 4);
    CHECK (! soloChords.empty() && ! soloBass.empty(), "solo reference is empty");
    CHECK (notesOf (a.events, 1) == soloChords, "instance A (role chords) differs from the single-instance chords");
    CHECK (notesOf (b.events, 1) == soloBass,   "instance B (role bass, channel 1) differs from the single-instance bass");
    for (auto& e : a.events) CHECK (e.ch == 1, "role instance sent on channel %d", e.ch);
    // a 4-bar phrase with a fill: the pattern must have looped at least once in 3 bars of this run
    CHECK (a.proc.uiStep.load() < 64, "step %d beyond a 4-bar phrase", a.proc.uiStep.load());

    // releasing the key on a stops b as well
    run (1, [] (int, juce::MidiBuffer& m) { noteOff (m, 50); });
    const size_t mark = b.events.size();
    run (blocksPerBar, idle);
    int ons = 0;
    for (size_t i = mark; i < b.events.size(); ++i) ons += b.events[i].on ? 1 : 0;
    CHECK (ons == 0, "instance B kept playing after the key was released on A (%d)", ons);
    CHECK (a.stuck() == 0 && b.stuck() == 0, "stuck notes a=%d b=%d", a.stuck(), b.stuck());
}

// ---------------------------------------------------------------------------------
void testMidiExport()
{
    seeder::GenParams p;
    p.seed = seeder::seedFromNote (48, 0, p.mood, p.style);
    seeder::Pattern pat;
    seeder::generate (p, pat);

    int perLayer[seeder::kNumLayers] {};
    for (int i = 0; i < pat.count; ++i) ++perLayer[pat.events[(size_t) i].layer];

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("seeder-test-export.mid");
    CHECK (seeder::writePatternMidi (pat, 1 << (int) seeder::Layer::Bass, 128.0, file), "export failed");
    juce::MidiFile mf;
    juce::FileInputStream in (file);
    CHECK (in.openedOk() && mf.readFrom (in), "cannot read the exported file");
    int ons = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (int i = 0; i < mf.getTrack (t)->getNumEvents(); ++i)
            if (mf.getTrack (t)->getEventPointer (i)->message.isNoteOn()) ++ons;
    CHECK (ons == perLayer[(int) seeder::Layer::Bass], "exported %d bass notes, expected %d", ons, perLayer[(int) seeder::Layer::Bass]);

    CHECK (seeder::writePatternMidi (pat, (1 << seeder::kNumLayers) - 1, 120.0, file), "export all failed");
    juce::MidiFile all;
    juce::FileInputStream in2 (file);
    CHECK (in2.openedOk() && all.readFrom (in2) && all.getNumTracks() == seeder::kNumLayers, "expected %d tracks", seeder::kNumLayers);
    file.deleteFile();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testSingleInstance();
    testLink();
    testMidiExport();
    std::printf ("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "Engine/Generator.h"
#include "Link.h"

// Everything needed to (re)build one pattern. Identical on all linked instances.
struct TriggerSnapshot
{
    int note = -1;
    uint64_t seed = 0;
    int mood = 0, style = 0;
    int complexity1000 = 500, density1000 = 500;
    int opts = 0;               // seeder::Options::pack()

    bool samePattern (const TriggerSnapshot& o) const
    {
        return note == o.note && seed == o.seed && mood == o.mood && style == o.style
            && complexity1000 == o.complexity1000 && density1000 == o.density1000 && opts == o.opts;
    }
};

namespace ParamIDs
{
inline const char* layerOn (int l)      { static const char* n[] { "onChords", "onStabs", "onPad", "onBass", "onMelody", "onCounter", "onArp" }; return n[l]; }
inline const char* layerChannel (int l) { static const char* n[] { "chChords", "chStabs", "chPad", "chBass", "chMelody", "chCounter", "chArp" }; return n[l]; }
}

class SeederProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SeederProcessor();
    explicit SeederProcessor (const juce::File& linkFile);   // tests: private link file
    ~SeederProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // UI actions (message thread)
    void resetToInit();     // all parameters back to default
    void newSeed();         // random seed + rebuild the running pattern
    void pollLink();        // shared parameters; called by a timer, also by tests

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // read by the editor
    std::atomic<int>      uiRootNote { -1 };          // active or pending trigger note, -1 = idle
    std::atomic<int>      uiStep { 0 };               // 0 .. steps-1
    std::atomic<int>      uiLinked { 0 };             // linked instances in this group (including this one)
    std::atomic<double>   uiBpm { 120.0 };
    std::atomic<uint32_t> uiPatternVersion { 0 };     // changes whenever a new pattern starts
    std::atomic<uint64_t> uiSeed { 0 };
    std::atomic<int>      uiMood { 0 }, uiStyle { 0 }, uiRootPc { 0 }, uiComplexity { 500 }, uiDensity { 500 }, uiOpts { 0 };

    // Rebuilds the pattern that is playing (or played last). False if there is none yet.
    bool getUiPattern (seeder::Pattern& out) const;

private:
    void timerCallback() override { pollLink(); }
    void init();
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool  linkOn() const { return param ("link") > 0.5f && link.isOpen(); }
    int   group() const { return juce::jlimit (0, seeder::kLinkGroups - 1, (int) param ("group")); }

    struct Scheduled { int64_t sample; uint8_t channel, note, velocity; bool on; };
    static constexpr int kMaxScheduled = 2048;

    void schedule (int64_t sample, int channel, int note, int vel, bool on);
    void flushAll (juce::MidiBuffer& out, int offsetInBlock);
    void fireTrigger (int note, bool broadcast);
    void startPattern (const TriggerSnapshot&, int64_t stepNumber);
    int  readOptions() const;
    void readShared (int32_t* v) const;
    void applyShared (const int32_t* v);

    // scheduler
    std::array<Scheduled, kMaxScheduled> queue {};
    int queueCount = 0;

    // musical state (audio thread)
    seeder::Pattern basePattern, playPattern;
    seeder::GenParams genParams;
    seeder::Rng randomSource { 12345 };
    TriggerSnapshot pendingSnap, activeSnap;
    int activeNote = -1, pendingNote = -1;
    int64_t patternStart = 0;
    int64_t loopsDone = 0;
    std::array<bool, 128> held {};
    int heldCount = 0;
    std::atomic<bool> regenRequested { false };

    // clock
    double sampleRate = 44100.0;
    double internalPpq = 0.0;
    int64_t sampleCounter = 0;
    bool hostWasPlaying = false;
    bool wasIdleStart = false;

    // link
    seeder::LinkHub link;
    uint32_t myId = 0;
    std::atomic<int>      joinedGroup { -1 };
    std::atomic<uint32_t> lastTrigSeq { 0 };
    uint32_t lastParamSeq = 0;
    int32_t  lastPublished[seeder::kLinkParams] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeederProcessor)
};

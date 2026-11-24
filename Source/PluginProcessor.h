#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "Engine/Generator.h"

class SeederProcessor : public juce::AudioProcessor
{
public:
    SeederProcessor();
    ~SeederProcessor() override = default;

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

    juce::AudioProcessorValueTreeState apvts;

    // read by the editor (message thread)
    std::atomic<int> uiRootNote { -1 };    // MIDI note of the active trigger, -1 = idle
    std::atomic<int> uiStep { 0 };         // 0 .. kPatternSteps-1

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Scheduled { int64_t sample; uint8_t channel, note, velocity; bool on; };
    static constexpr int kMaxScheduled = 2048;

    void schedule (int64_t sample, int channel, int note, int vel, bool on);
    void flushAll (juce::MidiBuffer& out, int offsetInBlock);
    void startPattern (int note, int64_t stepNumber);
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // scheduler
    std::array<Scheduled, kMaxScheduled> queue {};
    int queueCount = 0;

    // musical state
    seeder::Pattern basePattern, playPattern;
    seeder::GenParams genParams;
    seeder::Rng randomSource { 12345 };
    int activeNote = -1, pendingNote = -1, lastTrigger = -1;
    int64_t patternStart = 0;
    int64_t loopsDone = 0;
    std::array<bool, 128> held {};
    int heldCount = 0;

    // clock
    double sampleRate = 44100.0;
    double internalPpq = 0.0;
    int64_t sampleCounter = 0;
    bool hostWasPlaying = false;
    bool wasIdleStart = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeederProcessor)
};

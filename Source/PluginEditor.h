#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// All controls live in a fixed 360x600 panel that is scaled with the window,
// so the plugin opens at 100 % and stays resizable (like the other plugins).
class SeederEditor : public juce::AudioProcessorEditor
{
public:
    explicit SeederEditor (SeederProcessor&);
    ~SeederEditor() override;
    void resized() override;

    static constexpr int kBaseW = 360, kBaseH = 600;

private:
    class Panel;
    std::unique_ptr<Panel> panel;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeederEditor)
};

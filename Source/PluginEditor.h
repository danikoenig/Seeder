#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The whole UI lives in a fixed-size panel that is scaled with the window,
// so the plug-in opens at 100 % and can be resized by dragging the corner.
class SeederEditor : public juce::AudioProcessorEditor
{
public:
    explicit SeederEditor (SeederProcessor&);
    ~SeederEditor() override;
    void resized() override;

    static constexpr int kBaseW = 1100, kBaseH = 662;

private:
    class Panel;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeederEditor)
};

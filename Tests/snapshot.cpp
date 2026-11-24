// Renders the editor to a PNG (layout check without a DAW):  SeederSnapshot out.png
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : "seeder.png");

    SeederProcessor proc (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("seeder-snapshot-link.bin"));
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.apvts.getParameter ("mood")->setValueNotifyingHost (proc.apvts.getParameter ("mood")->convertTo0to1 (1.0f));
    proc.apvts.getParameter ("onArp")->setValueNotifyingHost (0.0f);

    juce::AudioBuffer<float> audio (2, 512);
    for (int b = 0; b < 400; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        proc.processBlock (audio, m);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    return 0;
}

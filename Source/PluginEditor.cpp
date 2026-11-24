#include "PluginEditor.h"

namespace
{
const juce::Colour kBg      { 0xff111318 };
const juce::Colour kPanel   { 0xff1b1e26 };
const juce::Colour kAccent  { 0xff3d8bff };
const juce::Colour kText    { 0xffe6e8ee };
const juce::Colour kDim     { 0xff8a90a0 };
}

class SeederEditor::Panel : public juce::Component, private juce::Timer
{
public:
    explicit Panel (SeederProcessor& p) : proc (p)
    {
        setSize (kBaseW, kBaseH);

        auto addLabel = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, kDim);
            l.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (l);
        };

        auto setupCombo = [this] (juce::ComboBox& c, const juce::StringArray& items)
        {
            c.addItemList (items, 1);
            c.setColour (juce::ComboBox::backgroundColourId, kPanel);
            c.setColour (juce::ComboBox::textColourId, kText);
            c.setColour (juce::ComboBox::outlineColourId, kPanel.brighter (0.2f));
            c.setColour (juce::ComboBox::arrowColourId, kAccent);
            addAndMakeVisible (c);
        };

        juce::StringArray moods, styles;
        for (int i = 0; i < seeder::kNumMoods; ++i)  moods.add (seeder::moodName (i));
        for (int i = 0; i < seeder::kNumStyles; ++i) styles.add (seeder::styleName (i));

        addLabel (lMood, "Mood");     setupCombo (cMood, moods);
        addLabel (lStyle, "Style");   setupCombo (cStyle, styles);
        addLabel (lMode, "Note Mode"); setupCombo (cMode, { "Seed per Note", "Transpose", "Random" });
        addLabel (lQuant, "Change at"); setupCombo (cQuant, { "Next 16th", "Next Beat", "Next Bar" });

        auto setupSlider = [this] (juce::Slider& s, bool integer)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
            s.setColour (juce::Slider::trackColourId, kAccent);
            s.setColour (juce::Slider::thumbColourId, kText);
            s.setColour (juce::Slider::backgroundColourId, kPanel);
            s.setColour (juce::Slider::textBoxTextColourId, kText);
            s.setColour (juce::Slider::textBoxOutlineColourId, kPanel);
            if (integer) s.setNumDecimalPlacesToDisplay (0);
            addAndMakeVisible (s);
        };
        addLabel (lComplexity, "Complexity"); setupSlider (sComplexity, false);
        addLabel (lDensity, "Density");       setupSlider (sDensity, false);
        addLabel (lSwing, "Swing");           setupSlider (sSwing, false);
        addLabel (lVariation, "Variation");   setupSlider (sVariation, false);
        addLabel (lSeed, "Seed");             setupSlider (sSeed, true);

        auto setupToggle = [this] (juce::ToggleButton& b, const juce::String& text)
        {
            b.setButtonText (text);
            b.setColour (juce::ToggleButton::textColourId, kText);
            b.setColour (juce::ToggleButton::tickColourId, kAccent);
            b.setColour (juce::ToggleButton::tickDisabledColourId, kDim);
            addAndMakeVisible (b);
        };
        setupToggle (bLatch, "Latch (keep playing after key release)");
        setupToggle (bChords, "Chords");
        setupToggle (bMelody, "Melody");
        setupToggle (bArp, "Arp");
        setupToggle (bBass, "Bass");

        auto& v = proc.apvts;
        aMood  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (v, "mood", cMood);
        aStyle = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (v, "style", cStyle);
        aMode  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (v, "mode", cMode);
        aQuant = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (v, "quant", cQuant);
        aComplexity = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (v, "complexity", sComplexity);
        aDensity    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (v, "density", sDensity);
        aSwing      = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (v, "swing", sSwing);
        aVariation  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (v, "variation", sVariation);
        aSeed       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (v, "seed", sSeed);
        aLatch  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (v, "latch", bLatch);
        aChords = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (v, "onChords", bChords);
        aMelody = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (v, "onMelody", bMelody);
        aArp    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (v, "onArp", bArp);
        aBass   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (v, "onBass", bBass);

        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBg);

        // header: big title, small "by Dani Koenig · Version" next to it
        g.setColour (kText);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText ("Seeder", 20, 14, 130, 40, juce::Justification::centredLeft);
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String ("by Dani Koenig  ") + juce::String::fromUTF8 ("\xc2\xb7") + " v" SEEDER_VERSION,
                    140, 14, 200, 40, juce::Justification::centredLeft);

        // status box
        auto box = juce::Rectangle<float> (20.0f, 62.0f, 320.0f, 54.0f);
        g.setColour (kPanel);
        g.fillRoundedRectangle (box, 6.0f);

        const int root = proc.uiRootNote.load();
        g.setColour (root >= 0 ? kText : kDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (root >= 0 ? "Root  " + juce::MidiMessage::getMidiNoteName (root, true, true, 3)
                              : juce::String ("Play a note to plant a seed"),
                    box.toNearestInt().reduced (10, 4).removeFromTop (22), juce::Justification::centredLeft);

        // 64-step position bar, grouped by bar
        const int cur = root >= 0 ? proc.uiStep.load() : -1;
        const float x0 = box.getX() + 10.0f, y0 = box.getBottom() - 20.0f;
        const float w = (box.getWidth() - 20.0f - 3.0f * 6.0f) / 64.0f;
        for (int i = 0; i < seeder::kPatternSteps; ++i)
        {
            const float x = x0 + (float) i * w + (float) (i / 16) * 6.0f;
            g.setColour (i == cur ? kAccent : (i / 4 % 2 == 0 ? kDim.withAlpha (0.45f) : kDim.withAlpha (0.25f)));
            g.fillRect (x, y0, juce::jmax (1.0f, w - 1.0f), 10.0f);
        }

        g.setColour (kDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("MIDI channels (change in host): Chords 1  Melody 2  Arp 3  Bass 4",
                    20, kBaseH - 40, 320, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto row = [] (int y, int h = 28) { return juce::Rectangle<int> (20, y, 320, h); };
        auto place = [&] (juce::Label& l, juce::Component& c, int y)
        {
            auto r = row (y);
            l.setBounds (r.removeFromLeft (92));
            c.setBounds (r);
        };
        place (lMood, cMood, 132);
        place (lStyle, cStyle, 166);
        place (lMode, cMode, 200);
        place (lQuant, cQuant, 234);

        place (lComplexity, sComplexity, 286);
        place (lDensity, sDensity, 318);
        place (lSwing, sSwing, 350);
        place (lVariation, sVariation, 382);
        place (lSeed, sSeed, 414);

        bLatch.setBounds (row (458));
        const int w = 320 / 4;
        bChords.setBounds (20, 494, w, 28);
        bMelody.setBounds (20 + w, 494, w, 28);
        bArp.setBounds (20 + 2 * w, 494, w, 28);
        bBass.setBounds (20 + 3 * w, 494, w, 28);
    }

private:
    void timerCallback() override { repaint (0, 56, kBaseW, 66); }

    SeederProcessor& proc;

    juce::Label lMood, lStyle, lMode, lQuant, lComplexity, lDensity, lSwing, lVariation, lSeed;
    juce::ComboBox cMood, cStyle, cMode, cQuant;
    juce::Slider sComplexity, sDensity, sSwing, sVariation, sSeed;
    juce::ToggleButton bLatch, bChords, bMelody, bArp, bBass;

    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> aMood, aStyle, aMode, aQuant;
    std::unique_ptr<SA> aComplexity, aDensity, aSwing, aVariation, aSeed;
    std::unique_ptr<BA> aLatch, aChords, aMelody, aArp, aBass;
};

SeederEditor::SeederEditor (SeederProcessor& p) : AudioProcessorEditor (&p)
{
    panel = std::make_unique<Panel> (p);
    addAndMakeVisible (*panel);

    constrainer.setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    constrainer.setSizeLimits (kBaseW * 3 / 4, kBaseH * 3 / 4, kBaseW * 2, kBaseH * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (kBaseW, kBaseH);   // opens at 100 %
}

SeederEditor::~SeederEditor() = default;

void SeederEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    panel->setBounds (0, 0, kBaseW, kBaseH);
    panel->setTransform (juce::AffineTransform::scale (s));
}

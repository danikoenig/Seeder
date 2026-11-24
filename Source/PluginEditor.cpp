#include "PluginEditor.h"
#include "Look.h"
#include "Knob.h"
#include "Widgets.h"

using namespace SeedColours;

namespace
{
constexpr int W = SeederEditor::kBaseW;
constexpr int H = SeederEditor::kBaseH;
constexpr int margin = 14;
constexpr int cardsY = 56, cardsH = 196;
constexpr int rowsY = 262, rowH = 90, rowGap = 8;

const juce::Colour triggerCol = accent;      // Orange
const juce::Colour styleCol   = env;         // Gelb
const juce::Colour shapeCol   = delayCol;    // Tuerkis
const juce::Colour linkCol    = reverbCol;   // Lavendel
const juce::Colour seedCol    = clip;        // Rot

const char* layerTitle[] { "CHORDS", "MELODY", "ARP", "BASS" };
const char* layerHint[]  { "harmony", "lead line", "arpeggio", "bass line" };

void drawCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
{
    auto f = r.toFloat();
    g.setColour (card);
    g.fillRoundedRectangle (f, 10.0f);
    g.setColour (cardBorder);
    g.drawRoundedRectangle (f.reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (c);
        g.fillRoundedRectangle ((float) r.getX() + 12.0f, (float) r.getY() + 13.0f, 3.0f, 11.0f, 1.5f);
        g.setColour (text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.getX() + 21, r.getY() + 8, 200, 20, juce::Justification::centredLeft);
    }
}

void smallLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (t, r, j);
}

juce::String fileSafe (juce::String s)
{
    juce::String out;
    for (auto c : s) out << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String ("-"));
    while (out.contains ("--")) out = out.replace ("--", "-");
    return out.trimCharactersAtEnd ("-");
}
}

class SeederEditor::Panel : public juce::Component, private juce::Timer
{
public:
    explicit Panel (SeederProcessor& p) : proc (p)
    {
        setLookAndFeel (&lnf);
        auto& a = proc.apvts;

        // ---- header buttons ----
        newButton.getProperties().set ("primary", true);
        newButton.setTooltip ("New seed: rebuilds the running pattern with a random seed");
        newButton.onClick = [this] { proc.newSeed(); };
        addAndMakeVisible (newButton);
        resetButton.setTooltip ("All controls back to default");
        resetButton.onClick = [this] { proc.resetToInit(); };
        addAndMakeVisible (resetButton);

        dragAll = std::make_unique<DragHandle> ("DRAG ALL", "4 TRACKS .MID", accent, [this] { return exportFile (0xF, "All"); });
        dragAll->setTooltip ("Drag all four layers into your DAW as one MIDI file (one track per layer)");
        addAndMakeVisible (*dragAll);

        // ---- trigger card ----
        modeSwitch = std::make_unique<ChoiceSwitch> (*a.getParameter ("mode"), juce::StringArray { "SEED", "TRANSPOSE", "RANDOM" }, triggerCol);
        modeSwitch->setTooltip ("SEED: every key has its own reproducible pattern. TRANSPOSE: same pattern, new root. RANDOM: new material at every key.");
        addAndMakeVisible (*modeSwitch);
        quantSwitch = std::make_unique<ChoiceSwitch> (*a.getParameter ("quant"), juce::StringArray { "16TH", "BEAT", "BAR" }, triggerCol);
        quantSwitch->setTooltip ("When a new key takes over: at the next 16th, beat or bar");
        addAndMakeVisible (*quantSwitch);

        latchButton.getProperties().set ("style", "labelled");
        latchButton.getProperties().set ("accent", triggerCol.toString());
        latchButton.setTooltip ("ON: keeps playing after you release the key. OFF: plays only while a key is held.");
        addAndMakeVisible (latchButton);
        latchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (a, "latch", latchButton);

        // ---- style card ----
        juce::StringArray moods, styles;
        for (int i = 0; i < seeder::kNumMoods; ++i)  moods.add (seeder::moodName (i));
        for (int i = 0; i < seeder::kNumStyles; ++i) styles.add (seeder::styleName (i));
        moodBox.addItemList (moods, 1);
        styleBox.addItemList (styles, 1);
        for (auto* b : { &moodBox, &styleBox })
        {
            b->getProperties().set ("accent", styleCol.toString());
            addAndMakeVisible (*b);
        }
        moodBox.setTooltip ("Scale, chord progressions, chord extensions and register");
        styleBox.setTooltip ("Rhythm templates, bass pattern, arp rate and swing feel");
        moodAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (a, "mood", moodBox);
        styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (a, "style", styleBox);

        // ---- shape card ----
        kComplexity.setup (*this, a, "complexity", "Complexity", shapeCol, true);
        kDensity.setup    (*this, a, "density",    "Density",    shapeCol, true);
        kSwing.setup      (*this, a, "swing",      "Swing",      shapeCol, true);
        kVariation.setup  (*this, a, "variation",  "Variation",  seedCol, true);
        kSeed.setup       (*this, a, "seed",       "Seed",       seedCol, true);
        kComplexity.slider.setTooltip ("Chord extensions, inversions and passing tones");
        kDensity.slider.setTooltip ("How many notes melody and arp play");
        kSwing.slider.setTooltip ("Delays every second 16th note");
        kVariation.slider.setTooltip ("Mutates melody, arp and bass from loop to loop. 0 = exact loop");
        kSeed.slider.setTooltip ("Changes the pattern of every key");
        auto percent = [] (juce::Slider& s, double scale)
        {
            s.textFromValueFunction = [scale] (double v) { return juce::String (juce::roundToInt (v * scale)) + "%"; };
            s.valueFromTextFunction = [scale] (const juce::String& t) { return t.getDoubleValue() / scale; };
            s.updateText();
        };
        percent (kComplexity.slider, 100.0); percent (kDensity.slider, 100.0);
        percent (kSwing.slider, 100.0);      percent (kVariation.slider, 100.0);

        // ---- link card ----
        linkButton.getProperties().set ("style", "labelled");
        linkButton.getProperties().set ("accent", linkCol.toString());
        linkButton.setTooltip ("Link all Seeder instances of the same group: shared settings, keys and seed");
        addAndMakeVisible (linkButton);
        linkAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (a, "link", linkButton);

        groupBox.addItemList ({ "GROUP 1", "GROUP 2", "GROUP 3", "GROUP 4" }, 1);
        groupBox.getProperties().set ("accent", linkCol.toString());
        groupBox.setTooltip ("Instances only talk to instances in the same group");
        addAndMakeVisible (groupBox);
        groupAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (a, "group", groupBox);

        roleSwitch = std::make_unique<ChoiceSwitch> (*a.getParameter ("role"), juce::StringArray { "ALL", "CHD", "MEL", "ARP", "BAS" }, linkCol);
        roleSwitch->setTooltip ("ALL: this instance plays all layers (on MIDI channels 1 to 4). Otherwise it plays only that layer, on MIDI channel 1.");
        addAndMakeVisible (*roleSwitch);

        // ---- layer rows ----
        for (int l = 0; l < seeder::kNumLayers; ++l)
        {
            auto& r = rows[(size_t) l];
            const auto col = slot (l);
            r.on.getProperties().set ("accent", col.toString());
            r.on.setTooltip ("ON / OFF: mute this layer");
            addAndMakeVisible (r.on);
            r.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (a, ParamIDs::layerOn (l), r.on);

            juce::StringArray chans;
            for (int c = 1; c <= 16; ++c) chans.add ("CH " + juce::String (c));
            r.out.addItemList (chans, 1);
            r.out.getProperties().set ("accent", col.toString());
            r.out.setTooltip ("MIDI channel of this layer (when the role is ALL)");
            addAndMakeVisible (r.out);
            r.outAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (a, ParamIDs::layerChannel (l), r.out);

            r.view = std::make_unique<PatternView> (proc, l);
            addAndMakeVisible (*r.view);

            r.drag = std::make_unique<DragHandle> ("DRAG MIDI", ".MID FILE", col, [this, l] { return exportFile (1 << l, layerTitle[l]); });
            r.drag->setTooltip (juce::String ("Drag the ") + juce::String (layerTitle[l]).toLowerCase() + " pattern into a track in your DAW");
            addAndMakeVisible (*r.drag);
        }

        setSize (W, H);   // after all children exist: this triggers resized()
        timerCallback();
        startTimerHz (30);
    }

    ~Panel() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) H, false));
        g.fillAll();

        // Header wie bei Repetitor: Logo, Titel 19 pt, "by Dani Koenig · Version"
        {
            const auto header = juce::Rectangle<int> (margin, 6, 400, 40);
            const float x0 = (float) header.getX() + 1.0f, base = (float) header.getY() + 30.0f;
            const float hs[] { 0.55f, 1.0f, 0.4f, 0.75f };
            for (int c = 0; c < seeder::kNumLayers; ++c)
            {
                g.setColour (slot (c));
                const float h = 20.0f * hs[c];
                g.fillRoundedRectangle (x0 + (float) c * 6.5f, base - h, 4.5f, h, 1.5f);
            }
            g.setColour (text);
            g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
            g.drawText (JucePlugin_IsSynth ? "SEEDER INSTRUMENT" : "SEEDER", header.getX() + 34, header.getY() + 4, 300, 22, juce::Justification::centredLeft);
            g.setColour (textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String::fromUTF8 ("by Dani Koenig  \xc2\xb7  v") + JucePlugin_VersionString,
                        header.getX() + 34, header.getY() + 24, 220, 14, juce::Justification::centredLeft);
        }

        drawCard (g, secTrigger, "TRIGGER", triggerCol);
        drawCard (g, secStyle,   "STYLE",   styleCol);
        drawCard (g, secShape,   "SHAPE",   shapeCol);
        drawCard (g, secLink,    "LINK",    linkCol);

        // ---- trigger card: root readout + 64-step position ----
        {
            const int root = proc.uiRootNote.load();
            auto box = juce::Rectangle<int> (secTrigger.getX() + 14, secTrigger.getY() + 34, secTrigger.getWidth() - 28, 48);
            g.setColour (inset);
            g.fillRoundedRectangle (box.toFloat(), 6.0f);
            smallLabel (g, "ROOT", box.reduced (10, 5).removeFromTop (12));
            g.setColour (root >= 0 ? triggerCol : textDim);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText (root >= 0 ? noteName (root) : juce::String ("-"), box.reduced (10, 0).withTrimmedTop (14).withWidth (70), juce::Justification::centredLeft);

            const int cur = root >= 0 ? proc.uiStep.load() : -1;
            const float x0 = (float) box.getX() + 84.0f, y0 = (float) box.getCentreY() + 2.0f;
            const float w = ((float) box.getWidth() - 84.0f - 12.0f - 3.0f * 5.0f) / 64.0f;
            for (int i = 0; i < seeder::kPatternSteps; ++i)
            {
                const float x = x0 + (float) i * w + (float) (i / 16) * 5.0f;
                g.setColour (i == cur ? triggerCol : (i / 4 % 2 == 0 ? textDim.withAlpha (0.45f) : textDim.withAlpha (0.25f)));
                g.fillRect (x, y0, juce::jmax (1.0f, w - 1.0f), 10.0f);
            }
            smallLabel (g, "BAR 1      BAR 2      BAR 3      BAR 4", juce::Rectangle<int> ((int) x0, box.getY() + 6, box.getWidth() - 96, 12));
        }
        smallLabel (g, "NOTE MODE", modeSwitch->getBounds().withX (secTrigger.getX() + 14).withWidth (70));
        smallLabel (g, "CHANGE AT", quantSwitch->getBounds().withX (secTrigger.getX() + 14).withWidth (70));
        smallLabel (g, "AFTER RELEASE", latchButton.getBounds().withX (secTrigger.getX() + 14).withWidth (80));

        // ---- style card ----
        smallLabel (g, "MOOD",  moodBox.getBounds().withY (moodBox.getY() - 14).withHeight (12));
        smallLabel (g, "STYLE", styleBox.getBounds().withY (styleBox.getY() - 14).withHeight (12));
        {
            auto info = juce::Rectangle<int> (secStyle.getX() + 14, styleBox.getBottom() + 12, secStyle.getWidth() - 28, 44);
            g.setColour (inset);
            g.fillRoundedRectangle (info.toFloat(), 6.0f);
            smallLabel (g, "IN USE", info.reduced (10, 4).removeFromTop (12));
            g.setColour (haveInfo ? text : textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (infoText, info.reduced (10, 0).withTrimmedTop (16), juce::Justification::centredLeft);
        }

        // ---- shape card: knobs are laid out in resized(); short hint underneath ----
        g.setColour (textDim);
        g.setFont (juce::FontOptions (9.5f));
        g.drawFittedText ("Complexity: chords and passing tones. Density: notes in melody and arp. "
                          "Variation: mutates the loops. Seed: changes every key.",
                          juce::Rectangle<int> (secShape.getX() + 14, secShape.getBottom() - 38, secShape.getWidth() - 28, 30),
                          juce::Justification::centredLeft, 2, 1.0f);

        // ---- link card ----
        smallLabel (g, "ROLE OF THIS INSTANCE", roleSwitch->getBounds().withY (roleSwitch->getY() - 14).withHeight (12));
        {
            const int linked = proc.uiLinked.load();
            const bool on = proc.apvts.getRawParameterValue ("link")->load() > 0.5f;
            auto st = juce::Rectangle<int> (secLink.getX() + 14, secLink.getBottom() - 64, secLink.getWidth() - 28, 50);
            g.setColour (inset);
            g.fillRoundedRectangle (st.toFloat(), 6.0f);
            const bool multi = on && linked > 1;
            g.setColour (multi ? linkCol : textDim.withAlpha (0.5f));
            g.fillEllipse ((float) st.getX() + 10.0f, (float) st.getY() + 11.0f, 8.0f, 8.0f);
            g.setColour (multi ? text : textDim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (! on ? "LINK OFF" : (linked > 1 ? juce::String (linked) + " INSTANCES LINKED" : "NO OTHER INSTANCE"),
                        st.getX() + 24, st.getY() + 4, st.getWidth() - 28, 20, juce::Justification::centredLeft);
            g.setColour (textDim);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText ("same seed, keys and settings", st.getX() + 24, st.getY() + 24, st.getWidth() - 28, 16, juce::Justification::centredLeft);
        }

        // ---- layer rows ----
        for (int l = 0; l < seeder::kNumLayers; ++l)
        {
            const auto col = slot (l);
            const auto r = rowArea (l);
            const bool on = proc.apvts.getRawParameterValue (ParamIDs::layerOn (l))->load() > 0.5f;
            drawCard (g, r, {}, col);
            g.setColour (on ? col : col.withAlpha (0.3f));
            g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 10.0f, 4.0f, (float) r.getHeight() - 20.0f, 2.0f);

            auto badge = juce::Rectangle<float> ((float) r.getX() + 24.0f, (float) r.getY() + 12.0f, 34.0f, 34.0f);
            g.setColour (col.withAlpha (on ? 0.22f : 0.1f));
            g.fillRoundedRectangle (badge, 7.0f);
            g.setColour (on ? col : col.withAlpha (0.5f));
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText (juce::String (l + 1), badge.toNearestInt(), juce::Justification::centred);

            g.setColour (on ? text : textDim);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (layerTitle[l], r.getX() + 100, r.getY() + 14, 110, 20, juce::Justification::centredLeft);
            smallLabel (g, layerHint[l], juce::Rectangle<int> (r.getX() + 100, r.getY() + 33, 110, 12));
            smallLabel (g, "MIDI OUT", juce::Rectangle<int> (r.getX() + 100, r.getY() + 50, 110, 12));
        }
    }

    void resized() override
    {
        // header
        newButton.setBounds (W - margin - 92, 9, 92, 30);
        resetButton.setBounds (W - margin - 92 * 2 - 8, 9, 92, 30);
        dragAll->setBounds (W - margin - 92 * 2 - 8 - 8 - 118, 9, 118, 30);

        // cards
        const int total = W - 2 * margin, gap = 8;
        const int wTrig = 290, wStyle = 236, wLink = 206;
        const int wShape = total - wTrig - wStyle - wLink - 3 * gap;
        int x = margin;
        secTrigger = { x, cardsY, wTrig, cardsH };   x += wTrig + gap;
        secStyle   = { x, cardsY, wStyle, cardsH };  x += wStyle + gap;
        secShape   = { x, cardsY, wShape, cardsH };  x += wShape + gap;
        secLink    = { x, cardsY, wLink, cardsH };

        // trigger
        const int tx = secTrigger.getX() + 14, tw = secTrigger.getWidth() - 28;
        modeSwitch->setBounds (tx + 76, secTrigger.getY() + 98, tw - 76, 26);
        quantSwitch->setBounds (tx + 76, secTrigger.getY() + 130, tw - 76, 26);
        latchButton.setBounds (tx + 90, secTrigger.getY() + 162, 120, 24);

        // style
        const int sx = secStyle.getX() + 14, sw = secStyle.getWidth() - 28;
        moodBox.setBounds (sx, secStyle.getY() + 50, sw, 28);
        styleBox.setBounds (sx, secStyle.getY() + 98, sw, 28);

        // shape: five knobs in a row
        Knob::layoutRow (juce::Rectangle<int> (secShape.getX() + 8, secShape.getY() + 44, secShape.getWidth() - 16, 108),
                         { &kComplexity, &kDensity, &kSwing, &kVariation, &kSeed }, 16);

        // link
        const int lx = secLink.getX() + 14, lw = secLink.getWidth() - 28;
        linkButton.setBounds (lx, secLink.getY() + 36, 86, 26);
        groupBox.setBounds (lx + 92, secLink.getY() + 36, lw - 92, 26);
        roleSwitch->setBounds (lx, secLink.getY() + 92, lw, 26);

        // rows
        for (int l = 0; l < seeder::kNumLayers; ++l)
        {
            auto& r = rows[(size_t) l];
            const auto a = rowArea (l);
            r.on.setBounds (a.getX() + 14, a.getY() + 54, 66, 24);
            r.out.setBounds (a.getX() + 100, a.getY() + 62, 98, 22);
            r.drag->setBounds (a.getRight() - 12 - 92, a.getY() + 10, 92, a.getHeight() - 20);
            r.view->setBounds (a.getX() + 222, a.getY() + 10, a.getWidth() - 222 - 12 - 92 - 10, a.getHeight() - 20);
        }
    }

private:
    juce::Rectangle<int> rowArea (int l) const
    {
        return { margin, rowsY + l * (rowH + rowGap), W - 2 * margin, rowH };
    }

    // writes the requested layers of the running pattern as a .mid file and returns it
    juce::File exportFile (int layerMask, const juce::String& what)
    {
        seeder::Pattern pat;
        if (! proc.getUiPattern (pat)) return {};
        const auto name = fileSafe (juce::String ("Seeder ") + what + " "
                                    + noteNames[proc.uiRootPc.load() % 12] + " "
                                    + seeder::moodName (proc.uiMood.load()) + " "
                                    + seeder::styleName (proc.uiStyle.load()));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("Seeder").getChildFile (name + ".mid");
        return seeder::writePatternMidi (pat, layerMask, proc.uiBpm.load(), file) ? file : juce::File();
    }

    void timerCallback() override
    {
        const uint32_t v = proc.uiPatternVersion.load();
        if (v != shownVersion)
        {
            shownVersion = v;
            seeder::Pattern pat;
            if (proc.getUiPattern (pat))
            {
                for (int l = 0; l < seeder::kNumLayers; ++l) rows[(size_t) l].view->setPattern (pat);
                infoText = noteNames[proc.uiRootPc.load() % 12] + " " + seeder::scaleName ((seeder::ScaleId) pat.scale)
                         + "   " + juce::String (pat.progression[0] + 1) + "-" + juce::String (pat.progression[1] + 1)
                         + "-" + juce::String (pat.progression[2] + 1) + "-" + juce::String (pat.progression[3] + 1);
                haveInfo = true;
            }
            const bool have = v > 0;
            dragAll->setAvailable (have);
            for (auto& r : rows) r.drag->setAvailable (have);
        }
        for (auto& r : rows) r.view->repaint();
        repaint (secTrigger.expanded (2));
        repaint (secLink.expanded (2));
        repaint (secStyle.expanded (2));
        for (int l = 0; l < seeder::kNumLayers; ++l) repaint (rowArea (l).removeFromLeft (220));
    }

    SeederProcessor& proc;
    SeederLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    juce::TextButton newButton { "NEW" }, resetButton { "INIT" };
    std::unique_ptr<DragHandle> dragAll;

    // trigger
    std::unique_ptr<ChoiceSwitch> modeSwitch, quantSwitch;
    juce::ToggleButton latchButton { "LATCH" };
    // style
    juce::ComboBox moodBox, styleBox;
    // shape
    Knob kComplexity, kDensity, kSwing, kVariation, kSeed;
    // link
    juce::ToggleButton linkButton { "LINK" };
    juce::ComboBox groupBox;
    std::unique_ptr<ChoiceSwitch> roleSwitch;

    struct Row
    {
        juce::ToggleButton on { "ON" };
        juce::ComboBox out;
        std::unique_ptr<PatternView> view;
        std::unique_ptr<DragHandle> drag;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outAtt;
    };
    std::array<Row, seeder::kNumLayers> rows;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAtt, linkAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> moodAtt, styleAtt, groupAtt;

    juce::Rectangle<int> secTrigger, secStyle, secShape, secLink;
    juce::String infoText { "play a note" };
    bool haveInfo = false;
    uint32_t shownVersion = 0xffffffffu;
};

SeederEditor::SeederEditor (SeederProcessor& p) : AudioProcessorEditor (&p)
{
    panel = std::make_unique<Panel> (p);
    addAndMakeVisible (*panel);

    setResizable (true, true);
    setResizeLimits (W * 6 / 10, H * 6 / 10, W * 2, H * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) W / (double) H);
    setSize (W, H);   // opens at 100 %, like the other plug-ins
}

SeederEditor::~SeederEditor() = default;

void SeederEditor::resized()
{
    const float s = (float) getWidth() / (float) W;
    panel->setBounds (0, 0, W, H);
    panel->setTransform (juce::AffineTransform::scale (s));
}

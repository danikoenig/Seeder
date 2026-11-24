#pragma once

#include "PluginProcessor.h"
#include "Colours.h"
#include "MidiExport.h"

inline const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline juce::String noteName (int midi) { return midi < 0 ? juce::String ("-") : noteNames[midi % 12] + juce::String (midi / 12 - 2); } // C3 = 60 (Bitwig / Ableton)

// Segment-Schalter (z.B. SEED | TRANSPOSE | RANDOM), an einen Choice-Parameter gebunden
class ChoiceSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceSwitch (juce::RangedAudioParameter& prm, juce::StringArray labelsIn, juce::Colour c)
        : labels (std::move (labelsIn)), colour (c),
          att (prm, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        att.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace SeedColours;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (inset);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (cardBorder);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const int n = labels.size();
        const float w = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
            const bool on = i == selected;
            if (on)
            {
                g.setColour (colour.withAlpha (0.9f));
                g.fillRoundedRectangle (seg, 4.0f);
            }
            else if (i == hover)
            {
                g.setColour (cardBorder);
                g.fillRoundedRectangle (seg, 4.0f);
            }
            g.setColour (on ? bgBottom : textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawFittedText (labels[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, labels.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) labels.size()));
        att.setValueAsCompleteGesture ((float) i);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = (int) ((float) e.x / (float) getWidth() * (float) labels.size());
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    juce::StringArray labels;
    juce::Colour colour;
    int selected = 0, hover = -1;
    juce::ParameterAttachment att;
};

// Mini piano roll of one layer over the whole phrase (4, 8 or 16 bars); the play position runs through it.
class PatternView : public juce::Component
{
public:
    PatternView (SeederProcessor& p, int layerIndex) : proc (p), layer (layerIndex) {}

    void setPattern (const seeder::Pattern& pat)
    {
        events.clear();
        steps = pat.steps();
        lo = 127; hi = 0;
        for (int i = 0; i < pat.count; ++i)
        {
            const auto& e = pat.events[(size_t) i];
            if (e.layer != layer) continue;
            events.push_back (e);
            lo = juce::jmin (lo, (int) e.note);
            hi = juce::jmax (hi, (int) e.note);
        }
        repaint();
    }
    void clear() { events.clear(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace SeedColours;
        const auto col = slot (layer);
        const bool on = proc.apvts.getRawParameterValue (ParamIDs::layerOn (layer))->load() > 0.5f;
        auto r = getLocalBounds().toFloat();
        g.setColour (inset);
        g.fillRoundedRectangle (r, 6.0f);
        auto inner = r.reduced (6.0f, 6.0f);
        const float sw = inner.getWidth() / (float) steps;

        for (int b = 0; b < steps; b += 4)
        {
            const bool bar = b % 16 == 0;
            g.setColour (bar ? textDim.withAlpha (0.35f) : cardBorder.withAlpha (0.8f));
            g.fillRect (inner.getX() + (float) b * sw, inner.getY(), bar ? 1.4f : 1.0f, inner.getHeight());
        }

        const int span = juce::jmax (1, hi - lo);
        for (const auto& e : events)
        {
            const float h = events.empty() ? 0.5f : (hi == lo ? 0.5f : (float) (e.note - lo) / (float) span);
            const float noteH = 5.0f;
            const float y = inner.getBottom() - noteH - h * (inner.getHeight() - noteH);
            auto bar = juce::Rectangle<float> (inner.getX() + (float) e.step * sw, y, juce::jmax (2.0f, (float) e.length * sw - 1.0f), noteH);
            const float vel = 0.45f + 0.55f * (float) e.velocity / 127.0f;
            g.setColour (col.withAlpha ((on ? 0.95f : 0.28f) * vel));
            g.fillRoundedRectangle (bar, 1.5f);
        }

        const int pos = proc.uiRootNote.load() >= 0 ? proc.uiStep.load() : -1;
        if (pos >= 0 && ! events.empty())
        {
            g.setColour (text.withAlpha (on ? 0.07f : 0.03f));
            g.fillRect (inner.getX() + (float) pos * sw, inner.getY(), sw, inner.getHeight());
            g.setColour (text.withAlpha (on ? 0.85f : 0.3f));
            g.fillRect (inner.getX() + (float) pos * sw, inner.getY(), 2.0f, inner.getHeight());
        }
        else if (events.empty())
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("play a note", getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    SeederProcessor& proc;
    int layer;
    std::vector<seeder::NoteEvent> events;
    int lo = 127, hi = 0;
    int steps = 8 * seeder::kStepsPerBar;
};

// Drag handle: dragging it out of the plug-in drops a .mid file into the DAW
class DragHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragHandle (juce::String title, juce::String sub, juce::Colour c, std::function<juce::File()> maker)
        : titleText (std::move (title)), subText (std::move (sub)), colour (c), makeFile (std::move (maker)) {}

    void setAvailable (bool a) { if (a != available) { available = a; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        using namespace SeedColours;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool hot = available && (isMouseOver() || dragging);
        g.setColour (hot ? colour.withAlpha (0.16f) : inset);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (available ? (hot ? colour : colour.withAlpha (0.55f)) : cardBorder);
        g.drawRoundedRectangle (r, 8.0f, 1.2f);

        const bool compact = getHeight() < 50;
        auto ic = compact ? juce::Rectangle<float> (12.0f, 15.0f).withCentre ({ r.getX() + 17.0f, r.getCentreY() })
                          : juce::Rectangle<float> (14.0f, 17.0f).withCentre ({ r.getCentreX(), r.getCentreY() - 12.0f });
        g.setColour (available ? text : textDim.withAlpha (0.5f));
        g.drawRoundedRectangle (ic, 2.0f, 1.3f);
        g.drawLine (ic.getX() + 3.0f, ic.getY() + 6.0f, ic.getRight() - 3.0f, ic.getY() + 6.0f, 1.2f);
        g.drawLine (ic.getX() + 3.0f, ic.getY() + 10.0f, ic.getRight() - 3.0f, ic.getY() + 10.0f, 1.2f);

        g.setColour (available ? text : textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        if (compact)
        {
            g.drawText (titleText, r.withTrimmedLeft (30.0f), juce::Justification::centred);
            return;
        }
        g.drawText (titleText, r.withTrimmedTop (r.getCentreY() + 2.0f).withHeight (14.0f), juce::Justification::centred);
        g.setColour (textDim);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (subText, r.withTrimmedTop (r.getCentreY() + 16.0f), juce::Justification::centredTop);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! available || dragging || e.getDistanceFromDragStart() < 6) return;
        const juce::File f = makeFile();
        if (! f.existsAsFile()) return;
        dragging = true;
        repaint();
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; repaint(); });
    }

private:
    juce::String titleText, subText;
    juce::Colour colour;
    std::function<juce::File()> makeFile;
    bool available = false, dragging = false;
};

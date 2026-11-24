#pragma once
#include <juce_graphics/juce_graphics.h>

namespace SeedColours
{
    const juce::Colour bgTop         { 0xff17181c };
    const juce::Colour bgBottom      { 0xff0f1013 };
    const juce::Colour card          { 0xff1d1f24 };
    const juce::Colour cardBorder    { 0xff2b2e35 };
    const juce::Colour inset         { 0xff121317 };
    const juce::Colour accent        { 0xffff7a2f };   // Orange (wie One Shooter)
    const juce::Colour accent2       { 0xffffb347 };   // Amber
    const juce::Colour clip          { 0xffff4d5e };   // Clipper = Rot
    const juce::Colour delayCol      { 0xff4dd0e1 };   // Delay = Tuerkis
    const juce::Colour reverbCol     { 0xff9fa8da };   // Reverb = Lavendel
    const juce::Colour env           { 0xffffd54f };   // Amp Envelope = Gelb
    const juce::Colour text          { 0xffeceef2 };
    const juce::Colour textDim       { 0xff7d828c };
    const juce::Colour knobTrack     { 0xff2c2f36 };
    const juce::Colour knobBody      { 0xff26282e };

    // Layer-Farben: Chords, Stabs, Pad, Bass, Melody, Counter, Arp
    inline juce::Colour slot (int i)
    {
        switch (i)
        {
            case 0:  return juce::Colour (0xffff7a2f);   // Chords  Orange
            case 1:  return juce::Colour (0xffffd54f);   // Stabs   Gelb
            case 2:  return juce::Colour (0xff9fa8da);   // Pad     Lavendel
            case 3:  return juce::Colour (0xffb388ff);   // Bass    Violett
            case 4:  return juce::Colour (0xff4fc3f7);   // Melody  Blau
            case 5:  return juce::Colour (0xfff48fb1);   // Counter Rosa
            default: return juce::Colour (0xff69f0ae);   // Arp     Gruen
        }
    }

    inline juce::String slotLetter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }
}

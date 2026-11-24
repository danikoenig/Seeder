#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using namespace seeder;

namespace
{
const char* layerOnId[kNumLayers]      { "onChords", "onMelody", "onArp", "onBass" };
const char* layerChannelId[kNumLayers] { "chChords", "chMelody", "chArp", "chBass" };
}

juce::AudioProcessorValueTreeState::ParameterLayout SeederProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    StringArray moods, styles;
    for (int i = 0; i < kNumMoods; ++i)  moods.add (moodName (i));
    for (int i = 0; i < kNumStyles; ++i) styles.add (styleName (i));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mood", 1 },  "Mood",  moods, (int) Mood::Melancholic));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style", styles, (int) Style::House));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 },  "Note Mode",
                 StringArray { "Seed per Note", "Transpose", "Random" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quant", 1 }, "Change at",
                 StringArray { "Next 16th", "Next Beat", "Next Bar" }, 2));

    auto pct = NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "complexity", 1 }, "Complexity", pct, 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "density", 1 },    "Density",    pct, 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 },      "Swing",      NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "variation", 1 },  "Variation",  pct, 0.0f));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { "seed", 1 },       "Seed", 0, 999, 0));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { "latch", 1 },      "Latch", true));

    const char* names[kNumLayers] { "Chords", "Melody", "Arp", "Bass" };
    for (int i = 0; i < kNumLayers; ++i)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { layerOnId[i], 1 }, String (names[i]) + " On", true));
        p.push_back (std::make_unique<AudioParameterInt>  (ParameterID { layerChannelId[i], 1 }, String (names[i]) + " Channel", 1, 16, i + 1));
    }
    return { p.begin(), p.end() };
}

SeederProcessor::SeederProcessor()
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                      ),
      apvts (*this, nullptr, "SEEDER", createLayout())
{
    randomSource = Rng ((uint64_t) juce::Time::currentTimeMillis());
}

bool SeederProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
#endif
}

void SeederProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    queueCount = 0;
    activeNote = pendingNote = -1;
    held.fill (false);
    heldCount = 0;
    internalPpq = 0.0;
    sampleCounter = 0;
    hostWasPlaying = false;
    wasIdleStart = false;
    uiRootNote = -1;
}

void SeederProcessor::schedule (int64_t sample, int channel, int note, int vel, bool on)
{
    if (queueCount >= kMaxScheduled) return;
    queue[(size_t) queueCount++] = { sample, (uint8_t) channel, (uint8_t) note, (uint8_t) vel, on };
}

void SeederProcessor::flushAll (juce::MidiBuffer& out, int offsetInBlock)
{
    for (int i = 0; i < queueCount; ++i)
    {
        const auto& s = queue[(size_t) i];
        if (! s.on)
            out.addEvent (juce::MidiMessage::noteOff ((int) s.channel, (int) s.note), offsetInBlock);
    }
    queueCount = 0;
}

void SeederProcessor::startPattern (int note, int64_t stepNumber)
{
    GenParams p;
    p.mood       = (Mood)  (int) param ("mood");
    p.style      = (Style) (int) param ("style");
    p.complexity = param ("complexity");
    p.density    = param ("density");
    p.rootPc     = note % 12;

    const int userSeed = (int) param ("seed");
    switch ((int) param ("mode"))
    {
        case 0:  p.seed = seedFromNote (note, userSeed, p.mood, p.style); break;   // every key = its own pattern
        case 1:  p.seed = seedFromNote (60,   userSeed, p.mood, p.style); break;   // same pattern, transposed
        default: p.seed = randomSource.next(); break;                              // new every time
    }

    generate (p, basePattern);
    playPattern = basePattern;
    genParams = p;
    patternStart = stepNumber;
    loopsDone = 0;
}

void SeederProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    juce::MidiBuffer out;
    const int64_t blockStart = sampleCounter;
    const bool latch = param ("latch") > 0.5f;

    // ---- transport -------------------------------------------------------------
    double bpm = 120.0, hostPpq = 0.0;
    bool hostPlaying = false, havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; havePpq = true; }
        }
    if (bpm < 20.0) bpm = 120.0;
    const bool useHost = hostPlaying && havePpq;

    if (useHost != hostWasPlaying)
    {
        flushAll (out, 0);
        const int resume = activeNote >= 0 ? activeNote : pendingNote;
        activeNote = pendingNote = -1;
        if (resume >= 0 && (heldCount > 0 || (useHost && latch)))
        {
            pendingNote = resume;
            if (! useHost) { internalPpq = 0.0; wasIdleStart = true; }
        }
        hostWasPlaying = useHost;
    }

    // ---- incoming MIDI: trigger notes -----------------------------------------
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);

        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (! held[(size_t) note]) { held[(size_t) note] = true; ++heldCount; }
            if (activeNote < 0 && pendingNote < 0 && ! useHost) { internalPpq = 0.0; wasIdleStart = true; }
            pendingNote = note;
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            if (held[(size_t) note]) { held[(size_t) note] = false; --heldCount; }
            if (! latch && heldCount == 0)
            {
                flushAll (out, pos);
                activeNote = pendingNote = -1;
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            flushAll (out, pos);
            held.fill (false); heldCount = 0;
            activeNote = pendingNote = -1;
        }
        else
        {
            out.addEvent (m, pos);   // pass CC / pitch bend etc. through
        }
    }

    // ---- step clock --------------------------------------------------------------
    const double spStep = sampleRate * 60.0 / (bpm * 4.0);          // samples per 16th
    const double s0 = (useHost ? hostPpq : internalPpq) * 4.0;
    const double s1 = s0 + (double) n / spStep;
    const int quant = (int) param ("quant");
    const float swing = param ("swing");
    const float variation = param ("variation");

    auto emitDue = [&] (int64_t limit)
    {
        int w = 0;
        for (int i = 0; i < queueCount; ++i)
        {
            const Scheduled s = queue[(size_t) i];
            if (s.sample < limit)
            {
                const int pos = (int) juce::jlimit<int64_t> (0, juce::jmax (0, n - 1), s.sample - blockStart);
                out.addEvent (s.on ? juce::MidiMessage::noteOn ((int) s.channel, (int) s.note, (juce::uint8) s.velocity)
                                   : juce::MidiMessage::noteOff ((int) s.channel, (int) s.note), pos);
            }
            else
                queue[(size_t) w++] = s;
        }
        queueCount = w;
    };

    if (activeNote >= 0 || pendingNote >= 0)
    {
        for (int64_t step = (int64_t) std::ceil (s0 - 1e-6); (double) step < s1; ++step)
        {
            const int offset = juce::jlimit (0, juce::jmax (0, n - 1), (int) std::lround (((double) step - s0) * spStep));
            emitDue (blockStart + offset);

            if (pendingNote >= 0)
            {
                const bool boundary = wasIdleStart || quant == 0
                                   || (quant == 1 && step % 4 == 0)
                                   || (quant == 2 && step % 16 == 0);
                if (boundary)
                {
                    flushAll (out, offset);
                    startPattern (pendingNote, step);
                    activeNote = pendingNote;
                    pendingNote = -1;
                    wasIdleStart = false;
                }
            }

            if (activeNote < 0) continue;

            int64_t rel = step - patternStart;
            if (rel < 0) { patternStart = step; rel = 0; loopsDone = 0; playPattern = basePattern; }
            const int ps = (int) (rel % kPatternSteps);
            const int64_t loop = rel / kPatternSteps;
            if (ps == 0 && loop != loopsDone)
            {
                applyVariation (basePattern, playPattern, genParams, variation, (int) loop);
                loopsDone = loop;
            }
            uiStep.store (ps);

            for (int i = 0; i < playPattern.count; ++i)
            {
                const NoteEvent& e = playPattern.events[(size_t) i];
                if (e.step != ps || param (layerOnId[e.layer]) < 0.5f) continue;

                const int ch = (int) param (layerChannelId[e.layer]);
                const double startDelay = (step & 1) ? (double) swing * spStep : 0.0;
                const double endDelay   = ((step + e.length) & 1) ? (double) swing * spStep : 0.0;
                const int64_t onAt  = blockStart + offset + (int64_t) startDelay;
                int64_t offAt = blockStart + offset + (int64_t) ((double) e.length * spStep + endDelay) - 2;
                if (offAt <= onAt) offAt = onAt + 1;

                schedule (onAt,  ch, e.note, e.velocity, true);
                schedule (offAt, ch, e.note, 0, false);
            }
        }
    }

    emitDue (blockStart + n);

    uiRootNote.store (activeNote >= 0 ? activeNote : (pendingNote >= 0 ? pendingNote : -1));
    if (! useHost) internalPpq += (double) n * bpm / (60.0 * sampleRate);
    sampleCounter += n;
    midi.swapWith (out);
}

juce::AudioProcessorEditor* SeederProcessor::createEditor() { return new SeederEditor (*this); }

void SeederProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SeederProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeederProcessor(); }

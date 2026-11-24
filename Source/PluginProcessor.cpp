#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace seeder;

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
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::layerOn (i), 1 }, String (names[i]) + " On", true));
        StringArray chans;
        for (int c = 1; c <= 16; ++c) chans.add ("CH " + String (c));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::layerChannel (i), 1 }, String (names[i]) + " Channel", chans, i));
    }

    // Instance link: with a role, an instance plays only that layer (on channel 1)
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "role", 1 }, "Role",
                 StringArray { "All", "Chords", "Melody", "Arp", "Bass" }, 0));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "link", 1 }, "Link", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "group", 1 }, "Link Group",
                 StringArray { "GROUP 1", "GROUP 2", "GROUP 3", "GROUP 4" }, 0));
    return { p.begin(), p.end() };
}

SeederProcessor::SeederProcessor() : SeederProcessor (LinkHub::defaultFile()) {}

SeederProcessor::SeederProcessor (const juce::File& linkFile)
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                      ),
      apvts (*this, nullptr, "SEEDER", createLayout()),
      link (linkFile)
{
    init();
}

void SeederProcessor::init()
{
    randomSource = Rng ((uint64_t) juce::Time::currentTimeMillis() ^ (uint64_t) (uintptr_t) this);
    do { myId = (uint32_t) randomSource.next(); } while (myId == 0);
    startTimerHz (20);
}

SeederProcessor::~SeederProcessor() { stopTimer(); }

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

// ---------------------------------------------------------------------------------
// UI actions
void SeederProcessor::resetToInit()
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());
}

void SeederProcessor::newSeed()
{
    if (auto* s = apvts.getParameter ("seed"))
        s->setValueNotifyingHost (s->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt (1000)));
    regenRequested = true;
}

bool SeederProcessor::getUiPattern (Pattern& out) const
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t v = uiPatternVersion.load();
        if (v == 0) return false;
        GenParams p;
        p.seed = uiSeed.load();
        p.rootPc = uiRootPc.load();
        p.mood = (Mood) uiMood.load();
        p.style = (Style) uiStyle.load();
        p.complexity = (float) uiComplexity.load() / 1000.0f;
        p.density = (float) uiDensity.load() / 1000.0f;
        if (uiPatternVersion.load() != v) continue;
        generate (p, out);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------
// Link (message thread)
void SeederProcessor::readShared (int32_t* v) const
{
    v[0] = (int32_t) param ("mood");
    v[1] = (int32_t) param ("style");
    v[2] = (int32_t) param ("mode");
    v[3] = (int32_t) param ("quant");
    v[4] = (int32_t) std::lround (param ("complexity") * 1000.0f);
    v[5] = (int32_t) std::lround (param ("density") * 1000.0f);
    v[6] = (int32_t) std::lround (param ("swing") * 1000.0f);
    v[7] = (int32_t) std::lround (param ("variation") * 1000.0f);
    v[8] = (int32_t) param ("seed");
    v[9] = param ("latch") > 0.5f ? 1 : 0;
}

void SeederProcessor::applyShared (const int32_t* v)
{
    auto set = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set ("mood", (float) v[0]);
    set ("style", (float) v[1]);
    set ("mode", (float) v[2]);
    set ("quant", (float) v[3]);
    set ("complexity", (float) v[4] / 1000.0f);
    set ("density", (float) v[5] / 1000.0f);
    set ("swing", (float) v[6] / 1000.0f);
    set ("variation", (float) v[7] / 1000.0f);
    set ("seed", (float) v[8]);
    set ("latch", (float) v[9]);
}

void SeederProcessor::pollLink()
{
    if (! link.isOpen()) { uiLinked = 0; return; }
    if (param ("link") < 0.5f) { joinedGroup = -1; uiLinked = 0; return; }

    const int g = group();
    link.beat (g, myId);

    int32_t cur[kLinkParams];
    readShared (cur);

    if (joinedGroup.load() != g)   // just joined (link switched on, or group changed)
    {
        lastTrigSeq = link.triggerSeq (g);
        lastParamSeq = link.paramSeq (g);
        if (link.othersAlive (g, myId) && link.hasParams (g))
        {
            int32_t v[kLinkParams];
            uint32_t seq = 0;
            if (link.readParams (g, myId, seq, v)) applyShared (v);   // adopt the group's settings
            readShared (lastPublished);
        }
        else
        {
            std::copy (cur, cur + kLinkParams, lastPublished);
            link.publishParams (g, myId, cur);
            lastParamSeq = link.paramSeq (g);
        }
        joinedGroup = g;
    }
    else if (! std::equal (cur, cur + kLinkParams, lastPublished))   // changed here -> publish
    {
        std::copy (cur, cur + kLinkParams, lastPublished);
        link.publishParams (g, myId, cur);
        lastParamSeq = link.paramSeq (g);
    }
    else
    {
        int32_t v[kLinkParams];
        if (link.readParams (g, myId, lastParamSeq, v))              // changed elsewhere -> adopt
        {
            applyShared (v);
            readShared (lastPublished);
        }
    }
    uiLinked = link.countAlive (g, myId, true);
}

// ---------------------------------------------------------------------------------
// Scheduler / pattern (audio thread)
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

void SeederProcessor::fireTrigger (int note, bool broadcast)
{
    TriggerSnapshot s;
    s.note = note;
    s.mood = (int) param ("mood");
    s.style = (int) param ("style");
    s.complexity1000 = (int) std::lround (param ("complexity") * 1000.0f);
    s.density1000 = (int) std::lround (param ("density") * 1000.0f);

    const int userSeed = (int) param ("seed");
    switch ((int) param ("mode"))
    {
        case 0:  s.seed = seedFromNote (note, userSeed, (Mood) s.mood, (Style) s.style); break;   // every key = its own pattern
        case 1:  s.seed = seedFromNote (60,   userSeed, (Mood) s.mood, (Style) s.style); break;   // same pattern, transposed
        default: s.seed = randomSource.next(); break;                                             // new every time
    }

    pendingSnap = s;
    pendingNote = note;

    if (broadcast && linkOn() && joinedGroup.load() == group())
    {
        LinkTrigger t;
        t.note = s.note; t.seed = s.seed; t.mood = s.mood; t.style = s.style;
        t.complexity = s.complexity1000; t.density = s.density1000;
        link.publishTrigger (group(), myId, t);
    }
}

void SeederProcessor::startPattern (const TriggerSnapshot& s, int64_t stepNumber)
{
    GenParams p;
    p.mood       = (Mood) s.mood;
    p.style      = (Style) s.style;
    p.complexity = (float) s.complexity1000 / 1000.0f;
    p.density    = (float) s.density1000 / 1000.0f;
    p.rootPc     = s.note % 12;
    p.seed       = s.seed;

    generate (p, basePattern);
    playPattern = basePattern;
    genParams = p;
    activeSnap = s;
    patternStart = stepNumber;
    loopsDone = 0;

    uiSeed = p.seed;
    uiRootPc = p.rootPc;
    uiMood = s.mood; uiStyle = s.style;
    uiComplexity = s.complexity1000; uiDensity = s.density1000;
    uiPatternVersion.fetch_add (1);
}

void SeederProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    juce::MidiBuffer out;
    const int64_t blockStart = sampleCounter;
    const bool latch = param ("latch") > 0.5f;
    const int  role = (int) param ("role");           // 0 = all layers, 1..4 = one layer
    const bool linked = linkOn() && joinedGroup.load() == group();

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
    uiBpm = bpm;
    const bool useHost = hostPlaying && havePpq;

    if (useHost != hostWasPlaying)
    {
        flushAll (out, 0);
        const int resume = activeNote >= 0 ? activeNote : pendingNote;
        const TriggerSnapshot resumeSnap = activeNote >= 0 ? activeSnap : pendingSnap;
        activeNote = pendingNote = -1;
        if (resume >= 0 && (heldCount > 0 || (useHost && latch)))
        {
            pendingNote = resume;
            pendingSnap = resumeSnap;
            if (! useHost) { internalPpq = 0.0; wasIdleStart = true; }
        }
        hostWasPlaying = useHost;
    }

    // ---- NEW button --------------------------------------------------------------
    if (regenRequested.exchange (false) && activeNote >= 0)
        fireTrigger (activeNote, true);

    // ---- triggers from other linked instances -----------------------------------
    if (linked)
    {
        LinkTrigger t;
        uint32_t seq = lastTrigSeq.load();
        if (link.readTrigger (group(), myId, seq, t))
        {
            if (t.note < 0)
            {
                flushAll (out, 0);
                activeNote = pendingNote = -1;
            }
            else
            {
                pendingSnap.note = t.note; pendingSnap.seed = t.seed;
                pendingSnap.mood = t.mood; pendingSnap.style = t.style;
                pendingSnap.complexity1000 = t.complexity; pendingSnap.density1000 = t.density;
                pendingNote = t.note;
                if (activeNote < 0 && ! useHost) { internalPpq = 0.0; wasIdleStart = true; }
            }
        }
        lastTrigSeq.store (seq);
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
            fireTrigger (note, true);
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            if (held[(size_t) note]) { held[(size_t) note] = false; --heldCount; }
            if (! latch && heldCount == 0)
            {
                flushAll (out, pos);
                activeNote = pendingNote = -1;
                if (linked)
                {
                    LinkTrigger stop;   // note = -1
                    link.publishTrigger (group(), myId, stop);
                }
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
                    const bool identical = activeNote >= 0 && pendingSnap.samePattern (activeSnap);
                    if (! identical)    // same key + same seed: keep running, so echoes from linked instances don't restart
                    {
                        flushAll (out, offset);
                        startPattern (pendingSnap, step);
                    }
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
                if (e.step != ps || param (ParamIDs::layerOn (e.layer)) < 0.5f) continue;
                if (role != 0 && role - 1 != (int) e.layer) continue;   // this instance plays another layer

                const int ch = role == 0 ? (int) param (ParamIDs::layerChannel (e.layer)) + 1 : 1;
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

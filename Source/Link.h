#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>

// Instance link: several Seeder instances (also in different processes, e.g. Bitwig
// plug-in sandboxes) share one small memory-mapped file. It carries the shared
// parameters, the trigger (root note + seed + mood/style snapshot) and a heartbeat per
// instance. Patterns are deterministic, so every instance generates the same pattern and
// plays only its own role.
namespace seeder
{
constexpr int kLinkGroups = 4;
constexpr int kLinkParams = 10;   // mood, style, mode, quant, complexity, density, swing, variation, seed, latch
constexpr int kLinkSlots  = 8;

struct LinkTrigger
{
    int32_t  note = -1;           // -1 = stop
    uint64_t seed = 0;
    int32_t  mood = 0, style = 0;
    int32_t  complexity = 0, density = 0;   // x1000
};

struct LinkGroup
{
    std::atomic<uint32_t> paramSeq, paramAuthor;
    std::atomic<int32_t>  params[kLinkParams];

    std::atomic<uint32_t> trigSeq, trigAuthor;     // trigSeq: odd while being written
    std::atomic<int32_t>  tNote, tMood, tStyle, tComplexity, tDensity;
    std::atomic<uint32_t> tSeedLo, tSeedHi;

    std::atomic<uint32_t> hbId[kLinkSlots], hbTime[kLinkSlots];
};

struct LinkFile
{
    std::atomic<uint32_t> magic;
    LinkGroup groups[kLinkGroups];
};

static_assert (std::atomic<uint32_t>::is_always_lock_free && std::atomic<int32_t>::is_always_lock_free,
               "link needs lock-free atomics");

class LinkHub
{
public:
    explicit LinkHub (const juce::File& file = defaultFile());

    bool isOpen() const { return data != nullptr; }
    static juce::File defaultFile();

    // heartbeat / presence
    void beat (int group, uint32_t myId);
    int  countAlive (int group, uint32_t myId, bool includeSelf) const;
    bool othersAlive (int group, uint32_t myId) const { return countAlive (group, myId, false) > 0; }

    // shared parameters
    bool hasParams (int group) const;
    void publishParams (int group, uint32_t myId, const int32_t* values);
    // returns true if a newer set from someone else is available
    bool readParams (int group, uint32_t myId, uint32_t& lastSeq, int32_t* values) const;
    uint32_t paramSeq (int group) const;

    // trigger
    void publishTrigger (int group, uint32_t myId, const LinkTrigger& t);
    // true = new trigger from another instance (lastSeq is updated either way)
    bool readTrigger (int group, uint32_t myId, uint32_t& lastSeq, LinkTrigger& out) const;
    uint32_t triggerSeq (int group) const;

private:
    LinkGroup* grp (int g) const;
    std::unique_ptr<juce::MemoryMappedFile> map;
    LinkFile* data = nullptr;
};
} // namespace seeder

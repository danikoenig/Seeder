#include "Link.h"

namespace seeder
{
namespace { constexpr uint32_t kMagic = 0x5EED0002u; }

juce::File LinkHub::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("seeder-link-v2.bin");
}

LinkHub::LinkHub (const juce::File& file)
{
    const auto need = (juce::int64) sizeof (LinkFile);
    if (! file.existsAsFile() || file.getSize() < need)
    {
        file.getParentDirectory().createDirectory();
        juce::FileOutputStream os (file);
        if (! os.openedOk()) return;
        os.setPosition (0);
        os.truncate();
        const juce::MemoryBlock zeros ((size_t) need, true);
        os.write (zeros.getData(), zeros.getSize());
        os.flush();
    }

    map = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite, false);
    if (map->getData() == nullptr || map->getSize() < (size_t) need) { map.reset(); return; }

    data = static_cast<LinkFile*> (map->getData());
    uint32_t expected = 0;
    data->magic.compare_exchange_strong (expected, kMagic);   // zero -> magic, otherwise leave as is
    if (data->magic.load() != kMagic) { data = nullptr; map.reset(); }
}

LinkGroup* LinkHub::grp (int g) const
{
    if (data == nullptr) return nullptr;
    return &data->groups[juce::jlimit (0, kLinkGroups - 1, g)];
}

static uint32_t nowMs() { return (uint32_t) (juce::Time::currentTimeMillis() & 0xffffffffLL); }

void LinkHub::beat (int group, uint32_t myId)
{
    auto* g = grp (group);
    if (g == nullptr) return;
    const uint32_t now = nowMs();
    int free = -1, mine = -1;
    for (int i = 0; i < kLinkSlots; ++i)
    {
        if (g->hbId[i].load() == myId) { mine = i; break; }
        if (free < 0 && (g->hbId[i].load() == 0 || (uint32_t) (now - g->hbTime[i].load()) > 3000u)) free = i;
    }
    const int slot = mine >= 0 ? mine : free;
    if (slot < 0) return;
    g->hbId[slot].store (myId);
    g->hbTime[slot].store (now);
}

int LinkHub::countAlive (int group, uint32_t myId, bool includeSelf) const
{
    auto* g = grp (group);
    if (g == nullptr) return 0;
    const uint32_t now = nowMs();
    int n = 0;
    for (int i = 0; i < kLinkSlots; ++i)
    {
        const uint32_t id = g->hbId[i].load();
        if (id == 0 || (uint32_t) (now - g->hbTime[i].load()) > 3000u) continue;
        if (id == myId && ! includeSelf) continue;
        ++n;
    }
    return n;
}

bool LinkHub::hasParams (int group) const
{
    auto* g = grp (group);
    return g != nullptr && g->paramSeq.load() > 0;
}

uint32_t LinkHub::paramSeq (int group) const
{
    auto* g = grp (group);
    return g ? g->paramSeq.load() : 0;
}

void LinkHub::publishParams (int group, uint32_t myId, const int32_t* values)
{
    auto* g = grp (group);
    if (g == nullptr) return;
    for (int i = 0; i < kLinkParams; ++i) g->params[i].store (values[i]);
    g->paramAuthor.store (myId);
    g->paramSeq.fetch_add (1);
}

bool LinkHub::readParams (int group, uint32_t myId, uint32_t& lastSeq, int32_t* values) const
{
    auto* g = grp (group);
    if (g == nullptr) return false;
    const uint32_t s = g->paramSeq.load();
    if (s == lastSeq) return false;
    lastSeq = s;
    if (g->paramAuthor.load() == myId) return false;
    for (int i = 0; i < kLinkParams; ++i) values[i] = g->params[i].load();
    return true;
}

uint32_t LinkHub::triggerSeq (int group) const
{
    auto* g = grp (group);
    return g ? g->trigSeq.load() : 0;
}

void LinkHub::publishTrigger (int group, uint32_t myId, const LinkTrigger& t)
{
    auto* g = grp (group);
    if (g == nullptr) return;
    const uint32_t v = g->trigSeq.load() & ~1u;
    g->trigSeq.store (v + 1);                      // odd: writing
    g->tNote.store (t.note);
    g->tMood.store (t.mood);
    g->tStyle.store (t.style);
    g->tComplexity.store (t.complexity);
    g->tDensity.store (t.density);
    g->tOptions.store (t.options);
    g->tSeedLo.store ((uint32_t) (t.seed & 0xffffffffULL));
    g->tSeedHi.store ((uint32_t) (t.seed >> 32));
    g->trigAuthor.store (myId);
    g->trigSeq.store (v + 2);                      // even: done
}

bool LinkHub::readTrigger (int group, uint32_t myId, uint32_t& lastSeq, LinkTrigger& out) const
{
    auto* g = grp (group);
    if (g == nullptr) return false;
    const uint32_t s1 = g->trigSeq.load();
    if (s1 == lastSeq || (s1 & 1u)) return false;  // nothing new, or still being written
    LinkTrigger t;
    t.note       = g->tNote.load();
    t.mood       = g->tMood.load();
    t.style      = g->tStyle.load();
    t.complexity = g->tComplexity.load();
    t.density    = g->tDensity.load();
    t.options    = g->tOptions.load();
    t.seed       = ((uint64_t) g->tSeedHi.load() << 32) | (uint64_t) g->tSeedLo.load();
    const uint32_t author = g->trigAuthor.load();
    if (g->trigSeq.load() != s1) return false;     // torn read, try again next block
    lastSeq = s1;
    if (author == myId) return false;
    out = t;
    return true;
}
} // namespace seeder

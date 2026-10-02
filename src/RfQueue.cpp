/*
 * mod-playerbot-raid-finder — RfQueue.cpp
 */

#include "RfQueue.h"

#include <algorithm>

namespace RaidFinder
{
    bool RfQueue::Add(RfQueueEntry const& entry)
    {
        return _entries.emplace(entry.guid, entry).second;
    }

    bool RfQueue::Remove(ObjectGuid guid)
    {
        return _entries.erase(guid) > 0;
    }

    RfQueueEntry const* RfQueue::Find(ObjectGuid guid) const
    {
        auto const it = _entries.find(guid);
        return it == _entries.end() ? nullptr : &it->second;
    }

    RfSessionId RfQueue::SessionOf(ObjectGuid guid) const
    {
        RfQueueEntry const* e = Find(guid);
        return e ? e->sessionId : 0;
    }

    void RfQueue::SetSession(ObjectGuid guid, RfSessionId session)
    {
        auto const it = _entries.find(guid);
        if (it != _entries.end())
            it->second.sessionId = session;
    }

    std::vector<RfQueueEntry> RfQueue::Entries() const
    {
        std::vector<RfQueueEntry> out;
        out.reserve(_entries.size());
        for (auto const& [guid, e] : _entries)
            out.push_back(e);
        std::sort(out.begin(), out.end(),
                  [](RfQueueEntry const& a, RfQueueEntry const& b) { return a.joinedAtMs < b.joinedAtMs; });
        return out;
    }
}

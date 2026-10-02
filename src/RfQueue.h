/*
 * mod-playerbot-raid-finder — RfQueue.h
 *
 * Real-player queue index: which real player is queued for what, and in which
 * session. A real player is in at most one queue / session at a time. Bots are
 * never in this index (they live in RfReservationManager).
 */

#ifndef MOD_RF_QUEUE_H
#define MOD_RF_QUEUE_H

#include "RfReservationManager.h"

#include "ObjectGuid.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace RaidFinder
{
    struct RfQueueEntry
    {
        ObjectGuid guid;
        std::string raidId;
        std::uint8_t role = 0;
        std::uint32_t joinedAtMs = 0;
        RfSessionId sessionId = 0;
    };

    class RfQueue
    {
    public:
        bool Add(RfQueueEntry const& entry);              // false if already present
        bool Remove(ObjectGuid guid);
        RfQueueEntry const* Find(ObjectGuid guid) const;
        RfSessionId SessionOf(ObjectGuid guid) const;
        void SetSession(ObjectGuid guid, RfSessionId session);
        std::vector<RfQueueEntry> Entries() const;
        std::size_t Size() const { return _entries.size(); }
        void Clear() { _entries.clear(); }

    private:
        std::unordered_map<ObjectGuid, RfQueueEntry> _entries;
    };
}

#endif

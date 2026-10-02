/*
 * mod-playerbot-raid-finder — RfReservationManager.h
 *
 * Bot reservation layer: one bot can belong to at most one active Raid Finder
 * session. Keys are raw 64-bit GUIDs (ObjectGuid::GetRawValue()) so this class
 * stays engine-independent and unit-testable.
 *
 * Every operation is idempotent: releasing an unknown or already released bot
 * is a no-op, and ReleaseSession() can be called any number of times.
 */

#ifndef MOD_RF_RESERVATION_MANAGER_H
#define MOD_RF_RESERVATION_MANAGER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace RaidFinder
{
    using RfSessionId = std::uint32_t;
    using RfRawGuid = std::uint64_t;

    class RfReservationManager
    {
    public:
        // True when reserved now, or already reserved by the same session.
        // False when another session holds the bot.
        bool Reserve(RfRawGuid bot, RfSessionId session);

        // Releases only if `session` holds the bot. Returns true if released.
        bool Release(RfRawGuid bot, RfSessionId session);

        // Releases every bot held by `session`. Returns the count released.
        std::size_t ReleaseSession(RfSessionId session);

        bool IsReserved(RfRawGuid bot) const;
        RfSessionId OwnerOf(RfRawGuid bot) const;   // 0 when free
        std::vector<RfRawGuid> BotsOf(RfSessionId session) const;
        std::size_t Count() const { return _owner.size(); }
        void Clear() { _owner.clear(); }

    private:
        std::unordered_map<RfRawGuid, RfSessionId> _owner;
    };
}

#endif

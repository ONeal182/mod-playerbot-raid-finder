/*
 * mod-playerbot-raid-finder — RfReservationManager.cpp
 */

#include "RfReservationManager.h"

namespace RaidFinder
{
    bool RfReservationManager::Reserve(RfRawGuid bot, RfSessionId session)
    {
        if (!bot || !session)
            return false;
        auto const [it, inserted] = _owner.emplace(bot, session);
        return inserted || it->second == session;
    }

    bool RfReservationManager::Release(RfRawGuid bot, RfSessionId session)
    {
        auto const it = _owner.find(bot);
        if (it == _owner.end() || it->second != session)
            return false;
        _owner.erase(it);
        return true;
    }

    std::size_t RfReservationManager::ReleaseSession(RfSessionId session)
    {
        std::size_t released = 0;
        for (auto it = _owner.begin(); it != _owner.end();)
        {
            if (it->second == session)
            {
                it = _owner.erase(it);
                ++released;
            }
            else
                ++it;
        }
        return released;
    }

    bool RfReservationManager::IsReserved(RfRawGuid bot) const
    {
        return _owner.find(bot) != _owner.end();
    }

    RfSessionId RfReservationManager::OwnerOf(RfRawGuid bot) const
    {
        auto const it = _owner.find(bot);
        return it == _owner.end() ? 0 : it->second;
    }

    std::vector<RfRawGuid> RfReservationManager::BotsOf(RfSessionId session) const
    {
        std::vector<RfRawGuid> out;
        for (auto const& [bot, owner] : _owner)
            if (owner == session)
                out.push_back(bot);
        return out;
    }
}

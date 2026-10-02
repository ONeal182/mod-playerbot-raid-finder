/*
 * mod-playerbot-raid-finder — RfSession.cpp
 */

#include "RfSession.h"

#include "StringFormat.h"

#include <algorithm>

namespace RaidFinder
{
    char const* StateName(RfState state)
    {
        switch (state)
        {
            case RfState::Searching:              return "Searching";
            case RfState::FillingBots:            return "FillingBots";
            case RfState::Proposal:               return "Proposal";
            case RfState::Assembling:             return "Assembling";
            case RfState::TeleportLeader:         return "TeleportLeader";
            case RfState::WaitingLeaderMapChange: return "WaitingLeaderMapChange";
            case RfState::TeleportBots:           return "TeleportBots";
            case RfState::InRaid:                 return "InRaid";
            case RfState::Releasing:              return "Releasing";
            case RfState::Done:                   return "Done";
        }
        return "?";
    }

    char const* StateProtocolId(RfState state)
    {
        switch (state)
        {
            case RfState::Searching:              return "SEARCHING";
            case RfState::FillingBots:            return "FILLING";
            case RfState::Proposal:               return "PROPOSAL";
            case RfState::Assembling:             return "ASSEMBLING";
            case RfState::TeleportLeader:
            case RfState::WaitingLeaderMapChange:
            case RfState::TeleportBots:           return "TELEPORTING";
            case RfState::InRaid:                 return "INRAID";
            default:                              return "IDLE";
        }
    }

    void RfSession::SetState(RfState state)
    {
        _state = state;
        _stateMs = 0;
    }

    void RfSession::Advance(std::uint32_t diff)
    {
        _stateMs += diff;
        _totalMs += diff;
        if (_state == RfState::Searching || _state == RfState::FillingBots)
            _searchMs += diff;
    }

    RfMember* RfSession::FindMember(ObjectGuid guid)
    {
        for (RfMember& m : _members)
            if (m.guid == guid)
                return &m;
        return nullptr;
    }

    RfMember const* RfSession::FindMember(ObjectGuid guid) const
    {
        for (RfMember const& m : _members)
            if (m.guid == guid)
                return &m;
        return nullptr;
    }

    bool RfSession::RemoveMember(ObjectGuid guid)
    {
        auto const it = std::find_if(_members.begin(), _members.end(),
                                     [guid](RfMember const& m) { return m.guid == guid; });
        if (it == _members.end())
            return false;
        _members.erase(it);
        return true;
    }

    RfRoleCounts RfSession::FilledRoles(bool realOnly) const
    {
        RfRoleCounts c;
        for (RfMember const& m : _members)
            if (!realOnly || !m.isBot)
                c.Add(m.role, 1);
        return c;
    }

    RfRoleCounts RfSession::OpenSlots() const
    {
        return RfCompositionPlanner::OpenSlots(_raid.composition, FilledRoles());
    }

    std::uint32_t RfSession::RealCount() const
    {
        return static_cast<std::uint32_t>(std::count_if(_members.begin(), _members.end(),
                                                        [](RfMember const& m) { return !m.isBot; }));
    }

    std::uint32_t RfSession::BotCount() const
    {
        return static_cast<std::uint32_t>(_members.size()) - RealCount();
    }

    bool RfSession::AllRealAccepted() const
    {
        for (RfMember const& m : _members)
            if (!m.isBot && !m.accepted)
                return false;
        return true;
    }

    std::uint32_t RfSession::AcceptedCount() const
    {
        std::uint32_t n = 0;
        for (RfMember const& m : _members)
            if (!m.isBot && m.accepted)
                ++n;
        return n;
    }

    std::string RfSession::Summary() const
    {
        RfRoleCounts const f = FilledRoles();
        RfRoleCounts const& r = _raid.composition;
        return Acore::StringFormat("#{} {} [{}] T {}/{} H {}/{} D {}/{} real {} bots {} age {}s", _id, _raid.id,
                                   StateName(_state), f.tank, r.tank, f.healer, r.healer, f.dps, r.dps, RealCount(),
                                   BotCount(), _totalMs / 1000);
    }
}

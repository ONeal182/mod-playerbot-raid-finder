/*
 * mod-playerbot-raid-finder — RfSession.h
 *
 * One Raid Finder session: a raid being formed, run or released. Holds only
 * GUIDs (never Player* or Group*): those objects can disappear between ticks,
 * so every use looks them up again.
 */

#ifndef MOD_RF_SESSION_H
#define MOD_RF_SESSION_H

#include "RfCompositionPlanner.h"
#include "RfRaidRegistry.h"
#include "RfReservationManager.h"

#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <string>
#include <vector>

namespace RaidFinder
{
    enum class RfState : std::uint8_t
    {
        Searching = 0,          // real players gather, waiting for BotFillDelay
        FillingBots,            // selecting + reserving bots
        Proposal,               // real players accept/decline
        Assembling,             // revalidate + create raid group
        TeleportLeader,         // send real leader first
        WaitingLeaderMapChange, // wait for the leader to be inside the instance
        TeleportBots,           // batched teleport of other members
        InRaid,                 // running
        Releasing,              // single central cleanup point
        Done                    // erased on next tick
    };

    char const* StateName(RfState state);
    char const* StateProtocolId(RfState state);

    struct RfMember
    {
        ObjectGuid guid;
        std::string name;
        bool isBot = false;
        std::uint8_t role = RF_ROLE_NONE;  // assigned single role
        std::uint32_t joinedAtMs = 0;      // real: queue join time (getMSTime)

        // Real players.
        bool accepted = false;

        // Teleport tracking (bots and non-leader real players).
        bool teleportIssued = false;
        std::uint32_t teleportIssuedAtMs = 0;
        bool arrived = false;
        bool failed = false;               // could not enter; awaiting removal

        // Bot details (for summaries).
        std::uint8_t classId = 0;
        std::uint32_t gearScore = 0;
        bool ranged = false;
    };

    class RfSession
    {
    public:
        RfSession(RfSessionId id, RfRaidDefinition const& raid) : _id(id), _raid(raid) { }

        RfSessionId Id() const { return _id; }
        RfRaidDefinition const& Raid() const { return _raid; }

        RfState State() const { return _state; }
        void SetState(RfState state);
        std::uint32_t StateMs() const { return _stateMs; }
        std::uint32_t SearchMs() const { return _searchMs; }
        std::uint32_t TotalMs() const { return _totalMs; }
        void ResetSearchTimer() { _searchMs = 0; }
        void Advance(std::uint32_t diff);

        ObjectGuid LeaderGuid() const { return _leader; }
        void SetLeader(ObjectGuid guid) { _leader = guid; }
        TeamId Team() const { return _team; }
        void SetTeam(TeamId team) { _team = team; }

        ObjectGuid GroupGuid() const { return _group; }
        void SetGroupGuid(ObjectGuid guid) { _group = guid; }

        std::uint32_t LeaderInstanceId() const { return _leaderInstanceId; }
        void SetLeaderInstanceId(std::uint32_t id) { _leaderInstanceId = id; }

        bool IsForced() const { return _forced; }
        void SetForced(bool forced) { _forced = forced; }

        bool IsOpenForJoin() const { return _state == RfState::Searching || _state == RfState::FillingBots; }
        bool IsPreRaid() const { return _state < RfState::InRaid; }
        bool HasGroupPhase() const { return _state >= RfState::Assembling && _state <= RfState::InRaid; }

        // Members.
        std::vector<RfMember>& Members() { return _members; }
        std::vector<RfMember> const& Members() const { return _members; }
        RfMember* FindMember(ObjectGuid guid);
        RfMember const* FindMember(ObjectGuid guid) const;
        void AddMember(RfMember member) { _members.push_back(std::move(member)); }
        bool RemoveMember(ObjectGuid guid);

        RfRoleCounts FilledRoles(bool realOnly = false) const;
        RfRoleCounts OpenSlots() const;
        std::uint32_t RealCount() const;
        std::uint32_t BotCount() const;
        bool AllRealAccepted() const;
        std::uint32_t AcceptedCount() const;
        bool IsComplete() const { return OpenSlots().Total() == 0; }

        // Bot replacement budget during teleport.
        std::uint32_t replacementsUsed = 0;
        std::uint32_t abandonMs = 0;
        std::uint32_t teleportTickMs = 0;
        std::uint32_t fillRetryMs = 0;
        std::uint32_t statusPushMs = 0;

        // Release bookkeeping.
        bool releaseStarted = false;
        std::string releaseReason;
        bool requeueOthers = false;
        RfState stateBeforeRelease = RfState::Searching;

        std::string Summary() const;

    private:
        RfSessionId _id;
        RfRaidDefinition _raid;   // copy: survives registry reloads
        RfState _state = RfState::Searching;
        std::uint32_t _stateMs = 0;
        std::uint32_t _searchMs = 0;
        std::uint32_t _totalMs = 0;
        ObjectGuid _leader;
        TeamId _team = TEAM_NEUTRAL;
        ObjectGuid _group;
        std::uint32_t _leaderInstanceId = 0;
        bool _forced = false;
        std::vector<RfMember> _members;
    };
}

#endif

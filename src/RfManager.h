/*
 * mod-playerbot-raid-finder — RfManager.h
 *
 * Orchestrates queue, sessions, bot fill, proposal, raid assembly, teleport and
 * release. UI layers (commands, gossip, addon) only call the public methods and
 * render RfStatusView; no UI layer selects bots, creates groups or teleports.
 *
 * Threading: hooks may fire from map-update threads (bot AI can leave a group
 * there), so hooks only push events into a mutex-protected queue. All session
 * work runs on the world thread in Update().
 */

#ifndef MOD_RF_MANAGER_H
#define MOD_RF_MANAGER_H

#include "RfBotSelector.h"
#include "RfQueue.h"
#include "RfReservationManager.h"
#include "RfSession.h"

#include "ObjectGuid.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class Player;

namespace RaidFinder
{
    enum class RfError : std::uint8_t
    {
        None = 0,
        Disabled,
        UnknownRaid,
        RaidDisabled,
        InvalidRole,
        AlreadyQueued,
        InGroup,
        LevelTooLow,
        GearTooLow,
        InBattleground,
        InLfg,
        InInstance,
        Lockout,
        Dead,
        NotQueued,
        NoProposal,
        AlreadyAccepted,
        NotAllowed,
        Internal
    };

    char const* ErrorProtocolId(RfError error);
    char const* ErrorText(RfError error);

    struct RfStatusMember
    {
        std::string name;
        bool accepted = false;
        bool isBot = false;
        std::uint8_t role = 0;
    };

    struct RfStatusView
    {
        bool active = false;
        RfSessionId sessionId = 0;
        RfState state = RfState::Done;
        std::string raidId;
        std::string raidName;
        bool experimental = false;
        std::uint8_t myRole = 0;
        bool isLeader = false;
        std::uint32_t queueSec = 0;
        RfRoleCounts filled;
        RfRoleCounts required;
        std::uint32_t realCount = 0;
        std::uint32_t botCount = 0;
        std::uint32_t botsRequired = 0;
        std::uint32_t proposalSecLeft = 0;
        std::uint32_t acceptedCount = 0;
        bool meAccepted = false;
        std::vector<RfStatusMember> realMembers;
    };

    enum class RfEventType : std::uint8_t
    {
        PlayerLogout,
        MemberRemoved,
        GroupDisbanded,
        MapChanged,
        LeaderChanged
    };

    struct RfEvent
    {
        RfEventType type;
        ObjectGuid player;
        ObjectGuid group;
    };

    class RfManager
    {
    public:
        static RfManager& Instance();

        void Update(std::uint32_t diff);

        // Player API (commands, gossip, addon all go through these).
        RfError JoinQueue(Player* player, std::string const& raidId, std::uint8_t role);
        RfError LeaveQueue(Player* player);
        RfError Accept(Player* player);
        RfError Decline(Player* player);
        RfStatusView GetStatus(Player* player) const;

        // GM API.
        bool Force(Player* gm, std::string const& raidId, std::uint8_t role, std::vector<std::string>& report);
        bool ReleaseSession(RfSessionId id, std::string const& reason);
        std::size_t ClearAll(std::string const& reason);
        // Shutdown: drop module state only, no teleports or group operations.
        void ShutdownClear();
        RfPoolStats PoolStats(Player* viewer, RfRaidDefinition const& raid, std::vector<RfBotEvaluation>* evals);

        std::vector<RfSession const*> Sessions() const;
        RfSession const* FindSession(RfSessionId id) const;
        RfQueue const& Queue() const { return _queue; }
        RfReservationManager const& Reservations() const { return _reservations; }

        // Hook entry points (any thread).
        void PushEvent(RfEvent const& ev);

        // Addon presence (players that sent at least one addon message).
        void MarkAddonUser(ObjectGuid guid) { _addonUsers.insert(guid); }
        bool IsAddonUser(ObjectGuid guid) const { return _addonUsers.count(guid) > 0; }
        void ForgetAddonUser(ObjectGuid guid) { _addonUsers.erase(guid); }

        // Pushes the current status to one player (addon + optional chat).
        void PushStatus(Player* player) const;

    private:
        RfManager() = default;

        RfSession* FindSessionMutable(RfSessionId id);
        RfSession* SessionOfPlayer(ObjectGuid guid);
        RfSession* SessionOfGroup(ObjectGuid group);
        RfSession* SessionOfBot(ObjectGuid bot);

        RfError JoinInternal(Player* player, std::string const& raidId, std::uint8_t role, std::uint32_t joinedAtMs);
        RfError ValidateRealPlayer(Player* player, RfRaidDefinition const& raid) const;
        RfSession* PickSessionForJoin(Player* player, RfRaidDefinition const& raid, std::uint8_t role);
        RfSession& CreateSession(RfRaidDefinition const& raid, Player* leader);
        void AddRealMember(RfSession& session, Player* player, std::uint8_t role, std::uint32_t joinedAtMs);

        // State machine.
        void TickSession(RfSession& s, std::uint32_t diff);
        void TickSearching(RfSession& s);
        void TickFillingBots(RfSession& s);
        void TickProposal(RfSession& s);
        void TickAssembling(RfSession& s);
        void TickTeleportLeader(RfSession& s);
        void TickWaitingLeader(RfSession& s);
        void TickTeleportBots(RfSession& s, std::uint32_t diff);
        void TickInRaid(RfSession& s, std::uint32_t diff);

        void EnterState(RfSession& s, RfState state);
        void EnterProposal(RfSession& s);
        void BackToSearching(RfSession& s, char const* why);

        // Bot fill: plans and reserves bots for the open slots. Returns true
        // when the composition is complete afterwards.
        bool FillBots(RfSession& s, bool forGroupPhase);
        RfSelectionContext MakeContext(RfSession const& s) const;
        bool IsCompositionComplete(RfSession const& s) const;

        // Removes a bot from the session (+ group, + reservation).
        void DropBot(RfSession& s, ObjectGuid bot, char const* reason, bool teleportOut);
        // Removes a real player from the session (+ queue, + group if asked).
        void DropReal(RfSession& s, ObjectGuid guid, char const* reason, bool removeFromGroup, bool notify);

        // Central idempotent cleanup.
        void Release(RfSession& s, std::string const& reason, bool requeueOthers);

        void ProcessEvents();
        void HandleEvent(RfEvent const& ev);
        void PushStatusToSession(RfSession const& s) const;
        void NotifySession(RfSession const& s, std::string const& text) const;

        std::map<RfSessionId, std::unique_ptr<RfSession>> _sessions;
        RfQueue _queue;
        RfReservationManager _reservations;
        RfSessionId _nextId = 1;
        std::uint32_t _tickAccumulator = 0;

        std::mutex _eventLock;
        std::vector<RfEvent> _events;
        std::set<ObjectGuid> _addonUsers;

        struct Requeue
        {
            ObjectGuid guid;
            std::string raidId;
            std::uint8_t role;
            std::uint32_t joinedAtMs;
        };
        std::vector<Requeue> _pendingRequeue;

        // Players to move out of a raid map once their teleport completes
        // (a far teleport takes them out of the world for a moment, so an
        // immediate TeleportOut would miss them).
        struct Evict
        {
            ObjectGuid guid;
            std::uint32_t mapId;
            std::uint32_t ageMs;
        };
        std::vector<Evict> _pendingEvict;
        void ScheduleEvict(ObjectGuid guid, std::uint32_t mapId);
        void ProcessEvicts(std::uint32_t diff);
    };
}

#define sRfManager RaidFinder::RfManager::Instance()

#endif

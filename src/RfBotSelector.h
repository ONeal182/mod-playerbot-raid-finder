/*
 * mod-playerbot-raid-finder — RfBotSelector.h
 *
 * Evaluates online Random Playerbots against a raid definition. The checks run
 * in a fixed order and the first failing check is the reject reason, so the
 * diagnostic funnel (.rf bots) shows exactly where the pool shrinks.
 */

#ifndef MOD_RF_BOT_SELECTOR_H
#define MOD_RF_BOT_SELECTOR_H

#include "RfCompositionPlanner.h"
#include "RfRaidRegistry.h"
#include "RfReservationManager.h"

#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <array>
#include <string>
#include <vector>

class Player;

namespace RaidFinder
{
    enum class RfBotReject : std::uint8_t
    {
        None = 0,
        Offline,
        NotRandomBot,
        WrongLevel,
        WrongFaction,
        AlreadyGrouped,
        InBattleground,
        InArena,
        InLfg,
        InInstance,
        Busy,
        ReservedByRaidFinder,
        RaidLockoutMismatch,
        WrongRole,
        GearTooLow,
        Other,
        Count
    };

    char const* RejectName(RfBotReject reason);

    struct RfBotEvaluation
    {
        ObjectGuid guid;
        std::string name;
        bool eligible = false;
        RfBotReject rejectReason = RfBotReject::Other;
        std::uint8_t roles = 0;       // RfRole mask (by spec)
        bool ranged = false;
        std::uint32_t gearScore = 0;
        std::uint8_t classId = 0;
    };

    struct RfSelectionContext
    {
        RfRaidDefinition raid;
        TeamId team = TEAM_NEUTRAL;   // TEAM_NEUTRAL = any faction
        ObjectGuid leaderGuid;        // used for lockout compatibility
        RfSessionId sessionId = 0;    // bots reserved by this session pass
        std::uint8_t roleFilter = RF_ROLE_TANK | RF_ROLE_HEALER | RF_ROLE_DPS;
        bool strictBusy = true;       // false at final validation: combat is tolerated
    };

    struct RfPoolStats
    {
        std::uint32_t total = 0;
        std::uint32_t online = 0;
        std::uint32_t levelEligible = 0;
        std::uint32_t factionEligible = 0;
        std::uint32_t free = 0;
        std::uint32_t lockoutCompatible = 0;
        std::uint32_t roleEligible = 0;
        std::uint32_t gearEligible = 0;
        std::uint32_t tanks = 0;
        std::uint32_t healers = 0;
        std::uint32_t dps = 0;
        std::uint32_t ranged = 0;
        std::array<std::uint32_t, static_cast<std::size_t>(RfBotReject::Count)> rejects{};
    };

    class RfBotSelector
    {
    public:
        // Evaluates one player as raid-bot candidate.
        static RfBotEvaluation Evaluate(Player* bot, RfSelectionContext const& ctx,
                                        RfReservationManager const& reservations);

        // Evaluates the whole online random-bot pool. Fills `stats` when given.
        static std::vector<RfBotEvaluation> EvaluatePool(RfSelectionContext const& ctx,
                                                         RfReservationManager const& reservations,
                                                         RfPoolStats* stats);

        // Lockout compatibility of any player with the leader's instance.
        static bool IsLockoutCompatible(ObjectGuid player, ObjectGuid leader, std::uint32_t mapId,
                                        std::uint8_t difficulty);

        // Role mask of any player (real or bot) by talent spec.
        static std::uint8_t RolesBySpec(Player* player);
        static std::uint32_t GearScore(Player* player);   // average item level
        static bool IsRandomBot(Player* player);
    };
}

#endif

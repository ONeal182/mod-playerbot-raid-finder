/*
 * mod-playerbot-raid-finder — RfCompositionPlanner.h
 *
 * Pure, engine-independent role assignment. No Player, Group, ObjectGuid or
 * PlayerbotAI here: the planner works on plain DTOs so it is unit-testable
 * without a running world.
 *
 * Problem: given the required composition (tanks/healers/dps), the roles that
 * real players already fill, and a pool of candidates each able to play one or
 * more roles, choose candidates for the remaining slots.
 *
 * Algorithm: bipartite matching (Kuhn's augmenting paths) between candidates
 * and open role slots. Candidates are tried least-flexible first (one-role
 * candidates before hybrids), then by preference score. Kuhn's algorithm never
 * un-matches a candidate once matched (an augmenting path may only change its
 * role), so the result is a maximum matching that keeps every earlier
 * (less flexible / better) candidate it can. A naive Tank->Heal->DPS greedy can
 * waste the only healer-capable hybrid on a tank slot; this cannot.
 */

#ifndef MOD_RF_COMPOSITION_PLANNER_H
#define MOD_RF_COMPOSITION_PLANNER_H

#include <cstdint>
#include <string>
#include <vector>

namespace RaidFinder
{
    // Bit mask, same values as playerbots' BotRoles so a conversion is trivial.
    enum RfRole : std::uint8_t
    {
        RF_ROLE_NONE   = 0x00,
        RF_ROLE_TANK   = 0x01,
        RF_ROLE_HEALER = 0x02,
        RF_ROLE_DPS    = 0x04,
    };

    char const* RoleName(std::uint8_t singleRole);     // "Tank" / "Healer" / "DPS" (logs, GM)
    char const* RoleLocalName(std::uint8_t singleRole); // player-facing (Russian)
    char const* RoleProtocolId(std::uint8_t singleRole); // "TANK" / "HEAL" / "DPS"
    std::uint8_t ParseRole(std::string const& text);   // tank/heal/healer/dps (any case), 0 if invalid
    std::string RoleMaskText(std::uint8_t mask);        // "tank+dps"
    int RoleCount(std::uint8_t mask);

    struct RfRoleCounts
    {
        std::uint32_t tank = 0;
        std::uint32_t healer = 0;
        std::uint32_t dps = 0;

        std::uint32_t Total() const { return tank + healer + dps; }
        std::uint32_t Get(std::uint8_t role) const;
        void Add(std::uint8_t role, std::int32_t delta);
        bool operator==(RfRoleCounts const& o) const
        {
            return tank == o.tank && healer == o.healer && dps == o.dps;
        }
    };

    struct RfCandidate
    {
        std::uint32_t id = 0;        // opaque caller id (index into caller's list)
        std::uint8_t roles = 0;      // RfRole mask the candidate can play
        bool ranged = false;         // ranged damage dealer (dps balance)
        std::uint32_t score = 0;     // higher = preferred (gear score)
    };

    struct RfAssignment
    {
        std::uint32_t id = 0;
        std::uint8_t role = 0;       // single RfRole
    };

    struct RfPlanInput
    {
        RfRoleCounts required;       // full raid composition
        RfRoleCounts filled;         // already filled by real players
        std::vector<RfCandidate> candidates;
        std::uint32_t maxSelected = 0xFFFFFFFF; // cap on chosen candidates (MaxBotsPerRaid)
        bool balanceRanged = true;   // alternate ranged/melee among equal dps candidates
        bool requireRoleBalance = true; // false: fill leftover slots with any candidate
    };

    struct RfPlanResult
    {
        std::vector<RfAssignment> selected;
        RfRoleCounts assigned;       // roles assigned to selected candidates
        RfRoleCounts missing;        // still empty after planning
        bool complete = false;       // no missing slot

        std::uint8_t RoleOf(std::uint32_t id) const;
    };

    class RfCompositionPlanner
    {
    public:
        static RfPlanResult Plan(RfPlanInput const& input);

        // Open slots = required - filled, clamped at zero per role.
        static RfRoleCounts OpenSlots(RfRoleCounts const& required, RfRoleCounts const& filled);
    };
}

#endif

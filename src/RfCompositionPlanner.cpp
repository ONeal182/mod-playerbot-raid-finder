/*
 * mod-playerbot-raid-finder — RfCompositionPlanner.cpp
 */

#include "RfCompositionPlanner.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace RaidFinder
{
    char const* RoleName(std::uint8_t singleRole)
    {
        switch (singleRole)
        {
            case RF_ROLE_TANK:   return "Tank";
            case RF_ROLE_HEALER: return "Healer";
            case RF_ROLE_DPS:    return "DPS";
            default:             return "None";
        }
    }

    char const* RoleLocalName(std::uint8_t singleRole)
    {
        switch (singleRole)
        {
            case RF_ROLE_TANK:   return "Танк";
            case RF_ROLE_HEALER: return "Лекарь";
            case RF_ROLE_DPS:    return "Урон";
            default:             return "Нет";
        }
    }

    char const* RoleProtocolId(std::uint8_t singleRole)
    {
        switch (singleRole)
        {
            case RF_ROLE_TANK:   return "TANK";
            case RF_ROLE_HEALER: return "HEAL";
            case RF_ROLE_DPS:    return "DPS";
            default:             return "NONE";
        }
    }

    std::uint8_t ParseRole(std::string const& text)
    {
        std::string t;
        t.reserve(text.size());
        for (char c : text)
            t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (t == "tank" || t == "t")
            return RF_ROLE_TANK;
        if (t == "heal" || t == "healer" || t == "h")
            return RF_ROLE_HEALER;
        if (t == "dps" || t == "damage" || t == "d")
            return RF_ROLE_DPS;
        return RF_ROLE_NONE;
    }

    std::string RoleMaskText(std::uint8_t mask)
    {
        std::string out;
        if (mask & RF_ROLE_TANK)
            out += "tank";
        if (mask & RF_ROLE_HEALER)
            out += out.empty() ? "heal" : "+heal";
        if (mask & RF_ROLE_DPS)
            out += out.empty() ? "dps" : "+dps";
        return out.empty() ? "none" : out;
    }

    int RoleCount(std::uint8_t mask)
    {
        return ((mask & RF_ROLE_TANK) ? 1 : 0) + ((mask & RF_ROLE_HEALER) ? 1 : 0) +
               ((mask & RF_ROLE_DPS) ? 1 : 0);
    }

    std::uint32_t RfRoleCounts::Get(std::uint8_t role) const
    {
        switch (role)
        {
            case RF_ROLE_TANK:   return tank;
            case RF_ROLE_HEALER: return healer;
            case RF_ROLE_DPS:    return dps;
            default:             return 0;
        }
    }

    void RfRoleCounts::Add(std::uint8_t role, std::int32_t delta)
    {
        std::uint32_t* v = nullptr;
        switch (role)
        {
            case RF_ROLE_TANK:   v = &tank; break;
            case RF_ROLE_HEALER: v = &healer; break;
            case RF_ROLE_DPS:    v = &dps; break;
            default:             return;
        }
        std::int64_t const next = static_cast<std::int64_t>(*v) + delta;
        *v = next < 0 ? 0 : static_cast<std::uint32_t>(next);
    }

    std::uint8_t RfPlanResult::RoleOf(std::uint32_t id) const
    {
        for (RfAssignment const& a : selected)
            if (a.id == id)
                return a.role;
        return RF_ROLE_NONE;
    }

    RfRoleCounts RfCompositionPlanner::OpenSlots(RfRoleCounts const& required, RfRoleCounts const& filled)
    {
        RfRoleCounts open;
        open.tank = required.tank > filled.tank ? required.tank - filled.tank : 0;
        open.healer = required.healer > filled.healer ? required.healer - filled.healer : 0;
        open.dps = required.dps > filled.dps ? required.dps - filled.dps : 0;
        return open;
    }

    namespace
    {
        // Kuhn augmenting path from candidate `c` over open slots.
        bool TryAugment(std::size_t c, std::vector<RfCandidate> const& order,
                        std::vector<std::uint8_t> const& slotRole, std::vector<int>& slotOwner,
                        std::vector<char>& visited)
        {
            for (std::size_t s = 0; s < slotRole.size(); ++s)
            {
                if (!(order[c].roles & slotRole[s]) || visited[s])
                    continue;
                visited[s] = 1;
                if (slotOwner[s] < 0 ||
                    TryAugment(static_cast<std::size_t>(slotOwner[s]), order, slotRole, slotOwner, visited))
                {
                    slotOwner[s] = static_cast<int>(c);
                    return true;
                }
            }
            return false;
        }

        // Orders candidates: least flexible first, then by score (desc), with an
        // optional ranged/melee interleave inside each equal-mask group. Each
        // mask group is capped at `cap` entries: members of one group are
        // interchangeable for matching, so more than `cap` can never help.
        std::vector<RfCandidate> OrderCandidates(std::vector<RfCandidate> const& in, bool balanceRanged,
                                                 std::size_t cap)
        {
            std::map<std::uint8_t, std::vector<RfCandidate>> groups;
            for (RfCandidate const& c : in)
            {
                std::uint8_t const mask = c.roles & (RF_ROLE_TANK | RF_ROLE_HEALER | RF_ROLE_DPS);
                if (mask)
                {
                    RfCandidate copy = c;
                    copy.roles = mask;
                    groups[mask].push_back(copy);
                }
            }

            std::vector<std::uint8_t> masks;
            for (auto const& [mask, list] : groups)
                masks.push_back(mask);
            std::stable_sort(masks.begin(), masks.end(), [](std::uint8_t a, std::uint8_t b)
            {
                return RoleCount(a) < RoleCount(b);
            });

            std::vector<RfCandidate> out;
            for (std::uint8_t mask : masks)
            {
                std::vector<RfCandidate>& list = groups[mask];
                std::stable_sort(list.begin(), list.end(), [](RfCandidate const& a, RfCandidate const& b)
                {
                    if (a.score != b.score)
                        return a.score > b.score;
                    return a.id < b.id;
                });

                if (balanceRanged && (mask & RF_ROLE_DPS))
                {
                    std::vector<RfCandidate> ranged, melee;
                    for (RfCandidate const& c : list)
                        (c.ranged ? ranged : melee).push_back(c);
                    std::vector<RfCandidate> mixed;
                    std::size_t r = 0, m = 0;
                    bool takeRanged = ranged.size() >= melee.size();
                    while (r < ranged.size() || m < melee.size())
                    {
                        if ((takeRanged && r < ranged.size()) || m >= melee.size())
                            mixed.push_back(ranged[r++]);
                        else
                            mixed.push_back(melee[m++]);
                        takeRanged = !takeRanged;
                    }
                    list.swap(mixed);
                }

                if (list.size() > cap)
                    list.resize(cap);
                out.insert(out.end(), list.begin(), list.end());
            }
            return out;
        }

        std::uint8_t FirstRole(std::uint8_t mask)
        {
            if (mask & RF_ROLE_DPS)
                return RF_ROLE_DPS;
            if (mask & RF_ROLE_HEALER)
                return RF_ROLE_HEALER;
            if (mask & RF_ROLE_TANK)
                return RF_ROLE_TANK;
            return RF_ROLE_NONE;
        }
    }

    RfPlanResult RfCompositionPlanner::Plan(RfPlanInput const& input)
    {
        RfPlanResult result;
        RfRoleCounts const open = OpenSlots(input.required, input.filled);

        std::vector<std::uint8_t> slotRole;
        slotRole.insert(slotRole.end(), open.tank, RF_ROLE_TANK);
        slotRole.insert(slotRole.end(), open.healer, RF_ROLE_HEALER);
        slotRole.insert(slotRole.end(), open.dps, RF_ROLE_DPS);

        std::size_t const slotCount = slotRole.size();
        std::size_t const maxSelected = std::min<std::size_t>(slotCount, input.maxSelected);

        // Without role balance a candidate of any role may take a leftover
        // slot, so keep a little more than the slot count per group.
        std::vector<RfCandidate> const order =
            OrderCandidates(input.candidates, input.balanceRanged, std::max<std::size_t>(slotCount, 1) * 2);

        std::vector<int> slotOwner(slotCount, -1);
        std::vector<char> isMatched(order.size(), 0);
        std::size_t matched = 0;

        for (std::size_t c = 0; c < order.size() && matched < maxSelected; ++c)
        {
            std::vector<char> visited(slotCount, 0);
            if (TryAugment(c, order, slotRole, slotOwner, visited))
            {
                ++matched;
                isMatched[c] = 1;
            }
        }

        for (std::size_t s = 0; s < slotCount; ++s)
        {
            if (slotOwner[s] < 0)
            {
                result.missing.Add(slotRole[s], 1);
                continue;
            }
            RfCandidate const& c = order[static_cast<std::size_t>(slotOwner[s])];
            result.selected.push_back({ c.id, slotRole[s] });
            result.assigned.Add(slotRole[s], 1);
        }

        if (!input.requireRoleBalance && result.missing.Total() > 0)
        {
            // Leftover slots: any remaining candidate fills them in its own role.
            std::uint32_t leftover = result.missing.Total();
            for (std::size_t c = 0; c < order.size() && leftover > 0 &&
                                    result.selected.size() < maxSelected; ++c)
            {
                if (isMatched[c])
                    continue;
                std::uint8_t const role = FirstRole(order[c].roles);
                result.selected.push_back({ order[c].id, role });
                result.assigned.Add(role, 1);
                isMatched[c] = 1;
                --leftover;
            }
            // Missing is now measured in raw seats, not per-role.
            RfRoleCounts stillMissing;
            stillMissing.dps = leftover;
            result.missing = stillMissing;
        }

        result.complete = result.missing.Total() == 0;
        return result;
    }
}

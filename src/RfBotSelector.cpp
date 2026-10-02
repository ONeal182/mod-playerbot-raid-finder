/*
 * mod-playerbot-raid-finder — RfBotSelector.cpp
 */

#include "RfBotSelector.h"

#include "RfConfig.h"

#include "Group.h"
#include "InstanceSaveMgr.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"

#include "AiFactory.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

namespace RaidFinder
{
    char const* RejectName(RfBotReject reason)
    {
        switch (reason)
        {
            case RfBotReject::None:                 return "none";
            case RfBotReject::Offline:              return "offline";
            case RfBotReject::NotRandomBot:         return "not_random_bot";
            case RfBotReject::WrongLevel:           return "wrong_level";
            case RfBotReject::WrongFaction:         return "wrong_faction";
            case RfBotReject::AlreadyGrouped:       return "already_grouped";
            case RfBotReject::InBattleground:       return "in_battleground";
            case RfBotReject::InArena:              return "in_arena";
            case RfBotReject::InLfg:                return "in_lfg";
            case RfBotReject::InInstance:           return "in_instance";
            case RfBotReject::Busy:                 return "busy";
            case RfBotReject::ReservedByRaidFinder: return "reserved";
            case RfBotReject::RaidLockoutMismatch:  return "lockout";
            case RfBotReject::WrongRole:            return "wrong_role";
            case RfBotReject::GearTooLow:           return "low_item_level";
            case RfBotReject::Other:                return "other";
            default:                                return "?";
        }
    }

    bool RfBotSelector::IsRandomBot(Player* player)
    {
        return player && GET_PLAYERBOT_AI(player) && sRandomPlayerbotMgr.IsRandomBot(player);
    }

    std::uint8_t RfBotSelector::RolesBySpec(Player* player)
    {
        if (!player)
            return RF_ROLE_NONE;
        // BotRoles uses the same bit values as RfRole.
        std::uint8_t roles = static_cast<std::uint8_t>(AiFactory::GetPlayerRoles(player));
        return roles & (RF_ROLE_TANK | RF_ROLE_HEALER | RF_ROLE_DPS);
    }

    std::uint32_t RfBotSelector::GearScore(Player* player)
    {
        // Same metric the core uses for dungeon_access_template.min_avg_item_level:
        // average item level of the equipped set, empty slots count as 0.
        return player ? static_cast<std::uint32_t>(player->GetAverageItemLevel()) : 0;
    }

    bool RfBotSelector::IsLockoutCompatible(ObjectGuid player, ObjectGuid leader, std::uint32_t mapId,
                                            std::uint8_t difficulty)
    {
        Difficulty const diff = Difficulty(difficulty);
        InstancePlayerBind* own = sInstanceSaveMgr->PlayerGetBoundInstance(player, mapId, diff);
        // Mirrors InstanceSaveMgr::PlayerGetDestinationInstanceId: only a
        // permanent bind of our own overrides the group leader's instance.
        if (!own || !own->perm || !own->save)
            return true;
        if (player == leader)
            return true;
        InstancePlayerBind* lead = sInstanceSaveMgr->PlayerGetBoundInstance(leader, mapId, diff);
        return lead && lead->save && lead->save->GetInstanceId() == own->save->GetInstanceId();
    }

    RfBotEvaluation RfBotSelector::Evaluate(Player* bot, RfSelectionContext const& ctx,
                                            RfReservationManager const& reservations)
    {
        RfBotEvaluation ev;
        auto reject = [&ev](RfBotReject reason) -> RfBotEvaluation&
        {
            ev.eligible = false;
            ev.rejectReason = reason;
            return ev;
        };

        if (!bot)
            return reject(RfBotReject::Offline);

        ev.guid = bot->GetGUID();
        ev.name = bot->GetName();
        ev.classId = bot->getClass();

        if (!bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->PlayerLogout() ||
            bot->IsDuringRemoveFromWorld())
            return reject(RfBotReject::Offline);

        if (!IsRandomBot(bot))
            return reject(RfBotReject::NotRandomBot);

        if (bot->GetLevel() < ctx.raid.minLevel || (ctx.raid.maxLevel && bot->GetLevel() > ctx.raid.maxLevel))
            return reject(RfBotReject::WrongLevel);

        if (ctx.team != TEAM_NEUTRAL && bot->GetTeamId() != ctx.team)
            return reject(RfBotReject::WrongFaction);

        if (bot->GetGroup() || bot->GetOriginalGroup())
            return reject(RfBotReject::AlreadyGrouped);

        if (bot->InArena())
            return reject(RfBotReject::InArena);

        if (bot->InBattleground() || bot->InBattlegroundQueue())
            return reject(RfBotReject::InBattleground);

        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            return reject(RfBotReject::InLfg);

        if (bot->GetMap() && bot->GetMap()->Instanceable())
            return reject(RfBotReject::InInstance);

        if (!bot->IsAlive() || bot->HasCorpse() || bot->IsInFlight() || bot->IsBeingTeleported() ||
            bot->GetVehicle() || (ctx.strictBusy && bot->IsInCombat()))
            return reject(RfBotReject::Busy);

        RfSessionId const owner = reservations.OwnerOf(bot->GetGUID().GetRawValue());
        if (owner && owner != ctx.sessionId)
            return reject(RfBotReject::ReservedByRaidFinder);

        if (!IsLockoutCompatible(bot->GetGUID(), ctx.leaderGuid, ctx.raid.mapId, ctx.raid.difficulty))
            return reject(RfBotReject::RaidLockoutMismatch);

        ev.roles = RolesBySpec(bot) & ctx.roleFilter;
        ev.ranged = PlayerbotAI::IsRanged(bot, true);
        if (!ev.roles)
            return reject(RfBotReject::WrongRole);

        ev.gearScore = GearScore(bot);
        if (ctx.raid.minGear && ev.gearScore < ctx.raid.minGear)
            return reject(RfBotReject::GearTooLow);

        ev.eligible = true;
        ev.rejectReason = RfBotReject::None;
        return ev;
    }

    std::vector<RfBotEvaluation> RfBotSelector::EvaluatePool(RfSelectionContext const& ctx,
                                                             RfReservationManager const& reservations,
                                                             RfPoolStats* stats)
    {
        std::vector<RfBotEvaluation> out;
        PlayerBotMap const bots = sRandomPlayerbotMgr.GetAllBots();
        out.reserve(bots.size());

        for (auto const& [guid, bot] : bots)
        {
            RfBotEvaluation ev = Evaluate(bot, ctx, reservations);
            if (!ev.guid)
                ev.guid = guid;

            if (stats)
            {
                ++stats->total;
                stats->rejects[static_cast<std::size_t>(ev.rejectReason)]++;
                RfBotReject const r = ev.rejectReason;
                // Funnel: a bot counts for a stage when it passed every check
                // up to and including that stage (checks run in enum order).
                auto passed = [r](RfBotReject stage) { return r == RfBotReject::None || r > stage; };
                if (passed(RfBotReject::NotRandomBot))
                    ++stats->online;
                if (passed(RfBotReject::WrongLevel))
                    ++stats->levelEligible;
                if (passed(RfBotReject::WrongFaction))
                    ++stats->factionEligible;
                if (passed(RfBotReject::ReservedByRaidFinder))
                    ++stats->free;
                if (passed(RfBotReject::RaidLockoutMismatch))
                    ++stats->lockoutCompatible;
                if (passed(RfBotReject::WrongRole))
                    ++stats->roleEligible;
                if (ev.eligible)
                {
                    ++stats->gearEligible;
                    if (ev.roles & RF_ROLE_TANK)
                        ++stats->tanks;
                    if (ev.roles & RF_ROLE_HEALER)
                        ++stats->healers;
                    if (ev.roles & RF_ROLE_DPS)
                    {
                        ++stats->dps;
                        if (ev.ranged)
                            ++stats->ranged;
                    }
                }
            }

            if (Config().debug && !ev.eligible)
                LOG_DEBUG(RF_LOG_SELECTOR, "[{}] bot {} ({}) rejected: {}", ctx.raid.id, ev.name,
                          ev.guid.ToString(), RejectName(ev.rejectReason));

            out.push_back(std::move(ev));
        }
        return out;
    }
}

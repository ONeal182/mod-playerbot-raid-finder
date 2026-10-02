/*
 * mod-playerbot-raid-finder — RfBotPromoter.cpp
 */

#include "RfBotPromoter.h"

#include "RfBotSelector.h"
#include "RfConfig.h"
#include "RfManager.h"

#include "Item.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"

#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <map>
#include <random>

namespace RaidFinder
{
    namespace
    {
        constexpr std::uint32_t kPromoteTickMs = 500;
        constexpr std::uint32_t kPromotePerTick = 1;

        struct SpecChoice
        {
            std::uint8_t classId;
            char const* spec;   // premade spec name in playerbots.conf
        };

        // Specs that AiFactory::GetPlayerRoles maps to each role.
        SpecChoice const kTankSpecs[] = {
            { CLASS_WARRIOR, "prot pve" }, { CLASS_PALADIN, "prot pve" }, { CLASS_DRUID, "bear pve" },
        };
        SpecChoice const kHealSpecs[] = {
            { CLASS_PRIEST, "holy pve" },  { CLASS_PRIEST, "disc pve" },  { CLASS_PALADIN, "holy pve" },
            { CLASS_SHAMAN, "resto pve" }, { CLASS_DRUID, "resto pve" },
        };
        SpecChoice const kDpsSpecs[] = {
            { CLASS_WARRIOR, "fury pve" },     { CLASS_WARRIOR, "arms pve" },   { CLASS_PALADIN, "ret pve" },
            { CLASS_HUNTER, "mm pve" },        { CLASS_HUNTER, "surv pve" },    { CLASS_HUNTER, "bm pve" },
            { CLASS_ROGUE, "combat pve" },     { CLASS_ROGUE, "as pve" },       { CLASS_PRIEST, "shadow pve" },
            { CLASS_DEATH_KNIGHT, "frost pve" }, { CLASS_DEATH_KNIGHT, "unholy pve" },
            { CLASS_SHAMAN, "ele pve" },       { CLASS_SHAMAN, "enh pve" },     { CLASS_MAGE, "fire pve" },
            { CLASS_MAGE, "frost pve" },       { CLASS_MAGE, "arcane pve" },    { CLASS_WARLOCK, "affli pve" },
            { CLASS_WARLOCK, "destro pve" },   { CLASS_WARLOCK, "demo pve" },   { CLASS_DRUID, "balance pve" },
            { CLASS_DRUID, "cat pve" },
        };

        int ResolveSpecNo(std::uint8_t classId, std::string const& wanted)
        {
            for (int i = 0; i < MAX_SPECNO; ++i)
            {
                std::string const& name = sPlayerbotAIConfig.premadeSpecName[classId][i];
                if (name.empty())
                    break;
                if (name == wanted)
                    return i;
            }
            return -1;
        }

        bool IsFreeCandidate(Player* bot)
        {
            return bot && bot->IsInWorld() && RfBotSelector::IsRandomBot(bot) && !bot->GetGroup() &&
                   !bot->InBattleground() && !bot->InArena() && !bot->InBattlegroundQueue() && bot->IsAlive() &&
                   !bot->IsInFlight() && !bot->IsBeingTeleported() && !bot->IsInCombat() &&
                   sLFGMgr->GetState(bot->GetGUID()) == lfg::LFG_STATE_NONE &&
                   !(bot->GetMap() && bot->GetMap()->Instanceable()) &&
                   !sRfManager.Reservations().IsReserved(bot->GetGUID().GetRawValue());
        }

        void StripEquipment(Player* bot)
        {
            for (std::uint8_t slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        }

        // Evenly spaced item-level ceilings for `n` bots, shuffled so the order
        // of the candidates (and so their class) does not correlate with gear.
        std::vector<std::uint32_t> SpreadItemLevels(std::uint32_t n, std::uint32_t lo, std::uint32_t hi,
                                                    std::mt19937& rng)
        {
            std::vector<std::uint32_t> out;
            for (std::uint32_t i = 0; i < n; ++i)
                out.push_back(n == 1 ? (lo + hi) / 2 : lo + (hi - lo) * i / (n - 1));
            std::shuffle(out.begin(), out.end(), rng);
            return out;
        }
    }

    RfBotPromoter& RfBotPromoter::Instance()
    {
        static RfBotPromoter instance;
        return instance;
    }

    std::vector<std::string> RfBotPromoter::Queue(RfPromoteRequest const& request)
    {
        std::vector<std::string> report;
        if (request.team == TEAM_NEUTRAL)
        {
            QueueFaction(request, TEAM_ALLIANCE, report);
            QueueFaction(request, TEAM_HORDE, report);
        }
        else
            QueueFaction(request, request.team, report);

        report.push_back(Acore::StringFormat("Queued {} bot(s); processed {} per {} ms. Watch log module.raidfinder.",
                                             _jobs.size(), kPromotePerTick, kPromoteTickMs));
        return report;
    }

    void RfBotPromoter::QueueFaction(RfPromoteRequest const& request, TeamId team, std::vector<std::string>& report)
    {
        std::mt19937 rng(std::random_device{}());

        // Free candidates of this faction, grouped by class, shuffled.
        std::map<std::uint8_t, std::vector<ObjectGuid>> byClass;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!IsFreeCandidate(bot) || bot->GetTeamId() != team)
                continue;
            if (std::find(_queued.begin(), _queued.end(), guid) != _queued.end())
                continue;
            byClass[bot->getClass()].push_back(guid);
        }
        // Shuffle, then order by priority so the BACK of each list (taken first) holds:
        //   bots already at the target level but under-geared (upgrade in place),
        //   then bots of other levels,
        //   and last bots that already meet the item-level range (left alone).
        auto priority = [&request](ObjectGuid guid) -> int
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot)
                return 0;
            bool const atLevel = bot->GetLevel() == request.level;
            bool const geared = RfBotSelector::GearScore(bot) >= request.minItemLevel;
            if (atLevel && !geared)
                return 2;
            if (!atLevel)
                return 1;
            return 0;
        };
        for (auto& [cls, list] : byClass)
        {
            std::shuffle(list.begin(), list.end(), rng);
            std::stable_sort(list.begin(), list.end(),
                             [&](ObjectGuid a, ObjectGuid b) { return priority(a) < priority(b); });
        }

        char const* const faction = team == TEAM_ALLIANCE ? "Alliance" : "Horde";
        // Round-robin over CLASSES (shuffled), then over that class's specs for
        // the role, so no class or spec dominates a role and dps mixes melee
        // and casters.
        auto take = [&](SpecChoice const* specs, std::size_t specCount, std::uint32_t wanted, std::uint8_t role)
        {
            std::map<std::uint8_t, std::vector<char const*>> specsByClass;
            for (std::size_t i = 0; i < specCount; ++i)
                specsByClass[specs[i].classId].push_back(specs[i].spec);
            std::vector<std::uint8_t> classOrder;
            for (auto const& [cls, list] : specsByClass)
                classOrder.push_back(cls);
            std::shuffle(classOrder.begin(), classOrder.end(), rng);
            std::map<std::uint8_t, std::size_t> visits;

            std::vector<Job> picked;
            std::size_t cursor = 0;
            std::size_t misses = 0;
            while (picked.size() < wanted && misses < classOrder.size())
            {
                std::uint8_t const cls = classOrder[cursor % classOrder.size()];
                ++cursor;
                std::vector<ObjectGuid>& list = byClass[cls];
                std::vector<char const*> const& clsSpecs = specsByClass[cls];
                char const* specName = clsSpecs[visits[cls] % clsSpecs.size()];
                int const specNo = ResolveSpecNo(cls, specName);
                if (list.empty() || specNo < 0)
                {
                    ++misses;
                    continue;
                }
                misses = 0;
                ++visits[cls];
                Job job;
                job.guid = list.back();
                list.pop_back();
                job.role = role;
                job.specNo = specNo;
                job.specName = specName;
                picked.push_back(job);
            }

            std::vector<std::uint32_t> const ilvls =
                SpreadItemLevels(static_cast<std::uint32_t>(picked.size()), request.minItemLevel,
                                 request.maxItemLevel, rng);
            float const mult = PlayerbotAI::GetItemScoreMultiplier(ITEM_QUALITY_EPIC);
            for (std::size_t i = 0; i < picked.size(); ++i)
            {
                picked[i].itemLevel = ilvls[i];
                picked[i].level = request.level;
                picked[i].gearScoreLimit = static_cast<std::uint32_t>(ilvls[i] * mult);
                _queued.push_back(picked[i].guid);
                _jobs.push_back(picked[i]);
            }

            report.push_back(Acore::StringFormat("{} {}: {}/{} queued (level {}, item level {}-{} evenly spread)",
                                                 faction, RoleName(role), picked.size(), wanted, request.level,
                                                 request.minItemLevel, request.maxItemLevel));
        };

        take(kTankSpecs, std::size(kTankSpecs), request.tanks, RF_ROLE_TANK);
        take(kHealSpecs, std::size(kHealSpecs), request.healers, RF_ROLE_HEALER);
        take(kDpsSpecs, std::size(kDpsSpecs), request.dps, RF_ROLE_DPS);
    }

    void RfBotPromoter::Update(std::uint32_t diff)
    {
        if (_jobs.empty())
            return;
        _timer += diff;
        if (_timer < kPromoteTickMs)
            return;
        _timer = 0;

        for (std::uint32_t i = 0; i < kPromotePerTick && !_jobs.empty(); ++i)
        {
            Job const job = _jobs.front();
            _jobs.pop_front();
            _queued.erase(std::remove(_queued.begin(), _queued.end(), job.guid), _queued.end());
            Process(job);
        }

        if (_jobs.empty())
            LOG_INFO(RF_LOG_GENERAL, "RaidFinder promote: queue finished, {} bot(s) promoted in total", _done);
    }

    void RfBotPromoter::Process(Job const& job)
    {
        Player* bot = ObjectAccessor::FindPlayer(job.guid);
        if (!IsFreeCandidate(bot))
        {
            LOG_INFO(RF_LOG_GENERAL, "RaidFinder promote: {} skipped (no longer free)", job.guid.ToString());
            return;
        }

        std::uint32_t const oldLevel = bot->GetLevel();

        // Same sequence as the playerbots `init=<gs>` + `talents spec` commands:
        // full roll at the target level, force the role spec, re-gear for it.
        PlayerbotFactory factory(bot, job.level, ITEM_QUALITY_LEGENDARY, job.gearScoreLimit);
        StripEquipment(bot);
        factory.Randomize(false);
        PlayerbotFactory::InitTalentsBySpecNo(bot, job.specNo, true);
        factory.InitEquipment(false, true);
        factory.InitGlyphs(false);
        if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
            factory.ApplyEnchantAndGemsNew();
        if (bot->getClass() == CLASS_HUNTER)
            factory.InitPet();
        factory.InitAmmo();

        sRandomPlayerbotMgr.SetValue(bot, "level", job.level);
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
            botAI->ResetStrategies();
        bot->SaveToDB(false, false);
        ++_done;

        LOG_INFO(RF_LOG_GENERAL,
                 "RaidFinder promote: {} class {} level {} -> {} spec '{}' roles {} target ilvl {} avg ilvl {}",
                 bot->GetName(), bot->getClass(), oldLevel, bot->GetLevel(), job.specName,
                 RoleMaskText(RfBotSelector::RolesBySpec(bot)), job.itemLevel, RfBotSelector::GearScore(bot));
    }
}

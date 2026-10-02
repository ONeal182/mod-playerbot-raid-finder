/*
 * mod-playerbot-raid-finder — RfManagerTeleport.cpp
 *
 * Group-phase states: Assembling, TeleportLeader, WaitingLeaderMapChange,
 * TeleportBots and InRaid.
 *
 * Teleport order invariant: the real leader enters first; only after the
 * leader is inside a valid instance (temp bind created) are the other members
 * sent, in batches, so every bot resolves to the leader's instance through
 * InstanceSaveMgr::PlayerGetDestinationInstanceId.
 */

#include "RfManager.h"

#include "RfAddonProtocol.h"
#include "RfConfig.h"
#include "RfGroupBuilder.h"
#include "RfInternalOp.h"
#include "RfTeleporter.h"

#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"

namespace RaidFinder
{
    void RfManager::TickAssembling(RfSession& s)
    {
        RfSelectionContext ctx = MakeContext(s);
        ctx.strictBusy = false;

        // 1. Real players must still be online and ungrouped.
        std::vector<ObjectGuid> realGone;
        for (RfMember const& m : s.Members())
        {
            if (m.isBot)
                continue;
            Player* p = ObjectAccessor::FindPlayer(m.guid);
            if (!p || p->GetGroup() || !p->IsInWorld())
                realGone.push_back(m.guid);
            else if (s.Raid().minGear && RfBotSelector::GearScore(p) < s.Raid().minGear)
            {
                SendRfChat(p, Acore::StringFormat("Поиск рейда: ваш средний уровень предметов {} ниже требуемого {}.",
                                                  RfBotSelector::GearScore(p), s.Raid().minGear));
                realGone.push_back(m.guid);
            }
        }
        for (ObjectGuid guid : realGone)
        {
            if (guid == s.LeaderGuid())
            {
                DropReal(s, guid, "лидер недоступен", false, true);
                Release(s, "лидер стал недоступен при сборе рейда", true);
                return;
            }
            DropReal(s, guid, "недоступен при сборе рейда", false, true);
        }

        // 2. Revalidate every reserved bot (state may have changed since selection).
        std::vector<std::pair<ObjectGuid, RfBotReject>> badBots;
        for (RfMember const& m : s.Members())
        {
            if (!m.isBot)
                continue;
            RfBotEvaluation const ev = RfBotSelector::Evaluate(ObjectAccessor::FindPlayer(m.guid), ctx, _reservations);
            if (!ev.eligible)
                badBots.emplace_back(m.guid, ev.rejectReason);
        }
        for (auto const& [guid, why] : badBots)
            DropBot(s, guid, RejectName(why), false);

        // 3. Refill holes left by step 1/2 (bots auto-accept, no new proposal).
        if (!IsCompositionComplete(s) && !FillBots(s, false))
        {
            if (s.IsForced())
            {
                Release(s, "не удалось собрать полный состав", false);
                return;
            }
            LOG_INFO(RF_LOG_MATCH, "[{}] session #{} incomplete after revalidation, back to bot fill", s.Raid().id,
                     s.Id());
            s.fillRetryMs = 0;
            EnterState(s, RfState::FillingBots);
            return;
        }

        // 4. Create the raid with the real leader.
        std::string error;
        if (!RfGroupBuilder::CreateRaid(s, error))
        {
            LOG_ERROR(RF_LOG_ERROR, "[{}] session #{} raid creation failed: {}", s.Raid().id, s.Id(), error);
            Release(s, "не удалось создать рейд: " + error, false);
            return;
        }

        // 5. Add real members first, then bots (each validated right before AddMember).
        std::vector<ObjectGuid> failedReal, failedBots;
        for (RfMember const& m : s.Members())
        {
            if (m.isBot || m.guid == s.LeaderGuid())
                continue;
            if (!RfGroupBuilder::AddToRaid(s, ObjectAccessor::FindPlayer(m.guid), m.role, error))
            {
                LOG_WARN(RF_LOG_GROUP, "[{}] session #{} cannot add {}: {}", s.Raid().id, s.Id(), m.name, error);
                failedReal.push_back(m.guid);
            }
        }
        for (RfMember const& m : s.Members())
        {
            if (!m.isBot)
                continue;
            Player* bot = ObjectAccessor::FindPlayer(m.guid);
            RfBotEvaluation const ev = RfBotSelector::Evaluate(bot, ctx, _reservations);
            if (!ev.eligible)
                error = RejectName(ev.rejectReason);
            if (!ev.eligible || !RfGroupBuilder::AddToRaid(s, bot, m.role, error))
            {
                LOG_WARN(RF_LOG_GROUP, "[{}] session #{} cannot add bot {}: {}", s.Raid().id, s.Id(), m.name, error);
                failedBots.push_back(m.guid);
            }
        }
        for (ObjectGuid guid : failedReal)
            DropReal(s, guid, "не удалось вступить в рейдовую группу", true, true);
        for (ObjectGuid guid : failedBots)
            DropBot(s, guid, "AddMember failed", false);
        if (!failedBots.empty() || !failedReal.empty())
            FillBots(s, true);

        RfRoleCounts const f = s.FilledRoles();
        LOG_INFO(RF_LOG_GROUP, "[{}] session #{} raid assembled: {} members (real {}, bots {}) T {} H {} D {}",
                 s.Raid().id, s.Id(), s.Members().size(), s.RealCount(), s.BotCount(), f.tank, f.healer, f.dps);

        NotifySession(s, Acore::StringFormat("Поиск рейда: рейд собран. Вход в {}...", s.Raid().displayName));
        EnterState(s, Config().autoTeleport ? RfState::TeleportLeader : RfState::WaitingLeaderMapChange);
        if (!Config().autoTeleport)
            if (Player* leader = ObjectAccessor::FindPlayer(s.LeaderGuid()))
                SendRfChat(leader, "Поиск рейда: войдите в рейд через вход; боты присоединятся внутри.");
    }

    void RfManager::TickTeleportLeader(RfSession& s)
    {
        Player* leader = ObjectAccessor::FindPlayer(s.LeaderGuid());
        if (!leader)
        {
            Release(s, "лидер вышел из игры до телепорта", true);
            return;
        }
        if (leader->GetMapId() == s.Raid().mapId)
        {
            EnterState(s, RfState::WaitingLeaderMapChange);
            return;
        }
        if (leader->IsBeingTeleported() || !leader->IsInWorld())
        {
            if (s.StateMs() > Config().enterTimeoutMs)
                Release(s, "лидер застрял в телепорте", false);
            return;
        }

        std::string error;
        if (!RfTeleporter::TeleportIn(leader, s.Raid().mapId, error))
        {
            LOG_WARN(RF_LOG_TELEPORT, "[{}] session #{} leader {} cannot enter: {}", s.Raid().id, s.Id(),
                     leader->GetName(), error);
            SendRfChat(leader, "Поиск рейда: вы не можете войти в рейд: " + error);
            Release(s, "лидер не может войти: " + error, false);
            return;
        }

        if (RfMember* m = s.FindMember(leader->GetGUID()))
        {
            m->teleportIssued = true;
            m->teleportIssuedAtMs = getMSTime();
        }
        EnterState(s, RfState::WaitingLeaderMapChange);
    }

    void RfManager::TickWaitingLeader(RfSession& s)
    {
        Player* leader = ObjectAccessor::FindPlayer(s.LeaderGuid());
        if (!leader)
        {
            Release(s, "лидер вышел из игры при входе", true);
            return;
        }

        std::uint32_t const timeout = Config().enterTimeoutMs * (Config().autoTeleport ? 1 : 10);
        if (!RfTeleporter::IsInside(leader, s.Raid().mapId, 0) || !leader->GetMap() || !leader->GetMap()->IsRaid())
        {
            if (s.StateMs() > timeout)
                Release(s, "лидер не вошёл в рейд вовремя", false);
            return;
        }

        std::uint32_t const instanceId = leader->GetInstanceId();
        Difficulty const diff = Difficulty(s.Raid().difficulty);
        if (leader->GetMap()->GetDifficulty() != diff)
        {
            LOG_ERROR(RF_LOG_ERROR, "[{}] session #{} leader entered difficulty {} instead of {}", s.Raid().id,
                      s.Id(), uint32(leader->GetMap()->GetDifficulty()), uint32(diff));
            Release(s, "неверная сложность рейда", false);
            return;
        }

        InstancePlayerBind const* bind = sInstanceSaveMgr->PlayerGetBoundInstance(leader->GetGUID(), s.Raid().mapId, diff);
        if (!instanceId || !bind || !bind->save || bind->save->GetInstanceId() != instanceId)
        {
            LOG_WARN(RF_LOG_TELEPORT, "[{}] session #{} leader instance {} has no matching bind yet", s.Raid().id,
                     s.Id(), instanceId);
            if (s.StateMs() > timeout)
                Release(s, "некорректный экземпляр подземелья лидера", false);
            return;
        }

        s.SetLeaderInstanceId(instanceId);
        if (RfMember* m = s.FindMember(leader->GetGUID()))
        {
            m->teleportIssued = true;
            m->arrived = true;
        }

        LOG_INFO(RF_LOG_TELEPORT, "[{}] session #{} leader {} inside instance {} (map {} diff {}), sending members",
                 s.Raid().id, s.Id(), leader->GetName(), instanceId, s.Raid().mapId, uint32(diff));
        s.teleportTickMs = Config().teleportTickMs;
        EnterState(s, RfState::TeleportBots);
    }

    void RfManager::TickTeleportBots(RfSession& s, std::uint32_t diff)
    {
        RfConfig const& cfg = Config();
        std::uint32_t const mapId = s.Raid().mapId;
        std::uint32_t const instanceId = s.LeaderInstanceId();

        if (!ObjectAccessor::FindPlayer(s.LeaderGuid()))
        {
            Release(s, "лидер вышел из игры во время телепорта", false);
            return;
        }
        if (!RfGroupBuilder::Lookup(s.GroupGuid()))
        {
            Release(s, "рейдовая группа исчезла во время телепорта", false);
            return;
        }

        std::uint32_t const now = getMSTime();

        // 1. Arrival / failure checks for issued teleports.
        std::vector<ObjectGuid> botFailures;
        for (RfMember& m : s.Members())
        {
            if (!m.teleportIssued || m.arrived || m.failed)
                continue;
            Player* p = ObjectAccessor::FindPlayer(m.guid);
            if (RfTeleporter::IsInside(p, mapId, instanceId))
            {
                m.arrived = true;
                LOG_INFO(RF_LOG_TELEPORT, "[{}] session #{} {} {} arrived in instance {}", s.Raid().id, s.Id(),
                         m.isBot ? "bot" : "player", m.name, instanceId);
                continue;
            }

            char const* why = nullptr;
            if (!p)
                why = "не в сети";
            else if (p->IsInWorld() && !p->IsBeingTeleported() && p->GetMapId() == mapId &&
                     p->GetInstanceId() != instanceId)
                why = "попал в другой экземпляр (привязка не совпадает)";
            else if (getMSTimeDiff(m.teleportIssuedAtMs, now) > cfg.enterTimeoutMs)
                why = "время телепорта истекло";

            if (!why)
                continue;

            LOG_WARN(RF_LOG_TELEPORT, "[{}] session #{} {} {} failed to arrive: {}", s.Raid().id, s.Id(),
                     m.isBot ? "bot" : "player", m.name, why);
            m.failed = true;
            if (m.isBot)
                botFailures.push_back(m.guid);
            else if (p)
                SendRfChat(p, std::string("Поиск рейда: не удалось перенести вас в рейд: ") + why);
        }
        for (ObjectGuid guid : botFailures)
            DropBot(s, guid, "failed to arrive", true);

        // 2. Batch: send real players first (no limit), then up to N bots.
        s.teleportTickMs += diff;
        if (s.teleportTickMs < cfg.teleportTickMs)
            return;
        s.teleportTickMs = 0;

        std::uint32_t botsSent = 0;
        std::vector<ObjectGuid> cannotEnter;
        for (int pass = 0; pass < 2; ++pass)
        {
            bool const bots = pass == 1;
            for (RfMember& m : s.Members())
            {
                if (m.isBot != bots || m.teleportIssued || m.arrived || m.failed)
                    continue;
                if (bots && botsSent >= cfg.teleportBotsPerTick)
                    break;

                Player* p = ObjectAccessor::FindPlayer(m.guid);
                Group* group = RfGroupBuilder::Lookup(s.GroupGuid()); // foreign hooks may change it
                if (!p || !group || !group->IsMember(m.guid))
                {
                    m.failed = true;
                    if (bots)
                        cannotEnter.push_back(m.guid);
                    continue;
                }
                if (RfTeleporter::IsInside(p, mapId, instanceId))
                {
                    m.teleportIssued = true;
                    m.arrived = true;
                    continue;
                }

                std::string error;
                if (!RfTeleporter::TeleportIn(p, mapId, error))
                {
                    LOG_WARN(RF_LOG_TELEPORT, "[{}] session #{} {} {} cannot enter: {}", s.Raid().id, s.Id(),
                             bots ? "bot" : "player", m.name, error);
                    m.failed = true;
                    if (bots)
                        cannotEnter.push_back(m.guid);
                    else
                        SendRfChat(p, "Поиск рейда: вы не можете войти в рейд: " + error);
                    continue;
                }
                m.teleportIssued = true;
                m.teleportIssuedAtMs = now;
                if (bots)
                    ++botsSent;
            }
        }
        for (ObjectGuid guid : cannotEnter)
            DropBot(s, guid, "cannot enter", false);

        // 3. Replacement for failed bots (bounded by raid size).
        if (!IsCompositionComplete(s) && s.replacementsUsed < s.Raid().Size())
        {
            std::size_t const before = s.Members().size();
            FillBots(s, true);
            std::size_t const after = s.Members().size();
            if (after > before)
            {
                s.replacementsUsed += static_cast<std::uint32_t>(after - before);
                LOG_INFO(RF_LOG_TELEPORT, "[{}] session #{} {} replacement bot(s) added", s.Raid().id, s.Id(),
                         after - before);
                return;
            }
            s.replacementsUsed = s.Raid().Size(); // pool exhausted, stop trying
        }

        // 4. Done when nothing is pending any more.
        for (RfMember const& m : s.Members())
            if (!m.arrived && !m.failed)
                return;

        std::uint32_t arrivedBots = 0;
        for (RfMember const& m : s.Members())
            if (m.isBot && m.arrived)
                ++arrivedBots;

        RfRoleCounts const f = s.FilledRoles();
        RfRoleCounts const& r = s.Raid().composition;
        LOG_INFO(RF_LOG_TELEPORT, "[{}] session #{} teleport complete: {} bots inside, T {}/{} H {}/{} D {}/{}",
                 s.Raid().id, s.Id(), arrivedBots, f.tank, r.tank, f.healer, r.healer, f.dps, r.dps);

        s.abandonMs = 0;
        EnterState(s, RfState::InRaid);
        NotifySession(s, Acore::StringFormat("Поиск рейда: вы в рейде - {}. Участников {}/{} (игроков {}, ботов {}).",
                                             s.Raid().displayName, s.Members().size(), s.Raid().Size(), s.RealCount(),
                                             s.BotCount()));
        if (!IsCompositionComplete(s))
            NotifySession(s, Acore::StringFormat("Поиск рейда: состав неполный (не хватает: танков {}, лекарей {}, урона {}).",
                                                 s.OpenSlots().tank, s.OpenSlots().healer, s.OpenSlots().dps));
        if (s.Raid().experimental)
            NotifySession(s, "Поиск рейда: экспериментальный рейд - у ботов нет для него тактики.");
    }

    void RfManager::TickInRaid(RfSession& s, std::uint32_t diff)
    {
        Group* group = RfGroupBuilder::Lookup(s.GroupGuid());
        if (!group)
        {
            s.SetGroupGuid(ObjectGuid::Empty);
            Release(s, "рейдовая группа больше не существует", false);
            return;
        }

        // Drop records of members that are no longer in the group.
        std::vector<std::pair<ObjectGuid, bool>> gone;
        for (RfMember const& m : s.Members())
            if (!group->IsMember(m.guid))
                gone.emplace_back(m.guid, m.isBot);
        for (auto const& [guid, isBot] : gone)
        {
            if (isBot)
                DropBot(s, guid, "no longer in raid group", true);
            else
                DropReal(s, guid, "покинул рейдовую группу", false, true);
        }

        group = RfGroupBuilder::Lookup(s.GroupGuid());
        if (!group || s.RealCount() == 0)
        {
            Release(s, "в рейде не осталось игроков", false);
            return;
        }

        // Leader must stay a real player (playerbots abandons bot-led groups).
        RfMember const* leaderMember = s.FindMember(group->GetLeaderGUID());
        if (!leaderMember || leaderMember->isBot)
        {
            ObjectGuid newLeader;
            for (RfMember const& m : s.Members())
                if (!m.isBot && ObjectAccessor::FindPlayer(m.guid))
                {
                    newLeader = m.guid;
                    break;
                }
            if (!newLeader)
            {
                Release(s, "нет игрока онлайн, чтобы вести рейд", false);
                return;
            }
            RfInternalOp guard;
            group->ChangeLeader(newLeader);
            s.SetLeader(newLeader);
            LOG_INFO(RF_LOG_GROUP, "[{}] session #{} leader moved to real player {}", s.Raid().id, s.Id(),
                     newLeader.ToString());
        }

        // Abandon: no real player inside the instance for AbandonTimeout.
        bool realInside = false;
        for (RfMember const& m : s.Members())
            if (!m.isBot && RfTeleporter::IsInside(ObjectAccessor::FindPlayer(m.guid), s.Raid().mapId,
                                                   s.LeaderInstanceId()))
            {
                realInside = true;
                break;
            }

        if (realInside)
            s.abandonMs = 0;
        else
        {
            s.abandonMs += diff;
            if (Config().abandonTimeoutMs && s.abandonMs >= Config().abandonTimeoutMs)
                Release(s, "рейд покинут (внутри нет игроков)", false);
        }
    }
}

/*
 * mod-playerbot-raid-finder — RfManagerStates.cpp
 *
 * Update loop and the pre-group states: Searching, FillingBots, Proposal.
 * Also bot fill, `.rf force` and the `.rf bots` pool statistics.
 */

#include "RfManager.h"

#include "RfAddonProtocol.h"
#include "RfConfig.h"
#include "RfGossip.h"
#include "RfGroupBuilder.h"
#include "RfRaidRegistry.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"

#include <algorithm>

namespace RaidFinder
{
    namespace
    {
        constexpr std::uint32_t kSessionTickMs = 250;
        constexpr std::uint32_t kFillRetryMs = 10000;
    }

    void RfManager::Update(std::uint32_t diff)
    {
        ProcessEvents();

        _tickAccumulator += diff;
        if (_tickAccumulator < kSessionTickMs)
            return;
        std::uint32_t const elapsed = _tickAccumulator;
        _tickAccumulator = 0;

        ProcessEvicts(elapsed);

        std::vector<RfSessionId> ids;
        ids.reserve(_sessions.size());
        for (auto const& [id, s] : _sessions)
            ids.push_back(id);

        for (RfSessionId id : ids)
            if (RfSession* s = FindSessionMutable(id))
                if (s->State() != RfState::Done)
                    TickSession(*s, elapsed);

        for (auto it = _sessions.begin(); it != _sessions.end();)
        {
            if (it->second->State() == RfState::Done)
                it = _sessions.erase(it);
            else
                ++it;
        }

        if (!_pendingRequeue.empty())
        {
            std::vector<Requeue> pending;
            pending.swap(_pendingRequeue);
            for (Requeue const& r : pending)
            {
                Player* p = ObjectAccessor::FindPlayer(r.guid);
                if (!p)
                    continue;
                RfError const err = JoinInternal(p, r.raidId, r.role, r.joinedAtMs);
                if (err != RfError::None)
                {
                    SendRfChat(p, Acore::StringFormat("Поиск рейда: не удалось вернуть вас в очередь: {}", ErrorText(err)));
                    PushStatus(p);
                }
                else
                    LOG_INFO(RF_LOG_QUEUE, "[{}] {} re-queued with original queue time", r.raidId, p->GetName());
            }
        }
    }

    void RfManager::EnterState(RfSession& s, RfState state)
    {
        if (s.State() == state)
            return;
        LOG_INFO(RF_LOG_MATCH, "[{}] session #{} {} -> {}", s.Raid().id, s.Id(), StateName(s.State()),
                 StateName(state));
        s.SetState(state);
        PushStatusToSession(s);
    }

    bool RfManager::IsCompositionComplete(RfSession const& s) const
    {
        if (Config().requireRoleBalance)
            return s.OpenSlots().Total() == 0;
        return s.Members().size() >= s.Raid().Size();
    }

    void RfManager::TickSession(RfSession& s, std::uint32_t diff)
    {
        s.Advance(diff);
        s.fillRetryMs = s.fillRetryMs > diff ? s.fillRetryMs - diff : 0;

        if (!Config().enable && s.IsPreRaid())
        {
            Release(s, "поиск рейда отключён", false);
            return;
        }

        switch (s.State())
        {
            case RfState::Searching:              TickSearching(s); break;
            case RfState::FillingBots:            TickFillingBots(s); break;
            case RfState::Proposal:               TickProposal(s); break;
            case RfState::Assembling:             TickAssembling(s); break;
            case RfState::TeleportLeader:         TickTeleportLeader(s); break;
            case RfState::WaitingLeaderMapChange: TickWaitingLeader(s); break;
            case RfState::TeleportBots:           TickTeleportBots(s, diff); break;
            case RfState::InRaid:                 TickInRaid(s, diff); break;
            case RfState::Releasing:              Release(s, s.releaseReason, false); break;
            case RfState::Done:                   break;
        }

        if (s.State() == RfState::Done)
            return;

        s.statusPushMs += diff;
        if (s.statusPushMs >= Config().statusUpdateIntervalMs)
        {
            s.statusPushMs = 0;
            PushStatusToSession(s);
        }
    }

    // --------------------------------------------------------------- Searching

    void RfManager::TickSearching(RfSession& s)
    {
        RfConfig const& cfg = Config();

        if (s.RealCount() == 0)
        {
            Release(s, "не осталось игроков", false);
            return;
        }
        if (cfg.queueTimeoutMs && s.SearchMs() >= cfg.queueTimeoutMs)
        {
            Release(s, "время ожидания истекло", false);
            return;
        }
        if (IsCompositionComplete(s))
        {
            EnterProposal(s);
            return;
        }
        if (cfg.botFillEnabled && s.RealCount() >= cfg.minRealPlayers &&
            (!cfg.preferRealPlayers || s.SearchMs() >= cfg.botFillDelayMs))
        {
            s.fillRetryMs = 0;
            EnterState(s, RfState::FillingBots);
        }
    }

    void RfManager::TickFillingBots(RfSession& s)
    {
        RfConfig const& cfg = Config();

        if (s.RealCount() == 0)
        {
            Release(s, "не осталось игроков", false);
            return;
        }
        if (cfg.queueTimeoutMs && s.SearchMs() >= cfg.queueTimeoutMs)
        {
            Release(s, "время ожидания истекло (не хватает ботов)", false);
            return;
        }
        if (!cfg.botFillEnabled || s.RealCount() < cfg.minRealPlayers)
        {
            EnterState(s, RfState::Searching);
            return;
        }
        if (IsCompositionComplete(s))
        {
            EnterProposal(s);
            return;
        }
        if (s.fillRetryMs > 0)
            return;

        s.fillRetryMs = kFillRetryMs;
        if (FillBots(s, false))
            EnterProposal(s);
    }

    RfSelectionContext RfManager::MakeContext(RfSession const& s) const
    {
        RfSelectionContext ctx;
        ctx.raid = s.Raid();
        ctx.team = Config().sameFactionOnly ? s.Team() : TEAM_NEUTRAL;
        ctx.leaderGuid = s.LeaderGuid();
        ctx.sessionId = s.Id();
        ctx.strictBusy = true;
        return ctx;
    }

    bool RfManager::FillBots(RfSession& s, bool forGroupPhase)
    {
        RfConfig const& cfg = Config();
        if (IsCompositionComplete(s))
            return true;

        std::uint32_t const botsNow = s.BotCount();
        if (botsNow >= cfg.maxBotsPerRaid)
        {
            LOG_INFO(RF_LOG_COMP, "[{}] session #{} MaxBotsPerRaid ({}) reached, {} slot(s) open", s.Raid().id,
                     s.Id(), cfg.maxBotsPerRaid, s.OpenSlots().Total());
            return false;
        }

        RfSelectionContext const ctx = MakeContext(s);
        RfPoolStats stats;
        std::vector<RfBotEvaluation> evals = RfBotSelector::EvaluatePool(ctx, _reservations, &stats);

        RfPlanInput in;
        in.required = s.Raid().composition;
        in.filled = s.FilledRoles();
        in.maxSelected = cfg.maxBotsPerRaid - botsNow;
        in.requireRoleBalance = cfg.requireRoleBalance;
        in.balanceRanged = true;
        for (std::uint32_t i = 0; i < evals.size(); ++i)
        {
            RfBotEvaluation const& ev = evals[i];
            if (!ev.eligible || s.FindMember(ev.guid))
                continue;
            in.candidates.push_back({ i, ev.roles, ev.ranged, ev.gearScore });
        }

        RfPlanResult const plan = RfCompositionPlanner::Plan(in);

        LOG_INFO(RF_LOG_SELECTOR,
                 "[{}] session #{} pool: total {} eligible {} (T {} H {} D {} ranged {}) rejects: grouped {} "
                 "busy {} reserved {} lockout {} level {} faction {} ilvl {}",
                 s.Raid().id, s.Id(), stats.total, stats.gearEligible, stats.tanks, stats.healers, stats.dps,
                 stats.ranged, stats.rejects[size_t(RfBotReject::AlreadyGrouped)],
                 stats.rejects[size_t(RfBotReject::Busy)], stats.rejects[size_t(RfBotReject::ReservedByRaidFinder)],
                 stats.rejects[size_t(RfBotReject::RaidLockoutMismatch)], stats.rejects[size_t(RfBotReject::WrongLevel)],
                 stats.rejects[size_t(RfBotReject::WrongFaction)], stats.rejects[size_t(RfBotReject::GearTooLow)]);

        std::uint32_t added = 0;
        for (RfAssignment const& a : plan.selected)
        {
            RfBotEvaluation const& ev = evals[a.id];
            if (!_reservations.Reserve(ev.guid.GetRawValue(), s.Id()))
            {
                LOG_WARN(RF_LOG_RESERVE, "[{}] session #{} could not reserve {} (owner #{})", s.Raid().id, s.Id(),
                         ev.name, _reservations.OwnerOf(ev.guid.GetRawValue()));
                continue;
            }

            if (forGroupPhase)
            {
                // Final validation right before Group::AddMember.
                Player* bot = ObjectAccessor::FindPlayer(ev.guid);
                RfSelectionContext finalCtx = ctx;
                finalCtx.strictBusy = false;
                RfBotEvaluation const recheck = RfBotSelector::Evaluate(bot, finalCtx, _reservations);
                std::string error;
                bool ok = recheck.eligible;
                if (!ok)
                    error = RejectName(recheck.rejectReason);
                else
                    ok = RfGroupBuilder::AddToRaid(s, bot, a.role, error);
                if (!ok)
                {
                    _reservations.Release(ev.guid.GetRawValue(), s.Id());
                    LOG_INFO(RF_LOG_GROUP, "[{}] session #{} replacement bot {} rejected: {}", s.Raid().id, s.Id(),
                             ev.name, error);
                    continue;
                }
            }

            RfMember m;
            m.guid = ev.guid;
            m.name = ev.name;
            m.isBot = true;
            m.role = a.role;
            m.classId = ev.classId;
            m.gearScore = ev.gearScore;
            m.ranged = ev.ranged;
            m.accepted = true;
            s.AddMember(m);
            ++added;

            LOG_INFO(RF_LOG_RESERVE, "[{}] session #{} reserved bot {} ({}) as {} ilvl {}{}", s.Raid().id, s.Id(),
                     ev.name, ev.guid.ToString(), RoleName(a.role), ev.gearScore, ev.ranged ? " ranged" : "");
        }

        RfRoleCounts const f = s.FilledRoles();
        RfRoleCounts const& r = s.Raid().composition;
        RfRoleCounts const open = s.OpenSlots();
        LOG_INFO(RF_LOG_COMP, "[{}] session #{} composition T {}/{} H {}/{} D {}/{} (+{} bots) missing T {} H {} D {}",
                 s.Raid().id, s.Id(), f.tank, r.tank, f.healer, r.healer, f.dps, r.dps, added, open.tank, open.healer,
                 open.dps);

        if (added)
            PushStatusToSession(s);
        return IsCompositionComplete(s);
    }

    // ---------------------------------------------------------------- Proposal

    void RfManager::EnterProposal(RfSession& s)
    {
        if (s.IsForced())
        {
            EnterState(s, RfState::Assembling);
            return;
        }

        for (RfMember& m : s.Members())
            if (!m.isBot)
                m.accepted = false;

        EnterState(s, RfState::Proposal);

        RfRoleCounts const f = s.FilledRoles();
        LOG_INFO(RF_LOG_PROPOSAL, "[{}] session #{} proposal: real {} bots {} T {} H {} D {}", s.Raid().id, s.Id(),
                 s.RealCount(), s.BotCount(), f.tank, f.healer, f.dps);

        for (RfMember const& m : s.Members())
        {
            if (m.isBot)
                continue;
            Player* p = ObjectAccessor::FindPlayer(m.guid);
            if (!p)
                continue;
            SendRfChat(p, Acore::StringFormat(
                "Поиск рейда: РЕЙД НАЙДЕН - {} (игроков: {}, ботов: {}). Введите .rf accept или .rf decline ({} сек.).",
                s.Raid().displayName, s.RealCount(), s.BotCount(), Config().proposalTimeoutMs / 1000));
            if (!IsAddonUser(m.guid) && Config().gossipEnabled)
                RfGossip::ShowStatus(p);
        }
    }

    void RfManager::BackToSearching(RfSession& s, char const* why)
    {
        LOG_INFO(RF_LOG_MATCH, "[{}] session #{} back to searching: {}", s.Raid().id, s.Id(), why);
        for (RfMember& m : s.Members())
            if (!m.isBot)
                m.accepted = false;
        s.ResetSearchTimer();
        EnterState(s, RfState::Searching);
        NotifySession(s, Acore::StringFormat("Поиск рейда: {} - ищем замену.", why));
    }

    void RfManager::TickProposal(RfSession& s)
    {
        if (s.RealCount() == 0)
        {
            Release(s, "не осталось игроков", false);
            return;
        }

        if (s.AllRealAccepted())
        {
            LOG_INFO(RF_LOG_PROPOSAL, "[{}] session #{} all {} real player(s) accepted", s.Raid().id, s.Id(),
                     s.RealCount());
            EnterState(s, RfState::Assembling);
            return;
        }

        if (s.StateMs() < Config().proposalTimeoutMs)
            return;

        std::vector<ObjectGuid> expired;
        bool leaderExpired = false;
        for (RfMember const& m : s.Members())
            if (!m.isBot && !m.accepted)
            {
                expired.push_back(m.guid);
                leaderExpired |= m.guid == s.LeaderGuid();
            }

        LOG_INFO(RF_LOG_PROPOSAL, "[{}] session #{} proposal expired, {} player(s) did not answer", s.Raid().id,
                 s.Id(), expired.size());

        for (ObjectGuid guid : expired)
            DropReal(s, guid, "время на подтверждение истекло", false, true);

        if (leaderExpired)
            Release(s, "лидер не подтвердил участие", true);
        else
            BackToSearching(s, "игрок не подтвердил участие");
    }

    // ------------------------------------------------------------- GM helpers

    bool RfManager::Force(Player* gm, std::string const& raidId, std::uint8_t role, std::vector<std::string>& report)
    {
        RfRaidDefinition const* raid = sRfRaidRegistry.Find(raidId);
        if (!raid)
        {
            report.push_back("Unknown raid " + raidId);
            return false;
        }
        if (!raid->valid)
        {
            report.push_back("Raid " + raid->id + " is invalid: " + raid->invalidReason);
            return false;
        }
        if (role != RF_ROLE_TANK && role != RF_ROLE_HEALER && role != RF_ROLE_DPS)
        {
            report.push_back("Invalid role (tank/heal/dps)");
            return false;
        }
        if (_queue.Find(gm->GetGUID()) || SessionOfPlayer(gm->GetGUID()))
        {
            report.push_back("You are already in a Raid Finder session (.rf leave first)");
            return false;
        }
        if (gm->GetGroup())
        {
            report.push_back("Leave your group first");
            return false;
        }

        RfSession& s = CreateSession(*raid, gm);
        s.SetForced(true);
        AddRealMember(s, gm, role, getMSTime());
        RfSessionId const id = s.Id();

        report.push_back(Acore::StringFormat("{} Raid Finder test (session #{})", raid->displayName, id));
        report.push_back(Acore::StringFormat("Real: {} - {}", gm->GetName(), RoleName(role)));

        bool const complete = FillBots(s, false);

        RfRoleCounts botRoles;
        for (RfMember const& m : s.Members())
            if (m.isBot)
                botRoles.Add(m.role, 1);
        report.push_back(Acore::StringFormat("Bots: Tank x{}  Heal x{}  DPS x{}", botRoles.tank, botRoles.healer,
                                             botRoles.dps));
        for (RfMember const& m : s.Members())
            if (m.isBot)
                report.push_back(Acore::StringFormat("  {} class {} {} ilvl {}{}", m.name, m.classId, RoleName(m.role),
                                                     m.gearScore, m.ranged ? " ranged" : ""));

        RfRoleCounts const f = s.FilledRoles();
        RfRoleCounts const& r = raid->composition;
        report.push_back(Acore::StringFormat("Composition: Tank {}/{}  Heal {}/{}  DPS {}/{}", f.tank, r.tank, f.healer,
                                             r.healer, f.dps, r.dps));

        if (!complete)
        {
            RfRoleCounts const open = s.OpenSlots();
            report.push_back(Acore::StringFormat("NOT ENOUGH BOTS: missing Tank {} Heal {} DPS {} (see .rf bots {})",
                                                 open.tank, open.healer, open.dps, raid->id));
            Release(s, "force: недостаточно подходящих ботов", false);
            return false;
        }

        EnterState(s, RfState::Assembling);
        TickAssembling(s);

        RfSession const* after = FindSession(id);
        if (!after || after->State() >= RfState::Releasing)
        {
            report.push_back("Raid assembly FAILED: " + (after ? after->releaseReason : std::string("session gone")));
            return false;
        }

        report.push_back(Acore::StringFormat("Raid created successfully (group {}), state {}",
                                             after->GroupGuid().ToString(), StateName(after->State())));
        return true;
    }

    RfPoolStats RfManager::PoolStats(Player* viewer, RfRaidDefinition const& raid, std::vector<RfBotEvaluation>* evals)
    {
        RfSelectionContext ctx;
        ctx.raid = raid;
        ctx.team = (viewer && Config().sameFactionOnly) ? viewer->GetTeamId() : TEAM_NEUTRAL;
        ctx.leaderGuid = viewer ? viewer->GetGUID() : ObjectGuid::Empty;
        ctx.sessionId = 0;

        RfPoolStats stats;
        std::vector<RfBotEvaluation> list = RfBotSelector::EvaluatePool(ctx, _reservations, &stats);
        if (evals)
            *evals = std::move(list);
        return stats;
    }
}

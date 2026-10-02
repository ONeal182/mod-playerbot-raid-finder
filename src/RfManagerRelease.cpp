/*
 * mod-playerbot-raid-finder — RfManagerRelease.cpp
 *
 * Member removal, the single central cleanup (Release) and deferred hook
 * events. Release is idempotent: the first call does the work, later calls
 * return immediately. Group operations run under RfInternalOp so the group
 * hooks never feed the cleanup back into itself.
 */

#include "RfManager.h"

#include "RfAddonProtocol.h"
#include "RfConfig.h"
#include "RfGroupBuilder.h"
#include "RfInternalOp.h"
#include "RfTeleporter.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"

namespace RaidFinder
{
    void RfManager::DropBot(RfSession& s, ObjectGuid bot, char const* reason, bool teleportOut)
    {
        RfInternalOp guard;

        RfMember const* m = s.FindMember(bot);
        if (!m || !m->isBot)
        {
            // Never leave a reservation behind, even for an unknown member.
            _reservations.Release(bot.GetRawValue(), s.Id());
            return;
        }
        std::string const name = m->name;

        if (s.GroupGuid())
            RfGroupBuilder::RemoveFromRaid(s.GroupGuid(), bot, reason);
        if (teleportOut || m->teleportIssued)
        {
            RfTeleporter::TeleportOut(ObjectAccessor::FindPlayer(bot), s.Raid().mapId);
            ScheduleEvict(bot, s.Raid().mapId);
        }

        bool const released = _reservations.Release(bot.GetRawValue(), s.Id());
        s.RemoveMember(bot);

        LOG_INFO(RF_LOG_RESERVE, "[{}] session #{} dropped bot {} ({}): {}{}", s.Raid().id, s.Id(), name,
                 bot.ToString(), reason, released ? ", reservation released" : "");
    }

    void RfManager::DropReal(RfSession& s, ObjectGuid guid, char const* reason, bool removeFromGroup, bool notify)
    {
        RfInternalOp guard;

        RfMember const* m = s.FindMember(guid);
        if (!m || m->isBot)
        {
            _queue.Remove(guid);
            return;
        }
        std::string const name = m->name;

        if (removeFromGroup && s.GroupGuid())
            RfGroupBuilder::RemoveFromRaid(s.GroupGuid(), guid, reason);

        // Like the Dungeon Finder: a player who leaves the Raid Finder raid goes
        // back to where they stood before entering (no-op when not inside).
        RfTeleporter::TeleportOut(ObjectAccessor::FindPlayer(guid), s.Raid().mapId);
        if (m->teleportIssued)
            ScheduleEvict(guid, s.Raid().mapId);

        _queue.Remove(guid);
        s.RemoveMember(guid);

        LOG_INFO(RF_LOG_QUEUE, "[{}] session #{} removed player {}: {}", s.Raid().id, s.Id(), name, reason);

        if (notify)
            if (Player* p = ObjectAccessor::FindPlayer(guid))
            {
                SendRfChat(p, Acore::StringFormat("Поиск рейда: вы покинули {} ({}).", s.Raid().displayName, reason));
                PushStatus(p);
            }
        PushStatusToSession(s);
    }

    void RfManager::Release(RfSession& s, std::string const& reason, bool requeueOthers)
    {
        if (s.releaseStarted)
        {
            if (s.State() != RfState::Done)
                s.SetState(RfState::Done);
            return;
        }

        RfInternalOp guard;
        s.releaseStarted = true;
        s.releaseReason = reason;
        s.requeueOthers = requeueOthers;
        s.stateBeforeRelease = s.State();
        s.SetState(RfState::Releasing);

        bool const preRaid = s.stateBeforeRelease < RfState::InRaid;
        std::uint32_t const mapId = s.Raid().mapId;
        ObjectGuid const groupGuid = s.GroupGuid();

        LOG_INFO(RF_LOG_CLEANUP, "[{}] session #{} RELEASE from {}: {} ({})", s.Raid().id, s.Id(),
                 StateName(s.stateBeforeRelease), reason, s.Summary());

        std::vector<RfMember> const members = s.Members();

        // Bots: out of the instance, out of the group, back to their lifecycle.
        for (RfMember const& m : members)
        {
            if (!m.isBot)
                continue;
            Player* bot = ObjectAccessor::FindPlayer(m.guid);
            RfTeleporter::TeleportOut(bot, mapId);
            if (m.teleportIssued)
                ScheduleEvict(m.guid, mapId);
            if (!preRaid && groupGuid)
                RfGroupBuilder::RemoveFromRaid(groupGuid, m.guid, "raid finder release");
        }

        // Real players go back to where they stood before entering, like the
        // Dungeon Finder does when the group ends.
        for (RfMember const& m : members)
            if (!m.isBot)
            {
                RfTeleporter::TeleportOut(ObjectAccessor::FindPlayer(m.guid), mapId);
                if (m.teleportIssued)
                    ScheduleEvict(m.guid, mapId);
            }

        // Before the raid started the group exists only for this session.
        if (preRaid && groupGuid)
            RfGroupBuilder::Disband(groupGuid);

        std::size_t const released = _reservations.ReleaseSession(s.Id());

        std::vector<ObjectGuid> realGuids;
        for (RfMember const& m : members)
        {
            if (m.isBot)
                continue;
            _queue.Remove(m.guid);
            realGuids.push_back(m.guid);
            bool const requeue = requeueOthers && s.stateBeforeRelease < RfState::TeleportLeader;
            if (requeue)
                _pendingRequeue.push_back({ m.guid, s.Raid().id, m.role, m.joinedAtMs });

            if (Player* p = ObjectAccessor::FindPlayer(m.guid))
            {
                if (s.stateBeforeRelease == RfState::InRaid)
                    SendRfChat(p, Acore::StringFormat("Поиск рейда: {} завершён ({}). Боты отпущены.",
                                                      s.Raid().displayName, reason));
                else
                    SendRfChat(p, Acore::StringFormat("Поиск рейда: {} отменён: {}.{}", s.Raid().displayName,
                                                      reason, requeue ? " Вы сохранили место в очереди." : ""));
            }
        }

        s.Members().clear();
        s.SetGroupGuid(ObjectGuid::Empty);
        s.SetState(RfState::Done);

        for (ObjectGuid guid : realGuids)
            if (Player* p = ObjectAccessor::FindPlayer(guid))
            {
                if (Config().addonEnabled && IsAddonUser(guid))
                    RfAddonProtocol::SendInfo(p, "RELEASED");
                PushStatus(p);
            }

        LOG_INFO(RF_LOG_CLEANUP, "[{}] session #{} released: {} reservation(s) freed, {} real player(s) notified",
                 s.Raid().id, s.Id(), released, realGuids.size());
    }

    bool RfManager::ReleaseSession(RfSessionId id, std::string const& reason)
    {
        RfSession* s = FindSessionMutable(id);
        if (!s || s->State() == RfState::Done)
            return false;
        Release(*s, reason, false);
        return true;
    }

    std::size_t RfManager::ClearAll(std::string const& reason)
    {
        std::size_t n = 0;
        for (auto& [id, s] : _sessions)
            if (s->State() != RfState::Done)
            {
                Release(*s, reason, false);
                ++n;
            }
        _queue.Clear();
        _reservations.Clear();
        _pendingRequeue.clear();
        return n;
    }

    void RfManager::ShutdownClear()
    {
        LOG_INFO(RF_LOG_CLEANUP, "Raid Finder shutdown: dropping {} session(s) without group/teleport operations",
                 _sessions.size());
        _sessions.clear();
        _queue.Clear();
        _reservations.Clear();
        _pendingRequeue.clear();
        _pendingEvict.clear();
    }

    void RfManager::ScheduleEvict(ObjectGuid guid, std::uint32_t mapId)
    {
        for (Evict const& e : _pendingEvict)
            if (e.guid == guid && e.mapId == mapId)
                return;
        _pendingEvict.push_back({ guid, mapId, 0 });
    }

    void RfManager::ProcessEvicts(std::uint32_t diff)
    {
        constexpr std::uint32_t kEvictWindowMs = 60000;
        for (auto it = _pendingEvict.begin(); it != _pendingEvict.end();)
        {
            it->ageMs += diff;
            Player* p = ObjectAccessor::FindPlayer(it->guid);
            bool done = !p || it->ageMs >= kEvictWindowMs;
            if (!done && p->IsInWorld() && !p->IsBeingTeleported())
            {
                // Never pull out someone who is now legitimately in a Raid Finder raid there.
                bool const inOurRaid = SessionOfPlayer(it->guid) ||
                                       _reservations.IsReserved(it->guid.GetRawValue());
                if (p->GetMapId() == it->mapId && !inOurRaid)
                {
                    LOG_INFO(RF_LOG_CLEANUP, "evicting {} from raid map {} after in-transit release", p->GetName(),
                             it->mapId);
                    RfTeleporter::TeleportOut(p, it->mapId);
                }
                done = true;
            }
            it = done ? _pendingEvict.erase(it) : it + 1;
        }
    }

    // ------------------------------------------------------------------ events

    void RfManager::ProcessEvents()
    {
        std::vector<RfEvent> events;
        {
            std::lock_guard<std::mutex> lock(_eventLock);
            events.swap(_events);
        }
        for (RfEvent const& ev : events)
            HandleEvent(ev);
    }

    void RfManager::HandleEvent(RfEvent const& ev)
    {
        switch (ev.type)
        {
            case RfEventType::PlayerLogout:
            {
                ForgetAddonUser(ev.player);
                if (RfSession* s = SessionOfPlayer(ev.player))
                {
                    LOG_INFO(RF_LOG_QUEUE, "[{}] session #{} real player {} logged out in {}", s->Raid().id, s->Id(),
                             ev.player.ToString(), StateName(s->State()));
                    if (!s->IsPreRaid())
                        return; // InRaid: the abandon timer decides
                    if (ev.player == s->LeaderGuid())
                    {
                        DropReal(*s, ev.player, "выход из игры", false, false);
                        Release(*s, "лидер вышел из игры", true);
                        return;
                    }
                    bool const wasProposal = s->State() == RfState::Proposal;
                    DropReal(*s, ev.player, "выход из игры", s->HasGroupPhase(), false);
                    if (wasProposal)
                        BackToSearching(*s, "игрок вышел из игры");
                    return;
                }
                if (RfSession* s = SessionOfBot(ev.player))
                    DropBot(*s, ev.player, "bot logged out", false);
                else
                    _reservations.Release(ev.player.GetRawValue(), _reservations.OwnerOf(ev.player.GetRawValue()));
                return;
            }
            case RfEventType::MemberRemoved:
            {
                RfSession* s = SessionOfGroup(ev.group);
                if (!s)
                    return;
                RfMember const* m = s->FindMember(ev.player);
                if (!m)
                    return;
                if (m->isBot)
                {
                    DropBot(*s, ev.player, "покинул рейдовую группу", s->State() == RfState::InRaid);
                    return;
                }
                if (s->IsPreRaid() && ev.player == s->LeaderGuid())
                {
                    DropReal(*s, ev.player, "покинул рейдовую группу", false, true);
                    Release(*s, "лидер покинул рейдовую группу", true);
                    return;
                }
                DropReal(*s, ev.player, "покинул рейдовую группу", false, true);
                return;
            }
            case RfEventType::GroupDisbanded:
            {
                if (RfSession* s = SessionOfGroup(ev.group))
                {
                    s->SetGroupGuid(ObjectGuid::Empty); // already gone, do not touch it
                    Release(*s, "рейдовая группа распущена", false);
                }
                return;
            }
            case RfEventType::MapChanged:
            {
                // Arrival is confirmed by the TeleportBots/WaitingLeader poll
                // (it also needs the instance id); this only traces it.
                if (Config().debug)
                    if (RfSession* s = SessionOfPlayer(ev.player) ? SessionOfPlayer(ev.player) : SessionOfBot(ev.player))
                        if (Player* p = ObjectAccessor::FindPlayer(ev.player))
                            LOG_DEBUG(RF_LOG_TELEPORT, "[{}] session #{} {} map changed to {} instance {}",
                                      s->Raid().id, s->Id(), p->GetName(), p->GetMapId(), p->GetInstanceId());
                return;
            }
            case RfEventType::LeaderChanged:
                return; // handled by the InRaid tick
        }
    }
}

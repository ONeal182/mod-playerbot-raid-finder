/*
 * mod-playerbot-raid-finder — RfManager.cpp
 *
 * Public API, queue joins and the per-player actions. The state machine lives
 * in RfManagerStates.cpp, release/cleanup and hook events in RfManagerRelease.cpp.
 */

#include "RfManager.h"

#include "RfAddonProtocol.h"
#include "RfConfig.h"
#include "RfGossip.h"
#include "RfRaidRegistry.h"

#include "Chat.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"

namespace RaidFinder
{
    char const* ErrorProtocolId(RfError error)
    {
        switch (error)
        {
            case RfError::None:            return "NONE";
            case RfError::Disabled:        return "DISABLED";
            case RfError::UnknownRaid:     return "UNKNOWN_RAID";
            case RfError::RaidDisabled:    return "RAID_DISABLED";
            case RfError::InvalidRole:     return "INVALID_ROLE";
            case RfError::AlreadyQueued:   return "ALREADY_QUEUED";
            case RfError::InGroup:         return "IN_GROUP";
            case RfError::LevelTooLow:     return "LEVEL_TOO_LOW";
            case RfError::GearTooLow:      return "GEAR_TOO_LOW";
            case RfError::InBattleground:  return "IN_BATTLEGROUND";
            case RfError::InLfg:           return "IN_LFG";
            case RfError::InInstance:      return "IN_INSTANCE";
            case RfError::Lockout:         return "LOCKOUT";
            case RfError::Dead:            return "DEAD";
            case RfError::NotQueued:       return "NOT_QUEUED";
            case RfError::NoProposal:      return "NO_PROPOSAL";
            case RfError::AlreadyAccepted: return "ALREADY_ACCEPTED";
            case RfError::NotAllowed:      return "NOT_ALLOWED";
            case RfError::Internal:        return "INTERNAL";
        }
        return "INTERNAL";
    }

    char const* ErrorText(RfError error)
    {
        switch (error)
        {
            case RfError::None:            return "Готово.";
            case RfError::Disabled:        return "Поиск рейда отключён на этом сервере.";
            case RfError::UnknownRaid:     return "Неизвестный рейд.";
            case RfError::RaidDisabled:    return "Этот рейд недоступен.";
            case RfError::InvalidRole:     return "Неверная роль. Используйте tank, heal или dps.";
            case RfError::AlreadyQueued:   return "Вы уже в поиске рейда.";
            case RfError::InGroup:         return "Сначала покиньте группу (поиск рейда только для одиночных игроков).";
            case RfError::LevelTooLow:     return "Ваш уровень слишком низок для этого рейда.";
            case RfError::GearTooLow:      return "Средний уровень ваших предметов ниже требований рейда.";
            case RfError::InBattleground:  return "Нельзя встать в очередь на поле боя, арене или в очереди на них.";
            case RfError::InLfg:           return "Сначала покиньте поиск подземелий.";
            case RfError::InInstance:      return "Сначала покиньте текущее подземелье.";
            case RfError::Lockout:         return "Вы сохранены в другом экземпляре этого рейда.";
            case RfError::Dead:            return "Нельзя встать в очередь мёртвым.";
            case RfError::NotQueued:       return "Вы не в поиске рейда.";
            case RfError::NoProposal:      return "Для вас нет предложения рейда.";
            case RfError::AlreadyAccepted: return "Вы уже приняли.";
            case RfError::NotAllowed:      return "Сейчас недоступно.";
            case RfError::Internal:        return "Внутренняя ошибка поиска рейда.";
        }
        return "Внутренняя ошибка поиска рейда.";
    }

    RfManager& RfManager::Instance()
    {
        static RfManager instance;
        return instance;
    }

    // ------------------------------------------------------------------ lookup

    RfSession* RfManager::FindSessionMutable(RfSessionId id)
    {
        auto const it = _sessions.find(id);
        return it == _sessions.end() ? nullptr : it->second.get();
    }

    RfSession const* RfManager::FindSession(RfSessionId id) const
    {
        auto const it = _sessions.find(id);
        return it == _sessions.end() ? nullptr : it->second.get();
    }

    std::vector<RfSession const*> RfManager::Sessions() const
    {
        std::vector<RfSession const*> out;
        for (auto const& [id, s] : _sessions)
            out.push_back(s.get());
        return out;
    }

    RfSession* RfManager::SessionOfPlayer(ObjectGuid guid)
    {
        for (auto& [id, s] : _sessions)
            if (s->State() != RfState::Done)
                if (RfMember const* m = s->FindMember(guid))
                    if (!m->isBot)
                        return s.get();
        return nullptr;
    }

    RfSession* RfManager::SessionOfBot(ObjectGuid bot)
    {
        if (RfSessionId owner = _reservations.OwnerOf(bot.GetRawValue()))
            return FindSessionMutable(owner);
        return nullptr;
    }

    RfSession* RfManager::SessionOfGroup(ObjectGuid group)
    {
        if (!group)
            return nullptr;
        for (auto& [id, s] : _sessions)
            if (s->GroupGuid() == group && s->State() != RfState::Done)
                return s.get();
        return nullptr;
    }

    // ------------------------------------------------------------------ joins

    RfError RfManager::ValidateRealPlayer(Player* player, RfRaidDefinition const& raid) const
    {
        if (!player || !player->IsInWorld())
            return RfError::Internal;
        if (player->GetGroup())
            return RfError::InGroup;
        if (player->GetLevel() < raid.minLevel)
            return RfError::LevelTooLow;
        if (player->InBattleground() || player->InArena() || player->InBattlegroundQueue())
            return RfError::InBattleground;
        if (sLFGMgr->GetState(player->GetGUID()) != lfg::LFG_STATE_NONE)
            return RfError::InLfg;
        if (player->GetMap() && player->GetMap()->Instanceable())
            return RfError::InInstance;
        if (!player->IsAlive())
            return RfError::Dead;
        if (raid.minGear && RfBotSelector::GearScore(player) < raid.minGear)
            return RfError::GearTooLow;
        return RfError::None;
    }

    RfSession& RfManager::CreateSession(RfRaidDefinition const& raid, Player* leader)
    {
        RfSessionId const id = _nextId++;
        auto session = std::make_unique<RfSession>(id, raid);
        session->SetLeader(leader->GetGUID());
        session->SetTeam(leader->GetTeamId());
        RfSession& ref = *session;
        _sessions.emplace(id, std::move(session));
        LOG_INFO(RF_LOG_QUEUE, "[{}] session #{} created, leader {}", raid.id, id, leader->GetName());
        return ref;
    }

    RfSession* RfManager::PickSessionForJoin(Player* player, RfRaidDefinition const& raid, std::uint8_t role)
    {
        RfConfig const& cfg = Config();
        RfSession* botHeldSlot = nullptr;

        for (auto& [id, s] : _sessions)
        {
            if (!s->IsOpenForJoin() || s->Raid().id != raid.id || s->IsForced())
                continue;
            if (!cfg.allowCrossFaction && s->Team() != player->GetTeamId())
                continue;
            if (!RfBotSelector::IsLockoutCompatible(player->GetGUID(), s->LeaderGuid(), raid.mapId, raid.difficulty))
                continue;

            if (s->OpenSlots().Get(role) > 0)
                return s.get();

            if (!botHeldSlot && cfg.preferRealPlayers)
                for (RfMember const& m : s->Members())
                    if (m.isBot && m.role == role)
                    {
                        botHeldSlot = s.get();
                        break;
                    }
        }

        if (botHeldSlot)
        {
            // Real players beat bots: free one reserved bot of that role.
            for (RfMember const& m : botHeldSlot->Members())
                if (m.isBot && m.role == role)
                {
                    ObjectGuid const bot = m.guid;
                    DropBot(*botHeldSlot, bot, "заменён игроком", false);
                    break;
                }
        }
        return botHeldSlot;
    }

    void RfManager::AddRealMember(RfSession& session, Player* player, std::uint8_t role, std::uint32_t joinedAtMs)
    {
        RfMember m;
        m.guid = player->GetGUID();
        m.name = player->GetName();
        m.isBot = false;
        m.role = role;
        m.joinedAtMs = joinedAtMs;
        m.classId = player->getClass();
        session.AddMember(m);

        RfQueueEntry e;
        e.guid = player->GetGUID();
        e.raidId = session.Raid().id;
        e.role = role;
        e.joinedAtMs = joinedAtMs;
        e.sessionId = session.Id();
        _queue.Add(e);
    }

    RfError RfManager::JoinQueue(Player* player, std::string const& raidId, std::uint8_t role)
    {
        return JoinInternal(player, raidId, role, getMSTime());
    }

    RfError RfManager::JoinInternal(Player* player, std::string const& raidId, std::uint8_t role,
                                    std::uint32_t joinedAtMs)
    {
        if (!Config().enable)
            return RfError::Disabled;
        if (!player)
            return RfError::Internal;

        RfRaidDefinition const* raid = sRfRaidRegistry.Find(raidId);
        if (!raid)
            return RfError::UnknownRaid;
        if (!raid->IsUsable())
            return RfError::RaidDisabled;
        if (role != RF_ROLE_TANK && role != RF_ROLE_HEALER && role != RF_ROLE_DPS)
            return RfError::InvalidRole;
        if (_queue.Find(player->GetGUID()) || SessionOfPlayer(player->GetGUID()))
            return RfError::AlreadyQueued;

        RfError const err = ValidateRealPlayer(player, *raid);
        if (err == RfError::GearTooLow)
            SendRfChat(player, Acore::StringFormat("Поиск рейда: {} требует средний уровень предметов {}+, у вас {}.",
                                                   raid->displayName, raid->minGear, RfBotSelector::GearScore(player)));
        if (err != RfError::None)
        {
            LOG_DEBUG(RF_LOG_QUEUE, "[{}] {} join refused: {}", raid->id, player->GetName(), ErrorProtocolId(err));
            return err;
        }

        RfSession* session = PickSessionForJoin(player, *raid, role);
        if (!session)
            session = &CreateSession(*raid, player);

        AddRealMember(*session, player, role, joinedAtMs);

        LOG_INFO(RF_LOG_QUEUE, "[{}] {} joined as {} -> session #{} ({})", raid->id, player->GetName(),
                 RoleName(role), session->Id(), session->Summary());

        SendRfChat(player, Acore::StringFormat("Поиск рейда: вы в очереди в {} как {}.", raid->displayName,
                                               RoleLocalName(role)));
        if (raid->experimental)
            SendRfChat(player, "Поиск рейда: рейд ЭКСПЕРИМЕНТАЛЬНЫЙ - у ботов нет для него тактики.");
        PushStatusToSession(*session);
        return RfError::None;
    }

    RfError RfManager::LeaveQueue(Player* player)
    {
        if (!player)
            return RfError::Internal;

        RfSession* s = SessionOfPlayer(player->GetGUID());
        if (!s)
        {
            _queue.Remove(player->GetGUID());
            return RfError::NotQueued;
        }

        ObjectGuid const guid = player->GetGUID();
        LOG_INFO(RF_LOG_QUEUE, "[{}] {} leaves session #{} in state {}", s->Raid().id, player->GetName(), s->Id(),
                 StateName(s->State()));

        if (s->IsPreRaid() && guid == s->LeaderGuid())
        {
            DropReal(*s, guid, "лидер покинул очередь", true, true);
            Release(*s, "лидер покинул очередь", true);
            return RfError::None;
        }

        bool const wasProposal = s->State() == RfState::Proposal;
        DropReal(*s, guid, "покинул очередь", true, true);
        if (wasProposal)
            BackToSearching(*s, "участник покинул очередь");
        else
            PushStatusToSession(*s);
        return RfError::None;
    }

    RfError RfManager::Accept(Player* player)
    {
        if (!player)
            return RfError::Internal;
        RfSession* s = SessionOfPlayer(player->GetGUID());
        if (!s || s->State() != RfState::Proposal)
            return RfError::NoProposal;
        RfMember* m = s->FindMember(player->GetGUID());
        if (!m || m->isBot)
            return RfError::NoProposal;
        if (m->accepted)
            return RfError::AlreadyAccepted;

        m->accepted = true;
        LOG_INFO(RF_LOG_PROPOSAL, "[{}] session #{} {} accepted ({}/{})", s->Raid().id, s->Id(), player->GetName(),
                 s->AcceptedCount(), s->RealCount());
        PushStatusToSession(*s);
        return RfError::None;
    }

    RfError RfManager::Decline(Player* player)
    {
        if (!player)
            return RfError::Internal;
        RfSession* s = SessionOfPlayer(player->GetGUID());
        if (!s || s->State() != RfState::Proposal)
            return RfError::NoProposal;

        ObjectGuid const guid = player->GetGUID();
        LOG_INFO(RF_LOG_PROPOSAL, "[{}] session #{} {} declined", s->Raid().id, s->Id(), player->GetName());

        if (guid == s->LeaderGuid())
        {
            DropReal(*s, guid, "отказ", false, true);
            Release(*s, "лидер отказался от рейда", true);
            return RfError::None;
        }

        DropReal(*s, guid, "отказ", false, true);
        BackToSearching(*s, "участник отказался");
        return RfError::None;
    }

    RfStatusView RfManager::GetStatus(Player* player) const
    {
        RfStatusView v;
        if (!player)
            return v;

        RfSession const* s = nullptr;
        for (auto const& [id, sess] : _sessions)
            if (sess->State() != RfState::Done)
                if (RfMember const* m = sess->FindMember(player->GetGUID()))
                    if (!m->isBot)
                    {
                        s = sess.get();
                        break;
                    }
        if (!s)
            return v;

        RfMember const* me = s->FindMember(player->GetGUID());
        v.active = true;
        v.sessionId = s->Id();
        v.state = s->State();
        v.raidId = s->Raid().id;
        v.raidName = s->Raid().displayName;
        v.experimental = s->Raid().experimental;
        v.myRole = me->role;
        v.isLeader = s->LeaderGuid() == player->GetGUID();
        v.queueSec = getMSTimeDiff(me->joinedAtMs, getMSTime()) / 1000;
        v.filled = s->FilledRoles();
        v.required = s->Raid().composition;
        v.realCount = s->RealCount();
        v.botCount = s->BotCount();
        v.botsRequired = s->OpenSlots().Total();
        if (s->State() == RfState::Proposal)
        {
            std::uint32_t const timeout = Config().proposalTimeoutMs;
            v.proposalSecLeft = s->StateMs() >= timeout ? 0 : (timeout - s->StateMs()) / 1000;
        }
        v.acceptedCount = s->AcceptedCount();
        v.meAccepted = me->accepted;
        for (RfMember const& m : s->Members())
            if (!m.isBot)
                v.realMembers.push_back({ m.name, m.accepted, false, m.role });
        return v;
    }

    void RfManager::PushStatus(Player* player) const
    {
        if (!player || !Config().addonEnabled || !IsAddonUser(player->GetGUID()))
            return;
        RfAddonProtocol::SendStatus(player, GetStatus(player));
    }

    void RfManager::PushStatusToSession(RfSession const& s) const
    {
        for (RfMember const& m : s.Members())
            if (!m.isBot)
                if (Player* p = ObjectAccessor::FindPlayer(m.guid))
                    PushStatus(p);
    }

    void RfManager::NotifySession(RfSession const& s, std::string const& text) const
    {
        for (RfMember const& m : s.Members())
            if (!m.isBot)
                if (Player* p = ObjectAccessor::FindPlayer(m.guid))
                    SendRfChat(p, text);
    }

    void RfManager::PushEvent(RfEvent const& ev)
    {
        std::lock_guard<std::mutex> lock(_eventLock);
        _events.push_back(ev);
    }
}

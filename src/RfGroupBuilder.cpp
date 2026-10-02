/*
 * mod-playerbot-raid-finder — RfGroupBuilder.cpp
 */

#include "RfGroupBuilder.h"

#include "RfBotSelector.h"
#include "RfConfig.h"
#include "RfInternalOp.h"
#include "RfSession.h"

#include "Group.h"
#include "GroupMgr.h"
#include "LFG.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"

namespace RaidFinder
{
    Group* RfGroupBuilder::Lookup(ObjectGuid groupGuid)
    {
        if (!groupGuid)
            return nullptr;
        return sGroupMgr->GetGroupByGUID(groupGuid.GetCounter());
    }

    std::uint8_t RfGroupBuilder::LfgRoles(std::uint8_t role)
    {
        switch (role)
        {
            case RF_ROLE_TANK:   return lfg::PLAYER_ROLE_TANK;
            case RF_ROLE_HEALER: return lfg::PLAYER_ROLE_HEALER;
            case RF_ROLE_DPS:    return lfg::PLAYER_ROLE_DAMAGE;
            default:             return 0;
        }
    }

    bool RfGroupBuilder::CreateRaid(RfSession& session, std::string& error)
    {
        RfInternalOp guard;

        Player* leader = ObjectAccessor::FindPlayer(session.LeaderGuid());
        if (!leader)
        {
            error = "лидер не в сети";
            return false;
        }
        if (leader->GetGroup())
        {
            error = "лидер уже в группе";
            return false;
        }

        Group* group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            error = "Group::Create failed";
            return false;
        }
        sGroupMgr->AddGroup(group);

        group->ConvertToRaid();
        group->SetRaidDifficulty(Difficulty(session.Raid().difficulty));

        RfMember const* leaderMember = session.FindMember(leader->GetGUID());
        std::uint8_t const role = leaderMember ? leaderMember->role : RF_ROLE_DPS;
        group->SetLfgRoles(leader->GetGUID(), LfgRoles(role));
        if (role == RF_ROLE_TANK)
            group->SetGroupMemberFlag(leader->GetGUID(), true, MEMBER_FLAG_MAINTANK);

        session.SetGroupGuid(group->GetGUID());

        LOG_INFO(RF_LOG_GROUP, "[{}] session #{} raid group {} created, leader {} ({}), difficulty {}",
                 session.Raid().id, session.Id(), group->GetGUID().ToString(), leader->GetName(), RoleName(role),
                 session.Raid().difficulty);
        return true;
    }

    bool RfGroupBuilder::AddToRaid(RfSession& session, Player* player, std::uint8_t role, std::string& error)
    {
        RfInternalOp guard;

        Group* group = Lookup(session.GroupGuid());
        if (!group)
        {
            error = "рейдовая группа больше не существует";
            return false;
        }
        if (!player)
        {
            error = "игрок не в сети";
            return false;
        }
        if (group->IsMember(player->GetGUID()))
            return true;
        if (player->GetGroup())
        {
            error = "уже в другой группе";
            return false;
        }
        if (group->IsFull())
        {
            error = "рейдовая группа заполнена";
            return false;
        }
        if (!group->AddMember(player, LfgRoles(role)))
        {
            error = "Group::AddMember failed";
            return false;
        }

        if (role == RF_ROLE_TANK)
            group->SetGroupMemberFlag(player->GetGUID(), true, MEMBER_FLAG_MAINTANK);

        LOG_INFO(RF_LOG_GROUP, "[{}] session #{} added {} {} as {}", session.Raid().id, session.Id(),
                 RfBotSelector::IsRandomBot(player) ? "bot" : "player", player->GetName(), RoleName(role));
        return true;
    }

    void RfGroupBuilder::RemoveFromRaid(ObjectGuid groupGuid, ObjectGuid member, char const* reason)
    {
        RfInternalOp guard;

        Group* group = Lookup(groupGuid);
        if (!group || !group->IsMember(member))
            return;

        LOG_INFO(RF_LOG_GROUP, "group {} remove {} ({})", groupGuid.ToString(), member.ToString(),
                 reason ? reason : "-");
        group->RemoveMember(member, GROUP_REMOVEMETHOD_DEFAULT);
    }

    void RfGroupBuilder::Disband(ObjectGuid groupGuid)
    {
        RfInternalOp guard;

        if (Group* group = Lookup(groupGuid))
        {
            LOG_INFO(RF_LOG_GROUP, "group {} disbanded by Raid Finder", groupGuid.ToString());
            group->Disband();
        }
    }
}

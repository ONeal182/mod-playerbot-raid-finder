/*
 * mod-playerbot-raid-finder — RfHooks.cpp
 *
 * Script hooks. Group/player hooks only push deferred events (they may fire
 * on map-update threads, and they must not re-enter cleanup); events raised
 * by the module's own group/teleport operations are ignored via RfInternalOp.
 */

#include "RfAddonProtocol.h"
#include "RfBotPromoter.h"
#include "RfConfig.h"
#include "RfGossip.h"
#include "RfInternalOp.h"
#include "RfManager.h"
#include "RfRaidRegistry.h"

#include "Group.h"
#include "GroupScript.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"

using namespace RaidFinder;

class RaidFinderWorldScript : public WorldScript
{
public:
    RaidFinderWorldScript() : WorldScript("RaidFinderWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Config().Load();
    }

    void OnStartup() override
    {
        sRfRaidRegistry.Load();
    }

    void OnUpdate(uint32 diff) override
    {
        sRfManager.Update(diff);
        sRfBotPromoter.Update(diff);
    }

    void OnShutdown() override
    {
        sRfManager.ShutdownClear();
    }
};

class RaidFinderPlayerScript : public PlayerScript
{
public:
    RaidFinderPlayerScript() : PlayerScript("RaidFinderPlayerScript",
        { PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_GOSSIP_SELECT,
          PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE }) { }

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            sRfManager.PushEvent({ RfEventType::PlayerLogout, player->GetGUID(), ObjectGuid::Empty });
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (player && !RfInternalOp::Active())
            sRfManager.PushEvent({ RfEventType::MapChanged, player->GetGUID(), ObjectGuid::Empty });
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 sender, uint32 action) override
    {
        RfGossip::HandleSelect(player, menuId, sender, action);
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& lang, std::string& msg) override
    {
        if (!player || !RfAddonProtocol::IsRequest(lang, msg))
            return;
        RfAddonProtocol::HandleRequest(player, msg);
    }
};

class RaidFinderGroupScript : public GroupScript
{
public:
    RaidFinderGroupScript() : GroupScript("RaidFinderGroupScript",
        { GROUPHOOK_ON_REMOVE_MEMBER, GROUPHOOK_ON_DISBAND }) { }

    void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod /*method*/, ObjectGuid /*kicker*/,
                        char const* /*reason*/) override
    {
        if (group && !RfInternalOp::Active())
            sRfManager.PushEvent({ RfEventType::MemberRemoved, guid, group->GetGUID() });
    }

    void OnDisband(Group* group) override
    {
        if (group && !RfInternalOp::Active())
            sRfManager.PushEvent({ RfEventType::GroupDisbanded, ObjectGuid::Empty, group->GetGUID() });
    }
};

void AddSC_raid_finder_hooks()
{
    new RaidFinderWorldScript();
    new RaidFinderPlayerScript();
    new RaidFinderGroupScript();
}

/*
 * mod-playerbot-raid-finder — RfAddonProtocol.cpp
 */

#include "RfAddonProtocol.h"

#include "RfBotSelector.h"
#include "RfConfig.h"
#include "RfManager.h"
#include "RfRaidRegistry.h"

#include "Chat.h"
#include "Log.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <sstream>

namespace RaidFinder
{
    void SendRfChat(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(text);
    }

    namespace
    {
        std::vector<std::string> Split(std::string const& text, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            std::istringstream in(text);
            while (std::getline(in, cur, sep))
                out.push_back(cur);
            return out;
        }

        // Client strings are untrusted: keep only [A-Za-z0-9_], bounded length.
        bool IsSafeToken(std::string const& s)
        {
            if (s.empty() || s.size() > 16)
                return false;
            for (char c : s)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                    return false;
            return true;
        }
    }

    void RfAddonProtocol::Send(Player* player, std::string const& body)
    {
        if (!player || !player->GetSession())
            return;

        std::string const payload = std::string(RF_ADDON_PREFIX_SERVER) + "\t" + body;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, payload);
        player->GetSession()->SendPacket(&data);

        if (Config().debug)
            LOG_DEBUG(RF_LOG_ADDON, "-> {}: {}", player->GetName(), body);
    }

    void RfAddonProtocol::SendRaidList(Player* player)
    {
        SendSelf(player);
        Send(player, "V1:RAIDS:BEGIN");
        for (RfRaidDefinition const* def : sRfRaidRegistry.Usable())
            Send(player, Acore::StringFormat("V1:RAID:{}:{}:{}:{}:{}:{}:{}:{}", def->id, def->Size(), def->minLevel,
                                             def->maxLevel, def->minGear, def->experimental ? 1 : 0, def->Tier(),
                                             def->displayName));
        Send(player, "V1:RAIDS:END");
    }

    void RfAddonProtocol::SendSelf(Player* player)
    {
        if (player)
            Send(player, Acore::StringFormat("V1:ME:{}:{}", RfBotSelector::GearScore(player), player->GetLevel()));
    }

    void RfAddonProtocol::SendStatus(Player* player, RfStatusView const& v)
    {
        SendSelf(player);
        if (!v.active)
        {
            Send(player, "V1:STATE:IDLE::NONE:0:0");
            return;
        }

        Send(player, Acore::StringFormat("V1:STATE:{}:{}:{}:{}:{}", StateProtocolId(v.state), v.raidId,
                                         RoleProtocolId(v.myRole), v.queueSec, v.isLeader ? 1 : 0));
        Send(player, Acore::StringFormat("V1:COMP:{}:{}:{}:{}:{}:{}:{}:{}:{}", v.filled.tank, v.required.tank,
                                         v.filled.healer, v.required.healer, v.filled.dps, v.required.dps,
                                         v.realCount, v.botCount, v.botsRequired));
        if (v.state == RfState::Proposal)
        {
            Send(player, Acore::StringFormat("V1:PROPOSAL:{}:{}:{}:{}:{}", v.raidId, v.proposalSecLeft,
                                             v.acceptedCount, v.realCount, v.meAccepted ? 1 : 0));
            for (RfStatusMember const& m : v.realMembers)
                Send(player, Acore::StringFormat("V1:MEMBER:{}:{}", m.name, m.accepted ? 1 : 0));
        }
    }

    void RfAddonProtocol::SendError(Player* player, RfError error)
    {
        Send(player, std::string("V1:ERROR:") + ErrorProtocolId(error));
    }

    void RfAddonProtocol::SendInfo(Player* player, std::string const& code)
    {
        Send(player, "V1:INFO:" + code);
    }

    bool RfAddonProtocol::IsRequest(std::uint32_t lang, std::string const& msg)
    {
        return lang == LANG_ADDON && msg.compare(0, 4, "RFC\t") == 0;
    }

    void RfAddonProtocol::HandleRequest(Player* player, std::string const& msg)
    {
        if (!player)
            return;

        std::string const body = msg.substr(4);
        if (Config().debug)
            LOG_DEBUG(RF_LOG_ADDON, "<- {}: {}", player->GetName(), body);

        if (!Config().addonEnabled)
            return;

        sRfManager.MarkAddonUser(player->GetGUID());

        if (!Config().enable)
        {
            SendError(player, RfError::Disabled);
            return;
        }

        std::vector<std::string> const parts = Split(body, ':');
        if (parts.size() < 2 || parts[0] != RF_PROTOCOL_VERSION)
        {
            Send(player, "V1:ERROR:BAD_VERSION");
            return;
        }

        std::string const& cmd = parts[1];
        RfError err = RfError::None;

        if (cmd == "LIST")
        {
            SendRaidList(player);
            return;
        }
        if (cmd == "STATUS")
        {
            sRfManager.PushStatus(player);
            return;
        }
        if (cmd == "JOIN")
        {
            if (parts.size() != 4 || !IsSafeToken(parts[2]) || !IsSafeToken(parts[3]))
            {
                Send(player, "V1:ERROR:BAD_REQUEST");
                return;
            }
            std::uint8_t const role = ParseRole(parts[3]);
            err = role ? sRfManager.JoinQueue(player, parts[2], role) : RfError::InvalidRole;
        }
        else if (cmd == "LEAVE")
            err = sRfManager.LeaveQueue(player);
        else if (cmd == "ACCEPT")
            err = sRfManager.Accept(player);
        else if (cmd == "DECLINE")
            err = sRfManager.Decline(player);
        else
        {
            Send(player, "V1:ERROR:BAD_REQUEST");
            return;
        }

        LOG_INFO(RF_LOG_ADDON, "{} addon {} -> {}", player->GetName(), cmd, ErrorProtocolId(err));
        if (err != RfError::None)
            SendError(player, err);
        sRfManager.PushStatus(player);
    }
}

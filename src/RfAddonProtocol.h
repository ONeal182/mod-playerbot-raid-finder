/*
 * mod-playerbot-raid-finder — RfAddonProtocol.h
 *
 * Versioned addon protocol (V1). Client and server use different addon
 * prefixes so the client never mistakes its own echoed request for a reply.
 *
 *   Client -> server, prefix "RFC" (whisper to self):
 *     V1:LIST | V1:STATUS | V1:JOIN:<raidId>:<TANK|HEAL|DPS> | V1:LEAVE
 *     V1:ACCEPT | V1:DECLINE
 *
 *   Server -> client, prefix "RF":
 *     V1:RAIDS:BEGIN
 *     V1:RAID:<raidId>:<size>:<minLevel>:<botMaxLevel>:<minItemLevel>:<experimental>:<tier 0-2>:<display name>
 *     V1:RAIDS:END
 *     V1:ME:<average item level>:<level>     (the player's own values)
 *     V1:STATE:<IDLE|SEARCHING|FILLING|PROPOSAL|ASSEMBLING|TELEPORTING|INRAID>:<raidId>:<role>:<queueSec>:<leader>
 *     V1:COMP:<tank>:<tankNeed>:<heal>:<healNeed>:<dps>:<dpsNeed>:<real>:<bots>:<botsRequired>
 *     V1:PROPOSAL:<raidId>:<secondsLeft>:<accepted>:<totalReal>:<meAccepted>
 *     V1:MEMBER:<name>:<accepted>
 *     V1:ERROR:<code>          (stable ids, see ErrorProtocolId)
 *     V1:INFO:<code>
 *
 * Every request is validated server side; the client can only name a raid id
 * and a role. Sessions, bots and teleports are never addressable by the client.
 */

#ifndef MOD_RF_ADDON_PROTOCOL_H
#define MOD_RF_ADDON_PROTOCOL_H

#include <cstdint>
#include <string>

class Player;

namespace RaidFinder
{
    struct RfStatusView;
    enum class RfError : std::uint8_t;

    constexpr char const* RF_ADDON_PREFIX_SERVER = "RF";
    constexpr char const* RF_ADDON_PREFIX_CLIENT = "RFC";
    constexpr char const* RF_PROTOCOL_VERSION = "V1";

    class RfAddonProtocol
    {
    public:
        // Raw send: "<prefix>\t<body>" as LANG_ADDON chat.
        static void Send(Player* player, std::string const& body);

        static void SendRaidList(Player* player);
        static void SendSelf(Player* player);   // V1:ME:<avg item level>:<level>
        static void SendStatus(Player* player, RfStatusView const& view);
        static void SendError(Player* player, RfError error);
        static void SendInfo(Player* player, std::string const& code);

        // True if `msg` is one of our client requests (prefix "RFC\t").
        static bool IsRequest(std::uint32_t lang, std::string const& msg);

        // Parses and executes one request from `player`.
        static void HandleRequest(Player* player, std::string const& msg);
    };

    // Chat helper (system message), used for players without the addon.
    void SendRfChat(Player* player, std::string const& text);
}

#endif

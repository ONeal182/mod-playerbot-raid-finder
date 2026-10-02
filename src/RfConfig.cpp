/*
 * mod-playerbot-raid-finder — RfConfig.cpp
 */

#include "RfConfig.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>

namespace RaidFinder
{
    RfConfig& Config()
    {
        static RfConfig config;
        return config;
    }

    void RfConfig::Load()
    {
        enable = sConfigMgr->GetOption<bool>("RaidFinder.Enable", true);
        botFillEnabled = sConfigMgr->GetOption<bool>("RaidFinder.BotFillEnabled", true);
        botFillDelayMs = sConfigMgr->GetOption<std::uint32_t>("RaidFinder.BotFillDelay", 30) * 1000;
        minRealPlayers = std::max<std::uint32_t>(1, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.MinRealPlayers", 1));
        maxBotsPerRaid = std::min<std::uint32_t>(39, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.MaxBotsPerRaid", 39));
        autoTeleport = sConfigMgr->GetOption<bool>("RaidFinder.AutoTeleport", true);
        queueTimeoutMs = sConfigMgr->GetOption<std::uint32_t>("RaidFinder.QueueTimeout", 1800) * 1000;
        proposalTimeoutMs = std::max<std::uint32_t>(10, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.ProposalTimeout", 45)) * 1000;
        enterTimeoutMs = std::max<std::uint32_t>(15, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.EnterTimeout", 120)) * 1000;
        abandonTimeoutMs = sConfigMgr->GetOption<std::uint32_t>("RaidFinder.AbandonTimeout", 600) * 1000;
        requireRoleBalance = sConfigMgr->GetOption<bool>("RaidFinder.RequireRoleBalance", true);
        preferRealPlayers = sConfigMgr->GetOption<bool>("RaidFinder.PreferRealPlayers", true);
        sameFactionOnly = sConfigMgr->GetOption<bool>("RaidFinder.SameFactionOnly", true);
        allowCrossFaction = sConfigMgr->GetOption<bool>("RaidFinder.AllowCrossFaction", false);
        teleportBotsPerTick = std::max<std::uint32_t>(1, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.TeleportBotsPerTick", 3));
        teleportTickMs = std::max<std::uint32_t>(100, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.TeleportTickMs", 1000));
        statusUpdateIntervalMs = std::max<std::uint32_t>(1, sConfigMgr->GetOption<std::uint32_t>("RaidFinder.StatusUpdateInterval", 5)) * 1000;
        gossipEnabled = sConfigMgr->GetOption<bool>("RaidFinder.GossipEnabled", true);
        addonEnabled = sConfigMgr->GetOption<bool>("RaidFinder.AddonEnabled", true);
        debug = sConfigMgr->GetOption<bool>("RaidFinder.Debug", false);

        LOG_INFO(RF_LOG_GENERAL,
                 "RaidFinder config: enable={} botFill={} delay={}s minReal={} maxBots={} autoTeleport={} "
                 "queueTimeout={}s proposalTimeout={}s roleBalance={} preferReal={} sameFactionBots={} "
                 "crossFaction={} botsPerTick={} debug={}",
                 enable, botFillEnabled, botFillDelayMs / 1000, minRealPlayers, maxBotsPerRaid, autoTeleport,
                 queueTimeoutMs / 1000, proposalTimeoutMs / 1000, requireRoleBalance, preferRealPlayers,
                 sameFactionOnly, allowCrossFaction, teleportBotsPerTick, debug);
    }
}

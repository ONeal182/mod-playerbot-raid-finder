/*
 * mod-playerbot-raid-finder — RfConfig.h
 *
 * Cached module configuration (RaidFinder.* keys) and the log categories.
 */

#ifndef MOD_RF_CONFIG_H
#define MOD_RF_CONFIG_H

#include <cstdint>

// Log categories. All are children of "module", so they print wherever the
// "module" logger prints; each can be tuned separately in worldserver.conf.
#define RF_LOG_QUEUE     "module.raidfinder.queue"
#define RF_LOG_MATCH     "module.raidfinder.match"
#define RF_LOG_SELECTOR  "module.raidfinder.selector"
#define RF_LOG_RESERVE   "module.raidfinder.reserve"
#define RF_LOG_COMP      "module.raidfinder.composition"
#define RF_LOG_GROUP     "module.raidfinder.group"
#define RF_LOG_PROPOSAL  "module.raidfinder.proposal"
#define RF_LOG_TELEPORT  "module.raidfinder.teleport"
#define RF_LOG_ADDON     "module.raidfinder.addon"
#define RF_LOG_CLEANUP   "module.raidfinder.cleanup"
#define RF_LOG_ERROR     "module.raidfinder.error"
#define RF_LOG_GENERAL   "module.raidfinder"

namespace RaidFinder
{
    struct RfConfig
    {
        bool enable = true;
        bool botFillEnabled = true;
        std::uint32_t botFillDelayMs = 30000;
        std::uint32_t minRealPlayers = 1;
        std::uint32_t maxBotsPerRaid = 39;
        bool autoTeleport = true;
        std::uint32_t queueTimeoutMs = 1800000;
        std::uint32_t proposalTimeoutMs = 45000;
        std::uint32_t enterTimeoutMs = 120000;
        std::uint32_t abandonTimeoutMs = 600000;
        bool requireRoleBalance = true;
        bool preferRealPlayers = true;
        bool sameFactionOnly = true;
        bool allowCrossFaction = false;
        std::uint32_t teleportBotsPerTick = 3;
        std::uint32_t teleportTickMs = 1000;
        std::uint32_t statusUpdateIntervalMs = 5000;
        bool gossipEnabled = true;
        bool addonEnabled = true;
        bool debug = false;

        void Load();
    };

    RfConfig& Config();
}

#endif

/*
 * mod-playerbot-raid-finder — RfRaidRegistry.h
 *
 * Raid definitions. Primary storage is the world DB table
 * `raid_finder_definition`; a new raid is one new row. When the table is
 * missing or empty the built-in WotLK defaults are used so the module still
 * works on a fresh server.
 */

#ifndef MOD_RF_RAID_REGISTRY_H
#define MOD_RF_RAID_REGISTRY_H

#include "RfCompositionPlanner.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RaidFinder
{
    struct RfRaidDefinition
    {
        std::string id;            // stable protocol id, e.g. "ICC10"
        std::string displayName;   // "Icecrown Citadel 10"
        std::uint32_t mapId = 0;
        std::uint8_t difficulty = 0; // Difficulty enum: 0=10N 1=25N 2=10H 3=25H
        std::uint8_t minLevel = 80;
        std::uint8_t maxLevel = 0;   // upper level bound for BOTS; 0 = no cap
        std::uint32_t minGear = 0;   // minimum AVERAGE ITEM LEVEL (Player::GetAverageItemLevel); 0 = disabled
        RfRoleCounts composition;
        bool enabled = true;
        bool experimental = false;   // no dedicated playerbot strategy (generic AI)
        std::int32_t sortOrder = 0;

        // Filled by validation.
        bool valid = false;
        std::string invalidReason;

        std::uint32_t Size() const { return composition.Total(); }
        // Menu grouping derived from the level band: 0 Classic, 1 TBC, 2 WotLK.
        std::uint8_t Tier() const { return minLevel < 70 ? 0 : (minLevel < 80 ? 1 : 2); }
        bool IsUsable() const { return enabled && valid; }
    };

    class RfRaidRegistry
    {
    public:
        static RfRaidRegistry& Instance();

        // Loads from DB (falls back to defaults) and validates each row.
        void Load();

        RfRaidDefinition const* Find(std::string const& id) const; // case-insensitive
        std::vector<RfRaidDefinition> const& All() const { return _defs; }
        std::vector<RfRaidDefinition const*> Usable() const;
        bool LoadedFromDb() const { return _fromDb; }

        static std::vector<RfRaidDefinition> BuiltInDefaults();
        static char const* TierName(std::uint8_t tier);

    private:
        void Validate(RfRaidDefinition& def) const;

        std::vector<RfRaidDefinition> _defs;
        bool _fromDb = false;
    };
}

#define sRfRaidRegistry RaidFinder::RfRaidRegistry::Instance()

#endif

/*
 * mod-playerbot-raid-finder — RfRaidRegistry.cpp
 */

#include "RfRaidRegistry.h"

#include "RfConfig.h"

#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QueryResult.h"
#include "StringFormat.h"

#include <algorithm>
#include <cctype>

namespace RaidFinder
{
    namespace
    {
        std::string Upper(std::string s)
        {
            for (char& c : s)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }

        RfRaidDefinition Make(char const* id, char const* name, std::uint32_t map, std::uint8_t diff,
                              std::uint8_t minLevel, std::uint8_t maxLevel, std::uint32_t t, std::uint32_t h,
                              std::uint32_t d, bool experimental, std::int32_t order, std::uint32_t minItemLevel)
        {
            RfRaidDefinition def;
            def.id = id;
            def.displayName = name;
            def.mapId = map;
            def.difficulty = diff;
            def.minLevel = minLevel;
            def.maxLevel = maxLevel;
            def.minGear = minItemLevel;
            def.composition.tank = t;
            def.composition.healer = h;
            def.composition.dps = d;
            def.enabled = true;
            def.experimental = experimental;
            def.sortOrder = order;
            return def;
        }
    }

    RfRaidRegistry& RfRaidRegistry::Instance()
    {
        static RfRaidRegistry instance;
        return instance;
    }

    std::vector<RfRaidDefinition> RfRaidRegistry::BuiltInDefaults()
    {
        // Same rows as data/sql/db-world/*.sql. Bots must be within
        // [min level, max level]; real players only need the min level.
        return {
            // Classic (bots 60-69)
            Make("MC",     "Огненные Недра",              409, 0, 60, 69, 4, 10, 26, false, 100, 55),
            Make("BWL",    "Логово Крыла Тьмы",           469, 0, 60, 69, 4, 10, 26, false, 110, 62),
            Make("ZG",     "Зул'Гуруб",                309, 0, 60, 69, 2, 5, 13, true, 120, 55),
            Make("AQ20",   "Руины Ан'Киража",       509, 0, 60, 69, 2, 5, 13, false, 130, 58),
            Make("ONY40",  "Логово Ониксии 40",         249, 2, 60, 69, 4, 10, 26, false, 145, 58),
            Make("AQ40",   "Храм Ан'Киража",      531, 0, 60, 69, 4, 10, 26, true, 140, 66),
            // The Burning Crusade (bots 70-79)
            Make("KARA",   "Каражан",                 532, 0, 70, 79, 2, 3, 5, false, 200, 105),
            Make("ZA",     "Зул'Аман",                 568, 0, 70, 79, 2, 3, 5, false, 210, 115),
            Make("GRUUL",  "Логово Груула",             565, 0, 70, 79, 3, 6, 16, false, 220, 112),
            Make("MAG",    "Логово Магтеридона",       544, 0, 70, 79, 3, 6, 16, false, 230, 112),
            Make("SSC",    "Змеиное святилище",     548, 0, 70, 79, 3, 6, 16, false, 240, 120),
            Make("TK",     "Крепость Бурь: Око",    550, 0, 70, 79, 3, 6, 16, false, 250, 120),
            Make("HYJAL",  "Битва за гору Хиджал",   534, 0, 70, 79, 3, 6, 16, false, 260, 128),
            Make("BT",     "Черный храм",             564, 0, 70, 79, 3, 6, 16, false, 270, 134),
            Make("SWP",    "Плато Солнечного Колодца",          580, 0, 70, 79, 3, 6, 16, true, 280, 141),
            // Wrath of the Lich King (bots 80)
            Make("VOA10",  "Склеп Аркавона 10",     624, 0, 80, 0, 2, 2, 6, false, 300, 180),
            Make("VOA25",  "Склеп Аркавона 25",     624, 1, 80, 0, 2, 5, 18, false, 301, 190),
            Make("NAXX10", "Наксрамас 10",             533, 0, 80, 0, 2, 2, 6, false, 310, 180),
            Make("NAXX25", "Наксрамас 25",             533, 1, 80, 0, 2, 5, 18, false, 311, 190),
            Make("OS10",   "Обсидиановое святилище 10",      615, 0, 80, 0, 2, 2, 6, false, 320, 180),
            Make("OS25",   "Обсидиановое святилище 25",      615, 1, 80, 0, 2, 5, 18, false, 321, 190),
            Make("EOE10",  "Око Вечности 10",       616, 0, 80, 0, 2, 2, 6, false, 330, 185),
            Make("EOE25",  "Око Вечности 25",       616, 1, 80, 0, 2, 5, 18, false, 331, 195),
            Make("ONY10",  "Логово Ониксии 10",         249, 0, 80, 0, 2, 2, 6, false, 340, 190),
            Make("ONY25",  "Логово Ониксии 25",         249, 1, 80, 0, 2, 5, 18, false, 341, 200),
            Make("ULD10",  "Ульдуар 10",                603, 0, 80, 0, 2, 2, 6, false, 350, 200),
            Make("ULD25",  "Ульдуар 25",                603, 1, 80, 0, 2, 5, 18, false, 351, 213),
            Make("TOC10",  "Испытание крестоносца 10", 649, 0, 80, 0, 2, 2, 6, true, 360, 213),
            Make("TOC25",  "Испытание крестоносца 25", 649, 1, 80, 0, 2, 5, 18, true, 361, 226),
            Make("ICC10",  "Цитадель Ледяной Короны 10",      631, 0, 80, 0, 2, 2, 6, false, 370, 232),
            Make("ICC25",  "Цитадель Ледяной Короны 25",      631, 1, 80, 0, 2, 5, 18, false, 371, 245),
            Make("RS10",   "Рубиновое святилище 10",          724, 0, 80, 0, 2, 2, 6, true, 380, 245),
            Make("RS25",   "Рубиновое святилище 25",          724, 1, 80, 0, 2, 5, 18, true, 381, 258),
        };
    }

    char const* RfRaidRegistry::TierName(std::uint8_t tier)
    {
        switch (tier)
        {
            case 0:  return "Классика";
            case 1:  return "The Burning Crusade";
            default: return "Wrath of the Lich King";
        }
    }

    void RfRaidRegistry::Load()
    {
        std::vector<RfRaidDefinition> defs;
        _fromDb = false;

        // A missing table only means the SQL was not applied yet: fall back.
        QueryResult tableCheck = WorldDatabase.Query(
            "SELECT 1 FROM information_schema.tables WHERE table_schema = DATABASE() "
            "AND table_name = 'raid_finder_definition'");

        if (tableCheck)
        {
            QueryResult result = WorldDatabase.Query(
                "SELECT `id`, `display_name`, `map_id`, `difficulty`, `min_level`, `min_gear`, "
                "`tank_count`, `healer_count`, `dps_count`, `enabled`, `experimental`, `sort_order`, `max_level` "
                "FROM `raid_finder_definition`");
            if (result)
            {
                do
                {
                    Field* f = result->Fetch();
                    RfRaidDefinition def;
                    def.id = Upper(f[0].Get<std::string>());
                    def.displayName = f[1].Get<std::string>();
                    def.mapId = f[2].Get<uint32>();
                    def.difficulty = f[3].Get<uint8>();
                    def.minLevel = f[4].Get<uint8>();
                    def.minGear = f[5].Get<uint32>();
                    def.composition.tank = f[6].Get<uint8>();
                    def.composition.healer = f[7].Get<uint8>();
                    def.composition.dps = f[8].Get<uint8>();
                    def.enabled = f[9].Get<uint8>() != 0;
                    def.experimental = f[10].Get<uint8>() != 0;
                    def.sortOrder = f[11].Get<int32>();
                    def.maxLevel = f[12].Get<uint8>();
                    defs.push_back(std::move(def));
                } while (result->NextRow());
                _fromDb = true;
            }
        }

        if (defs.empty())
        {
            LOG_WARN(RF_LOG_GENERAL,
                     "RaidFinder: table `raid_finder_definition` missing or empty, using built-in defaults");
            defs = BuiltInDefaults();
        }

        std::stable_sort(defs.begin(), defs.end(), [](RfRaidDefinition const& a, RfRaidDefinition const& b)
        {
            return a.sortOrder < b.sortOrder;
        });

        for (RfRaidDefinition& def : defs)
        {
            Validate(def);
            if (!def.valid)
                LOG_ERROR(RF_LOG_ERROR, "RaidFinder: raid definition {} disabled: {}", def.id, def.invalidReason);
            else
                LOG_INFO(RF_LOG_GENERAL, "RaidFinder: raid {} '{}' map {} diff {} comp {}/{}/{} lvl {}-{} minGear {}{}{}",
                         def.id, def.displayName, def.mapId, def.difficulty, def.composition.tank,
                         def.composition.healer, def.composition.dps, def.minLevel, def.maxLevel, def.minGear,
                         def.enabled ? "" : " (disabled)", def.experimental ? " (experimental)" : "");
        }

        _defs = std::move(defs);
    }

    void RfRaidRegistry::Validate(RfRaidDefinition& def) const
    {
        def.valid = false;

        if (def.id.empty() || def.id.size() > 16 ||
            def.id.find_first_of(":;\t ,") != std::string::npos)
        {
            def.invalidReason = "id must be 1-16 chars without ':;, \\t'";
            return;
        }

        MapEntry const* map = sMapStore.LookupEntry(def.mapId);
        if (!map || !map->IsRaid())
        {
            def.invalidReason = Acore::StringFormat("map {} is not a raid map", def.mapId);
            return;
        }

        if (def.difficulty >= MAX_RAID_DIFFICULTY ||
            !GetMapDifficultyData(def.mapId, Difficulty(def.difficulty)))
        {
            def.invalidReason = Acore::StringFormat("difficulty {} not available for map {}", def.difficulty, def.mapId);
            return;
        }

        std::uint32_t const size = def.Size();
        MapDifficulty const* md = GetMapDifficultyData(def.mapId, Difficulty(def.difficulty));
        if (size < 2 || size > 40 || (md && md->maxPlayers && size > md->maxPlayers))
        {
            def.invalidReason = Acore::StringFormat("composition size {} invalid (map max {})", size,
                                                    md ? md->maxPlayers : 0);
            return;
        }

        if (def.maxLevel && def.maxLevel < def.minLevel)
        {
            def.invalidReason = Acore::StringFormat("max_level {} below min_level {}", def.maxLevel, def.minLevel);
            return;
        }

        if (!sObjectMgr->GetMapEntranceTrigger(def.mapId))
        {
            def.invalidReason = Acore::StringFormat("no entrance areatrigger for map {}", def.mapId);
            return;
        }

        def.valid = true;
        def.invalidReason.clear();
    }

    RfRaidDefinition const* RfRaidRegistry::Find(std::string const& id) const
    {
        std::string const key = Upper(id);
        for (RfRaidDefinition const& def : _defs)
            if (def.id == key)
                return &def;
        return nullptr;
    }

    std::vector<RfRaidDefinition const*> RfRaidRegistry::Usable() const
    {
        std::vector<RfRaidDefinition const*> out;
        for (RfRaidDefinition const& def : _defs)
            if (def.IsUsable())
                out.push_back(&def);
        return out;
    }
}

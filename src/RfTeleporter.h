/*
 * mod-playerbot-raid-finder — RfTeleporter.h
 *
 * Raid entrance teleport helpers. The entrance comes from the map's entrance
 * areatrigger (lfg_dungeon_template has no raid rows). Every teleport-in is
 * checked with MapMgr::PlayerCannotEnter first so the exact reason is logged.
 */

#ifndef MOD_RF_TELEPORTER_H
#define MOD_RF_TELEPORTER_H

#include <cstdint>
#include <string>

class Player;

namespace RaidFinder
{
    class RfTeleporter
    {
    public:
        // Map::EnterState as int (CAN_ENTER = 0) with its readable name.
        static int CheckEnter(Player* player, std::uint32_t mapId);
        static char const* EnterStateName(int state);

        // SetEntryPoint + TeleportTo(entrance). False with `error` on failure.
        static bool TeleportIn(Player* player, std::uint32_t mapId, std::string& error);

        // Back to the stored entry point (or homebind) when on `mapId`.
        static void TeleportOut(Player* player, std::uint32_t mapId);

        // Inside the instance and done loading.
        static bool IsInside(Player* player, std::uint32_t mapId, std::uint32_t instanceId);
    };
}

#endif

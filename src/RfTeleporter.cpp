/*
 * mod-playerbot-raid-finder — RfTeleporter.cpp
 */

#include "RfTeleporter.h"

#include "RfConfig.h"
#include "RfInternalOp.h"

#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"

namespace RaidFinder
{
    int RfTeleporter::CheckEnter(Player* player, std::uint32_t mapId)
    {
        if (!player)
            return Map::CANNOT_ENTER_UNSPECIFIED_REASON;
        return static_cast<int>(sMapMgr->PlayerCannotEnter(mapId, player, false));
    }

    char const* RfTeleporter::EnterStateName(int state)
    {
        switch (state)
        {
            case Map::CAN_ENTER:                                  return "CAN_ENTER";
            case Map::CANNOT_ENTER_ALREADY_IN_MAP:                return "CANNOT_ENTER_ALREADY_IN_MAP";
            case Map::CANNOT_ENTER_NO_ENTRY:                      return "CANNOT_ENTER_NO_ENTRY";
            case Map::CANNOT_ENTER_UNINSTANCED_DUNGEON:           return "CANNOT_ENTER_UNINSTANCED_DUNGEON";
            case Map::CANNOT_ENTER_DIFFICULTY_UNAVAILABLE:        return "CANNOT_ENTER_DIFFICULTY_UNAVAILABLE";
            case Map::CANNOT_ENTER_NOT_IN_RAID:                   return "CANNOT_ENTER_NOT_IN_RAID";
            case Map::CANNOT_ENTER_CORPSE_IN_DIFFERENT_INSTANCE:  return "CANNOT_ENTER_CORPSE_IN_DIFFERENT_INSTANCE";
            case Map::CANNOT_ENTER_INSTANCE_BIND_MISMATCH:        return "CANNOT_ENTER_INSTANCE_BIND_MISMATCH";
            case Map::CANNOT_ENTER_TOO_MANY_INSTANCES:            return "CANNOT_ENTER_TOO_MANY_INSTANCES";
            case Map::CANNOT_ENTER_MAX_PLAYERS:                   return "CANNOT_ENTER_MAX_PLAYERS";
            case Map::CANNOT_ENTER_ZONE_IN_COMBAT:                return "CANNOT_ENTER_ZONE_IN_COMBAT";
            case Map::CANNOT_ENTER_UNSPECIFIED_REASON:            return "CANNOT_ENTER_UNSPECIFIED_REASON";
            default:                                              return "UNKNOWN";
        }
    }

    bool RfTeleporter::TeleportIn(Player* player, std::uint32_t mapId, std::string& error)
    {
        RfInternalOp guard;

        if (!player || !player->IsInWorld())
        {
            error = "не в мире";
            return false;
        }

        AreaTriggerTeleport const* at = sObjectMgr->GetMapEntranceTrigger(mapId);
        if (!at)
        {
            error = "нет точки входа в рейд";
            return false;
        }

        int const enter = CheckEnter(player, mapId);
        if (enter != Map::CAN_ENTER)
        {
            error = EnterStateName(enter);
            return false;
        }

        if (!player->IsAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }

        player->SetEntryPoint();
        if (!player->TeleportTo(at->target_mapId, at->target_X, at->target_Y, at->target_Z, at->target_Orientation))
        {
            error = "телепорт отклонён";
            return false;
        }

        LOG_INFO(RF_LOG_TELEPORT, "teleport {} -> map {} ({:.1f}, {:.1f}, {:.1f})", player->GetName(), mapId,
                 at->target_X, at->target_Y, at->target_Z);
        return true;
    }

    void RfTeleporter::TeleportOut(Player* player, std::uint32_t mapId)
    {
        RfInternalOp guard;

        if (!player || !player->IsInWorld() || player->GetMapId() != mapId)
            return;

        if (!player->IsAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }

        LOG_INFO(RF_LOG_TELEPORT, "teleport {} out of map {}", player->GetName(), mapId);
        if (!player->TeleportToEntryPoint())
            player->TeleportTo(player->m_homebindMapId, player->m_homebindX, player->m_homebindY,
                               player->m_homebindZ, player->GetOrientation());
    }

    bool RfTeleporter::IsInside(Player* player, std::uint32_t mapId, std::uint32_t instanceId)
    {
        return player && player->IsInWorld() && !player->IsBeingTeleported() && player->GetMapId() == mapId &&
               (!instanceId || player->GetInstanceId() == instanceId);
    }
}

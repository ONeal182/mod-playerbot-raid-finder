/*
 * mod-playerbot-raid-finder — RfGroupBuilder.h
 *
 * Raid group creation and member removal. Always looks the Group up by GUID:
 * Group::RemoveMember can disband (and delete) the group, so no Group* is kept
 * across calls.
 */

#ifndef MOD_RF_GROUP_BUILDER_H
#define MOD_RF_GROUP_BUILDER_H

#include "ObjectGuid.h"

#include <string>

class Group;
class Player;

namespace RaidFinder
{
    class RfSession;

    class RfGroupBuilder
    {
    public:
        static Group* Lookup(ObjectGuid groupGuid);

        // Creates the raid group with the session leader. Returns false with
        // `error` filled when the leader is unusable.
        static bool CreateRaid(RfSession& session, std::string& error);

        // Adds one member (real or bot) with its LFG role and tank flag.
        static bool AddToRaid(RfSession& session, Player* player, std::uint8_t role, std::string& error);

        // Removes one member from the session group if present (internal op).
        static void RemoveFromRaid(ObjectGuid groupGuid, ObjectGuid member, char const* reason);

        // Disbands the session group if it still exists (internal op).
        static void Disband(ObjectGuid groupGuid);

        // Converts an RfRole to the LFG role mask used by Group::AddMember.
        static std::uint8_t LfgRoles(std::uint8_t role);
    };
}

#endif

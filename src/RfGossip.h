/*
 * mod-playerbot-raid-finder — RfGossip.h
 *
 * Player-sourced gossip menu (opened by `.rf` with no arguments, or pushed on a
 * proposal for players without the addon). Pure presentation: every action is
 * a call into RfManager; nothing here selects bots, builds groups or teleports.
 */

#ifndef MOD_RF_GOSSIP_H
#define MOD_RF_GOSSIP_H

#include <cstdint>

class Player;

namespace RaidFinder
{
    constexpr std::uint32_t RF_GOSSIP_MENU_ID = 0x52460001; // "RF" + 1, never a DB menu id

    class RfGossip
    {
    public:
        // Opens the right page for the player's current state.
        static void Open(Player* player);

        static void ShowMain(Player* player);
        static void ShowTier(Player* player, std::uint8_t tier);
        static void ShowRaid(Player* player, std::uint32_t raidIndex, std::uint8_t role);
        static void ShowStatus(Player* player);

        // OnPlayerGossipSelect entry. Returns true when the menu was ours.
        static bool HandleSelect(Player* player, std::uint32_t menuId, std::uint32_t sender, std::uint32_t action);
    };
}

#endif

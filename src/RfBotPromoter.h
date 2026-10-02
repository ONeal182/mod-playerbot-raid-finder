/*
 * mod-playerbot-raid-finder — RfBotPromoter.h
 *
 * GM/dev tool behind `.rf promote`: turns existing online Random Playerbots
 * into raid-ready level-80 characters with a forced role spec, so a test
 * server has a raid pool. Nothing here runs unless a GM asks for it.
 *
 * Gear: each bot gets a gear-score ceiling. Inside every role the ceilings are
 * spread evenly from the low to the high end of the range, and candidates
 * are shuffled first, so neither a role nor a class ends up skewed towards
 * good or bad gear.
 *
 * Work is queued and processed a few bots per world tick: a full
 * PlayerbotFactory roll is expensive and must not stall the world.
 */

#ifndef MOD_RF_BOT_PROMOTER_H
#define MOD_RF_BOT_PROMOTER_H

#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace RaidFinder
{
    struct RfPromoteRequest
    {
        std::uint32_t tanks = 4;
        std::uint32_t healers = 7;
        std::uint32_t dps = 19;
        TeamId team = TEAM_NEUTRAL;   // TEAM_NEUTRAL = do both factions
        std::uint32_t minItemLevel = 200;
        std::uint32_t maxItemLevel = 258;
        std::uint32_t level = 80;     // target character level
    };

    class RfBotPromoter
    {
    public:
        static RfBotPromoter& Instance();

        // Picks the bots and queues them. Returns report lines.
        std::vector<std::string> Queue(RfPromoteRequest const& request);

        void Update(std::uint32_t diff);
        std::size_t Pending() const { return _jobs.size(); }
        std::uint32_t Done() const { return _done; }

    private:
        struct Job
        {
            ObjectGuid guid;
            std::uint8_t role = 0;
            int specNo = -1;
            std::string specName;
            std::uint32_t gearScoreLimit = 0;
            std::uint32_t itemLevel = 0;
            std::uint32_t level = 80;
        };

        void QueueFaction(RfPromoteRequest const& request, TeamId team, std::vector<std::string>& report);
        void Process(Job const& job);

        std::deque<Job> _jobs;
        std::vector<ObjectGuid> _queued;
        std::uint32_t _timer = 0;
        std::uint32_t _done = 0;
    };
}

#define sRfBotPromoter RaidFinder::RfBotPromoter::Instance()

#endif

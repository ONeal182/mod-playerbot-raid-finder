/*
 * mod-playerbot-raid-finder — RfCommands.cpp
 *
 * `.rf` commands. Player commands mirror the gossip/addon actions; GM commands
 * inspect and drive the system. Every handler only calls RfManager.
 */

#include "RfAddonProtocol.h"
#include "RfBotPromoter.h"
#include "RfBotSelector.h"
#include "RfConfig.h"
#include "RfGossip.h"
#include "RfManager.h"
#include "RfRaidRegistry.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Config.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"

#include <algorithm>

using namespace Acore::ChatCommands;
using namespace RaidFinder;

namespace
{
    bool Fail(ChatHandler* handler, std::string const& text)
    {
        handler->SendSysMessage(text);
        handler->SetSentErrorMessage(true);
        return false;
    }

    bool Report(ChatHandler* handler, RfError err, char const* okText)
    {
        if (err != RfError::None)
            return Fail(handler, std::string("Поиск рейда: ") + ErrorText(err));
        if (okText)
            handler->SendSysMessage(okText);
        return true;
    }

    std::string RaidList()
    {
        std::string out;
        for (RfRaidDefinition const* def : sRfRaidRegistry.Usable())
            out += (out.empty() ? "" : ", ") + def->id;
        return out;
    }
}

class raid_finder_commandscript : public CommandScript
{
public:
    raid_finder_commandscript() : CommandScript("raid_finder_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rfTable =
        {
            { "",         HandleMenu,     SEC_PLAYER,     Console::No },
            { "join",     HandleJoin,     SEC_PLAYER,     Console::No },
            { "leave",    HandleLeave,    SEC_PLAYER,     Console::No },
            { "accept",   HandleAccept,   SEC_PLAYER,     Console::No },
            { "decline",  HandleDecline,  SEC_PLAYER,     Console::No },
            { "status",   HandleStatus,   SEC_PLAYER,     Console::No },
            { "list",     HandleList,     SEC_PLAYER,     Console::Yes },
            { "queue",    HandleQueue,    SEC_GAMEMASTER, Console::Yes },
            { "sessions", HandleSessions, SEC_GAMEMASTER, Console::Yes },
            { "bots",     HandleBots,     SEC_GAMEMASTER, Console::Yes },
            { "force",    HandleForce,    SEC_GAMEMASTER, Console::No },
            { "promote",  HandlePromote,  SEC_ADMINISTRATOR, Console::Yes },
            { "release",  HandleRelease,  SEC_GAMEMASTER, Console::Yes },
            { "clear",    HandleClear,    SEC_GAMEMASTER, Console::Yes },
            { "debug",    HandleDebug,    SEC_GAMEMASTER, Console::Yes },
            { "reload",   HandleReload,   SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "rf", rfTable }
        };
        return commandTable;
    }

    static bool HandleMenu(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!Config().enable)
            return Fail(handler, ErrorText(RfError::Disabled));
        if (!Config().gossipEnabled)
            return Fail(handler, "Меню поиска рейда отключено; используйте .rf join <рейд> <роль> или аддон (/rf).");
        RfGossip::Open(player);
        return true;
    }

    static bool HandleJoin(ChatHandler* handler, std::string raid, std::string role)
    {
        std::uint8_t const r = ParseRole(role);
        if (!r)
            return Fail(handler, "Использование: .rf join <рейд> <tank|heal|dps>. Рейды: " + RaidList());
        return Report(handler, sRfManager.JoinQueue(handler->GetPlayer(), raid, r), nullptr);
    }

    static bool HandleLeave(ChatHandler* handler)
    {
        return Report(handler, sRfManager.LeaveQueue(handler->GetPlayer()), "Поиск рейда: вы покинули очередь.");
    }

    static bool HandleAccept(ChatHandler* handler)
    {
        return Report(handler, sRfManager.Accept(handler->GetPlayer()), "Поиск рейда: вы приняли приглашение.");
    }

    static bool HandleDecline(ChatHandler* handler)
    {
        return Report(handler, sRfManager.Decline(handler->GetPlayer()), "Поиск рейда: вы отказались.");
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        RfStatusView const v = sRfManager.GetStatus(handler->GetPlayer());
        if (!v.active)
        {
            handler->SendSysMessage("Поиск рейда: вы не в очереди. Рейды: " + RaidList());
            return true;
        }
        handler->PSendSysMessage("Поиск рейда: {} - {}, роль {}{} (сессия #{})", v.raidName, StateName(v.state),
                                 RoleLocalName(v.myRole), v.isLeader ? ", лидер" : "", v.sessionId);
        handler->PSendSysMessage("Танки {}/{}  Лекари {}/{}  Урон {}/{}  игроков {} ботов {} свободно {}  в очереди {} сек.",
                                 v.filled.tank, v.required.tank, v.filled.healer, v.required.healer, v.filled.dps,
                                 v.required.dps, v.realCount, v.botCount, v.botsRequired, v.queueSec);
        if (v.state == RfState::Proposal)
            handler->PSendSysMessage("Предложение: приняли {}/{}, осталось {} сек.{}", v.acceptedCount, v.realCount,
                                     v.proposalSecLeft, v.meAccepted ? "" : " - введите .rf accept или .rf decline");
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        for (RfRaidDefinition const& def : sRfRaidRegistry.All())
            handler->PSendSysMessage("{} - {} map {} diff {} T{}/H{}/D{} lvl {}{} ilvl {}+ {}{}", def.id, def.displayName,
                                     def.mapId, def.difficulty, def.composition.tank, def.composition.healer,
                                     def.composition.dps, def.minLevel,
                                     def.maxLevel ? Acore::StringFormat(" (bots {}-{})", def.minLevel, def.maxLevel) : "",
                                     def.minGear,
                                     def.IsUsable() ? "enabled" : (def.valid ? "disabled" : "INVALID: " + def.invalidReason),
                                     def.experimental ? " (experimental)" : "");
        handler->PSendSysMessage("Source: {}", sRfRaidRegistry.LoadedFromDb() ? "world DB" : "built-in defaults");
        return true;
    }

    static bool HandleQueue(ChatHandler* handler)
    {
        std::vector<RfQueueEntry> const entries = sRfManager.Queue().Entries();
        handler->PSendSysMessage("Raid Finder queue: {} real player(s)", entries.size());
        std::uint32_t const now = getMSTime();
        for (RfQueueEntry const& e : entries)
            handler->PSendSysMessage("  {} {} {} session #{} waiting {}s", e.guid.ToString(), e.raidId,
                                     RoleName(e.role), e.sessionId, getMSTimeDiff(e.joinedAtMs, now) / 1000);
        return true;
    }

    static bool HandleSessions(ChatHandler* handler)
    {
        std::vector<RfSession const*> const sessions = sRfManager.Sessions();
        handler->PSendSysMessage("Raid Finder sessions: {} (reservations {})", sessions.size(),
                                 sRfManager.Reservations().Count());
        for (RfSession const* s : sessions)
        {
            handler->PSendSysMessage("  {} group {}", s->Summary(), s->GroupGuid().ToString());
            for (RfMember const& m : s->Members())
                handler->PSendSysMessage("    {} {} {}{}{}{}", m.isBot ? "bot" : "real", m.name, RoleName(m.role),
                                         m.accepted ? " accepted" : "", m.arrived ? " inside" : "",
                                         m.failed ? " FAILED" : "");
        }
        return true;
    }

    static bool HandleBots(ChatHandler* handler, std::string raidId)
    {
        RfRaidDefinition const* raid = sRfRaidRegistry.Find(raidId);
        if (!raid)
            return Fail(handler, "Unknown raid. Raids: " + RaidList());

        RfPoolStats const st = sRfManager.PoolStats(handler->GetPlayer(), *raid, nullptr);

        handler->PSendSysMessage("Raid Finder pool for {} (bot level {}-{}, min avg ilvl {}, faction {}):", raid->id,
                                 raid->minLevel, raid->maxLevel ? raid->maxLevel : 80, raid->minGear,
                                 (Config().sameFactionOnly && handler->GetPlayer()) ? "same as you" : "any");
        handler->PSendSysMessage("Total random bots: {}", st.total);
        handler->PSendSysMessage("Online: {}", st.online);
        handler->PSendSysMessage("Level eligible: {}", st.levelEligible);
        handler->PSendSysMessage("Faction eligible: {}", st.factionEligible);
        handler->PSendSysMessage("Free: {}", st.free);
        handler->PSendSysMessage("Lockout compatible: {}", st.lockoutCompatible);
        handler->PSendSysMessage("Role eligible: {}", st.roleEligible);
        handler->PSendSysMessage("Item level eligible: {}", st.gearEligible);
        handler->PSendSysMessage("Eligible tanks: {}  healers: {}  dps: {}  ranged dps: {}", st.tanks, st.healers,
                                 st.dps, st.ranged);
        handler->PSendSysMessage("Need: tanks {} healers {} dps {}", raid->composition.tank, raid->composition.healer,
                                 raid->composition.dps);
        handler->SendSysMessage("Rejects:");
        for (std::size_t i = 1; i < static_cast<std::size_t>(RfBotReject::Count); ++i)
            if (st.rejects[i])
                handler->PSendSysMessage("  {}: {}", RejectName(static_cast<RfBotReject>(i)), st.rejects[i]);
        if (st.total == 0)
            handler->SendSysMessage("No random bots online (check AiPlayerbot.RandomBotAutologin / MinRandomBots).");
        return true;
    }

    static bool HandleForce(ChatHandler* handler, std::string raidId, Optional<std::string> role)
    {
        std::uint8_t const r = ParseRole(role ? *role : "dps");
        std::vector<std::string> report;
        bool const ok = sRfManager.Force(handler->GetPlayer(), raidId, r, report);
        for (std::string const& line : report)
            handler->SendSysMessage(line);
        if (!ok)
            handler->SetSentErrorMessage(true);
        return ok;
    }

    // .rf promote [tanks] [healers] [dps] [alliance|horde|both] [minIlvl] [maxIlvl] [level]
    // Dev tool: makes online random bots level 80 with a forced role spec and
    // gear ceilings spread evenly across the item-level range.
    static bool HandlePromote(ChatHandler* handler, Optional<uint32> tanks, Optional<uint32> healers,
                              Optional<uint32> dps, Optional<std::string> faction, Optional<uint32> minIlvl,
                              Optional<uint32> maxIlvl, Optional<uint32> level)
    {
        RfPromoteRequest req;
        if (tanks)
            req.tanks = *tanks;
        if (healers)
            req.healers = *healers;
        if (dps)
            req.dps = *dps;
        if (faction)
        {
            if (*faction == "alliance" || *faction == "a")
                req.team = TEAM_ALLIANCE;
            else if (*faction == "horde" || *faction == "h")
                req.team = TEAM_HORDE;
            else if (*faction != "both")
                return Fail(handler, "Faction must be alliance, horde or both.");
        }
        if (minIlvl)
            req.minItemLevel = *minIlvl;
        if (maxIlvl)
            req.maxItemLevel = *maxIlvl;
        if (level)
            req.level = *level;
        if (req.level < 10 || req.level > 80)
            return Fail(handler, "Level must be 10-80.");
        if (req.minItemLevel > req.maxItemLevel || req.maxItemLevel > 300 ||
            req.tanks + req.healers + req.dps > 200)
            return Fail(handler, "Bad arguments (ilvl min<=max<=300, at most 200 bots per faction).");

        for (std::string const& line : sRfBotPromoter.Queue(req))
            handler->SendSysMessage(line);
        return true;
    }

    static bool HandleRelease(ChatHandler* handler, uint32 sessionId)
    {
        if (!sRfManager.ReleaseSession(sessionId, "закрыт ГМом"))
            return Fail(handler, "No such active session.");
        handler->PSendSysMessage("Session #{} released.", sessionId);
        return true;
    }

    static bool HandleClear(ChatHandler* handler)
    {
        std::size_t const n = sRfManager.ClearAll("очищено ГМом");
        handler->PSendSysMessage("Raid Finder cleared: {} session(s) released.", n);
        return true;
    }

    static bool HandleDebug(ChatHandler* handler, Optional<std::string> mode)
    {
        if (mode)
            Config().debug = (*mode == "on" || *mode == "1");
        handler->PSendSysMessage("Raid Finder debug: {}", Config().debug ? "on" : "off");
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        Config().Load();
        sRfRaidRegistry.Load();
        handler->PSendSysMessage("Raid Finder reloaded: {} raid definition(s), {} usable.",
                                 sRfRaidRegistry.All().size(), sRfRaidRegistry.Usable().size());
        return true;
    }
};

void AddSC_raid_finder_commands()
{
    new raid_finder_commandscript();
}

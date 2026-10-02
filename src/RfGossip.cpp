/*
 * mod-playerbot-raid-finder — RfGossip.cpp
 *
 * Gossip UI: presentation only. All actions call RfManager.
 *
 * Encoding: `sender` is the page/action kind, `action` its argument.
 */

#include "RfGossip.h"

#include "RfAddonProtocol.h"
#include "RfBotSelector.h"
#include "RfConfig.h"
#include "RfManager.h"
#include "RfRaidRegistry.h"

#include "GossipDef.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "StringFormat.h"

namespace RaidFinder
{
    namespace
    {
        enum GossipSender : std::uint32_t
        {
            SENDER_MAIN = 1,        // action: 0
            SENDER_OPEN_RAID = 2,   // action: raidIndex
            SENDER_PICK_ROLE = 3,   // action: raidIndex * 8 + role
            SENDER_JOIN = 4,        // action: raidIndex * 8 + role
            SENDER_STATUS = 5,      // action: StatusAction
            SENDER_INFO = 6,        // action: 0, non-interactive line (refreshes)
            SENDER_TIER = 7         // action: tier (0 Classic, 1 TBC, 2 WotLK)
        };

        enum StatusAction : std::uint32_t
        {
            STATUS_REFRESH = 1,
            STATUS_LEAVE = 2,
            STATUS_ACCEPT = 3,
            STATUS_DECLINE = 4,
            STATUS_CLOSE = 5
        };

        void Begin(Player* player)
        {
            ClearGossipMenuFor(player);
            player->PlayerTalkClass->GetGossipMenu().SetMenuId(RF_GOSSIP_MENU_ID);
        }

        void Send(Player* player)
        {
            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, player->GetGUID());
        }

        void Info(Player* player, std::string const& text)
        {
            AddGossipItemFor(player, GOSSIP_ICON_DOT, text, SENDER_INFO, 0);
        }

        std::string Clock(std::uint32_t sec)
        {
            return Acore::StringFormat("{:02}:{:02}", sec / 60, sec % 60);
        }

        char const* StatusText(RfState state)
        {
            switch (state)
            {
                case RfState::Searching:              return "Поиск";
                case RfState::FillingBots:            return "Добор ботами";
                case RfState::Proposal:               return "РЕЙД НАЙДЕН";
                case RfState::Assembling:             return "Сбор рейда";
                case RfState::TeleportLeader:
                case RfState::WaitingLeaderMapChange:
                case RfState::TeleportBots:           return "Вход в рейд";
                case RfState::InRaid:                 return "В рейде";
                default:                              return "Не в очереди";
            }
        }
    }

    void RfGossip::Open(Player* player)
    {
        if (!player)
            return;
        if (sRfManager.GetStatus(player).active)
            ShowStatus(player);
        else
            ShowMain(player);
    }

    void RfGossip::ShowMain(Player* player)
    {
        Begin(player);
        Info(player, "Поиск рейда - выберите дополнение:");
        std::vector<RfRaidDefinition const*> const raids = sRfRaidRegistry.Usable();
        for (std::uint8_t tier = 0; tier < 3; ++tier)
        {
            std::uint32_t count = 0;
            for (RfRaidDefinition const* def : raids)
                count += def->Tier() == tier ? 1 : 0;
            if (count)
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                                 Acore::StringFormat("{} (рейдов: {})", RfRaidRegistry::TierName(tier), count),
                                 SENDER_TIER, tier);
        }
        if (raids.empty())
            Info(player, "Нет доступных рейдов.");
        Send(player);
    }

    void RfGossip::ShowTier(Player* player, std::uint8_t tier)
    {
        Begin(player);
        Info(player, Acore::StringFormat("{} - выберите рейд:", RfRaidRegistry::TierName(tier)));
        std::vector<RfRaidDefinition const*> const raids = sRfRaidRegistry.Usable();
        for (std::uint32_t i = 0; i < raids.size(); ++i)
        {
            if (raids[i]->Tier() != tier)
                continue;
            std::string label = raids[i]->minGear
                ? Acore::StringFormat("{} (ур. {}+, ilvl {}+)", raids[i]->displayName, raids[i]->minLevel, raids[i]->minGear)
                : Acore::StringFormat("{} (ур. {}+)", raids[i]->displayName, raids[i]->minLevel);
            if (raids[i]->experimental)
                label += " *";
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, label, SENDER_OPEN_RAID, i);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Назад", SENDER_MAIN, 0);
        Send(player);
    }

    void RfGossip::ShowRaid(Player* player, std::uint32_t raidIndex, std::uint8_t role)
    {
        std::vector<RfRaidDefinition const*> const raids = sRfRaidRegistry.Usable();
        if (raidIndex >= raids.size())
        {
            ShowMain(player);
            return;
        }
        RfRaidDefinition const& raid = *raids[raidIndex];

        Begin(player);
        Info(player, raid.displayName);
        Info(player, Acore::StringFormat("Танков {}  Лекарей {}  Урона {}  (уровень {}+)", raid.composition.tank,
                                         raid.composition.healer, raid.composition.dps, raid.minLevel));
        if (raid.minGear)
        {
            std::uint32_t const mine = RfBotSelector::GearScore(player);
            Info(player, Acore::StringFormat("Требуемый средний уровень предметов: {}+ (ваш: {}{})", raid.minGear, mine,
                                             mine < raid.minGear ? ", НЕ ХВАТАЕТ" : ""));
        }
        if (raid.experimental)
            Info(player, "Экспериментально: у ботов нет тактики для этого рейда.");

        Info(player, "Роль:");
        for (std::uint8_t r : { RF_ROLE_TANK, RF_ROLE_HEALER, RF_ROLE_DPS })
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                             Acore::StringFormat("{} {}", r == role ? "[x]" : "[  ]", RoleLocalName(r)), SENDER_PICK_ROLE,
                             raidIndex * 8 + r);

        if (role)
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, Acore::StringFormat("Встать в очередь: {}", RoleLocalName(role)),
                             SENDER_JOIN, raidIndex * 8 + role);
        else
            Info(player, "Выберите роль.");

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Назад", SENDER_TIER, raid.Tier());
        Send(player);
    }

    void RfGossip::ShowStatus(Player* player)
    {
        RfStatusView const v = sRfManager.GetStatus(player);
        if (!v.active)
        {
            ShowMain(player);
            return;
        }

        Begin(player);
        Info(player, v.raidName);

        if (v.state == RfState::Proposal)
        {
            Info(player, "РЕЙД НАЙДЕН");
            Info(player, Acore::StringFormat("Танки {}/{}   Лекари {}/{}   Урон {}/{}", v.filled.tank, v.required.tank,
                                             v.filled.healer, v.required.healer, v.filled.dps, v.required.dps));
            Info(player, Acore::StringFormat("Игроков: {}   Ботов: {}", v.realCount, v.botCount));
            if (v.meAccepted)
            {
                Info(player, "Ожидание игроков...");
                Info(player, Acore::StringFormat("Приняли: {} / {}   (осталось {} сек.)", v.acceptedCount, v.realCount,
                                                 v.proposalSecLeft));
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Обновить", SENDER_STATUS, STATUS_REFRESH);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Покинуть очередь", SENDER_STATUS, STATUS_LEAVE);
            }
            else
            {
                Info(player, Acore::StringFormat("Осталось: {} сек.", v.proposalSecLeft));
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Принять", SENDER_STATUS, STATUS_ACCEPT);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Отказаться", SENDER_STATUS, STATUS_DECLINE);
            }
            Send(player);
            return;
        }

        Info(player, Acore::StringFormat("Ваша роль: {}", RoleLocalName(v.myRole)));
        Info(player, Acore::StringFormat("Статус: {}", StatusText(v.state)));
        Info(player, Acore::StringFormat("Танки: {} / {}", v.filled.tank, v.required.tank));
        Info(player, Acore::StringFormat("Лекари: {} / {}", v.filled.healer, v.required.healer));
        Info(player, Acore::StringFormat("Урон: {} / {}", v.filled.dps, v.required.dps));
        Info(player, Acore::StringFormat("Игроков: {}", v.realCount));
        if (v.state == RfState::Searching)
            Info(player, Acore::StringFormat("Нужно ботов: {}", v.botsRequired));
        else
            Info(player, Acore::StringFormat("Выбрано ботов: {}", v.botCount));
        if (v.state <= RfState::FillingBots)
            Info(player, Acore::StringFormat("В очереди: {}", Clock(v.queueSec)));

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Обновить", SENDER_STATUS, STATUS_REFRESH);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, v.state == RfState::InRaid ? "Покинуть рейд" : "Покинуть очередь",
                         SENDER_STATUS, STATUS_LEAVE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Закрыть", SENDER_STATUS, STATUS_CLOSE);
        Send(player);
    }

    bool RfGossip::HandleSelect(Player* player, std::uint32_t menuId, std::uint32_t sender, std::uint32_t action)
    {
        if (!player || menuId != RF_GOSSIP_MENU_ID)
            return false;

        if (!Config().enable || !Config().gossipEnabled)
        {
            CloseGossipMenuFor(player);
            return true;
        }

        auto report = [player](RfError err)
        {
            if (err != RfError::None)
                SendRfChat(player, std::string("Поиск рейда: ") + ErrorText(err));
        };

        switch (sender)
        {
            case SENDER_MAIN:
                ShowMain(player);
                break;
            case SENDER_TIER:
                ShowTier(player, static_cast<std::uint8_t>(action));
                break;
            case SENDER_OPEN_RAID:
                ShowRaid(player, action, RF_ROLE_NONE);
                break;
            case SENDER_PICK_ROLE:
                ShowRaid(player, action / 8, static_cast<std::uint8_t>(action % 8));
                break;
            case SENDER_JOIN:
            {
                std::vector<RfRaidDefinition const*> const raids = sRfRaidRegistry.Usable();
                std::uint32_t const idx = action / 8;
                if (idx >= raids.size())
                {
                    ShowMain(player);
                    break;
                }
                RfError const err = sRfManager.JoinQueue(player, raids[idx]->id, static_cast<std::uint8_t>(action % 8));
                report(err);
                if (err == RfError::None)
                    ShowStatus(player);
                else
                    ShowRaid(player, idx, static_cast<std::uint8_t>(action % 8));
                break;
            }
            case SENDER_STATUS:
                switch (action)
                {
                    case STATUS_LEAVE:
                        report(sRfManager.LeaveQueue(player));
                        Open(player);
                        break;
                    case STATUS_ACCEPT:
                        report(sRfManager.Accept(player));
                        ShowStatus(player);
                        break;
                    case STATUS_DECLINE:
                        report(sRfManager.Decline(player));
                        Open(player);
                        break;
                    case STATUS_CLOSE:
                        CloseGossipMenuFor(player);
                        break;
                    default:
                        ShowStatus(player);
                        break;
                }
                break;
            case SENDER_INFO:
            default:
                Open(player);
                break;
        }
        return true;
    }
}

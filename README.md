# mod-playerbot-raid-finder

A Dungeon-Finder-style **Raid Finder** for [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) with [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Players queue for a raid as tank, healer or DPS. Real players fill the raid first. After a configurable delay, the empty seats are filled with existing **Random Playerbots** of the right level, role and gear. The module then forms the raid, teleports the real leader in first, and brings the bots in batches. Inside, the bots use the raid strategies that mod-playerbots already ships. When the raid ends or something fails, the bots go back to their normal random-bot life.

- Classic, The Burning Crusade and Wrath raids: 33 definitions, plus your own rows.
- Role composition per raid, a bot level band and a minimum average item level.
- Three ways to use it: a WotLK-style client addon, a gossip menu (`.rf`), and chat commands.
- No patches to AzerothCore core or to mod-playerbots; public APIs and script hooks only.

## Contents

- [Requirements](#requirements)
- [Installation](#installation)
- [Client addon](#client-addon)
- [Testing](#testing)
- [Commands](#commands)
- [Configuration](#configuration)
- [Raid definitions](#raid-definitions)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)

## Requirements

- AzerothCore WotLK (recent `master`), built from source.
- mod-playerbots installed in the same source tree, with random bots enabled:
  - `AiPlayerbot.RandomBotAutologin = 1`
  - a non-zero `AiPlayerbot.MinRandomBots` / `MaxRandomBots`
- MySQL / MariaDB with the world DB updater enabled (default). Without it, the SQL must be applied by hand.
- WoW 3.3.5a client (build 12340), only if you want the addon.

## Installation

### 1. Get the module

```bash
cd <azerothcore-source>/modules
git clone https://github.com/ONeal182/mod-playerbot-raid-finder.git
```

The folder must be named `mod-playerbot-raid-finder`, next to `mod-playerbots`.

### 2. Build

Re-run CMake so the new module is picked up, then rebuild `worldserver`:

```bash
cd <azerothcore-source>/build
cmake ..
cmake --build . --config RelWithDebInfo --target worldserver
```

On Linux use your usual `make -j$(nproc)` / `ninja` build instead.

### 3. Configuration file

Copy the config next to your other module configs:

```bash
cp modules/mod-playerbot-raid-finder/conf/mod_playerbot_raid_finder.conf.dist <server>/configs/modules/
cp <server>/configs/modules/mod_playerbot_raid_finder.conf.dist <server>/configs/modules/mod_playerbot_raid_finder.conf
```

The defaults work as they are. See [Configuration](#configuration).

### 4. Database

The world SQL lives in `data/sql/db-world/`:

| file | what it does |
|---|---|
| `rf_raid_finder_definition.sql` | Creates `raid_finder_definition` and the first 8 WotLK raids |
| `rf_raid_finder_definition_2_all_tiers.sql` | Adds the `max_level` column and the Classic, TBC and other WotLK raids |
| `rf_raid_finder_definition_3_ru_names.sql` | Russian raid names |
| `rf_raid_finder_definition_4_item_level.sql` | Minimum average item level per raid |

- **Automatic (recommended).** With `Updates.EnableDatabases` including the world DB (the default), the updater applies these files on the next `worldserver` start.
- **Manual.** Apply the four files in the order above to your world database.
- **No table at all.** The module then falls back to the same built-in list of 33 raids and logs a warning.

`rf_raid_finder_definition_3_ru_names.sql` is optional. To keep English raid names, delete that file before the first start.

### 5. Start and check

Start `worldserver`. The log shows one line per raid:

```
RaidFinder: raid ICC10 'Icecrown Citadel 10' map 631 diff 0 comp 2/2/6 lvl 80-0 minGear 232
```

A raid that fails validation is logged as `RaidFinder: raid definition <ID> disabled: <reason>` and stays hidden.

## Client addon

The addon `PlayerbotRaidFinder` is the main player UI. It talks to the server over addon messages; no client patch and no `WoW.exe` changes are needed.

### Install

1. Copy the folder `addon/PlayerbotRaidFinder` into your client:
   `World of Warcraft/Interface/AddOns/PlayerbotRaidFinder`
2. The folder must contain `PlayerbotRaidFinder.toc` directly (not a nested folder).
3. Start the game, or fully restart it if it was running. A `/reload` does not pick up a newly added addon or new files.
4. On the character screen, open **AddOns** and make sure **Playerbot Raid Finder** is enabled. Tick *Load out of date AddOns* if the client marks it as out of date.

The UI language follows the client: a `ruRU` client gets Russian, every other locale gets English.

### Open the window

Any of these:

- type `/rf` (or `/raidfinder`);
- click the minimap button (dragon head icon);
- open the stock **Dungeon Finder** (key `I`) and press the **Raid Finder** button in its top-right corner.

Minimap button collectors such as HidingBar, SlideBar or MBB may move the minimap button into their own bar. The Dungeon Finder button and `/rf` always work. `/rf button` puts the minimap button back to its default spot.

### Queue for a raid

1. Pick the expansion and the raid in the **Raid** drop-down. Each raid shows its minimum average item level.
2. Pick a role: **Tank**, **Heal** or **DPS**.
3. Check the line *Required avg item level: N+ (yours: M)*. It turns red when your gear is too low, and **Find Raid** stays disabled together with the reason.
4. Press **Find Raid**.

While you wait, the window shows the state (*Searching*, *Filling with Playerbots*), a progress bar per role, the number of real players and bots, and your queue time. **Leave Queue** takes you out.

### Proposal

When the raid is complete, a **RAID FOUND** popup appears with the raid, its size and the composition, plus a countdown.

- **Accept** — you are in. The popup then lists the other real players and whether they have accepted; bots accept automatically.
- **Decline** — you leave the queue. If the leader declines, the session is cancelled and the other players keep their place in the queue.

After everyone accepts, the raid group is created and you are teleported in (the leader first, then the others). The window switches to *In Raid* and shows the final composition.

### Leaving

Use **Leave Raid**, leave the group, or let the leader disband it. You are sent back to where you stood before you entered, like the Dungeon Finder does. The bots are released at the same time.

### Keyboard commands

| command | does |
|---|---|
| `/rf` | open / close the window |
| `/rf join ICC10 dps` | queue without the window |
| `/rf leave` | leave the queue or the raid |
| `/rf accept`, `/rf decline` | answer a proposal |
| `/rf button` | reset the minimap button position |

The addon state is rebuilt from the server after `/reload`, relog or reconnect.

### Without the addon

Type `.rf` to open the same flow as a gossip menu: expansion → raid → role → **Join Queue**, then a status page with **Refresh**, **Accept** / **Decline** and **Leave**. Players without the addon get the proposal popped up as a gossip menu automatically.

## Testing

### Unit tests

When the core is configured with `BUILD_TESTING=ON`, the module adds a stand-alone test target `raid_finder_tests`. It covers the composition planner (hybrid roles, shortages, caps, ranged/melee balance) and the bot reservation manager, and links only gtest, so it builds in seconds:

```bash
cmake --build build --config RelWithDebInfo --target raid_finder_tests
./build/RelWithDebInfo/raid_finder_tests      # Windows: build\RelWithDebInfo\raid_finder_tests.exe
```

`ctest -R raid_finder` runs the same suite.

### In-game smoke test (GM)

1. **Check the raid list.** `.rf list` shows every definition with its validation state.
2. **Check the bot pool.** Log in a GM character that is not in a group and run `.rf bots ICC10`. The output is a funnel (online → level → faction → free → lockout → role → item level), the eligible tanks / healers / DPS, and a counter per reject reason. For ICC10 you need at least 2 tanks, 2 healers and 5 DPS of your faction. `.rf bots` also works from the server console (faction = any).
3. **Force a raid.** `.rf force ICC10 dps` treats you as the leader and a DPS, reserves bots for the remaining 9 seats, creates the raid and prints the composition. With `AutoTeleport = 1` you enter ICC first and the bots follow in batches. The bots switch to the ICC playerbot strategy on arrival.
4. **Clean up.** Leave or disband the group, or run `.rf clear`. Bots are removed, teleported out and released; `.rf sessions` must then be empty and `.rf bots ICC10` must show no `reserved` bots.

### Full player flow

1. As a normal player (level and item level high enough), open the addon with `/rf`, choose a raid and a role, and press **Find Raid**. Or use `.rf join ICC10 dps`.
2. Wait `RaidFinder.BotFillDelay` seconds (30 by default). The state changes to *Filling with Playerbots* and the role bars fill up.
3. Accept the **RAID FOUND** proposal. The raid is created with you as leader, you are teleported in, then the bots.
4. Leave the raid: you are returned to where you stood, and the bots are released.

### Not enough bots?

`.rf bots <raid>` tells you why. The usual reasons and fixes:

| reject reason | fix |
|---|---|
| `wrong_level` | Not enough random bots in the raid's level band. Wait for more bots to log in, or use `.rf promote`. |
| `low_item_level` | Bots are under the raid's minimum average item level. Use `.rf promote` to re-gear them, or lower `min_gear`. |
| `wrong_role` | Not enough bots with a tank or healer talent spec. `.rf promote` assigns role specs. |
| `already_grouped`, `busy`, `in_instance` | Bots are doing other things; they become free over time. |
| `lockout` | Bots are saved to that raid this week. |

`.rf promote` (administrator only) prepares a test pool. It takes online random bots, sets them to a level, forces a role spec and re-gears them with gear ceilings spread evenly over an item-level range. Bots that are already at the target level but under-geared are upgraded first:

```
.rf promote <tanks> <healers> <dps> <alliance|horde|both> <minIlvl> <maxIlvl> <level>
.rf promote 6 12 30 both 240 275 80     # ICC / Ruby Sanctum ready pool
.rf promote 3 4 0 both 60 78 60         # extra tanks and healers for Classic 40-man raids
```

It works in small batches (one bot every 0.5 s) and writes one log line per bot. Note that other bot-management modules, for example level-bracket or level-reset modules, may move promoted bots out of their level again.

## Commands

| command | access | does |
|---|---|---|
| `.rf` | player | Open the gossip Raid Finder |
| `.rf join <raid> <tank\|heal\|dps>` | player | Queue for a raid |
| `.rf leave` | player | Leave the queue, or the raid once inside |
| `.rf accept` / `.rf decline` | player | Answer a proposal |
| `.rf status` | player | Show your queue / session state |
| `.rf list` | player | Raid definitions and their validation state |
| `.rf bots <raid>` | GM | Bot pool diagnostics (funnel, eligible roles, reject counters) |
| `.rf force <raid> [role]` | GM | Build a raid at once: you plus bots, no proposal |
| `.rf queue` / `.rf sessions` | GM | Inspect queued players and sessions with their members |
| `.rf release <sessionId>` | GM | Release one session |
| `.rf clear` | GM | Release every session |
| `.rf debug on\|off` | GM | Verbose selector / addon tracing at DEBUG level |
| `.rf promote ...` | admin | Prepare a geared bot pool (see [Testing](#testing)) |
| `.rf reload` | admin | Re-read config values and raid definitions |

## Configuration

All keys live in `mod_playerbot_raid_finder.conf` and are described in the `.conf.dist` file.

| key | default | meaning |
|---|---|---|
| `RaidFinder.Enable` | 1 | Master switch |
| `RaidFinder.BotFillEnabled` | 1 | Fill empty seats with random bots |
| `RaidFinder.BotFillDelay` | 30 | Seconds real players get before bots are added |
| `RaidFinder.MinRealPlayers` | 1 | Real players needed before bot fill starts |
| `RaidFinder.MaxBotsPerRaid` | 39 | Upper limit of bots in one raid (39 = a 40-man raid with one player) |
| `RaidFinder.AutoTeleport` | 1 | Teleport the raid in after accept. With 0 the leader walks in and the others follow |
| `RaidFinder.QueueTimeout` | 1800 | Seconds a forming raid may search (0 = never time out) |
| `RaidFinder.ProposalTimeout` | 45 | Seconds to accept a proposal |
| `RaidFinder.EnterTimeout` | 120 | Seconds a member may take to arrive inside |
| `RaidFinder.AbandonTimeout` | 600 | Seconds with no real player inside before the raid is released (0 = never) |
| `RaidFinder.RequireRoleBalance` | 1 | 1 = exact tank/healer/DPS split; 0 = any eligible bot may take a leftover seat |
| `RaidFinder.PreferRealPlayers` | 1 | Wait `BotFillDelay`, and let a joining player replace a reserved bot |
| `RaidFinder.SameFactionOnly` | 1 | Bots must match the leader's faction |
| `RaidFinder.AllowCrossFaction` | 0 | Real players of both factions may be matched together |
| `RaidFinder.TeleportBotsPerTick` | 3 | Bots teleported per batch |
| `RaidFinder.TeleportTickMs` | 1000 | Milliseconds between batches |
| `RaidFinder.StatusUpdateInterval` | 5 | Seconds between status pushes to the addon |
| `RaidFinder.GossipEnabled` | 1 | `.rf` gossip menu; proposal popup for players without the addon |
| `RaidFinder.AddonEnabled` | 1 | Answer the client addon |
| `RaidFinder.Debug` | 0 | Verbose tracing |

## Raid definitions

Each raid is one row in the world table `raid_finder_definition`. To add or change a raid, edit the table and run `.rf reload`.

| column | meaning |
|---|---|
| `id` | Stable id used by commands and the addon, 1–16 chars `[A-Z0-9_]`, e.g. `ICC10` |
| `display_name` | Name shown to players |
| `map_id` | Raid map id |
| `difficulty` | 0 = 10 normal, 1 = 25 normal, 2 = 10 heroic, 3 = 25 heroic |
| `min_level` | Minimum level for players and bots; it also picks the menu tier (< 70 Classic, < 80 TBC, else WotLK) |
| `max_level` | Highest level a **bot** may have (0 = no cap). Players only need `min_level` |
| `min_gear` | Minimum **average item level** for players and bots (0 = off). Same formula as the core's `dungeon_access_template.min_avg_item_level`: empty slots count as 0 |
| `tank_count`, `healer_count`, `dps_count` | Composition; their sum is the raid size |
| `enabled` | 0 hides the raid |
| `experimental` | 1 = no dedicated playerbot strategy; bots use generic AI there |
| `sort_order` | Menu order |

Rows are validated on load: the map must be a raid, the difficulty must exist for it, the size must fit the map, and the map needs an entrance areatrigger. Invalid rows are disabled and logged.

Default raids:

| tier | raids | bot levels | composition | min avg ilvl |
|---|---|---|---|---|
| Classic | MC, BWL, AQ40, ONY40 | 60–69 | 4 / 10 / 26 | 55–66 |
| Classic | ZG, AQ20 | 60–69 | 2 / 5 / 13 | 55–58 |
| TBC | KARA, ZA | 70–79 | 2 / 3 / 5 | 105–115 |
| TBC | GRUUL, MAG, SSC, TK, HYJAL, BT, SWP | 70–79 | 3 / 6 / 16 | 112–141 |
| WotLK | VOA, NAXX, OS, EOE, ONY, ULD, TOC, ICC, RS (10 and 25) | 80 | 2 / 2 / 6 and 2 / 5 / 18 | 180–258 |

ZG, AQ40, Sunwell, Trial of the Crusader and Ruby Sanctum are marked experimental. ONY40 needs a core or module that provides Onyxia difficulty 2; without it the row is disabled automatically.

## How it works

1. A player joins a raid queue with one role. Players are grouped into a forming *session*. A player who joins can take a seat that a reserved bot holds in the same role.
2. After `BotFillDelay`, the **bot selector** scans the online random bots. Checks run in a fixed order, and the first failed check is the reject reason that `.rf bots` reports: online, random bot, level band, faction, not grouped, not in arena / battleground / LFG / an instance, not busy, not reserved, lockout, role by talent spec, item level.
3. The **composition planner** assigns bots to the open tank / healer / DPS seats with bipartite matching. Single-role bots go first, so a hybrid (for example a Prot/Holy paladin) is never spent on a seat a single-role bot could fill. DPS picks alternate between ranged and melee. The chosen bots are **reserved**: one bot belongs to at most one session.
4. Real players get the proposal; bots accept automatically.
5. On accept, every member is validated again. The raid group is created with a **real player as leader** (never a bot), and members are added.
6. The leader is teleported first. Once the leader is inside a valid instance, the other members follow in batches, so everyone lands in the leader's instance.
7. On arrival mod-playerbots switches on its own raid strategy for the map.
8. **Release** is the single cleanup path for every end and every error: reservations are freed, bots leave the group and are teleported out, players are sent back to where they entered from, and the session is removed. Release is idempotent.

Session states:

`Searching → FillingBots → Proposal → Assembling → TeleportLeader → WaitingLeaderMapChange → TeleportBots → InRaid → Releasing → Done`

Script hooks only queue events (bot AI can leave a group on a map thread); all session work runs on the world thread. Group and teleport operations done by the module are ignored by its own hooks, so cleanup never re-enters cleanup.

### Addon protocol (V1)

Client → server messages use the prefix `RFC` (whisper to self); server → client use `RF`. The client can only name a raid id and a role. Every request is validated on the server, and sessions, bots and teleports are never addressable from the client. The message list is in `src/RfAddonProtocol.h`.

### Logs

All categories are children of `module`, so they print wherever your `module` logger prints:

`module.raidfinder.queue`, `.match`, `.selector`, `.reserve`, `.composition`, `.group`, `.proposal`, `.teleport`, `.addon`, `.cleanup`, `.error`.

## Troubleshooting

| symptom | check |
|---|---|
| `Unknown raid` / raid missing in the menu | `.rf list`; look for `raid definition ... disabled` in the server log |
| Stuck in *Filling with Playerbots* | `.rf bots <raid>` and the reject counters; see [Not enough bots?](#not-enough-bots) |
| **Find Raid** greyed out | Your level or average item level is below the raid's requirement (shown in red) |
| Addon window does not open | Fully restart the client after installing; check the AddOns list; try the Dungeon Finder button or `.rf` |
| Minimap button missing | A button-collector addon took it; use `/rf button`, `/rf` or the Dungeon Finder button |
| `V1:ERROR:BAD_VERSION` in chat | Addon and server module are from different versions; update both |

## Known limitations

- Only random bots that are already online are used; the module never logs bots in.
- Bots keep their weekly raid lockouts, which shrinks the pool for that raid during the week.
- Raids marked experimental have no dedicated playerbot strategy.
- Solo queue only: players in a group cannot queue.
- If the leader leaves before the raid starts, the session is cancelled and the other players are re-queued with their original queue time. Inside the raid, leadership moves to another real player.
- Bots are not re-geared on the fly; use `min_gear` and `.rf promote`.

## License

Released under the GNU General Public License v2, the same license as AzerothCore (see `LICENSE`).

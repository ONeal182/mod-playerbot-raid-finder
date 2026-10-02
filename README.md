# mod-playerbot-raid-finder

A Dungeon-Finder-style raid queue for AzerothCore with mod-playerbots.

Players queue for a raid as tank, healer or DPS. Real players fill the raid first. After `BotFillDelay` seconds, the module fills the remaining seats with existing **Random Playerbots**. It then forms the raid and teleports it into the instance. When the raid ends or something fails, the bots go back to their normal random-bot life.

The module needs no changes to AzerothCore core or to mod-playerbots. It uses only public APIs and script hooks.

## Flow

1. The player opens the Raid Finder (`/rf` with the addon, or `.rf` for the gossip menu).
2. The player picks a raid and a role, then clicks **Find Raid**.
3. The queue groups real players into a forming session. A real player who joins can take a seat that a reserved bot holds in the same role.
4. After `BotFillDelay`, the bot selector picks eligible online random bots. The composition planner assigns them to the open role seats. The chosen bots are reserved.
5. Only real players see the **Raid Found** proposal. Bots accept automatically.
6. When everyone has accepted:
   - The bots are validated again.
   - The raid group is created with a real player as leader.
   - The real players join the group, then the bots.
7. The leader is teleported first. Once the leader is inside a valid instance, the other members follow in batches (`TeleportBotsPerTick`).
8. When the bots arrive, mod-playerbots switches on its own raid strategies (`ApplyInstanceStrategies`):
   - Naxxramas, Ulduar and ICC have dedicated strategies.
   - Trial of the Crusader has none. It is marked *experimental*, and bots use the generic AI there.
9. When the raid ends, the bots are released:
   - Release triggers: group disband, no real player left, abandon timeout, or a GM release.
   - Released bots leave the group and are teleported out.
   - Their reservations are freed, and playerbots resets them.

Release is a single, idempotent cleanup path. Every timeout and every error path ends there.

### Session states

`Searching → FillingBots → Proposal → Assembling → TeleportLeader → WaitingLeaderMapChange → TeleportBots → InRaid → Releasing → Done`

## Installation

1. Put the module in `modules/mod-playerbot-raid-finder`. mod-playerbots must be present.
2. Re-run CMake and rebuild `worldserver`.
3. Copy `conf/mod_playerbot_raid_finder.conf.dist` to your server's `configs/modules/` directory. Rename the copy to `mod_playerbot_raid_finder.conf` if you want to change settings.
4. Set up the database:
   - **Automatic:** with `Updates.EnableDatabases` including world (the default), the DB updater applies `data/sql/db-world/rf_raid_finder_definition.sql` on the next start.
   - **Manual:** apply that same file to your world database.
   - The second file `rf_raid_finder_definition_2_all_tiers.sql` adds the `max_level` column and the Classic, TBC and remaining WotLK raids.
   - **Without the table:** the module uses the same built-in list (33 raids):
     - Classic, bots 60-69: MC, BWL, ZG, AQ20, AQ40, ONY40.
     - TBC, bots 70-79: KARA, ZA, GRUUL, MAG, SSC, TK, HYJAL, BT, SWP.
     - WotLK, bots 80: VOA, NAXX, OS, EOE, ONY, ULD, TOC, ICC, RS, each 10 and 25.
5. **Optional:** install the client addon. Copy `addon/PlayerbotRaidFinder` to `World of Warcraft/Interface/AddOns/`.
   - Open it with `/rf` or with the minimap button (drag the button to move it).
   - The addon follows the client locale: a ruRU client gets the Russian UI. Player-facing server messages, gossip and raid names are in Russian.
   - Leaving the Raid Finder raid (leave, kick, disband or release) sends a player back to where they stood before entering, like the Dungeon Finder.

### Adding a raid

Insert one row into `raid_finder_definition`, then run `.rf reload`.

| column | meaning |
|---|---|
| `id` | Stable protocol id, 1–16 chars `[A-Z0-9_]`, e.g. `ICC10H` |
| `display_name` | Name shown in the UI |
| `map_id` | Raid map id |
| `difficulty` | 0 = 10N, 1 = 25N, 2 = 10H, 3 = 25H |
| `min_level` | Minimum level (players and bots) |
| `max_level` | Highest level a bot may have (0 = no cap). Real players only need `min_level` |
| `min_gear` | Minimum **average item level** (`Player::GetAverageItemLevel`, same metric as `dungeon_access_template.min_avg_item_level`). Checked for players on join and again at raid assembly, and for every bot. 0 disables the check |
| `tank_count`, `healer_count`, `dps_count` | Composition |
| `enabled` | 0 hides the raid |
| `experimental` | 1 if there is no dedicated playerbot strategy |
| `sort_order` | Menu order |

Each row is validated at load. An invalid row is disabled and logged. Checks:

- The map is a raid map.
- The difficulty exists for that map.
- The size fits the map's player limit.
- An entrance areatrigger exists for the map.

## Commands

| command | who | |
|---|---|---|
| `.rf` | player | Open the gossip Raid Finder |
| `.rf join <raid> <tank\|heal\|dps>` | player | Queue |
| `.rf leave` | player | Leave the queue, or the raid once inside |
| `.rf accept` / `.rf decline` | player | Answer a proposal |
| `.rf status` | player | Show your state |
| `.rf list` | player | Raid definitions, with the validation state of each |
| `.rf bots <raid>` | GM | Bot pool diagnostics: funnel, eligible roles, reject counters |
| `.rf force <raid> [role]` | GM | Build a raid immediately: you plus bots, no proposal |
| `.rf promote [t] [h] [d] [faction] [minIlvl] [maxIlvl] [level]` | admin | Dev tool: re-roll online random bots to `level` with a forced role spec and gear ceilings spread evenly over the item-level range. Bots already at `level` but below `minIlvl` are upgraded first |
| `.rf queue` / `.rf sessions` | GM | Inspect queue and sessions |
| `.rf release <sessionId>` / `.rf clear` | GM | Release one session or all |
| `.rf debug on\|off` | GM | Verbose selector/addon tracing |
| `.rf reload` | admin | Reload config values and raid definitions |

### First test

1. Log in a level 80 GM character with no group. Random bots must be online (`AiPlayerbot.RandomBotAutologin = 1`).
2. Run `.rf bots ICC10`.
   - Check the eligible tanks, healers and dps. You need at least 2 tanks, 2 healers and 5 dps.
   - If the pool is too small, the reject counters show why.
3. Run `.rf force ICC10 dps`.
   - The command reserves 9 bots, creates the raid with you as leader and prints the composition.
   - With `AutoTeleport = 1` you are teleported into ICC first, and the bots follow in batches.
4. Leave the group, disband it, or run `.rf clear`. The bots leave, get teleported out, and return to random-bot life.

To test the normal flow, use `.rf join ICC10 dps`, or the gossip menu, or `/rf` in the addon. Wait `BotFillDelay` seconds, then accept the proposal.

## Configuration

See `conf/mod_playerbot_raid_finder.conf.dist`. Main keys:

- `Enable`
- `BotFillEnabled`
- `BotFillDelay`
- `MinRealPlayers`
- `MaxBotsPerRaid`
- `AutoTeleport`
- `QueueTimeout`
- `ProposalTimeout`
- `EnterTimeout`
- `AbandonTimeout`
- `RequireRoleBalance`
- `PreferRealPlayers`
- `SameFactionOnly` (applies to bots)
- `AllowCrossFaction` (applies to real players)
- `TeleportBotsPerTick`
- `TeleportTickMs`
- `StatusUpdateInterval`
- `GossipEnabled`
- `AddonEnabled`
- `Debug`

## Bot selection

Only online Random Playerbots are considered. The module never creates bots and never makes them owned bots.

The selector runs its checks in a fixed order. The first check that fails becomes the reject reason, which `.rf bots` reports:

| order | reject reason | what it checks |
|---|---|---|
| 1 | `offline` | The bot is online |
| 2 | `not_random_bot` | The bot is a random bot |
| 3 | `wrong_level` | Level |
| 4 | `wrong_faction` | Faction |
| 5 | `already_grouped` | Not already in a group |
| 6 | `in_arena` | Not in an arena |
| 7 | `in_battleground` | Not in a battleground or a battleground queue |
| 8 | `in_lfg` | Not in the Dungeon Finder |
| 9 | `in_instance` | Not inside an instance |
| 10 | `busy` | Not dead, in flight, teleporting, on a vehicle, or in combat |
| 11 | `reserved` | Not reserved by another session |
| 12 | `lockout` | Raid lockout is compatible |
| 13 | `wrong_role` | Role by talent spec fits the needed seats |
| 14 | `low_gear` | Gear score |

Before each `Group::AddMember` the bot is validated again. At that point combat is tolerated.

Role assignment uses bipartite matching. Single-role bots are placed first, and higher gear wins among bots of the same role. A hybrid (for example a Prot/Holy paladin) is never spent on a seat that a single-role bot can fill. DPS picks alternate between ranged and melee.

## Addon protocol (V1)

- Client to server: prefix `RFC`, whisper to self.
- Server to client: prefix `RF`.
- The full message list is in `src/RfAddonProtocol.h`.

The server treats the addon as untrusted. The client can only name a raid id and a role, and the server validates every request: raid, role, membership, proposal ownership, level, gear and lockout. The client cannot address sessions, bots or teleports.

## Logging

All log categories are children of `module`:

- `module.raidfinder.queue`
- `.match`
- `.selector`
- `.reserve`
- `.composition`
- `.group`
- `.proposal`
- `.teleport`
- `.addon`
- `.cleanup`
- `.error`

## Tests

The `raid_finder_tests` target covers the composition planner (hybrid assignment, shortages, caps, ranged/melee balance) and the reservation manager. It links only gtest. To build and run it:

```
cmake --build build --config RelWithDebInfo --target raid_finder_tests
build/RelWithDebInfo/raid_finder_tests.exe
```

## Known limitations

- **Online bots only.** The MVP uses only random bots that are already online. Logging in offline random bots would need a small public helper in mod-playerbots (`currentBots` is private).
- **Lockouts stay on bots.** A bot that kills a boss keeps the weekly lockout, as normal playerbots do. Over a week this shrinks the bot pool for that raid; `.rf bots` shows it as `lockout`.
- **ToC has no playerbot raid strategy.** Bots there use generic tank/heal/dps AI.
- **Solo queue only.** Players who are already in a group cannot queue.
- **No leader hand-off before the raid starts.** If the leader leaves before `InRaid`, the session is released and the other real players are re-queued with their original queue time. Inside the raid, leadership moves to another real player.
- **Bot gear is not upgraded.** Random bots keep whatever gear the playerbots factory gave them. Use `min_gear` to filter on it.

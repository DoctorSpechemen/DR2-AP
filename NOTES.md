# DR2AP research notes

Findings from the 2026-11-15 session. Everything here was observed on the Steam build of
Dead Rising 2 (`deadrising2.exe`, image base `0x00400000`, no address randomization seen).
Addresses are absolute; subtract `0x400000` for an offset into the image.

Nothing below has been checked against any other build of the game. Before release, the fixed
addresses should be replaced with pattern searches, the way ClownDR2Fix finds its hook points.

## Game data (`data/datafile.big`)

- Archive format: header (`magic, ?, file size, entry count, index offset, names offset`), then
  28-byte entries (`name offset, hash, packed size, unpacked size, data offset, alignment 0x800, flag`).
  Each packed file is a 4-byte big-endian unpacked size followed by a zlib stream (window 4096).
- `tools/unpack.py` and `tools/repack.py` in this folder handle it (Python 3, no extra packages).
  An unmodified round trip is byte-identical.
- `missions.txt` is the script language: 324 mission definitions. `items.txt` defines every item.
- Confirmed in game: item placement edits (drill bucket in `safehouse.txt`) apply to existing saves.
  Mission script edits (pawnshop price) apply only to a game where that script has not run yet.
- Pawnshop stock and prices: `cMissionPawnShopAddItem` entries in `PawnShop_1` to `PawnShop_4`.
- Changing `PawnShopScoop`'s `StartTime` lets the shop open but loses the phone call. Leave it alone.
- Each save slot has its own level, PP and money. Costume unlocks are NOT per slot (see below).

## Player stats block

Route: `[0x00DDE9A8]` -> manager; `[manager + 0x8]` -> stats block. The block starts with the
marker `0x00CB5880`. The route held across restarts and across loading a save.

| Offset | Type | Meaning |
|---|---|---|
| +0x08 .. +0x1F | bytes | Costume unlock switches, index 0 to 23 (knight beard 9, knight boots 11) |
| +0x20 | int | Probably level |
| +0x24, +0x28 | int | PP (kept twice) |
| +0x2C | float | Health, 100 per block. Writing it works and the HUD follows |
| +0x38 | int | Zombie kills |
| +0x70 | int | Money |
| +0x74, +0x78, +0x7C | int | Money earned / spent / earned from one source (unconfirmed) |

Loading a save restores this block in place. Writing 0 to health kills Chuck after about three
seconds, with an odd animation; a proper kill should go through the damage code.

## Game routines (call on the main thread)

`this` is passed in `ecx`. All of these clean up their own arguments.

| Address | Call | Notes |
|---|---|---|
| `0x00842060` | `ChangeMoney(this=stats, amount, setInsteadOfAdd, sourceType)` | Updates the HUD. Confirmed |
| `0x00845EC0` | `AwardPP(this=stats, amount, labelTextId, flag, flag)` | Adds PP. No popup seen when called by us |
| `0x00845FD0` | `AwardPPSimple(this=stats, isSurvivorBonus, amount)` | Wrapper around the above |
| `0x00842CD0` | `GiveKeyItem(this=stats, itemId, 0)` | Confirmed with Zombrex (85001) |
| `0x00842DB0` | `(this=stats, itemId)` | Next to GiveKeyItem in the script code; probably removes a key item. Untested |
| `0x00842F60` | `SetUnlock(this=stats, index, on)` | Writes the costume unlock switch. Untested |
| `0x00636DA0` | `WearClothing(player, clothingDefinition, flag)` (caller cleans up) | Confirmed: naked outfit trap |
| `0x004187B0` | `Transaction(this, player?, amount, 0)` | Used by shops and pickups |
| `0x0040B190` | `SendMessage(this=messenger, message*)` | The game's internal messenger |
| `0x004511D0` | `OnHealthChanged(this, player, old, new)` | Only announces; does not change health |

- Script action handlers for `cMissionSetChuckState` sit around `0x0070AD60` to `0x0070AE60`,
  one short block per state. `GiveKeyItem` was found there. The rest are unmapped and are the
  likely route to the remaining traps.
- Main-thread hook point: the call at `0x008C4FBB` (pattern `E8 ? ? ? ? ? ? ? ? 8B 0D ? ? ? ? 83 C4 ? 57`),
  the same one ClownDR2Fix uses to poll controllers.
- Graphics device: pattern `A3 ? ? ? ? A3 ? ? ? ? C3 33 C0` gives the graphics object pointer;
  the Direct3D 9 device is at `+0x198`. The message box hooks `Present` (slot 17).

## Player object

- Seen at `0x28FC0CE0` this session (changes every launch). It appears as a value in many messages.
- `[player + 0xD82C]` -> a data object used by the damage code.
- Clothing definitions are separate objects; their addresses also change every launch and need
  to be looked up by name.

## Internal messages

A message is `[+0] 0x00C81854, [+4] kind, [+8...] data`. Listening at `0x0040B190` sees them all.

| Kind | Data | Meaning | Confidence |
|---|---|---|---|
| 0x06 | | Player died | Seen twice, both deaths |
| 0x0B | amount | Money transaction: negative paid, zero free pickup, positive gained | Confirmed |
| 0x13 | key item id | Key item received (85001 Zombrex, 85017 Underground Access Key, 85019 Maintenance Room Key) | Confirmed |
| 0x15 | text | `"<mission DisplayName id> Activated"` / `"... Completed"` | Confirmed |
| 0x1A | player, old, new | Health changed (floats) | Confirmed |
| 0x2C | piece name | Clothing piece put on, one message per body slot | Confirmed |
| 0x5F | label id, amount | PP awarded. Label 2930 = survivor joined (5,000), 3104 = survivor rescued (10,000) | Confirmed |
| 0x61 | amount | Money changed | Confirmed |
| 0x62 | text id, ?, level | 3102 = level up (third value is the new level), 3146 = combo card acquired | Seen once each |
| 0x63 | survivor record, status | Survivor event: status 0 joined, 2 rescued. Name (`srv_...`) is text at record `+0x18` | Confirmed |
| 0x6B | text | A name, e.g. `rebecca_chang`, `central_security`; looks like cutscenes or notebook entries | Guess |
| 0x6E | small numbers | Appeared at two case completions | Guess |
| 0x43, 0x47 | | Appeared when a save was loaded after dying | Seen once |

Mission ids come from `DisplayName` in `missions.txt` (Case 1-1 = 81551, 1-2 = 81552, 1-3 = 81553,
1-4 = 81554). Missions without a `DisplayName` (e.g. `Zombrex1_Survivor`) do not announce this way.
Some missions share an id with their own sub-steps and fail variants; whether "Completed" fires
once per real completion is not yet confirmed. No "Failed" text has been seen yet.

## Open questions

- A natural-looking kill for DeathLink (damage routine instead of writing health).
- Looking up the player and clothing definitions by name at startup.
- Mapping the other `cMissionSetChuckState` handlers (drop item, etc.) for traps.
- Spawning zombies, looters and teleporting on demand.
- Combo cards, area keys, health and inventory upgrades as items.
- Psychopaths, combo weapons built, magazines, PP level-ups as checks.
- A save-stored counter for resyncing money/PP/Zombrex after a reload.
- Holding costume unlocks off until received (they carry over between save slots).
- Custom text for the "AP is calling..." trap (`data/frontend/str_en.bcs`).

## Message kinds are not unique

Several messengers share the one send routine, and their kind numbers overlap: kind 0x15 also arrives from
`exe+0x53226` carrying unrelated data. The plugin therefore checks what a message carries (the text, the
player object, a `srv_` name, the sender's address) and never trusts the kind number alone.

## The mission script can do most of what the mod needs (found 2026-11-15, from `missions.txt`)

`cMissionSetChuckState` values seen in the game's own missions:

| State | Value | Does |
|---|---|---|
| 7 | | Full health |
| 9 | amount | Give money |
| 10, 24, 73 | amount | Give PP (different popups) |
| 18 | | Drop the held item |
| 19 | card id | Give a combo card (ids seen: 36, 42, 44, 45, 46, ...) |
| 20 | key item id | Give a key item |
| 35 | | Empty the inventory |
| 58 | | Survivor mutiny (not wanted) |

Other actions that exist in the script language and so have handlers in the game: `cMissionLoadLevel`
(`Level`, `LevelSpawnPoint`: moves the player to a named entrance of another area), `cMissionTeleportPlayer`,
`cMissionSpawnZombie` (`ZombieType` may be `looter`), `cMissionAdjustZombieTuning`, `cMissionPhoneCall`,
`cMissionSetClock`, `cMissionPauseClock`, `cMissionSetMissionState`, `cMissionPrereq` with
`cMissionObjectiveEvent` (a mission can wait for a named event).

Plan that follows from this: the mod adds its own mission (`AP_Bridge`, appended to `missions.txt` by
`tools/build_ap_mission.py`) holding one action per trap, each behind a named event (`AP_Drop`, `AP_Rage`,
`AP_Call`, `AP_Horde`, `AP_Looters`, `AP_Tele1` to `AP_Tele7`). The plugin then needs only one thing from the
game: a way to send a named event, or to run one of those action objects directly. Case gates can use the same
idea (a case's countdown mission waits for an event such as `AP_Case2`).

`AP_Bridge` also gives $777 forty seconds after it starts. That is a test (does a mission added by the mod run,
also on an existing save?) and must be removed before release.

## Text table

`data/frontend/str_en.bcs`: `uint32 count`, ids (ascending), offsets, zero-ended UTF-8 texts, zero padding.
`tools/strings_tool.py` rebuilds it byte for byte and can add texts. Added for the phone trap: 88801 "AP",
88802 "Sorry, wrong number.".

## Survivors and psychopaths

`cMissionObjectiveNPCJoin` in a survivor mission names the survivors that belong to it (internal names such as
`srv_esther`); `cSurvivorItem` in `items.txt` gives each one's display name. The world's `Data.py` lists all 65
with the clock time their mission opens. Some survivors exist in several outfits (`srv_jack_0` ... `srv_jack_4`);
the plugin treats them as one person.

## Core 1.10 (the Archipelago link)

See the top of `Ap.inc`. Confirmed in game on 2026-11-15: item delivery, kill / level / case / survivor /
Zombrex checks, DeathLink both ways, Naked trap, Wrong Number call, the zombie spawn, the follower count.

- Cases, psychopaths, The Facts and Overtime are read from mission state (below), not from text messages.
- Nothing is delivered unless the mod's own mission `AP_Bridge` has been running for three seconds
  ("live"). That is false at the main menu and through every load.
- A trap that cannot run when it arrives is owed and retried every ten seconds.
- A DeathLink sets health to 0; Chuck only dies at his next hit or landing. The death it causes is not sent
  back out, however late it comes.
- Not detected yet: Katey's gifts and movie posters (both options are off by default for that reason).

## Rules learned the hard way

- **Never edit one of the game's own missions.** Saves made before the edit show as "corrupt" in the load
  menu (seen twice with an edit to `GiftsForKatie`). Adding parts to, and removing parts from, our own
  `AP_Bridge` mission did not have that effect on saves made with an earlier version of it.
- **The data file must not change during a playthrough** once this ships.
- Changing a *value* inside one of the game's missions did not upset saves: until 2026-11-15 the data file
  carried a leftover test edit (the sports car key at $1 in four pawnshops). It is gone now; the data file
  is built from the game's untouched `missions.txt` plus `AP_Bridge`, and nothing else in the archive differs.
- **`cMissionSetChuckState 19` (give combo card) is not used**: a save made after it crashed on load.
- **`cMissionSetChuckState 18` deletes the held item**; it does not drop it.
- **A scripted `cMissionLoadLevel` moves Chuck only.** Followers stay behind even with `MoveAllSurvivors`
  and even with the door flag set by hand. The Wrong Floor trap was removed for this reason.
- **Build with the game closed.** Switching away from the game for a build once left a black screen.
- Nothing counts as working until it has survived a save and a reload.

## Mission state (found 2026-11-15)

Mission definitions start with `0x00C8F0CC`. `+0x1C` name, `+0x44` start time, `+0x48` duration,
`+0x104` running copy. Running copy: `+0x04` game time started, `+0x08` state (0 not started, 1 running,
2 finished, 3 failed or expired), `+0x20` the mission system. Mission system `+0x34` = the player object.
Everything is rebuilt when a save loads. Script node: `+0x0C` first child, `+0x10` next, `+0x14` parent,
`+0x1C` name, `+0x3C` name length. `0x007071F0` RunChildren(this = node, mission system, running copy, 0, 0)
runs the actions under a node. A timer node counts how often it fired at `+0x50`. A spawn node keeps its
position at `+0x40..+0x48`, kind at `+0x50`, count at `+0x54`.

## Followers (found 2026-11-15)

One object starting with `0x00C46028`: a slot of `0xA8` bytes per follower from `+0x0C` (each starts with
that person's body, which starts with `0x00C9A820` and keeps its position at `+0x1C`), and the number of
followers at `+0x82C`. Seen at 0, 1 and 2. The game's own name for it is the "npc party manager".
Survivor message `0x63`: status 0 joined, 2 rescued, 1 seen once when Ted died (probably "died").

## Doors and level loads (found 2026-11-15)

A door and the script action both ask for a level through `0x007AB740` (builds the request) and
`0x00440BE0` (sends it). A door additionally sets the byte at `player + 0x858` first (from the door's own
data, `[door + 0x18] + 0x11C == 0`) and sends a second message built by `0x0079F480(player, door)`
afterwards. Door code is around `0x00628C1E`. The second message is the untested lead for followers.

## Health and damage (found 2026-11-15)

`0x008409E0` SetHealth(this = stats, float) stores the value, capped at 100 x the number of blocks
(`stats + 0x5C`). It is reached through the virtual function at `+0x60` of the player's controller object
(code ending at `0x006F5E6D`), which first sends the "health changed" message. Gunshots go through it.
The routine that decides the hit reaction is one caller further up and not found yet; `probe` now records
32 stack values to reach it.

## Leads from outside (looked up 2026-11-15)

- The Dead Rising wiki's "Modifying PC Dead Rising 2" pages document `missions.txt`, `items.txt`, spawn
  points and a developer debug build: https://deadrising.fandom.com/wiki/Mod:Debug_Mode
- That page lists about 500 setting names the debug build reads from `user.ini`. If the retail game still
  carries those settings, each can be found in the running game by its name and read or written. The
  ones that match what the mod wants: `force_sick_state` (Sickness trap), `force_phone_state` and
  `crank_calling_chuck` (phone), `number_of_player_health_bars` / `override_player_health_bars` (health as
  items), `number_of_player_inventory_slots` / `override_player_inventory_slots` (inventory as items),
  `enable_all_skill_moves` (skills), `disable_death_sequence`, `chuck_in_god_mode`,
  `npc_party_manager_debug_render`, `missions_everything_unlocked`, `get_all_combo_cards`,
  `disable_level_up_message`, `disable_casefiles_popup`.
  First test: `find override_player_health_bars` in the running game, then `refs` on what it finds.
- An Archipelago world exists for Dead Rising Deluxe Remaster (the first game, a different engine). Useful
  only for comparing check and item lists.
- No Archipelago work for Dead Rising 2 itself turned up.

## Saves (found 2026-11-15)

One file holds everything: `<Steam>\userdata\<steam id>\45740\remote\DR2SAVE.DR2S` (303,724 bytes, not
compressed, mostly zeros, no mission names in it). `Documents\My Games\Dead Rising 2\saves` is empty on
the Steam version. Steam Cloud tracks the file (`remotecache.vdf` beside it), so any swapping of saves per
multiworld has to happen while the game is closed. A backup from 2026-11-15 is in `save_backups`.

## From a cheat table for another build of the game (read 2026-11-15, nothing proven on ours yet)

The table was written for a different executable, so its addresses do not apply, but the offsets inside
objects and the byte runs it patches should carry over. `pattern <bytes>` finds a byte run in our game.

Stats block (the same block as ours: kills at `+0x38` and health at `+0x2C` match):

| Offset | What | Most |
|---|---|---|
| `+0x54` | attack upgrades | 7 |
| `+0x58` | speed upgrades | 3 |
| `+0x5C` | health blocks | 12 |
| `+0x60` | item (inventory) slots | 12 |
| `+0x64` | throwing distance upgrades | 5 |
| `+0x70` | money | |

`slots` shows these; `slots health 6` writes one.

Byte runs to look for:

| Bytes | What the table says it is |
|---|---|
| `01 51 20 01 41 1C` | the game clock being advanced (`add [ecx+20],edx` / `add [ecx+1C],eax`): the clock object is `ecx` |
| `8B 48 18 8D 0C D1` | reads the hour of day from `[eax+18]` |
| `F3 0F 11 41 4C` | writes an enemy's health (`[ecx+4C]`, a float) when it is hit |
| `D9 80 08 02 00 00` | reads a survivor's health (`[eax+208]`, a float) |
| `83 83 A0 00 00 00 01` | counts up a survivor's anger at Chuck (`[ebx+A0]`) |
| `D9 41 2C 5E 59` | reads Chuck's health |
| `F3 0F 10 05 ?? ?? ?? ?? ` near a float 110 | Chuck's run speed, a single global float |


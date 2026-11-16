# DR2AP: Dead Rising 2 for Archipelago

An [Archipelago](https://archipelago.gg) multiworld randomizer for **Dead Rising 2** (PC, Steam).

**Status: beta, in active development.** It runs and a full loop works (checks go out, items come in), but
several planned features are not built yet and nothing here has had a full playthrough. Expect rough edges.

This project is not affiliated with or endorsed by Capcom. **No game files are included.** You need your own
copy of Dead Rising 2 on Steam, and the tools here patch your own files.

## What works today

Confirmed in the game:

- **Checks:** cases, psychopaths, rescued survivors, zombie kill milestones, level-ups, Zombrex found
- **Items:** money, PP, Zombrex
- **DeathLink**, both ways
- **Traps:** Naked, Wrong Number (a phone call from Mr. Archipelago), Ambush (12 zombies)
- **Goals:** The Facts, or Overtime
- Nothing is delivered at the main menu or during a loading screen
- A normal playthrough is never touched: the mod only acts while the Archipelago client is running

Built but not yet proven in the game:

- Movie poster checks (off by default)

Planned, not built:

- Case gate items and the clock options (vanilla / slowed / frozen)
- Katey's gifts as checks (counted 1 to 10)
- Health blocks, inventory slots, skills, keys and useful combo weapons as items
- Butterfingers and Sickness traps
- Pawnshop checks
- A separate save per multiworld
- A one-click installer and a PopTracker pack

## Layout

| Path | What it is |
|---|---|
| `Core.cpp`, `Ap.inc`, `Tables.h`, `Loader.cpp` | The game plugin (`DR2AP.asi` + `DR2AP_core.dll`) |
| `apworld/dr2/` | The Archipelago world and its client |
| `tools/` | Scripts that build the patched game data from your own game files |
| `check/` | An offline test of the plugin's logic against a pretend game |
| `third_party/` | Our fixes to ClownDR2Fix, as a patch |
| `assets/` | The message chime |
| `NOTES.md` | Everything learned about the game's internals so far |

## Building the plugin

Needs Windows and Visual Studio 2022 with "Desktop development with C++".

1. Get the ClownDR2Fix source (Tervel1337's fix, in Clownacy's fork), with its submodules, and put it in a
   folder named `DR2Fix-source` **next to** this folder. The plugin uses the hooking library bundled there.
2. Close the game.
3. Run `build.bat`. It builds both files and, if the game is in the usual Steam folder, copies them there.

`Tables.h` is generated: after changing `apworld/dr2/Data.py`, run
`python tools/gen_tables.py apworld/dr2/Data.py Tables.h`.

## Building the game data

The mod adds one hidden mission, `AP_Bridge`, to the game's mission file, and four lines of text for the
phone call. The game's own missions are left exactly as they are.

```
python tools/unpack.py  "<game>/data/datafile.big" unpacked
python tools/build_ap_mission.py unpacked/missions.txt missions_ap.txt
python tools/repack.py  "<game>/data/datafile.big" datafile_ap.big missions.txt=missions_ap.txt
python tools/strings_tool.py "<game>/data/frontend/str_en.bcs" str_en_ap.bcs ^
    "88801=Mr. Archipelago" "88802=Frank?" "88803=No? This is Chuck." "88804=My bad, wrong number."
```

Keep the originals, then put `datafile_ap.big` in place of `data/datafile.big` and `str_en_ap.bcs` in place of
`data/frontend/str_en.bcs`. Do this with the game closed.

**Do not change the data file in the middle of a playthrough.** See "Rules learned the hard way" in `NOTES.md`.

## Installing and playing

1. Game folder: `dinput8.dll` (Ultimate ASI Loader), `DR2Fix.asi` (ClownDR2Fix), `DR2AP.asi`, `DR2AP_core.dll`
   and `assets/dr2_ap_chime.wav`, plus the two patched data files above.
2. Zip the `apworld/dr2` folder as `dr2.apworld` and put it in Archipelago's `custom_worlds` folder. Tested
   against Archipelago 0.6.8.
3. Generate a game, start **Dead Rising 2 Client** from the Archipelago Launcher and connect.
4. Start the game. Keep the client open while you play.

Client commands: `/gamedir <path>`, `/sendcheck <name>`, `/resync`, `/regive <n>`.

## Running the offline test

On Linux, or WSL: `cd check && ./run.sh`. It compiles `Ap.inc` against a pretend game and plays through
checks, items, traps, DeathLink and a client restart. It should end with `ALL CHECKS PASSED`.

The world has its own tests: copy `apworld/dr2` into an Archipelago source tree as `worlds/dr2` and run
`python -m pytest worlds/dr2/test`.

## Credits

- ClownDR2Fix by Tervel1337, fork by Clownacy (MIT): controller
  support and fixes, and the base this plugin is loaded beside
- [safetyhook](https://github.com/cursey/safetyhook) and [Zydis](https://github.com/zyantific/zydis)
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago)
- The Dead Rising Wiki's modding pages

See `THIRD_PARTY.md` for licences.

## Licence

MIT. See `LICENSE`.

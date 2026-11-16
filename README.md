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

With the game closed, run `tools/patch_game.bat` (it needs Python 3). It builds both files from your own
game files, puts them in place, and keeps the originals beside them as `<name>.original`. Running it again
is harmless: it always starts from the originals. `tools/restore_game.bat` puts the originals back.

If the game is not in the usual Steam folder, drag the game folder onto the `.bat` file, or run
`python tools/patch_game.py "<game folder>"`.

The steps it runs are `unpack.py`, `build_ap_mission.py`, `repack.py` and `strings_tool.py`, which can also
be used one at a time.

Besides `AP_Bridge` the data currently carries one small test mission, `AP_GateTest`. It does nothing unless
it is triggered by hand, and it will be removed at the next change that needs a fresh save anyway.

**Do not change the data file in the middle of a playthrough.** See "Rules learned the hard way" in `NOTES.md`.

## Installing and playing

1. Game folder: `dinput8.dll` (Ultimate ASI Loader), `DR2Fix.asi` (ClownDR2Fix), `DR2AP.asi`, `DR2AP_core.dll`
   and `assets/dr2_ap_chime.wav`. Then patch the game data as above.
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

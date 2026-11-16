# Building, installing and testing DR2AP

The technical side of the project: what is in the repository, how to build the plugin, how the game data is
patched, and how to run the tests. For what the mod does, see `README.md`.

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

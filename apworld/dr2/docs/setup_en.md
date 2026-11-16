# Dead Rising 2 (PC) setup guide

**Test build.** Checks, items, DeathLink and the naked trap work. The clock options, case gates, the 20% survivor timer
and the other traps are not active in the game yet.

## What you need

- Dead Rising 2 on Steam (PC).
- Archipelago 0.5.0 or newer.
- The DR2AP files in the game folder: `dinput8.dll`, `DR2Fix.asi`, `DR2AP.asi`, `DR2AP_core.dll`, `dr2_ap_chime.wav`.

## Playing

1. Put `dr2.apworld` in Archipelago's `custom_worlds` folder and generate a game as usual.
2. Open the Archipelago Launcher and start **Dead Rising 2 Client**. Connect to your room.
3. Start the game and use a save slot you keep for this multiworld only.

Keep the client open while you play: the game only sends checks and takes items while the client is running, so a normal
playthrough is never touched.

## Client commands

- `/gamedir <path>`: set the game folder if it is not the default Steam one.
- `/sendcheck <name>`: send a check the game missed. Part of the name is enough.
- `/resync`: send every check the game has found again.
- `/regive <n>`: hand out the last n received items again, after loading an older save.

## Credits

Controller support and fixes come from ClownDR2Fix by Tervel1337 and Clownacy (MIT), loaded through Ultimate ASI Loader.

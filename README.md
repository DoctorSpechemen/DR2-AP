# Dead Rising 2 for Archipelago

<img width="400" height="200" alt="image" src="https://github.com/user-attachments/assets/9a691570-b098-4149-848e-d556883e621a" />




An [Archipelago](https://archipelago.gg) multiworld randomizer for **Dead Rising 2** (PC, Steam).

**Status: beta, in active development.** It runs and a full loop works (checks go out, items come in), but
several planned features are not built yet and nothing here has had a full playthrough. Expect rough edges.

This project is not affiliated with or endorsed by Capcom. **No game files are included.** You need your own
copy of Dead Rising 2 on Steam, and the tools here patch your own files.

---
[!disclaimer]
# AI DISCLOSURE

**This project is made with heavy use of AI. Almost all of the code in this repository was written by
Claude, an AI model made by Anthropic, working with DoctorSpechemen.**

**Overall, about 90% of what is in this repository was produced by the AI, and about 10% by the human.**
The idea, the direction, the decisions about what the mod is, and every test in the real game are the
human's. The code, the tools, the tests and the notes are the AI's.

What that means for you:

- **The code has not been reviewed by an experienced C++ or reverse-engineering developer.** It was checked
  by the AI's own offline tests and by one person playing the game.
- The AI cannot run the game. Everything it knows about the game came from reading the game's memory through
  the plugin while DoctorSpechemen played, and from public modding notes.
- This is a plugin that runs inside a game and changes its memory. It can crash the game or upset a save.
  **Back up your save before using it**, and use it at your own risk.
- If you would rather not use AI-written software, this project is not for you, and that is a fair choice.

Bug reports and review from people who know this kind of work are very welcome.

---

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


## Credits

- DoctorSpechemen: project lead, design, and all in-game testing
- Claude, an AI model by [Anthropic](https://www.anthropic.com): the code, the tools, the tests and the notes
  (see the AI disclosure above)
- ClownDR2Fix by Tervel1337, fork by Clownacy (MIT): controller
  support and fixes, and the base this plugin is loaded beside
- [safetyhook](https://github.com/cursey/safetyhook) and [Zydis](https://github.com/zyantific/zydis)
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago)
- The Dead Rising Wiki's modding pages

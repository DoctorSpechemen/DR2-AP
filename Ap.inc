// DR2AP - the Archipelago side of the plugin. Included by Core.cpp (it uses Core.cpp's helpers).
//
// Files, all in the game folder (the client writes the first and last, the plugin writes the rest):
//   dr2_ap_items.txt                      seed, options, tally lines, then "<index>|<item name>|<sender>" for every item
//   dr2_ap_seeds\<seed>\dr2_ap_outbox.txt  one found location per line, and "GOAL: ..." lines
//   dr2_ap_seeds\<seed>\dr2_ap_state.txt   this multiworld's counters ("key value" per line)
//   dr2_ap_deaths.txt                     one line per death, for DeathLink
//   dr2_ap_in.txt                         commands: deathlink <text>, found <text>, sendcheck <location>, regive <n>
//
// The plugin only acts while the items file is fresh (the client touches it every second), so a normal
// playthrough without the client is never touched.

#include "Tables.h"

#include <map>
#include <set>
#include <vector>

// ---------------------------------------------------------------- what the game told us
// Game messages arrive on the game's own threads. They are boiled down to small events here and
// handled later by the plugin's thread.

enum { kApEventSurvivor = 1, kApEventKeyItem, kApEventDeath, kApEventLevel };

struct ApEvent {
    int type;
    int a;          // Survivor: status. KeyItem: item id. Death: how it was seen. Level: the new level.
    int b;          // Spare.
    char text[40];  // Survivor: internal name.
};

static const size_t kApEventRingSize = 256;
static ApEvent g_ap_events[kApEventRingSize];
static size_t g_ap_event_head;
static size_t g_ap_event_tail;
static volatile LONG g_ap_event_lock;

static volatile LONG g_ap_activity;      // Set when a message shows the game is being played (not sitting in a menu).
static volatile LONG g_ap_self_giving;   // Set while the plugin itself hands out a key item, so it is not counted as found.
static volatile LONG g_ap_player;        // The player object (changes with every launch and every loaded save).

static const uintptr_t kPlayerVtableOffset = 0x89A720;       // 0x00C9A720
static const uintptr_t kMissionDefinitionVtable = 0x88F0CC;  // 0x00C8F0CC: what every mission definition starts with
static const uintptr_t kDeathMessageCallerOffset = 0x53E4F;  // where the "player died" message is sent from

static void ApPushEvent(int type, int a, int b, const char* text) {
    while (InterlockedExchange(&g_ap_event_lock, 1) != 0)
        YieldProcessor();

    const size_t next = (g_ap_event_head + 1) % kApEventRingSize;
    if (next != g_ap_event_tail) {
        ApEvent& event = g_ap_events[g_ap_event_head];
        event.type = type;
        event.a = a;
        event.b = b;
        event.text[0] = '\0';
        if (text != nullptr)
            strncpy_s(event.text, sizeof(event.text), text, _TRUNCATE);
        g_ap_event_head = next;
    }

    InterlockedExchange(&g_ap_event_lock, 0);
}

static bool ApPopEvent(ApEvent* out) {
    while (InterlockedExchange(&g_ap_event_lock, 1) != 0)
        YieldProcessor();

    const bool available = g_ap_event_tail != g_ap_event_head;
    if (available) {
        *out = g_ap_events[g_ap_event_tail];
        g_ap_event_tail = (g_ap_event_tail + 1) % kApEventRingSize;
    }

    InterlockedExchange(&g_ap_event_lock, 0);
    return available;
}

static bool ApIsPlayerObject(uint32_t address) {
    uint32_t vtable = 0;
    return address >= 0x10000 && SafeRead32(address, &vtable) && vtable == g_exe_start + kPlayerVtableOffset;
}

// Called from the message hook for every message the game sends. Must stay quick and must not block.
static void ApOnGameMessage(uint32_t kind, uint32_t caller, uint32_t message, const uint32_t* carried) {
    switch (kind) {
    case 0x63: {
        // Survivor event: [0] = the survivor's record, [1] = status (0 joined, 2 rescued). Name at record + 0x18.
        const uint32_t record = carried[0];
        const uint32_t status = carried[1];
        if (record < 0x10000 || status > 16)   // 0 joined, 2 rescued; the others are logged to learn them
            break;

        char name[40];
        bool found = ReadTextAt(record + 0x18, name, sizeof(name));
        if (!found || strncmp(name, "srv_", 4) != 0) {
            uint32_t pointer = 0;
            found = SafeRead32(record + 0x18, &pointer) && ReadTextAt(pointer, name, sizeof(name));
        }
        if (found && strncmp(name, "srv_", 4) == 0) {
            ApPushEvent(kApEventSurvivor, static_cast<int>(status), 0, name);
            InterlockedExchange(&g_ap_activity, 1);
        }
        break;
    }
    case 0x13:
        // Key item received: [0] = the item's id (85001 = Zombrex).
        if (carried[0] >= 85000 && carried[0] < 86000 && g_ap_self_giving == 0)
            ApPushEvent(kApEventKeyItem, static_cast<int>(carried[0]), 0, nullptr);
        break;
    case 0x06:
        if (caller == g_exe_start + kDeathMessageCallerOffset)
            ApPushEvent(kApEventDeath, 0, 0, nullptr);
        break;
    case 0x1A:
        // Health changed: [0] = player, [1] = old, [2] = new (floats).
        if (ApIsPlayerObject(carried[0])) {
            InterlockedExchange(&g_ap_player, static_cast<LONG>(carried[0]));
            InterlockedExchange(&g_ap_activity, 1);
            if (BitsToFloat(carried[2]) <= 0.0f && BitsToFloat(carried[1]) > 0.0f)
                ApPushEvent(kApEventDeath, 1, 0, nullptr);
        }
        break;
    case 0x62:
        // Level up: [0] = text id 3102 ("LEVEL UP"), [2] = the new level.
        if (carried[0] == 3102 && carried[2] >= 2 && carried[2] <= 50)
            ApPushEvent(kApEventLevel, static_cast<int>(carried[2]), 0, nullptr);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- clothing
// The game puts clothes on through one routine: WearClothing(player, clothing definition, flag). The naked
// trap calls it with the "NakedOutfitDispenser" outfit. A clothing definition starts with 0x00C8D870 and
// keeps its name as text at +0x18 (seen on every launch, 2026-11-15), so the name's place in memory gives
// the definition away. (An earlier build hooked the routine to learn these; that hook is gone.)

static const uintptr_t kWearClothingFunction = 0x236DA0;   // 0x00636DA0, caller cleans up the stack
static const uintptr_t kClothingVtableOffset = 0x48D870;   // 0x00C8D870
static const char* const kNakedOutfitName = "NakedOutfitDispenser";
static uint32_t g_ap_naked_def;

static bool ApIsClothingDefinition(uint32_t address) {
    uint32_t vtable = 0;
    return address >= 0x10000 && SafeRead32(address, &vtable) && vtable == g_exe_start + kClothingVtableOffset;
}

static uint32_t ApFindClothingDefinition(const char* name) {
    const size_t length = strlen(name);

    FindText(name);
    std::vector<uint32_t> texts;
    for (size_t i = 0; i < g_result_count && i < 32; ++i)
        texts.push_back(static_cast<uint32_t>(g_results[i]));

    for (const uint32_t text : texts) {
        unsigned char after = 1;
        if (text % 4 != 0 || text < 0x10018 || !ApIsClothingDefinition(text - 0x18))
            continue;
        if (SafeCopy(text + length, &after, 1) != 1 || after != 0)
            continue;

        Log("AP: \"%s\" definition found at 0x%08X", name, text - 0x18);
        return text - 0x18;
    }

    Log("AP: no clothing definition named \"%s\" was found (%u place(s) hold that text)", name, static_cast<unsigned int>(texts.size()));
    return 0;
}

// ---------------------------------------------------------------- mission scripts
// The game keeps every mission as a tree of parts ("nodes") in memory. Found 2026-11-15:
//   node: +0x00 what kind of part it is, +0x0C first child, +0x10 next sibling, +0x14 parent,
//         +0x1C its name (text, up to 32 characters), +0x3C the name's length
//   mission definition (the top of a tree): +0x44 start time, +0x48 duration,
//         +0x104 its running copy ("instance"); instance + 0x20 = the mission system itself
//   0x007071F0: RunChildren(this = node, mission system, instance, player number, 0) runs every action under a node.
// The mod's own mission (AP_Bridge) holds one named part per trap, so running that part is the trap.

static const uintptr_t kRunChildrenFunction = 0x3071F0;       // 0x007071F0

static bool ApIsMissionDefinition(uint32_t address) {
    uint32_t vtable = 0;
    return address >= 0x10000 && SafeRead32(address, &vtable) && vtable == g_exe_start + kMissionDefinitionVtable;
}

static std::map<std::string, uint32_t> g_ap_nodes;  // Script parts already found, by name.
static std::set<std::string> g_ap_missing_nodes;    // Names already reported as missing (so the log says it once).
static DWORD g_ap_last_node_search;
static int g_ap_node_search_misses;

// Finds a part of a mission script by its exact name. Searches memory the first time, then remembers.
static uint32_t ApFindScriptNode(const char* name) {
    const size_t length = strlen(name);
    if (length == 0 || length > 31)
        return 0;

    const auto known = g_ap_nodes.find(name);
    if (known != g_ap_nodes.end()) {
        char text[40];
        uint32_t vtable = 0;
        if (SafeRead32(known->second, &vtable) && IsInExe(vtable) && ReadTextAt(known->second + 0x1C, text, sizeof(text)) && strcmp(text, name) == 0)
            return known->second;
        g_ap_nodes.erase(known);
    }

    // Searching all of memory takes a moment (the game can hitch), so a name that was missing is looked for
    // again after 5 seconds at first and then only once a minute. Without the mod's data file installed
    // nothing is ever found, and the game must not pay for that every few seconds.
    const DWORD wait = g_ap_node_search_misses < 6 ? 5000 : 60000;
    if (g_ap_missing_nodes.count(name) != 0 && GetTickCount() - g_ap_last_node_search < wait)
        return 0;
    g_ap_last_node_search = GetTickCount();

    FindText(name);
    std::vector<uint32_t> texts;
    for (size_t i = 0; i < g_result_count && i < 64; ++i)
        texts.push_back(static_cast<uint32_t>(g_results[i]));

    for (const uint32_t text_address : texts) {
        if (text_address % 4 != 0 || text_address < 0x1C)
            continue;

        const uint32_t node = text_address - 0x1C;
        uint32_t vtable = 0, name_length = 0;
        unsigned char after = 1;
        if (!SafeRead32(node, &vtable) || !IsInExe(vtable))
            continue;
        if (!SafeRead32(node + 0x3C, &name_length) || name_length != length)
            continue;  // A longer name that merely starts the same way.
        if (SafeCopy(text_address + length, &after, 1) != 1 || after != 0)
            continue;

        g_ap_nodes[name] = node;
        g_ap_missing_nodes.erase(name);
        g_ap_node_search_misses = 0;
        Log("AP: script part \"%s\" is at 0x%08X", name, node);
        return node;
    }

    if (g_ap_node_search_misses < 1000)
        ++g_ap_node_search_misses;
    if (g_ap_missing_nodes.insert(name).second)
        Log("AP: script part \"%s\" was not found", name);
    return 0;
}

// Runs every action under the named script part. Returns false if it could not be done right now.
static bool ApFireScriptNode(const char* name) {
    const uint32_t node = ApFindScriptNode(name);
    if (node == 0)
        return false;

    // Walk up to the mission the part belongs to.
    uint32_t definition = node;
    for (int i = 0; i < 16 && !ApIsMissionDefinition(definition); ++i) {
        uint32_t parent = 0;
        if (!SafeRead32(definition + 0x14, &parent) || parent == 0)
            break;
        definition = parent;
    }
    if (!ApIsMissionDefinition(definition)) {
        Log("AP: \"%s\": could not find the mission it belongs to", name);
        return false;
    }

    uint32_t instance = 0, mission_system = 0;
    if (!SafeRead32(definition + 0x104, &instance) || instance == 0 || !SafeRead32(instance + 0x20, &mission_system) || mission_system == 0) {
        Log("AP: \"%s\": its mission has no running copy right now (not in a game?)", name);
        return false;
    }

    const uint32_t arguments[4] = { mission_system, instance, 0, 0 };
    uint32_t result = 0;
    if (!CallOnMainThread(g_exe_start + kRunChildrenFunction, node, 0, arguments, 4, &result))
        return false;

    Log("AP: ran script part \"%s\"", name);
    return true;
}

// Notes where every part of a mission is by walking its tree, so the parts need no memory search of their own.
static void ApIndexTree(uint32_t node, int depth) {
    for (int guard = 0; node != 0 && guard < 400; ++guard) {
        uint32_t vtable = 0, first_child = 0, next = 0, name_length = 0;
        char name[40];
        if (!SafeRead32(node, &vtable) || !IsInExe(vtable))
            return;
        if (SafeRead32(node + 0x3C, &name_length) && name_length >= 4 && name_length < 32 && ReadTextAt(node + 0x1C, name, sizeof(name))
            && strlen(name) == name_length) {
            g_ap_nodes[name] = node;
            g_ap_missing_nodes.erase(name);
        }
        if (depth < 4 && SafeRead32(node + 0x0C, &first_child) && first_child != 0)
            ApIndexTree(first_child, depth + 1);
        if (!SafeRead32(node + 0x10, &next))
            return;
        node = next;
    }
}

static void ApSendLine(const std::string& line);
static void ApToast(const std::string& title, const std::string& text);
static bool g_ap_option_gifts = true;
static bool g_ap_option_posters = true;

// Katey's gifts are counted, in whatever order they are given: "Katey's Gift 1" ... "Katey's Gift 10".
// The game keeps a tally of its own (every gift ends with the "FatherOfTheMonth" achievement step); once
// the plugin can read it, it calls this with the number. Not called from anywhere yet.
static void ApOnGiftCount(int given) {
    for (int number = 1; g_ap_option_gifts && number <= given && number <= kApMaxKateyGifts; ++number)
        ApSendLine("Katey's Gift " + std::to_string(number));
}

// Movie posters: each has a timer in the mod's mission that goes off once when the game reports the
// poster examined. A timer keeps count at +0x50.
static void ApCheckCounters() {
    for (const ApCounterCheck& check : kApCounterChecks) {
        const auto known = g_ap_nodes.find(check.counter);
        uint32_t count = 0;
        if (known == g_ap_nodes.end() || !SafeRead32(known->second + 0x50, &count) || count == 0 || count > 1000)
            continue;
        if (g_ap_option_posters)
            ApSendLine(check.location);
    }
}

// True while the game is in live play: the mod's own mission is running. While a save loads the game
// resets every mission to "not started" first, so this is false at the main menu and through loading,
// and it has to stay true for three seconds before anything is delivered.
static DWORD g_ap_live_since;

static bool ApGameIsLive() {
    bool running = false;
    const uint32_t bridge = ApFindScriptNode("AP_Bridge");
    uint32_t instance = 0, state = 0;
    if (bridge != 0 && ApIsMissionDefinition(bridge) && SafeRead32(bridge + 0x104, &instance) && instance != 0
        && SafeRead32(instance + 0x08, &state) && state == 1)
        running = true;

    const DWORD now = GetTickCount();
    if (!running) {
        g_ap_live_since = 0;
        return false;
    }
    if (g_ap_live_since == 0) {
        g_ap_live_since = now != 0 ? now : 1;

        // The game rebuilds its missions when a save loads, so every part has moved: find them all again.
        g_ap_nodes.clear();
        g_ap_nodes["AP_Bridge"] = bridge;
        uint32_t first_child = 0;
        if (SafeRead32(bridge + 0x0C, &first_child))
            ApIndexTree(first_child, 0);
        Log("AP: %u script part(s) indexed", static_cast<unsigned int>(g_ap_nodes.size()));
    }
    return now - g_ap_live_since >= 3000;
}

// Research: every mission the game has loaded, with what is known about its state.
static void ApListMissions() {
    const uint32_t vtable = static_cast<uint32_t>(g_exe_start + kMissionDefinitionVtable);
    ScanAll(vtable, vtable);

    std::vector<uint32_t> definitions;
    for (size_t i = 0; i < g_result_count; ++i)
        definitions.push_back(static_cast<uint32_t>(g_results[i]));

    Log("%u mission definition(s)", static_cast<unsigned int>(definitions.size()));
    for (const uint32_t definition : definitions) {
        char name[40] = "?";
        ReadTextAt(definition + 0x1C, name, sizeof(name));

        uint32_t start = 0, duration = 0, instance = 0, flags = 0, more = 0;
        SafeRead32(definition + 0x44, &start);
        SafeRead32(definition + 0x48, &duration);
        SafeRead32(definition + 0x104, &instance);
        SafeRead32(definition + 0x108, &flags);
        SafeRead32(definition + 0x10C, &more);

        uint32_t words[8] = {};
        if (instance != 0) {
            for (int i = 0; i < 8; ++i)
                SafeRead32(instance + 4 + i * 4, &words[i]);
        }

        Log("  0x%08X %-32s start %6u dur %7u flags %08X %08X inst %08X: %X %X %X %X %X %X %X %X", definition, name, start, duration,
            flags, more, instance, words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
    }
}

// Research that runs by itself while the game is played: whenever a mission starts, ends or changes its
// result, one line goes to the log. This is how the meaning of the state values gets worked out.
//   instance + 0x04 = game time it started, + 0x08 = state (0 not started, 1 running, 2 finished),
//   + 0x10 and + 0x18 = not understood yet (they differ between missions that ended differently)
struct ApWatchedMission {
    uint32_t definition;
    uint32_t values[4];
};

static std::vector<ApWatchedMission> g_ap_watched_missions;
static bool g_ap_mission_watch = true;

static bool ApReadMissionValues(uint32_t definition, uint32_t* values) {
    uint32_t instance = 0;
    if (!ApIsMissionDefinition(definition) || !SafeRead32(definition + 0x104, &instance) || instance == 0)
        return false;
    // A state above 3 means this is not a live mission (a leftover copy in freed memory); leave it alone.
    return SafeRead32(instance + 0x04, &values[0]) && SafeRead32(instance + 0x08, &values[1]) && values[1] <= 3
        && SafeRead32(instance + 0x10, &values[2]) && SafeRead32(instance + 0x18, &values[3]);
}

static bool g_ap_option_psychopaths = true;
static void ApFactsFinished(const char* reason);

// A mission's state: 0 not started, 1 running, 2 finished, 3 failed or expired. Cases and psychopaths are
// checks when their mission reaches 2. This also catches up on anything finished while the client was off.
static void ApCheckMissionState(const char* mission_name, uint32_t state, bool changed) {
    for (const ApMissionCheck& check : kApMissionChecks) {
        if (strcmp(check.mission, mission_name) != 0)
            continue;
        if (state == 2 && (check.kind != 2 || g_ap_option_psychopaths))
            ApSendLine(check.location);
        else if (state == 3 && changed && check.kind == 1)
            ApToast("Archipelago", std::string(check.location) + " was failed. Load a save to continue the story.");
        return;
    }
    if (state == 2 && strcmp(mission_name, kApTheFactsMission) == 0)
        ApFactsFinished("its mission is finished");
    else if (state == 2 && strcmp(mission_name, kApOvertimeMission) == 0)
        ApSendLine("GOAL: Overtime");
}

static void ApWatchMissions() {

    // (Re)build the list when it is empty or the game has thrown its missions away.
    if (g_ap_watched_missions.empty() || !ApIsMissionDefinition(g_ap_watched_missions[0].definition)) {
        g_ap_watched_missions.clear();
        const uint32_t vtable = static_cast<uint32_t>(g_exe_start + kMissionDefinitionVtable);
        ScanAll(vtable, vtable);
        for (size_t i = 0; i < g_result_count && i < 600; ++i) {
            ApWatchedMission mission = {};
            mission.definition = static_cast<uint32_t>(g_results[i]);
            if (ApReadMissionValues(mission.definition, mission.values)) {
                g_ap_watched_missions.push_back(mission);

                char name[40] = "?";
                ReadTextAt(mission.definition + 0x1C, name, sizeof(name));
                ApCheckMissionState(name, mission.values[1], false);
            }
        }
        Log("AP: watching %u mission(s) for changes", static_cast<unsigned int>(g_ap_watched_missions.size()));
        return;
    }

    for (ApWatchedMission& mission : g_ap_watched_missions) {
        uint32_t now[4];
        if (!ApReadMissionValues(mission.definition, now) || memcmp(now, mission.values, sizeof(now)) == 0)
            continue;

        char name[40] = "?";
        ReadTextAt(mission.definition + 0x1C, name, sizeof(name));
        if (g_ap_mission_watch)
            Log("MISSION %s: state %u -> %u, started %u -> %u, a %u -> %u, b %u -> %u", name, mission.values[1], now[1],
                mission.values[0], now[0], mission.values[2], now[2], mission.values[3], now[3]);
        if (now[1] != mission.values[1])
            ApCheckMissionState(name, now[1], true);
        memcpy(mission.values, now, sizeof(now));
    }
}

// ---------------------------------------------------------------- state for the current multiworld

static bool g_ap_active;                 // The client is alive and a seed is known.
static bool g_ap_was_live;               // The game was in live play at the last look (not a menu, not loading).
static DWORD g_ap_hold_until;            // Test aid: treat the items file as fresh until this tick ("aphold").
static bool g_ap_in_play;                // The game has shown signs of being played since the plugin started.
static std::string g_ap_seed;            // Cleaned seed name (also the folder name).
static std::wstring g_ap_seed_dir;       // "<game>\dr2_ap_seeds\<seed>\"
static std::set<std::string> g_ap_sent;  // Every line already in the outbox.
static std::map<std::string, long long> g_ap_state;

static int g_ap_option_kills = 10;
static int g_ap_option_levels = 20;
static int g_ap_option_zombrex = 4;
static bool g_ap_option_survivors = true;

static DWORD g_ap_last_death_tick;
static DWORD g_ap_ignore_death_until;
static bool g_ap_deathlink_pending;
static std::string g_ap_deathlink_text;
static uint32_t g_ap_last_kills;
static bool g_ap_kills_known;
static int g_ap_regive_count;

static const uintptr_t kGiveKeyItemFunction = 0x442CD0;  // 0x00842CD0: this = stats; (item id, 0)
static const uintptr_t kStatsLevel = 0x20;
static const uintptr_t kStatsHealth = 0x2C;

static std::wstring ApWide(const std::string& text) {
    wchar_t buffer[512];
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, 512) == 0)
        return L"";
    return buffer;
}

static void ApToast(const std::string& title, const std::string& text) {
    QueueToast(ApWide(title).c_str(), ApWide(text).c_str());
}

static bool ApReadWholeFile(const std::wstring& path, std::string& contents) {
    contents.clear();
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    char chunk[4096];
    DWORD bytes_read = 0;
    while (ReadFile(file, chunk, sizeof(chunk), &bytes_read, nullptr) && bytes_read != 0)
        contents.append(chunk, bytes_read);
    CloseHandle(file);
    return true;
}

static bool ApWriteWholeFile(const std::wstring& path, const std::string& contents, bool append) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), append ? L"ab" : L"wb") != 0 || file == nullptr)
        return false;
    fwrite(contents.data(), 1, contents.size(), file);
    fclose(file);
    return true;
}

static std::vector<std::string> ApSplitLines(const std::string& contents) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < contents.size()) {
        size_t end = contents.find('\n', start);
        if (end == std::string::npos)
            end = contents.size();

        std::string line = contents.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty())
            lines.push_back(line);

        start = end + 1;
    }
    return lines;
}

static long long ApStateGet(const std::string& key) {
    const auto found = g_ap_state.find(key);
    return found == g_ap_state.end() ? 0 : found->second;
}

static void ApStateSet(const std::string& key, long long value) {
    if (g_ap_seed.empty())
        return;
    const auto found = g_ap_state.find(key);
    if (found != g_ap_state.end() && found->second == value)
        return;

    g_ap_state[key] = value;

    std::string contents;
    for (const auto& entry : g_ap_state)
        contents += entry.first + " " + std::to_string(entry.second) + "\r\n";
    ApWriteWholeFile(g_ap_seed_dir + L"dr2_ap_state.txt", contents, false);
}

// Same rule as the client's clean_seed(): letters, digits, '-' and '_' stay, everything else becomes '_'.
static std::string ApCleanSeed(const std::string& seed) {
    std::string clean;
    for (const char c : seed) {
        const bool keep = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        clean += keep ? c : '_';
    }
    return clean;
}

// Switches to the given multiworld's folder and loads what was saved there.
static void ApUseSeed(const std::string& seed) {
    const std::string clean = ApCleanSeed(seed);
    if (clean.empty() || clean == g_ap_seed)
        return;

    g_ap_seed = clean;
    const std::wstring seeds_dir = PathInGameDir(L"dr2_ap_seeds");
    CreateDirectoryW(seeds_dir.c_str(), nullptr);
    g_ap_seed_dir = seeds_dir + L"\\" + ApWide(clean) + L"\\";
    CreateDirectoryW(g_ap_seed_dir.c_str(), nullptr);

    g_ap_sent.clear();
    g_ap_state.clear();

    std::string contents;
    if (ApReadWholeFile(g_ap_seed_dir + L"dr2_ap_outbox.txt", contents)) {
        for (const std::string& line : ApSplitLines(contents))
            g_ap_sent.insert(line);
    }
    if (ApReadWholeFile(g_ap_seed_dir + L"dr2_ap_state.txt", contents)) {
        for (const std::string& line : ApSplitLines(contents)) {
            const size_t space = line.find(' ');
            if (space != std::string::npos)
                g_ap_state[line.substr(0, space)] = _strtoi64(line.c_str() + space + 1, nullptr, 10);
        }
    }

    Log("AP: multiworld %s (%u check(s) already found, %d item(s) already handed out)", clean.c_str(),
        static_cast<unsigned int>(g_ap_sent.size()), static_cast<int>(ApStateGet("ap_index")));
}

// Writes a line to the outbox once. Used for locations and for "GOAL: ..." lines.
static void ApSendLine(const std::string& line) {
    if (g_ap_seed.empty()) {
        Log("AP: \"%s\" not recorded: no multiworld is linked", line.c_str());
        return;
    }
    if (g_ap_sent.count(line) != 0)
        return;

    g_ap_sent.insert(line);
    if (ApWriteWholeFile(g_ap_seed_dir + L"dr2_ap_outbox.txt", line + "\r\n", true))
        Log("AP: CHECK %s", line.c_str());
    else
        Log("AP: could not write \"%s\" to the outbox", line.c_str());
}

// ---------------------------------------------------------------- checks

static void ApFactsFinished(const char* reason) {
    Log("AP: The Facts finished (%s)", reason);
    ApSendLine(kApTheFactsLocation);
    ApSendLine("GOAL: The Facts");
}

static void ApOnSurvivor(int status, const char* internal_name) {
    // Some survivors exist in several outfits: "srv_jack_0", "srv_jack_3". They are one person.
    std::string base = internal_name;
    const size_t underscore = base.rfind('_');
    if (underscore != std::string::npos && underscore > 4 && underscore + 1 < base.size()) {
        bool digits = true;
        for (size_t i = underscore + 1; i < base.size(); ++i)
            digits = digits && base[i] >= '0' && base[i] <= '9';
        if (digits)
            base.erase(underscore);
    }

    if (status == 0 || status == 2)
        Log("AP: survivor %s %s", internal_name, status == 2 ? "rescued" : "joined");
    else
        Log("AP: survivor %s status %d (meaning not known yet)", internal_name, status);
    if (status != 2 || !g_ap_option_survivors)
        return;

    for (const ApSurvivor& survivor : kApSurvivors) {
        if (base == survivor.internal_name) {
            ApSendLine(survivor.location);
            return;
        }
    }
    Log("AP: %s is not a survivor with a check", base.c_str());
}

static void ApOnLevel(int level) {
    Log("AP: level %d reached", level);
    for (int reached = 2; reached <= level && reached <= kApMaxLevel && reached - 1 <= g_ap_option_levels; ++reached)
        ApSendLine("Reach Level " + std::to_string(reached));
}

static void ApOnKeyItem(int item_id) {
    Log("AP: key item %d received from the game", item_id);
    if (item_id != kApZombrexKeyItem)
        return;

    const long long found = ApStateGet("zombrex_found") + 1;
    ApStateSet("zombrex_found", found);
    if (found <= g_ap_option_zombrex && found <= kApMaxZombrexChecks)
        ApSendLine("Zombrex Found " + std::to_string(found));
}

static void ApOnDeath(int source) {
    const DWORD now = GetTickCount();
    if (g_ap_last_death_tick != 0 && now - g_ap_last_death_tick < 10000)
        return;  // The same death, reported a second way.
    g_ap_last_death_tick = now != 0 ? now : 1;

    if (g_ap_ignore_death_until != 0 && static_cast<int32_t>(g_ap_ignore_death_until - now) > 0) {
        g_ap_ignore_death_until = now + 15000;   // this was the death the DeathLink caused; only its echoes are left
        if (g_ap_ignore_death_until == 0)
            g_ap_ignore_death_until = 1;
        Log("AP: death seen (%s); not sent, it came from a DeathLink", source == 1 ? "health reached 0" : "death message");
        return;
    }

    Log("AP: death seen (%s); sending a DeathLink", source == 1 ? "health reached 0" : "death message");
    ApWriteWholeFile(PathInGameDir(L"dr2_ap_deaths.txt"), "died\r\n", true);
}

static void ApCheckKills(uintptr_t stats) {
    uint32_t kills = 0;
    if (!SafeRead32(stats + kStatsKills, &kills) || kills > 10000000)
        return;

    if (g_ap_kills_known && kills != g_ap_last_kills) {
        if (kills < g_ap_last_kills)
            Log("AP: zombie kills went from %u to %u: a save was loaded or a new game started", g_ap_last_kills, kills);
        else
            g_ap_in_play = true;
    }
    g_ap_last_kills = kills;
    g_ap_kills_known = true;

    if (!g_ap_in_play)
        return;

    const int count = static_cast<int>(sizeof(kApKillMilestones) / sizeof(kApKillMilestones[0]));
    for (int i = 0; i < count && i < g_ap_option_kills; ++i) {
        if (kills >= static_cast<uint32_t>(kApKillMilestones[i]))
            ApSendLine(kApKillLocations[i]);
    }
}

// ---------------------------------------------------------------- items

static const ApItem* ApFindItem(const std::string& name) {
    for (const ApItem& item : kApItems) {
        if (name == item.name)
            return &item;
    }
    return nullptr;
}

static int g_ap_naked_search_misses;

static uint32_t ApFindPlayer();

// Horde and Looter traps: the spawn actions in the mod's mission carry a placeholder position. Chuck's
// position (three floats at player + 0x1C) is written over it, spread out a little, then the part is run.
static bool ApSpawnAroundPlayer(const char* part, const char* const* spawn_names, int spawn_count, uint32_t each) {
    const uint32_t player = ApFindPlayer();
    uint32_t x = 0, y = 0, z = 0;
    if (player == 0 || !SafeRead32(player + 0x1C, &x) || !SafeRead32(player + 0x20, &y) || !SafeRead32(player + 0x24, &z))
        return false;

    const float px = BitsToFloat(x), py = BitsToFloat(y), pz = BitsToFloat(z);
    if (!(px > -5000.0f && px < 5000.0f && py > -500.0f && py < 500.0f && pz > -5000.0f && pz < 5000.0f)) {
        Log("AP: %s: Chuck's position does not look right (%g, %g, %g); not spawning", part, px, py, pz);
        return false;
    }

    static const float kOffsets[4][2] = { { 4.0f, 0.0f }, { -4.0f, 0.0f }, { 0.0f, 4.0f }, { 0.0f, -4.0f } };
    for (int i = 0; i < spawn_count; ++i) {
        const uint32_t node = ApFindScriptNode(spawn_names[i]);
        if (node == 0)
            return false;
        SafeWrite32(node + 0x40, FloatToBits(px + kOffsets[i % 4][0]));
        SafeWrite32(node + 0x44, y);
        SafeWrite32(node + 0x48, FloatToBits(pz + kOffsets[i % 4][1]));
        SafeWrite32(node + 0x54, each);  // how many appear at this spot
    }

    Log("AP: %s around (%g, %g, %g)", part, px, py, pz);
    return ApFireScriptNode(part);
}

static bool ApHordeTrap() {
    static const char* const kNames[4] = { "AP_Horde1", "AP_Horde2", "AP_Horde3", "AP_Horde4" };
    return ApSpawnAroundPlayer("AP_OnHorde", kNames, 4, 3);   // 12 zombies
}

static bool ApLooterTrap() {
    static const char* const kNames[1] = { "AP_Looter1" };
    return ApSpawnAroundPlayer("AP_OnLooters", kNames, 1, 3);
}

static bool ApMercenaryTrap() {
    static const char* const kNames[1] = { "AP_Merc1" };
    return ApSpawnAroundPlayer("AP_OnMercs", kNames, 1, 2);
}

// The Ambush trap: 2 mercenaries where the area has them ready, otherwise 3 looters, otherwise 12 zombies.
// The game only loads certain enemy kinds per area and per time of day (zombiesets.txt), and asking for
// a kind that is not loaded spawns nothing (seen 2026-11-15 with looters). Telling which kinds are
// loaded needs the current area and the hour it was entered; until the plugin can read those, the
// ambush is always the zombies, which every area has.
static bool ApAmbushTrap() {
    return ApHordeTrap();
}

// The player object. The mission system keeps a pointer to it at +0x34 (seen 2026-11-15), and the mod's own
// mission leads to the mission system: definition + 0x104 = its running copy, running copy + 0x20 = the
// mission system. That is three reads and always current, so it is asked every time; the address from
// the game's messages is only the fallback.
static uint32_t ApFindPlayer() {
    const uint32_t bridge = ApFindScriptNode("AP_Bridge");
    uint32_t instance = 0, mission_system = 0, player = 0;
    if (bridge != 0 && ApIsMissionDefinition(bridge) && SafeRead32(bridge + 0x104, &instance) && instance != 0
        && SafeRead32(instance + 0x20, &mission_system) && mission_system != 0
        && SafeRead32(mission_system + 0x34, &player) && ApIsPlayerObject(player)) {
        InterlockedExchange(&g_ap_player, static_cast<LONG>(player));
        return player;
    }

    player = static_cast<uint32_t>(g_ap_player);
    return ApIsPlayerObject(player) ? player : 0;
}

// The party: who is following Chuck right now. Found 2026-11-15 with two survivors in tow: one object
// (it starts with 0x00C46028) keeps a slot of 0xA8 bytes per follower from +0x0C, each slot starting with
// that person's body in the world, and the number of followers at +0x82C.
static const uintptr_t kPartyVtableOffset = 0x846028;  // 0x00C46028
static const uintptr_t kPersonVtableOffset = 0x89A820; // 0x00C9A820: a survivor's (or other named person's) body
static const uint32_t kPartySlotsAt = 0x0C, kPartySlotSize = 0xA8, kPartyCountAt = 0x82C, kPartyMost = 12;
static uint32_t g_ap_party;

static bool ApReadParty(uint32_t party, uint32_t* count) {
    uint32_t vtable = 0;
    return party >= 0x10000 && SafeRead32(party, &vtable) && vtable == g_exe_start + kPartyVtableOffset &&
           SafeRead32(party + kPartyCountAt, count) && *count <= kPartyMost;
}

static uint32_t ApFindParty() {
    uint32_t count = 0;
    if (ApReadParty(g_ap_party, &count))
        return g_ap_party;

    g_ap_party = 0;
    const uint32_t vtable = static_cast<uint32_t>(g_exe_start + kPartyVtableOffset);
    ScanAll(vtable, vtable);
    for (size_t i = 0; i < g_result_count && i < 64; ++i) {
        if (ApReadParty(static_cast<uint32_t>(g_results[i]), &count)) {
            g_ap_party = static_cast<uint32_t>(g_results[i]);
            Log("AP: party list found at 0x%08X", g_ap_party);
            break;
        }
    }
    return g_ap_party;
}

// How many people are following Chuck, or -1 when that cannot be read.
static int ApPartyCount() {
    const uint32_t party = ApFindParty();
    uint32_t count = 0;
    return party != 0 && ApReadParty(party, &count) ? static_cast<int>(count) : -1;
}

static void ApLogParty() {
    const uint32_t party = ApFindParty();
    uint32_t count = 0;
    if (party == 0 || !ApReadParty(party, &count)) {
        Log("  the party list was not found");
        return;
    }
    Log("  party list at 0x%08X: %u follower(s)", party, count);
    for (uint32_t i = 0; i < kPartyMost; ++i) {
        uint32_t body = 0, vtable = 0, x = 0, y = 0, z = 0;
        if (!SafeRead32(party + kPartySlotsAt + i * kPartySlotSize, &body) || body == 0)
            continue;
        const bool person = SafeRead32(body, &vtable) && vtable == g_exe_start + kPersonVtableOffset;
        SafeRead32(body + 0x1C, &x); SafeRead32(body + 0x20, &y); SafeRead32(body + 0x24, &z);
        Log("    slot %u: 0x%08X%s at (%g, %g, %g)", i, body, person ? "" : " (not a person)", BitsToFloat(x), BitsToFloat(y), BitsToFloat(z));
    }
}

static bool ApNakedTrap() {
    const uint32_t player = ApFindPlayer();
    if (player == 0) {
        Log("AP: naked trap waiting: the player object was not found (not in a game?)");
        return false;
    }

    // The outfit's definition is looked up by name. That search goes through all of memory, so after
    // three misses in a row it is not tried again until the next load.
    if (!ApIsClothingDefinition(g_ap_naked_def)) {
        g_ap_naked_def = 0;
        if (g_ap_naked_search_misses >= 3)
            return false;
        g_ap_naked_def = ApFindClothingDefinition(kNakedOutfitName);
        if (g_ap_naked_def == 0) {
            ++g_ap_naked_search_misses;
            return false;
        }
    }
    const uint32_t definition = g_ap_naked_def;

    const uint32_t arguments[3] = { player, definition, 1 };
    uint32_t result = 0;
    if (!CallOnMainThread(g_exe_start + kWearClothingFunction, 0, 0, arguments, 3, &result))
        return false;

    Log("AP: naked trap done");
    return true;
}

// Springs a trap. False means it could not be done right now. Traps that are not built yet count as done
// (their message box still shows).
static bool ApRunTrap(int trap) {
    switch (trap) {
    case kApTrapNaked:
        return ApNakedTrap();
    case kApTrapWrongNumber:
        return ApFireScriptNode("AP_OnCall");
    case kApTrapAmbush:
        return ApAmbushTrap();
    default:
        return true;
    }
}

static const int kApOwedTraps[] = { kApTrapNaked, kApTrapWrongNumber, kApTrapAmbush };

static std::string ApOwedKey(int trap) {
    // The naked trap keeps the key it had in earlier builds, so nothing already owed is forgotten.
    return trap == kApTrapNaked ? "naked_owed" : "trap_owed_" + std::to_string(trap);
}

// Hands one item to the player. Returns false if it could not be done right now (it is tried again).
static bool ApApplyItem(const ApItem& item, uintptr_t stats) {
    uint32_t result = 0;

    switch (item.kind) {
    case kApItemMoney: {
        const uint32_t arguments[3] = { static_cast<uint32_t>(item.value), 0, 0 };
        return CallOnMainThread(g_exe_start + kChangeMoneyFunction, static_cast<uint32_t>(stats), 0, arguments, 3, &result);
    }
    case kApItemPP: {
        const uint32_t arguments[4] = { static_cast<uint32_t>(item.value), 0xC20, 1, 0 };
        return CallOnMainThread(g_exe_start + kAwardPPFunction, static_cast<uint32_t>(stats), 0, arguments, 4, &result);
    }
    case kApItemZombrex: {
        const uint32_t arguments[2] = { static_cast<uint32_t>(kApZombrexKeyItem), 0 };
        InterlockedExchange(&g_ap_self_giving, 1);
        const bool done = CallOnMainThread(g_exe_start + kGiveKeyItemFunction, static_cast<uint32_t>(stats), 0, arguments, 2, &result);
        InterlockedExchange(&g_ap_self_giving, 0);
        return done;
    }
    case kApItemCaseGate:
        ApStateSet("gate_" + std::to_string(item.value), 1);  // Recorded; the game does not hold cases back yet.
        return true;
    case kApItemTrap:
        // A trap never holds up the items behind it. One that cannot run right now is owed, and tried again later.
        if (!ApRunTrap(item.value))
            ApStateSet(ApOwedKey(item.value), ApStateGet(ApOwedKey(item.value)) + 1);
        return true;
    default:
        return true;
    }
}

static std::string ApItemMessage(const ApItem* item, const std::string& name, const std::string& sender) {
    if (item != nullptr && item->kind == kApItemTrap) {
        std::string text = sender.empty() ? name + "!" : sender + " sent a " + name + "!";
        if (item->value == kApTrapButterfingers || item->value == kApTrapSickness)
            text += " (not built yet)";
        return text;
    }
    return sender.empty() ? "Received " + name : "Received " + name + " from " + sender;
}

struct ApReceivedItem {
    long long index;
    std::string name;
    std::string sender;
};

// Reads the items file: seed, options, and the list of received items.
static bool ApReadItemsFile(std::vector<ApReceivedItem>& items) {
    std::string contents;
    if (!ApReadWholeFile(PathInGameDir(L"dr2_ap_items.txt"), contents))
        return false;

    for (const std::string& line : ApSplitLines(contents)) {
        if (line.compare(0, 5, "seed ") == 0) {
            ApUseSeed(line.substr(5));
        }
        else if (line.compare(0, 7, "option ") == 0) {
            const size_t space = line.find(' ', 7);
            if (space == std::string::npos)
                continue;
            const std::string key = line.substr(7, space - 7);
            const std::string value = line.substr(space + 1);

            if (key == "kill_milestones")
                g_ap_option_kills = atoi(value.c_str());
            else if (key == "level_checks")
                g_ap_option_levels = atoi(value.c_str());
            else if (key == "zombrex_checks")
                g_ap_option_zombrex = atoi(value.c_str());
            else if (key == "psychopaths")
                g_ap_option_psychopaths = value == "on";
            else if (key == "survivors")
                g_ap_option_survivors = value == "on";
            else if (key == "katey_gifts")
                g_ap_option_gifts = value == "on";
            else if (key == "movie_posters")
                g_ap_option_posters = value == "on";
        }
        else if (line.compare(0, 6, "tally ") == 0) {
            continue;
        }
        else {
            const size_t first = line.find('|');
            const size_t second = first == std::string::npos ? std::string::npos : line.find('|', first + 1);
            if (second == std::string::npos)
                continue;

            ApReceivedItem item;
            item.index = _strtoi64(line.c_str(), nullptr, 10);
            item.name = line.substr(first + 1, second - first - 1);
            item.sender = line.substr(second + 1);
            if (item.index > 0)
                items.push_back(item);
        }
    }
    return true;
}

// True while the client is alive: it touches the items file every second.
static bool ApItemsFileIsFresh() {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(PathInGameDir(L"dr2_ap_items.txt").c_str(), GetFileExInfoStandard, &data))
        return false;

    if (g_ap_hold_until != 0 && static_cast<int32_t>(g_ap_hold_until - GetTickCount()) > 0)
        return true;

    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER written, current;
    written.LowPart = data.ftLastWriteTime.dwLowDateTime;
    written.HighPart = data.ftLastWriteTime.dwHighDateTime;
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;

    // File times count in units of 100 nanoseconds.
    return current.QuadPart < written.QuadPart || current.QuadPart - written.QuadPart < 15ULL * 10000000ULL;
}

static void ApHandOutItems(const std::vector<ApReceivedItem>& items, uintptr_t stats) {
    long long applied = ApStateGet("ap_index");

    // After "regive <n>": the last n items again, without traps or case gates.
    if (g_ap_regive_count > 0) {
        int given = 0;
        for (const ApReceivedItem& received : items) {
            if (received.index > applied || received.index <= applied - g_ap_regive_count)
                continue;
            const ApItem* const item = ApFindItem(received.name);
            if (item != nullptr && item->kind != kApItemTrap && item->kind != kApItemCaseGate && ApApplyItem(*item, stats))
                ++given;
        }
        Log("AP: handed out %d of the last %d item(s) again", given, g_ap_regive_count);
        ApToast("Archipelago", std::to_string(given) + " item(s) handed out again");
        g_ap_regive_count = 0;
    }

    int pending = 0;
    for (const ApReceivedItem& received : items) {
        if (received.index > applied)
            ++pending;
    }
    if (pending == 0)
        return;

    int handed = 0;
    for (const ApReceivedItem& received : items) {
        if (received.index <= applied)
            continue;

        const ApItem* const item = ApFindItem(received.name);
        if (item == nullptr) {
            Log("AP: item %d \"%s\" is not known to this plugin version; skipped", static_cast<int>(received.index), received.name.c_str());
        }
        else if (!ApApplyItem(*item, stats)) {
            Log("AP: item %d \"%s\" could not be handed out right now; it will be tried again", static_cast<int>(received.index), received.name.c_str());
            break;
        }
        else {
            Log("AP: ITEM %d %s%s%s", static_cast<int>(received.index), received.name.c_str(), received.sender.empty() ? "" : " from ", received.sender.c_str());
        }

        applied = received.index;
        ApStateSet("ap_index", applied);
        ++handed;

        // A few at a time get a message box each; a pile gets one box for the lot.
        if (pending <= 3)
            ApToast("Archipelago", ApItemMessage(item, received.name, received.sender));
    }

    if (pending > 3 && handed > 0)
        ApToast("Archipelago", std::to_string(handed) + " items received");
}

static void ApDoDeathLink(uintptr_t stats) {
    g_ap_deathlink_pending = false;
    // The death this causes is not sent back out. Chuck only dies at his next hit or landing, which can be a
    // while, so the wait is ten minutes; it ends early when the death is seen or a save is loaded.
    g_ap_ignore_death_until = GetTickCount() + 600000;
    if (g_ap_ignore_death_until == 0)
        g_ap_ignore_death_until = 1;

    // Health to zero. The game notices at Chuck's next hit or landing.
    SafeWrite32(stats + kStatsHealth, FloatToBits(0.0f));
    Log("AP: DeathLink received: %s", g_ap_deathlink_text.c_str());
    ApToast("DeathLink", g_ap_deathlink_text);
}

// ---------------------------------------------------------------- commands from the client
// Returns true if the line was one of ours.

static bool ApHandleCommand(const char* line) {
    if (strncmp(line, "deathlink ", 10) == 0) {
        g_ap_deathlink_pending = true;
        g_ap_deathlink_text = line + 10;
        return true;
    }
    if (strncmp(line, "found ", 6) == 0) {
        ApToast("Archipelago", line + 6);
        return true;
    }
    if (strncmp(line, "sendcheck ", 10) == 0) {
        ApSendLine(line + 10);
        return true;
    }
    if (strncmp(line, "regive ", 7) == 0) {
        g_ap_regive_count = atoi(line + 7);
        return true;
    }
    if (strncmp(line, "aphold ", 7) == 0) {
        // Test aid: act as if the client were running for this many seconds.
        g_ap_hold_until = GetTickCount() + static_cast<DWORD>(atoi(line + 7)) * 1000;
        Log("AP: treating the items file as fresh for %d second(s)", atoi(line + 7));
        return true;
    }
    if (strncmp(line, "fire ", 5) == 0) {
        Log(ApFireScriptNode(line + 5) ? "fired" : "could not fire");
        return true;
    }
    if (strncmp(line, "node ", 5) == 0) {
        // Research: where a named script part is, and the 64 values it starts with.
        const uint32_t node = ApFindScriptNode(line + 5);
        for (uint32_t offset = 0; node != 0 && offset < 0x100; offset += 4) {
            uint32_t value = 0;
            if (SafeRead32(node + offset, &value) && value != 0)
                Log("  +0x%02X = %d (hex 0x%08X, float %g)", offset, static_cast<int>(value), value, BitsToFloat(value));
        }
        return true;
    }
    if (strcmp(line, "missionwatch on") == 0 || strcmp(line, "missionwatch off") == 0) {
        g_ap_mission_watch = strcmp(line, "missionwatch on") == 0;
        Log("mission watch %s", g_ap_mission_watch ? "on" : "off");
        return true;
    }
    if (strcmp(line, "missions") == 0) {
        ApListMissions();
        return true;
    }
    if (strcmp(line, "horde") == 0 || strcmp(line, "looters") == 0 || strcmp(line, "mercs") == 0) {
        const bool done = strcmp(line, "horde") == 0 ? ApHordeTrap() : strcmp(line, "looters") == 0 ? ApLooterTrap() : ApMercenaryTrap();
        Log(done ? "spawned" : "could not spawn");
        return true;
    }
    if (strncmp(line, "pattern ", 8) == 0) {
        // Research: find a run of bytes in the game's own code as it sits in memory, e.g. "pattern 01 51 20 01 41 1C".
        // Used to locate a routine that someone else has described by its bytes (a cheat table, a forum post).
        unsigned char wanted[32];
        size_t length = 0;
        for (const char* at = line + 8; *at != 0 && length < sizeof(wanted);) {
            char* end = nullptr;
            const unsigned long value = strtoul(at, &end, 16);
            if (end == at || value > 0xFF)
                break;
            wanted[length++] = static_cast<unsigned char>(value);
            at = end;
        }
        if (length < 4) {
            Log("  give at least four bytes, in hex, separated by spaces");
            return true;
        }

        int found = 0;
        unsigned char block[0x1000 + sizeof(wanted)];
        for (uintptr_t page = g_exe_start; page < g_exe_end && found < 16; page += 0x1000) {
            const size_t available = SafeCopy(page, block, sizeof(block));
            for (size_t i = 0; i + length <= available && i < 0x1000 && found < 16; ++i) {
                if (block[i] == wanted[0] && memcmp(block + i, wanted, length) == 0) {
                    Log("  found at 0x%08X [exe+0x%X]", static_cast<unsigned int>(page + i), static_cast<unsigned int>(page + i - g_exe_start));
                    ++found;
                }
            }
        }
        Log("  %d place(s)%s", found, found == 16 ? " (stopped at 16)" : "");
        return true;
    }
    if (strncmp(line, "slots", 5) == 0 && (line[5] == 0 || line[5] == ' ')) {
        // Research: Chuck's upgrade counts in the stats block, as a cheat table for another build of the game
        // gives them: +0x54 attack (most 7), +0x58 speed (3), +0x5C health blocks (12), +0x60 item slots (12),
        // +0x64 throwing distance (5). "slots" shows them; "slots health 6" writes one. Not yet proven on this build.
        static const struct { const char* name; uint32_t offset; uint32_t most; } kSlots[] = {
            { "attack", 0x54, 7 }, { "speed", 0x58, 3 }, { "health", 0x5C, 12 }, { "items", 0x60, 12 }, { "throw", 0x64, 5 } };
        const uintptr_t stats = FindPlayerStats();
        if (stats == 0) {
            Log("  the player's stats were not found");
            return true;
        }
        for (const auto& slot : kSlots) {
            // "slots <name> <number>"
            const size_t name_length = strlen(slot.name);
            if (line[5] != ' ' || strncmp(line + 6, slot.name, name_length) != 0 || line[6 + name_length] != ' ')
                continue;
            const int value = atoi(line + 7 + name_length);
            if (value >= 1 && static_cast<uint32_t>(value) <= slot.most) {
                SafeWrite32(stats + slot.offset, static_cast<uint32_t>(value));
                Log("  %s set to %d", slot.name, value);
            }
            else {
                Log("  %s takes 1 to %u", slot.name, slot.most);
            }
        }
        for (const auto& slot : kSlots) {
            uint32_t now = 0;
            SafeRead32(stats + slot.offset, &now);
            Log("  %-6s (+0x%02X) = %u   (most %u)", slot.name, slot.offset, now, slot.most);
        }
        return true;
    }
    if (strcmp(line, "party") == 0) {
        ApLogParty();
        return true;
    }
    if (strcmp(line, "naked") == 0) {
        Log(ApNakedTrap() ? "naked trap ran" : "naked trap could not run");
        return true;
    }
    if (strcmp(line, "ap") == 0) {
        const uintptr_t stats = FindPlayerStats();
        uint32_t level = 0, health = 0, kills = 0;
        if (stats != 0) {
            SafeRead32(stats + kStatsLevel, &level);
            SafeRead32(stats + kStatsHealth, &health);
            SafeRead32(stats + kStatsKills, &kills);
        }
        Log("AP status: %s, multiworld \"%s\", live %s, %u check(s) found, %d item(s) handed out",
            g_ap_active ? "linked" : "not linked (no client running)", g_ap_seed.c_str(), g_ap_was_live ? "yes" : "no",
            static_cast<unsigned int>(g_ap_sent.size()), static_cast<int>(ApStateGet("ap_index")));
        Log("  stats 0x%08X: level field %d, health %g, kills %u", static_cast<unsigned int>(stats), static_cast<int>(level), BitsToFloat(health), kills);
        Log("  player 0x%08X, naked outfit 0x%08X, followers %d", static_cast<unsigned int>(ApFindPlayer()),
            static_cast<unsigned int>(g_ap_naked_def), ApPartyCount());
        Log("  options: kills %d, levels %d, zombrex %d, psychopaths %s, survivors %s", g_ap_option_kills, g_ap_option_levels,
            g_ap_option_zombrex, g_ap_option_psychopaths ? "on" : "off", g_ap_option_survivors ? "on" : "off");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- once per pass of the plugin's loop

static DWORD g_ap_last_slow_tick;
static DWORD g_ap_last_owed_tick;

static void ApTick() {
    if (InterlockedExchange(&g_ap_activity, 0) != 0 && !g_ap_in_play) {
        g_ap_in_play = true;
        Log("AP: the game is being played");
    }

    // Events from the game. Without a linked multiworld they are only noted in the log.
    ApEvent event;
    while (ApPopEvent(&event)) {
        if (!g_ap_active) {
            if (event.type == kApEventSurvivor)
                Log("AP (not linked): survivor %s status %d", event.text, event.a);
            else if (event.type == kApEventDeath)
                Log("AP (not linked): death seen");
            continue;
        }

        switch (event.type) {
        case kApEventSurvivor:
            ApOnSurvivor(event.a, event.text);
            break;
        case kApEventKeyItem:
            ApOnKeyItem(event.a);
            break;
        case kApEventDeath:
            ApOnDeath(event.a);
            break;
        case kApEventLevel:
            ApOnLevel(event.a);
            break;
        default:
            break;
        }
    }

    // The slower work runs twice a second.
    const DWORD now = GetTickCount();
    if (now - g_ap_last_slow_tick < 500)
        return;
    g_ap_last_slow_tick = now;

    const bool fresh = ApItemsFileIsFresh();
    std::vector<ApReceivedItem> items;
    const bool linked = fresh && ApReadItemsFile(items) && !g_ap_seed.empty();
    if (linked != g_ap_active) {
        g_ap_active = linked;
        Log(linked ? "AP: linked to the client" : "AP: the client is gone; standing by");
    }
    if (!g_ap_active)
        return;

    const uintptr_t stats = FindPlayerStats();
    if (stats == 0)
        return;

    ApCheckKills(stats);

    // Nothing is delivered at the main menu or while a save loads.
    const bool live = ApGameIsLive();
    if (live != g_ap_was_live) {
        g_ap_was_live = live;
        Log(live ? "AP: the game is live" : "AP: the game is not live (menu or loading); holding deliveries");
        if (live) {
            g_ap_in_play = true;
        }
        else {
            // A load throws away and rebuilds most of what the plugin has found; forget it all.
            g_ap_watched_missions.clear();
            InterlockedExchange(&g_ap_player, 0);
            g_ap_party = 0;
            g_ap_naked_def = 0;
            g_ap_naked_search_misses = 0;
            // A DeathLink that was waiting for Chuck's death is over: the load undid it.
            if (g_ap_ignore_death_until != 0 && static_cast<int32_t>(g_ap_ignore_death_until - now) > 15000)
                g_ap_ignore_death_until = 0;
        }
    }
    if (!live || !g_ap_in_play)
        return;

    ApWatchMissions();
    ApCheckCounters();

    if (g_ap_deathlink_pending)
        ApDoDeathLink(stats);
    ApHandOutItems(items, stats);

    // Traps that could not run when they arrived: every ten seconds, one of each kind is tried again.
    if (now - g_ap_last_owed_tick >= 10000) {
        g_ap_last_owed_tick = now;
        for (const int trap : kApOwedTraps) {
            const std::string key = ApOwedKey(trap);
            if (ApStateGet(key) > 0 && ApRunTrap(trap))
                ApStateSet(key, ApStateGet(key) - 1);
        }
    }
}

static void ApStart() {
    g_ap_event_head = g_ap_event_tail = 0;
    g_ap_active = false;
    g_ap_in_play = false;
    g_ap_kills_known = false;
    g_ap_deathlink_pending = false;
    g_ap_regive_count = 0;
}

static void ApStop() {
}

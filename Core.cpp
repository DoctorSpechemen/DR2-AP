// DR2AP core - Dead Rising 2 Archipelago plugin logic (test build 1.12)
//
// Built as DR2AP_core.dll. The small loader (DR2AP.asi, see Loader.cpp) loads a copy of this file,
// and loads a fresh copy again when the "reload" command arrives. That means a new build of this
// file can be swapped in while the game keeps running.
//
// Link with the outside world (all files live in the game folder):
//   dr2_ap.log     - everything this plugin reports
//   dr2_ap_in.txt  - commands, one per line; read, then deleted
//   dr2_ap_out.txt - a line is appended when F7 is pressed in game
//
// Build 0.2 adds research commands for finding where the game keeps its values:
//   scan <number>          find every 4-byte whole number equal to <number>
//   scanf <number>         the same, for a decimal (float) value
//   next <number>          keep only the previous results that now equal <number>
//   nextf <number>         the same, for a float value
//   list                   show the current results
//   read <address> [n]     show n 4-byte values starting at <address> (default 8)
//   write <address> <n>    write a 4-byte whole number
//   ptr <address> [range]  find pointers that point at, or up to <range> bytes before, <address>
//
// Build 0.3 adds:
//   player                 show money, PP and zombie kills, found through the fixed route
//   setmoney <n>           set money
//   addmoney <n>           add to money (negative numbers take it away)
//   watch <address>        record which game code writes to this 4-byte value
//   hits                   show what "watch" has recorded so far
//   unwatch                stop watching
//   disasm <address> [n]   show n machine instructions starting at <address> (default 24)
//
// Build 0.4 adds:
//   reload                 load a fresh DR2AP_core.dll without restarting the game
//   find <text>            find a piece of text anywhere in memory
//   refs <address>         find every place (code and data included) that holds this address
//   snap <address> <size>  remember a block of memory (size in hex, up to 0x100000)
//   diff                   show which 4-byte values in that block changed since "snap"
//   call <address> <ecx> <edx> [args...]   call game code; use 0 for ecx/edx when unused
//   setpp <n>, addpp <n>   set or add PP
//
// Build 0.5 adds:
//   mcall <address> <ecx> <edx> [args...]   like "call", but run on the game's own main thread
//   givemoney <n>          add money through the game's own routine (main thread)
//   givepp <n> [label]     award PP through the game's own routine (main thread)
//
// Build 0.6 adds:
//   toast <title>|<text>   show an Archipelago message box drawn by the plugin itself
//
// Build 0.7 adds:
//   events on | off        start or stop noting every message the game sends through its message system
//   events clear           forget what has been noted
//   events                 list the kinds of message seen, how often, and what they carried
//
// Build 0.9: while listening, every message is also written in order, with the time, to
// dr2_ap_events.log (kinds that fire constantly are thinned out after their first 30).
//   probe <address>        note the processor's registers each time the game runs this instruction
//   unprobe                remove all probes
//
// Build 1.0 adds the Archipelago link itself (see Ap.inc): checks go out, items come in, DeathLink both ways.
//   ap                     show the state of the link
//   aphold <seconds>       test aid: act as if the client were running
//   naked                  run the naked trap now
//   wearlog on | off       note what clothing definitions look like as clothes are put on
//   deathlink <text>, found <text>, sendcheck <location>, regive <n>    what the client sends
// and more research commands:
//   scanr <low> <high>     find every 4-byte whole number in a range; scanfr does the same for floats (0 or more)
//   nextr / nextfr <low> <high>   keep the results that are now in the range
//   nextinc, nextdec       keep the results that went up / down since the last scan or filter (whole numbers)
//   nextincf, nextdecf     the same for floats
//   nextsame, nextdiff     keep the results that stayed the same / changed
//   obj <address> [n]      show an object's first n virtual functions (default 12)
//   text <address>         show the text at an address
//   writef <address> <x>   write a float
//
// Build 1.1: the message box now also works when the plugin starts before the game's graphics do.
//   missions               list every mission the game has loaded, with its state
//   fire <node name>       run the actions under a named part of a mission script (e.g. fire AP_OnTele3)
//   node <node name>       show where a named script part is and what it holds
//   horde, looters, mercs  spawn that kind of enemy around Chuck now (only works where the area has them loaded)
//   missionwatch on | off  log every mission that starts, ends or changes result (on by default)
// Addresses are written in hex, e.g. 0x1A2B3C4D.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d9.h>
#include <mmsystem.h>

#include <Zydis.h>
#include <safetyhook.hpp>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::wstring g_game_dir;
static CRITICAL_SECTION g_log_lock;

static const size_t kMaxResults = 500000;
static const size_t kMaxListed = 60;
static uintptr_t* g_results;
static uint32_t* g_result_values;  // What each result held at the last scan or filter.
static size_t g_result_count;
static bool g_results_truncated;

static uintptr_t g_exe_start, g_exe_end;
static uintptr_t g_self_base;
static bool g_reload_requested;

// ---------------------------------------------------------------- files and logging

static std::wstring PathInGameDir(const wchar_t* name) {
    return g_game_dir + name;
}

static void AppendLine(const wchar_t* file_name, const char* text) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, PathInGameDir(file_name).c_str(), L"ab") != 0 || file == nullptr)
        return;

    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(file, "[%02u:%02u:%02u.%03u] %s\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    fclose(file);
}

static void Log(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);

    EnterCriticalSection(&g_log_lock);
    AppendLine(L"dr2_ap.log", buffer);
    LeaveCriticalSection(&g_log_lock);
}

// ---------------------------------------------------------------- safe memory access
// These use Windows structured exception handling, so a bad address is reported instead of crashing the game.

static bool SafeRead32(uintptr_t address, uint32_t* value) {
    __try {
        *value = *reinterpret_cast<const uint32_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeWrite32(uintptr_t address, uint32_t value) {
    __try {
        *reinterpret_cast<uint32_t*>(address) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Records every aligned 4-byte value in the region that lies between 'low' and 'high' inclusive.
static void ScanRegion(uintptr_t start, size_t size, uint32_t low, uint32_t high) {
    __try {
        const uint32_t* cursor = reinterpret_cast<const uint32_t*>(start);
        const uint32_t* const end = cursor + size / sizeof(uint32_t);

        for (; cursor < end; ++cursor) {
            const uint32_t value = *cursor;
            if (value >= low && value <= high) {
                if (g_result_count >= kMaxResults) {
                    g_results_truncated = true;
                    return;
                }
                g_results[g_result_count++] = reinterpret_cast<uintptr_t>(cursor);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // The region went away while we were reading it; keep what we found.
    }
}

// ---------------------------------------------------------------- scanning

// When true, scans also cover read-only memory and code, not just memory the game can write to.
static bool g_scan_everything;

static bool IsScannable(const MEMORY_BASIC_INFORMATION& info) {
    if (info.State != MEM_COMMIT)
        return false;
    if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const DWORD readable = writable | PAGE_READONLY | PAGE_EXECUTE_READ;
    if ((info.Protect & (g_scan_everything ? readable : writable)) == 0)
        return false;

    // Skip our own memory, which would otherwise show up as false matches.
    const uintptr_t allocation_base = reinterpret_cast<uintptr_t>(info.AllocationBase);
    if (allocation_base == reinterpret_cast<uintptr_t>(g_results) || allocation_base == reinterpret_cast<uintptr_t>(g_result_values) || allocation_base == g_self_base)
        return false;

    return true;
}

static void ScanAll(uint32_t low, uint32_t high) {
    g_result_count = 0;
    g_results_truncated = false;

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    uintptr_t address = reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION info;
    while (address < maximum && VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const size_t size = info.RegionSize;

        if (IsScannable(info))
            ScanRegion(base, size, low, high);

        const uintptr_t next = base + size;
        if (next <= address)
            break; // Wrapped around the top of the address space.
        address = next;
    }
}


// Remembers what every result holds right now, for the "went up / went down / changed" filters.
static void CaptureResultValues() {
    for (size_t i = 0; i < g_result_count; ++i) {
        uint32_t value = 0;
        SafeRead32(g_results[i], &value);
        g_result_values[i] = value;
    }
}

enum FilterMode { kFilterRange, kFilterIncreased, kFilterDecreased, kFilterIncreasedFloat, kFilterDecreasedFloat, kFilterSame, kFilterDifferent };

static float BitsToFloat(uint32_t bits);

// Keeps only the results that pass the test, then remembers their new values.
static void FilterResultsBy(FilterMode mode, uint32_t low, uint32_t high) {
    size_t kept = 0;
    for (size_t i = 0; i < g_result_count; ++i) {
        uint32_t value;
        if (!SafeRead32(g_results[i], &value))
            continue;

        const uint32_t before = g_result_values[i];
        bool keep = false;
        switch (mode) {
        case kFilterRange:          keep = value >= low && value <= high; break;
        case kFilterIncreased:      keep = static_cast<int32_t>(value) > static_cast<int32_t>(before); break;
        case kFilterDecreased:      keep = static_cast<int32_t>(value) < static_cast<int32_t>(before); break;
        case kFilterIncreasedFloat: keep = BitsToFloat(value) > BitsToFloat(before); break;
        case kFilterDecreasedFloat: keep = BitsToFloat(value) < BitsToFloat(before); break;
        case kFilterSame:           keep = value == before; break;
        case kFilterDifferent:      keep = value != before; break;
        }

        if (keep) {
            g_results[kept] = g_results[i];
            g_result_values[kept] = value;
            ++kept;
        }
    }
    g_result_count = kept;
}

static bool IsInExe(uintptr_t address) {
    return address >= g_exe_start && address < g_exe_end;
}

static void DescribeLocation(uintptr_t address, char* buffer, size_t buffer_size) {
    if (IsInExe(address))
        sprintf_s(buffer, buffer_size, "exe+0x%X", static_cast<unsigned int>(address - g_exe_start));
    else
        sprintf_s(buffer, buffer_size, "heap");
}

static float BitsToFloat(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t FloatToBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void LogValueAt(uintptr_t address) {
    char location[32];
    DescribeLocation(address, location, sizeof(location));

    uint32_t value;
    if (SafeRead32(address, &value))
        Log("  0x%08X = %d (hex 0x%08X, float %g) [%s]", static_cast<unsigned int>(address), static_cast<int>(value), value, BitsToFloat(value), location);
    else
        Log("  0x%08X = <unreadable> [%s]", static_cast<unsigned int>(address), location);
}

static void ListResults() {
    Log("%u result(s)%s", static_cast<unsigned int>(g_result_count), g_results_truncated ? " (stopped at the limit; there are more)" : "");

    const size_t shown = g_result_count < kMaxListed ? g_result_count : kMaxListed;
    for (size_t i = 0; i < shown; ++i)
        LogValueAt(g_results[i]);

    if (shown < g_result_count)
        Log("  ... %u more not shown", static_cast<unsigned int>(g_result_count - shown));
}

static void FindPointers(uintptr_t target, uint32_t range) {
    const uint32_t high = static_cast<uint32_t>(target);
    const uint32_t low = high >= range ? high - range : 0;
    ScanAll(low, high);

    Log("%u pointer(s) to 0x%08X or up to 0x%X before it%s", static_cast<unsigned int>(g_result_count), high, range, g_results_truncated ? " (stopped at the limit; there are more)" : "");

    // Pointers stored inside the game's own image come first: those stay at the same place every run.
    size_t shown = 0;
    for (int pass = 0; pass < 2 && shown < kMaxListed; ++pass) {
        for (size_t i = 0; i < g_result_count && shown < kMaxListed; ++i) {
            const uintptr_t holder = g_results[i];
            if (IsInExe(holder) != (pass == 0))
                continue;

            uint32_t pointer;
            if (!SafeRead32(holder, &pointer))
                continue;

            char location[32];
            DescribeLocation(holder, location, sizeof(location));
            Log("  [0x%08X] = 0x%08X, target is that +0x%X [%s]", static_cast<unsigned int>(holder), pointer, high - pointer, location);
            ++shown;
        }
    }

    if (shown < g_result_count)
        Log("  ... %u more not shown", static_cast<unsigned int>(g_result_count - shown));
}

// ---------------------------------------------------------------- player values
// Route found by searching on 2026-11-15 (Steam build, image base 0x00400000):
//   [exe + 0x9DE9A8] -> manager object; [manager + 0x8] -> player stats block.

static const uintptr_t kManagerPointerOffset = 0x9DE9A8;
static const uintptr_t kStatsVtableOffset = 0x8B5880;
static const uintptr_t kStatsPP = 0x24;
static const uintptr_t kStatsKills = 0x38;
static const uintptr_t kStatsMoney = 0x70;

// Returns the address of the player stats block, or 0 if it is not there (e.g. at the main menu).
static uintptr_t FindPlayerStats() {
    uint32_t manager = 0;
    if (!SafeRead32(g_exe_start + kManagerPointerOffset, &manager) || manager == 0)
        return 0;

    uint32_t stats = 0;
    if (!SafeRead32(manager + 0x8, &stats) || stats == 0)
        return 0;

    // Every object of this kind starts with the same marker; check it so we never write to the wrong thing.
    uint32_t marker = 0;
    if (!SafeRead32(stats, &marker) || marker != g_exe_start + kStatsVtableOffset)
        return 0;

    return stats;
}

static void LogPlayer() {
    const uintptr_t stats = FindPlayerStats();
    if (stats == 0) {
        Log("player stats not found (not in game, or the route is wrong)");
        return;
    }

    uint32_t money = 0, pp = 0, kills = 0;
    SafeRead32(stats + kStatsMoney, &money);
    SafeRead32(stats + kStatsPP, &pp);
    SafeRead32(stats + kStatsKills, &kills);
    Log("player stats at 0x%08X: money %d, PP %d, kills %d", static_cast<unsigned int>(stats), static_cast<int>(money), static_cast<int>(pp), static_cast<int>(kills));
}

static void SetMoney(int amount, bool relative) {
    const uintptr_t stats = FindPlayerStats();
    if (stats == 0) {
        Log("player stats not found; money not changed");
        return;
    }

    uint32_t money = 0;
    SafeRead32(stats + kStatsMoney, &money);

    int updated = relative ? static_cast<int>(money) + amount : amount;
    if (updated < 0)
        updated = 0;

    SafeWrite32(stats + kStatsMoney, static_cast<uint32_t>(updated));
    Log("money %d -> %d", static_cast<int>(money), updated);
}

static void SetPP(int amount, bool relative) {
    const uintptr_t stats = FindPlayerStats();
    if (stats == 0) {
        Log("player stats not found; PP not changed");
        return;
    }

    uint32_t pp = 0;
    SafeRead32(stats + kStatsPP, &pp);

    int updated = relative ? static_cast<int>(pp) + amount : amount;
    if (updated < 0)
        updated = 0;

    // The game keeps PP twice, side by side; keep both in step.
    SafeWrite32(stats + kStatsPP, static_cast<uint32_t>(updated));
    SafeWrite32(stats + kStatsPP + 4, static_cast<uint32_t>(updated));
    Log("PP %d -> %d", static_cast<int>(pp), updated);
}

static size_t SafeCopy(uintptr_t address, unsigned char* buffer, size_t size);

// ---------------------------------------------------------------- text search

static const char* g_find_text;
static size_t g_find_length;

static void FindTextInRegion(uintptr_t start, size_t size) {
    __try {
        if (size < g_find_length)
            return;

        const char* const begin = reinterpret_cast<const char*>(start);
        const char* const end = begin + (size - g_find_length);
        const char first = g_find_text[0];

        for (const char* cursor = begin; cursor <= end; ++cursor) {
            if (*cursor == first && memcmp(cursor, g_find_text, g_find_length) == 0) {
                if (g_result_count >= kMaxResults) {
                    g_results_truncated = true;
                    return;
                }
                g_results[g_result_count++] = reinterpret_cast<uintptr_t>(cursor);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void FindText(const char* text) {
    g_find_text = text;
    g_find_length = strlen(text);
    g_result_count = 0;
    g_results_truncated = false;
    if (g_find_length == 0)
        return;

    g_scan_everything = true;

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    uintptr_t address = reinterpret_cast<uintptr_t>(system_info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(system_info.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION info;
    while (address < maximum && VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const size_t size = info.RegionSize;

        if (IsScannable(info))
            FindTextInRegion(base, size);

        const uintptr_t next = base + size;
        if (next <= address)
            break;
        address = next;
    }

    g_scan_everything = false;

    Log("%u place(s) hold the text \"%s\"%s", static_cast<unsigned int>(g_result_count), text, g_results_truncated ? " (stopped at the limit)" : "");
    const size_t shown = g_result_count < kMaxListed ? g_result_count : kMaxListed;
    for (size_t i = 0; i < shown; ++i) {
        char location[32];
        DescribeLocation(g_results[i], location, sizeof(location));
        Log("  0x%08X [%s]", static_cast<unsigned int>(g_results[i]), location);
    }
}

// ---------------------------------------------------------------- snapshot and compare

static const size_t kMaxSnapshot = 0x100000;
static unsigned char* g_snapshot;
static uintptr_t g_snapshot_address;
static size_t g_snapshot_size;

static void TakeSnapshot(uintptr_t address, size_t size) {
    if (size > kMaxSnapshot)
        size = kMaxSnapshot;

    g_snapshot_address = address;
    g_snapshot_size = SafeCopy(address, g_snapshot, size);
    Log("remembered 0x%X byte(s) starting at 0x%08X", static_cast<unsigned int>(g_snapshot_size), static_cast<unsigned int>(address));
}

static void CompareSnapshot() {
    unsigned int changed = 0;
    for (size_t offset = 0; offset + 4 <= g_snapshot_size; offset += 4) {
        uint32_t before;
        memcpy(&before, g_snapshot + offset, sizeof(before));

        uint32_t now;
        if (!SafeRead32(g_snapshot_address + offset, &now) || now == before)
            continue;

        ++changed;
        if (changed <= 120)
            Log("  0x%08X (+0x%X): %d -> %d (hex 0x%08X -> 0x%08X)", static_cast<unsigned int>(g_snapshot_address + offset), static_cast<unsigned int>(offset), static_cast<int>(before), static_cast<int>(now), before, now);
    }
    Log("%u value(s) changed since the snapshot", changed);
}

// ---------------------------------------------------------------- calling game code
// Runs a game function from this plugin's own thread. That is fine for simple functions, but game code
// that expects to run on the game's main thread may misbehave, so this is a research tool.

static bool CallGameFunction(uintptr_t function, uint32_t ecx_value, uint32_t edx_value, const uint32_t* arguments, uint32_t argument_count, uint32_t* result_out) {
    uint32_t result = 0;
    uint32_t saved_esp = 0;

    __try {
        __asm {
            mov saved_esp, esp
            mov ecx, argument_count
            mov eax, arguments
        push_next:
            test ecx, ecx
            jz make_call
            dec ecx
            push dword ptr [eax + ecx * 4]
            jmp push_next
        make_call:
            mov ecx, ecx_value
            mov edx, edx_value
            mov eax, function
            call eax
            mov esp, saved_esp
            mov result, eax
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    *result_out = result;
    return true;
}

// ---------------------------------------------------------------- running code on the game's main thread
// Game code generally expects to be run by the game's main thread. We borrow a call the game makes
// roughly once per frame (the same one the fix uses to poll controllers) and run our pending work there.

typedef double (__cdecl *TickFunction)(LARGE_INTEGER tick);

static unsigned char* g_tick_call_site;
static TickFunction g_original_tick;

struct PendingCall {
    uintptr_t function;
    uint32_t ecx_value;
    uint32_t edx_value;
    uint32_t arguments[8];
    uint32_t argument_count;
    uint32_t result;
    bool succeeded;
};

static PendingCall g_pending_call;
static volatile LONG g_pending_state; // 0 = nothing waiting, 1 = waiting to run, 2 = finished
static volatile LONG g_calls_in_progress;

static double __cdecl TickHook(LARGE_INTEGER tick) {
    InterlockedIncrement(&g_calls_in_progress);

    if (g_pending_state == 1) {
        g_pending_call.succeeded = CallGameFunction(g_pending_call.function, g_pending_call.ecx_value, g_pending_call.edx_value, g_pending_call.arguments, g_pending_call.argument_count, &g_pending_call.result);
        InterlockedExchange(&g_pending_state, 2);
    }

    const double result = g_original_tick(tick);
    InterlockedDecrement(&g_calls_in_progress);
    return result;
}

// Finds a byte pattern in the game's image. "pattern" is hex bytes separated by spaces, with ? for any byte.
static unsigned char* FindPatternInExe(const char* pattern) {
    unsigned char bytes[64];
    bool wildcard[64];
    size_t length = 0;

    for (const char* cursor = pattern; *cursor != '\0' && length < 64;) {
        if (*cursor == ' ') {
            ++cursor;
        }
        else if (*cursor == '?') {
            bytes[length] = 0;
            wildcard[length++] = true;
            while (*cursor == '?')
                ++cursor;
        }
        else {
            char* end = nullptr;
            bytes[length] = static_cast<unsigned char>(strtoul(cursor, &end, 16));
            wildcard[length++] = false;
            cursor = end;
        }
    }

    if (length == 0)
        return nullptr;

    unsigned char* found = nullptr;
    __try {
        unsigned char* const begin = reinterpret_cast<unsigned char*>(g_exe_start);
        unsigned char* const end = reinterpret_cast<unsigned char*>(g_exe_end) - length;

        for (unsigned char* cursor = begin; cursor < end && found == nullptr; ++cursor) {
            size_t i = 0;
            while (i < length && (wildcard[i] || cursor[i] == bytes[i]))
                ++i;
            if (i == length)
                found = cursor;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return found;
}

static bool WriteCallTarget(unsigned char* call_site, uintptr_t target) {
    DWORD old_protection = 0;
    if (!VirtualProtect(call_site, 5, PAGE_EXECUTE_READWRITE, &old_protection))
        return false;

    const int32_t relative = static_cast<int32_t>(target - (reinterpret_cast<uintptr_t>(call_site) + 5));
    memcpy(call_site + 1, &relative, sizeof(relative));

    VirtualProtect(call_site, 5, old_protection, &old_protection);
    FlushInstructionCache(GetCurrentProcess(), call_site, 5);
    return true;
}

static uintptr_t ReadCallTarget(const unsigned char* call_site) {
    int32_t relative;
    memcpy(&relative, call_site + 1, sizeof(relative));
    return reinterpret_cast<uintptr_t>(call_site) + 5 + relative;
}

static void InstallTickHook() {
    // The same call site ClownDR2Fix hooks; whatever is there now becomes "the original" for us.
    unsigned char* const site = FindPatternInExe("E8 ? ? ? ? ? ? ? ? 8B 0D ? ? ? ? 83 C4 ? 57");
    if (site == nullptr || site[0] != 0xE8) {
        Log("main-thread hook: call site not found; mcall/givemoney/givepp will not work");
        return;
    }

    g_original_tick = reinterpret_cast<TickFunction>(ReadCallTarget(site));
    if (!WriteCallTarget(site, reinterpret_cast<uintptr_t>(&TickHook))) {
        Log("main-thread hook: could not patch the call site");
        g_original_tick = nullptr;
        return;
    }

    g_tick_call_site = site;
    Log("main-thread hook installed at 0x%08X", static_cast<unsigned int>(reinterpret_cast<uintptr_t>(site)));
}

// The controller fix hooks the same call, and when both start with the game it does so after us, which
// silently cut our hook out (found 2026-11-15). So this is checked regularly: if the call no longer leads
// to us, whatever is there now becomes "the original" and we go back in front of it.
static void KeepTickHook() {
    if (g_tick_call_site == nullptr)
        return;

    const uintptr_t target = ReadCallTarget(g_tick_call_site);
    if (target == reinterpret_cast<uintptr_t>(&TickHook))
        return;

    g_original_tick = reinterpret_cast<TickFunction>(target);
    if (WriteCallTarget(g_tick_call_site, reinterpret_cast<uintptr_t>(&TickHook)))
        Log("main-thread hook had been replaced (by the controller fix); put back in front of it");
}

static void RemoveTickHook() {
    if (g_tick_call_site == nullptr)
        return;

    // Only put the original back if the call still points at us.
    if (ReadCallTarget(g_tick_call_site) == reinterpret_cast<uintptr_t>(&TickHook))
        WriteCallTarget(g_tick_call_site, reinterpret_cast<uintptr_t>(g_original_tick));

    // Wait until the game is no longer inside our hook before this file can be unloaded.
    for (int i = 0; i < 200 && g_calls_in_progress != 0; ++i)
        Sleep(10);
    Sleep(50);

    g_tick_call_site = nullptr;
}

// Queues a call for the main thread and waits for it to finish.
static bool CallOnMainThread(uintptr_t function, uint32_t ecx_value, uint32_t edx_value, const uint32_t* arguments, uint32_t argument_count, uint32_t* result_out) {
    if (g_tick_call_site == nullptr) {
        Log("main-thread hook is not installed");
        return false;
    }

    g_pending_call.function = function;
    g_pending_call.ecx_value = ecx_value;
    g_pending_call.edx_value = edx_value;
    g_pending_call.argument_count = argument_count > 8 ? 8 : argument_count;
    for (uint32_t i = 0; i < g_pending_call.argument_count; ++i)
        g_pending_call.arguments[i] = arguments[i];
    g_pending_call.succeeded = false;
    g_pending_call.result = 0;
    InterlockedExchange(&g_pending_state, 1);

    for (int i = 0; i < 300; ++i) {
        if (g_pending_state == 2) {
            InterlockedExchange(&g_pending_state, 0);
            *result_out = g_pending_call.result;
            if (!g_pending_call.succeeded)
                Log("the game code crashed (caught; the game keeps running)");
            return g_pending_call.succeeded;
        }
        Sleep(10);
    }

    // Give up, but only if the main thread has not started on it.
    if (InterlockedCompareExchange(&g_pending_state, 0, 1) != 1) {
        while (g_pending_state != 2)
            Sleep(10);
        InterlockedExchange(&g_pending_state, 0);
    }
    Log("the main thread did not pick the call up within 3 seconds (is the game paused or loading?)");
    return false;
}

// Found by watching the game on 2026-11-15 (Steam build):
//   0x00842060: change money. this = player stats; (amount, set-instead-of-add flag, source type)
//   0x00845EC0: award PP.     this = player stats; (amount, text label id, flag, flag)
static const uintptr_t kChangeMoneyFunction = 0x442060;
static const uintptr_t kAwardPPFunction = 0x445EC0;

static void GiveMoney(int amount) {
    const uintptr_t stats = FindPlayerStats();
    if (stats == 0) {
        Log("player stats not found; nothing given");
        return;
    }

    const uint32_t arguments[3] = { static_cast<uint32_t>(amount), 0, 0 };
    uint32_t result = 0;
    if (CallOnMainThread(g_exe_start + kChangeMoneyFunction, static_cast<uint32_t>(stats), 0, arguments, 3, &result))
        Log("gave %d money", amount);
}

static void GivePP(int amount, uint32_t label) {
    const uintptr_t stats = FindPlayerStats();
    if (stats == 0) {
        Log("player stats not found; nothing given");
        return;
    }

    const uint32_t arguments[4] = { static_cast<uint32_t>(amount), label, 1, 0 };
    uint32_t result = 0;
    if (CallOnMainThread(g_exe_start + kAwardPPFunction, static_cast<uint32_t>(stats), 0, arguments, 4, &result))
        Log("gave %d PP (label 0x%X)", amount, label);
}

// ---------------------------------------------------------------- on-screen messages
// The plugin draws its own message box over the finished frame, so it can show any text at all
// (item names, other players' names) without touching the game's own text tables.
//
// How: the game's Direct3D device has a table of functions. We swap the "Present" entry (called once
// per frame, when the finished picture is shown) for ours, draw the box, then call the real one.

static const int kToastWidth = 640;
static const int kToastHeight = 110;
static const DWORD kToastShowMs = 6000;
static const DWORD kToastFadeMs = 300;
static const size_t kMaxToasts = 8;
static const int kPresentSlot = 17;

struct Toast {
    wchar_t title[96];
    wchar_t text[256];
};

static CRITICAL_SECTION g_toast_lock;
static Toast g_toasts[kMaxToasts];
static size_t g_toast_count;
static DWORD g_toast_started;        // 0 = the front toast has not been drawn yet
static volatile LONG g_chime_wanted; // Set when a box appears; the plugin's own thread plays the sound.
static IDirect3DTexture9* g_toast_texture;

typedef HRESULT (__stdcall *PresentFunction)(IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty);
static void** g_device_vtable;
static PresentFunction g_original_present;
static volatile LONG g_presents_in_progress;

static void QueueToast(const wchar_t* title, const wchar_t* text) {
    EnterCriticalSection(&g_toast_lock);
    if (g_toast_count < kMaxToasts) {
        Toast& toast = g_toasts[g_toast_count++];
        wcsncpy_s(toast.title, title, _TRUNCATE);
        wcsncpy_s(toast.text, text, _TRUNCATE);
    }
    LeaveCriticalSection(&g_toast_lock);
}

// Draws the box's picture with ordinary Windows text drawing, then copies it into a texture.
static IDirect3DTexture9* BuildToastTexture(IDirect3DDevice9* device, const Toast& toast) {
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(kToastWidth, kToastHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || texture == nullptr)
        return nullptr;

    const HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = kToastWidth;
    info.bmiHeader.biHeight = -kToastHeight; // Negative = top row first.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HBITMAP bitmap = dc != nullptr ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        if (dc != nullptr)
            DeleteDC(dc);
        texture->Release();
        return nullptr;
    }

    const HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    uint32_t* const pixels = static_cast<uint32_t*>(bits);

    // Dark box with a coloured strip down the left edge.
    for (int y = 0; y < kToastHeight; ++y)
        for (int x = 0; x < kToastWidth; ++x)
            pixels[y * kToastWidth + x] = x < 8 ? 0x00C97EF0 : 0x00161A22;

    SetBkMode(dc, TRANSPARENT);

    const HFONT title_font = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const HFONT text_font = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const HGDIOBJ old_font = SelectObject(dc, title_font);
    SetTextColor(dc, RGB(0xC9, 0x7E, 0xF0)); // The same purple as the strip.
    RECT title_area = { 24, 10, kToastWidth - 16, 44 };
    DrawTextW(dc, toast.title, -1, &title_area, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, text_font);
    SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
    RECT text_area = { 24, 48, kToastWidth - 16, kToastHeight - 8 };
    DrawTextW(dc, toast.text, -1, &text_area, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

    GdiFlush();

    // Windows text drawing leaves the transparency byte empty; make the whole box nearly opaque.
    for (int i = 0; i < kToastWidth * kToastHeight; ++i)
        pixels[i] = (pixels[i] & 0x00FFFFFF) | 0xE8000000;

    D3DLOCKED_RECT locked;
    if (SUCCEEDED(texture->LockRect(0, &locked, nullptr, 0))) {
        for (int y = 0; y < kToastHeight; ++y)
            memcpy(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch, pixels + y * kToastWidth, kToastWidth * 4);
        texture->UnlockRect(0);
    }

    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(title_font);
    DeleteObject(text_font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return texture;
}

struct ToastVertex {
    float x, y, z, rhw;
    DWORD colour;
    float u, v;
};

static void DrawToast(IDirect3DDevice9* device, IDirect3DTexture9* texture, float opacity) {
    IDirect3DSurface9* back_buffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer)) || back_buffer == nullptr)
        return;

    D3DSURFACE_DESC description;
    back_buffer->GetDesc(&description);

    IDirect3DStateBlock9* saved_state = nullptr;
    IDirect3DSurface9* saved_target = nullptr;
    device->GetRenderTarget(0, &saved_target);

    if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &saved_state)) && saved_state != nullptr) {
        saved_state->Capture();

        device->SetRenderTarget(0, back_buffer);

        D3DVIEWPORT9 viewport = { 0, 0, description.Width, description.Height, 0.0f, 1.0f };
        device->SetViewport(&viewport);

        // Sized for a 1080-line screen and scaled from there.
        const float scale = description.Height / 1080.0f;
        const float width = kToastWidth * scale;
        const float height = kToastHeight * scale;
        const float left = (description.Width - width) * 0.5f - 0.5f;
        const float top = description.Height * 0.10f - 0.5f;

        const DWORD colour = (static_cast<DWORD>(opacity * 255.0f) << 24) | 0x00FFFFFF;
        const ToastVertex vertices[4] = {
            { left,         top,          0.0f, 1.0f, colour, 0.0f, 0.0f },
            { left + width, top,          0.0f, 1.0f, colour, 1.0f, 0.0f },
            { left,         top + height, 0.0f, 1.0f, colour, 0.0f, 1.0f },
            { left + width, top + height, 0.0f, 1.0f, colour, 1.0f, 1.0f },
        };

        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        device->SetTexture(0, texture);
        device->SetTexture(1, nullptr);

        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

        if (SUCCEEDED(device->BeginScene())) {
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ToastVertex));
            device->EndScene();
        }

        saved_state->Apply();
        saved_state->Release();
    }

    if (saved_target != nullptr) {
        device->SetRenderTarget(0, saved_target);
        saved_target->Release();
    }
    back_buffer->Release();
}

static HRESULT __stdcall PresentHook(IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
    InterlockedIncrement(&g_presents_in_progress);

    EnterCriticalSection(&g_toast_lock);
    if (g_toast_count != 0) {
        const DWORD now = GetTickCount();

        if (g_toast_started == 0) {
            if (g_toast_texture != nullptr) {
                g_toast_texture->Release();
                g_toast_texture = nullptr;
            }
            g_toast_texture = BuildToastTexture(device, g_toasts[0]);
            g_toast_started = now != 0 ? now : 1;
            InterlockedExchange(&g_chime_wanted, 1);
        }

        const DWORD elapsed = now - g_toast_started;
        if (elapsed >= kToastShowMs || g_toast_texture == nullptr) {
            // Finished: move the queue along.
            for (size_t i = 1; i < g_toast_count; ++i)
                g_toasts[i - 1] = g_toasts[i];
            --g_toast_count;
            g_toast_started = 0;
        }
        else {
            float opacity = 1.0f;
            if (elapsed < kToastFadeMs)
                opacity = static_cast<float>(elapsed) / kToastFadeMs;
            else if (elapsed > kToastShowMs - kToastFadeMs)
                opacity = static_cast<float>(kToastShowMs - elapsed) / kToastFadeMs;

            DrawToast(device, g_toast_texture, opacity);
        }
    }
    LeaveCriticalSection(&g_toast_lock);

    const HRESULT result = g_original_present(device, source, destination, window, dirty);
    InterlockedDecrement(&g_presents_in_progress);
    return result;
}

static bool WriteVtableSlot(void** slot, void* value) {
    DWORD old_protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old_protection, &old_protection);
    return true;
}

static bool g_present_hook_quiet;

static void InstallPresentHook() {
    // The game keeps a pointer to its graphics object here (the fix's source uses the same pattern),
    // and the Direct3D device sits at +0x198 inside that object in Dead Rising 2.
    const unsigned char* const site = FindPatternInExe("A3 ? ? ? ? A3 ? ? ? ? C3 33 C0");
    if (site == nullptr) {
        if (!g_present_hook_quiet)
            Log("message box: graphics pointer not found; toast will not work");
        return;
    }

    uint32_t graphics_pointer_address = 0;
    memcpy(&graphics_pointer_address, site + 1, sizeof(graphics_pointer_address));

    uint32_t graphics = 0, device_address = 0, vtable = 0;
    if (!SafeRead32(graphics_pointer_address, &graphics) || graphics == 0
        || !SafeRead32(graphics + 0x198, &device_address) || device_address == 0
        || !SafeRead32(device_address, &vtable) || vtable == 0) {
        // Normal when the plugin starts with the game: the graphics are not up yet. The main loop tries again.
        if (!g_present_hook_quiet)
            Log("message box: graphics not ready yet; will keep trying");
        g_present_hook_quiet = true;
        return;
    }

    void** const table = reinterpret_cast<void**>(static_cast<uintptr_t>(vtable));
    g_original_present = reinterpret_cast<PresentFunction>(table[kPresentSlot]);
    if (!WriteVtableSlot(&table[kPresentSlot], reinterpret_cast<void*>(&PresentHook))) {
        Log("message box: could not hook the device");
        g_original_present = nullptr;
        return;
    }

    g_device_vtable = table;
    Log("message box ready (device 0x%08X)", device_address);
}

static void RemovePresentHook() {
    if (g_device_vtable == nullptr)
        return;

    if (g_device_vtable[kPresentSlot] == reinterpret_cast<void*>(&PresentHook))
        WriteVtableSlot(&g_device_vtable[kPresentSlot], reinterpret_cast<void*>(g_original_present));

    for (int i = 0; i < 200 && g_presents_in_progress != 0; ++i)
        Sleep(10);
    Sleep(50);

    if (g_toast_texture != nullptr) {
        g_toast_texture->Release();
        g_toast_texture = nullptr;
    }
    g_toast_count = 0;
    g_toast_started = 0;
    g_device_vtable = nullptr;
}

// "title|text", as typed in a command (UTF-8).
static void ToastFromCommand(const char* rest) {
    wchar_t wide[400];
    if (MultiByteToWideChar(CP_UTF8, 0, rest, -1, wide, 400) == 0)
        return;

    wchar_t* const bar = wcschr(wide, L'|');
    if (bar != nullptr) {
        *bar = L'\0';
        QueueToast(wide, bar + 1);
    }
    else {
        QueueToast(L"Archipelago", wide);
    }
    Log("message queued");
}

// ---------------------------------------------------------------- listening to the game's messages
// The game announces things internally by sending small messages through one function: money changed
// (kind 0x61), PP awarded (kind 0x5F) and so on. Listening there shows what kind of message goes out
// when something happens (a death, a finished case, a rescue), which is how checks can be detected.
//
// A message is: [+0] a fixed marker, [+4] its kind, [+8...] whatever it carries.

static const uintptr_t kSendMessageFunction = 0xB190; // 0x0040B190 in the Steam build

static const size_t kMaxMessageKinds = 256;

struct SeenMessage {
    uint32_t kind;
    uint32_t count;
    uint32_t first[4];
    uint32_t last[4];
    uint32_t first_caller;
};

static SeenMessage g_seen_messages[kMaxMessageKinds];
static volatile LONG g_seen_message_count;
static volatile LONG g_seen_message_lock;
static volatile LONG g_message_listening;
static SafetyHookMid g_message_hook;

// Messages waiting to be written to dr2_ap_events.log by the plugin's own thread.
struct LoggedMessage {
    DWORD tick;
    uint32_t kind;
    uint32_t occurrence;
    uint32_t carried[6];
    uint32_t caller;
};

static const size_t kMessageRingSize = 1024;
static const size_t kKindCounters = 1024;
static LoggedMessage g_message_ring[kMessageRingSize];
static size_t g_message_ring_head; // Next slot to write.
static size_t g_message_ring_tail; // Next slot to read.
static uint32_t g_kind_counts[kKindCounters];

static void ApOnGameMessage(uint32_t kind, uint32_t caller, uint32_t message, const uint32_t* carried);

static void OnGameMessage(SafetyHookContext& context) {
    // At the start of the function: [esp] = where it was called from, [esp+4] = the message.
    uint32_t caller = 0, message = 0, kind = 0;
    if (!SafeRead32(context.esp, &caller) || !SafeRead32(context.esp + 4, &message) || message == 0 || !SafeRead32(message + 4, &kind))
        return;

    uint32_t carried[6] = {};
    for (int i = 0; i < 6; ++i)
        SafeRead32(message + 8 + i * 4, &carried[i]);

    // The Archipelago side looks at every message; the research log below only while "events on".
    ApOnGameMessage(kind, caller, message, carried);

    if (g_message_listening == 0)
        return;

    while (InterlockedExchange(&g_seen_message_lock, 1) != 0)
        YieldProcessor();

    // Queue it for the ordered log: the first 30 of each kind, then every 100th.
    const uint32_t occurrence = ++g_kind_counts[kind % kKindCounters];
    if (occurrence <= 30 || occurrence % 100 == 0) {
        const size_t next = (g_message_ring_head + 1) % kMessageRingSize;
        if (next != g_message_ring_tail) {
            LoggedMessage& logged = g_message_ring[g_message_ring_head];
            logged.tick = GetTickCount();
            logged.kind = kind;
            logged.occurrence = occurrence;
            memcpy(logged.carried, carried, sizeof(carried));
            logged.caller = caller;
            g_message_ring_head = next;
        }
    }

    SeenMessage* seen = nullptr;
    const LONG count = g_seen_message_count;
    for (LONG i = 0; i < count; ++i) {
        if (g_seen_messages[i].kind == kind) {
            seen = &g_seen_messages[i];
            break;
        }
    }

    if (seen == nullptr && count < static_cast<LONG>(kMaxMessageKinds)) {
        seen = &g_seen_messages[count];
        seen->kind = kind;
        seen->count = 0;
        seen->first_caller = caller;
        memcpy(seen->first, carried, sizeof(seen->first));
        g_seen_message_count = count + 1;
    }

    if (seen != nullptr) {
        ++seen->count;
        memcpy(seen->last, carried, sizeof(seen->last));
    }

    InterlockedExchange(&g_seen_message_lock, 0);
}

// If "address" points at readable text of at least four ordinary characters, copies it out.
static bool ReadTextAt(uintptr_t address, char* buffer, size_t buffer_size) {
    if (address < 0x10000)
        return false;

    unsigned char raw[48];
    const size_t available = SafeCopy(address, raw, sizeof(raw));
    size_t length = 0;
    while (length < available && length + 1 < buffer_size && raw[length] >= 0x20 && raw[length] < 0x7F) {
        buffer[length] = static_cast<char>(raw[length]);
        ++length;
    }
    buffer[length] = '\0';
    return length >= 4;
}

// Writes queued messages to dr2_ap_events.log. Runs on the plugin's own thread.
static void FlushMessageLog() {
    LoggedMessage batch[64];
    size_t count = 0;

    while (InterlockedExchange(&g_seen_message_lock, 1) != 0)
        YieldProcessor();
    while (g_message_ring_tail != g_message_ring_head && count < 64) {
        batch[count++] = g_message_ring[g_message_ring_tail];
        g_message_ring_tail = (g_message_ring_tail + 1) % kMessageRingSize;
    }
    InterlockedExchange(&g_seen_message_lock, 0);

    if (count == 0)
        return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, PathInGameDir(L"dr2_ap_events.log").c_str(), L"ab") != 0 || file == nullptr)
        return;

    SYSTEMTIME now;
    GetLocalTime(&now);
    const DWORD now_tick = GetTickCount();

    for (size_t i = 0; i < count; ++i) {
        const LoggedMessage& logged = batch[i];

        // Work back from the current clock time to when the message was sent.
        const DWORD age = now_tick - logged.tick;
        long total_ms = (now.wHour * 3600L + now.wMinute * 60L + now.wSecond) * 1000L + now.wMilliseconds - static_cast<long>(age);
        if (total_ms < 0)
            total_ms += 24L * 3600L * 1000L;

        fprintf(file, "[%02ld:%02ld:%02ld.%03ld] kind 0x%02X #%u from exe+0x%X:",
            total_ms / 3600000L, (total_ms / 60000L) % 60, (total_ms / 1000L) % 60, total_ms % 1000L,
            logged.kind, logged.occurrence, static_cast<unsigned int>(logged.caller - g_exe_start));

        for (int j = 0; j < 6; ++j)
            fprintf(file, " %d", static_cast<int>(logged.carried[j]));

        // The data itself may be text.
        char text[52];
        char inline_text[28] = {};
        memcpy(inline_text, logged.carried, 24);
        size_t inline_length = 0;
        while (inline_length < 24 && inline_text[inline_length] >= 0x20 && inline_text[inline_length] < 0x7F)
            ++inline_length;
        if (inline_length >= 4) {
            inline_text[inline_length] = '\0';
            fprintf(file, " | text \"%s\"", inline_text);
        }

        // Or it may point at text, or at an object that starts with a pointer to its name.
        for (int j = 0; j < 6; ++j) {
            if (ReadTextAt(logged.carried[j], text, sizeof(text))) {
                fprintf(file, " | [%d]->\"%s\"", j, text);
                continue;
            }
            for (int k = 0; k < 4; ++k) {
                uint32_t inner = 0;
                if (logged.carried[j] >= 0x10000 && SafeRead32(logged.carried[j] + k * 4, &inner) && ReadTextAt(inner, text, sizeof(text))) {
                    fprintf(file, " | [%d]+%d->\"%s\"", j, k * 4, text);
                    break;
                }
            }
        }

        fprintf(file, "\r\n");
    }

    fclose(file);
}

// ---------------------------------------------------------------- probes
// A probe sits on one instruction of game code and notes what the processor was holding each time the
// game gets there. It answers "what values does the game pass when it does X?".

static const size_t kMaxProbes = 4;
static const size_t kProbeRingSize = 64;
static const uint32_t kProbeHitLimit = 40;

struct ProbeHit {
    uint32_t eip, eax, ecx, edx, ebx, esi, edi, ebp, esp;
    uint32_t stack[32];
};

static SafetyHookMid g_probes[kMaxProbes];
static size_t g_probe_count;
static ProbeHit g_probe_ring[kProbeRingSize];
static size_t g_probe_ring_head;
static size_t g_probe_ring_tail;
static volatile LONG g_probe_lock;
static uint32_t g_probe_hits;

static void OnProbe(SafetyHookContext& context) {
    while (InterlockedExchange(&g_probe_lock, 1) != 0)
        YieldProcessor();

    const size_t next = (g_probe_ring_head + 1) % kProbeRingSize;
    if (g_probe_hits < kProbeHitLimit && next != g_probe_ring_tail) {
        ++g_probe_hits;
        ProbeHit& hit = g_probe_ring[g_probe_ring_head];
        hit.eip = static_cast<uint32_t>(context.eip);
        hit.eax = static_cast<uint32_t>(context.eax);
        hit.ecx = static_cast<uint32_t>(context.ecx);
        hit.edx = static_cast<uint32_t>(context.edx);
        hit.ebx = static_cast<uint32_t>(context.ebx);
        hit.esi = static_cast<uint32_t>(context.esi);
        hit.edi = static_cast<uint32_t>(context.edi);
        hit.ebp = static_cast<uint32_t>(context.ebp);
        hit.esp = static_cast<uint32_t>(context.esp);
        for (int i = 0; i < 32; ++i) {
            hit.stack[i] = 0;
            SafeRead32(context.esp + i * 4, &hit.stack[i]);
        }
        g_probe_ring_head = next;
    }

    InterlockedExchange(&g_probe_lock, 0);
}

static void AddProbe(uintptr_t address) {
    if (g_probe_count >= kMaxProbes) {
        Log("all %u probes are in use; \"unprobe\" first", static_cast<unsigned int>(kMaxProbes));
        return;
    }

    g_probe_hits = 0;
    g_probes[g_probe_count++] = safetyhook::create_mid(reinterpret_cast<void*>(address), &OnProbe);
    Log("probe set at 0x%08X (notes the next %u times the game gets there)", static_cast<unsigned int>(address), kProbeHitLimit);
}

static void RemoveProbes() {
    for (size_t i = 0; i < g_probe_count; ++i)
        g_probes[i].reset();
    g_probe_count = 0;
}

static void FlushProbeLog() {
    for (;;) {
        ProbeHit hit;

        while (InterlockedExchange(&g_probe_lock, 1) != 0)
            YieldProcessor();
        const bool available = g_probe_ring_tail != g_probe_ring_head;
        if (available) {
            hit = g_probe_ring[g_probe_ring_tail];
            g_probe_ring_tail = (g_probe_ring_tail + 1) % kProbeRingSize;
        }
        InterlockedExchange(&g_probe_lock, 0);

        if (!available)
            return;

        Log("probe hit near 0x%08X: eax=%08X ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X",
            hit.eip, hit.eax, hit.ecx, hit.edx, hit.ebx, hit.esi, hit.edi, hit.ebp, hit.esp);
        // 32 values, so the caller's caller can be found past a routine's own local values.
        for (int row = 0; row < 4; ++row)
            Log("  stack +0x%02X: %08X %08X %08X %08X %08X %08X %08X %08X", row * 32,
                hit.stack[row * 8], hit.stack[row * 8 + 1], hit.stack[row * 8 + 2], hit.stack[row * 8 + 3],
                hit.stack[row * 8 + 4], hit.stack[row * 8 + 5], hit.stack[row * 8 + 6], hit.stack[row * 8 + 7]);
    }
}

static void InstallMessageHook() {
    g_message_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_exe_start + kSendMessageFunction), &OnGameMessage);
    Log("message listener installed (research log off until \"events on\")");
}

static void RemoveMessageHook() {
    InterlockedExchange(&g_message_listening, 0);
    g_message_hook.reset();
    Sleep(100);
}

static void ListSeenMessages() {
    const LONG count = g_seen_message_count;
    Log("%d kind(s) of message seen (listening is %s)", static_cast<int>(count), g_message_listening != 0 ? "on" : "off");

    for (LONG i = 0; i < count; ++i) {
        const SeenMessage& seen = g_seen_messages[i];
        Log("  kind 0x%02X: %u time(s); first carried %d %d %d %d; last carried %d %d %d %d; first sent from exe+0x%X",
            seen.kind, seen.count,
            static_cast<int>(seen.first[0]), static_cast<int>(seen.first[1]), static_cast<int>(seen.first[2]), static_cast<int>(seen.first[3]),
            static_cast<int>(seen.last[0]), static_cast<int>(seen.last[1]), static_cast<int>(seen.last[2]), static_cast<int>(seen.last[3]),
            static_cast<unsigned int>(seen.first_caller - g_exe_start));
    }
}

// ---------------------------------------------------------------- disassembly

static size_t SafeCopy(uintptr_t address, unsigned char* buffer, size_t size) {
    size_t copied = 0;
    __try {
        for (; copied < size; ++copied)
            buffer[copied] = reinterpret_cast<const unsigned char*>(address)[copied];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return copied;
}

static void Disassemble(uintptr_t address, unsigned long count) {
    for (unsigned long i = 0; i < count; ++i) {
        unsigned char bytes[16];
        const size_t available = SafeCopy(address, bytes, sizeof(bytes));
        if (available == 0) {
            Log("  0x%08X: <unreadable>", static_cast<unsigned int>(address));
            return;
        }

        ZydisDisassembledInstruction instruction;
        if (ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LEGACY_32, address, bytes, available, &instruction))) {
            Log("  0x%08X: %s", static_cast<unsigned int>(address), instruction.text);
            address += instruction.info.length;
        }
        else {
            Log("  0x%08X: db 0x%02X", static_cast<unsigned int>(address), bytes[0]);
            address += 1;
        }
    }
}

// ---------------------------------------------------------------- watching for writes
// Uses a processor "hardware breakpoint": the processor itself reports any write to one chosen 4-byte value,
// and we note which instruction did it and what was on the stack (which shows who called that code).

static const size_t kMaxHits = 32;
static const size_t kStackWords = 48;
static const size_t kCallersKept = 8;

struct WatchHit {
    uint32_t eip;
    uint32_t count;
    uint32_t callers[kCallersKept];
};

static WatchHit g_hits[kMaxHits];
static volatile LONG g_hit_count;
static volatile LONG g_hit_lock;
static uintptr_t g_watch_address;
static PVOID g_watch_handler;

static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info) {
    if (g_watch_address == 0 || info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT* const context = info->ContextRecord;
    context->Dr6 = 0;

    const uint32_t eip = static_cast<uint32_t>(context->Eip);

    while (InterlockedExchange(&g_hit_lock, 1) != 0)
        YieldProcessor();

    WatchHit* hit = nullptr;
    const LONG count = g_hit_count;
    for (LONG i = 0; i < count; ++i) {
        if (g_hits[i].eip == eip) {
            hit = &g_hits[i];
            break;
        }
    }

    if (hit != nullptr) {
        ++hit->count;
    }
    else if (count < static_cast<LONG>(kMaxHits)) {
        hit = &g_hits[count];
        hit->eip = eip;
        hit->count = 1;

        // Keep the stack values that point into the game's code: those are the callers.
        size_t kept = 0;
        for (size_t i = 0; i < kStackWords && kept < kCallersKept; ++i) {
            uint32_t value = 0;
            if (!SafeRead32(static_cast<uintptr_t>(context->Esp) + i * 4, &value))
                break;
            if (value >= g_exe_start && value < g_exe_end)
                hit->callers[kept++] = value;
        }
        for (; kept < kCallersKept; ++kept)
            hit->callers[kept] = 0;

        g_hit_count = count + 1;
    }

    InterlockedExchange(&g_hit_lock, 0);
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets (or, with address 0, clears) the write breakpoint on every thread of the game except this one.
static unsigned int ApplyWatchToThreads(uintptr_t address) {
    const DWORD process_id = GetCurrentProcessId();
    const DWORD own_thread_id = GetCurrentThreadId();
    unsigned int changed = 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != process_id || entry.th32ThreadID == own_thread_id)
                continue;

            const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
            if (thread == nullptr)
                continue;

            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                CONTEXT context;
                ZeroMemory(&context, sizeof(context));
                context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

                if (GetThreadContext(thread, &context)) {
                    // Slot 0: enable bit 0; type bits 16-17 = 01 (write); size bits 18-19 = 11 (4 bytes).
                    context.Dr7 &= ~static_cast<DWORD>(0x000F0003);
                    if (address != 0) {
                        context.Dr0 = static_cast<DWORD>(address);
                        context.Dr7 |= 0x00000001 | (0x1 << 16) | (0x3 << 18);
                    }
                    else {
                        context.Dr0 = 0;
                    }
                    context.Dr6 = 0;
                    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

                    if (SetThreadContext(thread, &context))
                        ++changed;
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return changed;
}

static void StartWatch(uintptr_t address) {
    if (g_watch_handler == nullptr)
        g_watch_handler = AddVectoredExceptionHandler(1, &WatchHandler);

    g_hit_count = 0;
    g_watch_address = address;
    const unsigned int threads = ApplyWatchToThreads(address);
    Log("watching writes to 0x%08X on %u thread(s)", static_cast<unsigned int>(address), threads);
}

static void StopWatch() {
    const unsigned int threads = ApplyWatchToThreads(0);
    g_watch_address = 0;
    Log("stopped watching (%u thread(s))", threads);
}

static void LogHits() {
    const LONG count = g_hit_count;
    Log("%d different place(s) wrote to the watched value", static_cast<int>(count));

    for (LONG i = 0; i < count; ++i) {
        const WatchHit& hit = g_hits[i];
        Log("  after the write, next instruction 0x%08X [exe+0x%X], seen %u time(s)", hit.eip, static_cast<unsigned int>(hit.eip - g_exe_start), hit.count);

        char line[256];
        size_t used = 0;
        for (size_t j = 0; j < kCallersKept && hit.callers[j] != 0; ++j)
            used += sprintf_s(line + used, sizeof(line) - used, " 0x%08X", hit.callers[j]);
        if (used != 0)
            Log("    code addresses on the stack:%s", line);
    }
}

// ---------------------------------------------------------------- Archipelago

#include "Ap.inc"

// ---------------------------------------------------------------- commands

// Shows the start of an object: what it begins with and, if that is a table of functions, the first few.
static void DescribeObject(uintptr_t address, unsigned long count) {
    uint32_t vtable = 0;
    if (!SafeRead32(address, &vtable)) {
        Log("  0x%08X is not readable", static_cast<unsigned int>(address));
        return;
    }

    Log("  object 0x%08X starts with 0x%08X%s", static_cast<unsigned int>(address), vtable, IsInExe(vtable) ? " (inside the game image)" : "");
    if (!IsInExe(vtable))
        return;

    for (unsigned long i = 0; i < count; ++i) {
        uint32_t function = 0;
        if (!SafeRead32(vtable + i * 4, &function) || !IsInExe(function))
            break;
        Log("    [%lu] (+0x%lX) 0x%08X", i, i * 4, function);
    }
}

static void HandleCommand(char* line) {
    Log("> %s", line);

    if (ApHandleCommand(line)) {
        Log("done");
        return;
    }

    if (strncmp(line, "toast ", 6) == 0) {
        ToastFromCommand(line + 6);
        Log("done");
        return;
    }

    // "find" takes the rest of the line as its text, spaces included.
    if (strncmp(line, "find ", 5) == 0) {
        FindText(line + 5);
        Log("done");
        return;
    }

    char* context = nullptr;
    const char* const command = strtok_s(line, " \t", &context);

    const char* tokens[10] = {};
    size_t token_count = 0;
    while (token_count < 10) {
        const char* const token = strtok_s(nullptr, " \t", &context);
        if (token == nullptr)
            break;
        tokens[token_count++] = token;
    }
    const char* const first = tokens[0];
    const char* const second = tokens[1];

    if (command == nullptr)
        return;

    if (strcmp(command, "scan") == 0 && first != nullptr) {
        const uint32_t wanted = static_cast<uint32_t>(strtol(first, nullptr, 0));
        ScanAll(wanted, wanted);
        CaptureResultValues();
        ListResults();
    }
    else if (strcmp(command, "scanf") == 0 && first != nullptr) {
        const uint32_t wanted = FloatToBits(strtof(first, nullptr));
        ScanAll(wanted, wanted);
        CaptureResultValues();
        ListResults();
    }
    else if (strcmp(command, "scanr") == 0 && first != nullptr && second != nullptr) {
        ScanAll(static_cast<uint32_t>(strtol(first, nullptr, 0)), static_cast<uint32_t>(strtol(second, nullptr, 0)));
        CaptureResultValues();
        ListResults();
    }
    else if (strcmp(command, "scanfr") == 0 && first != nullptr && second != nullptr) {
        // For floats of zero or more, a bigger number is also a bigger bit pattern, so the same range search works.
        ScanAll(FloatToBits(strtof(first, nullptr)), FloatToBits(strtof(second, nullptr)));
        CaptureResultValues();
        ListResults();
    }
    else if (strcmp(command, "next") == 0 && first != nullptr) {
        const uint32_t wanted = static_cast<uint32_t>(strtol(first, nullptr, 0));
        FilterResultsBy(kFilterRange, wanted, wanted);
        ListResults();
    }
    else if (strcmp(command, "nextf") == 0 && first != nullptr) {
        const uint32_t wanted = FloatToBits(strtof(first, nullptr));
        FilterResultsBy(kFilterRange, wanted, wanted);
        ListResults();
    }
    else if (strcmp(command, "nextr") == 0 && first != nullptr && second != nullptr) {
        FilterResultsBy(kFilterRange, static_cast<uint32_t>(strtol(first, nullptr, 0)), static_cast<uint32_t>(strtol(second, nullptr, 0)));
        ListResults();
    }
    else if (strcmp(command, "nextfr") == 0 && first != nullptr && second != nullptr) {
        FilterResultsBy(kFilterRange, FloatToBits(strtof(first, nullptr)), FloatToBits(strtof(second, nullptr)));
        ListResults();
    }
    else if (strcmp(command, "nextinc") == 0) {
        FilterResultsBy(kFilterIncreased, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "nextdec") == 0) {
        FilterResultsBy(kFilterDecreased, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "nextincf") == 0) {
        FilterResultsBy(kFilterIncreasedFloat, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "nextdecf") == 0) {
        FilterResultsBy(kFilterDecreasedFloat, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "nextsame") == 0) {
        FilterResultsBy(kFilterSame, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "nextdiff") == 0) {
        FilterResultsBy(kFilterDifferent, 0, 0);
        ListResults();
    }
    else if (strcmp(command, "obj") == 0 && first != nullptr) {
        unsigned long count = second != nullptr ? strtoul(second, nullptr, 0) : 12;
        if (count > 64)
            count = 64;
        DescribeObject(static_cast<uintptr_t>(strtoul(first, nullptr, 16)), count);
    }
    else if (strcmp(command, "text") == 0 && first != nullptr) {
        const uintptr_t address = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        char text[52];
        if (ReadTextAt(address, text, sizeof(text)))
            Log("  0x%08X: \"%s\"", static_cast<unsigned int>(address), text);
        else
            Log("  0x%08X: no readable text there", static_cast<unsigned int>(address));
    }
    else if (strcmp(command, "writef") == 0 && first != nullptr && second != nullptr) {
        const uintptr_t address = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        const float value = strtof(second, nullptr);
        if (SafeWrite32(address, FloatToBits(value)))
            Log("wrote %g to 0x%08X", value, static_cast<unsigned int>(address));
        else
            Log("could not write to 0x%08X", static_cast<unsigned int>(address));
    }
    else if (strcmp(command, "list") == 0) {
        ListResults();
    }
    else if (strcmp(command, "read") == 0 && first != nullptr) {
        const uintptr_t address = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        unsigned long count = second != nullptr ? strtoul(second, nullptr, 0) : 8;
        if (count > 256)
            count = 256;

        for (unsigned long i = 0; i < count; ++i)
            LogValueAt(address + i * sizeof(uint32_t));
    }
    else if (strcmp(command, "write") == 0 && first != nullptr && second != nullptr) {
        const uintptr_t address = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        const uint32_t value = static_cast<uint32_t>(strtol(second, nullptr, 0));

        if (SafeWrite32(address, value))
            Log("wrote %d to 0x%08X", static_cast<int>(value), static_cast<unsigned int>(address));
        else
            Log("could not write to 0x%08X", static_cast<unsigned int>(address));
    }
    else if (strcmp(command, "player") == 0) {
        LogPlayer();
    }
    else if (strcmp(command, "setmoney") == 0 && first != nullptr) {
        SetMoney(static_cast<int>(strtol(first, nullptr, 0)), false);
    }
    else if (strcmp(command, "addmoney") == 0 && first != nullptr) {
        SetMoney(static_cast<int>(strtol(first, nullptr, 0)), true);
    }
    else if (strcmp(command, "watch") == 0 && first != nullptr) {
        StartWatch(static_cast<uintptr_t>(strtoul(first, nullptr, 16)));
    }
    else if (strcmp(command, "unwatch") == 0) {
        StopWatch();
    }
    else if (strcmp(command, "hits") == 0) {
        LogHits();
    }
    else if (strcmp(command, "disasm") == 0 && first != nullptr) {
        const uintptr_t address = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        unsigned long count = second != nullptr ? strtoul(second, nullptr, 0) : 24;
        if (count > 400)
            count = 400;
        Disassemble(address, count);
    }
    else if (strcmp(command, "setpp") == 0 && first != nullptr) {
        SetPP(static_cast<int>(strtol(first, nullptr, 0)), false);
    }
    else if (strcmp(command, "addpp") == 0 && first != nullptr) {
        SetPP(static_cast<int>(strtol(first, nullptr, 0)), true);
    }
    else if (strcmp(command, "refs") == 0 && first != nullptr) {
        const uint32_t wanted = static_cast<uint32_t>(strtoul(first, nullptr, 16));
        g_scan_everything = true;
        ScanAll(wanted, wanted);
        g_scan_everything = false;

        Log("%u place(s) hold 0x%08X%s", static_cast<unsigned int>(g_result_count), wanted, g_results_truncated ? " (stopped at the limit)" : "");
        const size_t shown = g_result_count < kMaxListed ? g_result_count : kMaxListed;
        for (size_t i = 0; i < shown; ++i) {
            char location[32];
            DescribeLocation(g_results[i], location, sizeof(location));
            Log("  0x%08X [%s]", static_cast<unsigned int>(g_results[i]), location);
        }
    }
    else if (strcmp(command, "snap") == 0 && first != nullptr && second != nullptr) {
        TakeSnapshot(static_cast<uintptr_t>(strtoul(first, nullptr, 16)), static_cast<size_t>(strtoul(second, nullptr, 16)));
    }
    else if (strcmp(command, "diff") == 0) {
        CompareSnapshot();
    }
    else if (strcmp(command, "call") == 0 && token_count >= 3) {
        const uintptr_t function = static_cast<uintptr_t>(strtoul(tokens[0], nullptr, 16));
        const uint32_t ecx_value = static_cast<uint32_t>(strtoul(tokens[1], nullptr, 0));
        const uint32_t edx_value = static_cast<uint32_t>(strtoul(tokens[2], nullptr, 0));

        uint32_t arguments[8] = {};
        uint32_t argument_count = 0;
        for (size_t i = 3; i < token_count && argument_count < 8; ++i)
            arguments[argument_count++] = static_cast<uint32_t>(strtoul(tokens[i], nullptr, 0));

        uint32_t result = 0;
        if (CallGameFunction(function, ecx_value, edx_value, arguments, argument_count, &result))
            Log("call returned %d (hex 0x%08X)", static_cast<int>(result), result);
        else
            Log("call crashed inside the game code (caught; the game keeps running)");
    }
    else if (strcmp(command, "mcall") == 0 && token_count >= 3) {
        const uintptr_t function = static_cast<uintptr_t>(strtoul(tokens[0], nullptr, 16));
        const uint32_t ecx_value = static_cast<uint32_t>(strtoul(tokens[1], nullptr, 0));
        const uint32_t edx_value = static_cast<uint32_t>(strtoul(tokens[2], nullptr, 0));

        uint32_t arguments[8] = {};
        uint32_t argument_count = 0;
        for (size_t i = 3; i < token_count && argument_count < 8; ++i)
            arguments[argument_count++] = static_cast<uint32_t>(strtoul(tokens[i], nullptr, 0));

        uint32_t result = 0;
        if (CallOnMainThread(function, ecx_value, edx_value, arguments, argument_count, &result))
            Log("main-thread call returned %d (hex 0x%08X)", static_cast<int>(result), result);
    }
    else if (strcmp(command, "givemoney") == 0 && first != nullptr) {
        GiveMoney(static_cast<int>(strtol(first, nullptr, 0)));
    }
    else if (strcmp(command, "givepp") == 0 && first != nullptr) {
        GivePP(static_cast<int>(strtol(first, nullptr, 0)), second != nullptr ? static_cast<uint32_t>(strtoul(second, nullptr, 0)) : 0xC20);
    }
    else if (strcmp(command, "events") == 0) {
        if (first != nullptr && strcmp(first, "on") == 0) {
            InterlockedExchange(&g_message_listening, 1);
            Log("listening to game messages");
        }
        else if (first != nullptr && strcmp(first, "off") == 0) {
            InterlockedExchange(&g_message_listening, 0);
            Log("stopped listening");
        }
        else if (first != nullptr && strcmp(first, "clear") == 0) {
            while (InterlockedExchange(&g_seen_message_lock, 1) != 0)
                YieldProcessor();
            g_seen_message_count = 0;
            memset(g_kind_counts, 0, sizeof(g_kind_counts));
            InterlockedExchange(&g_seen_message_lock, 0);
            Log("cleared");
        }
        else {
            ListSeenMessages();
        }
    }
    else if (strcmp(command, "probe") == 0 && first != nullptr) {
        AddProbe(static_cast<uintptr_t>(strtoul(first, nullptr, 16)));
    }
    else if (strcmp(command, "unprobe") == 0) {
        RemoveProbes();
        Log("probes removed");
    }
    else if (strcmp(command, "reload") == 0) {
        g_reload_requested = true;
    }
    else if (strcmp(command, "ptr") == 0 && first != nullptr) {
        const uintptr_t target = static_cast<uintptr_t>(strtoul(first, nullptr, 16));
        const uint32_t range = second != nullptr ? static_cast<uint32_t>(strtoul(second, nullptr, 16)) : 0x1000;
        FindPointers(target, range);
    }
    else {
        Log("unknown command, or a number is missing");
    }

    Log("done");
}

// Reads the whole inbox file, then deletes it so each command is handled once.
static bool ReadAndDeleteInbox(std::string& contents) {
    const std::wstring path = PathInGameDir(L"dr2_ap_in.txt");

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false; // Not there, or the client is still writing it; try again next time.

    contents.clear();
    char chunk[4096];
    DWORD bytes_read = 0;
    while (ReadFile(file, chunk, sizeof(chunk), &bytes_read, nullptr) && bytes_read != 0)
        contents.append(chunk, bytes_read);
    CloseHandle(file);

    DeleteFileW(path.c_str());
    return true;
}

static void HandleInbox(const std::string& contents) {
    size_t start = 0;
    while (start < contents.size()) {
        size_t end = contents.find('\n', start);
        if (end == std::string::npos)
            end = contents.size();

        std::string line = contents.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (!line.empty())
            HandleCommand(&line[0]);

        start = end + 1;
    }
}

// ---------------------------------------------------------------- main loop

static bool GameWindowIsActive() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
        return false;

    DWORD process_id = 0;
    GetWindowThreadProcessId(foreground, &process_id);
    return process_id == GetCurrentProcessId();
}

// Runs until the "reload" command arrives, then cleans up completely and returns,
// so the loader can unload this file and load a newer copy.
extern "C" __declspec(dllexport) void CoreRun() {
    InitializeCriticalSection(&g_log_lock);
    InitializeCriticalSection(&g_toast_lock);
    g_reload_requested = false;
    g_present_hook_quiet = false;

    wchar_t exe_path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    g_game_dir.assign(exe_path, length);
    const size_t slash = g_game_dir.find_last_of(L"\\/");
    g_game_dir = slash == std::wstring::npos ? L"" : g_game_dir.substr(0, slash + 1);

    // Work out where the game's own image sits in memory.
    g_exe_start = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const IMAGE_DOS_HEADER* const dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_exe_start);
    const IMAGE_NT_HEADERS* const nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_exe_start + dos_header->e_lfanew);
    g_exe_end = g_exe_start + nt_headers->OptionalHeader.SizeOfImage;

    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&CoreRun), &self);
    g_self_base = reinterpret_cast<uintptr_t>(self);

    g_results = static_cast<uintptr_t*>(VirtualAlloc(nullptr, kMaxResults * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_result_values = static_cast<uint32_t*>(VirtualAlloc(nullptr, kMaxResults * sizeof(uint32_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_snapshot = static_cast<unsigned char*>(VirtualAlloc(nullptr, kMaxSnapshot, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    if (g_results != nullptr && g_result_values != nullptr && g_snapshot != nullptr) {
        Log("DR2AP core 1.12 started");
        Log("game image: 0x%08X to 0x%08X", static_cast<unsigned int>(g_exe_start), static_cast<unsigned int>(g_exe_end));
        Log("DR2Fix.asi loaded: %s", GetModuleHandleW(L"DR2Fix.asi") != nullptr ? "yes" : "no");
        InstallTickHook();
        InstallPresentHook();
        InstallMessageHook();
        ApStart();

        DWORD last_present_try = GetTickCount();
        bool f7_was_down = false;
        unsigned int f7_presses = 0;
        DWORD last_f7_time = 0;

        while (!g_reload_requested) {
            std::string inbox;
            if (ReadAndDeleteInbox(inbox))
                HandleInbox(inbox);

            // A quiet chime when a message box appears. The sound is dr2_ap_chime.wav in the game folder;
            // if the file is missing, nothing plays.
            FlushMessageLog();
            FlushProbeLog();
            ApTick();

            if (GetTickCount() - last_present_try >= 2000) {
                last_present_try = GetTickCount();
                if (g_device_vtable == nullptr)
                    InstallPresentHook();
                KeepTickHook();
            }

            if (InterlockedExchange(&g_chime_wanted, 0) != 0)
                PlaySoundW(PathInGameDir(L"dr2_ap_chime.wav").c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);

            const bool f7_down = GameWindowIsActive() && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            const DWORD now = GetTickCount();
            if (f7_down && !f7_was_down && now - last_f7_time >= 400) {
                last_f7_time = now;
                ++f7_presses;
                Log("F7 pressed (%u)", f7_presses);

                char message[64];
                sprintf_s(message, "F7 %u", f7_presses);
                EnterCriticalSection(&g_log_lock);
                AppendLine(L"dr2_ap_out.txt", message);
                LeaveCriticalSection(&g_log_lock);
            }
            f7_was_down = f7_down;

            Sleep(50);
        }

        Log("reload requested; core 1.12 shutting down");
    }

    // Leave nothing behind that points into this file once it is unloaded.
    PlaySoundW(nullptr, nullptr, 0); // Stop any chime still playing.
    ApStop();
    RemoveProbes();
    RemoveMessageHook();
    RemoveTickHook();
    RemovePresentHook();
    DeleteCriticalSection(&g_toast_lock);
    if (g_watch_address != 0)
        StopWatch();
    if (g_watch_handler != nullptr) {
        RemoveVectoredExceptionHandler(g_watch_handler);
        g_watch_handler = nullptr;
    }
    Sleep(100); // Let any exception handler call that was already under way finish.

    if (g_results != nullptr) {
        VirtualFree(g_results, 0, MEM_RELEASE);
        g_results = nullptr;
    }
    if (g_result_values != nullptr) {
        VirtualFree(g_result_values, 0, MEM_RELEASE);
        g_result_values = nullptr;
    }
    if (g_snapshot != nullptr) {
        VirtualFree(g_snapshot, 0, MEM_RELEASE);
        g_snapshot = nullptr;
    }
    g_result_count = 0;
    g_snapshot_size = 0;

    DeleteCriticalSection(&g_log_lock);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

// Test harness: runs the plugin's Archipelago logic (Ap.inc) on Linux against a pretend game.
#include "../shim/windows.h"
#include "../shim/safetyhook.hpp"
#include <string>
#include <map>
#include <vector>
#include <sys/stat.h>
#include <ctime>
#include <cassert>
#include <clocale>
#include <unistd.h>

// ---- pretend Windows
static DWORD g_fake_tick = 100000;
DWORD GetTickCount() { return g_fake_tick; }
void YieldProcessor() {}
LONG InterlockedExchange(volatile LONG* target, LONG value) { LONG old = *target; *target = value; return old; }
static std::string Narrow(const wchar_t* w) { std::string s; for (; *w; ++w) s += (*w == L'\\') ? '/' : static_cast<char>(*w); return s; }
int _wfopen_s(FILE** file, const wchar_t* path, const wchar_t* mode) { *file = fopen(Narrow(path).c_str(), Narrow(mode).c_str()); return *file ? 0 : 1; }
HANDLE CreateFileW(LPCWSTR path, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) { FILE* f = fopen(Narrow(path).c_str(), "rb"); return f ? f : INVALID_HANDLE_VALUE; }
BOOL ReadFile(HANDLE h, void* buffer, DWORD size, DWORD* read, void*) { *read = fread(buffer, 1, size, static_cast<FILE*>(h)); return TRUE; }
BOOL CloseHandle(HANDLE h) { fclose(static_cast<FILE*>(h)); return TRUE; }
BOOL CreateDirectoryW(LPCWSTR path, void*) { std::string p = Narrow(path); while (!p.empty() && p.back() == '/') p.pop_back(); return mkdir(p.c_str(), 0777) == 0; }
static bool g_file_fresh = true;
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS, void* out) {
    struct stat st; if (stat(Narrow(path).c_str(), &st) != 0) return FALSE;
    auto* data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
    unsigned long long t = g_file_fresh ? 1000000000ULL : 1000000000ULL - 60ULL * 10000000ULL;
    data->ftLastWriteTime.dwLowDateTime = static_cast<DWORD>(t & 0xFFFFFFFF); data->ftLastWriteTime.dwHighDateTime = static_cast<DWORD>(t >> 32); return TRUE;
}
void GetSystemTimeAsFileTime(FILETIME* ft) { unsigned long long t = 1000000000ULL; ft->dwLowDateTime = static_cast<DWORD>(t & 0xFFFFFFFF); ft->dwHighDateTime = static_cast<DWORD>(t >> 32); }
int MultiByteToWideChar(UINT, DWORD, const char* s, int, wchar_t* out, int size) { int n = 0; for (; s[n] && n < size - 1; ++n) out[n] = static_cast<unsigned char>(s[n]); out[n] = 0; return n + 1; }
long long _strtoi64(const char* s, char** e, int b) { return strtoll(s, e, b); }
int strncpy_s(char* buffer, size_t size, const char* source, size_t) { strncpy(buffer, source, size - 1); buffer[size - 1] = 0; return 0; }
void SafetyHookMid::reset() {}
namespace safetyhook { SafetyHookMid create_mid(void*, void (*)(SafetyHookContext&)) { return {}; } }

template <typename... A> int sprintf_s(char* buffer, size_t size, const char* format, A... args) { return snprintf(buffer, size, format, args...); }
// ---- pretend Core.cpp
static std::vector<std::string> g_log;
static void Log(const char* format, ...) { char b[1024]; va_list a; va_start(a, format); vsnprintf(b, sizeof b, format, a); va_end(a); g_log.push_back(b); printf("    log: %s\n", b); }
static std::map<uint32_t, uint32_t> g_memory;   // pretend game memory, 4 bytes at a time
static std::map<uint32_t, std::string> g_texts;  // pretend text in game memory
static uintptr_t g_exe_start = 0x400000, g_exe_end = 0xEE2000;
static uintptr_t* g_results; static size_t g_result_count; static uintptr_t g_result_storage[64]; static uintptr_t g_result_storage2[64];
static bool IsInExe(uintptr_t a) { return a >= g_exe_start && a < g_exe_end; }
static bool SafeRead32(uintptr_t a, uint32_t* v) { auto f = g_memory.find(static_cast<uint32_t>(a)); if (f == g_memory.end()) return false; *v = f->second; return true; }
static bool SafeWrite32(uintptr_t a, uint32_t v) { g_memory[static_cast<uint32_t>(a)] = v; return true; }
static size_t SafeCopy(uintptr_t a, unsigned char* buffer, size_t size) {
    for (auto& t : g_texts) {   // an address inside a text (used to look at the byte after a name)
        if (a > t.first && a <= t.first + t.second.size()) { size_t n = 0; for (; n < size; ++n) { size_t i = a - t.first + n; buffer[n] = i < t.second.size() ? t.second[i] : 0; } return n; }
    }
    auto f = g_texts.find(static_cast<uint32_t>(a));
    if (f != g_texts.end()) { size_t n = 0; for (; n < size; ++n) buffer[n] = n < f->second.size() ? f->second[n] : 0; return n; }
    size_t n = 0;
    for (; n + 4 <= size; n += 4) { auto m = g_memory.find(static_cast<uint32_t>(a + n)); if (m == g_memory.end()) break; memcpy(buffer + n, &m->second, 4); }
    return n;
}
static bool ReadTextAt(uintptr_t a, char* buffer, size_t size) { auto f = g_texts.find(static_cast<uint32_t>(a)); if (f == g_texts.end()) return false; snprintf(buffer, size, "%s", f->second.c_str()); return f->second.size() >= 4; }
static float BitsToFloat(uint32_t b) { float v; memcpy(&v, &b, 4); return v; }
static uint32_t FloatToBits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static void FindText(const char* name) { g_results = g_result_storage; g_result_count = 0; for (auto& t : g_texts) if (t.second == name && g_result_count < 64) g_results[g_result_count++] = t.first; }
static std::vector<uint32_t> g_fake_missions;   // addresses of pretend mission definitions
static void ScanAll(uint32_t low, uint32_t) { g_results = g_result_storage2; g_result_count = 0; if (low == 0x400000 + 0x88F0CC) for (uint32_t d : g_fake_missions) g_results[g_result_count++] = d; }
static std::wstring PathInGameDir(const wchar_t* name) { return std::wstring(L"/tmp/aptest/") + name; }
static std::vector<std::string> g_toasts;
static void QueueToast(const wchar_t* title, const wchar_t* text) { std::string s = Narrow(title) + " | " + Narrow(text); g_toasts.push_back(s); printf("    TOAST: %s\n", s.c_str()); }
static const uintptr_t kStatsKills = 0x38, kChangeMoneyFunction = 0x442060, kAwardPPFunction = 0x445EC0;
static const uint32_t kStats = 0x1000000;
static bool g_main_thread_ok = true;
static uintptr_t FindPlayerStats() { return kStats; }
static std::vector<std::string> g_calls;
static void ApOnGameMessage(uint32_t kind, uint32_t caller, uint32_t message, const uint32_t* carried);
static bool CallOnMainThread(uintptr_t function, uint32_t ecx, uint32_t, const uint32_t* args, uint32_t count, uint32_t*) {
    if (!g_main_thread_ok) return false;
    char b[200]; int n = snprintf(b, sizeof b, "call 0x%08X ecx=0x%X", static_cast<unsigned>(function), ecx);
    for (uint32_t i = 0; i < count; ++i) n += snprintf(b + n, sizeof b - n, " %u", args[i]);
    g_calls.push_back(b); printf("    GAME CALL: %s\n", b);
    if (function == g_exe_start + 0x442CD0) { uint32_t carried[6] = { args[0], 0, 0, 0, 0, 0 }; ApOnGameMessage(0x13, 0, 0x2000000, carried); }  // the game announces the key item
    return true;
}

#include "../../Ap.inc"

// ---- the test itself
static std::string ReadAll(const char* path) { FILE* f = fopen(path, "rb"); if (!f) return "<missing>"; std::string s; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
static void WriteItems(const std::string& body) { FILE* f = fopen("/tmp/aptest/dr2_ap_items.txt", "wb"); fputs(body.c_str(), f); fclose(f); }
static void Tick(int times = 1) { for (int i = 0; i < times; ++i) { g_fake_tick += 600; ApTick(); } }
static void Text(const char* text) { static uint32_t at = 0x3000000; at += 0x100; g_texts[at + 8] = text; uint32_t c[6] = {}; ApOnGameMessage(0x15, 0, at, c); }
static void Survivor(const char* name, uint32_t status) { static uint32_t rec = 0x4000000; rec += 0x100; g_texts[rec + 0x18] = name; uint32_t c[6] = { rec, status, 0, 0, 0, 0 }; ApOnGameMessage(0x63, 0, 0, c); }
static uint32_t Mission(const char* name, uint32_t state = 0) {   // makes, or updates, a pretend mission
    static std::map<std::string, uint32_t> made; static uint32_t next = 0x9000000;
    if (!made.count(name)) { uint32_t d = next; next += 0x1000; made[name] = d; g_fake_missions.push_back(d);
        g_memory[d] = 0x400000 + 0x88F0CC; g_texts[d + 0x1C] = name; g_memory[d + 0x3C] = strlen(name); g_memory[d + 0x104] = d + 0x800;
        g_memory[d + 0x804] = 0; g_memory[d + 0x810] = 0; g_memory[d + 0x818] = 0; g_memory[d + 0x820] = 0x8200000; }
    g_memory[made[name] + 0x808] = state; return made[name]; }
static int g_failed;
#define EXPECT(cond) do { if (!(cond)) { printf("FAILED: %s (line %d)\n", #cond, __LINE__); ++g_failed; } } while (0)
static bool Outbox(const std::string& line) { std::string all = ReadAll(("/tmp/aptest/dr2_ap_seeds/" + g_ap_seed + "/dr2_ap_outbox.txt").c_str()); return ("\n" + all).find("\n" + line + "\r\n") != std::string::npos; }
static size_t Count(const std::vector<std::string>& v, const std::string& part) { size_t n = 0; for (auto& s : v) n += s.find(part) != std::string::npos; return n; }

int main() {
    if (system("rm -rf /tmp/aptest && mkdir -p /tmp/aptest") != 0) return 2;
    const uint32_t player = 0x5000000;
    g_memory[player] = static_cast<uint32_t>(g_exe_start + kPlayerVtableOffset);
    g_memory[kStats + kStatsKills] = 12; g_memory[kStats + kStatsHealth] = FloatToBits(500.0f); g_memory[kStats + kStatsLevel] = 3;
    // the mod's own mission, as the game would hold it: not running yet (main menu)
    const uint32_t bridge = 0x8000000, bridge_instance = 0x8100000;
    g_memory[bridge] = static_cast<uint32_t>(g_exe_start + kMissionDefinitionVtable); g_texts[bridge + 0x1C] = "AP_Bridge"; g_memory[bridge + 0x3C] = 9;
    g_memory[bridge + 0x104] = bridge_instance; g_memory[bridge_instance + 0x08] = 0; g_memory[bridge_instance + 0x20] = 0x8200000;
    g_memory[0x8200000 + 0x34] = player;   // the mission system knows the player
    // one part of the mod's mission: the phone call
    const uint32_t call_part = 0x8300000;
    g_memory[call_part] = 0x400000 + 0x482E90; g_texts[call_part + 0x1C] = "AP_OnCall"; g_memory[call_part + 0x3C] = 9;
    g_memory[call_part + 0x0C] = 0; g_memory[call_part + 0x10] = 0; g_memory[call_part + 0x14] = bridge; g_memory[bridge + 0x0C] = call_part;
    ApStart();

    printf("== no client: nothing happens\n");
    Text("81552 Activated"); Tick(3);
    EXPECT(!g_ap_active); EXPECT(g_calls.empty());

    printf("== client appears with two items, but the game is still in a menu\n");
    WriteItems("seed 123-AB c\noption kill_milestones 3\noption level_checks 4\noption zombrex_checks 2\noption psychopaths on\noption survivors on\ntally Cases|0|19\n1|$5,000|Bob\n2|Zombrex|\n");
    Tick(3);
    EXPECT(g_ap_active); EXPECT(g_ap_seed == "123-AB_c"); EXPECT(g_calls.empty()); EXPECT(!g_ap_in_play);

    printf("== Chuck takes a hit: the game is in play, items arrive\n");
    { uint32_t c[6] = { player, FloatToBits(500.0f), FloatToBits(400.0f), 0, 0, 0 }; ApOnGameMessage(0x1A, 0, 0, c); }
    Tick(2); EXPECT(g_calls.empty());                       // a hit alone is not enough: the save is still loading
    g_memory[bridge_instance + 0x08] = 1; Tick(3); EXPECT(g_calls.empty());   // live, but not yet for three seconds
    Tick(5);
    EXPECT(g_ap_in_play); EXPECT(Count(g_calls, "0x00842060 ecx=0x1000000 5000 0 0") == 1); EXPECT(Count(g_calls, "0x00842CD0 ecx=0x1000000 85001 0") == 1);
    EXPECT(Count(g_toasts, "Received $5,000 from Bob") == 1); EXPECT(Count(g_toasts, "Archipelago | Received Zombrex") == 1);
    EXPECT(ApStateGet("ap_index") == 2); EXPECT(ApStateGet("zombrex_found") == 0);   // our own Zombrex is not a find
    Tick(3); EXPECT(g_calls.size() == 2);                                             // and nothing is handed out twice

    printf("== cases and psychopaths come from mission states\n");
    Mission("Case1-3", 1); Mission("Case1-4", 0); Mission("TIRUltimateFan1", 1); Mission("Case7-3", 0); Mission("TheFacts", 0); Mission("Overtime", 0);
    g_ap_watched_missions.clear(); Tick(2);
    Text("81553 Completed"); Text("81554 Activated"); Tick(); EXPECT(!Outbox("Case 1-3: Insecurity"));   // messages alone no longer count
    Mission("Case1-3", 2); Tick(2); EXPECT(Outbox("Case 1-3: Insecurity"));
    Mission("Case1-4", 1); Tick(2); EXPECT(!Outbox("Case 1-4: Alliance"));
    Mission("Case1-4", 3); Tick(2); EXPECT(!Outbox("Case 1-4: Alliance")); EXPECT(Count(g_toasts, "Case 1-4: Alliance was failed") == 1);
    Mission("Case1-4", 2); Tick(2); EXPECT(Outbox("Case 1-4: Alliance"));
    printf("== survivors, psychopaths, level, Zombrex found, kills\n");
    Survivor("srv_esther", 0); Tick(); EXPECT(!Outbox("Rescue Esther Alwin"));
    Survivor("srv_esther", 2); Survivor("srv_jack_0", 2); Survivor("srv_nobody", 2); Tick();
    EXPECT(Outbox("Rescue Esther Alwin")); EXPECT(Outbox("Rescue Jack Ellis"));
    Text("80001 Completed"); Tick(); EXPECT(!Outbox("Psychopath: Meet the Contestants"));
    Mission("TIRUltimateFan1", 2); Tick(2); EXPECT(Outbox("Psychopath: Meet the Contestants"));
    { uint32_t c[6] = { 3102, 0, 9, 0, 1, 1 }; ApOnGameMessage(0x62, 0, 0, c); } Tick();
    EXPECT(Outbox("Reach Level 2")); EXPECT(Outbox("Reach Level 5")); EXPECT(!Outbox("Reach Level 6"));   // option: 4 level checks
    { uint32_t c[6] = { 85001, 0, 0, 0, 0, 0 }; ApOnGameMessage(0x13, 0, 0, c); ApOnGameMessage(0x13, 0, 0, c); ApOnGameMessage(0x13, 0, 0, c); } Tick();
    EXPECT(Outbox("Zombrex Found 1")); EXPECT(Outbox("Zombrex Found 2")); EXPECT(!Outbox("Zombrex Found 3"));
    g_memory[kStats + kStatsKills] = 260; Tick();
    EXPECT(Outbox("Kill 50 Zombies")); EXPECT(Outbox("Kill 250 Zombies")); EXPECT(!Outbox("Kill 500 Zombies"));

    printf("== death goes out once; a DeathLink coming in kills and is not echoed\n");
    { uint32_t c[6] = { player, FloatToBits(1.0f), FloatToBits(0.0f), 0, 0, 0 }; ApOnGameMessage(0x1A, 0, 0, c); ApOnGameMessage(0x06, static_cast<uint32_t>(g_exe_start + kDeathMessageCallerOffset), 0, c); } Tick();
    EXPECT(ReadAll("/tmp/aptest/dr2_ap_deaths.txt") == "died\r\n");
    remove("/tmp/aptest/dr2_ap_deaths.txt"); g_fake_tick += 60000;
    EXPECT(ApHandleCommand("deathlink Bob fell in a pit.")); Tick();
    EXPECT(BitsToFloat(g_memory[kStats + kStatsHealth]) == 0.0f); EXPECT(Count(g_toasts, "DeathLink | Bob fell in a pit.") == 1);
    g_fake_tick += 300000;   // Chuck only dies at his next hit or landing: five minutes later here
    { uint32_t c[6] = { player, FloatToBits(1.0f), FloatToBits(0.0f), 0, 0, 0 }; ApOnGameMessage(0x1A, 0, 0, c); } Tick();
    EXPECT(ReadAll("/tmp/aptest/dr2_ap_deaths.txt") == "<missing>");
    g_fake_tick += 60000;    // a later death of his own does go out
    { uint32_t c[6] = { player, FloatToBits(1.0f), FloatToBits(0.0f), 0, 0, 0 }; ApOnGameMessage(0x1A, 0, 0, c); } Tick();
    EXPECT(ReadAll("/tmp/aptest/dr2_ap_deaths.txt") == "died\r\n");

    printf("== traps that cannot run yet are owed and run later; a busy game delays items\n");
    WriteItems("seed 123-AB c\n1|$5,000|Bob\n2|Zombrex|\n3|Naked Trap|Bob\n4|Wrong Number Trap|\n5|1,000 PP|\n6|Case 2 Missions|Ann\n7|Mystery Item|\n");
    g_main_thread_ok = false; Tick(); EXPECT(ApStateGet("ap_index") == 4);          // traps pass, PP waits for the game
    EXPECT(ApStateGet("naked_owed") == 1); EXPECT(ApStateGet(ApOwedKey(kApTrapWrongNumber)) == 1);   // neither trap is lost
    g_main_thread_ok = true; Tick();
    EXPECT(ApStateGet("ap_index") == 7); EXPECT(Count(g_calls, "0x00845EC0 ecx=0x1000000 1000 3104 1 0") == 1); EXPECT(ApStateGet("gate_2") == 1);
    // the outfit's definition appears (its name sits at +0x18): the owed traps run at the next retry
    const uint32_t def = 0x6000000; g_memory[def] = 0x400000 + 0x48D870; g_texts[def + 0x18] = "NakedOutfitDispenser";
    g_fake_tick += 11000; Tick(2);
    EXPECT(Count(g_calls, "0x00636DA0 ecx=0x0 83886080 100663296 1") == 1); EXPECT(ApStateGet("naked_owed") == 0);
    EXPECT(Count(g_calls, "0x007071F0 ecx=0x8300000") == 1); EXPECT(ApStateGet(ApOwedKey(kApTrapWrongNumber)) == 0);

    printf("== regive after loading an older save: no traps, no gates\n");
    size_t before = g_calls.size(); EXPECT(ApHandleCommand("regive 6")); Tick();
    EXPECT(g_calls.size() == before + 2);   // Zombrex and 1,000 PP; not the trap, the gate or the unknown item
    EXPECT(ApStateGet("zombrex_found") == 3);

    printf("== The Facts and Overtime\n");
    Mission("Case7-3", 2); Tick(2); EXPECT(Outbox("Case 7-3: What Lies Beneath")); EXPECT(!Outbox("GOAL: The Facts"));
    Mission("TheFacts", 1); Tick(2); EXPECT(!Outbox("GOAL: The Facts"));
    Mission("TheFacts", 2); Tick(2); EXPECT(Outbox("GOAL: The Facts")); EXPECT(Outbox("The Facts")); EXPECT(!Outbox("GOAL: Overtime"));
    Mission("Overtime", 2); Tick(2); EXPECT(Outbox("GOAL: Overtime"));
    printf("== client closes; a new session picks the multiworld up where it left off\n");
    g_file_fresh = false; Tick(); EXPECT(!g_ap_active);
    Survivor("srv_chad", 2); Tick(); EXPECT(!Outbox("Rescue Chad Elchart"));
    g_ap_seed.clear(); g_ap_sent.clear(); g_ap_state.clear(); g_file_fresh = true; before = g_calls.size(); Tick(2);
    EXPECT(g_ap_active); EXPECT(ApStateGet("ap_index") == 7); EXPECT(g_calls.size() == before); EXPECT(g_ap_sent.count("Rescue Esther Alwin") == 1);
    EXPECT(ApHandleCommand("sendcheck Rescue Chad Elchart")); EXPECT(Outbox("Rescue Chad Elchart"));
    EXPECT(!ApHandleCommand("scan 5")); EXPECT(ApHandleCommand("ap"));
    printf("== research commands\n");
    g_memory[kStats + 0x5C] = 4; EXPECT(ApHandleCommand("slots")); EXPECT(ApHandleCommand("slots health 6")); EXPECT(g_memory[kStats + 0x5C] == 6);
    EXPECT(ApHandleCommand("slots health 99")); EXPECT(g_memory[kStats + 0x5C] == 6); EXPECT(ApHandleCommand("pattern 01 51 20 01"));

    printf("\noutbox:\n%s\nstate:\n%s\n", ReadAll("/tmp/aptest/dr2_ap_seeds/123-AB_c/dr2_ap_outbox.txt").c_str(), ReadAll("/tmp/aptest/dr2_ap_seeds/123-AB_c/dr2_ap_state.txt").c_str());
    printf(g_failed ? "%d CHECK(S) FAILED\n" : "ALL CHECKS PASSED\n", g_failed);
    return g_failed ? 1 : 0;
}

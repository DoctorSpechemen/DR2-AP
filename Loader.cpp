// DR2AP loader - Dead Rising 2 Archipelago plugin (loader 1.0)
//
// Loaded by Ultimate ASI Loader (dinput8.dll) as DR2AP.asi. It does one job: keep the real plugin
// logic (DR2AP_core.dll) loaded, and swap in a newer copy when the core asks for a reload.
//
// The core is loaded from a copy (DR2AP_core_live.dll), so DR2AP_core.dll itself is never locked
// and can be replaced while the game is running.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

static std::wstring g_game_dir;

static void LoaderLog(const char* text) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (g_game_dir + L"dr2_ap.log").c_str(), L"ab") != 0 || file == nullptr)
        return;

    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(file, "[%02u:%02u:%02u.%03u] loader: %s\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
    fclose(file);
}

static DWORD WINAPI LoaderThread(LPVOID) {
    const std::wstring source = g_game_dir + L"DR2AP_core.dll";
    const std::wstring live = g_game_dir + L"DR2AP_core_live.dll";
    bool reported_failure = false;

    for (;;) {
        HMODULE core = nullptr;
        if (CopyFileW(source.c_str(), live.c_str(), FALSE))
            core = LoadLibraryW(live.c_str());

        typedef void (*CoreRunFunction)();
        const CoreRunFunction core_run = core != nullptr ? reinterpret_cast<CoreRunFunction>(GetProcAddress(core, "CoreRun")) : nullptr;

        if (core_run == nullptr) {
            if (!reported_failure) {
                LoaderLog("could not load DR2AP_core.dll (missing, or not a valid core); will keep trying");
                reported_failure = true;
            }
            if (core != nullptr)
                FreeLibrary(core);
            Sleep(2000);
            continue;
        }

        reported_failure = false;
        LoaderLog("core loaded");
        core_run(); // Returns when the core has been asked to reload.
        FreeLibrary(core);
        LoaderLog("core unloaded; loading the current DR2AP_core.dll");
        Sleep(200);
    }
}

// Called by Ultimate ASI Loader once the game is ready. Work is done here, not in DllMain.
extern "C" __declspec(dllexport) void InitializeASI() {
    static bool started = false;
    if (started)
        return;
    started = true;

    wchar_t exe_path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    g_game_dir.assign(exe_path, length);
    const size_t slash = g_game_dir.find_last_of(L"\\/");
    g_game_dir = slash == std::wstring::npos ? L"" : g_game_dir.substr(0, slash + 1);

    const HANDLE thread = CreateThread(nullptr, 0, &LoaderThread, nullptr, 0, nullptr);
    if (thread != nullptr)
        CloseHandle(thread);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

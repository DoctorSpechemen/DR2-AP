// Minimal stand-ins so the plugin's own logic can be syntax-checked on Linux. Not used in the real build.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cwchar>
typedef unsigned long DWORD; typedef int BOOL; typedef void* HANDLE; typedef void* HMODULE; typedef void* PVOID; typedef void* LPVOID; typedef const void* LPCVOID;
typedef long LONG; typedef unsigned short WORD; typedef unsigned char BYTE; typedef const wchar_t* LPCWSTR; typedef void* HWND; typedef void* HDC; typedef void* HBITMAP; typedef void* HFONT; typedef void* HGDIOBJ;
typedef unsigned int UINT; typedef size_t SIZE_T; typedef unsigned long long ULONGLONG; typedef long HRESULT;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define CALLBACK
#define APIENTRY
#define WINAPI
#define __stdcall
#define __cdecl
#define __declspec(x)
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define _TRUNCATE ((size_t)-1)
#define CP_UTF8 65001
enum { MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000, PAGE_GUARD = 0x100, PAGE_NOACCESS = 1, PAGE_READONLY = 2, PAGE_READWRITE = 4, PAGE_WRITECOPY = 8, PAGE_EXECUTE_READ = 0x20, PAGE_EXECUTE_READWRITE = 0x40, PAGE_EXECUTE_WRITECOPY = 0x80 };
enum { GENERIC_READ = 1, OPEN_EXISTING = 3, FILE_ATTRIBUTE_NORMAL = 0x80, FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, FILE_SHARE_DELETE = 4 };
enum { EXCEPTION_EXECUTE_HANDLER = 1, EXCEPTION_CONTINUE_SEARCH = 0, EXCEPTION_CONTINUE_EXECUTION = -1, EXCEPTION_SINGLE_STEP = 0x80000004 };
enum { DLL_PROCESS_ATTACH = 1, VK_F7 = 0x76, SND_FILENAME = 1, SND_ASYNC = 2, SND_NODEFAULT = 4 };
enum { THREAD_SUSPEND_RESUME = 2, THREAD_GET_CONTEXT = 8, THREAD_SET_CONTEXT = 0x10, CONTEXT_DEBUG_REGISTERS = 0x10010, TH32CS_SNAPTHREAD = 4 };
enum { GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 4, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT = 2 };
struct CRITICAL_SECTION { int x; };
struct SYSTEMTIME { WORD wHour, wMinute, wSecond, wMilliseconds; };
struct MEMORY_BASIC_INFORMATION { PVOID BaseAddress; PVOID AllocationBase; DWORD State, Protect; SIZE_T RegionSize; };
struct SYSTEM_INFO { LPVOID lpMinimumApplicationAddress, lpMaximumApplicationAddress; };
union LARGE_INTEGER { long long QuadPart; };
union ULARGE_INTEGER { struct { DWORD LowPart; DWORD HighPart; }; unsigned long long QuadPart; };
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct WIN32_FILE_ATTRIBUTE_DATA { FILETIME ftLastWriteTime; };
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };
struct CONTEXT { DWORD ContextFlags, Dr0, Dr6, Dr7, Eip, Esp; };
struct EXCEPTION_RECORD { DWORD ExceptionCode; };
struct EXCEPTION_POINTERS { EXCEPTION_RECORD* ExceptionRecord; CONTEXT* ContextRecord; };
struct IMAGE_DOS_HEADER { LONG e_lfanew; };
struct IMAGE_NT_HEADERS { struct { DWORD SizeOfImage; } OptionalHeader; };
struct THREADENTRY32 { DWORD dwSize, th32OwnerProcessID, th32ThreadID; };
struct RECT { LONG left, top, right, bottom; };
struct RGNDATA;
void InitializeCriticalSection(CRITICAL_SECTION*); void DeleteCriticalSection(CRITICAL_SECTION*); void EnterCriticalSection(CRITICAL_SECTION*); void LeaveCriticalSection(CRITICAL_SECTION*);
void GetLocalTime(SYSTEMTIME*); DWORD GetTickCount(); void Sleep(DWORD); void YieldProcessor();
LONG InterlockedExchange(volatile LONG*, LONG); LONG InterlockedIncrement(volatile LONG*); LONG InterlockedDecrement(volatile LONG*); LONG InterlockedCompareExchange(volatile LONG*, LONG, LONG);
SIZE_T VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION*, SIZE_T); void GetSystemInfo(SYSTEM_INFO*); BOOL VirtualProtect(LPVOID, SIZE_T, DWORD, DWORD*);
LPVOID VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD); BOOL VirtualFree(LPVOID, SIZE_T, DWORD); BOOL FlushInstructionCache(HANDLE, LPCVOID, SIZE_T); HANDLE GetCurrentProcess();
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE); BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*); BOOL CloseHandle(HANDLE); BOOL DeleteFileW(LPCWSTR);
BOOL CreateDirectoryW(LPCWSTR, void*); BOOL GetFileAttributesExW(LPCWSTR, GET_FILEEX_INFO_LEVELS, void*); void GetSystemTimeAsFileTime(FILETIME*);
int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int);
HWND GetForegroundWindow(); DWORD GetWindowThreadProcessId(HWND, DWORD*); DWORD GetCurrentProcessId(); DWORD GetCurrentThreadId(); short GetAsyncKeyState(int);
DWORD GetModuleFileNameW(HMODULE, wchar_t*, DWORD); HMODULE GetModuleHandleW(LPCWSTR); BOOL GetModuleHandleExW(DWORD, LPCWSTR, HMODULE*); BOOL DisableThreadLibraryCalls(HMODULE);
PVOID AddVectoredExceptionHandler(DWORD, LONG (*)(EXCEPTION_POINTERS*)); DWORD RemoveVectoredExceptionHandler(PVOID);
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD); BOOL Thread32First(HANDLE, THREADENTRY32*); BOOL Thread32Next(HANDLE, THREADENTRY32*);
HANDLE OpenThread(DWORD, BOOL, DWORD); DWORD SuspendThread(HANDLE); DWORD ResumeThread(HANDLE); BOOL GetThreadContext(HANDLE, CONTEXT*); BOOL SetThreadContext(HANDLE, const CONTEXT*);
BOOL PlaySoundW(LPCWSTR, HMODULE, DWORD);
void ZeroMemory(void*, size_t);
int _wfopen_s(FILE**, const wchar_t*, const wchar_t*);
int vsnprintf_s(char*, size_t, size_t, const char*, va_list);
template <size_t N, typename... A> int sprintf_s(char (&buffer)[N], const char* format, A... args);
template <typename... A> int sprintf_s(char* buffer, size_t size, const char* format, A... args);
template <size_t N> int strncpy_s(char (&buffer)[N], const char* source, size_t count);
int strncpy_s(char* buffer, size_t size, const char* source, size_t count);
template <size_t N> int wcsncpy_s(wchar_t (&buffer)[N], const wchar_t* source, size_t count);
char* strtok_s(char*, const char*, char**);
long long _strtoi64(const char*, char**, int);

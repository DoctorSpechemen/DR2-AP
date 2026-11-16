#pragma once
#include <cstdint>
struct SafetyHookContext { uintptr_t eflags, edi, esi, edx, ecx, ebx, eax, ebp, esp, eip; };
struct SafetyHookMid { void reset(); };
namespace safetyhook { SafetyHookMid create_mid(void* target, void (*destination)(SafetyHookContext&)); }

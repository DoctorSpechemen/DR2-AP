#pragma once
struct ZydisDisassembledInstruction { struct { unsigned char length; } info; char text[96]; };
enum { ZYDIS_MACHINE_MODE_LEGACY_32 = 1 };
int ZydisDisassembleIntel(int, unsigned long long, const void*, size_t, ZydisDisassembledInstruction*);
#define ZYAN_SUCCESS(x) ((x) == 0)

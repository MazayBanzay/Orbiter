// D3D11Client: a watchdog for freezes. When no frame has come for 4 s (after the first 300: not while the scenario
// loads), every thread's stack goes once to Modules\D3D11Client\hang.log: each thread suspended only while its
// registers and a copy of its stack are taken, then the copy scanned for return addresses (values inside a module's
// code that follow a CALL), written as module+offset - the client's offsets resolve against build\<tag>\D3D11Client.map.
// Nothing here loads symbols, takes the loader lock or writes Orbiter.log from this thread: the watchdog itself must
// never be what holds the game.
#include "Client.h"
#include <tlhelp32.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

static std::atomic<bool> g_hwRun{ false };
static std::thread g_hw;
static BYTE g_stk[128 * 1024];      // one thread's stack copy at a time

struct HwMod { UINT_PTR lo, hi; char name[64]; };

static const HwMod* ModOf(const std::vector<HwMod>& ms, UINT_PTR a)
{
    for (const HwMod& m : ms) if (a >= m.lo && a < m.hi) return &m;
    return nullptr;
}

// a return address: the bytes before it are a CALL (E8 rel32, or FF /2 in its 2, 3, 6 and 7 byte forms)
static bool AfterCall(UINT_PTR a)
{
    BYTE b[8] = {}; SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(a - 8), b, 8, &got) || got != 8) return false;
    if (b[3] == 0xE8) return true;                                                      // call rel32
    if (b[6] == 0xFF && ((b[7] >> 3) & 7) == 2) return true;                            // call [reg] / call reg
    if (b[5] == 0xFF && ((b[6] >> 3) & 7) == 2) return true;                            // call [reg+d8]
    if (b[2] == 0xFF && ((b[3] >> 3) & 7) == 2) return true;                            // call [reg+d32] / [d32]
    if (b[1] == 0xFF && ((b[2] >> 3) & 7) == 2) return true;                            // call [reg+reg*s+d32]
    return false;
}

static void Dump(DWORD frame)
{
    FILE* f = fopen("Modules\\D3D11Client\\hang.log", "a");
    if (!f) return;
    std::vector<HwMod> ms;
    {
        HMODULE hm[512]; DWORD need = 0;
        if (EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need))
            for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 512); i++) {
                MODULEINFO mi = {}; HwMod m = {};
                if (!GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi)) continue;
                m.lo = (UINT_PTR)mi.lpBaseOfDll; m.hi = m.lo + mi.SizeOfImage;
                GetModuleBaseNameA(GetCurrentProcess(), hm[i], m.name, sizeof m.name);
                ms.push_back(m);
            }
    }
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "=== %02d:%02d:%02d no frame for 4 s: frame %u ===\n", st.wHour, st.wMinute, st.wSecond, frame);   // (no lock taken here)
    for (const HwMod& m : ms) if (strstr(m.name, "D3D11Client")) fprintf(f, "module %s at %08X\n", m.name, (unsigned)m.lo);
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te = {}; te.dwSize = sizeof te;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
            if (!th) continue;
            CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
            DWORD len = 0, lo = 0;
            if (SuspendThread(th) != (DWORD)-1) {
                if (GetThreadContext(th, &c)) {
                    MEMORY_BASIC_INFORMATION mi = {};
                    if (VirtualQuery((LPCVOID)(UINT_PTR)c.Esp, &mi, sizeof mi)) {
                        lo = c.Esp;
                        len = std::min<DWORD>((DWORD)(UINT_PTR)mi.BaseAddress + (DWORD)mi.RegionSize - c.Esp, sizeof g_stk);
                        memcpy(g_stk, (const void*)(UINT_PTR)c.Esp, len);
                    }
                }
                ResumeThread(th);
            }
            CloseHandle(th);
            const HwMod* m = ModOf(ms, c.Eip);
            fprintf(f, "thread %u: eip %s+%X |", te.th32ThreadID, m ? m->name : "?", m ? (unsigned)(c.Eip - m->lo) : (unsigned)c.Eip);
            int n = 0;
            for (DWORD o = 0; o + 4 <= len && n < 28; o += 4) {
                UINT_PTR v = *(DWORD*)(g_stk + o);
                const HwMod* mv = ModOf(ms, v);
                if (mv && AfterCall(v)) { fprintf(f, " %s+%X", mv->name, (unsigned)(v - mv->lo)); n++; }
            }
            fprintf(f, "\n");
        }
        CloseHandle(snap);
    }
    fclose(f);
}

void HangWatchStart()
{
    if (g_hwRun.exchange(true)) return;
    g_hw = std::thread([] {
        DWORD last = 0; ULONGLONG since = GetTickCount64(); bool told = false;
        while (g_hwRun) {
            Sleep(250);
            const DWORD fr = g_client ? g_client->frameNo : 0;
            const ULONGLONG now = GetTickCount64();
            if (fr != last) { last = fr; since = now; told = false; continue; }
            if (!told && fr > 300 && now - since > 4000) { told = true; Dump(fr); }
        }
    });
}

void HangWatchStop()
{
    if (!g_hwRun.exchange(false)) return;
    if (g_hw.joinable()) g_hw.join();
}

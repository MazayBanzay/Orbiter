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

// A crash report (Modules\D3D11Client\crash.log): an unhandled exception, from any thread, writes where it happened -
// the code, the faulting module+offset, the address read or written, the registers and the return addresses on the
// stack (as the hang dump does), then the exception goes on to Windows as before. Orbiter.log loses its last lines in
// a crash (it is written buffered); this file is written and closed before the process goes (testers' crashes,
// 2026-10-21)
// the shaders made last (file, entry, frame): the video driver compiles a new shader on threads of its own right after
// it is made, and a crash in its compiler has none of our code on the stack - this list names the shader it was on
// (a tester's crash in nvgpucomp32.dll, 2026-10-21)
struct ShNote { char what[80]; DWORD frame; };
static ShNote g_shRing[24];
static volatile LONG g_shN = 0;
void NoteShader(const char* file, const char* entry)
{
    const LONG i = InterlockedIncrement(&g_shN) - 1;
    ShNote& n = g_shRing[i % 24];
    const char* f = strrchr(file, '\\'); f = f ? f + 1 : file;
    _snprintf_s(n.what, _TRUNCATE, "%s %s", f, entry ? entry : "");
    n.frame = g_client ? g_client->frameNo : 0;
}

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
static volatile LONG g_crashOnce = 0;
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    if (InterlockedExchange(&g_crashOnce, 1) == 0 && ep && ep->ExceptionRecord && ep->ContextRecord) {
        if (FILE* f = fopen("Modules\\D3D11Client\\crash.log", "a")) {
            std::vector<HwMod> ms;
            HMODULE hm[512]; DWORD need = 0;
            if (EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need))
                for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 512); i++) {
                    MODULEINFO mi = {}; HwMod m = {};
                    if (!GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi)) continue;
                    m.lo = (UINT_PTR)mi.lpBaseOfDll; m.hi = m.lo + mi.SizeOfImage;
                    GetModuleBaseNameA(GetCurrentProcess(), hm[i], m.name, sizeof m.name);
                    ms.push_back(m);
                }
            const EXCEPTION_RECORD& er = *ep->ExceptionRecord; const CONTEXT& c = *ep->ContextRecord;
            SYSTEMTIME st; GetLocalTime(&st);
            const HwMod* m = ModOf(ms, (UINT_PTR)er.ExceptionAddress);
            fprintf(f, "=== %04d-%02d-%02d %02d:%02d:%02d crash: code %08X at %s+%X, thread %u, frame %u ===\n",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (unsigned)er.ExceptionCode,
                    m ? m->name : "?", m ? (unsigned)((UINT_PTR)er.ExceptionAddress - m->lo) : (unsigned)(UINT_PTR)er.ExceptionAddress,
                    GetCurrentThreadId(), g_client ? (unsigned)g_client->frameNo : 0u);
            if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2)
                fprintf(f, "access violation: %s address %08X\n", er.ExceptionInformation[0] == 1 ? "writing" : er.ExceptionInformation[0] == 8 ? "executing" : "reading", (unsigned)er.ExceptionInformation[1]);
            fprintf(f, "eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X\n",
                    (unsigned)c.Eax, (unsigned)c.Ebx, (unsigned)c.Ecx, (unsigned)c.Edx, (unsigned)c.Esi, (unsigned)c.Edi, (unsigned)c.Ebp, (unsigned)c.Esp);
            for (const HwMod& mm : ms) if (strstr(mm.name, "D3D11Client") || strstr(mm.name, "nv") == mm.name) fprintf(f, "module %s at %08X\n", mm.name, (unsigned)mm.lo);
            // the stack: return addresses (after a CALL) from esp up, read in place (this thread is the crashed one)
            MEMORY_BASIC_INFORMATION mi = {};
            if (VirtualQuery((LPCVOID)(UINT_PTR)c.Esp, &mi, sizeof mi)) {
                const UINT_PTR top = (UINT_PTR)mi.BaseAddress + mi.RegionSize;
                fprintf(f, "stack:");
                int n = 0;
                for (UINT_PTR a = c.Esp; a + 4 <= top && n < 40; a += 4) {
                    const UINT_PTR v = *(const DWORD*)a;
                    const HwMod* mv = ModOf(ms, v);
                    if (mv && AfterCall(v)) { fprintf(f, " %s+%X", mv->name, (unsigned)(v - mv->lo)); n++; }
                }
                fprintf(f, "\n");
            }
            const LONG n = g_shN;
            fprintf(f, "shaders made last (oldest first, of %ld):", n);
            for (LONG k = std::max<LONG>(0, n - 24); k < n; k++) fprintf(f, " [%u] %s;", (unsigned)g_shRing[k % 24].frame, g_shRing[k % 24].what);
            fprintf(f, "\n");
            fclose(f);
        }
    }
    if (getenv("D3D11CLIENT_TEST")) return EXCEPTION_EXECUTE_HANDLER;   // test runs: end quietly (no error dialog left open)
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void HangWatchStart()
{
    static bool filterOn = false;
    if (!filterOn) { filterOn = true; g_prevFilter = SetUnhandledExceptionFilter(CrashFilter); }
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

// The address space by owner, once a run (LogMemory, Client.cpp, calls it at the second "memory now"): in a 32-bit
// Orbiter the 2 GB of addresses run out long before the RAM, and LogMemory's kinds (image / mapped / private / reserved)
// do not say who holds them. Here every allocation is put to an owner:
//   image    - each module's code and data (the biggest modules by name)
//   heap     - the segments of each process heap (the CRT's malloc/new land in the default one), committed and reserved
//   heap big - blocks a heap took straight from VirtualAlloc (allocations over ~512 KB: they sit outside the segments)
//   stack    - the threads' stacks (each reserves its full size, 1 MB by default, commits only what it used)
//   other    - VirtualAlloc made directly by someone (drivers, runtimes, modules): the biggest listed with their sizes
// The mapped views are listed by LogMemory itself. Costs a walk of the heaps (a few ms; the heap is locked meanwhile).
#include "Client.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#pragma comment(lib, "psapi.lib")

namespace {
    struct Alloc { SIZE_T commit = 0, reserve = 0; DWORD prot = 0; int kind = 0; int heap = -1; };   // kind: 0 other, 1 heap, 2 heap big, 3 stack
    const char* kKind[] = { "other", "heap", "heap big", "stack" };

    void ThreadStacks(std::set<const void*>& bases)
    {
        typedef NTSTATUS(NTAPI* QIT)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
        struct TBI { NTSTATUS st; PVOID teb; CLIENT_ID cid; ULONG_PTR aff; LONG pri, basePri; };
        QIT q = (QIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
        if (!q) return;
        HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (sn == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te = { sizeof te };
        const DWORD pid = GetCurrentProcessId();
        for (BOOL ok = Thread32First(sn, &te); ok; ok = Thread32Next(sn, &te)) {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th) continue;
            TBI b = {};
            if (q(th, (THREADINFOCLASS)0, &b, sizeof b, nullptr) == 0 && b.teb) {
                const NT_TIB* tib = (const NT_TIB*)b.teb;      // our own process: the TEB is readable here
                MEMORY_BASIC_INFORMATION mi;
                if (VirtualQuery((const char*)tib->StackBase - 1, &mi, sizeof mi)) bases.insert(mi.AllocationBase);
            }
            CloseHandle(th);
        }
        CloseHandle(sn);
    }
}

void LogMemoryCensus()
{
    LARGE_INTEGER t0, t1, f; QueryPerformanceCounter(&t0); QueryPerformanceFrequency(&f);
    std::map<const void*, Alloc> priv;                   // private allocations by base
    std::map<const void*, SIZE_T> img;                   // modules by base
    MEMORY_BASIC_INFORMATION mi;
    for (const char* a = nullptr; VirtualQuery(a, &mi, sizeof mi); a = (const char*)mi.BaseAddress + mi.RegionSize) {
        if (mi.State != MEM_FREE) {
            if (mi.Type == MEM_IMAGE) img[mi.AllocationBase] += mi.RegionSize;
            else if (mi.Type == MEM_PRIVATE || mi.State == MEM_RESERVE) {
                Alloc& al = priv[mi.AllocationBase];
                if (mi.State == MEM_COMMIT) { al.commit += mi.RegionSize; if (!al.prot) al.prot = mi.Protect; }
                else al.reserve += mi.RegionSize;
            }
        }
        if ((const char*)mi.BaseAddress + mi.RegionSize < (const char*)mi.BaseAddress) break;
    }
    // heaps: their segments (regions) and their big blocks
    HANDLE heaps[64]; const DWORD nh = std::min<DWORD>(GetProcessHeaps(64, heaps), 64);
    const HANDLE crt = GetProcessHeap();
    int defHeap = -1;
    for (DWORD i = 0; i < nh; i++) {
        if (heaps[i] == crt) defHeap = (int)i;
        if (!HeapLock(heaps[i])) continue;
        PROCESS_HEAP_ENTRY e = {};
        while (HeapWalk(heaps[i], &e)) {
            if (!e.lpData || !VirtualQuery(e.lpData, &mi, sizeof mi)) continue;
            auto it = priv.find(mi.AllocationBase);
            if (it == priv.end()) continue;
            if (e.wFlags & PROCESS_HEAP_REGION) { it->second.kind = 1; it->second.heap = (int)i; }
            else if (it->second.kind == 0) { it->second.kind = 2; it->second.heap = (int)i; }
        }
        HeapUnlock(heaps[i]);
    }
    std::set<const void*> stacks; ThreadStacks(stacks);
    for (const void* b : stacks) { auto it = priv.find(b); if (it != priv.end()) it->second.kind = 3; }

    QueryPerformanceCounter(&t1);
    SIZE_T sumC[4] = {}, sumR[4] = {}; int cnt[4] = {};
    for (auto& e : priv) { sumC[e.second.kind] += e.second.commit; sumR[e.second.kind] += e.second.reserve; cnt[e.second.kind]++; }
    LOGC("memory census (%.1f ms; %u heaps, the CRT's is #%d, %zu thread stacks):", (t1.QuadPart - t0.QuadPart) * 1e3 / f.QuadPart,
         (unsigned)nh, defHeap, stacks.size());
    for (int k = 0; k < 4; k++)
        LOGC("memory census:   %-8s %4d allocations, committed %6.1f MB, reserved only %6.1f MB", kKind[k], cnt[k],
             sumC[k] / 1048576.0, sumR[k] / 1048576.0);
    // per heap
    for (DWORD i = 0; i < nh; i++) {
        SIZE_T c = 0, r = 0, bc = 0;
        for (auto& e : priv) if (e.second.heap == (int)i) { if (e.second.kind == 1) { c += e.second.commit; r += e.second.reserve; } else bc += e.second.commit + e.second.reserve; }
        if (c + r + bc >= (4u << 20)) LOGC("memory census:   heap #%u%s: segments committed %.1f MB + reserved %.1f MB, big blocks %.1f MB", (unsigned)i,
                                           (int)i == defHeap ? " (default, CRT)" : "", c / 1048576.0, r / 1048576.0, bc / 1048576.0);
    }
    // the biggest direct allocations, and how many of each size (many of one size = one owner's pool)
    std::vector<std::pair<SIZE_T, const void*>> big;
    std::map<SIZE_T, std::pair<int, SIZE_T>> bySize;
    for (auto& e : priv) if (e.second.kind == 0) {
        const SIZE_T s = e.second.commit + e.second.reserve;
        big.push_back({ s, e.first });
        auto& bs = bySize[(s + 0xFFFF) & ~(SIZE_T)0xFFFF]; bs.first++; bs.second += s;
    }
    std::sort(big.begin(), big.end(), [](auto& x, auto& y) { return x.first > y.first; });
    for (size_t i = 0; i < big.size() && i < 20; i++) {
        const Alloc& al = priv[big[i].second];
        LOGC("memory census:   other %7.1f MB at %p: committed %.1f, reserved %.1f MB, protect 0x%X", big[i].first / 1048576.0,
             big[i].second, al.commit / 1048576.0, al.reserve / 1048576.0, al.prot);
    }
    std::vector<std::pair<SIZE_T, SIZE_T>> sz;     // (total, size)
    for (auto& e : bySize) if (e.second.first >= 4) sz.push_back({ e.second.second, e.first });
    std::sort(sz.begin(), sz.end(), [](auto& x, auto& y) { return x.first > y.first; });
    for (size_t i = 0; i < sz.size() && i < 8; i++)
        LOGC("memory census:   other of %.2f MB each: %d of them, %.1f MB", sz[i].second / 1048576.0, bySize[sz[i].second].first, sz[i].first / 1048576.0);
    // modules
    std::vector<std::pair<SIZE_T, const void*>> mods;
    for (auto& e : img) mods.push_back({ e.second, e.first });
    std::sort(mods.begin(), mods.end(), [](auto& x, auto& y) { return x.first > y.first; });
    SIZE_T imgSum = 0; for (auto& m : mods) imgSum += m.first;
    LOGC("memory census:   image %.1f MB in %zu modules, the biggest:", imgSum / 1048576.0, mods.size());
    for (size_t i = 0; i < mods.size() && i < 12; i++) {
        char fn[MAX_PATH] = "?";
        GetModuleFileNameA((HMODULE)mods[i].second, fn, MAX_PATH);
        const char* b = strrchr(fn, 92);
        LOGC("memory census:     %6.1f MB %s", mods[i].first / 1048576.0, b ? b + 1 : fn);
    }
}

// Who holds the C/C++ heap, by module (test runs only: Client.cfg "HeapByModule = 1", read when the client loads).
// The census (MemCensus.cpp) shows the CRT heap as the biggest owner of the 32-bit address space (~400 MB on Titan) but
// not whose blocks those are. Here every module's calls of the CRT's malloc / calloc / realloc / free (imported from
// api-ms-win-crt-heap / ucrtbase: Orbiter.exe, the client, every add-on built with /MD) go through our functions,
// patched into that module's import table; each live block is kept in a table (address -> module, size) so that a
// free, from whichever module, takes its bytes back off the module that made it. Modules loaded later (vessels' DLLs)
// are patched as Orbiter.exe loads them (its LoadLibrary* imports are patched too) and at each report.
// Blocks made before the patch (Orbiter's start, before the plugins) are not known: they show as the heap's "untracked"
// rest. The table lives outside the CRT heap (VirtualAlloc), 32 MB of addresses: a diagnostic, never on in play.
#include "Client.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace {
    typedef void* (__cdecl* MallocF)(size_t);
    typedef void* (__cdecl* CallocF)(size_t, size_t);
    typedef void* (__cdecl* ReallocF)(void*, size_t);
    typedef void  (__cdecl* FreeF)(void*);
    typedef HMODULE(WINAPI* LoadLibAF)(LPCSTR);
    typedef HMODULE(WINAPI* LoadLibWF)(LPCWSTR);
    typedef HMODULE(WINAPI* LoadLibExAF)(LPCSTR, HANDLE, DWORD);
    typedef HMODULE(WINAPI* LoadLibExWF)(LPCWSTR, HANDLE, DWORD);
    MallocF rMalloc; CallocF rCalloc; ReallocF rRealloc; FreeF rFree;
    LoadLibAF rLLA; LoadLibWF rLLW; LoadLibExAF rLLExA; LoadLibExWF rLLExW;

    // modules: address ranges (sorted) -> index; per index the live bytes and blocks
    struct Mod { uintptr_t lo, hi; char name[48]; };
    const int MAXMOD = 255;
    Mod mods[MAXMOD + 1]; int nMods = 0;          // index 0: "unknown" (a caller outside every patched module)
    volatile LONGLONG liveB[MAXMOD + 1]; volatile LONG liveN[MAXMOD + 1];
    std::vector<std::pair<uintptr_t, int>> order;    // (lo, index) sorted, for the caller's lookup
    SRWLOCK modLock = SRWLOCK_INIT;
    std::set<HMODULE> patched;

    // the live blocks: open addressing, linear probing, backward-shift deletion; key 0 = empty
    struct Slot { uint32_t p, info; };              // info: module (8 bits) | size in 16-byte units (24 bits)
    const uint32_t TBITS = 22, TSIZE = 1u << TBITS, TMASK = TSIZE - 1;
    Slot* tab = nullptr; uint32_t tUsed = 0, tDropped = 0;
    CRITICAL_SECTION tLock;
    bool on = false;
    thread_local int inside = 0;     // this thread is in our own bookkeeping: its allocations are not counted (no lock taken twice)

    inline uint32_t H(uint32_t p) { return (p >> 4) * 2654435761u >> (32 - TBITS); }

    int ModOf(uintptr_t a)
    {
        AcquireSRWLockShared(&modLock);
        int r = 0;
        auto it = std::upper_bound(order.begin(), order.end(), std::make_pair(a, INT_MAX));
        if (it != order.begin()) { --it; if (a < mods[it->second].hi) r = it->second; }
        ReleaseSRWLockShared(&modLock);
        return r;
    }

    void Add(void* p, size_t n, int m)
    {
        if (!p) return;
        uint32_t units = (uint32_t)std::min<size_t>((n + 15) >> 4, 0xFFFFFF);
        EnterCriticalSection(&tLock);
        if (tUsed < TSIZE / 10 * 7) {
            uint32_t i = H((uint32_t)(uintptr_t)p);
            while (tab[i].p && tab[i].p != (uint32_t)(uintptr_t)p) i = (i + 1) & TMASK;
            if (!tab[i].p) tUsed++;
            else { const int om = tab[i].info >> 24; InterlockedExchangeAdd64(&liveB[om], -(LONGLONG)((tab[i].info & 0xFFFFFF) << 4)); InterlockedDecrement(&liveN[om]); }
            tab[i] = { (uint32_t)(uintptr_t)p, ((uint32_t)m << 24) | units };
            InterlockedExchangeAdd64(&liveB[m], (LONGLONG)units << 4); InterlockedIncrement(&liveN[m]);
        } else tDropped++;
        LeaveCriticalSection(&tLock);
    }

    void Remove(void* p)
    {
        if (!p) return;
        EnterCriticalSection(&tLock);
        uint32_t i = H((uint32_t)(uintptr_t)p);
        while (tab[i].p && tab[i].p != (uint32_t)(uintptr_t)p) i = (i + 1) & TMASK;
        if (tab[i].p) {
            const int m = tab[i].info >> 24;
            InterlockedExchangeAdd64(&liveB[m], -(LONGLONG)((tab[i].info & 0xFFFFFF) << 4)); InterlockedDecrement(&liveN[m]);
            // backward-shift deletion: pull later entries of the run back so no probe chain breaks
            uint32_t j = i;
            for (;;) {
                j = (j + 1) & TMASK;
                if (!tab[j].p) break;
                const uint32_t k = H(tab[j].p);
                if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) { tab[i] = tab[j]; i = j; }
            }
            tab[i] = { 0, 0 }; tUsed--;
        }
        LeaveCriticalSection(&tLock);
    }

    // where in the client the bytes are asked for (blocks of 4 KB and more): the call stack's first frames, summed over
    // the run (allocated, not live - for data kept, as caches, the two are close); reported as module offsets for the PDB
    // ... and what each site still holds (live): its blocks remembered by address in a table of their own, so a free
    // takes the bytes back off the site that asked for them (the peaks a zoom leaves behind, 2026-10-21)
    struct Site { uint32_t key; uint32_t fr[6]; LONGLONG bytes; LONG n; LONGLONG live; LONG liveN; };
    const int NSITE = 4096;
    Site sites[NSITE]; int clientMod = -1;
    struct BSlot { uint32_t p; int32_t site; uint32_t sz; };
    const uint32_t BBITS = 18, BSIZE = 1u << BBITS, BMASK = BSIZE - 1;
    BSlot* btab = nullptr; uint32_t bUsed = 0;
    inline uint32_t HB(uint32_t p) { return (p >> 4) * 2654435761u >> (32 - BBITS); }
    int Note(size_t n)
    {
        void* fr[8] = {};
        const USHORT c = CaptureStackBackTrace(2, 8, fr, nullptr);     // past hMalloc and the module's operator new
        uint32_t k = 2166136261u;
        for (USHORT i = 0; i < c && i < 6; i++) k = (k ^ (uint32_t)(uintptr_t)fr[i]) * 16777619u;
        int r = -1;
        EnterCriticalSection(&tLock);
        for (uint32_t i = k & (NSITE - 1), probes = 0; probes < 64; i = (i + 1) & (NSITE - 1), probes++) {
            Site& st = sites[i];
            if (st.n && st.key != k) continue;
            if (!st.n) { st.key = k; for (int j = 0; j < 6; j++) st.fr[j] = j < c ? (uint32_t)(uintptr_t)fr[j] : 0; }
            st.bytes += n; st.n++; r = (int)i;
            break;
        }
        LeaveCriticalSection(&tLock);
        return r;
    }
    void AddBig(void* p, int site, size_t n)
    {
        if (!p || site < 0 || !btab) return;
        EnterCriticalSection(&tLock);
        if (bUsed < BSIZE / 10 * 7) {
            uint32_t i = HB((uint32_t)(uintptr_t)p);
            while (btab[i].p && btab[i].p != (uint32_t)(uintptr_t)p) i = (i + 1) & BMASK;
            if (!btab[i].p) bUsed++;
            else { sites[btab[i].site].live -= btab[i].sz; sites[btab[i].site].liveN--; }
            btab[i] = { (uint32_t)(uintptr_t)p, site, (uint32_t)n };
            sites[site].live += n; sites[site].liveN++;
        }
        LeaveCriticalSection(&tLock);
    }
    void RemoveBig(void* p)
    {
        if (!p || !btab) return;
        EnterCriticalSection(&tLock);
        uint32_t i = HB((uint32_t)(uintptr_t)p);
        while (btab[i].p && btab[i].p != (uint32_t)(uintptr_t)p) i = (i + 1) & BMASK;
        if (btab[i].p) {
            sites[btab[i].site].live -= btab[i].sz; sites[btab[i].site].liveN--;
            uint32_t j = i;
            for (;;) {
                j = (j + 1) & BMASK;
                if (!btab[j].p) break;
                const uint32_t k = HB(btab[j].p);
                if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) { btab[i] = btab[j]; i = j; }
            }
            btab[i] = { 0, 0, 0 }; bUsed--;
        }
        LeaveCriticalSection(&tLock);
    }

    void* __cdecl hMalloc(size_t n) { void* p = rMalloc(n); if (on && !inside) { const int m = ModOf((uintptr_t)_ReturnAddress()); Add(p, n, m); if (m == clientMod && n >= 4096) AddBig(p, Note(n), n); } return p; }
    void* __cdecl hCalloc(size_t c, size_t n) { void* p = rCalloc(c, n); if (on && !inside) Add(p, c * n, ModOf((uintptr_t)_ReturnAddress())); return p; }
    void* __cdecl hRealloc(void* q, size_t n)
    {
        void* p = rRealloc(q, n);
        if (on && !inside) { if (q && (p || !n)) { Remove(q); RemoveBig(q); } if (p) { const int m = ModOf((uintptr_t)_ReturnAddress()); Add(p, n, m); if (m == clientMod && n >= 4096) AddBig(p, Note(n), n); } }
        return p;
    }
    void __cdecl hFree(void* p) { if (on && !inside && p) { Remove(p); RemoveBig(p); } rFree(p); }

    void PatchAll();
    HMODULE WINAPI hLLA(LPCSTR a) { HMODULE m = rLLA(a); if (m) PatchAll(); return m; }
    HMODULE WINAPI hLLW(LPCWSTR a) { HMODULE m = rLLW(a); if (m) PatchAll(); return m; }
    HMODULE WINAPI hLLExA(LPCSTR a, HANDLE h, DWORD f) { HMODULE m = rLLExA(a, h, f); if (m) PatchAll(); return m; }
    HMODULE WINAPI hLLExW(LPCWSTR a, HANDLE h, DWORD f) { HMODULE m = rLLExW(a, h, f); if (m) PatchAll(); return m; }

    void SetIat(void** slot, void* to)
    {
        DWORD old;
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { *slot = to; VirtualProtect(slot, sizeof(void*), old, &old); }
    }

    // a module's imports: the CRT heap functions (and, in Orbiter.exe, LoadLibrary*) pointed at ours
    void PatchModule(HMODULE h, bool loader)
    {
        auto* b = (BYTE*)h;
        auto* dos = (IMAGE_DOS_HEADER*)b;
        auto* nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) return;
        for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dir.VirtualAddress); d->Name; d++) {
            const char* dll = (const char*)(b + d->Name);
            const bool crt = !_strnicmp(dll, "api-ms-win-crt-heap", 19) || !_stricmp(dll, "ucrtbase.dll");
            const bool k32 = loader && !_stricmp(dll, "KERNEL32.dll");
            if (!crt && !k32) continue;
            if (!d->OriginalFirstThunk) continue;
            auto* names = (IMAGE_THUNK_DATA*)(b + d->OriginalFirstThunk);
            auto* iat = (IMAGE_THUNK_DATA*)(b + d->FirstThunk);
            for (; names->u1.AddressOfData; names++, iat++) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                const char* fn = (const char*)((IMAGE_IMPORT_BY_NAME*)(b + names->u1.AddressOfData))->Name;
                void* to = nullptr;
                if (crt) {
                    if (!strcmp(fn, "malloc")) to = (void*)hMalloc;
                    else if (!strcmp(fn, "calloc")) to = (void*)hCalloc;
                    else if (!strcmp(fn, "realloc")) to = (void*)hRealloc;
                    else if (!strcmp(fn, "free")) to = (void*)hFree;
                } else {
                    if (!strcmp(fn, "LoadLibraryA")) to = (void*)hLLA;
                    else if (!strcmp(fn, "LoadLibraryW")) to = (void*)hLLW;
                    else if (!strcmp(fn, "LoadLibraryExA")) to = (void*)hLLExA;
                    else if (!strcmp(fn, "LoadLibraryExW")) to = (void*)hLLExW;
                }
                if (to && (void*)iat->u1.Function != to) SetIat((void**)&iat->u1.Function, to);
            }
        }
    }

    // every module not of Windows itself (system DLLs keep their own heaps and CRTs): patched once, given an index
    void PatchAll()
    {
        struct In { In() { inside++; } ~In() { inside--; } } in;
        HMODULE hm[1024]; DWORD need = 0;
        if (!EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need)) return;
        const DWORD n = std::min<DWORD>(need / sizeof(HMODULE), 1024);
        char win[MAX_PATH]; const UINT wl = GetWindowsDirectoryA(win, MAX_PATH);
        HMODULE exe = GetModuleHandleA(nullptr);
        for (DWORD i = 0; i < n; i++) {
            AcquireSRWLockExclusive(&modLock);
            const bool seen = !patched.insert(hm[i]).second;
            ReleaseSRWLockExclusive(&modLock);
            if (seen) continue;
            char path[MAX_PATH] = {};
            GetModuleFileNameA(hm[i], path, MAX_PATH);
            if (wl && !_strnicmp(path, win, wl)) continue;      // Windows, the driver store
            MODULEINFO mi = {};
            GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi);
            AcquireSRWLockExclusive(&modLock);
            if (nMods < MAXMOD) {
                Mod& m = mods[++nMods];
                m.lo = (uintptr_t)mi.lpBaseOfDll; m.hi = m.lo + mi.SizeOfImage;
                const char* s = strrchr(path, 92); strncpy_s(m.name, s ? s + 1 : path, _TRUNCATE);
                if (!_stricmp(m.name, "D3D11Client.dll")) clientMod = nMods;
                order.push_back({ m.lo, nMods });
                std::sort(order.begin(), order.end());
            }
            ReleaseSRWLockExclusive(&modLock);
            PatchModule(hm[i], hm[i] == exe);
        }
    }
}

void HeapTrackStart()
{
    double FxTestNum(const char*);
    if (on || FxTestNum("HeapByModule") <= 0) return;
    HMODULE crt = GetModuleHandleA("ucrtbase.dll");
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!crt || !k32) return;
    rMalloc = (MallocF)GetProcAddress(crt, "malloc"); rCalloc = (CallocF)GetProcAddress(crt, "calloc");
    rRealloc = (ReallocF)GetProcAddress(crt, "realloc"); rFree = (FreeF)GetProcAddress(crt, "free");
    rLLA = (LoadLibAF)GetProcAddress(k32, "LoadLibraryA"); rLLW = (LoadLibWF)GetProcAddress(k32, "LoadLibraryW");
    rLLExA = (LoadLibExAF)GetProcAddress(k32, "LoadLibraryExA"); rLLExW = (LoadLibExWF)GetProcAddress(k32, "LoadLibraryExW");
    if (!rMalloc || !rCalloc || !rRealloc || !rFree || !rLLA || !rLLW || !rLLExA || !rLLExW) return;
    tab = (Slot*)VirtualAlloc(nullptr, TSIZE * sizeof(Slot), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!tab) return;
    btab = (BSlot*)VirtualAlloc(nullptr, BSIZE * sizeof(BSlot), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    InitializeCriticalSection(&tLock);
    strcpy_s(mods[0].name, "(other callers)");
    on = true;
    PatchAll();
    LOGC("heap by module: on (%d modules patched)", nMods);
}

// the report (with each "memory now" line): the modules holding the most, and the heap's untracked rest
void HeapTrackReport()
{
    if (!on) return;
    PatchAll();
    std::vector<std::pair<LONGLONG, int>> v;
    LONGLONG sum = 0;
    for (int i = 0; i <= nMods; i++) { const LONGLONG b = liveB[i]; sum += b; if (b > 0) v.push_back({ b, i }); }
    std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
    LOGC("heap by module: tracked %.1f MB in %u blocks%s", sum / 1048576.0, tUsed, tDropped ? " (table full: some dropped)" : "");
    std::string line;
    for (size_t i = 0; i < v.size() && i < 14; i++) {
        char b[96]; sprintf_s(b, "%s%s %.1f MB (%ld)", i ? ", " : "", mods[v[i].second].name, v[i].first / 1048576.0, liveN[v[i].second]);
        line += b;
    }
    if (!line.empty()) LOGC("heap by module: %s", line.c_str());
    // the client's call sites by what they still hold (blocks of 4 KB and more): every report
    static int nRep = 0;
    if (clientMod > 0) {
        std::vector<int> idx;
        for (int i = 0; i < NSITE; i++) if (sites[i].liveN > 0) idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [](int x, int y) { return sites[x].live > sites[y].live; });
        const uintptr_t lo = mods[clientMod].lo, hi = mods[clientMod].hi;
        for (size_t i = 0; i < idx.size() && i < 10; i++) {
            const Site& st = sites[idx[i]];
            char fr[160] = ""; size_t o = 0;
            for (int j = 0; j < 6 && st.fr[j]; j++) {
                const uintptr_t a = st.fr[j];
                if (a >= lo && a < hi) o += sprintf_s(fr + o, sizeof fr - o, " +%x", (unsigned)(a - lo));
                else { const int m = ModOf(a); o += sprintf_s(fr + o, sizeof fr - o, " [%s]", m ? mods[m].name : "sys"); }
                if (o > sizeof fr - 24) break;
            }
            LOGC("heap live: %7.1f MB in %5ld blocks:%s", st.live / 1048576.0, st.liveN, fr);
            if (i == 0 && btab) {   // the biggest holder: its live blocks by size (a size names the buffer when the stack can't)
                std::map<uint32_t, std::pair<LONG, LONGLONG>> bySize;
                EnterCriticalSection(&tLock);
                for (uint32_t k = 0; k < BSIZE; k++) if (btab[k].p && btab[k].site == idx[0]) { auto& e = bySize[btab[k].sz]; e.first++; e.second += btab[k].sz; }
                LeaveCriticalSection(&tLock);
                std::vector<std::pair<LONGLONG, uint32_t>> bs;
                for (auto& e : bySize) bs.push_back({ e.second.second, e.first });
                std::sort(bs.begin(), bs.end(), [](auto& x, auto& y) { return x.first > y.first; });
                std::string ln;
                for (size_t q = 0; q < bs.size() && q < 12; q++) { char b[64]; sprintf_s(b, "%s%u B x%ld = %.1f MB", q ? ", " : "", bs[q].second, bySize[bs[q].second].first, bs[q].first / 1048576.0); ln += b; }
                LOGC("heap live by size: %s", ln.c_str());
            }
        }
    }
    // once, at the third report: the client's biggest call sites
    if (++nRep == 3 && clientMod > 0) {
        std::vector<int> idx;
        for (int i = 0; i < NSITE; i++) if (sites[i].n) idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [](int x, int y) { return sites[x].bytes > sites[y].bytes; });
        const uintptr_t lo = mods[clientMod].lo, hi = mods[clientMod].hi;
        for (size_t i = 0; i < idx.size() && i < 16; i++) {
            const Site& st = sites[idx[i]];
            char fr[160] = ""; size_t o = 0;
            for (int j = 0; j < 6 && st.fr[j]; j++) {
                const uintptr_t a = st.fr[j];
                if (a >= lo && a < hi) o += sprintf_s(fr + o, sizeof fr - o, " +%x", (unsigned)(a - lo));
                else { const int m = ModOf(a); o += sprintf_s(fr + o, sizeof fr - o, " [%s]", m ? mods[m].name : "sys"); }
                if (o > sizeof fr - 24) break;
            }
            LOGC("heap site: %7.1f MB in %5ld blocks:%s", st.bytes / 1048576.0, st.n, fr);
        }
    }
}

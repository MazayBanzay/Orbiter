// OrbiterCrew - the main-thread probe (see StallProbe.h).
#include "StallProbe.h"
#include <Orbitersdk.h>
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

namespace ocrew::stallprobe
{
#if defined(_M_IX86)
	namespace
	{
		const double STALL = 0.100;          // s without a crew step: the main thread is in a long frame
		const DWORD PERIOD_MS = 5;
		// copies kept: the long frames' samples (all, up to MAXS) and a fair pick of the normal frames' (a reservoir)
		const size_t MAXS = 600, MAXN = 600, STACK = 16 * 1024, MAXF = 60000, MAXM = 32;

		struct Sample { double t; DWORD eip, ebp, esp, top, len; };
		struct MarkRec { double t; char what[64]; };

		double qf = 0, windowS = 40;
		double Now() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return double(q.QuadPart) / qf; }

		HANDLE worker = nullptr, mainThread = nullptr;
		DWORD stackBase = 0;   // the main thread's stack top (its TIB): all between its esp and this is committed
		std::atomic<bool> stopFlag{ false }, windowOver{ false }, reportReady{ false }, alive{ false };
		std::atomic<double> lastStep{ -1.0 }, firstStep{ -1.0 };
		double lastSysT = -1;
		double frames[MAXF]; std::atomic<int> nFrames{ 0 };
		MarkRec marks[MAXM]; std::atomic<int> nMarks{ 0 };
		std::vector<std::string> report;
		// the keyboard (the main thread only): where the focus went, the Alt-keys Windows beeps at
		HWND lastFocus = reinterpret_cast<HWND>(-1), lastFg = reinterpret_cast<HWND>(-1); int focusLines = 0, sysKeyLines = 0; long sysChars = 0, sysKeys = 0;

		// ---- after the window: the copies walked and summed ----
		HANDLE symProc = nullptr;
		const Sample* cur = nullptr; const unsigned char* curStk = nullptr;

		BOOL CALLBACK ReadMem(HANDLE, DWORD64 base, PVOID buf, DWORD size, LPDWORD got)
		{
			*got = 0;
			// (dbghelp passes the 32-bit addresses sign-extended: taken as 32 bits, the end in 64 so it cannot wrap)
			const DWORD a = static_cast<DWORD>(base);
			const DWORD64 lo = cur->esp, hi = lo + cur->len, end = static_cast<DWORD64>(a) + size;
			if (a >= lo && end <= hi) { std::memcpy(buf, curStk + (a - cur->esp), size); *got = size; return TRUE; }
			if (end > lo && a < cur->top) return FALSE;   // the live stack moved on since the copy
			SIZE_T n = 0; const BOOL ok = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(a), buf, size, &n);
			*got = static_cast<DWORD>(n); return ok;
		}

		// one sample's walk (leaf first) under SEH: a bad read ends that sample's walk, never the game
		int WalkOne(const Sample& s, DWORD* out, int max)
		{
			int n = 0;
			__try
			{
				STACKFRAME64 f{}; f.AddrPC.Offset = s.eip; f.AddrFrame.Offset = s.ebp; f.AddrStack.Offset = s.esp;
				f.AddrPC.Mode = f.AddrFrame.Mode = f.AddrStack.Mode = AddrModeFlat;
				CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER; c.Eip = s.eip; c.Ebp = s.ebp; c.Esp = s.esp;
				while (n < max && StackWalk64(IMAGE_FILE_MACHINE_I386, symProc, mainThread, &f, &c, ReadMem, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
					&& f.AddrPC.Offset)
					out[n++] = static_cast<DWORD>(f.AddrPC.Offset);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			return n;
		}

		// the main thread's stack copied while it stands, under SEH (no allocation, no lock): -> bytes copied
		DWORD CopyStack(void* dst, DWORD from, DWORD len)
		{
			__try { std::memcpy(dst, reinterpret_cast<const void*>(from), len); return len; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
		}

		// the process's modules, taken once before the walks (a sorted table: an address's module by a binary search)
		struct Mod { DWORD lo, hi; std::string name; };
		std::vector<Mod> mods;
		void LoadModules()
		{
			mods.clear();
			HMODULE hm[1024]; DWORD need = 0;
			if (!EnumProcessModules(GetCurrentProcess(), hm, sizeof hm, &need)) return;
			for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i)
			{
				MODULEINFO mi{}; char nm[MAX_PATH] = {};
				if (!GetModuleInformation(GetCurrentProcess(), hm[i], &mi, sizeof mi)) continue;
				GetModuleBaseNameA(GetCurrentProcess(), hm[i], nm, MAX_PATH);
				const DWORD lo = reinterpret_cast<DWORD>(mi.lpBaseOfDll);
				mods.push_back({ lo, lo + mi.SizeOfImage, nm });
			}
			std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.lo < b.lo; });
		}
		const Mod* ModAt(DWORD a)
		{
			auto it = std::upper_bound(mods.begin(), mods.end(), a, [](DWORD x, const Mod& m) { return x < m.lo; });
			if (it == mods.begin()) return nullptr;
			--it; return a < it->hi ? &*it : nullptr;
		}
		std::string ModuleOf(DWORD a, DWORD* base = nullptr)
		{
			const Mod* m = ModAt(a);
			if (!m) return "?";
			if (base) *base = m->lo;
			return m->name;
		}

		std::map<DWORD, std::string> names;
		// a function by the module's PDB (a function's name: one entry per function); without a PDB the call site
		// (module+offset) - an export's name only when the address is right at it (system DLLs' entry points)
		const std::string& NameOf(DWORD a)
		{
			auto it = names.find(a);
			if (it != names.end()) return it->second;
			DWORD base = 0; std::string mod = ModuleOf(a, &base), r;
			alignas(8) char buf[sizeof(SYMBOL_INFO) + 512] = {};
			SYMBOL_INFO* si = reinterpret_cast<SYMBOL_INFO*>(buf); si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 511;
			IMAGEHLP_MODULE64 mi{}; mi.SizeOfStruct = sizeof mi;
			const bool pdb = SymGetModuleInfo64(symProc, a, &mi) && (mi.SymType == SymPdb || mi.SymType == SymDia || mi.SymType == SymCv);
			DWORD64 disp = 0;
			if (SymFromAddr(symProc, a, &disp, si) && (pdb || disp < 0x200)) r = mod + "!" + si->Name;
			else { char o[32]; std::snprintf(o, sizeof o, "+0x%lX", a - base); r = mod + o; }
			if (r.size() > 150) r.resize(150);
			return names.emplace(a, r).first->second;
		}

		bool SystemModule(const std::string& m)
		{
			static const char* sys[] = { "ntdll.dll", "KERNEL32.DLL", "KERNELBASE.dll", "ucrtbase.dll", "MSVCP140.dll", "VCRUNTIME140.dll",
				"win32u.dll", "USER32.dll", "GDI32.dll", "gdi32full.dll", "d3d9.dll", "combase.dll", "RPCRT4.dll", "msvcrt.dll",
				"wow64.dll", "wow64cpu.dll", "wow64win.dll", "dbghelp.dll", "apphelp.dll", "?" };
			for (const char* s : sys) if (_stricmp(m.c_str(), s) == 0) return true;
			return false;
		}

		bool LooksLikeReturn(DWORD a)   // the bytes before it are a call (E8 rel32, FF /2)
		{
			unsigned char b[8] = {}; SIZE_T n = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(a - 7), b, 7, &n) || n != 7) return false;
			if (b[2] == 0xE8) return true;                                                  // call rel32
			if (b[5] == 0xFF && (b[6] & 0x38) == 0x10) return true;                         // call r/m (2 bytes)
			if (b[4] == 0xFF && (b[5] & 0x38) == 0x10) return true;                         // + disp8 / sib
			if (b[1] == 0xFF && (b[2] & 0x38) == 0x10) return true;                         // + disp32
			if (b[0] == 0xFF && (b[1] & 0x38) == 0x10) return true;                         // + sib + disp32
			return false;
		}

		void Line(const char* fmt, ...)
		{
			char b[512]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
			report.emplace_back(b);
		}

		void LogLine(const char* fmt, ...)   // the main thread: one line straight to Orbiter.log, cut at 900 characters
		{
			char b[900]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
			oapiWriteLogV("%s", b);   // (as text: oapiWriteLog reads its line as a format - a "%" in it was read as one)
		}

		template <class K> std::vector<std::pair<K, int>> Top(const std::map<K, int>& m, size_t n)
		{
			std::vector<std::pair<K, int>> v(m.begin(), m.end());
			std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			if (v.size() > n) v.resize(n);
			return v;
		}

		struct Walked { std::vector<DWORD> fr; std::set<std::string> scanned; };
		std::vector<Walked> WalkAll(const std::vector<Sample>& S, const std::vector<unsigned char>& pool)
		{
			std::vector<Walked> w(S.size());
			for (size_t i = 0; i < S.size() && !stopFlag; ++i)
			{
				cur = &S[i]; curStk = pool.data() + i * STACK;
				DWORD fr[96]; const int nfr = WalkOne(S[i], fr, 96);
				w[i].fr.assign(fr, fr + nfr);
				for (DWORD o = 0; o + 4 <= S[i].len; o += 4)
				{
					DWORD a; std::memcpy(&a, curStk + o, 4);
					const Mod* m = a >= 0x10000 ? ModAt(a) : nullptr;
					if (m && LooksLikeReturn(a)) w[i].scanned.insert(m->name);
				}
			}
			return w;
		}

		// one set of samples summed: modules, leaves, whose code, functions (inclusive)
		void Summary(const char* what, const std::vector<Sample>& S, const std::vector<Walked>& W, int nFn)
		{
			const int N = static_cast<int>(S.size());
			if (!N) return;
			auto pct = [&](int n) { return 100.0 * n / N; };
			std::map<std::string, int> modW, modS, leaf, owner, incl;
			int depthSum = 0;
			for (int i = 0; i < N; ++i)
			{
				if (stopFlag) return;
				std::set<std::string> ms, fn;
				for (DWORD a : W[i].fr) { ms.insert(ModuleOf(a)); fn.insert(NameOf(a)); }
				for (auto& m : ms) ++modW[m];
				for (auto& m : W[i].scanned) ++modS[m];
				for (auto& n : fn) ++incl[n];
				leaf[W[i].fr.empty() ? NameOf(S[i].eip) : NameOf(W[i].fr[0])]++;
				for (DWORD a : W[i].fr) if (!SystemModule(ModuleOf(a))) { ++owner[NameOf(a)]; break; }
				depthSum += static_cast<int>(W[i].fr.size());
			}
			Line("OrbiterCrew: stall probe: === %s: %d samples, %.1f frames per walk ===", what, N, double(depthSum) / N);
			Line("OrbiterCrew: stall probe: modules on the stack (walked):");
			for (auto& [k, n] : Top(modW, 14)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", pct(n), k.c_str());
			Line("OrbiterCrew: stall probe: modules on the stack (scanned for return addresses):");
			for (auto& [k, n] : Top(modS, 14)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", pct(n), k.c_str());
			Line("OrbiterCrew: stall probe: where the main thread was (leaf):");
			for (auto& [k, n] : Top(leaf, 12)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", pct(n), k.c_str());
			Line("OrbiterCrew: stall probe: the first frame outside the system DLLs (whose code it was):");
			for (auto& [k, n] : Top(owner, 15)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", pct(n), k.c_str());
			Line("OrbiterCrew: stall probe: functions on the stack (inclusive):");
			for (auto& [k, n] : Top(incl, nFn)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", pct(n), k.c_str());
		}

		void Analyse(const std::vector<Sample>& S, const std::vector<unsigned char>& pool, const std::vector<Sample>& SN,
			const std::vector<unsigned char>& poolN, long long normal, long long dropped, double period)
		{
			const int nf = nFrames.load(std::memory_order_acquire), nm = nMarks.load(std::memory_order_acquire);
			Line("OrbiterCrew: stall probe: %d frames in %.0f s from the first step; %lld samples in normal frames (%d kept), %d in long ones (over %.0f ms without a crew step), %lld not kept; one sample per %.1f ms",
				nf, windowS, normal, static_cast<int>(SN.size()), static_cast<int>(S.size()), STALL * 1000, dropped, period * 1000);
			if (normal + static_cast<long long>(S.size()) + dropped > 0)
				Line("OrbiterCrew: stall probe: the main thread's time in long frames: %.1f%%", 100.0 * (S.size() + dropped) / double(normal + S.size() + dropped));
			// the frame times (between two crew steps): the spread and the long ones
			std::vector<double> ft; std::vector<std::pair<double, double>> lf;   // lengths; long: start, length
			for (int i = 1; i < nf; ++i)
			{
				const double d = frames[i] - frames[i - 1]; ft.push_back(d);
				if (d > 0.25) lf.push_back({ frames[i - 1], d });
			}
			if (!ft.empty())
			{
				std::vector<double> s = ft; std::sort(s.begin(), s.end());
				auto q = [&](double p) { return s[static_cast<size_t>(p * (s.size() - 1))] * 1000; };
				double sum = 0; int o50 = 0, o100 = 0; for (double d : ft) { sum += d; o50 += d > 0.05; o100 += d > 0.1; }
				Line("OrbiterCrew: stall probe: frame time: mean %.1f ms (%.0f fps), median %.1f, p95 %.1f, p99 %.1f, max %.0f ms; over 50 ms: %d, over 100 ms: %d",
					sum / ft.size() * 1000, ft.size() / sum, q(0.5), q(0.95), q(0.99), s.back() * 1000, o50, o100);
			}
			for (int k = 0; k < nm; ++k) Line("OrbiterCrew: stall probe: mark +%.2f s %s", marks[k].t, marks[k].what);
			Line("OrbiterCrew: stall probe: keyboard: %ld Alt-keys (WM_SYSKEYDOWN) and %ld WM_SYSCHAR reached the render window (Windows beeps at a WM_SYSCHAR)", sysKeys, sysChars);
			{
				auto v = lf; std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
				double sum = 0; for (auto& f : lf) sum += f.second;
				Line("OrbiterCrew: stall probe: %d frames over 0.25 s, %.1f s in all; the longest:", static_cast<int>(lf.size()), sum);
				for (size_t i = 0; i < v.size() && i < 8; ++i) Line("OrbiterCrew: stall probe:   +%.2f s  %.0f ms", v[i].first, v[i].second * 1000);
			}
			if (S.empty() && SN.empty()) return;

			HANDLE me = GetCurrentProcess();
			if (!DuplicateHandle(me, me, me, &symProc, 0, FALSE, DUPLICATE_SAME_ACCESS)) { Line("OrbiterCrew: stall probe: no process handle for dbghelp"); return; }
			char cwd[MAX_PATH] = {}; GetCurrentDirectoryA(MAX_PATH, cwd);
			const std::string path = std::string(cwd) + "\\Modules\\OrbiterCrew;" + cwd + "\\Modules\\Plugin;" + cwd + "\\Modules;" + cwd + "\\Modules\\Server;" + cwd;
			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
			LoadModules();
			const double ta = Now();
			if (!SymInitialize(symProc, path.c_str(), TRUE)) { Line("OrbiterCrew: stall probe: SymInitialize failed (%lu)", GetLastError()); CloseHandle(symProc); symProc = nullptr; return; }

			const std::vector<Walked> W = WalkAll(S, pool), WN = WalkAll(SN, poolN);
			if (stopFlag) { SymCleanup(symProc); CloseHandle(symProc); symProc = nullptr; return; }

			// the normal frames: where an ordinary frame's time goes (a fair pick of their samples)
			Summary("normal frames", SN, WN, 50);
			// the long frames: all of them together, then the longest one by one
			Summary("long frames (over 100 ms without a crew step)", S, W, 40);
			auto v = lf; std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
			if (v.size() > 4) v.resize(4);
			std::sort(v.begin(), v.end());
			const int N = static_cast<int>(S.size());
			for (auto& fr : v)
			{
				std::map<std::string, int> in, own; int n = 0;
				for (int i = 0; i < N && !stopFlag; ++i)
				{
					if (S[i].t < fr.first || S[i].t > fr.first + fr.second) continue;
					++n; std::set<std::string> fn;
					for (DWORD a : W[i].fr) fn.insert(NameOf(a));
					for (auto& x : fn) ++in[x];
					for (DWORD a : W[i].fr) if (!SystemModule(ModuleOf(a))) { ++own[NameOf(a)]; break; }
				}
				if (!n) continue;
				Line("OrbiterCrew: stall probe: the frame at +%.2f s, %.0f ms, %d samples - whose code:", fr.first, fr.second * 1000, n);
				for (auto& [k, c] : Top(own, 8)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", 100.0 * c / n, k.c_str());
				Line("OrbiterCrew: stall probe:  ... on the stack:");
				for (auto& [k, c] : Top(in, 24)) Line("OrbiterCrew: stall probe:   %5.1f%% %s", 100.0 * c / n, k.c_str());
			}
			Line("OrbiterCrew: stall probe: (the report took %.1f s)", Now() - ta);
			SymCleanup(symProc); CloseHandle(symProc); symProc = nullptr;
		}

		// the main thread stood still: its registers and stack copied (no allocation, no lock while it stands)
		bool Take(Sample& s, unsigned char* dst)
		{
			if (SuspendThread(mainThread) == static_cast<DWORD>(-1)) return false;
			alignas(16) CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
			if (GetThreadContext(mainThread, &c) && c.Esp < stackBase && stackBase - c.Esp < (64u << 20))   // on its own stack: all above esp is committed
			{
				s.top = stackBase;
				s.len = CopyStack(dst, c.Esp, (std::min)(static_cast<DWORD>(STACK), stackBase - c.Esp));
				if (s.len) { s.eip = c.Eip; s.ebp = c.Ebp; s.esp = c.Esp; }
			}
			ResumeThread(mainThread);
			return s.eip != 0;
		}

		DWORD WINAPI Body();
		DWORD WINAPI Run(LPVOID)
		{
			HMODULE self = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(&Run), &self);   // +1: the module stays while this runs
			Body();
			alive = false;
			if (self) FreeLibraryAndExitThread(self, 0);
			return 0;
		}
		DWORD WINAPI Body()
		{
			std::vector<Sample> S, SN; S.reserve(MAXS); SN.reserve(MAXN);
			std::vector<unsigned char> pool(MAXS * STACK), poolN(MAXN * STACK);
			long long normal = 0, dropped = 0; const double t0 = Now(); double tLast = 0, gaps = 0; long long nGaps = 0;
			unsigned rng = 2463534242u;
			while (!stopFlag)
			{
				Sleep(PERIOD_MS);
				const double first = firstStep.load(), now = Now();
				if (first < 0) { if (now - t0 > 300) break; continue; }   // the scenario still loading
				if (tLast > 0) { gaps += now - tLast; ++nGaps; } tLast = now;
				if (now - first > windowS) break;
				Sample s{}; s.t = now - first;
				if (now - lastStep.load() < STALL)
				{
					// a normal frame: the reservoir keeps a fair pick (each sample seen so far equally likely kept)
					++normal;
					size_t slot = SN.size();
					if (slot >= MAXN)
					{
						rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
						const unsigned long long j = rng % static_cast<unsigned long long>(normal);
						if (j >= MAXN) continue;
						slot = static_cast<size_t>(j);
					}
					if (!Take(s, poolN.data() + slot * STACK)) continue;
					if (slot == SN.size()) SN.push_back(s); else SN[slot] = s;
					continue;
				}
				if (S.size() >= MAXS) { ++dropped; continue; }
				if (Take(s, pool.data() + S.size() * STACK)) S.push_back(s);
			}
			windowOver = true;
			if (!stopFlag && firstStep.load() >= 0)
			{
				Analyse(S, pool, SN, poolN, normal, dropped, nGaps ? gaps / nGaps : PERIOD_MS / 1000.0);
				reportReady.store(true, std::memory_order_release);
			}
			return 0;
		}

		// a window's text without a message to it (GetWindowText would wait on another thread's window, a hung one too)
		std::string TextOf(HWND h, int max)
		{
			wchar_t w[256] = {}; const int n = InternalGetWindowText(h, w, (std::min)(max, 255));
			if (n <= 0) return std::string();
			char a[512] = {}; WideCharToMultiByte(CP_ACP, 0, w, n, a, sizeof a - 1, nullptr, nullptr);
			return a;
		}
		std::string ClassOf(HWND h)
		{
			if (!h) return "none";
			char c[64] = {}; GetClassNameA(h, c, sizeof c); const std::string t = TextOf(h, 64);
			return std::string(c) + (t.empty() ? std::string() : " \"" + t + "\"");
		}
		BOOL CALLBACK ChildText(HWND h, LPARAM l)
		{
			std::string& out = *reinterpret_cast<std::string*>(l);
			char c[48] = {}; GetClassNameA(h, c, sizeof c); const std::string t = TextOf(h, 160);
			if (!t.empty()) out += std::string(" [") + c + ": " + t + "]";
			return out.size() < 400;
		}
		// a window not of Orbiter's main thread: its process and thread, shown or not, where, its controls' text
		std::string WhoseWindow(HWND h)
		{
			if (!h) return "none";
			DWORD pid = 0; const DWORD tid = GetWindowThreadProcessId(h, &pid);
			char exe[MAX_PATH] = "?"; DWORD n = MAX_PATH;
			if (HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) { QueryFullProcessImageNameA(ph, 0, exe, &n); CloseHandle(ph); }
			const char* base = std::strrchr(exe, '\\');
			RECT r{}; GetWindowRect(h, &r);
			char b[256];
			std::snprintf(b, sizeof b, "process %s (pid %lu%s), thread %lu%s, %s, at %ld,%ld %ldx%ld; owner %p", base ? base + 1 : exe, pid,
				pid == GetCurrentProcessId() ? ", Orbiter itself" : "", tid, tid == GetCurrentThreadId() ? " (Orbiter's main)" : "",
				IsWindowVisible(h) ? "shown" : "hidden", r.left, r.top, r.right - r.left, r.bottom - r.top, static_cast<void*>(GetWindow(h, GW_OWNER)));
			std::string kids; EnumChildWindows(h, ChildText, reinterpret_cast<LPARAM>(&kids));
			return std::string(b) + (kids.empty() ? "" : "; text:" + kids);
		}
	}

	void Arm(double window)
	{
		if (worker || alive) return;
		LARGE_INTEGER f; QueryPerformanceFrequency(&f); qf = double(f.QuadPart);
		windowS = (std::max)(5.0, window);
		HANDLE me = GetCurrentProcess();
		if (!DuplicateHandle(me, GetCurrentThread(), me, &mainThread, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) return;
		stackBase = reinterpret_cast<DWORD>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
		stopFlag = false; windowOver = false; reportReady = false; lastStep = -1.0; firstStep = -1.0; lastSysT = -1; nFrames = 0; nMarks = 0;
		lastFocus = lastFg = reinterpret_cast<HWND>(-1); focusLines = sysKeyLines = 0; sysChars = sysKeys = 0;
		alive = true;
		worker = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
		if (!worker) alive = false;
		if (worker) oapiWriteLogV("OrbiterCrew: stall probe armed (StallProbe): the main thread sampled for %.0f s from the first step, the report then", windowS);
	}

	void Step()
	{
		if (!worker) return;
		const double now = Now();
		if (firstStep.load() < 0) { firstStep = now; oapiWriteLog(const_cast<char*>("OrbiterCrew: stall probe: the first step (+0.00 s)")); }
		lastStep = now;
		const double sysT = oapiGetSysTime();
		if (sysT != lastSysT && !windowOver)
		{
			lastSysT = sysT;
			const int n = nFrames.load(std::memory_order_relaxed);
			if (n < static_cast<int>(MAXF)) { frames[n] = now - firstStep.load(); nFrames.store(n + 1, std::memory_order_release); }
			// the keyboard focus (keys with no focus go to the active window as Alt-keys, and Windows beeps at them)
			const HWND fgw = GetForegroundWindow();
			if (fgw != lastFg && focusLines < 60)
			{
				lastFg = fgw; ++focusLines;
				LogLine("OrbiterCrew: stall probe: +%.2f s foreground -> %p %s: %s", now - firstStep.load(), static_cast<void*>(fgw), ClassOf(fgw).c_str(), WhoseWindow(fgw).c_str());
			}
			const HWND fo = GetFocus();
			if (fo != lastFocus && focusLines < 60)
			{
				++focusLines; lastFocus = fo;
				const HWND fg = GetForegroundWindow(), ac = GetActiveWindow();
				LogLine("OrbiterCrew: stall probe: +%.2f s keyboard focus -> %p %s | active %p %s | foreground %p %s", now - firstStep.load(),
					static_cast<void*>(fo), ClassOf(fo).c_str(), static_cast<void*>(ac), ClassOf(ac).c_str(), static_cast<void*>(fg), ClassOf(fg).c_str());
			}
		}
		if (reportReady.load(std::memory_order_acquire))
		{
			reportReady = false;
			for (const std::string& l : report) oapiWriteLogV("%s", l.c_str());   // (as text, never as a format)
			report.clear();
		}
	}

	void KeyMsg(void* wnd, unsigned msg, unsigned wp)
	{
		const HWND h = static_cast<HWND>(wnd);
		if (!worker || windowOver || (msg != WM_SYSKEYDOWN && msg != WM_SYSCHAR)) return;
		if (msg == WM_SYSCHAR) ++sysChars; else ++sysKeys;
		if (sysKeyLines >= 20) return;
		++sysKeyLines;
		const HWND fo = GetFocus();
		LogLine("OrbiterCrew: stall probe: +%.2f s %s 0x%02X ('%c') at %p - focus %p %s, Alt %s", Now() - firstStep.load(),
			msg == WM_SYSCHAR ? "WM_SYSCHAR" : "WM_SYSKEYDOWN", wp, (wp >= 32 && wp < 127) ? static_cast<char>(wp) : '?', static_cast<void*>(h),
			static_cast<void*>(fo), ClassOf(fo).c_str(), (GetKeyState(VK_MENU) & 0x8000) ? "held" : "up");
	}

	void Mark(const char* what)
	{
		if (!worker || windowOver) return;
		const int n = nMarks.load(std::memory_order_relaxed);
		if (n >= static_cast<int>(MAXM)) return;
		marks[n].t = firstStep.load() < 0 ? -1.0 : Now() - firstStep.load();
		std::snprintf(marks[n].what, sizeof marks[n].what, "%s", what);
		nMarks.store(n + 1, std::memory_order_release);
	}

	void Stop()
	{
		if (!worker) return;
		stopFlag = true;
		const bool done = WaitForSingleObject(worker, 3000) == WAIT_OBJECT_0;
		CloseHandle(worker); worker = nullptr;
		if (!done) return;   // (still in dbghelp: it ends by itself, its module reference keeps the code loaded)
		if (mainThread) { CloseHandle(mainThread); mainThread = nullptr; }
		report.clear(); names.clear(); mods.clear();
	}
#else
	void Arm(double) {}
	void Step() {}
	void KeyMsg(void*, unsigned, unsigned) {}
	void Mark(const char*) {}
	void Stop() {}
#endif
}

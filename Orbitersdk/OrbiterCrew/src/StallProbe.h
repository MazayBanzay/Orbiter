#pragma once
// The main-thread probe (2026-10-18: the start froze 5-10 s; on Io the frame rate fell and play lagged). Opt-in: with
// StallProbe = 1 (the first 40 s) or = N (N s of play) in the crew class cfg (Config\Vessels\OrbiterCrew\*.cfg; off by
// default, a diagnostic only) a sampler thread copies Orbiter's main thread's registers and stack every few ms: all of
// it in the long frames (over 100 ms without a crew step), a fair pick in the normal ones. After the window the copies
// are walked (dbghelp, the modules' own PDBs, no symbol server) and Orbiter.log gets the frame times and which modules
// and functions the normal and the long frames were spent in - whose code holds the frame, not a guess. Also logged:
// where the keyboard focus goes and the Alt-keys the render window gets (Windows beeps at a WM_SYSCHAR)
namespace ocrew::stallprobe
{
	void Arm(double window);       // the main thread, a crew vessel made: starts the sampler once per session (window s)
	void Step();                   // the main thread, each crew step: the heartbeat; prints the report when it is ready
	void KeyMsg(void* wnd, unsigned msg, unsigned wp);   // the render window's messages: the Alt-keys Windows beeps at
	void Mark(const char* what);   // the main thread: a named moment for the report (e.g. her visual made)
	void Stop();                   // ExitModule: the sampler thread ends before the module goes
}

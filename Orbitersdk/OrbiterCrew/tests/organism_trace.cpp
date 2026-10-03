// OrbiterCrew - organism trace: runs the Body (the person's organism) through fixed situations and prints its state.
// Used to prove that a restructuring of the organism changes nothing: record a golden trace before, compare after.
// Builds without Orbiter: tests\stub\Orbitersdk.h stands in for the SDK header. Build: tests\build_trace.cmd; run: organism_trace.exe > trace.txt
#include "../src/LifeSupport.h"   // with tests\stub first on the include path: no Orbiter needed
#include <cstdio>

using ocrew::Body;

static void Print(const char* scene, double t, const Body& b)
{
	std::printf("%-10s t=%8.0f st=%d res=%.6g core=%.6g wbal=%.6g met=%.6g pulse=%.6g br=%.6g wdef=%.6g gly=%.6g fat=%.6g fast=%.6g "
		"hurt=%.4g,%.4g,%.4g,%.4g inj=%.6g dose=%.6g aer=%.6g run=%.6g lift=%.6g warn=%s\n",
		scene, t, static_cast<int>(b.state), b.reserve, b.coreT, b.wbal, b.met, b.pulse, b.breath, b.waterDef, b.glycogen, b.fat,
		b.fastDays, b.hurt[0], b.hurt[1], b.hurt[2], b.hurt[3], b.injury, b.doseSv, b.AerobicMax(), b.RunLimit(), b.LiftCapacity(),
		b.warning.empty() ? "-" : b.warning.c_str());
}

// one step of the organism the way CrewMember drives it: Step, Irradiate, Sustain
struct In { double activity{}, ppO2{ 21.2 }, ppCO2{ 0.04 }, ambient{ 101.3 }, heat{}, dose{}, airT{ 293.15 }; bool suited{}, supplied{ true }; double* water{}; };
static void Run(const char* scene, Body& b, const In& in, double seconds, double dt, double every)
{
	double next = 0;
	for (double t = 0; t <= seconds; t += dt)
	{
		b.Step(dt, in.activity, in.ppO2, in.ppCO2, in.ambient, in.suited, in.heat);
		b.Irradiate(dt, in.dose);
		b.Sustain(dt, in.supplied, in.water, in.airT, in.suited);
		if (t >= next) { Print(scene, t, b); next += every; }
	}
}

int main()
{
	{ Body b; In in; Run("rest", b, in, 3600, 1, 600); }
	{ Body b; In in; in.activity = ocrew::LocomotionPower(62, 4.0, 9.81); Run("run", b, in, 600, 0.1, 60); }
	{ Body b; In in; in.ppO2 = 0; in.ppCO2 = 0; in.ambient = 0; Run("vacuum", b, in, 150, 0.1, 10); }
	{ Body b; In in; in.heat = 300; Run("heat", b, in, 7200, 1, 600); }
	{ Body b; In in; in.heat = -300; Run("cold", b, in, 7200, 1, 600); }
	{ Body b; In in; b.Impact(9); Print("impact9", 0, b); b.Impact(12); Print("impact12", 0, b); Run("impact", b, in, 86400, 10, 21600); }
	{ Body b; b.Impact(12, Body::ON_FEET); Print("feet12", 0, b); }
	{ Body b; b.Impact(9, Body::ON_BACK); Print("back9", 0, b); }
	{ Body b; b.Impact(7, Body::ON_HEAD); Print("head7", 0, b); }
	{ Body b; In in; in.supplied = false; Run("nowater", b, in, 5 * 86400, 10, 43200); }
	{ Body b; In in; in.supplied = false; double water = 1e9; in.water = &water; Run("nofood", b, in, 9 * 86400, 30, 86400); }
	{ Body b; In in; in.dose = 0.5; Run("rad", b, in, 24 * 3600, 1, 3600); }
	{ Body b; In in; in.ppO2 = 8; Run("hypoxia", b, in, 300, 0.1, 30); }
	{ Body b; In in; in.ppCO2 = 9; Run("co2", b, in, 600, 0.1, 60); }
	return 0;
}

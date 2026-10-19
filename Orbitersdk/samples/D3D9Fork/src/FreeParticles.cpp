// AtmoFX fork: free particles from module sources - see FreeParticles.h and shaders\JetEjecta.fx (the same sprites)
#include "FreeParticles.h"
#include "Scene.h"
#include "VPlanet.h"
#include "LiquidWake.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
	enum Kind { SOIL = 0, ICE = 1, FOG = 2, DROP = 3 };

	// ---------------------------------------------------------------- the modules' sources
	struct Source {
		OBJHANDLE hv; int id;
		VECTOR3 pos, vel;					// vessel frame: the source point, the mean velocity of what leaves it
		double cone, spread, kgps;			// half-angle [rad], speed scatter 0..1, mass rate of what is seen [kg/s]
		int kind, ground; double wet, life, jetJ;
		double lastT;
		double acc[8];						// fractional emission per grain class of its mixture
	};
	std::vector<Source> srcs;

	// ---------------------------------------------------------------- particles (each sprite: a little cloud of grains)
	struct Part {
		VECTOR3 p, v;						// planet frame (rotating): from the planet centre / relative to the ground [m, m/s]
		float d, rho;						// grain diameter [m], density [kg/m^3]
		float age, life;					// [s]
		float s0, a0;						// sprite size [m], opacity
		float alb[3];
		float seed;
		double Rg;							// the ground's radius where it left [m]
		bool settled, fog, hard;			// hard: a clod (a solid lump, its own size; drawn crisp)
		bool drop, liq, spot;				// a cluster of liquid drops; it falls into a liquid (not onto the ground); now a wet spot
		float mass;							// the mass the sprite stands for [kg]
		VECTOR3 gust; float gustT;
		float wakeU;						// the speed of the body whose wake it was born in (a vehicle's, a walker's) [m/s]
		VECTOR3 ug0; float tg;				// the gas puff that threw it (a sole's squeeze, a blast): its velocity, decay time [s]
	};
	std::vector<Part> parts;
	const size_t MAX_PARTS = 8000;
	OBJHANDLE hBody = nullptr;

	// d: grain diameter [m]; frac: mass share; s0: the puff's sprite half-size at the nominal sprite mass [m]; a0: its opacity
	// (ice, fog; the soil's is worked out from its grains' cross-section); hard: clods (d, s0, a0 then from each one's mass)
	struct GrainClass { float d, frac, s0, a0; bool hard; };
	// dry soil: sorted sand, a little silt (what a tread or a sole lifts of it)
	const GrainClass G_DRY[] = { { 5e-4f, 0.20f, 0.10f, 0, false }, { 2e-4f, 0.40f, 0.18f, 0, false }, { 1e-4f, 0.30f, 0.28f, 0, false },
								 { 3e-5f, 0.07f, 0.40f, 0, false }, { 1e-5f, 0.03f, 0.60f, 0, false } };
	// damp / wet soil: the liquid binds the fines - clods and wet sand, no dust
	const GrainClass G_WET[] = { { 0, 0.65f, 0, 0, true }, { 2e-4f, 0.35f, 0.18f, 0, false } };
	const GrainClass G_LIQ[] = { { 1e-5f, 1.00f, 0.9f, 0.10f, false } };
	// airless regolith: fine and loose (the Moon's: ~20 % under 20 um)
	const GrainClass G_REG[] = { { 2e-4f, 0.30f, 0.15f, 0, false }, { 1e-4f, 0.30f, 0.22f, 0, false }, { 3e-5f, 0.25f, 0.30f, 0, false },
								 { 1e-5f, 0.15f, 0.40f, 0, false } };
	const GrainClass G_ICEG[] = { { 2e-4f, 0.40f, 0.14f, 0, false }, { 5e-5f, 0.40f, 0.25f, 0, false }, { 1e-5f, 0.20f, 0.45f, 0, false } };
	// ice crystals frozen out of vented vapour: 10-100 um plates and columns
	const GrainClass G_XTAL[] = { { 1e-4f, 0.25f, 0.08f, 0.40f, false }, { 4e-5f, 0.45f, 0.14f, 0.25f, false }, { 1.5e-5f, 0.30f, 0.24f, 0.12f, false } };
	// condensate where a cold jet chills the air: micron droplets / frost, a thin white fog
	const GrainClass G_FOG[] = { { 4e-6f, 1.00f, 0.10f, 0.16f, false } };
	// liquid drops a wheel or a bow throws out of a sea (Titan's methane): a spray sheet torn into drops of 0.1-3 mm (each
	// one also no bigger than the gas lets it fly whole, below); s0: the cluster's half-size at the nominal mass, its drops
	// a thousandth of its volume as they leave
	const GrainClass G_DROP[] = { { 3e-3f, 0.30f, 0.21f, 0, false }, { 1e-3f, 0.40f, 0.21f, 0, false }, { 3e-4f, 0.25f, 0.21f, 0, false },
								  { 1e-4f, 0.05f, 0.21f, 0, false } };
	const double SIGMA_LIQ = 0.017, RHO_LIQ = 520.0;	// the liquid's surface tension [N/m], density [kg/m^3] (Titan's seas)
	// the nominal mass one sprite stands for [kg]; each class's sprites get heavier or lighter by its rate (below) and their
	// size follows (a puff by the square root - its cross-section, what is seen, stays its mass's; a clod by the cube root)
	const double M_SPRITE[4] = { 6e-3, 2e-4, 2e-5, 2e-2 };
	const double MAX_RATE = 160.0;			// sprites per second from one source, short-lived grains (by the class's mass share)
	const double CLASS_CAP = 30.0;			//   and at least this per class
	const double FINE_CAP = 5.0;			// long-lived fines (they hang a minute in a dense gas): per class
	const double MIN_CLASS = 20.0;			// a small source (a footfall's ~15 g): lighter sprites, so each class still shows
	const double M_CLOD_MAX = 0.3;			// a clod no heavier [kg] (~8 cm): more of them instead

	// free jets blowing through the dense gas: origin and axis (planet frame), sqrt(J / rho_gas)
	struct FreeJet { VECTOR3 o, ax; double k, cap; };
	std::vector<FreeJet> fjets;

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
	bool failed = false;
	unsigned int rngState = 0x2545f491u;
	float Rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (rngState & 0xFFFFFF) / 16777216.0f; }
	float Rnd(float a, float b) { return a + (b - a) * Rnd(); }

	struct Vtx { float x, y, z, u, v, r, g, b, a, seed; };
	const DWORD FVF = D3DFVF_XYZ | D3DFVF_TEX3 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE4(1) | D3DFVF_TEXCOORDSIZE1(2);
	std::vector<Vtx> vb;
	std::vector<int> order;

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\FreeParticles.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\FreeParticles.fxo" : "Modules\\D3D9Client\\FreeParticles.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX free particles: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLogV("%s", buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &noise))) {
			oapiWriteLog((char*)"AtmoFX free particles: Textures\\AtmoFX\\noise3d.dds missing"); fx->Release(); fx = nullptr; failed = true; return false;
		}
		oapiWriteLog((char*)"AtmoFX free particles: on");
		return true;
	}

	// the body's ground colours (dry material); the ground class and the kind pick the variant (as the ejecta's)
	void Albedo(const char* body, int kind, int ground, float* a)
	{
		float r = 0.18f, g = 0.17f, b = 0.16f;
		if (!_stricmp(body, "Titan")) { r = 0.78f; g = 0.80f; b = 0.90f; }			// lit by the orange sky light (AtmoFX_GroundLight)
		else if (!_stricmp(body, "Moon")) { r = 0.15f; g = 0.145f; b = 0.14f; }
		else if (!_stricmp(body, "Mars")) { r = 0.36f; g = 0.20f; b = 0.11f; }
		else if (!_stricmp(body, "Earth")) { r = 0.46f; g = 0.40f; b = 0.31f; }
		else if (!_stricmp(body, "Io")) { r = 0.42f; g = 0.38f; b = 0.18f; }			// sulphur-yellow frost and dust
		if (kind == SOIL && ground == 1) { r *= 0.6f; g *= 0.6f; b *= 0.6f; }			// wet clods are darker
		if (kind == SOIL && ground == 2) { r = 0.85f; g = 0.85f; b = 0.85f; }			// vapour of a liquid: white fog
		if (kind == SOIL && ground == 4) { r = 0.80f; g = 0.82f; b = 0.86f; }			// frost, ice chips
		if (kind == ICE) { r = 0.90f; g = 0.92f; b = 0.95f; }
		if (kind == FOG) { r = 0.86f; g = 0.87f; b = 0.88f; }
		if (kind == DROP) { r = 0.80f; g = 0.68f; b = 0.50f; }						// a clear brownish liquid, lit by the sky through it
		a[0] = r; a[1] = g; a[2] = b;
	}

	// gas dynamic viscosity, Sutherland's law for N2 (close enough for CO2 and air) [Pa s]
	double Mu(double T) { return 1.663e-5 * pow(T / 273.0, 1.5) * (273.0 + 107.0) / (T + 107.0); }

	// the free jets' gas velocity at a point (planet frame): a round turbulent jet of momentum flux J in a gas of density rho,
	// centreline u = 7.0 sqrt(J/rho) / x (= 6.2 u0 d0 / x), Gaussian across with the half-velocity radius 0.094 x
	VECTOR3 GasVel(const VECTOR3& p)
	{
		VECTOR3 u = _V(0, 0, 0);
		for (const FreeJet& j : fjets) {
			const VECTOR3 r = p - j.o;
			const double x = dotp(r, j.ax);
			if (x <= 0.01) continue;
			const VECTOR3 rr = r - j.ax * x;
			const double b = 0.094 * x, uc = std::min(7.0 * j.k / x, j.cap);
			u += j.ax * (uc * exp(-0.693 * dotp(rr, rr) / (b * b)));
		}
		return u;
	}

	// a direction within a cone about 'ax' (uniform over the cap)
	VECTOR3 InCone(const VECTOR3& ax, double cone)
	{
		VECTOR3 e1 = crossp(ax, fabs(ax.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(ax, e1);
		const double cz = 1.0 - Rnd() * (1.0 - cos(cone)), sz = sqrt(std::max(0.0, 1.0 - cz * cz)), az = Rnd(0.0f, 6.2831853f);
		return ax * cz + e1 * (sz * cos(az)) + e2 * (sz * sin(az));
	}
}

void AtmoFX_Emit(OBJHANDLE hVessel, int id, const double* pos, const double* vel, double cone, double spread, double kgps, int kind,
	int ground, double wet, double life, double jetJ)
{
	for (size_t i = 0; i < srcs.size(); i++) {
		if (srcs[i].hv == hVessel && srcs[i].id == id) {
			if (kgps <= 0.0) { srcs.erase(srcs.begin() + i); return; }
			Source& s = srcs[i];
			s.pos = _V(pos[0], pos[1], pos[2]); s.vel = _V(vel[0], vel[1], vel[2]);
			s.cone = cone; s.spread = spread; s.kgps = kgps; s.kind = kind; s.ground = ground; s.wet = wet; s.life = life; s.jetJ = jetJ;
			s.lastT = oapiGetSimTime();
			return;
		}
	}
	if (kgps <= 0.0) return;
	Source s = { hVessel, id, _V(pos[0], pos[1], pos[2]), _V(vel[0], vel[1], vel[2]), cone, spread, kgps, kind, ground, wet, life, jetJ,
		oapiGetSimTime(), { 0, 0, 0, 0, 0, 0, 0, 0 } };
	// a random start of each class's count: a short burst (a footfall's 0.15 s) gets its fraction of a sprite on average,
	// not none
	for (double& a : s.acc) a = Rnd();
	srcs.push_back(s);
}

static void RenderParticles(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	LARGE_INTEGER tc0, tcF; QueryPerformanceCounter(&tc0); QueryPerformanceFrequency(&tcF);
	static double logT = 0.0, cpuSum = 0.0; static int cpuN = 0;
	if (!scn) return;
	const double simt = oapiGetSimTime();
	double dt = oapiGetSimStep();
	if (dt < 0.0) dt = 0.0;

	// drop stale sources and deleted vessels
	for (size_t i = 0; i < srcs.size();) {
		if (!oapiIsVessel(srcs[i].hv) || simt - srcs[i].lastT > 0.5) { srcs.erase(srcs.begin() + i); continue; }
		i++;
	}
	if (srcs.empty() && parts.empty()) return;

	// the body: the surface reference of the first source, or the one the particles already belong to
	if (!srcs.empty()) {
		OBJHANDLE h = oapiGetVesselInterface(srcs[0].hv)->GetSurfaceRef();
		if (h != hBody) { parts.clear(); hBody = h; }
	}
	if (!hBody) return;
	MATRIX3 Rp; oapiGetRotationMatrix(hBody, &Rp);
	VECTOR3 pp; oapiGetGlobalPos(hBody, &pp);
	const double GM = oapiGetMass(hBody) * GGRAV;
	char body[64]; oapiGetObjectName(hBody, body, 64);

	// the gas at the sources: its density from Orbiter, its temperature by the gas law from Orbiter's pressure and density
	// (a body without an atmosphere model of its own, Titan, gets Orbiter's default 288 K from GetAtmTemperature)
	static double rhoG = 0.0, Tg = 0.0;
	if (!srcs.empty()) {
		VESSEL* v = oapiGetVesselInterface(srcs[0].hv);
		rhoG = v->GetAtmDensity(); Tg = v->GetAtmTemperature();
		const ATMCONST* ac = oapiGetPlanetAtmConstants(hBody);
		const double pPa = v->GetAtmPressure();
		if (ac && ac->R > 1.0 && rhoG > 1e-9 && pPa > 1.0) {
			const double tGas = pPa / (rhoG * ac->R);
			if (tGas > 2.0 && tGas < 3000.0) Tg = tGas;
		}
	}
	const bool dense = rhoG > 1e-3;
	const double mu = Mu(Tg > 20.0 ? Tg : 94.0);
	// the body's liquid (AtmoFX_Liquid, AtmoFX_LiquidLevel of its atm.cfg): its surface's radius, 0 without one
	vPlanet* vpl = (oapiGetObjectType(hBody) == OBJTP_PLANET) ? (vPlanet*)scn->GetVisObject(hBody) : nullptr;
	const double Rliq = vpl && vpl->Liquid().x > 0.0f ? oapiGetSize(hBody) + vpl->Liquid().w : 0.0;

	// ---------------------------------------------------------------- sources: their jets and what they emit
	// the pool full: the oldest go first (the haze that hung longest), so what the sources throw now is always there
	if (parts.size() > MAX_PARTS * 95 / 100) parts.erase(parts.begin(), parts.begin() + (parts.size() - MAX_PARTS * 85 / 100));
	fjets.clear();
	for (Source& s : srcs) {
		VESSEL* v = oapiGetVesselInterface(s.hv);
		VECTOR3 eg, vg, gsG; v->Local2Global(s.pos, eg); v->GetGlobalPos(vg);
		v->GetGroundspeedVector(FRAME_GLOBAL, gsG);								// the vessel over the ground (global axes)
		const double vmag = length(s.vel);
		VECTOR3 axG = _V(0, -1, 0);
		if (vmag > 1e-6) { v->GlobalRot(s.vel / vmag, axG); normalise(axG); }
		const VECTOR3 pl = tmul(Rp, eg - pp), axl = tmul(Rp, axG), vgl = tmul(Rp, gsG);
		if (dense && s.jetJ > 0.0) fjets.push_back({ pl, axl, sqrt(s.jetJ / rhoG), std::max(vmag, 5.0) });
		if (dt <= 0.0) continue;
		// the ground under the vessel: its radius there (each grain falls back onto the ground it left) - or the liquid's
		// surface over it, where the vessel stands in a sea
		const double Rground = length(vg - pp) - v->GetAltitude(ALTMODE_GROUND);
		const bool inLiq = Rliq > Rground;
		const double Rg = inLiq ? Rliq : Rground;

		// what leaves the source as grain classes and their mass shares: the soil by its ground and moisture - a damp ground's
		// wet share comes off as clods, and a gas puff (a footfall's squeeze) lifts only the loose dry part, never the
		// cohesive clods; the ice crystals; the condensate fog
		struct Mix { const GrainClass* g; double frac; } mix[8]; int nm = 0;
		auto add = [&](const GrainClass* g, int n, double share) {
			for (int c = 0; c < n && nm < 8; c++) if (share * g[c].frac > 0.0) mix[nm++] = { &g[c], share * g[c].frac };
		};
		const bool puff = s.kind == SOIL && s.life > 0.0;
		if (s.kind == ICE) add(G_XTAL, 3, 1.0);
		else if (s.kind == FOG) add(G_FOG, 1, 1.0);
		else if (s.kind == DROP) add(G_DROP, 4, 1.0);
		else switch (s.ground) {
			case 1: if (!puff) add(G_WET, 2, 1.0); break;
			case 2: add(G_LIQ, 1, 1.0); break;
			case 3: add(G_REG, 4, 1.0); break;
			case 4: add(G_ICEG, 3, 1.0); break;
			default: {
				const double cl = std::min(0.9, std::max(0.0, s.wet * 2.0));			// damp sand: its wet share sticks into clods
				add(G_DRY, 5, 1.0 - cl);
				if (!puff) add(G_WET, 2, cl);
				break;
			}
		}
		const int k = std::max(0, std::min(3, s.kind));
		const double rhoP = s.kind == DROP ? RHO_LIQ : s.kind == ICE ? 917.0 : s.kind == FOG ? 600.0 : (s.ground == 4 ? 920.0 : (!_stricmp(body, "Titan") ? 1200.0 : 2650.0));
		const double rhoClod = 0.65 * rhoP + (!_stricmp(body, "Titan") ? 160.0 : 350.0);	// grains + the liquid in their pores
		float alb[3], albW[3]; Albedo(body, s.kind, s.ground, alb); Albedo(body, SOIL, 1, albW);
		// the pool's load: the fuller, the heavier (and bigger) the new sprites - the same mass and cross-section in fewer
		// of them, so what hangs in the air is not cut short
		const double load = (double)parts.size() / MAX_PARTS;
		const double capK = std::min(1.0, std::max(0.15, (0.85 - load) / 0.35));
		for (int c = 0; c < nm; c++) {
			const GrainClass& G = *mix[c].g;
			const double kc = s.kgps * mix[c].frac;									// this class's mass rate [kg/s]
			const double want = kc / M_SPRITE[k];										// sprites/s at the nominal mass
			const bool longLived = !G.hard && G.d < 5e-5f && s.kind != FOG;
			const double cap = (longLived ? FINE_CAP : std::max(CLASS_CAP, MAX_RATE * mix[c].frac)) * capK;
			// (the floor for a small source too: the long-lived fines no more than their cap, all of it lower in a full pool)
			double rate = std::max(std::min(want, cap), std::min((longLived ? FINE_CAP : MIN_CLASS) * capK, want * 50.0));
			if (G.hard) rate = std::max(rate, kc / M_CLOD_MAX);
			if (rate <= 0.0) continue;
			const double m = kc / rate;												// the mass one sprite stands for [kg]
			s.acc[c] += rate * dt;
			while (s.acc[c] >= 1.0) {
				s.acc[c] -= 1.0;
				if (parts.size() >= MAX_PARTS) continue;
				Part P;
				const VECTOR3 dir = InCone(axl, s.cone);
				// fines leave faster from a wheel's tread or a vent than the heavy grains
				const double sp = vmag * (1.0 - s.spread * Rnd()) * (s.kind == SOIL && !G.hard ? std::min(1.3, pow(2e-4 / G.d, 0.15)) : 1.0);
				// a jet's condensate and crystals form a little down the jet (where it has taken in the air): born there,
				// in its flow; the rest at the source
				P.p = pl + _V(Rnd(-1, 1), Rnd(-1, 1), Rnd(-1, 1)) * 0.03 + (s.jetJ > 0.0 ? axl * Rnd(0.03f, 0.15f) : _V(0, 0, 0));
				P.v = vgl + dir * sp;
				// a gas puff that threw it carries the fines a little way, dying out over its time
				if (puff) { P.ug0 = dir * sp; P.tg = (float)s.life; } else { P.ug0 = _V(0, 0, 0); P.tg = 0.0f; }
				P.age = 0;
				P.hard = G.hard;
				if (G.hard) {
					// a clod: one lump of the sprite's mass, flying as itself (its own size in the drag)
					const double dc = std::min(0.08, cbrt(6.0 * m / (3.14159265 * rhoClod))) * Rnd(0.75f, 1.25f);
					P.d = (float)dc; P.rho = (float)rhoClod;
					P.s0 = (float)(0.62 * dc); P.a0 = 0.97f;
					P.life = 60.0f;														// till it lands (then 2 s)
					P.alb[0] = albW[0]; P.alb[1] = albW[1]; P.alb[2] = albW[2];
				}
				else {
					P.d = G.d; P.rho = (float)(s.kind == SOIL && s.ground == 2 ? 450.0 : rhoP);
					// a drop flies whole only while the gas's push does not tear it, We = rho_gas v^2 d / sigma < 12
					if (s.kind == DROP && dense) {
						const double vr2 = std::max(dotp(P.v, P.v), 1e-6);
						P.d = (float)std::max(5e-5, std::min((double)G.d, 12.0 * SIGMA_LIQ / (rhoG * vr2)) * Rnd(0.7f, 1.3f));
					}
					const double kq = m / M_SPRITE[k];											// this sprite's mass to the nominal
					P.s0 = (float)(G.s0 * sqrt(kq) * Rnd(0.7f, 1.3f));
					if (s.kind == SOIL || s.kind == DROP) {
						// the puff's opacity from its grains' cross-section, 1.5 m / (rho d), over the soft disc (~2 s^2); clear
						// drops pass most of the light on (refracted forward): ~0.6 of it
						const double tau = 1.5 * m / (P.rho * P.d) / (2.0 * P.s0 * P.s0) * (s.kind == DROP ? 0.6 : 1.0);
						P.a0 = (float)std::min(0.9, 1.0 - exp(-tau)) * Rnd(0.8f, 1.1f);
					}
					else P.a0 = G.a0 * Rnd(0.7f, 1.2f);
					if (s.kind == FOG) P.life = (float)(std::max(0.2, s.life) * Rnd(0.6f, 1.3f));				// it evaporates as it mixes
					else if (s.kind == ICE) P.life = (float)(s.life > 0.0 ? s.life * Rnd(0.7f, 1.3f) : Rnd(20.0f, 40.0f));
					else if (s.kind == DROP) P.life = 30.0f;											// till it lands (the air saturated: no evaporation)
					else P.life = s.ground == 2 ? Rnd(20.0f, 40.0f) : (G.d < 2e-5f ? Rnd(30.0f, 70.0f) : 40.0f);
					P.alb[0] = alb[0]; P.alb[1] = alb[1]; P.alb[2] = alb[2];
				}
				P.seed = Rnd(); P.settled = false; P.fog = s.kind == FOG || (s.kind == SOIL && s.ground == 2);
				P.drop = s.kind == DROP; P.liq = inLiq; P.spot = false; P.mass = (float)m;
				P.Rg = Rg; P.gust = _V(0, 0, 0); P.gustT = 0;
				// a body moving through a dense gas leaves a turbulent wake, eddies of ~0.3 its speed and its own size, dying out
				// in a few seconds behind it: they carry the fines up to its height (a dust trail stands as high as the truck)
				P.wakeU = dense ? (float)length(vgl) : 0.0f;									// (the gusts take 0.3 of it)
				parts.push_back(P);
			}
		}
	}

	// ---------------------------------------------------------------- flight: gravity + drag in the gas (and the jets)
	static double ripTok = 0.0;												// rings on the liquid: ~40 a second at most
	ripTok = std::min(40.0, ripTok + 40.0 * std::max(dt, 0.0));
	if (dt > 0.0) {
		int ns = (int)ceil(dt / 0.02); if (ns > 8) ns = 8;
		const double h = std::min(dt, 0.16) / ns;								// time acceleration: capped, the dust lags behind
		for (Part& P : parts) {
			P.age += (float)dt;
			if (P.settled) continue;
			for (int s = 0; s < ns; s++) {
				const double rl = length(P.p);
				const VECTOR3 up = P.p / rl;
				VECTOR3 a = up * (-GM / (rl * rl));
				if (dense) {
					P.gustT -= (float)h;
					VECTOR3 ug = GasVel(P.p);
					if (P.tg > 0.0f) ug += P.ug0 * exp(-P.age / P.tg);					// the puff that threw it, dying out
					if (P.gustT <= 0.0f) {
						const double ul = length(ug) + 0.05 + P.wakeU * exp(-P.age / 2.5);
						P.gust = _V(Rnd(-1, 1), Rnd(-1, 1), Rnd(-1, 1)) * (0.3 * ul);
						P.gustT = Rnd(0.2f, 0.6f);
					}
					ug += P.gust;
					const VECTOR3 rel = P.v - ug;
					const double Re = rhoG * length(rel) * P.d / mu;
					const double f = std::max(1.0 + 0.15 * pow(Re, 0.687), 0.01833 * Re);	// Schiller-Naumann; Newton's Cd 0.44 above Re ~1000 (drops, clods)
					const double tauP = P.rho * P.d * P.d / (18.0 * mu);
					P.v = ug + rel * exp(-h * f / tauP);								// exact for the drag step, stable for any grain
					a = a * (1.0 - rhoG / P.rho);										// buoyancy
				}
				P.v += a * h;
				P.p += P.v * h;
				if (P.Rg > 0.0 && length(P.p) < P.Rg) {								// down on the ground
					if (P.drop) {
						P.p += up * (P.Rg - length(P.p));
						P.settled = true;
						if (P.liq) {
							// into the liquid: capillary rings, a few of the cluster's drops (the client's rings are few)
							P.life = P.age;
							const int nr = (int)std::min(3.0, 1.0 + P.s0 * 4.0);
							for (int r = 0; r < nr && ripTok >= 1.0; r++, ripTok -= 1.0) {
								const VECTOR3 e1 = unit(crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0))), e2 = crossp(up, e1);
								const VECTOR3 at = P.p + (e1 * Rnd(-1, 1) + e2 * Rnd(-1, 1)) * (0.7 * P.s0);
								LiquidWake::Ripple(hBody, mul(Rp, at) + pp, std::max(1e-3, 1.5 * (double)P.d), std::min(4.0, 1.0 + 0.5 * P.d / 1e-3));
							}
						}
						else {
							// onto the ground: a wet spot - each drop wets about its own diameter's disc (6 / (rho d) m2 a kg),
							// over the cluster's spread; it soaks in over some seconds
							const double A = 6.0 * (double)P.mass / (RHO_LIQ * P.d);
							const double r = std::max(0.05, std::min(1.2, (double)P.s0 + 0.10 * P.age));
							P.spot = true; P.s0 = (float)r;
							P.a0 = (float)(0.45 * std::min(1.0, A / (3.14159265 * r * r)));
							P.life = P.age + Rnd(5.0f, 9.0f);
						}
						break;
					}
					if (!P.fog && !P.hard && P.d > 1e-3 && length(P.v) > 1.0) { P.v = (P.v - up * (2.0 * dotp(P.v, up))) * 0.3; P.p += up * (P.Rg - length(P.p)); }
					else { P.settled = true; P.p += up * (P.Rg - length(P.p)); P.life = std::min(P.life, P.age + (P.fog ? 0.3f : P.hard ? 2.0f : 1.5f)); break; }
				}
			}
		}
		parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Part& P) { return P.age > P.life; }), parts.end());
	}
	if (parts.empty() || !Init(dev)) return;

	// ---------------------------------------------------------------- draw: lit, sorted soft sprites (as the ejecta)
	const VECTOR3 cam = scn->GetCameraGPos();
	VECTOR3 sunG; oapiGetGlobalPos(oapiGetObjectByIndex(0), &sunG);
	vPlanet* vp = (oapiGetObjectType(hBody) == OBJTP_PLANET) ? (vPlanet*)scn->GetVisObject(hBody) : nullptr;
	const VECTOR3 pRef = mul(Rp, parts[0].p);
	FVECTOR4 sc = vp ? vp->SunLightColor(pRef, 1.0) : FVECTOR4(1, 1, 1, 1);
	const VECTOR3 sunDir = unit(sunG - (pRef + pp));
	const double ambK = std::min(0.8, std::max(0.06, rhoG / 6.0 + 0.06));
	const FVECTOR4 gl = vp ? vp->GroundLight() : FVECTOR4(0, 0, 0, 0);
	const float scMax = std::max(1e-3f, std::max(sc.r, std::max(sc.g, sc.b)));
	const double cosSun = dotp(unit(pRef), sunDir);
	const float day = (float)std::max(0.0, std::min(1.0, cosSun * 8.0 + 0.3));

	const D3DXMATRIX& V = *scn->GetViewMatrix();
	const VECTOR3 cx = _V(V._11, V._21, V._31), cy = _V(V._12, V._22, V._32);
	const size_t n = parts.size();
	order.resize(n);
	static std::vector<double> dist; dist.resize(n);
	static std::vector<VECTOR3> wpos; wpos.resize(n);
	for (size_t i = 0; i < n; i++) { wpos[i] = mul(Rp, parts[i].p) + pp - cam; dist[i] = length(wpos[i]); order[i] = (int)i; }
	std::sort(order.begin(), order.end(), [&](int a, int b) { return dist[a] > dist[b]; });
	vb.resize(n * 6);
	size_t nv = 0;
	for (int k : order) {
		const Part& P = parts[k];
		const double dd = dist[k];
		if (dd < 0.3 || dd > 4000.0) continue;
		const float t = P.age;
		float s, a;
		if (P.spot) {																// a wet spot: soaking in
			s = P.s0;
			a = P.a0 * std::max(0.0f, std::min(1.0f, (P.life - t) / 2.0f));
		}
		else if (P.hard) {															// a clod: its own size, solid
			s = P.s0;
			a = P.a0 * std::min(1.0f, t * 30.0f);
			if (P.settled) a *= std::max(0.0f, 1.0f - (t - (P.life - 2.0f)) / 2.0f);
		}
		else if (P.fog && P.life < 6.0f) {													// a short-lived fog: grows with the jet's mixing, evaporates
			s = std::min(P.s0 + (dense ? 0.30f : 0.6f) * t, 3.0f);
			const float x = t / std::max(P.life, 0.05f);
			a = P.a0 * std::min(1.0f, t * 20.0f) * std::max(0.0f, 1.0f - x * x);
		}
		else {
			s = std::min(P.s0 + (dense ? 0.10f * t : 0.02f * t) * (P.d < 5e-5f ? 2.0f : 1.0f), 4.0f);
			a = P.a0 * (1.0f - std::min(1.0f, std::max(0.0f, (t - (P.life - std::min(3.0f, 0.5f * P.life))) / std::min(3.0f, 0.5f * P.life))));
			a *= std::min(1.0f, t * 8.0f);
			if (P.settled) a *= std::max(0.0f, 1.0f - (t - (P.life - 1.5f)) / 1.5f);
			a *= (float)std::min(1.0, P.s0 / (s + 1e-3) * 1.5);
		}
		a *= (float)std::min(1.0, std::max(0.0, (dd - 0.5 * s) / (1.5 * s)));							// fade sprites at the camera
		if (a < 0.003f) continue;
		const VECTOR3 view = wpos[k] / dd;
		const double fw = std::max(0.0, dotp(view, sunDir));
		const double phase = P.hard ? 0.9 : 0.55 + (P.d < 5e-5f || P.drop ? 1.6 : 0.5) * pow(fw, 6.0);
		float L[3];
		if (gl.w > 0.0f) {
			const float dir = (float)(gl.w * std::max(0.0, cosSun) * phase);
			L[0] = gl.r + sc.r * dir; L[1] = gl.g + sc.g * dir; L[2] = gl.b + sc.b * dir;
		}
		else {
			const float kk = (float)(phase * (1.0 - ambK) * day + ambK) / scMax * std::min(1.0f, scMax * 4.0f);
			L[0] = sc.r * kk; L[1] = sc.g * kk; L[2] = sc.b * kk;
		}
		// a flat sprite cut by the ground shows a hard line along it: the drawn puff (or the clod) is lifted clear of it
		VECTOR3 c = wpos[k];
		VECTOR3 ux = cx * s, uy = cy * s;
		if (P.spot) {																// flat on the ground, a little over it
			const VECTOR3 upW = mul(Rp, P.p / length(P.p));
			VECTOR3 e1 = cx - upW * dotp(cx, upW);
			if (length(e1) < 1e-3) e1 = cy - upW * dotp(cy, upW);
			normalise(e1);
			ux = e1 * s; uy = crossp(upW, e1) * s;
			c += upW * 0.04;
		}
		else if (P.Rg > 0.0) {
			const double rl = length(P.p), lift = (P.hard ? 0.8 : 0.7) * s - (rl - P.Rg);
			if (lift > 0.0) c += mul(Rp, P.p / rl) * lift;
		}
		const VECTOR3 q0 = c - ux - uy, q1 = c + ux - uy, q2 = c - ux + uy, q3 = c + ux + uy;
		const float r = P.alb[0] * L[0], g = P.alb[1] * L[1], b = P.alb[2] * L[2];
		// the shader's look: a puff [0, 1), a clod [2, 3), drops [4, 5), a wet spot [6, 7)
		const float sd = P.seed + (P.spot ? 6.0f : P.drop ? 4.0f : P.hard ? 2.0f : 0.0f);
		const Vtx v0 = { (float)q0.x, (float)q0.y, (float)q0.z, 0, 0, r, g, b, a, sd };
		const Vtx v1 = { (float)q1.x, (float)q1.y, (float)q1.z, 1, 0, r, g, b, a, sd };
		const Vtx v2 = { (float)q2.x, (float)q2.y, (float)q2.z, 0, 1, r, g, b, a, sd };
		const Vtx v3 = { (float)q3.x, (float)q3.y, (float)q3.z, 1, 1, r, g, b, a, sd };
		vb[nv++] = v0; vb[nv++] = v1; vb[nv++] = v2; vb[nv++] = v2; vb[nv++] = v1; vb[nv++] = v3;
	}
	{	// cost log, every 5 s
		LARGE_INTEGER tc1; QueryPerformanceCounter(&tc1);
		cpuSum += (tc1.QuadPart - tc0.QuadPart) * 1000.0 / tcF.QuadPart; cpuN++;
		const double now = oapiGetSysTime();
		if (now - logT > 5.0) {
			oapiWriteLogV("AtmoFX free particles: %u sources, %u particles, %u sprites drawn, gas %.3f kg/m3 %.0f K, CPU %.2f ms per frame",
				(unsigned)srcs.size(), (unsigned)parts.size(), (unsigned)(nv / 6), rhoG, Tg, cpuSum / cpuN);
			logT = now; cpuSum = 0; cpuN = 0;
		}
	}
	if (!nv) return;

	fx->SetTechnique("Free");
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetTexture("tNoise", noise);
	fx->SetFloat("gTime", (float)fmod(simt, 1000.0));
	dev->SetFVF(FVF);
	UINT np = 0;
	fx->Begin(&np, 0);
	fx->BeginPass(0);
	const UINT maxTri = 20000;
	for (size_t i = 0; i < nv; i += maxTri * 3) {
		const UINT tri = (UINT)std::min((size_t)maxTri, (nv - i) / 3);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tri, &vb[i], sizeof(Vtx));
	}
	fx->EndPass();
	fx->End();
}

// ==================================================================== ground marks (module side: AtmoFXMark.h)
// The ruts, pits and wet ruts the modules lay: kept in the body's frame, lying on the drawn terrain (the client's own
// elevation of its tiles). Their depth is shown by the light - the wall facing the sun lighter, the far one darker, a berm of
// thrown soil along the edge - over the disturbed soil's own darkening (compacted, its liquid squeezed up: darker); a wet rut
// darker still, drying over its life. They fade out at the end of it; the oldest go first past the limit.
namespace {
	struct MarkV { VECTOR3 p; float sA, sL, tr, taper, u, v, cr, cg, cb, ca; };	// on the ground (body frame); slope across /
					// along; trough weight (1 at the bottom); end fade; metres (the grain); its shade, premultiplied, at full strength
	struct Mark {
		OBJHANDLE body, hv; int id, kind;
		VECTOR3 c, al;						// the centre (body frame), along (unit, level)
		double len, wid, dep, t0, life, built, shaded;	// built, shaded: system time
		float seed;
		bool dirty;
		std::vector<MarkV> v; std::vector<WORD> ix;
	};
	std::deque<Mark> marks;
	const size_t MAX_MARKS = 3000;
	const double MARK_RANGE = 300.0;		// drawn within [m] of the camera
	struct MVtx { float x, y, z, u, v, r, g, b, a; };
	const DWORD MFVF = D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE4(1);
	std::vector<MVtx> mvb;
	std::vector<WORD> mib;

	// the profile across a rut, or out from a pit's centre, at x (1: the trough's edge): z in units of the depth, dz/dx
	void Profile(double x, double& z, double& dz)
	{
		const double a = fabs(x), sg = x < 0.0 ? -1.0 : 1.0, k = 3.14159265 / 0.35;
		if (a <= 1.0) { z = -(1.0 - a * a); dz = 2.0 * x; }
		else if (a <= 1.35) { z = 0.35 * sin(k * (a - 1.0)); dz = sg * 0.35 * k * cos(k * (a - 1.0)); }
		else { z = 0.0; dz = 0.0; }
	}

	double Elev(vPlanet* vp, OBJHANDLE hB, double lng, double lat)
	{
		double e = 0.0;
		if (!vp || vp->GetElevation(lng, lat, &e) <= 0) e = oapiSurfaceElevation(hB, lng, lat);
		return e;
	}

	// the mark's grid on the terrain: a rut 9 columns across (out to the berms), rows every ~0.4 m along, 5 cm more at each
	// end fading there (the next segment overlaps); a pit rings out to its berm, 16 round
	void Build(Mark& m, vPlanet* vp)
	{
		m.v.clear(); m.ix.clear();
		const double R = oapiGetSize(m.body);
		const VECTOR3 up = unit(m.c), al = m.al, ac = crossp(up, al);
		auto put = [&](double a, double b, double sA, double sL, double tr, double tp) {
			const VECTOR3 q = m.c + al * a + ac * b;
			double lng, lat, rad; oapiLocalToEqu(m.body, q, &lng, &lat, &rad);
			MarkV mv;
			mv.p = unit(q) * (R + Elev(vp, m.body, lng, lat) + 0.04);
			mv.sA = (float)sA; mv.sL = (float)sL; mv.tr = (float)tr; mv.taper = (float)tp; mv.u = (float)a; mv.v = (float)b;
			m.v.push_back(mv);
		};
		if (m.kind == 1) {
			const double rr[5] = { 0.0, 0.35, 0.7, 1.0, 1.35 }, A = 0.5 * m.len, B = 0.5 * m.wid;
			const int ns = 16;
			for (int r = 0; r < 5; r++) {
				double z, dz; Profile(rr[r], z, dz);
				for (int s = 0; s < (r ? ns : 1); s++) {
					const double th = 6.2831853 * s / ns, ca = cos(th), sa = sin(th);
					const double rad = sqrt(A * A * ca * ca + B * B * sa * sa);	// the ellipse's radius that way
					const double sl = m.dep * dz / rad;								// the slope along the ray
					put(A * rr[r] * ca, B * rr[r] * sa, sl * B * sa / rad, sl * A * ca / rad, std::max(0.0, -z), 1.0);
				}
			}
			for (int s = 0; s < ns; s++) { m.ix.push_back(0); m.ix.push_back((WORD)(1 + s)); m.ix.push_back((WORD)(1 + (s + 1) % ns)); }
			for (int r = 1; r < 4; r++) {
				const int b0 = 1 + (r - 1) * ns, b1 = 1 + r * ns;
				for (int s = 0; s < ns; s++) {
					const WORD i0 = (WORD)(b0 + s), i1 = (WORD)(b0 + (s + 1) % ns), j0 = (WORD)(b1 + s), j1 = (WORD)(b1 + (s + 1) % ns);
					m.ix.push_back(i0); m.ix.push_back(j0); m.ix.push_back(j1);
					m.ix.push_back(i0); m.ix.push_back(j1); m.ix.push_back(i1);
				}
			}
		}
		else {
			const double cols[9] = { -1.35, -1.0, -0.6, -0.25, 0.0, 0.25, 0.6, 1.0, 1.35 }, ext = 0.05, L = m.len + 2.0 * ext;
			const int rows = 2 + (int)std::min(8.0, floor(L / 0.4));
			for (int r = 0; r < rows; r++) {
				const double a = -0.5 * L + L * r / (rows - 1);
				const double tp = std::min(1.0, (0.5 * L - fabs(a)) / (2.0 * ext));
				for (int c = 0; c < 9; c++) {
					double z, dz; Profile(cols[c], z, dz);
					put(a, cols[c] * 0.5 * m.wid, m.dep * dz / (0.5 * m.wid), 0.0, std::max(0.0, -z), tp);
				}
			}
			for (int r = 0; r + 1 < rows; r++)
				for (int c = 0; c < 8; c++) {
					const WORD i = (WORD)(r * 9 + c);
					m.ix.push_back(i); m.ix.push_back((WORD)(i + 9)); m.ix.push_back((WORD)(i + 10));
					m.ix.push_back(i); m.ix.push_back((WORD)(i + 10)); m.ix.push_back((WORD)(i + 1));
				}
		}
		m.dirty = false; m.built = oapiGetSysTime(); m.shaded = -1.0;
	}

	void RenderMarks(LPDIRECT3DDEVICE9 dev, Scene* scn)
	{
		if (marks.empty()) return;
		const double simt = oapiGetSimTime(), syst = oapiGetSysTime();
		while (marks.size() > MAX_MARKS) marks.pop_front();
		marks.erase(std::remove_if(marks.begin(), marks.end(), [&](const Mark& m) { return simt - m.t0 > m.life || simt < m.t0 - 1.0; }), marks.end());
		if (marks.empty()) return;
		OBJHANDLE hB = oapiCameraProxyGbody();
		if (!hB || oapiGetObjectType(hB) != OBJTP_PLANET) return;
		vPlanet* vp = (vPlanet*)scn->GetVisObject(hB);
		MATRIX3 Rp; oapiGetRotationMatrix(hB, &Rp);
		VECTOR3 pp; oapiGetGlobalPos(hB, &pp);
		const VECTOR3 cam = scn->GetCameraGPos(), camL = tmul(Rp, cam - pp);
		const double Rliq = vp && vp->Liquid().x > 0.0f ? oapiGetSize(hB) + vp->Liquid().w : 0.0;
		// the light: the sun's direction (the same over the marks), its colour at the ground, the sky's share
		VECTOR3 sunG; oapiGetGlobalPos(oapiGetObjectByIndex(0), &sunG);
		const VECTOR3 sunL = unit(tmul(Rp, sunG - pp));
		const VECTOR3 camGround = unit(camL) * oapiGetSize(hB);
		const FVECTOR4 sc = vp ? vp->SunLightColor(mul(Rp, camGround), 1.0) : FVECTOR4(1, 1, 1, 1);
		const FVECTOR4 gl = vp ? vp->GroundLight() : FVECTOR4(0, 0, 0, 0);
		const ATMCONST* ac = oapiGetPlanetAtmConstants(hB);
		const double ambK = std::min(0.8, std::max(0.06, (ac ? ac->rho0 : 0.0) / 6.0 + 0.06));
		const bool glow = gl.w > 0.0f;								// a hazy sky (Titan): mostly diffuse light, soft relief
		const double Edir = glow ? 0.2 : 1.0, Eamb = glow ? 1.0 : ambK;
		char name[64]; oapiGetObjectName(hB, name, 64);
		float alb[3]; Albedo(name, SOIL, 0, alb);

		// a mark's shade: the light on its relief and its soil (slow: the sun, the drying) - worked out every 2 s, and at once
		// for a new or rebuilt one; each frame only its fade and where it is from the camera
		auto shade = [&](Mark& m, double age) {
			const double wetK = m.kind == 2 ? 0.35 * std::max(0.0, 1.0 - age / m.life) : 0.0;
			const double darkK = 0.10 + 0.25 * std::min(1.0, m.dep / 0.08);
			const VECTOR3 up0 = unit(m.c), al = m.al, acr = crossp(up0, al);
			const double sunK = std::max(0.0, dotp(up0, sunL));
			// lighter: what the brighter wall reflects more (the ground's colour in its light)
			const double lr = alb[0] * (glow ? gl.r + 0.2 * sc.r * sunK : sc.r * (sunK + ambK));
			const double lg = alb[1] * (glow ? gl.g + 0.2 * sc.g * sunK : sc.g * (sunK + ambK));
			const double lb = alb[2] * (glow ? gl.b + 0.2 * sc.b * sunK : sc.b * (sunK + ambK));
			for (MarkV& q : m.v) {
				const VECTOR3 up = unit(q.p);
				const VECTOR3 n = unit(up - acr * q.sA - al * q.sL);
				const double i0 = Edir * std::max(0.0, dotp(up, sunL)) + Eamb, i1 = Edir * std::max(0.0, dotp(n, sunL)) + Eamb;
				double f = (i1 / std::max(i0, 1e-3)) * (1.0 - q.tr * (darkK + wetK));
				f = 1.0 + (f - 1.0) * q.taper;
				if (f < 1.0) { q.cr = q.cg = q.cb = 0.0f; q.ca = (float)std::min(0.9, 1.0 - f); }
				else { const double k = f - 1.0; q.cr = (float)(k * lr); q.cg = (float)(k * lg); q.cb = (float)(k * lb); q.ca = 0.0f; }
			}
			m.shaded = syst;
		};
		static double logT = 0.0, cpuSum = 0.0; static int cpuN = 0;
		LARGE_INTEGER tc0, tc1, tcF; QueryPerformanceCounter(&tc0); QueryPerformanceFrequency(&tcF);
		mvb.clear(); mib.clear();
		int rebuilt = 0, shadedN = 0; size_t nDrawn = 0, nVerts = 0;
		auto flush = [&]() {
			if (mib.empty()) return;
			dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, (UINT)mvb.size(), (UINT)(mib.size() / 3), mib.data(), D3DFMT_INDEX16, mvb.data(), sizeof(MVtx));
			nVerts += mvb.size();
			mvb.clear(); mib.clear();
		};
		bool begun = false;
		for (Mark& m : marks) {
			if (m.body != hB) continue;
			const double d = length(m.c - camL);
			if (d > MARK_RANGE) continue;
			if (Rliq > 0.0 && length(m.c) < Rliq - 0.05 && (m.v.empty() || length(m.v[0].p) < Rliq)) continue;	// under the liquid
			// built when new or changed; laid on the terrain again now and then (the drawn level follows the tiles' detail)
			if (m.dirty || m.v.empty() || (syst - m.built > 20.0 && rebuilt < 8)) { Build(m, vp); rebuilt++; }
			if (m.v.empty()) continue;
			const double age = simt - m.t0;
			if (m.shaded < 0.0 || (syst - m.shaded > 2.0 && shadedN < 60)) { shade(m, age); shadedN++; }
			if (!begun) {
				if (!Init(dev)) return;
				// an older Modules\D3D9Client\FreeParticles.fx has no "Mark": no marks then (not the sprites' shader on them)
				static bool noTech = false;
				if (noTech || !fx->GetTechniqueByName("Mark")) {
					if (!noTech) oapiWriteLog((char*)"AtmoFX ground marks: FreeParticles.fx has no technique Mark (an older shader file) - not drawn");
					noTech = true; return;
				}
				fx->SetTechnique("Mark");
				fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
				fx->SetTexture("tNoise", noise);
				dev->SetFVF(MFVF);
				UINT np = 0; fx->Begin(&np, 0); fx->BeginPass(0);
				begun = true;
			}
			const float fade = (float)(std::min(1.0, std::max(0.0, (m.life - age) / (0.2 * m.life))) * std::min(1.0, (MARK_RANGE - d) / 100.0));
			if (mvb.size() + m.v.size() > 60000) flush();
			const WORD base = (WORD)mvb.size();
			const float su = m.seed * 37.0f;
			for (const MarkV& q : m.v) {
				const VECTOR3 w = mul(Rp, q.p) + pp - cam;
				MVtx o = { (float)w.x, (float)w.y, (float)w.z, q.u + su, q.v, q.cr * fade, q.cg * fade, q.cb * fade, q.ca * fade };
				mvb.push_back(o);
			}
			for (WORD i : m.ix) mib.push_back((WORD)(base + i));
			nDrawn++;
		}
		if (begun) { flush(); fx->EndPass(); fx->End(); }
		QueryPerformanceCounter(&tc1);
		cpuSum += (tc1.QuadPart - tc0.QuadPart) * 1000.0 / tcF.QuadPart; cpuN++;
		if (syst - logT > 5.0) {
			oapiWriteLogV("AtmoFX ground marks: %u kept, %u drawn, %u vertices, CPU %.2f ms per frame",
				(unsigned)marks.size(), (unsigned)nDrawn, (unsigned)nVerts, cpuSum / std::max(1, cpuN));
			logT = syst; cpuSum = 0; cpuN = 0;
		}
	}
}

void AtmoFX_Mark(OBJHANDLE hVessel, int id, const double* pos, const double* dir, double len, double width, double depth, int kind, double life)
{
	if (!oapiIsVessel(hVessel) || len <= 0.0 || width <= 0.0 || life <= 0.0) return;
	VESSEL* v = oapiGetVesselInterface(hVessel);
	OBJHANDLE hB = v->GetSurfaceRef();
	if (!hB) return;
	VECTOR3 g, dG; v->Local2Global(_V(pos[0], pos[1], pos[2]), g); v->GlobalRot(_V(dir[0], dir[1], dir[2]), dG);
	MATRIX3 R; oapiGetRotationMatrix(hB, &R);
	VECTOR3 bp; oapiGetGlobalPos(hB, &bp);
	const VECTOR3 c = tmul(R, g - bp), up = unit(c);
	VECTOR3 al = tmul(R, dG); al = al - up * dotp(al, up);
	if (length(al) < 1e-6) al = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0));
	normalise(al);
	Mark* m = nullptr;
	if (id >= 0) for (Mark& k : marks) if (k.hv == hVessel && k.id == id) { m = &k; break; }
	const double now = oapiGetSimTime();
	if (!m) {
		marks.push_back(Mark());
		m = &marks.back();
		m->hv = hVessel; m->id = id; m->seed = Rnd(); m->dirty = true; m->built = 0.0;
	}
	else {
		// a pit growing as it digs: rebuilt only when it changed enough to see (its terrain is looked up again then)
		if (length(m->c - c) > 0.02 || fabs(m->dep - depth) > 0.005 || fabs(m->len - len) > 0.02 || fabs(m->wid - width) > 0.02 || m->kind != kind)
			m->dirty = true;
	}
	m->body = hB; m->kind = kind; m->c = c; m->al = al; m->len = len; m->wid = width; m->dep = std::max(0.0, depth);
	m->t0 = now; m->life = life;
	while (marks.size() > MAX_MARKS) marks.pop_front();
}

void FreeParticles::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn) return;
	RenderMarks(dev, scn);				// on the ground first, under what flies
	RenderParticles(dev, scn);
}

void FreeParticles::Release()
{
	if (noise) { noise->Release(); noise = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	srcs.clear(); parts.clear(); fjets.clear(); hBody = nullptr;
	marks.clear();
}

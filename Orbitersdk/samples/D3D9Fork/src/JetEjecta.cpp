// AtmoFX fork: ground ejecta under rocket jets - see JetEjecta.h and shaders\JetEjecta.fx
#include "JetEjecta.h"
#include "Scene.h"
#include "VPlanet.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
	// ---------------------------------------------------------------- the module's nozzles
	struct Jet {
		OBJHANDLE hv; int id;
		VECTOR3 pos, dir;					// vessel frame
		double thrust, ve, T, wet, rExit; int ground;
		double lastT;						// simulation time of the last refresh
		double acc[6];						// fractional emission per grain class
	};
	std::vector<Jet> jets;

	// ---------------------------------------------------------------- grains
	enum Kind { GRAIN = 0, FOG = 1 };
	struct Part {
		VECTOR3 p, v;						// planet frame (rotating), relative to the planet centre / to the ground [m, m/s]
		float d;							// grain diameter [m]
		float rho;							// grain density [kg/m^3]
		float age, life;					// [s]
		float s0, a0;						// sprite size [m], opacity
		float alb[3];						// albedo
		float seed;
		bool settled, fog;
		VECTOR3 gust; float gustT;			// turbulent velocity seen by the grain, and when it is renewed
	};
	std::vector<Part> parts;
	const size_t MAX_PARTS = 4000;
	OBJHANDLE hBody = nullptr;				// the body the particles belong to
	double groundR = 0.0;					// ground radius at the last impact (planet frame) [m]

	struct GrainClass { float d, frac, s0, a0; };
	// mass-weighted share of the sprites by ground; size and opacity of one sprite (a cloud of grains)
	const GrainClass G_DRY[] = { { 5e-4f, 0.12f, 0.10f, 0.55f }, { 2e-4f, 0.28f, 0.18f, 0.35f }, { 1e-4f, 0.25f, 0.28f, 0.22f },
								 { 3e-5f, 0.22f, 0.40f, 0.10f }, { 1e-5f, 0.13f, 0.60f, 0.04f } };
	const GrainClass G_WET[] = { { 3e-3f, 0.75f, 0.08f, 0.85f }, { 2e-4f, 0.17f, 0.18f, 0.25f }, { 1e-5f, 0.08f, 0.6f, 0.05f } };
	const GrainClass G_LIQ[] = { { 1e-5f, 1.00f, 0.9f, 0.10f } };
	const GrainClass G_REG[] = { { 2e-4f, 0.30f, 0.15f, 0.40f }, { 1e-4f, 0.30f, 0.22f, 0.25f }, { 3e-5f, 0.25f, 0.30f, 0.15f },
								 { 1e-5f, 0.15f, 0.40f, 0.10f } };
	const GrainClass G_ICE[] = { { 2e-4f, 0.40f, 0.14f, 0.35f }, { 5e-5f, 0.40f, 0.25f, 0.18f }, { 1e-5f, 0.20f, 0.45f, 0.08f } };

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
	bool failed = false;
	unsigned int rngState = 0x9e3779b9u;
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
		FILE* pre = fopen("Modules\\D3D9Client\\JetEjecta.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\JetEjecta.fxo" : "Modules\\D3D9Client\\JetEjecta.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX jet ejecta: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &noise))) {
			oapiWriteLog((char*)"AtmoFX jet ejecta: Textures\\AtmoFX\\noise3d.dds missing"); fx->Release(); fx = nullptr; failed = true; return false;
		}
		oapiWriteLog((char*)"AtmoFX jet ejecta: on");
		return true;
	}

	// ground colours by body (dry material); the module's ground type picks the variant
	void Albedo(const char* body, int ground, float* a)
	{
		float r = 0.18f, g = 0.17f, b = 0.16f;										// grey default
		if (!_stricmp(body, "Titan")) { r = 0.78f; g = 0.80f; b = 0.90f; }			// lit by the orange sky light (AtmoFX_GroundLight): sand-coloured
		else if (!_stricmp(body, "Moon")) { r = 0.15f; g = 0.145f; b = 0.14f; }		// regolith
		else if (!_stricmp(body, "Mars")) { r = 0.36f; g = 0.20f; b = 0.11f; }
		else if (!_stricmp(body, "Earth")) { r = 0.46f; g = 0.40f; b = 0.31f; }		// quartz sand
		if (ground == 1) { r *= 0.6f; g *= 0.6f; b *= 0.6f; }						// wet clods are darker
		if (ground == 2) { r = 0.85f; g = 0.85f; b = 0.85f; }						// condensed vapour: white fog
		if (ground == 4) { r = 0.80f; g = 0.82f; b = 0.86f; }						// frost
		a[0] = r; a[1] = g; a[2] = b;
	}

	// gas dynamic viscosity, Sutherland's law for N2 (Titan 94 K: 6.2e-6 Pa s; close enough for CO2 and air) [Pa s]
	double Mu(double T) { return 1.663e-5 * pow(T / 273.0, 1.5) * (273.0 + 107.0) / (T + 107.0); }

	// the wall jet's gas velocity at a point (planet frame), summed over the active jets of this body
	struct Impact { VECTOR3 hit, up; double r, uw; };
	std::vector<Impact> impacts;
	VECTOR3 GasVel(const VECTOR3& p)
	{
		VECTOR3 u = _V(0, 0, 0);
		for (const Impact& im : impacts) {
			const VECTOR3 rv = p - im.hit;
			const double z = dotp(rv, im.up);
			const VECTOR3 hz = rv - im.up * z;
			const double rh = length(hz);
			if (rh < 1e-3) { u += im.up * (0.3 * im.uw); continue; }
			const double rr = rh > im.r ? rh : im.r;
			const double ur = im.uw * (im.r / rr);										// radial wall jet ~ 1/r
			const double prof = exp(-(z > 0 ? z : 0) / (0.12 * rh + 0.15));				// thin near the ground
			u += hz * (ur * prof / rh);
			u += im.up * (0.12 * ur * prof);											// entrainment lifts the edge
		}
		return u;
	}
}

void AtmoFX_SetJet(OBJHANDLE hVessel, int id, const double* exitPos, const double* jetDir, double thrust, double ve, double exitT,
	int ground, double wet, double rExit)
{
	for (size_t i = 0; i < jets.size(); i++) {
		if (jets[i].hv == hVessel && jets[i].id == id) {
			if (thrust <= 0.0) { jets.erase(jets.begin() + i); return; }
			Jet& j = jets[i];
			j.pos = _V(exitPos[0], exitPos[1], exitPos[2]); j.dir = _V(jetDir[0], jetDir[1], jetDir[2]);
			j.thrust = thrust; j.ve = ve; j.T = exitT; j.ground = ground; j.wet = wet; j.rExit = rExit > 0.005 ? rExit : 0.005;
			j.lastT = oapiGetSimTime();
			return;
		}
	}
	if (thrust <= 0.0) return;
	Jet j = { hVessel, id, _V(exitPos[0], exitPos[1], exitPos[2]), _V(jetDir[0], jetDir[1], jetDir[2]), thrust, ve, exitT, wet,
		rExit > 0.005 ? rExit : 0.005, ground, oapiGetSimTime(), { 0, 0, 0, 0, 0, 0 } };
	jets.push_back(j);
}

void JetEjecta::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	LARGE_INTEGER tc0, tcF; QueryPerformanceCounter(&tc0); QueryPerformanceFrequency(&tcF);
	static double logT = 0.0, cpuSum = 0.0; static int cpuN = 0;
	if (!scn) return;
	const double simt = oapiGetSimTime();
	double dt = oapiGetSimStep();
	if (dt < 0.0) dt = 0.0;

	// drop stale nozzles and deleted vessels
	for (size_t i = 0; i < jets.size();) {
		if (!oapiIsVessel(jets[i].hv) || simt - jets[i].lastT > 0.5) { jets.erase(jets.begin() + i); continue; }
		i++;
	}
	if (jets.empty() && parts.empty()) return;

	// the body: the surface reference of the first active jet, or the one the particles already belong to
	if (!jets.empty()) {
		OBJHANDLE h = oapiGetVesselInterface(jets[0].hv)->GetSurfaceRef();
		if (h != hBody) { parts.clear(); hBody = h; groundR = 0.0; }
	}
	if (!hBody) return;
	MATRIX3 Rp; oapiGetRotationMatrix(hBody, &Rp);
	VECTOR3 pp; oapiGetGlobalPos(hBody, &pp);
	const double GM = oapiGetMass(hBody) * GGRAV;
	char body[64]; oapiGetObjectName(hBody, body, 64);

	// the gas at the jets (taken from the first vessel; all nozzles are close together)
	double rhoG = 0.0, Tg = 0.0;
	if (!jets.empty()) { VESSEL* v = oapiGetVesselInterface(jets[0].hv); rhoG = v->GetAtmDensity(); Tg = v->GetAtmTemperature(); }
	const bool dense = rhoG > 1e-3;
	const double mu = Mu(Tg > 20.0 ? Tg : 94.0);

	// ---------------------------------------------------------------- impingement and emission
	impacts.clear();
	for (Jet& j : jets) {
		VESSEL* v = oapiGetVesselInterface(j.hv);
		VECTOR3 eg, dg, vg; v->Local2Global(j.pos, eg); v->GlobalRot(j.dir, dg); v->GetGlobalPos(vg);
		normalise(dg);
		const VECTOR3 upg = unit(eg - pp);
		const double cosd = -dotp(dg, upg);										// 1 straight down
		if (cosd < 0.25) continue;
		double hExit = v->GetAltitude(ALTMODE_GROUND) + dotp(eg - vg, upg);
		if (hExit < 0.02) hExit = 0.02;
		const double t = hExit / cosd;
		const double tanT = tan((dense ? 12.0 : 30.0) * PI / 180.0);
		const double r = j.rExit + t * tanT;
		const double q = j.thrust * cosd / (PI * r * r);							// stagnation pressure on the ground [Pa]
		const double tau = 0.06 * q;												// wall-jet shear [Pa]
		const VECTOR3 hitG = eg + dg * t;
		Impact im;
		im.hit = tmul(Rp, hitG - pp); im.up = tmul(Rp, upg); im.r = r;
		im.uw = dense ? std::min(0.5 * sqrt(q / rhoG), 80.0) : 0.0;
		if (dense) impacts.push_back(im);

		// grains: threshold (Shao & Lu) for 100 um, cohesion raised by moisture
		const double g = GM / dotp(hitG - pp, hitG - pp);
		const double rhoP = j.ground == 4 ? 920.0 : (!_stricmp(body, "Titan") ? 1200.0 : 2650.0);
		const double dRef = 1e-4, gam = 3e-4 * (1.0 + 20.0 * j.wet);
		const double tauC = 0.0123 * (rhoP * g * dRef + gam / dRef);
		const double E = tau / tauC;
		if (E <= 1.0 || dt <= 0.0) continue;
		const double rate = 110.0 * std::min(log(E) + 0.3, 3.0);					// sprites per second for this nozzle

		const GrainClass* gc; int nc;
		switch (j.ground) {
		case 1: gc = G_WET; nc = 3; break;
		case 2: gc = G_LIQ; nc = 1; break;
		case 3: gc = G_REG; nc = 4; break;
		case 4: gc = G_ICE; nc = 3; break;
		default: gc = G_DRY; nc = 5; break;
		}
		float alb[3]; Albedo(body, j.ground, alb);
		// ground frame at the hit: tilt of the jet pushes the sheet downstream
		VECTOR3 e1 = crossp(im.up, fabs(im.up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(im.up, e1);
		const VECTOR3 dl = tmul(Rp, dg);
		const VECTOR3 tilt = dl - im.up * dotp(dl, im.up);
		for (int c = 0; c < nc; c++) {
			j.acc[c] += rate * gc[c].frac * dt;
			while (j.acc[c] >= 1.0) {
				j.acc[c] -= 1.0;
				if (parts.size() >= MAX_PARTS) continue;
				Part P;
				const float az = Rnd(0.0f, 6.2831853f);
				const VECTOR3 rad = e1 * cos(az) + e2 * sin(az);
				P.p = im.hit + rad * (r * Rnd(0.6f, 1.1f)) + im.up * 0.05;
				if (dense) {
					const double u = std::min(0.5 * sqrt(q / rhoG), 80.0);
					P.v = rad * (u * Rnd(0.3f, 1.0f)) + im.up * (u * Rnd(0.05f, 0.35f)) + tilt * (u * 0.5);
				}
				else {																// vacuum: ballistic sheet, fines fastest, a few degrees up
					const double sp = std::max(20.0, std::min(8.0 * sqrt(q), 600.0)) * Rnd(0.3f, 1.0f) * pow(1e-4 / gc[c].d, 0.3);
					const double el = Rnd(1.0f, 6.0f) * PI / 180.0;
					P.v = rad * (sp * cos(el)) + im.up * (sp * sin(el)) + tilt * (sp * 0.3);
				}
				P.d = gc[c].d; P.rho = (float)(j.ground == 2 ? 450.0 : rhoP);
				P.age = 0; P.life = j.ground == 2 ? Rnd(20.0f, 40.0f) : (gc[c].d < 2e-5f ? Rnd(30.0f, 70.0f) : 40.0f);
				P.s0 = gc[c].s0 * Rnd(0.7f, 1.3f); P.a0 = gc[c].a0 * Rnd(0.7f, 1.2f);
				P.alb[0] = alb[0]; P.alb[1] = alb[1]; P.alb[2] = alb[2];
				P.seed = Rnd(); P.settled = false; P.fog = (j.ground == 2);
				P.gust = _V(0, 0, 0); P.gustT = 0;
				parts.push_back(P);
			}
		}
	}

	// ---------------------------------------------------------------- flight: gravity + drag in the wall jet
	if (dt > 0.0) {
		int ns = (int)ceil(dt / 0.04); if (ns > 4) ns = 4;
		const double h = std::min(dt, 0.16) / ns;								// time acceleration: capped, the dust lags behind
		if (!impacts.empty()) groundR = length(impacts[0].hit);				// ground radius near the jets (kept after shutdown)
		const double Rg = groundR;
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
					if (P.gustT <= 0.0f) {
						const double ul = length(ug) + 0.05;
						P.gust = _V(Rnd(-1, 1), Rnd(-1, 1), Rnd(-1, 1)) * (0.3 * ul);
						P.gustT = Rnd(0.2f, 0.6f);
					}
					ug += P.gust;
					const VECTOR3 rel = P.v - ug;
					const double Re = rhoG * length(rel) * P.d / mu;
					const double f = 1.0 + 0.15 * pow(Re, 0.687);
					const double tauP = P.rho * P.d * P.d / (18.0 * mu);
					P.v = ug + rel * exp(-h * f / tauP);								// exact for the drag step, stable for any grain
					a = a * (1.0 - rhoG / P.rho);										// buoyancy
				}
				P.v += a * h;
				P.p += P.v * h;
				if (Rg > 0.0 && length(P.p) < Rg) {								// hit the ground
					if (!P.fog && P.d > 1e-3 && length(P.v) > 1.0) { P.v = (P.v - up * (2.0 * dotp(P.v, up))) * 0.3; P.p += up * (Rg - length(P.p)); }
					else { P.settled = true; P.life = std::min(P.life, P.age + 1.5f); break; }
				}
			}
		}
		parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Part& P) { return P.age > P.life; }), parts.end());
	}
	// ---------------------------------------------------------------- hydrogen afterburning in air (Earth only: O2)
	// the module's exhaust is hydrogen-rich; in air the excess H2 burns at the jet's mixing layer a few exit diameters
	// down the jet: a pale blue, nearly transparent flame (H2/O2 flames emit little visible light - seen against dark
	// backgrounds, lost in daylight). Additive sprites along the axis (alpha < 0 in the vertex = add, no cover).
	const VECTOR3 cam = scn->GetCameraGPos();
	struct Flame { VECTOR3 c; float s, I; };
	static std::vector<Flame> flames; flames.clear();
	if (!_stricmp(body, "Earth") && rhoG > 0.3) {
		for (const Jet& j : jets) {
			VESSEL* v = oapiGetVesselInterface(j.hv);
			VECTOR3 eg, dg; v->Local2Global(j.pos, eg); v->GlobalRot(j.dir, dg); normalise(dg);
			const double k = sqrt(j.thrust / 500.0);
			const double L = 0.25 + 1.2 * k, d0 = 3.0 * 2.0 * j.rExit;					// starts a few exit diameters out
			for (int i = 0; i < 10; i++) {
				const double f = (i + 0.5) / 10.0;
				const float fl = 0.85f + 0.3f * Rnd();										// flicker
				flames.push_back({ eg + dg * (d0 + L * f) - cam, (float)(j.rExit * (1.5 + 4.0 * f)), (float)(0.16 * k * sin(PI * f) * fl) });
			}
		}
	}
	if ((parts.empty() && flames.empty()) || !Init(dev)) return;

	// ---------------------------------------------------------------- draw: lit, sorted soft sprites
	VECTOR3 sunG; oapiGetGlobalPos(oapiGetObjectByIndex(0), &sunG);
	vPlanet* vp = (oapiGetObjectType(hBody) == OBJTP_PLANET) ? (vPlanet*)scn->GetVisObject(hBody) : nullptr;
	const VECTOR3 pRef = parts.empty() ? cam - pp : mul(Rp, parts[0].p);			// light taken once for the cloud
	FVECTOR4 sc = vp ? vp->SunLightColor(pRef, 1.0) : FVECTOR4(1, 1, 1, 1);
	const VECTOR3 sunDir = unit(sunG - (pRef + pp));
	const double ambK = std::min(0.8, std::max(0.06, rhoG / 6.0 + 0.06));		// sky light: Titan ~80 %, airless ~ none
	// the terrain's own light where the body defines it (AtmoFX_GroundLight: sky rgb + direct scale), so the dust matches the ground
	const FVECTOR4 gl = vp ? vp->GroundLight() : FVECTOR4(0, 0, 0, 0);
	const float scMax = std::max(1e-3f, std::max(sc.r, std::max(sc.g, sc.b)));
	const double cosSun = dotp(unit(pRef), sunDir);
	const float day = (float)std::max(0.0, std::min(1.0, cosSun * 8.0 + 0.3));

	const D3DXMATRIX& V = *scn->GetViewMatrix();
	const VECTOR3 cx = _V(V._11, V._21, V._31), cy = _V(V._12, V._22, V._32);	// camera right and up (world)
	const size_t n = parts.size();
	order.resize(n);
	static std::vector<double> dist; dist.resize(n);
	static std::vector<VECTOR3> wpos; wpos.resize(n);
	for (size_t i = 0; i < n; i++) { wpos[i] = mul(Rp, parts[i].p) + pp - cam; dist[i] = length(wpos[i]); order[i] = (int)i; }
	std::sort(order.begin(), order.end(), [&](int a, int b) { return dist[a] > dist[b]; });
	vb.resize(n * 6 + flames.size() * 6);
	size_t nv = 0;
	for (int k : order) {
		const Part& P = parts[k];
		const double dd = dist[k];
		if (dd < 0.3 || dd > 4000.0) continue;
		const float t = P.age;
		const float s = std::min(P.s0 + (dense ? 0.10f * t : 0.02f * t) * (P.d < 5e-5f ? 2.0f : 1.0f), 4.0f);	// the cloud spreads (capped)
		float a = P.a0 * (1.0f - std::min(1.0f, std::max(0.0f, (t - (P.life - 3.0f)) / 3.0f)));		// fade out at the end
		a *= std::min(1.0f, t * 8.0f);																// and in at the start
		if (P.settled) a *= std::max(0.0f, 1.0f - (t - (P.life - 1.5f)) / 1.5f);
		a *= (float)std::min(1.0, P.s0 / (s + 1e-3) * 1.5);											// spreading thins it
		a *= (float)std::min(1.0, std::max(0.0, (dd - 0.5 * s) / (1.5 * s)));							// fade sprites at the camera
		if (a < 0.003f) continue;
		// light: direct sun (forward scattering for the fines), sky ambient
		const VECTOR3 view = wpos[k] / dd;
		const double fw = std::max(0.0, dotp(view, sunDir));
		const double phase = 0.55 + (P.d < 5e-5f ? 1.6 : 0.5) * pow(fw, 6.0);
		float L[3];
		if (gl.w > 0.0f) {																			// sky light + the dimmed sun, as on the ground
			const float dir = (float)(gl.w * std::max(0.0, cosSun) * phase);
			L[0] = gl.r + sc.r * dir; L[1] = gl.g + sc.g * dir; L[2] = gl.b + sc.b * dir;
		}
		else {
			const float k = (float)(phase * (1.0 - ambK) * day + ambK) / scMax * std::min(1.0f, scMax * 4.0f);
			L[0] = sc.r * k; L[1] = sc.g * k; L[2] = sc.b * k;
		}
		const VECTOR3 c = wpos[k], ux = cx * s, uy = cy * s;
		const VECTOR3 q0 = c - ux - uy, q1 = c + ux - uy, q2 = c - ux + uy, q3 = c + ux + uy;
		const float r = P.alb[0] * L[0], g = P.alb[1] * L[1], b = P.alb[2] * L[2];
		const Vtx v0 = { (float)q0.x, (float)q0.y, (float)q0.z, 0, 0, r, g, b, a, P.seed };
		const Vtx v1 = { (float)q1.x, (float)q1.y, (float)q1.z, 1, 0, r, g, b, a, P.seed };
		const Vtx v2 = { (float)q2.x, (float)q2.y, (float)q2.z, 0, 1, r, g, b, a, P.seed };
		const Vtx v3 = { (float)q3.x, (float)q3.y, (float)q3.z, 1, 1, r, g, b, a, P.seed };
		vb[nv++] = v0; vb[nv++] = v1; vb[nv++] = v2; vb[nv++] = v2; vb[nv++] = v1; vb[nv++] = v3;
	}
	for (const Flame& F : flames) {
		const VECTOR3 ux = cx * F.s, uy = cy * F.s;
		const VECTOR3 q0 = F.c - ux - uy, q1 = F.c + ux - uy, q2 = F.c - ux + uy, q3 = F.c + ux + uy;
		const float r = 0.22f * F.I, g = 0.34f * F.I, b = 1.0f * F.I, a = -1.0f, sd = 0.37f;
		const Vtx v0 = { (float)q0.x, (float)q0.y, (float)q0.z, 0, 0, r, g, b, a, sd };
		const Vtx v1 = { (float)q1.x, (float)q1.y, (float)q1.z, 1, 0, r, g, b, a, sd };
		const Vtx v2 = { (float)q2.x, (float)q2.y, (float)q2.z, 0, 1, r, g, b, a, sd };
		const Vtx v3 = { (float)q3.x, (float)q3.y, (float)q3.z, 1, 1, r, g, b, a, sd };
		vb[nv++] = v0; vb[nv++] = v1; vb[nv++] = v2; vb[nv++] = v2; vb[nv++] = v1; vb[nv++] = v3;
	}
	{	// cost log (CPU: flight + sort + vertices), every 5 s
		LARGE_INTEGER tc1; QueryPerformanceCounter(&tc1);
		cpuSum += (tc1.QuadPart - tc0.QuadPart) * 1000.0 / tcF.QuadPart; cpuN++;
		const double now = oapiGetSysTime();
		if (now - logT > 5.0) {
			char buf[160]; sprintf_s(buf, "AtmoFX jet ejecta: %u particles, %u sprites drawn, CPU %.2f ms per frame", (unsigned)parts.size(), (unsigned)(nv / 6), cpuSum / cpuN);
			oapiWriteLog(buf); logT = now; cpuSum = 0; cpuN = 0;
		}
	}
	if (!nv) return;

	fx->SetTechnique("Ejecta");
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

void JetEjecta::Release()
{
	if (noise) { noise->Release(); noise = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	jets.clear(); parts.clear(); impacts.clear(); hBody = nullptr; groundR = 0.0;
}

// OrbiterCrew - the jet pack (see JetPack.h).
#include "JetPack.h"
#include "../../samples/AtmoFX/AtmoFXHeat.h"   // the AtmoFX client's heat shimmer (no-op without it)
#if __has_include("../../samples/AtmoFX/AtmoFXJet.h")
#include "../../samples/AtmoFX/AtmoFXJet.h"    // the AtmoFX client's ground ejecta under the jet (no-op without it)
#endif
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ocrew
{
	namespace
	{
		// exhaust: hot monopropellant products are all but invisible in vacuum, a white plume in air
		PARTICLESTREAMSPEC PodVac = { 0, 0.035, 40, 60, 0.10, 0.20, 2.5, 1.0, PARTICLESTREAMSPEC::EMISSIVE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.18, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC PodAir = { 0, 0.05, 60, 40, 0.12, 1.2, 1.6, 3.0, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.9, PARTICLESTREAMSPEC::ATM_PLOG, 20, 1e5, 0 };
		PARTICLESTREAMSPEC Dust = { 0, 0.35, 70, 3.5, 0.9, 1.8, 1.6, 0.2, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_LIN, 0, 0.55, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC Boost = { 0, 0.025, 60, 30, 0.10, 0.18, 0.8, 1.0, PARTICLESTREAMSPEC::EMISSIVE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.35, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC RcsVac = { 0, 0.006, 30, 30, 0.08, 0.10, 0.8, 1.5, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.3, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC RcsAir = { 0, 0.012, 40, 20, 0.10, 0.5, 1.0, 3.0, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.7, PARTICLESTREAMSPEC::ATM_PLOG, 20, 1e5, 0 };

		void RotY(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { c, 0, s, 0, 1, 0, -s, 0, c }; std::copy(M, M + 9, R); }
		void RotZ(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { c, -s, 0, s, c, 0, 0, 0, 1 }; std::copy(M, M + 9, R); }
		void RotX(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { 1, 0, 0, 0, c, -s, 0, s, c }; std::copy(M, M + 9, R); }
		void Mul(const double A[9], const double B[9], double C[9])
		{
			for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) C[r * 3 + c] = A[r * 3] * B[c] + A[r * 3 + 1] * B[3 + c] + A[r * 3 + 2] * B[6 + c];
		}
		void RotAxis(const VECTOR3& u, double a, double R[9])   // about the unit axis u by a (right hand, as RotX/Y/Z)
		{
			const double c = std::cos(a), s = std::sin(a), t = 1 - c, x = u.x, y = u.y, z = u.z;
			const double M[9] = { c + x * x * t, x * y * t - z * s, x * z * t + y * s, y * x * t + z * s, c + y * y * t, y * z * t - x * s,
				z * x * t - y * s, z * y * t + x * s, c + z * z * t };
			std::copy(M, M + 9, R);
		}
		VECTOR3 Apply(const double R[9], const VECTOR3& p) { return _V(R[0] * p.x + R[1] * p.y + R[2] * p.z, R[3] * p.x + R[4] * p.y + R[5] * p.z, R[6] * p.x + R[7] * p.y + R[8] * p.z); }
	}

	bool JetPack::LoadGeo()
	{
		std::ifstream f("Config\\Tantra\\JetPack.geo");
		if (!f) { oapiWriteLog(const_cast<char*>("OrbiterCrew: Config\\Tantra\\JetPack.geo missing, jet pack geometry guessed")); return false; }
		std::string line; int nh = 0, np = 0;
		while (std::getline(f, line))
		{
			std::istringstream ss(line); std::string k; ss >> k;
			if (k == "HINGE" && nh < 2) { ss >> hinge[nh].x >> hinge[nh].y >> hinge[nh].z; ++nh; }
			else if (k == "PIVOT" && np < 2) { ss >> pivot[np].x >> pivot[np].y >> pivot[np].z; ++np; }
			else if (k == "SHELL") ss >> shellC.x >> shellC.y >> shellC.z >> shellSize.x >> shellSize.y >> shellSize.z;
			else if (k == "EXITDY") ss >> exitDy;
		}
		// index 0 is the pod on +x (her right)
		if (pivot[0].x < pivot[1].x) { std::swap(pivot[0], pivot[1]); std::swap(hinge[0], hinge[1]); }
		return nh == 2 && np == 2;
	}

	// the impeller pack (build_jetpack_fan.py): its core, hinges, rings and which group of the worn mesh is which
	const JetPack::AirSpec& JetPack::Spec(int m)
	{
		static const AirSpec soty{ FAN_DRY, FAN_FUEL, FAN_D, FAN_FM, FAN_P, FAN_TMAX, FAN_BOOST, FAN_TILT, FAN_SIDE, { 0.36, 0.20, 0.22 },
			"OrbiterCrew\\JetPackFan", "Config\\Tantra\\JetPackFan.geo", "Tantra\\JetPackFanWorn" };
		static const AirSpec trefoil{ 74, 32, 0.394, 0.65, 150e3, 3000, 500, 35 * RAD, 15 * RAD, { 0.42, 0.22, 0.30 },
			"OrbiterCrew\\JetPackTrefoil", "Config\\Tantra\\JetPackTrefoil.geo", "Tantra\\JetPackTrefoilWorn" };
		static const AirSpec hexa{ HEXA_DRY, HEXA_FUEL, 0, 0, 0, 2 * POD_F, 0, 0, 0, { 0.17, 0.05, 0.16 },
			"OrbiterCrew\\JetPackHexa", "Config\\Tantra\\JetPackHexa.geo", "Tantra\\JetPackHexaWorn" };
		return m == TREFOIL ? trefoil : m == HEXA ? hexa : soty;
	}

	bool JetPack::LoadFanGeo(const char* file, FanGeo& fan)
	{
		std::ifstream f(file);
		if (!f) { oapiWriteLog(const_cast<char*>((std::string("OrbiterCrew: ") + file + " missing: no air pack mesh").c_str())); return false; }
		std::string line; int nh = 0, np = 0;
		fan.role.clear(); fan.side.clear(); fan.gC.clear(); fan.gA.clear(); fan.gDir.clear(); fan.kin = 1;
		while (std::getline(f, line))
		{
			std::istringstream ss(line); std::string k; ss >> k;
			if (k == "SHELL") ss >> fan.shellC.x >> fan.shellC.y >> fan.shellC.z >> fan.shellSize.x >> fan.shellSize.y >> fan.shellSize.z;
			else if (k == "HINGE" && nh < 2) { ss >> fan.hinge[nh].x >> fan.hinge[nh].y >> fan.hinge[nh].z; ++nh; }
			else if (k == "PIVOT" && np < 2) { ss >> fan.pivot[np].x >> fan.pivot[np].y >> fan.pivot[np].z; ++np; }
			else if (k == "ROTOR_Y") ss >> fan.rotorY;
			else if (k == "BOOST_Y") ss >> fan.boostY;
			else if (k == "FOLD") { double d = 75; ss >> d; fan.fold = d * RAD; }
			else if (k == "FOLD_AXIS") { std::string a; ss >> a; fan.foldZ = a == "Z" || a == "z"; }
			else if (k == "GLOW_MAT") ss >> fan.glowMat;
			else if (k == "FOLD_SHIFT") ss >> fan.foldShift.x >> fan.foldShift.y >> fan.foldShift.z;
			else if (k == "KIN") ss >> fan.kin;
			else if (k == "STROKE") ss >> fan.stroke;
			else if (k == "LIFT") ss >> fan.lift;
			else if (k == "SWING") { double d = 90; ss >> d; fan.swing = d * RAD; }
			else if (k == "GLOW_POD") ss >> fan.glowPod[0] >> fan.glowPod[1] >> fan.glowPod[2] >> fan.glowPod[3];
			else if (k == "FOLD_TWIST") { double d = 0; ss >> d; fan.twist = d * RAD; }
			else if (k == "FOLD_V") { double d = 0; ss >> fan.foldU.x >> fan.foldU.y >> fan.foldU.z >> d; fan.foldU = unit(fan.foldU); fan.foldA = d * RAD; }
			else if (k == "GROUP")
			{
				int gi = -1, sd = -1; std::string r; ss >> gi >> r >> sd;
				if (gi < 0) continue;
				if (static_cast<int>(fan.role.size()) <= gi)
				{
					fan.role.resize(gi + 1, 0); fan.side.resize(gi + 1, -1);
					fan.gC.resize(gi + 1, _V(0, 0, 0)); fan.gA.resize(gi + 1, _V(0, 1, 0)); fan.gDir.resize(gi + 1, 1);
				}
				fan.role[gi] = r == "boom" ? 1 : r == "ring" ? 2 : r == "rotor" ? 3 : r == "vane" ? 4 : r == "tele1" ? 5 : r == "tele2" ? 6 : 0; fan.side[gi] = sd;
				VECTOR3 gc, ga; int dir = 1;   // a rotor's or a vane's own centre and axis (KIN 2)
				if (ss >> gc.x >> gc.y >> gc.z >> ga.x >> ga.y >> ga.z) { fan.gC[gi] = gc; fan.gA[gi] = unit(ga); if (ss >> dir) fan.gDir[gi] = dir; }
			}
		}
		if (fan.pivot[0].x < fan.pivot[1].x) { std::swap(fan.pivot[0], fan.pivot[1]); std::swap(fan.hinge[0], fan.hinge[1]); }
		return fan.ok = nh == 2 && np == 2 && !fan.role.empty();
	}

	double JetPack::FanThrust(double r, double share, double p) const
	{
		if (r < 1e-4 || share <= 0) return 0.0;
		const double A = 0.25 * PI * S().D * S().D;
		double T = (std::min)(S().TMAX, std::pow(S().FM * S().P * share * std::sqrt(2 * r * A), 2.0 / 3.0));   // by the power
		if (p > 0)   // by the blade tips: the speed of sound from the gas itself (gamma ~1.35 for N2, CO2, air)
		{
			const double a = std::sqrt(1.35 * p / r), vt = FAN_TIP_M * a;
			T = (std::min)(T, FAN_CT * r * A * vt * vt);
		}
		return T;
	}

	double JetPack::FanPower(double T, double r) const
	{
		if (r < 1e-4) return FAN_IDLE;
		const double A = 0.25 * PI * S().D * S().D;
		return (std::max)(FAN_IDLE, std::pow((std::max)(0.0, T), 1.5) / (S().FM * std::sqrt(2 * r * A)));
	}

	void JetPack::PodStreams(bool on)
	{
		if (!v) return;
		for (int i = 0; i < 2; ++i)
		{
			const VECTOR3 exitPt = pivot[i] + _V(0, exitDy, 0);
			if (on && model != HEXA && !podVac[i] && pod[i]) podVac[i] = v->AddExhaustStream(pod[i], exitPt, &PodVac);
			else if ((!on || model == HEXA) && podVac[i]) { v->DelExhaustStream(podVac[i]); podVac[i] = nullptr; }
		}
		for (Steam& st : steam)
		{
			if (st.th != pod[0] && st.th != pod[1]) continue;
			if (on && steamOn && !st.h) st.h = v->AddExhaustStream(st.th, st.pos, st.spec);
			else if (!on && st.h) { v->DelExhaustStream(st.h); st.h = nullptr; }
		}
	}

	void JetPack::RcsStreams(bool on)
	{
		if (!v) return;
		for (size_t k = 0; k < rcsVac.size() && k < rcs.all.size() && k < rcsPos.size(); ++k)
		{
			if ((!on || model == HEXA) && rcsVac[k]) { v->DelExhaustStream(rcsVac[k]); rcsVac[k] = nullptr; }
			else if (on && model != HEXA && !rcsVac[k]) rcsVac[k] = v->AddExhaustStream(rcs.all[k], rcsPos[k], &RcsVac);
		}
		for (Steam& st : steam)
		{
			if (st.th == pod[0] || st.th == pod[1]) continue;
			if (!on && st.h) { v->DelExhaustStream(st.h); st.h = nullptr; }
			else if (on && steamOn && !st.h) st.h = v->AddExhaustStream(st.th, st.pos, st.spec);
		}
	}

	void JetPack::Setup(VESSEL4* vessel)
	{
		v = vessel;
		steam.clear(); rcs = {}; dust = nullptr; dustT = nullptr; rcsVac.clear(); rcsPos.clear();   // a new body: its parts are made anew
		if (!LoadGeo())
		{
			for (int i = 0; i < 2; ++i) { const double s = i ? -1 : 1; hinge[i] = _V(s * 0.204, 0.54, -0.18); pivot[i] = _V(s * 0.60, 0.575, -0.17); }
			shellC = _V(0, 0.29, -0.21); shellSize = _V(0.40, 0.475, 0.215);
		}
		prop = v->CreatePropellantResource(FUEL, 0);
		// the particle streams use Orbiter's own textures: the D3D9 client shapes the puff itself, custom ones drew squares
		for (int i = 0; i < 2; ++i)
		{
			// physics: at the pod's height and side, in the plane of the centre of mass (no pitch torque while upright)
			const VECTOR3 exitPt = pivot[i] + _V(0, exitDy, 0);
			// the vector steers her path, not her body: the frame and the small ports keep the body upright, so the
			// thrust is applied at the height of the centre of mass
			pod[i] = v->CreateThruster(_V(pivot[i].x, 0, 0), _V(0, 1, 0), POD_F, prop, ISP);
			podVac[i] = v->AddExhaustStream(pod[i], exitPt, &PodVac);
			steam.push_back({ pod[i], exitPt, &PodAir, v->AddExhaustStream(pod[i], exitPt, &PodAir) });
		}
		// RCS in the shell corners: same layout as the suit's own N2 unit, ten times the push
		const double PX = 0.22, PY = 0.25;
		const VECTOR3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		for (int ix = 0; ix < 2; ++ix)
			for (int iy = 0; iy < 2; ++iy)
				for (int k = 0; k < 6; ++k)
				{
					const double sx = ix ? 1 : -1, sy = iy ? 1 : -1;
					if ((k == 0 && ix == 1) || (k == 1 && ix == 0)) continue;   // sideways only from the outward faces
					THRUSTER_HANDLE t = v->CreateThruster(_V(sx * PX, sy * PY, 0), dirs[k], RCS_F, nullptr, ISP);
					const VECTOR3 corner = shellC + _V(sx * (shellSize.x / 2 - 0.03), sy * (shellSize.y / 2 - 0.04), 0);
					rcsVac.push_back(v->AddExhaustStream(t, corner - dirs[k] * 0.02, &RcsVac)); rcsPos.push_back(corner - dirs[k] * 0.02);
					steam.push_back({ t, corner - dirs[k] * 0.02, &RcsAir, v->AddExhaustStream(t, corner - dirs[k] * 0.02, &RcsAir) });
						rcs.th[ix][iy][k] = t; rcs.all.push_back(t);
					}
		// the impeller pack's boost plumes, out of the hubs (level: the boost's share; zero for the light pack)
		for (int i = 0; i < 2; ++i) { boostLvl[i] = 0; boostPs[i] = nullptr; }
		// the impeller pack's mesh: always on the body, shown only while that pack is worn (PoseFan)
		for (int k = 0; k < 3; ++k)
		{
			AirKit& kt = kits[k]; const AirSpec& sp = Spec(k == 2 ? HEXA : k ? TREFOIL : FAN);
			kt.vtx.clear(); kt.mesh = static_cast<UINT>(-1);
			if (!LoadFanGeo(sp.geo, kt.geo) || (kt.tpl = oapiLoadMeshGlobal(const_cast<char*>(sp.worn))) == nullptr) continue;
			for (DWORD gi = 0, n = oapiMeshGroupCount(kt.tpl); gi < n; ++gi)
			{
				const MESHGROUP* g = oapiMeshGroup(kt.tpl, gi);
				if (g && g->Vtx && g->nVtx) kt.vtx.emplace_back(g->Vtx, g->Vtx + g->nVtx); else kt.vtx.emplace_back();
			}
			kt.mesh = v->AddMesh(kt.tpl);
			v->SetMeshVisibilityMode(kt.mesh, MESHVIS_NEVER);
		}
		{
			static SURFHANDLE tx[4] = { oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2vac")), oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2air")),
				oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2fors")), oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2inert")) };
			static SURFHANDLE te[4] = { oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2vac_e")), oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2air_e")),
				oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2fors_e")), oapiRegisterExhaustTexture(const_cast<char*>("Tantra\\h2inert_e")) };
			// wsize is the strip's HALF-width (D3D9Effect::RenderExhaust draws it +-wsize); the textures start the jet at the
			// lip's 81 mm and spread it. We: the end-on glow's flare quad is +-7 We
			static const double L[4] = { 1.40, 0.80, 1.20, 0.35 }, Wd[4] = { 0.30, 0.08, 0.13, 0.07 }, We[4] = { 0.045, 0.012, 0.02, 0.012 };
			for (int i = 0; i < 2; ++i)
			{
				hxPos[i] = kits[2].geo.ok ? kits[2].geo.pivot[i] - _V(0, 0.137, 0) : pivot[i]; hxDir[i] = _V(0, 1, 0);
				for (int k = 0; k < 4; ++k)
				{
					hxLvl[i][k] = 0;
					EXHAUSTSPEC es{}; es.th = nullptr; es.level = &hxLvl[i][k]; es.lpos = &hxPos[i]; es.ldir = &hxDir[i];
					// the AtmoFX D3D9Client fork draws an exhaust flagged 0x100 as a ray-marched volume (the stock one: a ribbon)
					es.lsize = L[k]; es.wsize = Wd[k]; es.lofs = 0.0; es.modulate = k == 0 ? 0.08 : 0.10; es.tex = tx[k]; es.flags = 0x100;
					hxEx[i][k] = v->AddExhaust(&es);
					hxEndLvl[i][k] = 0; es.level = &hxEndLvl[i][k]; es.lsize = 0.01; es.wsize = We[k]; es.tex = te[k];
					hxEnd[i][k] = v->AddExhaust(&es);
				}
			}
		}
		for (int i = 0; i < 2; ++i)
		{
			boostExPos[i] = _V(kits[0].geo.pivot[i].x, kits[0].geo.boostY, kits[0].geo.pivot[i].z); boostExDir[i] = _V(0, -1, 0); boostExLvl[i] = 0;
			boostEx[i] = 0;   // (2026-10-16: the boost flame drawn as billboards over the rings - off until the new nozzles)
		}
	}

	// the ground class under her for the jet's ejecta: Config\OrbiterCrew\Ground\<body>.cfg/.bin (a raster of classes round
	// a site, made by the site's tools) when there is one, else by the body's air: none - regolith (3), thin - dry dust (0),
	// dense - dry sand (0) a little damp. The API's classes: 0 dry sand/dust, 1 wet ground, 2 liquid, 3 airless regolith, 4 ice
	namespace {
		struct GroundMap {
			std::string body; bool ok{}; double lat0{}, lon0{}, step{ 50 }, R{}; int W{}, H{}; std::vector<unsigned char> cls;
			void Load(const char* name, double rad)
			{
				body = name; ok = false; cls.clear(); R = rad;
				char path[256]; sprintf_s(path, "Config\\OrbiterCrew\\Ground\\%s.cfg", name);
				FILE* f = fopen(path, "r"); if (!f) return;
				char ln[256];
				while (fgets(ln, sizeof ln, f)) {
					double val;
					if (sscanf_s(ln, " CenterLat = %lf", &val) == 1) lat0 = val * RAD;
					else if (sscanf_s(ln, " CenterLon = %lf", &val) == 1) lon0 = val * RAD;
					else if (sscanf_s(ln, " StepM = %lf", &val) == 1) step = val;
					else if (sscanf_s(ln, " W = %lf", &val) == 1) W = int(val);
					else if (sscanf_s(ln, " H = %lf", &val) == 1) H = int(val);
				}
				fclose(f);
				sprintf_s(path, "Config\\OrbiterCrew\\Ground\\%s.bin", name);
				FILE* b = fopen(path, "rb"); if (!b || W <= 0 || H <= 0) { if (b) fclose(b); return; }
				cls.resize(size_t(W) * H); ok = fread(cls.data(), 1, cls.size(), b) == cls.size(); fclose(b);
			}
			int At(double lat, double lon) const   // -1: off the raster
			{
				if (!ok) return -1;
				double dl = lon - lon0; while (dl > PI) dl -= 2 * PI; while (dl < -PI) dl += 2 * PI;
				const double x = R * std::cos(lat0) * dl, y = R * (lat - lat0);
				const int c = int(std::floor(W / 2.0 + x / step)), r = int(std::floor(H / 2.0 - y / step));
				return c < 0 || r < 0 || c >= W || r >= H ? -1 : cls[size_t(r) * W + c];
			}
		} gmap;
	}

	void JetPack::GroundUnder(int& gnd, double& wet) const
	{
		gnd = 0; wet = 0.0;
		OBJHANDLE ref = v->GetSurfaceRef(); if (!ref) return;
		char nm[64] = {}; oapiGetObjectName(ref, nm, sizeof nm);
		if (gmap.body != nm) gmap.Load(nm, oapiGetSize(ref));
		double lon, lat, rad; v->GetEquPos(lon, lat, rad);
		const double rho = v->GetAtmDensity();
		switch (gmap.At(lat, lon)) {   // the site raster's classes: 0 sand, 1 cobbles, 2 ice bedrock, 3 wet margin, 4 liquid
		case 0: gnd = 0; wet = 0.25; return;   // Titan's sand: organic grains, a little damp - sticky, some dust
		case 1: gnd = 4; wet = 0.1; return;    // water-ice cobbles: chips, no dust
		case 2: gnd = 4; wet = 0.0; return;    // ice bedrock: frost blown off, little else
		case 3: gnd = 1; wet = 0.8; return;    // the wet margin: clods
		case 4: gnd = 2; wet = 1.0; return;    // liquid methane: vapour, mist
		default: break;
		}
		if (rho < 1e-4) { gnd = 3; wet = 0.0; }          // airless: regolith (the Moon, Io, ...)
		else if (rho < 0.05) { gnd = 0; wet = 0.0; }     // thin air: dry dust (Mars)
		else { gnd = 0; wet = 0.2; }                     // dense air without a raster: dry sand, a little damp
	}

	void JetPack::SetupDust(double feetY)
	{
		if (!dust) dust = v->AddParticleStream(&Dust, _V(0, feetY + 0.05, 0), _V(0, 1, 0), &dustLevel);
		// Titan: the jet's blast in 5.4 kg/m3 of nitrogen raises a low, slow ring of damp sand that hangs 8-15 s
		static PARTICLESTREAMSPEC DustTitan = { 0, 0.30, 25, 1.5, 0.8, 11.0, 0.15, 2.5, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_LIN, 0, 0.6, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		DustTitan.tex = oapiRegisterParticleTexture(const_cast<char*>("Tantra\\titan_dust"));
		// (dir (0, -1, 0): the client launches particles against dir - upward here, not into the ground; see CrewMember's step dust)
		if (!dustT) dustT = v->AddParticleStream(&DustTitan, _V(0, feetY + 0.05, 0), _V(0, -1, 0), &dustTLevel);
	}

	void JetPack::SetOxygen(bool o2)
	{
		if (o2 == steamOn) return;
		steamOn = o2;
		for (Steam& s : steam)
			if (Air() && (s.th == pod[0] || s.th == pod[1])) continue;   // the fans make no steam
			else if (o2 && !s.h) s.h = v->AddExhaustStream(s.th, s.pos, s.spec);
			else if (!o2 && s.h) { v->DelExhaustStream(s.h); s.h = nullptr; }
	}

	double JetPack::Fuel() const { return v && prop ? v->GetPropellantMass(prop) : kept; }

	void JetPack::Detach()
	{
		if (!v) return;
		if (!worn) kept = 0;   // the fuel was copied every step (no Orbiter calls while the body is being deleted)
		keptKnown = true;
		v = nullptr; prop = nullptr; pod[0] = pod[1] = nullptr; rcs = {}; steam.clear(); dust = nullptr; dustT = nullptr; podVac[0] = podVac[1] = nullptr;
		for (AirKit& kt : kits) { kt.mesh = static_cast<UINT>(-1); kt.vtx.clear(); }
	}

	void JetPack::Wear(double fuel, int m)
	{
		worn = true;
		model = m == FAN ? FAN : m == TREFOIL ? TREFOIL : m == HEXA ? HEXA : SHOULDER;
		v->SetPropellantMaxMass(prop, FuelMax());
		v->SetPropellantMass(prop, std::clamp(fuel, 0.0, FuelMax()));
		deploy = deployTarget = 0; cmd = 0; mode = MANUAL; manualDeploy = false; tilt[0] = tilt[1] = 0;
		outLvl[0] = outLvl[1] = 0;
		// the pack and the booms far out: slower to pitch, and much slower to turn (the fans' rings: 60 kg, 0.66 m out)
		v->SetPMI(Air() ? S().pmi : _V(0.17, 0.05, 0.16));
		for (int i = 0; i < 2; ++i)
		{
			const double x = Air() && fanG().ok ? fanG().pivot[i].x : pivot[i].x;
			v->SetThrusterRef(pod[i], _V(x, 0, 0));
			v->SetThrusterMax0(pod[i], Air() ? S().TMAX : POD_F);
			v->SetThrusterIsp(pod[i], ISP);
		}
		PodStreams(!Air());   // the fans blow air: no flame, no steam
		RcsStreams(!Air());   // and they steer with the vanes in their exits: no puffs at the ports
		for (int k = 0; k < 3; ++k)
			if (kits[k].mesh != static_cast<UINT>(-1)) v->SetMeshVisibilityMode(kits[k].mesh, HasKit() && K() == k ? MESHVIS_ALWAYS | MESHVIS_VC | MESHVIS_EXTPASS : MESHVIS_NEVER);
	}

	double JetPack::Remove()
	{
		const double f = Fuel();
		worn = false;
		v->SetPMI(_V(0.15, 0.03, 0.15));
		for (THRUSTER_HANDLE t : pod) v->SetThrusterLevel(t, 0);
		cmd = 0; outLvl[0] = outLvl[1] = 0; fanLvl[0] = fanLvl[1] = 0; boostLvl[0] = boostLvl[1] = 0;
		v->SetPropellantMass(prop, 0);
		mode = MANUAL;
		for (AirKit& kt : kits) if (kt.mesh != static_cast<UINT>(-1)) v->SetMeshVisibilityMode(kt.mesh, MESHVIS_NEVER);
		return f;
	}

	int JetPack::ChamberFault() const
	{
		int m = 0;
		if (model != HEXA) return 0;
		for (int i = 0; i < 2; ++i) for (int ch = 0; ch < 2; ++ch) { const int b = 2 * (2 * i + ch); if (chDamaged[i][ch]) m |= 1 << b; if (chHot[i][ch]) m |= 2 << b; }
		return m;
	}

	void JetPack::Throttle(double delta)
	{
		if (!worn) return;
		mode = MANUAL;
		cmd = std::clamp(cmd + delta, 0.0, 1.0);
	}

	double JetPack::DeltaV() const
	{
		const double m = v->GetMass(), p = Fuel();
		// the fans: the momentum per kilogram at a hover here (the turbine's fuel against the air they throw down)
		double isp = ISP;
		if (Air())   // at a hover here: the fans' part over the turbine's flow, the rest the boost's
		{
			const double T = 0.5 * m * 9.81, tf = (std::min)(T, pfFan), tr = T - tf;
			const double flow = (tf > 0 ? FanPower(tf, rho) / (FanEnergy()) : 0.0) + tr / ISP;
			if (flow > 0) isp = T / flow;
		}
		return p > 0 && m > p ? isp * std::log(m / (m - p)) : 0;
	}

	bool JetPack::Key(DWORD key, bool landed)
	{
		if (!worn) return false;
		switch (key)
		{
		case OAPI_KEY_J:
			assist = true;   // the height hold is the assistant's
			if (!surfaceMode) return true;
			if (mode == HOLD) mode = MANUAL;
			else { mode = HOLD; altTarget = landed ? 1.5 : (std::max)(1.0, tAlt); }
			return true;
		case OAPI_KEY_C: assist = true; mode = mode == DESCENT ? MANUAL : DESCENT; return true;
		case OAPI_KEY_INSERT: if (mode == HOLD) vTarget = (std::min)(vTarget + 0.5, 5.0); return true;
		case OAPI_KEY_DELETE: if (mode == HOLD) vTarget = (std::max)(vTarget - 0.5, -5.0); return true;
		case OAPI_KEY_G: manualDeploy = !manualDeploy; return true;
		}
		return false;
	}

	void JetPack::Update(double dt, bool landed, bool canFly, double g, double altFeet, const FlightInput& in0)
	{
		if (v && prop) kept = v->GetPropellantMass(prop);   // the pack's own record of its fuel, kept current
		if (!worn || dt <= 0) return;
		const FlightInput& in = in0;
		if (!canFly) mode = MANUAL;
		const bool free = !landed;
		surfaceMode = altFeet < 3000 && g > 0.05;
		if (!surfaceMode && mode == HOLD) mode = MANUAL;       // no height to hold far from any surface
		if (mode == VECTOR && !vecFresh) { mode = surfaceMode ? HOLD : MANUAL; altTarget = (std::max)(1.5, altFeet); spdSet = false; }
		const bool vecNow = mode == VECTOR; vecFresh = false;

		// ---- where is up, and how does she lean ----
		OBJHANDLE ref = v->GetSurfaceRef();
		VECTOR3 rpos, rG, uG, fG; v->GetRelativePos(ref, rpos);
		const VECTOR3 up = unit(rpos);
		v->GlobalRot(_V(1, 0, 0), rG); v->GlobalRot(_V(0, 1, 0), uG); v->GlobalRot(_V(0, 0, 1), fG);
		// how far from upright, as a rotation in her own frame: right at any angle (lying, head down), not only small leans
		MATRIX3 Rm; v->GetRotationMatrix(Rm);
		const VECTOR3 ax = crossp(uG, up); const double sn = length(ax), cs = dotp(uG, up);
		const double tiltAng = std::atan2(sn, cs);
		const VECTOR3 rb = sn > 1e-6 ? tmul(Rm, ax * (tiltAng / sn)) : (cs < 0 ? _V(-PI, 0, 0) : _V(0, 0, 0));   // head down: pitch over
		const double leanF = -rb.x;    // + leaning forward (rad, up to pi)
		const double leanR = rb.z;     // + leaning to her right
		double hdg = 0; oapiGetHeading(v->GetHandle(), &hdg);
		// the rates from her own angular velocity, not from differences of the lean and the heading: those wrap and
		// flip when she is head down (the heading turns over by 180 deg) - the damping then pushed the spin on instead of
		// stopping it. The signs of the body rates are learned while she is near upright (where the differences agree)
		VECTOR3 av; v->GetAngularVel(av);
		if (prevValid)
		{
			const double dF = leanF - prevLeanF, dR = leanR - prevLeanR;
			double dh = hdg - prevHdg; if (dh > PI) dh -= PI2; if (dh < -PI) dh += PI2;
			if (cs > 0.8 && dt > 0)
			{
				auto learn = [&](double diff, double body, double& sgn) { if (std::abs(diff / dt) > 0.15 && std::abs(body) > 0.15) sgn = diff * body > 0 ? 1 : -1; };
				learn(dF, av.x, sgnF); learn(dR, av.z, sgnR); learn(dh, av.y, sgnY);
			}
			const double k = (std::min)(1.0, dt / 0.06);
			leanFRate += (sgnF * av.x - leanFRate) * k;
			leanRRate += (sgnR * av.z - leanRRate) * k;
			yawRate += (sgnY * av.y - yawRate) * k;
		}
		prevLeanF = leanF; prevLeanR = leanR; prevHdg = hdg; prevValid = true;
		VECTOR3 hv; v->GetGroundspeedVector(FRAME_HORIZON, hv);
		if (!landed) lastVs = hv.y;
		rho = v->GetAtmDensity();
		if (Air())
		{
			// the fans by the air they have, the turbine by its heat (hot air starves it); the boost on top
			const double Ta = v->GetAtmTemperature();
			heatShare = rho > 1e-4 && Ta > FAN_TLIM ? std::clamp((FAN_TCUT - Ta) / (FAN_TCUT - FAN_TLIM), 0.0, 1.0) : 1.0;
			pfFan = FanThrust(rho, heatShare, v->GetAtmPressure());
			pf = pfFan + S().BOOST;
		}
		else pf = POD_F;
		forsazh = model == HEXA && in.boost && rho > 0.05;
		if (model == HEXA)
		{
			pf = forsazh ? 2 * POD_F : POD_F;
			double sc[2];
			for (int i = 0; i < 2; ++i) sc[i] = 0.5 * ((chDamaged[i][0] || chHot[i][0] ? 0 : 1) + (chDamaged[i][1] || chHot[i][1] ? 0 : 1));
			const double both = sc[0] > 0 && sc[1] > 0 ? (std::min)(sc[0], sc[1]) : -1.0;
			for (int i = 0; i < 2; ++i) { v->SetThrusterMax0(pod[i], pf * (both >= 0 ? both : sc[i]) + 1e-3); v->SetThrusterIsp(pod[i], forsazh ? ISP / 2 : ISP); }
		}
		const double tmax = (std::max)(2 * pf, 1.0), m = v->GetMass();   // (no air for the fans: nothing to divide by)

		// ---- thrust vector limits: 30 deg, 75 with Shift ("full vector"); the height hold keeps enough lift ----
		if (braking && (mode != HOLD || in.pitch != 0 || in.boost)) braking = false;   // the pilot steers again
		double maxTilt = in.boost ? 75 * RAD : mode == DESCENT || braking ? 55 * RAD : 30 * RAD;
		if (Air()) maxTilt = (std::min)(maxTilt, S().TILT);   // the rings tilt no further
		tLimited = false;
		if (mode != MANUAL && surfaceMode)
		{
			const double c = tmax > 1 ? m * g / (0.95 * tmax) : 2.0;
			const double lim = c < 1 ? std::acos(c) : 0.0;
			if (lim < maxTilt) { maxTilt = lim; tLimited = true; }
		}

		// ---- throttle: by hand (numpad 0 / .), or the height hold, or the landing autopilot ----
		// the lift is set as a force along the local vertical (the pods' vectors are built from forces below)
		const double cosAll = 1.0;
		if (assist && !in.boost && surfaceMode && in.vertical != 0 && mode != HOLD && canFly && !(landed && in.vertical < 0))
		{
			mode = HOLD; spdSet = false; altTarget = landed ? (Air() ? 1.5 : 0.3) : (std::max)(0.5, altFeet);   // Space lifts off from the ground
		}
		if (mode == HOLD && !in.boost) altTarget = (std::max)(0.5, altTarget + 1.5 * in.vertical * dt);
		if (mode == HOLD && altTarget <= 0.55 && altFeet < 0.8 && in.vertical <= 0) mode = DESCENT;   // held at the bottom, near the ground: touch down
		if (vecNow) cmd = std::clamp(m * length(vecA) / tmax, 0.0, 1.0);   // the autopilot sets the whole vector
		else if (mode == MANUAL) cmd = std::clamp(cmd + (0.5 * in.throttle + (assist || in.boost ? 0.0 : 0.8 * in.vertical)) * dt * (fine ? 0.25 : 1.0), 0.0, 1.0);
		else if (mode == HOLD)
		{
			altTarget = (std::max)(0.5, altTarget + 1.5 * in.throttle * dt);       // numpad 0 / . move the held height
			// the ground ahead rising: hold a little higher over it
			tTerrain = false;
			if (free)
			{
				double lng, lat, rad; OBJHANDLE body = v->GetEquPos(lng, lat, rad);
				const double R = oapiGetSize(body), here = oapiSurfaceElevation(body, lng, lat);
				for (double t : { 1.0, 2.0 })
				{
					const double la = lat + hv.z * t / R, lo = lng + hv.x * t / (R * (std::max)(0.05, std::cos(lat)));
					const double rise = oapiSurfaceElevation(body, lo, la) - here;
					if (rise + 1.5 > altTarget) { altTarget = rise + 1.5; tTerrain = true; }
				}
			}
			const double aCmd = std::clamp(1.5 * (altTarget - altFeet) - 2.0 * hv.y, -3.0, 4.0);
			cmd = std::clamp(m * (g + aCmd) / (tmax * cosAll), -1.0, 1.0);   // negative: the jets turned up, pushing her down
		}
		else   // DESCENT: come down fast and brake late - a stopping profile, not a crawl
		{
			// the sink the pods can still stop by 1 m over the ground, braking at half of what the landing limit gives
			// (2.2 weights: 1.2 g net); at most 40 m/s; the last metre at 0.6 m/s
			const double drift = std::hypot(hv.x, hv.z), aV = 0.5 * 1.2 * (std::max)(0.5, g);
			double vt = altFeet > 1.0 ? -(std::min)(40.0, (std::max)(0.6, std::sqrt(2 * aV * (altFeet - 1.0)))) : -0.6;
			// the drift must be gone by 2 m: never come down faster than the braking it still needs allows - with much
			// drift low down, hold (or climb a little) while braking
			const double aH = 0.7 * 0.5 * (std::max)(0.5, g) * std::tan((std::max)(5 * RAD, maxTilt));
			if (drift > 0.3) vt = (std::max)(vt, (std::min)(1.0, -(altFeet - 2.0) * aH / drift));
			if (altFeet < 2.0 && drift > 0.5) vt = (std::max)(vt, 0.5 * (1.5 - altFeet));   // still sliding at the ground: wait
			cmd = std::clamp(m * (g + 1.5 * (vt - hv.y)) / (tmax * cosAll), -1.0, 1.0);
			if (landed) { mode = MANUAL; cmd = 0; }
		}
		// ground proximity, in any mode: sinking too fast for the height left -> the pods brake on their own
		tProtect = false;
		if (assist && surfaceMode && free && canFly && altFeet < 25 && !vecNow)
		{
			const double safe = -(1.0 + 0.8 * altFeet);
			if (hv.y < safe)
			{
				cmd = (std::max)(cmd, std::clamp(m * (g + 2.0 * (0.5 * safe - hv.y)) / (tmax * cosAll), 0.0, 1.0));
				tProtect = true;
			}
		}
		if (assist && surfaceMode && free) cmd *= std::clamp((cs - 0.3) / 0.4, 0.0, 1.0);   // full up to 45 deg of lean, none past 72
		tUpright = cs;
		tHover = surfaceMode && g > 0.05 ? Fuel() / (m * g / ISP) : 0;
		if (Air())
		{
			const double T = 0.5 * m * g, tf = (std::min)(T, pfFan), tr = T - tf;
			const double flow = 2 * ((tf > 0 ? FanPower(tf, rho) / (FanEnergy()) : 0.0) + tr / ISP);
			tHover = surfaceMode && g > 0.05 && pf > T && flow > 0 ? Fuel() / flow : 0;
		}
		if (assist && surfaceMode && !tProtect && !vecNow) cmd = (std::min)(cmd, std::clamp((mode == DESCENT ? 2.2 : 1.6) * m * g / (tmax * cosAll), 0.0, 1.0));   // the assistant: 1.6 of her weight, 2.2 to land
		// Shift+Space: full thrust, Shift+Ctrl: none, while held (both modes); otherwise the throttle or the assistant
		// (the impeller pack) at the ground and not asked to climb: the blades' pitch goes to zero at once - the thrust is gone,
		// the rotors keep turning at idle; she sets down the last centimetres and stands (the user: landing = idle, at once)
		if (Air() && altFeet < 0.25 && hv.y < 0.3 && in.vertical <= 0 && !in.boost && in.throttle <= 0 && mode != VECTOR && !(mode == HOLD && altTarget > 0.6))
		{ cmd = 0; mode = MANUAL; fanLvl[0] = fanLvl[1] = 0; }
		level = in.boost && in.vertical > 0 ? 1.0 : in.boost && in.vertical < 0 ? 0.0 : cmd;
		if (Fuel() <= 0) cmd = level = 0;   // the tank is dry: nothing to push with
		// down: the engines are off and stay off until the thrust keys are released and pressed again
		if (lockout)
		{
			const bool asking = in.vertical > 0 || in.throttle > 0;
			if (!asking) released = true;
			// the fans: asking again is enough (a hop off the ground on spooling rotors touches down again at once - the
			// lockout that followed shut them down while she held Space)
			else if (released || Air()) lockout = false;
			if (lockout) { cmd = level = 0; if (mode != MANUAL) mode = MANUAL; }
		}
		// the vertical force, signed: the assistant may turn the jets up to stop a climb (or push her down faster than
		// the weak lunar g would); 'level' stays the upward share (dust, displays, the liftoff test)
		if (Air()) level = (std::max)(0.0, level);   // the fans cannot push down
		lift = (landed && level < 0) ? 0.0 : level * tmax;
		level = (std::max)(0.0, level);
		dustLevel = worn && surfaceMode && deploy > 0.98 ? level * std::clamp(1.0 - altFeet / 1.5, 0.0, 1.0) * std::clamp(cs, 0.0, 1.0) : 0.0;
		if (Air()) dustLevel *= 0.35;
		if (titan) { dustTLevel = dustLevel; dustLevel = 0; } else dustTLevel = 0;
#if __has_include("../../samples/AtmoFX/AtmoFXJet.h")
		if (model == HEXA && AtmoFXJet::Fn()) dustLevel = dustTLevel = 0;   // the AtmoFX client throws the real ejecta (see the nozzles' Set)
#endif

		// ---- booms: out when the pods are wanted, folded again after a while idle on the ground ----
		const bool wanted = Air() ? manualDeploy : manualDeploy || level > 1e-3 || mode != MANUAL || free;
		idleT = wanted ? 0 : idleT + dt;
		if (wanted) deployTarget = 1;
		else if (landed && idleT > 1.5) deployTarget = 0;
		if (Air()) deployTarget = manualDeploy && Fuel() > 0 ? 1.0 : 0.0;   // the turbine: G starts it (5 s to idle), G stops it (4 s)
		deploy += Air() ? std::clamp(deployTarget - deploy, -dt / 4.0, dt / 5.0) : model == HEXA ? std::clamp(deployTarget - deploy, -dt / 3.0, dt / 3.0)
			: std::clamp(deployTarget - deploy, -dt / 1.2, dt / 1.2);
		// (the impeller pack with no air to work in - the Moon, open space: its boost alone, straight off the tank, no turbine to start)
		const bool ready = (deploy > 0.98 || (Air() && rho < 1e-3)) && canFly;

		// ---- the landing brakes the drift by itself (W/S/Q/E take over); the pilot's input otherwise ----
		FlightInput pin = in;
		// ---- the cruise (height hold by hand): W/S set the ground speed, the pods keep it and kill the side drift ----
		{
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const double vf = hv.z * ch + hv.x * sh, vr = hv.x * ch - hv.z * sh;
			tVf = vf;
			if (mode == HOLD && assist && free && !in.ap && !in.boost)
			{
				if (!spdSet) { spdTarget = std::clamp(vf, -5.0, SpdMax()); spdSet = true; }
				spdTarget = std::clamp(spdTarget + 3.0 * in.pitch * dt * (fine ? 0.1 : 1.0), -5.0, SpdMax());
				if (braking) { spdTarget = 0; if (std::hypot(vf, vr) < 0.15 && std::abs(hv.y) < 0.2) braking = false; }
				const double gg = (std::max)(0.5, g), mt = (std::max)(5 * RAD, maxTilt), ms = (std::min)(braking ? 45 * RAD : 20 * RAD, mt);
				pin.pitch = std::clamp(std::atan(std::clamp(1.0 * (spdTarget - vf), -4.0, 4.0) / gg) / mt, -1.0, 1.0);
				if (in.strafe == 0 || braking) pin.strafe = std::clamp(std::atan(std::clamp(-0.8 * vr, -4.0, 4.0) / gg) / ms, -1.0, 1.0);
			}
			else if (mode != HOLD) spdSet = false;
		}
		if (mode != DESCENT || !free || in.pitch != 0 || in.strafe != 0) landSet = false;   // steering by hand: a new point when let go
		if (mode == DESCENT && assist && free && in.pitch == 0 && in.strafe == 0)
		{
			double lng, lat, rad; OBJHANDLE body = v->GetEquPos(lng, lat, rad);
			const double R = oapiGetSize(body), cl = (std::max)(0.05, std::cos(lat)), gs = std::hypot(hv.x, hv.z);
			if (!landSet)
			{
				pinned = false;
				// the point she can stop at, braking at ~3 m/s^2 (kept for the display)
				const double dStop = gs * gs / (2 * 3.0);
				landLat = lat + (gs > 0.1 ? hv.z / gs : 0) * dStop / R;
				landLng = lng + (gs > 0.1 ? hv.x / gs : 0) * dStop / (R * cl);
				landSet = true;
			}
			double aN = 0, aE = 0;
			if (pinned)   // the autopilot's point (a pad, beside a target): hold over it
			{
				double dl = landLng - lng; while (dl > PI) dl -= PI2; while (dl < -PI) dl += PI2;
				const double errN = (landLat - lat) * R, errE = dl * R * cl;
				aN = std::clamp(0.6 * errN - 1.6 * hv.z, -4.0, 4.0); aE = std::clamp(0.6 * errE - 1.6 * hv.x, -4.0, 4.0);
			}
			else if (gs > 0.05)
			{
				// no point to chase: let the drift run while there is time, brake it so it is gone by the ground.
				// Time left to the ground at the planned sink; the drift allowed = what the pods can still stop in that time
				const double aH = 0.5 * (std::max)(0.5, g) * std::tan((std::max)(5 * RAD, maxTilt));
				const double tg = (std::max)(0.0, altFeet - 2.0) / (std::max)(0.6, -hv.y);
				const double allowed = altFeet < 2.0 ? 0.0 : 0.7 * aH * tg;
				if (gs > allowed)
				{
					const double a = (std::min)(aH * 2, 1.2 * (gs - allowed) + (altFeet < 2.0 ? 0.8 * gs : aH));
					aN = -hv.z / gs * a; aE = -hv.x / gs * a;
				}
			}
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const double aF = aN * ch + aE * sh, aR = aE * ch - aN * sh;
			const double gg = (std::max)(0.5, g), mt = (std::max)(5 * RAD, maxTilt);
			pin.pitch = std::clamp(std::atan(aF / gg) / mt, -1.0, 1.0);
			pin.strafe = std::clamp(std::atan(aR / gg) / (std::min)(45 * RAD, mt), -1.0, 1.0);
		}
		// ---- the pods: W swings the jets aft (push forward), S forward (brake, back); A/D against each other (turn) ----
		// the turn: calm by default (assistant: up to ~17 deg/s; by hand: 40 % of the differential tilt);
		// the full rate and the full tilt only with Shift - an emergency manoeuvre
		const double kf = fine ? 0.1 : 1.0;   // the limiter
		const double yawMax = (in.boost ? 1.0 : 0.3) * (fine ? 0.2 : 1.0);   // rad/s the assistant turns her at, full deflection
		const double tYaw = assist ? YAW_TILT * std::clamp(1.5 * (yawMax * in.yaw - yawRate) / yawMax * 0.6, -1.0, 1.0)
		                           : YAW_TILT * in.yaw * (in.boost ? 1.0 : 0.4) * kf;
		const double sideMax = (std::min)(in.boost ? 35 * RAD : mode == DESCENT || braking ? 45 * RAD : 20 * RAD, maxTilt + 1e-6);
		// ---- the pods' vectors, built from forces: the lift, the steering and the turn are separate demands, so the
		// steering works whatever the lift is (zero lift included) - the pods swing and run for it on their own ----
		const VECTOR3 fH0 = fG - up * dotp(fG, up), fH = length(fH0) > 0.2 ? unit(fH0) : unit(uG - up * dotp(uG, up));
		const VECTOR3 rH0 = rG - up * dotp(rG, up) - fH * dotp(rG, fH), rH = length(rH0) > 1e-3 ? unit(rH0) : _V(0, 0, 0);
		const double gg = (std::max)(0.5, g);
		const bool steer = free && !lockout && Fuel() > 0;   // on the ground the pods only lift
		VECTOR3 T;   // the total force wanted from both pods, in her frame, N
		if (vecNow)
		{
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const VECTOR3 north = fH * ch - rH * sh, east = fH * sh + rH * ch;
			T = tmul(Rm, (east * vecA.x + up * vecA.y + north * vecA.z) * m);
		}
		else if (assist && free && surfaceMode)
		{
			// the assistant: the lift along the local vertical, the steering as a horizontal acceleration
			// (W/S and Q/E ask for g*tan of the old vector angle - the same feel at a hover); her lean is taken out
			const double kp = in.ap ? 1.0 : kf;   // the autopilot and the assistant's own laws are not limited
			const double Ff = steer ? m * gg * std::tan(maxTilt * pin.pitch * (pin.pitch == in.pitch ? kp : 1.0)) : 0.0;
			const double Fs = steer ? m * gg * std::tan(sideMax * pin.strafe * (pin.strafe == in.strafe ? kp : 1.0)) : 0.0;
			T = tmul(Rm, up * lift + fH * Ff + rH * Fs);
		}
		else
		{
			// by hand: in her own frame, no compensation; the steering as a share of the full thrust
			T = _V(steer ? tmax * std::sin(sideMax) * pin.strafe * kf : 0.0, lift, steer ? tmax * std::sin(maxTilt) * pin.pitch * kf : 0.0);
		}
		// the turn: opposite fore/aft forces at the pods, as with a quarter of the thrust at least (works with no lift)
		const double Fy = pf * (std::max)(0.25, level) * std::sin(tYaw) * (steer || level > 0 ? 1.0 : 0.0);
		VECTOR3 want[2]; double need[2];
		for (int i = 0; i < 2; ++i)
		{
			const double sgn = i == 0 ? 1 : -1;   // +1: the pod on her right
			// sideways only from the pod whose jet goes outwards (thrust to +x blows the jet to -x: the left pod)
			const bool outward = T.x * sgn < 0;
			VECTOR3 w = _V(outward ? T.x : 0.0, 0.5 * T.y, 0.5 * T.z - sgn * Fy);
			const double hz = std::hypot(w.x, w.z);
			(void)hz;   // the pods swing all round in their fore/aft plane: the jets at 0.6 m out pass beside her and the helmet
			// the lift comes first: the steering and the turn get only what the pod has left after it (cutting the whole
			// vector cut the lift too - braking hard near the ground she sank and hit)
			w.y = std::clamp(w.y, Air() ? 0.0 : -pf, pf);
			const double room = std::sqrt((std::max)(0.0, pf * pf - w.y * w.y)), hz2 = std::hypot(w.x, w.z);
			if (hz2 > room && hz2 > 1e-9) { w.x *= room / hz2; w.z *= room / hz2; }
			want[i] = w; need[i] = pf > 1 ? length(w) / pf : 0.0;
		}
		const double over = (std::max)({ 1.0, need[0], need[1] });   // (rounding only now)
		// both pods swing together: one common angle follows the total vector the short way round, each pod only adds
		// its small offset (the turn). Swinging separately near 180 deg, one went over the front and the other over the
		// back - for two seconds they pushed opposite ways 1.2 m apart, and she spun up
		auto wrap = [](double a) { while (a > PI) a -= PI2; while (a < -PI) a += PI2; return a; };
		if (std::hypot(T.y, T.z) > 1.0 && ready)
			tiltBase = wrap(tiltBase + std::clamp(wrap(std::atan2(T.z, T.y) - tiltBase), -dt * 180 * RAD, dt * 180 * RAD));
		else if (!ready) tiltBase = wrap(tiltBase - std::clamp(tiltBase, -dt * 90 * RAD, dt * 90 * RAD));
		for (int i = 0; i < 2; ++i)
		{
			const double sgn = i == 0 ? 1 : -1;
			const VECTOR3 w = want[i];
			const double len = length(w);
			double tT = 0, sT = 0;
			if (len > 1e-6 && ready)
			{
				tT = std::atan2(w.z, w.y);   // any direction in the pod's plane, up included
				sT = std::asin(std::clamp(w.x / len, -1.0, 1.0));
				// never inwards past 6 deg (the jet would cross her), never outwards past 60
				sT = sgn > 0 ? std::clamp(sT, -60 * RAD, 6 * RAD) : std::clamp(sT, -6 * RAD, 60 * RAD);
			}
			// the common angle plus this pod's own small offset (the turn), never more than 30 deg apart
			const double base = std::hypot(T.y, T.z) > 1.0 ? std::atan2(T.z, T.y) : tiltBase;
			double target = wrap(tiltBase + std::clamp(wrap(tT - base), -30 * RAD, 30 * RAD));
			if (Air()) { target = std::clamp(target, -S().TILT, S().TILT); sT = std::clamp(sT, -S().SIDE, S().SIDE); }
			tilt[i] = wrap(tilt[i] + std::clamp(wrap(target - tilt[i]), -dt * 240 * RAD, dt * 240 * RAD));
			// a jet blowing up (over the head, raised arms) or forward (past the arms) is canted 15 deg outwards: the
			// plume spreads wide in vacuum and must not cross her (the jets back and down pass clear as they are)
			const double cant = Air() ? 0.0 : 15 * RAD * std::clamp((std::max)(-std::cos(tilt[i]), -std::sin(tilt[i])) * 2.0, 0.0, 1.0);
			sT = sgn > 0 ? (std::min)(sT, -cant) : (std::max)(sT, cant);
			sidePod[i] += std::clamp(sT - sidePod[i], -dt * 180 * RAD, dt * 180 * RAD);   // fast: the pods answer a danger at once
			podLvl[i] = ready && Fuel() > 0 ? (std::min)(1.0, need[i] / over) : 0.0;
			// a pod still swinging pushes only as much as it points the way it should (no thrust the wrong way)
			if (len > 1e-6)
			{
				const VECTOR3 now = _V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i]));
				podLvl[i] *= std::clamp(dotp(now, w / len), 0.0, 1.0);
			}
			v->SetThrusterDir(pod[i], _V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i])));
			if (Air())
			{
				// the rotors spool after the throttle; Orbiter pushes what they give and burns what the turbine needs for
				// it: the thrust over the flow is the pack's momentum per kilogram of fuel now
				// the fans spool towards their part of what is asked; the boost gives the rest at once (up to its 400 N):
				// the rotors' lag, thin air, vacuum
				const double want = podLvl[i] * pf, fanWant = pfFan > 1 ? (std::min)(want, pfFan) / pfFan : 0.0;
				fanLvl[i] += (fanWant - fanLvl[i]) * (std::min)(1.0, dt / (fanWant > fanLvl[i] ? FAN_UP : FAN_DOWN));
				const double tf = fanLvl[i] * pfFan, tr = std::clamp(want - tf, 0.0, S().BOOST);
				boostLvl[i] = tr / S().BOOST;
				if (i == 0) liftNow = 0;
				liftNow += tf + (fanLvl[i] >= fanWant - 0.03 ? tr : 0.0);   // the boost counts once the rotors are up (thin air, vacuum)
				outLvl[i] = (tf + tr) / pf;
				const double flow = (tf > 0.5 || fanLvl[i] > 0.01 ? FanPower(tf, rho) / (FanEnergy()) : 0.0) + tr / ISP;
				v->SetThrusterMax0(pod[i], pf);
				v->SetThrusterIsp(pod[i], tf + tr > 1.0 && flow > 0 ? (tf + tr) / flow : 1e7);
			}
			else outLvl[i] = podLvl[i];
			v->SetThrusterLevel(pod[i], outLvl[i]);
			if (model == HEXA && kits[2].geo.ok)   // the exhaust out of the capsule's exit, along its gimbal; the air's share by the density
			{
				// the jet's size by the hydrogen's flow, not the share of the current maximum: the «форсаж» doubles the thrust
				// at half the exhaust speed - four times the flow - so its jet is the larger one at any thrust (it was drawn at
				// half the level against the doubled maximum, and shorter: the user, 2026-10-18 «на форсаже поток меньше»)
				const double a = std::clamp(rho / 0.05, 0.0, 1.0), l = deploy > 0.98 ? std::clamp(outLvl[i] * pf / POD_F, 0.0, 1.0) : 0.0;   // thin air (Mars) is near vacuum for the jet
				hxLvl[i][0] = l * (1 - a);
				hxLvl[i][1] = steamOn && !forsazh ? l * a : 0.0; hxLvl[i][2] = steamOn && forsazh ? l * a : 0.0; hxLvl[i][3] = steamOn ? 0.0 : l * a;
				// seen along its axis the strip (a ribbon turned to the camera about the axis) is a line: fade it and raise the
				// end-on glow as the camera comes round to the axis (the user: «плоские потоки») - with the stock client only;
				// the AtmoFX fork draws the jet as a volume (ExhaustVolume.fx present)
				static const bool volClient = GetFileAttributesA("Modules\\D3D9Client\\ExhaustVolume.fx") != INVALID_FILE_ATTRIBUTES;
				if (volClient) for (int k = 0; k < 4; ++k) hxEndLvl[i][k] = 0;
				else
				{
					VECTOR3 cg, cl; oapiCameraGlobalPos(&cg); v->Global2Local(cg, cl);
					const VECTOR3 dv = cl - hxPos[i]; const double dl = length(dv);
					const double c = dl > 1e-6 ? std::abs(dotp(dv, hxDir[i])) / dl : 0.0;
					double e = std::clamp((c - 0.55) / 0.40, 0.0, 1.0); e = e * e * (3 - 2 * e);
					for (int k = 0; k < 4; ++k) { hxEndLvl[i][k] = hxLvl[i][k] * e; hxLvl[i][k] *= 1.0 - 0.85 * e; }
				}
				// the chambers: the glow follows each one's output; full thrust loads the wall (the «форсаж» more), the hydrogen
				// cooling holds up to ~55 %: a long run at full overheats a chamber (its sensor shuts it until the wall cools)
				for (int ch = 0; ch < 2; ++ch)
				{
					const bool on = !chDamaged[i][ch] && !chHot[i][ch];
					const double tgt = on ? outLvl[i] : 0.0;
					chHeat[i][ch] += (tgt - chHeat[i][ch]) * (std::min)(1.0, dt / (tgt > chHeat[i][ch] ? HEXA_UP : HEXA_DOWN));
					if (on) chLoad[i][ch] = (std::max)(0.0, chLoad[i][ch] + dt * (outLvl[i] * outLvl[i] * (forsazh ? 2.2 : 1.0) * (ch ? 0.97 : 1.0) - 0.55) / 45.0);
					else chLoad[i][ch] = (std::max)(0.0, chLoad[i][ch] - dt / 20.0);
					if (on && chLoad[i][ch] > 1.0) chHot[i][ch] = true;
					else if (chHot[i][ch] && chLoad[i][ch] < 0.3) chHot[i][ch] = false;
				}
				// the hot nozzle as a heat node for the AtmoFX client's shimmer (the jet itself shimmers as an exhaust): it
				// keeps the air trembling over the capsule while the wall cools after the jet stops; nothing in vacuum
				{
					const double wall = 300.0 + (HEXA_TMAX - 300.0) * std::clamp((std::max)(chHeat[i][0], chHeat[i][1]), 0.0, 1.0);
					const double dT = (std::max)(0.0, wall - v->GetAtmTemperature());
					if (std::abs(dT - heatDT[i]) > 15.0 || (dT == 0.0 && heatDT[i] != 0.0)) {
						AtmoFXHeat::Set(v->GetHandle(), 7100 + i, hxPos[i] + hxDir[i] * 0.06, 0.06, dT < 30.0 ? 0.0 : dT);
						heatDT[i] = dT < 30.0 ? 0.0 : dT;
					}
				}
#if __has_include("../../samples/AtmoFX/AtmoFXJet.h")
				// the ground thrown out from under the jet (the AtmoFX client integrates the grains and draws them): each
				// nozzle every frame while it runs - exit, the jet's way, thrust, exhaust speed, the gas's exit temperature,
				// the ground under her (Config\OrbiterCrew\Ground\<body>) and its wetness
				{
					const double T = outLvl[i] * pf, ve = forsazh ? ISP / 2 : ISP;
					int gnd; double wet; GroundUnder(gnd, wet);
					AtmoFXJet::Set(v->GetHandle(), 7200 + i, hxPos[i], -hxDir[i], deploy > 0.98 ? T : 0.0, ve, forsazh ? 1800.0 : 3000.0, gnd, wet, 0.0405);
				}
#endif
			}
			else
			{
				for (int k = 0; k < 4; ++k) hxLvl[i][k] = hxEndLvl[i][k] = 0;
				if (heatDT[i] != 0.0) { AtmoFXHeat::Set(v->GetHandle(), 7100 + i, _V(0, 0, 0), 0, 0); heatDT[i] = 0.0; }
			}
			// the nozzle heats up in a few seconds of thrust and cools slower (for a glowing exit material, not yet drawn)
			if (model == HEXA) heat[i] += (outLvl[i] - heat[i]) * (std::min)(1.0, dt / (outLvl[i] > heat[i] ? HEXA_UP : HEXA_DOWN));   // the nozzle's wall: fast up, slow down
			else heat[i] += (podLvl[i] - heat[i]) * (std::min)(1.0, dt / (podLvl[i] > heat[i] ? 3.0 : 7.0));
		}
		side = 0.5 * (sidePod[0] + sidePod[1]);
		tMaxTilt = maxTilt;
		if (Air())
		{
			// the turbine idles while the booms are out and she can fly: that fuel is burnt by hand (no thrust to carry it)
			// the ram drag: each fan swallows rho*A*v_exit = sqrt(rho*A*T) kg/s of air and turns it to its axis - the air
			// coming across it (her flight through the air) loses its cross speed: a force against it. With the body's own
			// drag it holds her to ~29 m/s at the rings' full 35 deg (Earth)
			{
				VECTOR3 va; v->GetAirspeedVector(FRAME_LOCAL, va);
				const double A = 0.25 * PI * S().D * S().D;
				VECTOR3 F = _V(0, 0, 0);
				for (int i = 0; i < 2; ++i)
				{
					const double tf = fanLvl[i] * pfFan;
					if (tf <= 1 || rho < 1e-4) continue;
					const VECTOR3 d = _V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i]));
					const VECTOR3 cross = va - d * dotp(va, d);
					F -= cross * std::sqrt(rho * A * tf);
				}
				if (length(F) > 0.1) v->AddForce(F, _V(0, 0, 0));
			}
			// the boost's flame along each ring's axis, out of its hub
			const bool vac = rho < 1e-3;
			for (int i = 0; i < 2; ++i)
			{
				const VECTOR3 d = _V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i]));
				boostExDir[i] = -d;
				boostExPos[i] = fanG().pivot[i] - d * (fanG().pivot[i].y - fanG().boostY);
				boostExLvl[i] = boostLvl[i] * (vac ? 0.35 : 1.0);
			}
			{ const double b = 0.5 * (boostLvl[0] + boostLvl[1]); glow += (b - glow) * (std::min)(1.0, dt / (b > glow ? 1.5 : 6.0)); }
			fanFlow = 0;
			const bool idling = ready && Fuel() > 0 && pfFan > 1;
			for (int i = 0; i < 2; ++i) fanFlow += (idling ? FanPower(fanLvl[i] * pfFan, rho) / (FanEnergy()) : 0.0) + boostLvl[i] * S().BOOST / ISP;
			if (idling && fanLvl[0] + fanLvl[1] < 0.02) v->SetPropellantMass(prop, (std::max)(0.0, Fuel() - 2 * FAN_IDLE / (FanEnergy()) * dt));
			for (int i = 0; i < 2; ++i) spin[i] = std::fmod(spin[i] + dt * 80.0 * (std::max)(fanLvl[i], 0.12 * deploy), PI2);   // drawn (idle 12 %, start, run-down)
			tLimited = tLimited || heatShare < 1.0;   // the display: the turbine starved by hot air
		}

		// ---- the body stays upright: the pack's small ports hold her attitude near a surface ----
		if (assist && free && surfaceMode && canFly && Fuel() > 0)
		{
			const double p = std::clamp(4.0 * leanF + 2.0 * leanFRate, -1.0, 1.0);    // leaning forward -> nose up
			const double r = std::clamp(4.0 * leanR + 2.0 * leanRRate, -1.0, 1.0);    // leaning right -> roll left
			const double y = in.yaw ? 0.0 : std::clamp(1.5 * yawRate, -1.0, 1.0);    // no turn asked: the ports help stop the turn
			// pitch and yaw share the fore/aft nozzles: setting the groups one after another let the yaw groups overwrite
			// the pitch every step (the flight log: pitch-up and pitch-down equal, she tipped over unopposed). Sum per nozzle.
			std::map<THRUSTER_HANDLE, double> lv;
			auto add = [&](THGROUP_TYPE t, double l)
			{
				for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) lv[v->GetGroupThruster(t, i)] += (std::max)(0.0, l);
			};
			add(THGROUP_ATT_PITCHUP, p); add(THGROUP_ATT_PITCHDOWN, -p);
			add(THGROUP_ATT_BANKLEFT, r); add(THGROUP_ATT_BANKRIGHT, -r);
			add(THGROUP_ATT_YAWLEFT, y); add(THGROUP_ATT_YAWRIGHT, -y);
			for (auto& t : lv) v->SetThrusterLevel(t.first, (std::min)(1.0, t.second));
			holdingAttitude = true;
		}
		else if (holdingAttitude)
		{
			for (THGROUP_TYPE t : { THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT })
				for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) v->SetThrusterLevel(v->GetGroupThruster(t, i), 0);
			holdingAttitude = false;
		}

		tLeanF = leanF; tLeanR = leanR; tVs = hv.y; tAlt = altFeet; tGs = std::hypot(hv.x, hv.z); tFlying = free; tBoost = in.boost;
	}

	void JetPack::Nudge(double dAlt, double dSpd, bool stop)
	{
		if (!worn) return;
		assist = true; lockout = false;
		if (mode != HOLD) { mode = HOLD; altTarget = (std::max)(1.5, tAlt); spdTarget = std::clamp(tVf, -5.0, SpdMax()); spdSet = true; }
		if (!spdSet) { spdTarget = std::clamp(tVf, -5.0, SpdMax()); spdSet = true; }
		altTarget = (std::max)(0.5, altTarget + dAlt);
		spdTarget = stop ? 0.0 : std::clamp(spdTarget + dSpd, -5.0, SpdMax());
	}

	double JetPack::MaxAccel() const { return worn && v ? 2 * pf / v->GetMass() : 0.0; }

	void JetPack::Stop()
	{
		if (!worn || !surfaceMode) return;
		assist = true; lockout = false;
		mode = HOLD; altTarget = (std::max)(1.0, tAlt); spdTarget = 0; spdSet = true; braking = true;
	}

	void JetPack::Touchdown()
	{
		if (model == HEXA && lastVs < -6.0)   // a hard landing: the capsules hit the ground or her - a chamber may be damaged
			for (int i = 0; i < 2; ++i)
				if (std::rand() % 100 < static_cast<int>(std::clamp((-lastVs - 6.0) * 12.0, 0.0, 80.0))) chDamaged[i][std::rand() % 2] = true;
		lastVs = 0;
		braking = false;
		mode = MANUAL; cmd = level = 0; lockout = true; released = false; landSet = false;
		for (THRUSTER_HANDLE t : pod) v->SetThrusterLevel(t, 0);
		outLvl[0] = outLvl[1] = 0; boostLvl[0] = boostLvl[1] = 0;   // (the fans' rotors spin down by themselves)
		// the attitude ports too: Orbiter wakes a landed vessel that still has a thruster firing (the 'static flight'
		// after a touchdown: set down, lifted by the ports set earlier in the same step, set down again...)
		for (THGROUP_TYPE t : { THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT,
			THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT, THGROUP_ATT_UP, THGROUP_ATT_DOWN, THGROUP_ATT_FORWARD, THGROUP_ATT_BACK })
			for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) v->SetThrusterLevel(v->GetGroupThruster(t, i), 0);
		holdingAttitude = false;
	}

	void JetPack::Pose(Skin& skin) const
	{
		skin.SetPartHidden("Jet", !worn || Air() || model == HEXA);   // the impeller pack and the Гекса are their own meshes (PoseFan)
		if (worn && model == TREFOIL)   // the trefoil: the core is «Наплечный»'s, the arms and the gondolas its own mesh
			for (const char* part : { "JetBoom", "JetHinge", "JetPod" }) skin.SetPartHidden(part, true);
		if (!worn || HasKit()) return;
		// modelled deployed; the booms swing back about the hinge's vertical axis to fold, the pods tilt on their pivots.
		// Label side: the model's "L" parts sit on +x (her right) in the Orbiter frame.
		for (int i = 0; i < 2; ++i)
		{
			const char* boom = i == 0 ? "JetBoomL" : "JetBoomR"; const char* podName = i == 0 ? "JetPodL" : "JetPodR";
			const double side = pivot[i].x > 0 ? 1 : -1;
			double Ry[9], Rx[9], Rz[9], Rxz[9], R[9];
			RotY(side * PI05 * (1 - deploy), Ry); RotX(tilt[i], Rx); RotZ(-sidePod[i], Rz); Mul(Rx, Rz, Rxz); Mul(Ry, Rxz, R);
			const VECTOR3 tb = hinge[i] - Apply(Ry, hinge[i]);
			const VECTOR3 tp = Apply(Ry, pivot[i] - Apply(Rxz, pivot[i]) - hinge[i]) + hinge[i];
			float Rf[9], Tb[3] = { float(tb.x), float(tb.y), float(tb.z) }, Rp[9], Tp[3] = { float(tp.x), float(tp.y), float(tp.z) };
			for (int k = 0; k < 9; ++k) { Rf[k] = float(Ry[k]); Rp[k] = float(R[k]); }
			skin.SetPartTransform(boom, Rf, Tb);
			skin.SetPartTransform(podName, Rp, Tp);
		}
	}

	// the impeller pack on her back: the worn mesh is her model frame at rest; each frame its groups go through the fold
	// (the booms about their hinges), the rings' tilt (about their centres, as the pods' vectors), the rotors' spin, and
	// then onto her back as the figure stands now (three points of bone Spine1 round the core)
	void JetPack::PoseFan(const Skin& skin, VISHANDLE vis)
	{
		if (!v || !vis || !worn || !HasKit() || kits[K()].mesh == static_cast<UINT>(-1)) return;
		const FanGeo& fan = kits[K()].geo; const std::vector<std::vector<NTVERTEX>>& fanVtx = kits[K()].vtx;
		DEVMESHHANDLE dev = v->GetDevMesh(vis, kits[K()].mesh);
		if (!dev) return;
		const int b = skin.Bone("Spine1");
		const VECTOR3 c0 = fan.shellC;
		VECTOR3 P0 = c0, X = _V(1, 0, 0), Y = _V(0, 1, 0), Z = _V(0, 0, 1);
		if (b >= 0)
		{
			P0 = skin.Point(b, c0);
			const VECTOR3 xr = skin.Point(b, c0 + _V(0.1, 0, 0)) - P0, yr = skin.Point(b, c0 + _V(0, 0.1, 0)) - P0;
			if (length(xr) > 1e-6 && length(yr) > 1e-6) { X = unit(xr); Z = unit(crossp(X, yr)); Y = crossp(Z, X); }
		}
		for (int j = 0; j < 4 && fan.kin == 3; ++j)   // the Гекса's chambers: the colour by the wall's temperature (as the «Панцирь» soles)
			if (fan.glowPod[j] >= 0)
			{
				const int i = j / 2, ch = j % 2;
				const double T = 300.0 + (HEXA_TMAX - 300.0) * std::clamp(chHeat[i][ch], 0.0, 1.0) * (forsazh ? 0.88 : 1.0);
				const double f = std::clamp((T - 760.0) / 740.0, 0.0, 1.0);
				const oapi::FVECTOR4 e(static_cast<float>(std::sqrt(f)), static_cast<float>(std::pow(f, 1.5)), static_cast<float>(std::pow(f, 8.0)), 1.0f);
				oapiSetMaterialEx(dev, static_cast<DWORD>(fan.glowPod[j]), Emission, &e);
			}
		for (int sd = 0; sd < 2 && fan.kin == 3; ++sd)   // the exhaust from the nozzle as it is drawn: through the gimbal, the telescope, the hinge, onto her
		{
			const double sg = fan.pivot[sd].x > 0 ? 1 : -1;
			const VECTOR3 H = fan.hinge[sd], Pv = fan.pivot[sd];
			double Rx[9], Rz[9], Rxz[9], Rw[9];
			RotX(tilt[sd], Rx); RotZ(-sidePod[sd], Rz); Mul(Rx, Rz, Rxz);
			const double sw = std::clamp((0.5 - deploy) / 0.5, 0.0, 1.0), ex = std::clamp((deploy - 0.5) / 0.5, 0.0, 1.0);
			RotY(sg * fan.swing * sw, Rw);
			auto map = [&](VECTOR3 p)
			{
				p = Pv + Apply(Rxz, p - Pv) + _V(-sg * fan.stroke * (1 - ex), 0, 0);
				p = H + Apply(Rw, p - H) + _V(0, fan.lift * sw, 0);
				const VECTOR3 l = p - c0; return P0 + X * l.x + Y * l.y + Z * l.z;
			};
			const VECTOR3 e0 = map(Pv - _V(0, 0.137, 0)), e1 = map(Pv - _V(0, 0.337, 0));   // the exit plane (the lip, 2 mm in), and down the axis
			hxPos[sd] = e0; hxDir[sd] = unit(e0 - e1);   // Orbiter draws the exhaust against ldir: ldir is the thrust's way, the jet runs down
		}
		if (fan.glowMat >= 0)   // the aerospikes' ramps: dull metal, red-orange as the boost heats them
		{
			const float g = static_cast<float>(std::clamp(glow, 0.0, 1.0));
			MATERIAL m{}; m.diffuse = { 0.30f, 0.24f, 0.20f, 1 }; m.ambient = m.diffuse; m.specular = { 0.5f, 0.5f, 0.5f, 1 }; m.power = 20;
			m.emissive = { 1.0f * g, 0.40f * g * g, 0.10f * g * g * g, 1 };
			oapiSetMaterial(dev, static_cast<DWORD>(fan.glowMat), &m);
		}
		for (size_t gi = 0; gi < fanVtx.size() && gi < fan.role.size(); ++gi)
		{
			if (fanVtx[gi].empty()) continue;
			const int role = fan.role[gi], sd = fan.side[gi];
			double Ry[9], Rx[9], Rz[9], Rxz[9], Rs[9];
			VECTOR3 H = _V(0, 0, 0), Pv = _V(0, 0, 0);
			if (sd >= 0)
			{
				const double sgn = fan.pivot[sd].x > 0 ? 1 : -1;
				H = fan.hinge[sd]; Pv = fan.pivot[sd];
				if (fan.foldZ) RotZ(sgn * fan.fold * (1 - deploy), Ry); else RotY(sgn * fan.fold * (1 - deploy), Ry);   // (Ry: the fold, whichever axis)
				RotX(tilt[sd], Rx); RotZ(-sidePod[sd], Rz); Mul(Rx, Rz, Rxz); RotY(spin[sd] * sgn, Rs);
			}
			double Rk[9]{}, Rl[9]{};   // KIN 2: the joints' chain, and a rotor's spin or a vane's turn about its own axis
			// KIN 3: unfolding, the swing out first (deploy 0..0.5, the screw hinge coming down), then the telescope (0.5..1)
			double Rw[9]{}; VECTOR3 tele = _V(0, 0, 0), up = _V(0, 0, 0);
			if (fan.kin == 3 && sd >= 0)
			{
				const double sg = fan.pivot[sd].x > 0 ? 1 : -1;
				const double sw = std::clamp((0.5 - deploy) / 0.5, 0.0, 1.0), ex = std::clamp((deploy - 0.5) / 0.5, 0.0, 1.0);
				RotY(sg * fan.swing * sw, Rw); up = _V(0, fan.lift * sw, 0);
				const double k = role == 5 ? 0.5 : (role == 6 || role == 2) ? 1.0 : 0.0;
				tele = _V(-sg * fan.stroke * k * (1 - ex), 0, 0);
			}
			if (fan.kin == 2 && sd >= 0)
			{
				const double sg = fan.pivot[sd].x > 0 ? 1 : -1, f = 1 - deploy;
				double Ra[9], Rf[9];
				RotAxis(unit(Pv - H), sg * (tilt[sd] + fan.twist * f), Ra);                         // about the arm: pitch, twist
				RotAxis(_V(fan.foldU.x, sg * fan.foldU.y, sg * fan.foldU.z), fan.foldA * f, Rf);    // the fold (mirrored)
				Mul(Rf, Ra, Rk);
				if (role == 3 && gi < fan.gA.size()) RotAxis(fan.gA[gi], spin[sd] * fan.gDir[gi], Rl);
				else if (role == 4 && gi < fan.gA.size()) RotAxis(fan.gA[gi], std::clamp(1.5 * sg * sidePod[sd], -25 * RAD, 25 * RAD), Rl);   // the side vector
			}
			std::vector<NTVERTEX> out = fanVtx[gi];
			for (NTVERTEX& q : out)
			{
				VECTOR3 p = _V(q.x, q.y, q.z), n = _V(q.nx, q.ny, q.nz);
				if (fan.kin == 3)
				{
					if (sd >= 0 && role >= 1)
					{
						if (role == 2) { p = Pv + Apply(Rxz, p - Pv); n = Apply(Rxz, n); }   // the capsule on its gimbal
						p = p + tele;
						p = H + Apply(Rw, p - H) + up; n = Apply(Rw, n);
					}
				}
				else if (fan.kin == 2)
				{
					if (sd >= 0 && role >= 1)
					{
						if (role >= 3 && gi < fan.gC.size()) { const VECTOR3 C = fan.gC[gi]; p = C + Apply(Rl, p - C); n = Apply(Rl, n); }
						p = H + Apply(Rk, p - H); n = Apply(Rk, n);
					}
				}
				else if (sd >= 0 && role >= 1)
				{
					if (role == 3) { const VECTOR3 a = _V(Pv.x, fan.rotorY, Pv.z); p = a + Apply(Rs, p - a); n = Apply(Rs, n); }
					if (role >= 2) { p = Pv + Apply(Rxz, p - Pv); n = Apply(Rxz, n); }
					p = H + Apply(Ry, p - H) + fan.foldShift * (1 - deploy); n = Apply(Ry, n);
				}
				const VECTOR3 l = p - c0, w = P0 + X * l.x + Y * l.y + Z * l.z, m = X * n.x + Y * n.y + Z * n.z;
				q.x = float(w.x); q.y = float(w.y); q.z = float(w.z); q.nx = float(m.x); q.ny = float(m.y); q.nz = float(m.z);
			}
			GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = out.data(); ges.nVtx = static_cast<DWORD>(out.size());
			oapiEditMeshGroup(dev, static_cast<DWORD>(gi), &ges);
		}
	}

	std::string JetPack::Hud() const
	{
		if (!worn) return std::string();
		char b[200];
		static const char* MODE[] = { "manual", "HOVER HOLD", "AUTO DESCENT", "AP VECTOR" };
		snprintf(b, sizeof b, "JET  fuel %d%%  dv %.0f m/s  thrust %d%%  booms %s  %s%s",static_cast<int>(100 * Fuel() / FuelMax()), DeltaV(),
			static_cast<int>(100 * level), deploy > 0.98 ? "out" : deploy < 0.02 ? "folded" : "moving", MODE[mode],
			mode == HOLD ? (vTarget >= 0 ? "  +" : "  ") : "");
		std::string s = b;
		if (mode == HOLD) { snprintf(b, sizeof b, "%.1f m/s", vTarget); s += b; }
		if (tFlying && surfaceMode)
		{
			snprintf(b, sizeof b, "\nFLY  lean fwd %+.0f (want %+.0f)  side %+.0f (want %+.0f)  vs %+.1f m/s (hold %+.1f)  alt %.1f m  gs %.1f m/s%s%s",
				tLeanF * DEG, tWantF * DEG, tLeanR * DEG, tWantR * DEG, tVs, tVsCmd, tAlt, tGs, tBoost ? "  TURBO" : "", tProtect ? "  GROUND PROXIMITY" : tTerrain ? "  TERRAIN AHEAD" : "");
			s += b;
		}
		return s;
	}
}

namespace ocrew
{
	void JetPack::Fill(HudData& d) const
	{
		d.jet = worn;
		if (!worn) return;
		d.jetFlying = tFlying; d.jetSurface = surfaceMode; d.jetLimited = tLimited; d.jetProtect = tProtect; d.jetTerrain = tTerrain;
		d.jetFuel = Fuel() / FuelMax(); d.jetDv = DeltaV(); d.jetThrottle = level; d.jetFlow = Air() ? fanFlow : Thrust() / ISP; d.jetTilt[0] = tilt[0]; d.jetTilt[1] = tilt[1];
		d.jetMaxTilt = tMaxTilt; d.jetDeploy = deploy; d.jetAltHold = altTarget; d.jetSpd = spdTarget; d.jetVf = tVf; d.jetBraking = braking; d.jetFine = fine; d.alt = tAlt; d.vs = tVs; d.gs = tGs;
		d.leanF = tLeanF; d.leanR = tLeanR; d.jetMode = static_cast<int>(mode); d.boost = tBoost; d.jetAssist = assist; d.jetHover = tHover;
		d.jetModel = model; d.jetTurbine = TurbineOn(); d.jetFanLvl = FanLevel(); d.jetRocket = RocketLevel(); d.jetHeat = heatShare; d.jetFanAuth = FanAuthority(); d.jetForsazh = Forsazh(); d.jetChFault = ChamberFault();
	}
}

// AtmoFX fork: Titan's methane clouds, ray-marched in a reduced-resolution pass (see TitanVolume.h)

#include "TitanVolume.h"
#include "Scene.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

namespace {

struct Settings {
	char body[64] = "Titan";
	bool enabled = true;
	// haze: k(h) = kLow exp(-h/HLow) + kUp exp(-h/HUp) [1/km]; vertical optical depth kLow*HLow + kUp*HUp
	double kLow = 0.025, hLow = 60, kUp = 0.03, hUp = 65;
	double ground = -0.617, top = 600;						// ray end: liquid level / marched top [km]
	double hazeLight[3] = { 1.00, 0.78, 0.48 };				// light entering the top of the haze
	double hazeAbs[3] = { 0.20, 0.45, 0.85 };				// reddening per optical depth above
	double hazeBright = 1.0, hazeFwd = 0.35, twilight = 0.25, hazeRedCap = 0, lowBright = 0, lowFwd = 0.12;
	// streaks (northern summer 2016-2017, Cassini): latitude belt [deg], altitudes [km]
	double stLatMin = 45, stLatMax = 55, stBase = 26, stTop = 36;
	double stCov = 0.35, stSize = 40, stStretch = 8, stLifeH = 5;
	// convective cells: latitude belt, altitudes, coverage, cell size, anvil; storm cluster
	double cuLatMin = 50, cuLatMax = 75, cuBase = 12, cuTop = 32, cuCov = 0.08, cuCell = 30, cuAnvil = 0.5;
	double stormLat = 0, stormLon = 0, stormRadius = 0;		// radius 0 = no storm
	double density = 0.6, fineKm = 6, windKmS = 0.008, fineShare = 0.25;
	double cloudAlb[3] = { 0.95, 0.93, 0.90 };
	double sunGain = 1.0;
	double expo = 1.0;
	double maxCamAlt = 20000;								// pass active below this camera altitude [km] (also the disk and limb from orbit)
	// near haze in depth-tested slices (things close by get only the haze in front of them)
	double nearKm = 80;										// slices cover the view depths up to this [km]
	int slices = 12;
	// summer-pole fog (Cassini VIMS, Brown et al. 2009): optical depth ~0.25, scattering near 0.75 km (tops < 3 km)
	double fogTau = 0.25, fogTop = 1.5, fogLat = 60, fogCov = 0.3, fogPatch = 150, fogLifeH = 30;
	int steps = 48;
	int downscale = 2;
	bool autoQ = true;
	double budgetMs = 2.5;
};

Settings S;
bool tried = false, ok = false, cfgRead = false;
LPD3DXEFFECT fx = nullptr;
LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
LPDIRECT3DTEXTURE9 lowTex = nullptr;
UINT lowW = 0, lowH = 0;
double logT = -1e9;
LPDIRECT3DQUERY9 qDisj = nullptr, qFreq = nullptr, qT0 = nullptr, qT1 = nullptr;
bool qPending = false;
double gpuMsSum = 0; int gpuN = 0;

struct QuadVtx { float x, y, z; float rx, ry, rz; };
const DWORD QUAD_FVF = D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0);

// own log, flushed per line (Orbiter.log is rewritten by the launcher)
void TLog(const char* fmt, ...)
{
	char buf[1024];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	oapiWriteLog(buf);
	FILE* f = fopen("TitanFX_Volume.log", "at");
	if (f) { SYSTEMTIME st; GetLocalTime(&st); fprintf(f, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf); fclose(f); }
}

void ReadSettings()
{
	FILE* f = fopen("Config\\TitanFX_Volume.cfg", "rt");
	if (!f) return;
	char line[256], key[64];
	double v, a, b, c;
	while (fgets(line, sizeof line, f)) {
		char* sc = strchr(line, ';'); if (sc) *sc = 0;
		if (sscanf(line, " Body = %63s", key) == 1) { strncpy(S.body, key, 63); continue; }
		if (sscanf(line, " Quality = %63s", key) == 1) {
			if (!_stricmp(key, "low"))         { S.steps = 40; S.downscale = 3; S.autoQ = false; }
			else if (!_stricmp(key, "medium")) { S.steps = 64; S.downscale = 2; S.autoQ = false; }
			else if (!_stricmp(key, "high"))   { S.steps = 96; S.downscale = 1; S.autoQ = false; }
			else                               { S.steps = 64; S.downscale = 2; S.autoQ = true; }
			continue;
		}
		if (sscanf(line, " Clouds = %63s", key) == 1) {		// presets (Cassini); explicit keys after it override
			if (!_stricmp(key, "storm")) { S.stCov = 0.2; S.cuCov = 0.1; S.stormRadius = 250; }
			else if (!_stricmp(key, "none")) { S.stCov = 0; S.cuCov = 0; S.stormRadius = 0; }
			else { S.stCov = 0.35; S.cuCov = 0.08; S.stormRadius = 0; }	// summer2016
			continue;
		}
		if (sscanf(line, " %63[A-Za-z0-9_] = %lf %lf %lf", key, &a, &b, &c) == 4) {
			double* t = !_stricmp(key, "HazeLight") ? S.hazeLight : !_stricmp(key, "HazeAbs") ? S.hazeAbs : !_stricmp(key, "CloudAlbedo") ? S.cloudAlb : nullptr;
			if (t) { t[0] = a; t[1] = b; t[2] = c; continue; }
		}
		if (sscanf(line, " %63[A-Za-z0-9_] = %lf", key, &v) != 2) continue;
		if (!_stricmp(key, "Enabled")) S.enabled = v != 0;
		else if (!_stricmp(key, "HazeKLow")) S.kLow = v; else if (!_stricmp(key, "HazeHLow")) S.hLow = v;
		else if (!_stricmp(key, "HazeKUp")) S.kUp = v; else if (!_stricmp(key, "HazeHUp")) S.hUp = v;
		else if (!_stricmp(key, "HazeBright")) S.hazeBright = v; else if (!_stricmp(key, "HazeForward")) S.hazeFwd = v;
		else if (!_stricmp(key, "Twilight")) S.twilight = v;
		else if (!_stricmp(key, "HazeRedCap")) S.hazeRedCap = v;
		else if (!_stricmp(key, "HazeLowBright")) S.lowBright = v;
		else if (!_stricmp(key, "HazeLowForward")) S.lowFwd = v;
		else if (!_stricmp(key, "Ground")) S.ground = v; else if (!_stricmp(key, "Top")) S.top = v;
		else if (!_stricmp(key, "StLatMin")) S.stLatMin = v; else if (!_stricmp(key, "StLatMax")) S.stLatMax = v;
		else if (!_stricmp(key, "StBase")) S.stBase = v; else if (!_stricmp(key, "StTop")) S.stTop = v;
		else if (!_stricmp(key, "StCoverage")) S.stCov = v; else if (!_stricmp(key, "StSizeKm")) S.stSize = v;
		else if (!_stricmp(key, "StStretch")) S.stStretch = v; else if (!_stricmp(key, "StLifeH")) S.stLifeH = v;
		else if (!_stricmp(key, "CuLatMin")) S.cuLatMin = v; else if (!_stricmp(key, "CuLatMax")) S.cuLatMax = v;
		else if (!_stricmp(key, "CuBase")) S.cuBase = v; else if (!_stricmp(key, "CuTop")) S.cuTop = v;
		else if (!_stricmp(key, "CuCoverage")) S.cuCov = v; else if (!_stricmp(key, "CuCellKm")) S.cuCell = v;
		else if (!_stricmp(key, "CuAnvil")) S.cuAnvil = v;
		else if (!_stricmp(key, "StormLat")) S.stormLat = v; else if (!_stricmp(key, "StormLon")) S.stormLon = v;
		else if (!_stricmp(key, "StormRadiusKm")) S.stormRadius = v;
		else if (!_stricmp(key, "Density")) S.density = v; else if (!_stricmp(key, "FineKm")) S.fineKm = v;
		else if (!_stricmp(key, "WindKmS")) S.windKmS = v; else if (!_stricmp(key, "FineShare")) S.fineShare = v;
		else if (!_stricmp(key, "SunGain")) S.sunGain = v;
		else if (!_stricmp(key, "Exposure")) S.expo = v;
		else if (!_stricmp(key, "MaxCamAlt")) S.maxCamAlt = v;
		else if (!_stricmp(key, "Steps")) S.steps = (int)v;
		else if (!_stricmp(key, "Downscale")) S.downscale = v < 1 ? 1 : (v > 4 ? 4 : (int)v);
		else if (!_stricmp(key, "BudgetMs")) S.budgetMs = v;
		else if (!_stricmp(key, "NearKm")) S.nearKm = v; else if (!_stricmp(key, "Slices")) S.slices = v < 0 ? 0 : (v > 24 ? 24 : (int)v);
		else if (!_stricmp(key, "FogTau")) S.fogTau = v; else if (!_stricmp(key, "FogTop")) S.fogTop = v;
		else if (!_stricmp(key, "FogLat")) S.fogLat = v; else if (!_stricmp(key, "FogCoverage")) S.fogCov = v;
		else if (!_stricmp(key, "FogPatchKm")) S.fogPatch = v; else if (!_stricmp(key, "FogLifeH")) S.fogLifeH = v;
	}
	fclose(f);
}

bool Init(LPDIRECT3DDEVICE9 dev)
{
	tried = true;
	if (!S.enabled) { TLog("TitanFX: disabled in Config\\TitanFX_Volume.cfg"); return false; }
	LPD3DXBUFFER err = nullptr;
	FILE* pre = fopen("Modules\\D3D9Client\\TitanFX_Volume.fxo", "rb");
	if (pre) fclose(pre);
	HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\TitanFX_Volume.fxo" : "Modules\\D3D9Client\\TitanFX_Volume.fx",
		nullptr, nullptr, 0, nullptr, &fx, &err);
	if (FAILED(hr)) {
		TLog("TitanFX: shader failed: %s", err ? (const char*)err->GetBufferPointer() : "file missing?");
		if (err) err->Release();
		return false;
	}
	hr = D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
		0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &noise);
	if (FAILED(hr)) { TLog("TitanFX: Textures\\AtmoFX\\noise3d.dds failed"); return false; }
	dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &qDisj); dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &qFreq);
	dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &qT0); dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &qT1);
	S.steps = S.steps < 16 ? 16 : (S.steps > 96 ? 96 : S.steps);
	TLog("TitanFX: ready for %s: haze tau %.2f (low %.3f/km H %.0f, up %.3f/km H %.0f); streaks %.0f-%.0f N %.0f-%.0f km cov %.2f; cells %.0f-%.0f km cov %.2f, storm %.0f km; %d steps, 1/%d resolution%s",
		S.body, S.kLow * S.hLow + S.kUp * S.hUp, S.kLow, S.hLow, S.kUp, S.hUp, S.stLatMin, S.stLatMax, S.stBase, S.stTop, S.stCov,
		S.cuBase, S.cuTop, S.cuCov, S.stormRadius, S.steps, S.downscale, S.autoQ ? " (auto)" : "");
	return true;
}

inline VECTOR3 ToLocal(const MATRIX3& R, const VECTOR3& g) { return tmul(R, g); }	// planet frame (global = R * local)

} // namespace

namespace TitanVolume {

bool Render(LPDIRECT3DDEVICE9 dev, OBJHANDLE hObj, Scene* scn)
{
	if (!cfgRead) { S = Settings(); ReadSettings(); cfgRead = true; }	// per session: the launcher keeps the client loaded
	char name[64]; oapiGetObjectName(hObj, name, 64);
	if (_stricmp(name, S.body) != 0) return false;
	if (!tried) ok = Init(dev);
	if (!ok) return false;

	const double R = oapiGetSize(hObj);
	VECTOR3 pg, cg, sg;
	oapiGetGlobalPos(hObj, &pg);
	cg = scn->GetCameraGPos();
	oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sg);
	const double camAlt = length(cg - pg) - R;
	if (camAlt > S.maxCamAlt * 1e3) return false;

	MATRIX3 Rp; oapiGetRotationMatrix(hObj, &Rp);
	const VECTOR3 cam = ToLocal(Rp, cg - pg) * 1e-3;
	const VECTOR3 sun = unit(ToLocal(Rp, sg - pg));

	const D3DXMATRIX& V = *scn->GetViewMatrix();
	const D3DXMATRIX& P = *scn->GetProjectionMatrix();
	QuadVtx q[4];
	const float cx[4] = { -1, 1, -1, 1 }, cy[4] = { 1, 1, -1, -1 };
	for (int i = 0; i < 4; i++) {
		const double vx = cx[i] / P._11, vy = cy[i] / P._22, vz = 1.0;
		VECTOR3 w = _V(vx * V._11 + vy * V._12 + vz * V._13, vx * V._21 + vy * V._22 + vz * V._23, vx * V._31 + vy * V._32 + vz * V._33);
		VECTOR3 r = ToLocal(Rp, w);
		q[i] = { cx[i], cy[i], 0.5f, (float)r.x, (float)r.y, (float)r.z };
	}

	const D3DXVECTOR3 vCam((float)cam.x, (float)cam.y, (float)cam.z), vSun((float)sun.x, (float)sun.y, (float)sun.z);
	auto v3 = [](const double* c) { return D3DXVECTOR3((float)c[0], (float)c[1], (float)c[2]); };
	const D3DXVECTOR4 haze((float)S.kLow, (float)S.hLow, (float)S.kUp, (float)S.hUp);
	const D3DXVECTOR4 hazeP((float)S.hazeBright, (float)S.hazeFwd, (float)S.twilight, (float)S.hazeRedCap);
	const D3DXVECTOR4 stA((float)S.stLatMin, (float)S.stLatMax, (float)S.stBase, (float)S.stTop);
	const D3DXVECTOR4 stB((float)S.stCov, (float)S.stSize, (float)S.stStretch, (float)S.stLifeH);
	const D3DXVECTOR4 cuA((float)S.cuLatMin, (float)S.cuLatMax, (float)S.cuBase, (float)S.cuTop);
	const D3DXVECTOR4 cuB((float)S.cuCov, (float)S.cuCell, (float)S.cuAnvil, 0);
	const double sla = S.stormLat * PI / 180.0, slo = S.stormLon * PI / 180.0;
	const D3DXVECTOR4 storm((float)(cos(sla) * cos(slo)), (float)sin(sla), (float)(cos(sla) * sin(slo)), (float)S.stormRadius);
	const D3DXVECTOR4 wx((float)S.density, (float)S.fineKm, (float)S.windKmS, (float)S.fineShare);
	const D3DXVECTOR3 hl = v3(S.hazeLight), ha = v3(S.hazeAbs), ca3 = v3(S.cloudAlb);
	fx->SetValue("gCam", &vCam, sizeof(vCam));
	fx->SetValue("gSun", &vSun, sizeof(vSun));
	fx->SetFloat("gR", (float)(R * 1e-3));
	fx->SetFloat("gGround", (float)S.ground); fx->SetFloat("gTop", (float)S.top);
	const D3DXVECTOR4 hazeLow((float)S.lowBright, (float)S.lowFwd, 0, 0);
	fx->SetVector("gHaze", &haze); fx->SetVector("gHazeP", &hazeP); fx->SetVector("gHazeLow", &hazeLow);
	fx->SetValue("gHazeLight", &hl, sizeof(hl)); fx->SetValue("gHazeAbs", &ha, sizeof(ha));
	fx->SetVector("gStA", &stA); fx->SetVector("gStB", &stB); fx->SetVector("gCuA", &cuA); fx->SetVector("gCuB", &cuB);
	fx->SetVector("gStorm", &storm); fx->SetVector("gWx", &wx);
	fx->SetValue("gCloudAlb", &ca3, sizeof(ca3)); fx->SetFloat("gSunGain", (float)S.sunGain);
	fx->SetFloat("gExpo", (float)S.expo);
	fx->SetFloat("gTime", (float)fmod(oapiGetSimTime(), 1.0e6));
	fx->SetTexture("tNoise3D", noise);
	// fog at the summer pole: the hemisphere the Sun is over (none near the equinox)
	const double subLat = asin(sun.y) * 180.0 / PI;
	const float summer = subLat > 3.0 ? 1.0f : (subLat < -3.0 ? -1.0f : 0.0f);
	const D3DXVECTOR4 fogA((float)(S.fogTop > 0 ? S.fogTau / S.fogTop : 0), (float)S.fogTop, (float)S.fogLat, (float)S.fogCov);
	const D3DXVECTOR4 fogB((float)S.fogPatch, (float)S.fogLifeH, summer, 0);
	fx->SetVector("gFogA", &fogA); fx->SetVector("gFogB", &fogB);
	// near slices: below ~200 km of camera altitude (the near haze matters only there)
	const VECTOR3 fwdW = _V(V._13, V._23, V._33);
	const VECTOR3 fwd = unit(ToLocal(Rp, fwdW));
	const D3DXVECTOR3 vFwd((float)fwd.x, (float)fwd.y, (float)fwd.z);
	fx->SetValue("gCamFwd", &vFwd, sizeof(vFwd));
	const bool useSlices = S.slices > 0 && camAlt < 200e3;
	const double nearKm = useSlices ? S.nearKm : 0.0;
	fx->SetFloat("gNear", (float)nearKm);
	// fade out above the haze (the clouds are not seen from orbit in visible light)
	const double ca = camAlt * 1e-3;
	fx->SetFloat("gFade", (float)(ca < 0.6 * S.maxCamAlt ? 1.0 : (ca > S.maxCamAlt ? 0.0 : (S.maxCamAlt - ca) / (0.4 * S.maxCamAlt))));

	LPDIRECT3DSURFACE9 rt = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return false;
	D3DSURFACE_DESC rd; rt->GetDesc(&rd);
	const UINT w = (rd.Width + S.downscale - 1) / S.downscale, h = (rd.Height + S.downscale - 1) / S.downscale;
	if (!lowTex || w != lowW || h != lowH) {
		if (lowTex) { lowTex->Release(); lowTex = nullptr; }
		if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &lowTex, nullptr))) {
			rt->Release(); TLog("TitanFX: cannot create the low-res target"); ok = false; return false;
		}
		lowW = w; lowH = h;
	}
	D3DVIEWPORT9 vp; dev->GetViewport(&vp);
	LPDIRECT3DSURFACE9 low = nullptr; lowTex->GetSurfaceLevel(0, &low);
	dev->SetRenderTarget(0, low);
	dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);

	auto draw = [&](const char* tech) {
		fx->SetTechnique(tech);
		UINT np = 0;
		fx->Begin(&np, 0);	// saves and restores device state
		for (UINT p = 0; p < np; p++) {
			fx->BeginPass(p);
			dev->SetFVF(QUAD_FVF);
			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QuadVtx));
			fx->EndPass();
		}
		fx->End();
	};
	bool timing = false;
	if (qT0 && qT1 && qDisj && qFreq) {
		if (qPending) {
			BOOL disj = TRUE; UINT64 f = 0, a = 0, b = 0;
			if (qDisj->GetData(&disj, sizeof disj, 0) == S_OK && qFreq->GetData(&f, sizeof f, 0) == S_OK &&
				qT0->GetData(&a, sizeof a, 0) == S_OK && qT1->GetData(&b, sizeof b, 0) == S_OK) {
				if (!disj && f && b > a) { gpuMsSum += (double)(b - a) * 1000.0 / (double)f; gpuN++; }
				qPending = false;
			}
		}
		if (!qPending) { qDisj->Issue(D3DISSUE_BEGIN); qT0->Issue(D3DISSUE_END); timing = true; }
	}
	fx->SetFloat("gPixAng", (float)(2.0 / (P._22 * h)));
	fx->SetInt("gSteps", S.steps);
	static bool first = true;
	if (first) TLog("TitanFX: first draw (%ux%u), camera alt %.1f km", w, h, ca);
	draw("Clouds");
	dev->SetRenderTarget(0, rt);
	dev->SetViewport(&vp);
	const D3DXVECTOR2 lt(0.5f / w, 0.5f / h);
	fx->SetValue("gLowTexel", &lt, sizeof(lt));
	fx->SetTexture("tLow", lowTex);
	auto zOf = [&](double km) { double z = P._33 + P._43 / (km * 1e3); return (float)(z < 0.0 ? 0.0 : (z > 0.999999 ? 0.999999 : z)); };
	fx->SetFloat("gLayerZ", useSlices ? zOf(nearKm) : 0.0f);		// the march covers only what lies beyond the slices
	draw("Composite");
	if (useSlices) {
		const double d0 = 0.02;										// first slice starts 20 m out
		for (int i = S.slices - 1; i >= 0; i--) {					// far to near
			const double a = i == 0 ? 0.0 : d0 * pow(nearKm / d0, (i - 1) / double(S.slices - 1));
			const double b = d0 * pow(nearKm / d0, i / double(S.slices - 1));
			const D3DXVECTOR2 sl((float)a, (float)b);
			fx->SetValue("gSlice", &sl, sizeof(sl));
			fx->SetFloat("gLayerZ", zOf(b));
			draw("Slice");
		}
	}
	if (first) { TLog("TitanFX: composite issued"); first = false; }
	fx->SetTexture("tLow", nullptr);
	if (timing) { qT1->Issue(D3DISSUE_END); qFreq->Issue(D3DISSUE_END); qDisj->Issue(D3DISSUE_END); qPending = true; }
	low->Release(); rt->Release();

	const double t = oapiGetSysTime();
	if (t - logT > 5.0) {
		logT = t;
		const double ms = gpuN ? gpuMsSum / gpuN : -1.0;
		TLog("TitanFX: camera alt %.2f km, sun %.1f deg over the local horizon, GPU %.2f ms per frame (%d samples), %ux%u, %d steps",
			ca, asin(dotp(sun, unit(cam))) * 180.0 / PI, ms, gpuN, w, h, S.steps);
		if (S.autoQ && ms > 0) {
			int ds = S.downscale;
			if (ms > S.budgetMs * 1.2 && ds < 4) ds++;
			else if (ds > 1 && ms * (double)(ds * ds) / (double)((ds - 1) * (ds - 1)) < S.budgetMs * 0.9) ds--;
			if (ds != S.downscale) { TLog("TitanFX: auto quality -> 1/%d resolution", ds); S.downscale = ds; }
		}
		gpuMsSum = 0; gpuN = 0;
	}
	return true;
}

void Release()
{
	if (fx) { fx->Release(); fx = nullptr; }
	if (noise) { noise->Release(); noise = nullptr; }
	if (lowTex) { lowTex->Release(); lowTex = nullptr; }
	for (LPDIRECT3DQUERY9* qq : { &qDisj, &qFreq, &qT0, &qT1 }) if (*qq) { (*qq)->Release(); *qq = nullptr; }
	qPending = false;
	lowW = lowH = 0;
	tried = ok = false;
	cfgRead = false;
}

}

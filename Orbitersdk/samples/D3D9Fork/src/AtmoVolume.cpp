// AtmoFX fork: Jupiter's cloud layers, ray-marched in a half-resolution pass (see AtmoVolume.h)

#include "AtmoVolume.h"
#include "Scene.h"
#include "Log.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdarg>

namespace {

struct Settings {
	char planet[64] = "Jupiter";
	// layers: base/top altitude [km above the reference radius], density scale (haze: extinction 1/km at its base)
	double hazeBase = 470, hazeTop = 560, hazeExt = 0.004;
	double nh3Base = 445, nh3Top = 475, nh3Dens = 1.0;
	double nh4Base = 390, nh4Top = 415, nh4Dens = 0.8;
	double h2oBase = 330, h2oTop = 370, h2oDens = 0.7;
	double deepAlt = 300;
	double maxCamAlt = 2.0e7;					// pass active below this camera altitude [m]
	double mapKm = 800.0;						// weather map tile [km]
	double noiseKm = 38.0, detailKm = 7.0;		// 3D noise tiles [km]
	double maxDist = 2000.0;					// march range [km]; the stock layer shows beyond
	double expo = 1.0;
	int steps = 72;
	int downscale = 2;							// layers rendered at 1/downscale resolution
	bool autoQ = true;							// adapt the resolution to the GPU budget
	double budgetMs = 3.0;
	double nearKm = 35.0;
	double globalMix = 1.0;						// share of the real map in colour and coverage
	bool globalFlip = false;
	bool lightning = true;						// clouds fade in from the camera over this distance
	bool enabled = true;
	// Io: the Prometheus plume
	char plumeBody[64] = "Io";
	bool plumeOn = true;
	double plumeLng = -155.8, plumeLat = -1.6;		// vent [deg, east longitude]
	double plumeH = 80.0;							// canopy height [km]; deposition ring at 2H
	double plumeExt = 0.0075;						// peak extinction in the shell [1/km]: vertical optical depth ~0.03
	double plumeShell = 3.0;						// shell half-width [km]
	double plumeBright = 1.0;
	double plumeColTop = 70.0;						// column top [km]
	double plumeMaxCamAlt = 6000.0;					// pass active below this camera altitude [km]
};

Settings S;
bool tried = false, ok = false;
LPD3DXEFFECT fx = nullptr;
LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
LPDIRECT3DTEXTURE9 weather = nullptr;
LPDIRECT3DTEXTURE9 globalMap = nullptr;
LPDIRECT3DTEXTURE9 flowDir = nullptr;
LPDIRECT3DTEXTURE9 blueNoise = nullptr;		// 64x64 blue noise: the step jitter (white noise gave coarse grain, IGN gave rings)
LPDIRECT3DTEXTURE9 lowTex = nullptr;
UINT lowW = 0, lowH = 0;
double logT = -1e9;
bool cfgRead = false;
// GPU time of the pass (timestamp queries, read back a frame or more later without stalling)
LPDIRECT3DQUERY9 qDisj = nullptr, qFreq = nullptr, qT0 = nullptr, qT1 = nullptr;
bool qPending = false;
double gpuMsSum = 0; int gpuN = 0;

struct QuadVtx { float x, y, z; float rx, ry, rz; };
const DWORD QUAD_FVF = D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0);


// own log, flushed per line and kept across launcher restarts (Orbiter.log is rewritten and may lose buffered lines)
void VLog(const char* fmt, ...)
{
	char buf[1024];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	oapiWriteLog(buf);
	FILE* f = fopen("AtmoFX_Volume.log", "at");
	if (f) { SYSTEMTIME st; GetLocalTime(&st); fprintf(f, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf); fclose(f); }
}

void ReadSettings()
{
	FILE* f = fopen("Config\\AtmoFX_Volume.cfg", "rt");
	if (!f) return;
	char line[256], key[64];
	double v;
	while (fgets(line, sizeof line, f)) {
		char* c = strchr(line, ';'); if (c) *c = 0;
		if (sscanf(line, " Planet = %63s", key) == 1) { strncpy(S.planet, key, 63); continue; }
		if (sscanf(line, " PlumeBody = %63s", key) == 1) { strncpy(S.plumeBody, key, 63); continue; }
		if (sscanf(line, " Quality = %63s", key) == 1) {		// presets; explicit Steps/Downscale after it override
			if (!_stricmp(key, "low"))         { S.steps = 48; S.downscale = 3; S.autoQ = false; }
			else if (!_stricmp(key, "medium")) { S.steps = 72; S.downscale = 2; S.autoQ = false; }
			else if (!_stricmp(key, "high"))   { S.steps = 96; S.downscale = 1; S.autoQ = false; }
			else                               { S.steps = 72; S.downscale = 2; S.autoQ = true; }
			continue;
		}
		if (sscanf(line, " %63[A-Za-z0-9_] = %lf", key, &v) != 2) continue;
		if (!_stricmp(key, "HazeBase")) S.hazeBase = v; else if (!_stricmp(key, "HazeTop")) S.hazeTop = v; else if (!_stricmp(key, "HazeExt")) S.hazeExt = v;
		else if (!_stricmp(key, "NH3Base")) S.nh3Base = v; else if (!_stricmp(key, "NH3Top")) S.nh3Top = v; else if (!_stricmp(key, "NH3Dens")) S.nh3Dens = v;
		else if (!_stricmp(key, "NH4SHBase")) S.nh4Base = v; else if (!_stricmp(key, "NH4SHTop")) S.nh4Top = v; else if (!_stricmp(key, "NH4SHDens")) S.nh4Dens = v;
		else if (!_stricmp(key, "H2OBase")) S.h2oBase = v; else if (!_stricmp(key, "H2OTop")) S.h2oTop = v; else if (!_stricmp(key, "H2ODens")) S.h2oDens = v;
		else if (!_stricmp(key, "DeepAlt")) S.deepAlt = v;
		else if (!_stricmp(key, "MaxCamAlt")) S.maxCamAlt = v * 1e3;
		else if (!_stricmp(key, "MapKm")) S.mapKm = v;
		else if (!_stricmp(key, "NoiseKm")) S.noiseKm = v;
		else if (!_stricmp(key, "DetailKm")) S.detailKm = v;
		else if (!_stricmp(key, "MaxDist")) S.maxDist = v;
		else if (!_stricmp(key, "Exposure")) S.expo = v;
		else if (!_stricmp(key, "Steps")) S.steps = (int)v;
		else if (!_stricmp(key, "Enabled")) S.enabled = v != 0;
		else if (!_stricmp(key, "PlumeOn")) S.plumeOn = v != 0;
		else if (!_stricmp(key, "PlumeLng")) S.plumeLng = v;
		else if (!_stricmp(key, "PlumeLat")) S.plumeLat = v;
		else if (!_stricmp(key, "PlumeH")) S.plumeH = v;
		else if (!_stricmp(key, "PlumeExt")) S.plumeExt = v;
		else if (!_stricmp(key, "PlumeShell")) S.plumeShell = v;
		else if (!_stricmp(key, "PlumeBright")) S.plumeBright = v;
		else if (!_stricmp(key, "PlumeColTop")) S.plumeColTop = v;
		else if (!_stricmp(key, "PlumeMaxCamAlt")) S.plumeMaxCamAlt = v;
		else if (!_stricmp(key, "Downscale")) S.downscale = v < 1 ? 1 : (v > 4 ? 4 : (int)v);
		else if (!_stricmp(key, "BudgetMs")) S.budgetMs = v;
		else if (!_stricmp(key, "NearFadeKm")) S.nearKm = v < 0 ? 0 : v;
		else if (!_stricmp(key, "GlobalMix")) S.globalMix = v < 0 ? 0 : (v > 1 ? 1 : v);
		else if (!_stricmp(key, "GlobalFlip")) S.globalFlip = v != 0;
		else if (!_stricmp(key, "Lightning")) S.lightning = v != 0;
	}
	fclose(f);
}

bool Init(LPDIRECT3DDEVICE9 dev)
{
	tried = true;
	if (!S.enabled) { VLog("AtmoFX volume: disabled in Config\\AtmoFX_Volume.cfg"); return false; }
	LPD3DXBUFFER err = nullptr;
	VLog("AtmoFX volume: loading the effect");
	// precompiled by fxc (AtmoFX_Volume.fxo, fixed step count) if present: the D3DX9 compiler is slow on these loops
	FILE* pre = fopen("Modules\\D3D9Client\\AtmoFX_Volume.fxo", "rb");
	if (pre) fclose(pre);
	HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\AtmoFX_Volume.fxo" : "Modules\\D3D9Client\\AtmoFX_Volume.fx",
		nullptr, nullptr, 0, nullptr, &fx, &err);
	if (FAILED(hr)) {
		VLog("AtmoFX volume: shader failed: %s", err ? (const char*)err->GetBufferPointer() : "file missing?");
		if (err) err->Release();
		return false;
	}
	VLog("AtmoFX volume: effect %s, loading noise3d", SUCCEEDED(hr) ? "ok" : "FAILED");
	hr = D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
		0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &noise);
	if (FAILED(hr)) { VLog("AtmoFX volume: Textures\\AtmoFX\\noise3d.dds failed"); return false; }
	VLog("AtmoFX volume: loading jup_weather");
	hr = D3DXCreateTextureFromFileExA(dev, "Textures\\AtmoFX\\jup_weather.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
		0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &weather);
	if (FAILED(hr)) { VLog("AtmoFX volume: Textures\\AtmoFX\\jup_weather.dds failed"); return false; }
	VLog("AtmoFX volume: loading jup_global");
	hr = D3DXCreateTextureFromFileExA(dev, "Textures\\AtmoFX\\jup_global.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
		0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &globalMap);
	if (FAILED(hr)) { VLog("AtmoFX volume: jup_global.dds failed - flow map only"); globalMap = nullptr; S.globalMix = 0; }
	VLog("AtmoFX volume: loading jup_flowdir");
	hr = D3DXCreateTextureFromFileExA(dev, "Textures\\AtmoFX\\jup_flowdir.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
		0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &flowDir);
	if (FAILED(hr)) { VLog("AtmoFX volume: jup_flowdir.dds failed - cells not aligned to the map"); flowDir = nullptr; }
	if (FAILED(D3DXCreateTextureFromFileExA(dev, "Textures/AtmoFX/bluenoise64.png", 64, 64, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED,
		D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, nullptr, nullptr, &blueNoise))) { VLog("AtmoFX volume: bluenoise64.png failed - white jitter"); blueNoise = nullptr; }
	VLog("AtmoFX volume: textures done, creating GPU queries");
	dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &qDisj); dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &qFreq);
	dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &qT0); dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &qT1);
	S.steps = S.steps < 16 ? 16 : (S.steps > 128 ? 128 : S.steps);
	VLog("AtmoFX volume: ready for %s: haze %.0f-%.0f, NH3 %.0f-%.0f, NH4SH %.0f-%.0f, H2O %.0f-%.0f km; %d steps, 1/%d resolution%s%s",
		S.planet, S.hazeBase, S.hazeTop, S.nh3Base, S.nh3Top, S.nh4Base, S.nh4Top, S.h2oBase, S.h2oTop, S.steps, S.downscale,
		S.autoQ ? " (auto)" : "", qT0 ? ", GPU timing on" : "");
	return true;
}

inline VECTOR3 ToLocal(const MATRIX3& R, const VECTOR3& g) { return tmul(R, g); }	// planet frame (global = R * local)

} // namespace

namespace AtmoVolume {

bool Render(LPDIRECT3DDEVICE9 dev, OBJHANDLE hObj, Scene* scn, double cloudRot)
{
	if (!cfgRead) { S = Settings(); ReadSettings(); cfgRead = true; }	// per session: the launcher keeps the client loaded
	char name[64]; oapiGetObjectName(hObj, name, 64);
	const bool plumeMode = S.plumeOn && _stricmp(name, S.plumeBody) == 0;
	if (!plumeMode && _stricmp(name, S.planet) != 0) return false;
	if (!tried) ok = Init(dev);
	if (!ok) return false;

	const double R = oapiGetSize(hObj);
	VECTOR3 pg, cg, sg;
	oapiGetGlobalPos(hObj, &pg);
	cg = scn->GetCameraGPos();
	oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sg);
	const double camAlt = length(cg - pg) - R;
	if (camAlt > (plumeMode ? S.plumeMaxCamAlt * 1e3 : S.maxCamAlt)) return false;

	MATRIX3 Rp; oapiGetRotationMatrix(hObj, &Rp);
	const VECTOR3 cam = ToLocal(Rp, cg - pg) * 1e-3;		// km, planet frame
	const VECTOR3 sun = unit(ToLocal(Rp, sg - pg));

	// corner rays: view space -> world (camera-relative global axes) -> planet frame
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
	fx->SetValue("gCam", &vCam, sizeof(D3DXVECTOR3));
	fx->SetValue("gSun", &vSun, sizeof(D3DXVECTOR3));
	fx->SetFloat("gR", (float)(R * 1e-3));
	const float Rk = (float)(R * 1e-3);
	const D3DXVECTOR4 vHaze(Rk + (float)S.hazeBase, Rk + (float)S.hazeTop, (float)S.hazeExt, 0);
	const D3DXVECTOR4 vNH3(Rk + (float)S.nh3Base, Rk + (float)S.nh3Top, (float)S.nh3Dens, 0);
	const D3DXVECTOR4 vNH4(Rk + (float)S.nh4Base, Rk + (float)S.nh4Top, (float)S.nh4Dens, 0);
	const D3DXVECTOR4 vH2O(Rk + (float)S.h2oBase, Rk + (float)S.h2oTop, (float)S.h2oDens, 0);
	fx->SetVector("gHaze", &vHaze); fx->SetVector("gNH3", &vNH3); fx->SetVector("gNH4", &vNH4); fx->SetVector("gH2O", &vH2O);
	fx->SetFloat("gDeep", Rk + (float)S.deepAlt);
	fx->SetFloat("gTime", (float)fmod(oapiGetSimTime(), 1.0e6));
	fx->SetFloat("gMapKm", (float)S.mapKm);
	fx->SetFloat("gNoiseKm", (float)S.noiseKm);
	fx->SetFloat("gDetailKm", (float)S.detailKm);
	fx->SetFloat("gMaxDist", (float)S.maxDist);
	fx->SetFloat("gExpo", (float)S.expo);
	fx->SetTexture("tNoise3D", noise);
	fx->SetTexture("tWeather", weather);
	fx->SetTexture("tGlobal", globalMap ? globalMap : weather);
	fx->SetTexture("tFlowDir", flowDir ? flowDir : weather);
	fx->SetTexture("tBlue", blueNoise);
	fx->SetFloat("gBlueOn", blueNoise ? 1.0f : 0.0f);
	fx->SetFloat("gCloudRot", (float)cloudRot);
	{	// the Great Red Spot from its place on the stock map (u 0.361, v 0.610), in the planet frame now
		const double PIx = 3.14159265358979;
		const D3DXVECTOR4 grs((float)((0.361 - 0.5) * 2 * PIx - cloudRot), (float)((0.5 - 0.610) * PIx), 8000.0f, 5500.0f);
		fx->SetVector("gGRS", &grs);
	}
	fx->SetFloat("gFade", (float)(1.0 - std::min(1.0, std::max(0.0, (camAlt - 0.6 * S.maxCamAlt) / (0.4 * S.maxCamAlt)))));
	fx->SetFloat("gGlobalFlip", S.globalFlip ? 1.0f : 0.0f);
	fx->SetFloat("gGlobalMix", (float)S.globalMix);
	fx->SetFloat("gRealTime", (float)fmod(oapiGetSysTime(), 1.0e5));
	fx->SetFloat("gLightning", S.lightning ? 1.0f : 0.0f);
	if (plumeMode) {
		const double la = S.plumeLat * PI / 180.0, lo = S.plumeLng * PI / 180.0;
		const D3DXVECTOR4 pv((float)(cos(la) * cos(lo)), (float)sin(la), (float)(cos(la) * sin(lo)), Rk);
		const D3DXVECTOR4 pp((float)S.plumeH, (float)S.plumeExt, (float)S.plumeShell, (float)S.plumeBright);
		const D3DXVECTOR4 pc(0.80f, 0.86f, 1.0f, (float)S.plumeColTop);
		fx->SetVector("gPlume", &pv); fx->SetVector("gPlumeP", &pp); fx->SetVector("gPlumeC", &pc);
		fx->SetFloat("gFade", 1.0f);
	}

	static bool first = true;
	if (first) { first = false; VLog("AtmoFX volume: first draw, camera alt %.0f km", camAlt / 1e3); }
	// pass 1: deck into the low-resolution target; pass 2: upsample over the current target
	LPDIRECT3DSURFACE9 rt = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return false;
	D3DSURFACE_DESC rd; rt->GetDesc(&rd);
	const UINT w = (rd.Width + S.downscale - 1) / S.downscale, h = (rd.Height + S.downscale - 1) / S.downscale;
	if (!lowTex || w != lowW || h != lowH) {
		if (lowTex) { lowTex->Release(); lowTex = nullptr; }
		if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &lowTex, nullptr))) {
			rt->Release(); VLog("AtmoFX volume: cannot create the low-res target"); ok = false; return false;
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
	// GPU timing: read the previous measurement if ready (no flush), then time this frame's passes
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
	fx->SetFloat("gPixAng", (float)(2.0 / (P._22 * h)));	// vertical field / pass pixels
	fx->SetInt("gSteps", S.steps);
	fx->SetFloat("gNearKm", (float)(S.nearKm > 0.1 ? S.nearKm : 0.1));
	static int firstDraws = 0;
	if (firstDraws < 1) VLog("AtmoFX volume: drawing the deck pass (%ux%u)", w, h);
	draw(plumeMode ? "Plume" : "Deck");
	if (firstDraws < 1) VLog("AtmoFX volume: deck pass issued");
	dev->SetRenderTarget(0, rt);
	dev->SetViewport(&vp);
	const D3DXVECTOR2 lt(0.5f / w, 0.5f / h);
	fx->SetValue("gLowTexel", &lt, sizeof(lt));
	fx->SetTexture("tLow", lowTex);
	draw("Composite");
	if (firstDraws < 1) { VLog("AtmoFX volume: composite issued"); firstDraws++; }
	fx->SetTexture("tLow", nullptr);
	if (timing) { qT1->Issue(D3DISSUE_END); qFreq->Issue(D3DISSUE_END); qDisj->Issue(D3DISSUE_END); qPending = true; }
	low->Release(); rt->Release();

	const double t = oapiGetSysTime();
	if (t - logT > 5.0) {
		logT = t;
		const double ms = gpuN ? gpuMsSum / gpuN : -1.0;
		VLog("AtmoFX volume: camera alt %.0f km, GPU %.2f ms per frame (%d samples), %ux%u, %d steps", camAlt / 1e3, ms, gpuN, w, h, S.steps);
		if (plumeMode) {
			const double la = S.plumeLat * PI / 180.0, lo = S.plumeLng * PI / 180.0;
			const VECTOR3 vent = _V(cos(la) * cos(lo), sin(la), cos(la) * sin(lo)) * (R + S.plumeH * 0.5e3);
			const VECTOR3 toV = vent - cam * 1e3, fw = unit(ToLocal(Rp, _V(V._13, V._23, V._33)));
			VLog("AtmoFX volume: plume: cam %.1f m alt, vent mid-column %.0f km away, %.0f deg off the view axis, sun %.1f deg over the vent horizon",
				camAlt, length(toV) / 1e3, acos(dotp(unit(toV), fw)) * 180.0 / PI,
				asin(dotp(sun, unit(vent))) * 180.0 / PI);
		}
		// auto quality: coarser when over the budget, finer when well under it
		if (S.autoQ && ms > 0) {
			int ds = S.downscale;
			if (ms > S.budgetMs * 1.2 && ds < 4) ds++;
			else if (ds > 1 && ms * (double)(ds * ds) / (double)((ds - 1) * (ds - 1)) < S.budgetMs * 0.9) ds--;	// predicted cost at the finer resolution fits
			if (ds != S.downscale) { VLog("AtmoFX volume: auto quality -> 1/%d resolution", ds); S.downscale = ds; }
		}
		gpuMsSum = 0; gpuN = 0;
	}
	return true;
}

void Release()
{
	if (fx) { fx->Release(); fx = nullptr; }
	if (noise) { noise->Release(); noise = nullptr; }
	if (weather) { weather->Release(); weather = nullptr; }
	if (globalMap) { globalMap->Release(); globalMap = nullptr; }
	if (flowDir) { flowDir->Release(); flowDir = nullptr; }
	if (blueNoise) { blueNoise->Release(); blueNoise = nullptr; }
	if (lowTex) { lowTex->Release(); lowTex = nullptr; }
	for (LPDIRECT3DQUERY9* q : { &qDisj, &qFreq, &qT0, &qT1 }) if (*q) { (*q)->Release(); *q = nullptr; }
	qPending = false;
	lowW = lowH = 0;
	tried = ok = false;
	cfgRead = false;
}

}

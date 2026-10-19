// AtmoFX fork: the view from under a liquid surface - see LiquidVolume.h and shaders\LiquidVolume.fx
#include "LiquidVolume.h"
#include "Scene.h"
#include "VPlanet.h"
#include <map>
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
	struct LiqPrm { bool on; double level, ext, index; float col[3]; };
	std::map<OBJHANDLE, LiqPrm> prmCache;

	const LiqPrm& Prm(OBJHANDLE hPlanet)
	{
		auto it = prmCache.find(hPlanet);
		if (it != prmCache.end()) return it->second;
		LiqPrm p = { false, 0.0, 0.07, 1.29, { 0.40f, 0.30f, 0.14f } };
		char name[64], path[128];
		oapiGetObjectName(hPlanet, name, 64);
		sprintf_s(path, "GC/%s.atm.cfg", name);
		FILEHANDLE f = oapiOpenFile(path, FILE_IN_ZEROONFAIL, CONFIG);
		if (f) {
			if (oapiReadItem_float(f, (char*)"AtmoFX_LiquidLevel", p.level)) p.on = true;
			oapiReadItem_float(f, (char*)"AtmoFX_LiquidExt", p.ext);
			oapiReadItem_float(f, (char*)"AtmoFX_LiquidIndex", p.index);
			char sb[128];
			if (oapiReadItem_string(f, (char*)"AtmoFX_LiquidColor", sb)) sscanf(sb, "%f %f %f", &p.col[0], &p.col[1], &p.col[2]);
			oapiCloseFile(f, FILE_IN_ZEROONFAIL);
			if (p.on) {
				char buf[200]; sprintf_s(buf, "AtmoFX liquid: [%s] level %.2f m, extinction %.3f 1/m, n %.2f", name, p.level, p.ext, p.index);
				oapiWriteLog(buf);
			}
		}
		if (p.ext < 1e-3) p.ext = 1e-3;
		prmCache[hPlanet] = p;
		return prmCache[hPlanet];
	}

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
	bool failed = false;
	bool submerged = false;

	struct SVtx { float x, y, z; };						// surface grid, camera-relative world [m]
	struct QVtx { float x, y, z, w; };					// fog layer, clip space
	std::vector<SVtx> grid;

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\LiquidVolume.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\LiquidVolume.fxo" : "Modules\\D3D9Client\\LiquidVolume.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX liquid: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &noise))) noise = nullptr;
		oapiWriteLog((char*)"AtmoFX liquid: underwater view on");
		return true;
	}
}

// camera above the level: the liquid surface as a sheet on the level (Fresnel sky + glint) over thin slices of liquid at
// growing depths under it, all depth-tested - whatever lies deeper (the bottom, a leg, a hull) is seen through more liquid,
// and things in the liquid are cut at the waterline. Inside the sheet's radius the terrain shader draws only the lit bottom.
static void RenderSheet(LPDIRECT3DDEVICE9 dev, Scene* scn, OBJHANDLE hP, const LiqPrm& L)
{
	const VECTOR3 cam = scn->GetCameraGPos();
	double rmax = 0;
	if (!LiquidVolume::SheetRadius(hP, cam, &rmax) || !Init(dev)) return;
	VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
	const double R = oapiGetSize(hP), Rl = R + L.level;
	const VECTOR3 rc = cam - pp, up = unit(rc);
	VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
	const VECTOR3 e2 = crossp(up, e1);
	const VECTOR3 C = pp - cam;
	vPlanet* vp = (vPlanet*)scn->GetVisObject(hP);
	if (!vp) return;
	FVECTOR4 gl = vp->GroundLight(), hor = vp->SkyHorizon(), zen = vp->SkyZenith(), lc = vp->LiquidColor();
	if (gl.w <= 0.0f) gl = FVECTOR4(0.6f, 0.55f, 0.5f, 1.0f);
	const D3DXVECTOR4 deep(lc.r * 0.06f * gl.r, lc.g * 0.06f * gl.g, lc.b * 0.06f * gl.b, 0);
	const D3DXVECTOR4 sh(hor.r, hor.g, hor.b, hor.w), sz(zen.r, zen.g, zen.b, 0);
	VECTOR3 sunG; oapiGetGlobalPos(oapiGetObjectByIndex(0), &sunG);
	const VECTOR3 sd = unit(sunG - cam);
	const FVECTOR4 sc = vp->SunLightColor(rc, 1.0);
	const D3DXVECTOR4 sun((float)sd.x, (float)sd.y, (float)sd.z, 0), sunc(sc.r, sc.g, sc.b, 0);
	const D3DXVECTOR3 cen((float)C.x, (float)C.y, (float)C.z);
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetValue("gCentre", &cen, sizeof(cen));
	fx->SetVector("gDeep", &deep); fx->SetVector("gSkyHor", &sh); fx->SetVector("gSkyZen", &sz);
	fx->SetVector("gSunDir", &sun); fx->SetVector("gSunCol", &sunc);
	fx->SetFloat("gIndex", (float)L.index); fx->SetFloat("gExt", (float)L.ext);
	fx->SetFloat("gTime", (float)fmod(oapiGetSimTime(), 1000.0));
	fx->SetTexture("tNoise", noise);
	const int NA = 48, NR = 26;
	auto build = [&](double Rs) {
		grid.clear();
		auto P = [&](double s, double az) {
			const double a = s / Rs;
			const VECTOR3 q = C + (up * cos(a) + (e1 * cos(az) + e2 * sin(az)) * sin(a)) * Rs;
			return SVtx{ (float)q.x, (float)q.y, (float)q.z };
		};
		for (int r = 0; r < NR; r++) {
			const double s0 = r == 0 ? 0.0 : 0.5 * pow(rmax / 0.5, (r - 1) / double(NR - 1));
			const double s1 = 0.5 * pow(rmax / 0.5, r / double(NR - 1));
			for (int a = 0; a < NA; a++) {
				const double a0 = a * 2.0 * PI / NA, a1 = (a + 1) * 2.0 * PI / NA;
				const SVtx v00 = P(s0, a0), v01 = P(s0, a1), v10 = P(s1, a0), v11 = P(s1, a1);
				grid.push_back(v00); grid.push_back(v10); grid.push_back(v11);
				grid.push_back(v00); grid.push_back(v11); grid.push_back(v01);
			}
		}
	};
	UINT np = 0;
	dev->SetFVF(D3DFVF_XYZ);
	// slices, deepest first: each holds the liquid between its depth and the next shallower one
	const double dz[] = { 12.0, 6.0, 3.0, 1.5, 0.7, 0.3, 0.0 };
	fx->SetTechnique("Slice");
	for (int k = 0; k < 6; k++) {
		build(Rl - dz[k]);
		fx->SetFloat("gThick", (float)(dz[k] - dz[k + 1]));
		fx->Begin(&np, 0); fx->BeginPass(0);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(grid.size() / 3), grid.data(), sizeof(SVtx));
		fx->EndPass(); fx->End();
	}
	// the surface itself
	build(Rl);
	fx->SetTechnique("Top");
	fx->Begin(&np, 0); fx->BeginPass(0);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(grid.size() / 3), grid.data(), sizeof(SVtx));
	fx->EndPass(); fx->End();
}

bool LiquidVolume::CameraSubmerged() { return submerged; }

bool LiquidVolume::SheetRadius(OBJHANDLE hPlanet, const VECTOR3& camGlobal, double* radius)
{
	if (!hPlanet || oapiGetObjectType(hPlanet) != OBJTP_PLANET) return false;
	const LiqPrm& L = Prm(hPlanet);
	if (!L.on) return false;
	VECTOR3 pp; oapiGetGlobalPos(hPlanet, &pp);
	const double h = length(camGlobal - pp) - (oapiGetSize(hPlanet) + L.level);	// camera height over the level [m]
	if (h <= 0.0 || h > 1500.0) return false;			// the scene keeps the terrain depth only below ~2 km (Scene: bClearZBuffer)
	if (radius) *radius = std::min(20000.0, 3000.0 + 8.0 * h);
	return true;
}

bool LiquidVolume::Level(OBJHANDLE hPlanet, double* level, double* ext)
{
	if (!hPlanet || oapiGetObjectType(hPlanet) != OBJTP_PLANET) return false;
	const LiqPrm& L = Prm(hPlanet);
	if (!L.on) return false;
	if (level) *level = L.level;
	if (ext) *ext = L.ext;
	return true;
}

void LiquidVolume::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	submerged = false;
	if (!scn) return;
	OBJHANDLE hP = oapiCameraProxyGbody();
	if (!hP || oapiGetObjectType(hP) != OBJTP_PLANET) return;
	const LiqPrm& L = Prm(hP);
	if (!L.on) return;

	const VECTOR3 cam = scn->GetCameraGPos();
	VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
	const double R = oapiGetSize(hP), Rl = R + L.level;
	const VECTOR3 rc = cam - pp;
	const double rcam = length(rc);
	const double depth = Rl - rcam;										// camera depth under the level [m]
	if (depth <= 0.0) { RenderSheet(dev, scn, hP, L); return; }
	double lng, lat, rad; oapiGlobalToEqu(hP, cam, &lng, &lat, &rad);
	const double elev = oapiSurfaceElevation(hP, lng, lat);
	if (elev >= L.level || rcam < R + elev) return;						// over land, or under the ground
	submerged = true;
	if (!Init(dev)) return;

	const VECTOR3 up = rc / rcam;
	VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
	const VECTOR3 e2 = crossp(up, e1);
	const VECTOR3 C = pp - cam;											// planet centre, camera-relative

	// light reaching the camera's depth: the sky light (the terrain's AtmoFX_GroundLight if any) through the column above
	vPlanet* vp = (vPlanet*)scn->GetVisObject(hP);
	FVECTOR4 sky = vp ? vp->GroundLight() : FVECTOR4(0, 0, 0, 0);
	if (sky.w <= 0.0f) { FVECTOR4 sc = vp ? vp->SunLightColor(rc, 1.0) : FVECTOR4(1, 1, 1, 1); sky = FVECTOR4(sc.r * 0.6f, sc.g * 0.6f, sc.b * 0.6f, 1.0f); }
	// organics in the methane absorb the blue: extinction weights by channel (as the terrain shader's view from above)
	const double W[3] = { 0.6, 1.0, 1.8 };
	const D3DXVECTOR4 fogCol((float)(L.col[0] * sky.r * exp(-L.ext * W[0] * 1.3 * depth)), (float)(L.col[1] * sky.g * exp(-L.ext * W[1] * 1.3 * depth)),
		(float)(L.col[2] * sky.b * exp(-L.ext * W[2] * 1.3 * depth)), 1.0f);
	const D3DXVECTOR4 skyCol(sky.r, sky.g, sky.b, 1.0f);

	// ---- surface grid: rings around the nadir, out to where the column has hidden everything
	const double rmax = std::min(20000.0, 8.0 / L.ext + 50.0);
	const int NA = 48, NR = 28;
	grid.clear();
	auto P = [&](double s, double az) {										// ground distance s along the level sphere, azimuth az
		const double a = s / Rl;
		const VECTOR3 d = up * cos(a) + (e1 * cos(az) + e2 * sin(az)) * sin(a);
		const VECTOR3 q = C + d * Rl;
		return SVtx{ (float)q.x, (float)q.y, (float)q.z };
	};
	for (int r = 0; r < NR; r++) {
		const double s0 = r == 0 ? 0.0 : 0.5 * pow(rmax / 0.5, (r - 1) / double(NR - 1));
		const double s1 = 0.5 * pow(rmax / 0.5, r / double(NR - 1));
		for (int a = 0; a < NA; a++) {
			const double a0 = a * 2.0 * PI / NA, a1 = (a + 1) * 2.0 * PI / NA;
			const SVtx v00 = P(s0, a0), v01 = P(s0, a1), v10 = P(s1, a0), v11 = P(s1, a1);
			grid.push_back(v00); grid.push_back(v10); grid.push_back(v11);
			grid.push_back(v00); grid.push_back(v11); grid.push_back(v01);
		}
	}

	const D3DXMATRIX& P4 = *scn->GetProjectionMatrix();
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	const D3DXVECTOR3 cen((float)C.x, (float)C.y, (float)C.z);
	fx->SetValue("gCentre", &cen, sizeof(cen));
	fx->SetVector("gFog", &fogCol);
	fx->SetVector("gSky", &skyCol);
	fx->SetFloat("gIndex", (float)L.index);
	fx->SetFloat("gExt", (float)L.ext);
	fx->SetFloat("gTime", (float)fmod(oapiGetSimTime(), 1000.0));
	fx->SetTexture("tNoise", noise);

	UINT np = 0;
	fx->SetTechnique("Surface");
	dev->SetFVF(D3DFVF_XYZ);
	fx->Begin(&np, 0); fx->BeginPass(0);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(grid.size() / 3), grid.data(), sizeof(SVtx));
	fx->EndPass(); fx->End();

	// ---- the column: fog layers at growing view depths; each covers what lies behind it (depth test LessEqual)
	const int NL = 12;
	const double z0 = 0.3, z1 = 7.0 / L.ext;
	double zPrev = 0.0;
	fx->SetTechnique("Layer");
	dev->SetFVF(D3DFVF_XYZW);
	fx->Begin(&np, 0); fx->BeginPass(0);
	for (int i = 0; i < NL; i++) {
		const double z = z0 * pow(z1 / z0, i / double(NL - 1));
		float a[3];
		for (int c = 0; c < 3; c++) a[c] = (float)(1.0 - exp(-L.ext * W[c] * (z - zPrev)));
		zPrev = z;
		const float zn = (float)(P4._33 + P4._43 / z);
		if (zn <= 0.0f || zn >= 1.0f) continue;
		const QVtx q[4] = { { -1, -1, zn, 1 }, { -1, 1, zn, 1 }, { 1, -1, zn, 1 }, { 1, 1, zn, 1 } };
		const D3DXVECTOR4 a4(a[0], a[1], a[2], 1.0f);
		fx->SetVector("gAlpha3", &a4);
		fx->CommitChanges();
		dev->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_COLORVALUE(a[0], a[1], a[2], 1.0f));	// per-channel opacity
		dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QVtx));
	}
	// and everything beyond the last layer (the empty background too)
	{
		const QVtx q[4] = { { -1, -1, 0.99999f, 1 }, { -1, 1, 0.99999f, 1 }, { 1, -1, 0.99999f, 1 }, { 1, 1, 0.99999f, 1 } };
		const D3DXVECTOR4 a4(1, 1, 1, 1);
		fx->SetVector("gAlpha3", &a4);
		fx->CommitChanges();
		dev->SetRenderState(D3DRS_BLENDFACTOR, 0xFFFFFFFF);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QVtx));
	}
	fx->EndPass(); fx->End();
	dev->SetRenderState(D3DRS_BLENDFACTOR, 0xFFFFFFFF);
}

void LiquidVolume::Release()
{
	if (noise) { noise->Release(); noise = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	prmCache.clear();
}

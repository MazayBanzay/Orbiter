// AtmoFX fork: heat shimmer - see HeatShimmer.h and shaders\HeatShimmer.fx
#include "HeatShimmer.h"
#include "Scene.h"
#include "LiquidVolume.h"
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
	struct HeatNode { OBJHANDLE hv; int id; VECTOR3 pos; double r, dT; };
	std::vector<HeatNode> nodes;					// explicit hot nodes (AtmoFX_SetHeatSource)

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
	LPDIRECT3DTEXTURE9 copyTex = nullptr;		// the scene so far, sampled through the warm air
	UINT copyW = 0, copyH = 0; D3DFORMAT copyF = D3DFMT_UNKNOWN;
	bool failed = false;
	const double MAX_DIST = 3000.0;				// [m] beyond this the shimmer is below a pixel
	const float MAX_PX = 7.0f;					// strongest displacement [px] for a source filling ~150 px

	struct Vtx { float x, y, z, u, v; };
	const DWORD FVF = D3DFVF_XYZ | D3DFVF_TEX1;

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\HeatShimmer.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\HeatShimmer.fxo" : "Modules\\D3D9Client\\HeatShimmer.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX heat shimmer: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &noise))) {
			oapiWriteLog((char*)"AtmoFX heat shimmer: Textures\\AtmoFX\\noise3d.dds missing"); fx->Release(); fx = nullptr; failed = true; return false;
		}
		oapiWriteLog((char*)"AtmoFX heat shimmer: on");
		return true;
	}

	// relative strength 0..1 from the ambient air: the deflection ~ density * dT / T
	double Strength(VESSEL* v, double dT)
	{
		const double rho = v->GetAtmDensity(), T = v->GetAtmTemperature();
		if (rho < 1e-3 || T < 10.0) return 0.0;
		const double s = (rho / 1.225) * (dT / T) * 0.5;
		return s > 1.0 ? 1.0 : s;
	}

	struct Quad { VECTOR3 a, b; double w0, w1, s, speed; int kind; float seed; };	// a -> b along the axis, half-widths at a and b, strength, rise [axis lengths/s], 0 exhaust 1 node
}

void AtmoFX_SetHeatSource(OBJHANDLE hVessel, int id, double x, double y, double z, double radius, double dT)
{
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i].hv == hVessel && nodes[i].id == id) {
			if (dT <= 0.0 || radius <= 0.0) nodes.erase(nodes.begin() + i);
			else nodes[i] = { hVessel, id, _V(x, y, z), radius, dT };
			return;
		}
	}
	if (dT > 0.0 && radius > 0.0) nodes.push_back({ hVessel, id, _V(x, y, z), radius, dT });
}

void HeatShimmer::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn || LiquidVolume::CameraSubmerged()) return;	// no air shimmer under a liquid
	const VECTOR3 cam = scn->GetCameraGPos();

	// collect the sources (camera-relative world frame, metres)
	std::vector<Quad> quads;
	for (size_t i = 0; i < nodes.size();) {
		if (!oapiIsVessel(nodes[i].hv)) { nodes.erase(nodes.begin() + i); continue; }
		i++;
	}
	for (const HeatNode& n : nodes) {
		VESSEL* v = oapiGetVesselInterface(n.hv);
		VECTOR3 g; v->Local2Global(n.pos, g);
		const VECTOR3 c = g - cam;
		if (length(c) > MAX_DIST + n.r) continue;
		const double s = Strength(v, n.dT);
		if (s < 0.01) continue;
		VECTOR3 pp; oapiGetGlobalPos(v->GetSurfaceRef(), &pp);
		const VECTOR3 up = unit(g - pp);								// warm air rises
		quads.push_back({ c - up * (0.5 * n.r), c + up * (4.0 * n.r), 1.3 * n.r, 1.6 * n.r, s, 1.5 / (4.5 * n.r), 1, (float)fmod(n.id * 0.6180339 + (size_t)n.hv * 1e-3, 1.0) });
	}
	const DWORD nv = oapiGetVesselCount();
	for (DWORD k = 0; k < nv; k++) {
		OBJHANDLE h = oapiGetVesselByIndex(k);
		VESSEL* v = oapiGetVesselInterface(h);
		VECTOR3 vg; v->GetGlobalPos(vg);
		if (length(vg - cam) > MAX_DIST + v->GetSize()) continue;
		const DWORD ne = v->GetExhaustCount();
		for (DWORD e = 0; e < ne; e++) {
			const double lvl = v->GetExhaustLevel(e);
			if (lvl <= 0.0) continue;
			EXHAUSTSPEC es; v->GetExhaustSpec(e, &es);
			const double s = Strength(v, 1500.0 * lvl);					// hot gas at the exit, mixing down the jet
			if (s < 0.01) continue;
			VECTOR3 dir = -(*es.ldir); normalise(dir);					// down the jet
			const VECTOR3 o = (*es.lpos) - (*es.ldir) * es.lofs;
			VECTOR3 og, dg; v->Local2Global(o, og); v->GlobalRot(dir, dg);
			const VECTOR3 a = og - cam;
			const double L = 2.0 * es.lsize;								// the warm wake outlasts the visible flame
			quads.push_back({ a, a + dg * L, 1.2 * es.wsize, 3.0 * es.wsize, s, 0.9 + 1.5 * lvl, 0, (float)fmod(k * 0.381966 + e * 0.6180339, 1.0) });
		}
	}
	if (quads.empty() || !Init(dev)) return;

	// copy the scene drawn so far
	LPDIRECT3DSURFACE9 rt = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
	D3DSURFACE_DESC rd; rt->GetDesc(&rd);
	if (!copyTex || rd.Width != copyW || rd.Height != copyH || rd.Format != copyF) {
		if (copyTex) { copyTex->Release(); copyTex = nullptr; }
		if (FAILED(dev->CreateTexture(rd.Width, rd.Height, 1, D3DUSAGE_RENDERTARGET, rd.Format, D3DPOOL_DEFAULT, &copyTex, nullptr))) {
			rt->Release(); copyTex = nullptr; return;
		}
		copyW = rd.Width; copyH = rd.Height; copyF = rd.Format;
	}
	LPDIRECT3DSURFACE9 cs = nullptr; copyTex->GetSurfaceLevel(0, &cs);
	dev->StretchRect(rt, nullptr, cs, nullptr, D3DTEXF_NONE);
	cs->Release(); rt->Release();

	const D3DXMATRIX& VP = *scn->GetProjectionViewMatrix();
	const D3DXMATRIX& P = *scn->GetProjectionMatrix();
	const D3DXVECTOR2 inv(1.0f / copyW, 1.0f / copyH);
	fx->SetTechnique("Shimmer");
	fx->SetMatrix("gVP", &VP);
	fx->SetValue("gInvView", &inv, sizeof(inv));
	fx->SetTexture("tScene", copyTex);
	fx->SetTexture("tNoise", noise);
	fx->SetFloat("gTime", (float)fmod(oapiGetSimTime(), 1000.0));
	dev->SetFVF(FVF);
	UINT np = 0;
	fx->Begin(&np, 0);
	fx->BeginPass(0);
	for (const Quad& q : quads) {
		const VECTOR3 ax = q.b - q.a, mid = (q.a + q.b) * 0.5;
		const double dist = length(mid);
		if (dist < 1e-3) continue;
		VECTOR3 side = crossp(ax, mid);								// cylindrical billboard: axis kept, faces the camera
		const double sl = length(side);
		if (sl < 1e-9) continue;
		side = side / sl;
		const double face = sl / (length(ax) * dist);					// 1 seen from the side, 0 along the axis
		const double px = q.w1 / dist * P._22 * 0.5 * copyH;			// projected half-width [px]
		const float amp = (float)(MAX_PX * q.s * (px / 150.0 > 1.0 ? 1.0 : (px / 150.0 < 0.15 ? 0.15 : px / 150.0)) * (0.3 + 0.7 * face));
		if (amp < 0.2f) continue;
		const VECTOR3 a0 = q.a - side * q.w0, a1 = q.a + side * q.w0, b0 = q.b - side * q.w1, b1 = q.b + side * q.w1;
		const Vtx t[4] = {
			{ (float)a0.x, (float)a0.y, (float)a0.z, 0, 0 }, { (float)a1.x, (float)a1.y, (float)a1.z, 1, 0 },
			{ (float)b0.x, (float)b0.y, (float)b0.z, 0, 1 }, { (float)b1.x, (float)b1.y, (float)b1.z, 1, 1 } };
		const D3DXVECTOR4 prm(amp, (float)q.speed, (float)q.kind, q.seed);
		fx->SetVector("gPrm", &prm);
		fx->CommitChanges();
		dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, t, sizeof(Vtx));
	}
	fx->EndPass();
	fx->End();
	fx->SetTexture("tScene", nullptr);
}

void HeatShimmer::Release()
{
	if (copyTex) { copyTex->Release(); copyTex = nullptr; }
	if (noise) { noise->Release(); noise = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	copyW = copyH = 0; failed = false;
	nodes.clear();
}

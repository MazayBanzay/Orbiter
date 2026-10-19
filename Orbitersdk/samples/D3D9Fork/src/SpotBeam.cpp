// AtmoFX fork: spotlight beams and their glints on liquids - see SpotBeam.h and shaders\SpotBeam.fx
#include "SpotBeam.h"
#include "LiquidVolume.h"
#include "Scene.h"
#include "VPlanet.h"
#include <map>
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
	LPD3DXEFFECT fx = nullptr;
	bool failed = false;
	std::map<OBJHANDLE, double> scatterCache;		// AtmoFX_BeamScatter per body (-1: not set)
	struct Vtx { float x, y, z; };
	std::vector<Vtx> vb;

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\SpotBeam.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\SpotBeam.fxo" : "Modules\\D3D9Client\\SpotBeam.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX beams: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		oapiWriteLog((char*)"AtmoFX beams: on");
		return true;
	}

	double BodyScatter(OBJHANDLE hP)
	{
		auto it = scatterCache.find(hP);
		if (it != scatterCache.end()) return it->second;
		double k = -1.0;
		char name[64], path[128];
		oapiGetObjectName(hP, name, 64);
		sprintf_s(path, "GC/%s.atm.cfg", name);
		FILEHANDLE f = oapiOpenFile(path, FILE_IN_ZEROONFAIL, CONFIG);
		if (f) { oapiReadItem_float(f, (char*)"AtmoFX_BeamScatter", k); oapiCloseFile(f, FILE_IN_ZEROONFAIL); }
		scatterCache[hP] = k;
		return k;
	}

	// a cone mesh (apex at the lamp, base at the range) in camera-relative world coordinates
	void Cone(const VECTOR3& a, const VECTOR3& d, double L, double R)
	{
		VECTOR3 e1 = crossp(d, fabs(d.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(d, e1);
		const int N = 24;
		const VECTOR3 b = a + d * L;
		for (int i = 0; i < N; i++) {
			const double t0 = i * 2 * PI / N, t1 = (i + 1) * 2 * PI / N;
			const VECTOR3 p0 = b + (e1 * cos(t0) + e2 * sin(t0)) * R, p1 = b + (e1 * cos(t1) + e2 * sin(t1)) * R;
			const Vtx va = { (float)a.x, (float)a.y, (float)a.z }, v0 = { (float)p0.x, (float)p0.y, (float)p0.z }, v1 = { (float)p1.x, (float)p1.y, (float)p1.z };
			const Vtx vb2 = { (float)b.x, (float)b.y, (float)b.z };
			vb.push_back(va); vb.push_back(v0); vb.push_back(v1);			// side
			vb.push_back(vb2); vb.push_back(v1); vb.push_back(v0);			// base cap
		}
	}
}

void SpotBeam::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn || LiquidVolume::CameraSubmerged()) return;
	const D3D9Light* Ls = scn->GetLights();
	const DWORD nL = scn->GetLightCount();
	if (!Ls || !nL) return;
	OBJHANDLE hP = oapiCameraProxyGbody();
	if (!hP || oapiGetObjectType(hP) != OBJTP_PLANET) return;

	// scattering coefficient of the air at the camera
	const VECTOR3 cam = scn->GetCameraGPos();
	VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
	const double R = oapiGetSize(hP), alt = length(cam - pp) - R;
	ATMPARAM ap; oapiGetPlanetAtmParams(hP, length(cam - pp), &ap);
	double ks = BodyScatter(hP);
	if (ks < 0.0) ks = 1.2e-5 * ap.rho / 1.225;							// clear air: Rayleigh at ~550 nm, scaled by density
	if (ks < 1e-8 || alt > 30000.0) return;									// vacuum (the Moon): no beam

	// the liquid level for the glints
	double level = 0, ext = 0;
	const bool liquid = LiquidVolume::Level(hP, &level, &ext);
	const VECTOR3 upW = unit(cam - pp);
	const double hCam = length(cam - pp) - (R + level);

	if (!Init(dev)) return;
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetFloat("gKs", (float)ks);
	dev->SetFVF(D3DFVF_XYZ);
	UINT np = 0;
	for (DWORD i = 0; i < nL; i++) {
		const D3D9Light& Li = Ls[i];
		if (Li.Type != 1) continue;
		const VECTOR3 a = _V(Li.Position.x, Li.Position.y, Li.Position.z);
		VECTOR3 d = _V(Li.Direction.x, Li.Direction.y, Li.Direction.z); normalise(d);
		const double range = std::min((double)Li.Param[D3D9LRange], 400.0);
		if (range < 1.0 || length(a) > range + 2000.0) continue;
		const double cosp = Li.Param[D3D9LPhi], cosu = cosp + (Li.Param[D3D9LTheta] > 0 ? 1.0 / Li.Param[D3D9LTheta] : 0.05);
		const double tanp = sqrt(std::max(0.0, 1.0 - cosp * cosp)) / std::max(cosp, 0.05);
		const D3DXVECTOR4 lp((float)a.x, (float)a.y, (float)a.z, (float)range), ld((float)d.x, (float)d.y, (float)d.z, 0);
		const D3DXVECTOR4 lc(Li.Diffuse.r, Li.Diffuse.g, Li.Diffuse.b, 0), la(Li.Attenuation.x, Li.Attenuation.y, Li.Attenuation.z, 0);
		const D3DXVECTOR4 cone((float)cosp, (float)std::min(cosu, 0.9999), (float)(liquid ? R + level : 0.0), 0);
		const D3DXVECTOR4 pc((float)(pp.x - cam.x), (float)(pp.y - cam.y), (float)(pp.z - cam.z), 0);
		fx->SetVector("gLPos", &lp); fx->SetVector("gLDir", &ld); fx->SetVector("gLCol", &lc); fx->SetVector("gLAtt", &la);
		fx->SetVector("gCone", &cone); fx->SetVector("gPlanet", &pc);
		// the beam: camera inside the cone -> draw its far side, else its near side
		const VECTOR3 toCam = -a;
		const double dl = length(toCam);
		const bool inside = dl < 1e-3 || (dotp(toCam, d) / dl > cosp && dotp(toCam, d) < range);
		vb.clear();
		Cone(a, d, range, range * tanp);
		fx->SetTechnique(inside ? "BeamInside" : "Beam");
		fx->Begin(&np, 0); fx->BeginPass(0);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(vb.size() / 3), vb.data(), sizeof(Vtx));
		fx->EndPass(); fx->End();

		// the glint on the liquid: the lamp's mirror image under the level (camera and lamp above it)
		if (liquid && hCam > 0.0 && hCam < 1500.0) {
			const VECTOR3 aG = a + cam;											// lamp, global
			const double hL = length(aG - pp) - (R + level);
			if (hL > 0.0 && hL < 50.0) {
				const VECTOR3 upL = unit(aG - pp);
				const VECTOR3 img = a - upL * (2.0 * hL);						// mirror image, camera-relative
				const double dist = length(img);
				// where the path meets the surface, and whether the lamp shines that way
				const double s = hCam / (hCam + hL);								// fraction of the way camera -> image
				const VECTOR3 S = img * s;
				const VECTOR3 toS = unit(S - a);
				const double cs = dotp(toS, d);
				const double spot = cs <= cosp ? 0.0 : std::min(1.0, (cs - cosp) / std::max(cosu - cosp, 1e-3));
				if (spot > 0.0 && dist < 3000.0) {
					const double cosV = dotp(unit(-S), upL);
					const double f0 = 0.016, F = f0 + (1 - f0) * pow(1.0 - std::max(0.0, cosV), 5.0);
					const double rS = length(S - a), att = 1.0 / (Li.Attenuation.x + Li.Attenuation.y * rS + Li.Attenuation.z * rS * rS);
					const double I = spot * F * std::min(att, 50.0) * 0.02;		// a small bright lamp image
					const double sz = std::max(0.12, dist * 0.004);
					const VECTOR3 V = unit(img);
					VECTOR3 sx = crossp(V, upL); normalise(sx);
					const VECTOR3 sy = crossp(sx, V);
					const VECTOR3 q0 = img - sx * sz - sy * sz * 2.5, q1 = img + sx * sz - sy * sz * 2.5, q2 = img - sx * sz + sy * sz * 2.5, q3 = img + sx * sz + sy * sz * 2.5;
					const Vtx g[6] = { { (float)q0.x, (float)q0.y, (float)q0.z }, { (float)q1.x, (float)q1.y, (float)q1.z }, { (float)q2.x, (float)q2.y, (float)q2.z },
									   { (float)q2.x, (float)q2.y, (float)q2.z }, { (float)q1.x, (float)q1.y, (float)q1.z }, { (float)q3.x, (float)q3.y, (float)q3.z } };
					const D3DXVECTOR4 gc((float)(Li.Diffuse.r * I), (float)(Li.Diffuse.g * I), (float)(Li.Diffuse.b * I), 0);
					const D3DXVECTOR4 gm((float)img.x, (float)img.y, (float)img.z, (float)sz);
					fx->SetVector("gGlint", &gc); fx->SetVector("gGlintPos", &gm);
					fx->SetTechnique("Glint");
					fx->Begin(&np, 0); fx->BeginPass(0);
					dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, g, sizeof(Vtx));
					fx->EndPass(); fx->End();
				}
			}
		}
	}
}

void SpotBeam::Release()
{
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	scatterCache.clear();
}

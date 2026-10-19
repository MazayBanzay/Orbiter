// AtmoFX fork: wakes on a liquid surface - see LiquidWake.h and shaders\LiquidWake.fx
#include "LiquidWake.h"
#include "LiquidVolume.h"
#include "Scene.h"
#include "VPlanet.h"
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
	struct WakeNode { OBJHANDLE hv; int id; VECTOR3 pos; double r, s; };
	std::vector<WakeNode> nodes;
	struct Impulse { OBJHANDLE hBody; VECTOR3 p; double r, amp, t0; };	// p: planet frame on the level
	std::vector<Impulse> impulses;
	std::vector<std::pair<OBJHANDLE, int>> pendingImpulse;				// (vessel, id) whose next frame makes an impulse
	std::vector<double> pendingAmp;

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DTEXTURE9 kelvin = nullptr;
	bool failed = false;

	struct Vtx { float x, y, z, u, v; };

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\LiquidWake.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\LiquidWake.fxo" : "Modules\\D3D9Client\\LiquidWake.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX wake: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateTextureFromFileExA(dev, "Textures/AtmoFX/kelvin_wake.png", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8,
			D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &kelvin))) {
			oapiWriteLog((char*)"AtmoFX wake: Textures/AtmoFX/kelvin_wake.png missing"); fx->Release(); fx = nullptr; failed = true; return false;
		}
		oapiWriteLog((char*)"AtmoFX wake: on");
		return true;
	}

	void Quad(std::vector<Vtx>& vb, const VECTOR3& c, const VECTOR3& ax, const VECTOR3& ay, double u0, double u1, double v0, double v1)
	{	// c + ax*(u) + ay*(v) over [u0,u1] x [v0,v1], uv = the same numbers
		auto P = [&](double u, double v) { VECTOR3 q = c + ax * u + ay * v; return Vtx{ (float)q.x, (float)q.y, (float)q.z, (float)u, (float)v }; };
		const Vtx a = P(u0, v0), b = P(u1, v0), d = P(u0, v1), e = P(u1, v1);
		vb.push_back(a); vb.push_back(b); vb.push_back(d); vb.push_back(d); vb.push_back(b); vb.push_back(e);
	}
}

void AtmoFX_SetWake(OBJHANDLE hVessel, int id, const double* pos, double radius, double strength)
{
	if (strength > 1.0) { pendingImpulse.push_back({ hVessel, id }); pendingAmp.push_back(strength); }
	const double s = strength > 1.0 ? 1.0 : strength;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i].hv == hVessel && nodes[i].id == id) {
			if (s <= 0.0 || radius <= 0.0) nodes.erase(nodes.begin() + i);
			else nodes[i] = { hVessel, id, _V(pos[0], pos[1], pos[2]), radius, s };
			return;
		}
	}
	if (s > 0.0 && radius > 0.0) nodes.push_back({ hVessel, id, _V(pos[0], pos[1], pos[2]), radius, s });
}

void LiquidWake::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn || LiquidVolume::CameraSubmerged()) return;
	for (size_t i = 0; i < nodes.size();) { if (!oapiIsVessel(nodes[i].hv)) { nodes.erase(nodes.begin() + i); continue; } i++; }
	const double simt = oapiGetSimTime();
	for (size_t i = 0; i < impulses.size();) { if (simt - impulses[i].t0 > 40.0 || simt < impulses[i].t0) { impulses.erase(impulses.begin() + i); continue; } i++; }
	if (nodes.empty() && impulses.empty() && pendingImpulse.empty()) return;

	const VECTOR3 cam = scn->GetCameraGPos();
	struct Draw { std::vector<Vtx> vb; int mode; D3DXVECTOR4 a, b, u; };	// u: up; b: motion (mode 0) or centre (rings)
	std::vector<Draw> draws;
	OBJHANDLE hBody = nullptr;

	auto levelPoint = [&](OBJHANDLE hP, const VECTOR3& g, VECTOR3& onLevel, double& depthBelow) {
		double level = 0, ext = 0;
		if (!LiquidVolume::Level(hP, &level, &ext)) return false;
		VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
		const double Rl = oapiGetSize(hP) + level;
		const VECTOR3 rv = g - pp;
		depthBelow = Rl - length(rv);
		onLevel = pp + unit(rv) * Rl;
		return true;
	};

	for (const WakeNode& n : nodes) {
		VESSEL* v = oapiGetVesselInterface(n.hv);
		const OBJHANDLE hP = v->GetSurfaceRef();
		VECTOR3 g; v->Local2Global(n.pos, g);
		VECTOR3 P; double dep;
		if (!levelPoint(hP, g, P, dep) || dep < -n.r) continue;				// not in the liquid
		hBody = hP;
		VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
		const VECTOR3 up = unit(P - pp);
		VECTOR3 vg; v->GetGroundspeedVector(FRAME_GLOBAL, vg);
		VECTOR3 vh = vg - up * dotp(vg, up);
		const double U = length(vh);
		const double gacc = GGRAV * oapiGetMass(hP) / dotp(P - pp, P - pp);
		const VECTOR3 c = P - cam;
		if (length(c) > 3000.0) continue;
		// pending impulse of this node: expanding rings from here
		for (size_t k = 0; k < pendingImpulse.size(); k++)
			if (pendingImpulse[k].first == n.hv && pendingImpulse[k].second == n.id) {
				MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
				impulses.push_back({ hP, tmul(Rp, P - pp), n.r, std::min(pendingAmp[k], 4.0), simt });
				pendingImpulse.erase(pendingImpulse.begin() + k); pendingAmp.erase(pendingAmp.begin() + k); k--;
			}
		VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(up, e1);
		// Kelvin wake (gravity waves need more than the minimum phase speed of ~0.11 m/s on liquid methane)
		if (U > 0.15) {
			const VECTOR3 f = vh / U, side = crossp(up, f);
			const double lam = 2.0 * PI * U * U / gacc;								// transverse wavelength [m]
			const double Lx = 32.0 * lam, Wy = 13.0 * lam;
			Draw d; d.mode = 0;
			Quad(d.vb, c + up * 0.02, -f, side, 0.0, Lx, -Wy, Wy);				// u: behind [m], v: lateral [m]
			// slope amplitude: grows with how the body size compares with the wavelength (Froude), and with the strength
			const double amp = 0.10 * n.s * std::min(1.0, 2.5 * n.r / lam) * std::min(1.0, (U - 0.15) / 0.3);
			d.a = D3DXVECTOR4((float)lam, (float)amp, (float)n.r, 0); d.b = D3DXVECTOR4((float)f.x, (float)f.y, (float)f.z, 0);
			d.u = D3DXVECTOR4((float)up.x, (float)up.y, (float)up.z, 0);
			draws.push_back(d);
		}
		// rings around the part: capillary-gravity ripples of a bobbing body, ~0.3-1 m wavelength
		{
			const double R = std::max(6.0 * n.r, 3.0);
			Draw d; d.mode = 1;
			Quad(d.vb, c + up * 0.02, e1, e2, -R, R, -R, R);
			d.a = D3DXVECTOR4((float)std::max(0.35, 0.6 * n.r), (float)(0.06 * n.s), (float)n.r, (float)fmod(simt, 1000.0));
			d.b = D3DXVECTOR4((float)c.x, (float)c.y, (float)c.z, 0);
			d.u = D3DXVECTOR4((float)up.x, (float)up.y, (float)up.z, 0);
			draws.push_back(d);
		}
	}
	// expanding rings after impulses: a gravity-wave packet, wavelength ~ the part, group speed half the phase speed
	for (const Impulse& im : impulses) {
		hBody = im.hBody;
		MATRIX3 Rp; oapiGetRotationMatrix(im.hBody, &Rp);
		VECTOR3 pp; oapiGetGlobalPos(im.hBody, &pp);
		const VECTOR3 P = pp + mul(Rp, im.p), up = unit(P - pp), c = P - cam;
		if (length(c) > 3000.0) continue;
		const double gacc = GGRAV * oapiGetMass(im.hBody) / dotp(P - pp, P - pp);
		const double lam = std::max(im.r < 0.1 ? 0.04 : 0.5, 2.0 * im.r);		// drops: capillary rings of a few cm
		const double cg = 0.5 * sqrt(gacc * lam / (2.0 * PI));
		const double t = simt - im.t0, rf = im.r + cg * t;
		VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(up, e1);
		const double R = rf + 2.0 * lam;
		Draw d; d.mode = 2;
		Quad(d.vb, c + up * 0.02, e1, e2, -R, R, -R, R);
		const double amp = 0.12 * im.amp * sqrt(im.r / std::max(rf, im.r)) * exp(-t / std::min(25.0, 2.0 + 20.0 * im.r));	// small splashes die out in seconds
		d.a = D3DXVECTOR4((float)lam, (float)amp, (float)rf, (float)t); d.b = D3DXVECTOR4((float)c.x, (float)c.y, (float)c.z, 0);
		d.u = D3DXVECTOR4((float)up.x, (float)up.y, (float)up.z, 0);
		draws.push_back(d);
	}
	if (draws.empty() || !hBody || !Init(dev)) return;

	// the surface's own light: sky reflected (AtmoFX_SkyHorizon/Zenith), deep liquid colour (AtmoFX_LiquidColor x GroundLight)
	vPlanet* vp = (vPlanet*)scn->GetVisObject(hBody);
	if (!vp) return;
	const FVECTOR4 hor = vp->SkyHorizon(), zen = vp->SkyZenith(), lc = vp->LiquidColor(), gl = vp->GroundLight();
	const D3DXVECTOR4 sh(hor.r, hor.g, hor.b, 0), sz(zen.r, zen.g, zen.b, 0);
	const D3DXVECTOR4 deep(lc.r * 0.25f * gl.r, lc.g * 0.25f * gl.g, lc.b * 0.25f * gl.b, 0);
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetVector("gSkyHor", &sh); fx->SetVector("gSkyZen", &sz); fx->SetVector("gDeep", &deep);
	fx->SetTexture("tKelvin", kelvin);
	fx->SetTechnique("Wake");
	dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
	UINT np = 0;
	fx->Begin(&np, 0); fx->BeginPass(0);
	for (const Draw& d : draws) {
		fx->SetInt("gMode", d.mode);
		fx->SetVector("gA", &d.a); fx->SetVector("gB", &d.b); fx->SetVector("gU", &d.u);
		fx->CommitChanges();
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(d.vb.size() / 3), d.vb.data(), sizeof(Vtx));
	}
	fx->EndPass(); fx->End();
}

void LiquidWake::Ripple(OBJHANDLE hBody, const VECTOR3& gpos, double radius, double amp)
{
	if (!hBody || radius <= 0.0 || amp <= 0.0) return;
	double level = 0, ext = 0;
	if (!LiquidVolume::Level(hBody, &level, &ext)) return;
	VECTOR3 cam; oapiCameraGlobalPos(&cam);
	if (length(gpos - cam) > 300.0) return;
	VECTOR3 pp; oapiGetGlobalPos(hBody, &pp);
	MATRIX3 Rp; oapiGetRotationMatrix(hBody, &Rp);
	const double Rl = oapiGetSize(hBody) + level;
	if (impulses.size() >= 96) impulses.erase(impulses.begin());
	impulses.push_back({ hBody, tmul(Rp, unit(gpos - pp) * Rl), radius, std::min(amp, 4.0), oapiGetSimTime() });
}

void LiquidWake::Release()
{
	if (kelvin) { kelvin->Release(); kelvin = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	nodes.clear(); impulses.clear(); pendingImpulse.clear(); pendingAmp.clear();
}

// AtmoFX fork: bubbles and foam in a liquid - see LiquidFoam.h (drawn with the ejecta sprite effect, JetEjecta.fx)
#include "LiquidFoam.h"
#include "LiquidVolume.h"
#include "Scene.h"
#include "VPlanet.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
	struct FoamNode { OBJHANDLE hv; int id; VECTOR3 pos; double r, dT, gas; double acc; };
	std::vector<FoamNode> nodes;

	struct Bub {
		VECTOR3 p;						// planet frame (rotating), from the centre [m]
		float age, life, s, a, seed;
		bool foam;						// burst at the surface: a flat patch
		float sway;
	};
	std::vector<Bub> bubs;
	OBJHANDLE hBody = nullptr;
	const size_t MAX_BUBS = 3000;
	const double RISE = 0.22;			// rise speed of mm bubbles in liquid methane [m/s]

	LPD3DXEFFECT fx = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 noise = nullptr;
	bool failed = false;
	unsigned int rng = 0x2545F491u;
	float Rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
	float Rnd(float a, float b) { return a + (b - a) * Rnd(); }

	struct Vtx { float x, y, z, u, v, r, g, b, a, seed; };
	const DWORD FVF = D3DFVF_XYZ | D3DFVF_TEX3 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE4(1) | D3DFVF_TEXCOORDSIZE1(2);
	std::vector<Vtx> vb;

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\JetEjecta.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\JetEjecta.fxo" : "Modules\\D3D9Client\\JetEjecta.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX foam: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		if (FAILED(D3DXCreateVolumeTextureFromFileExA(dev, "Textures\\AtmoFX\\noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
			D3DFMT_UNKNOWN, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &noise))) {
			fx->Release(); fx = nullptr; failed = true; return false;
		}
		oapiWriteLog((char*)"AtmoFX foam: on");
		return true;
	}
}

void AtmoFX_SetFoam(OBJHANDLE hVessel, int id, const double* pos, double radius, double dT, double gasFlow)
{
	const bool off = (dT <= 0.0 && gasFlow <= 0.0) || radius <= 0.0;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i].hv == hVessel && nodes[i].id == id) {
			if (off) nodes.erase(nodes.begin() + i);
			else { nodes[i].pos = _V(pos[0], pos[1], pos[2]); nodes[i].r = radius; nodes[i].dT = dT; nodes[i].gas = gasFlow; }
			return;
		}
	}
	if (!off) nodes.push_back({ hVessel, id, _V(pos[0], pos[1], pos[2]), radius, dT, gasFlow, 0.0 });
}

void LiquidFoam::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn) return;
	for (size_t i = 0; i < nodes.size();) { if (!oapiIsVessel(nodes[i].hv)) { nodes.erase(nodes.begin() + i); continue; } i++; }
	if (nodes.empty() && bubs.empty()) return;
	if (!nodes.empty()) {
		OBJHANDLE h = oapiGetVesselInterface(nodes[0].hv)->GetSurfaceRef();
		if (h != hBody) { bubs.clear(); hBody = h; }
	}
	double level = 0.0, ext = 0.07;
	if (!hBody || !LiquidVolume::Level(hBody, &level, &ext)) return;
	const double Rl = oapiGetSize(hBody) + level;
	MATRIX3 Rp; oapiGetRotationMatrix(hBody, &Rp);
	VECTOR3 pp; oapiGetGlobalPos(hBody, &pp);
	double dt = oapiGetSimStep(); if (dt < 0.0) dt = 0.0;
	const double simt = oapiGetSimTime();

	// ---- emission: dissolved N2 from warm parts (more with the excess temperature and the area), leaks as dense streams
	if (dt > 0.0) for (FoamNode& n : nodes) {
		VESSEL* v = oapiGetVesselInterface(n.hv);
		VECTOR3 g; v->Local2Global(n.pos, g);
		const VECTOR3 pl = tmul(Rp, g - pp);
		if (length(pl) >= Rl) continue;													// above the liquid
		const double area = (n.r / 0.5) * (n.r / 0.5);
		const double rate = 35.0 * area * std::min(n.dT / 100.0, 2.5) + 400.0 * std::min(n.gas / 0.002, 5.0);
		n.acc += rate * dt;
		const VECTOR3 up = unit(pl);
		VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
		const VECTOR3 e2 = crossp(up, e1);
		while (n.acc >= 1.0) {
			n.acc -= 1.0;
			if (bubs.size() >= MAX_BUBS) continue;
			const float az = Rnd(0.0f, 6.2831853f), rr = (float)n.r * sqrtf(Rnd()) * (n.gas > 0 ? 0.3f : 1.0f);
			Bub b;
			b.p = pl + (e1 * cos(az) + e2 * sin(az)) * rr;
			b.age = 0; b.life = 60.0f; b.s = Rnd(0.02f, 0.06f) * (n.gas > 0 ? 1.6f : 1.0f); b.a = Rnd(0.25f, 0.45f);
			b.seed = Rnd(); b.foam = false; b.sway = Rnd(0.0f, 6.28f);
			bubs.push_back(b);
		}
	}

	// ---- rise and burst
	if (dt > 0.0) {
		for (Bub& b : bubs) {
			b.age += (float)dt;
			if (b.foam) continue;
			const double rl = length(b.p);
			const VECTOR3 up = b.p / rl;
			VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
			const VECTOR3 e2 = crossp(up, e1);
			const double w = 0.05 * sin(b.sway + b.age * 9.0);									// zig-zag of a rising bubble
			b.p += up * (RISE * (0.8 + 0.4 * b.seed) * dt) + (e1 * w + e2 * (w * 0.6)) * dt;
			if (length(b.p) >= Rl) {																// burst: a foam patch on the surface
				b.p = b.p * (Rl / length(b.p));
				b.foam = true; b.age = 0; b.life = Rnd(4.0f, 12.0f); b.s = Rnd(0.08f, 0.2f); b.a = Rnd(0.35f, 0.6f);
			}
		}
		bubs.erase(std::remove_if(bubs.begin(), bubs.end(), [](const Bub& b) { return b.age > b.life; }), bubs.end());
	}
	if (bubs.empty() || !Init(dev)) return;

	// ---- draw (before the liquid column, which dims what is under the surface)
	const VECTOR3 cam = scn->GetCameraGPos();
	const bool camAbove = length(cam - pp) > Rl;
	vPlanet* vp = (vPlanet*)scn->GetVisObject(hBody);
	FVECTOR4 sky = vp ? vp->GroundLight() : FVECTOR4(0, 0, 0, 0);
	if (sky.w <= 0.0f) sky = FVECTOR4(0.6f, 0.55f, 0.5f, 1.0f);
	const D3DXMATRIX& V = *scn->GetViewMatrix();
	const VECTOR3 cx = _V(V._11, V._21, V._31), cy = _V(V._12, V._22, V._32);
	vb.clear(); vb.reserve(bubs.size() * 6);
	for (const Bub& b : bubs) {
		const VECTOR3 w = mul(Rp, b.p) + pp - cam;
		const double dd = length(w);
		if (dd < 0.05 || dd > 600.0) continue;
		float a = b.a, s = b.s;
		VECTOR3 ux, uy;
		if (b.foam) {																				// flat on the surface, spreading and thinning
			s = b.s * (1.0f + 1.5f * std::min(1.0f, b.age / 4.0f));
			a *= std::max(0.0f, 1.0f - b.age / b.life) * std::min(1.0f, b.age * 6.0f);
			const VECTOR3 up = unit(mul(Rp, b.p));
			VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(e1);
			ux = e1 * s; uy = crossp(up, e1) * s;
		}
		else {
			a *= std::min(1.0f, b.age * 4.0f);
			if (camAbove) a *= (float)exp(-ext * 1.5 * (Rl - length(b.p)) * 2.0);					// seen through the liquid from above
			ux = cx * s; uy = cy * s;
		}
		a *= (float)std::min(1.0, std::max(0.0, (dd - 0.5 * s) / (1.5 * s)));
		if (a < 0.003f) continue;
		const float k = b.foam ? 0.95f : 1.1f;														// bubbles glint, foam is matt white
		const float r = sky.r * k, g = sky.g * k, bl = sky.b * k;
		const VECTOR3 q0 = w - ux - uy, q1 = w + ux - uy, q2 = w - ux + uy, q3 = w + ux + uy;
		const Vtx v0 = { (float)q0.x, (float)q0.y, (float)q0.z, 0, 0, r, g, bl, a, b.seed };
		const Vtx v1 = { (float)q1.x, (float)q1.y, (float)q1.z, 1, 0, r, g, bl, a, b.seed };
		const Vtx v2 = { (float)q2.x, (float)q2.y, (float)q2.z, 0, 1, r, g, bl, a, b.seed };
		const Vtx v3 = { (float)q3.x, (float)q3.y, (float)q3.z, 1, 1, r, g, bl, a, b.seed };
		vb.push_back(v0); vb.push_back(v1); vb.push_back(v2); vb.push_back(v2); vb.push_back(v1); vb.push_back(v3);
	}
	if (vb.empty()) return;
	fx->SetTechnique("Ejecta");
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetTexture("tNoise", noise);
	fx->SetFloat("gTime", (float)fmod(simt, 1000.0));
	dev->SetFVF(FVF);
	UINT np = 0;
	fx->Begin(&np, 0); fx->BeginPass(0);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(vb.size() / 3), vb.data(), sizeof(Vtx));
	fx->EndPass(); fx->End();
}

void LiquidFoam::Release()
{
	if (noise) { noise->Release(); noise = nullptr; }
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
	nodes.clear(); bubs.clear(); hBody = nullptr;
}

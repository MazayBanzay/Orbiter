// AtmoFX fork: conformal helmet HUD - see HudOverlay.h and shaders\HudOverlay.fx
#include "HudOverlay.h"
#include "Scene.h"
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
	int hudFlags = 0;
	float hudRGB[3] = { 0.35f, 1.0f, 0.45f };
	float hudStep = 10.0f;
	double lastFrame = -1e9;						// system time of the last AtmoFX_HudFrame call

	struct Mark { int id; OBJHANDLE hBody; double lng, lat, alt, size, heading; int type; };
	std::vector<Mark> marks;

	LPD3DXEFFECT fx = nullptr;
	bool failed = false;
	struct Vtx { float x, y, z; };
	std::vector<Vtx> vb;

	bool Active() { return oapiGetSysTime() - lastFrame < 0.5; }

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		FILE* pre = fopen("Modules\\D3D9Client\\HudOverlay.fxo", "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? "Modules\\D3D9Client\\HudOverlay.fxo" : "Modules\\D3D9Client\\HudOverlay.fx", 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "AtmoFX HUD: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		oapiWriteLog((char*)"AtmoFX HUD: conformal markers on");
		return true;
	}

	// a thick line: a camera-facing quad of 'px' pixels (two triangles)
	void Seg(const VECTOR3& a, const VECTOR3& b, double pxScale)
	{
		const VECTOR3 d = b - a, m = (a + b) * 0.5;
		VECTOR3 s = crossp(d, m);
		const double sl = length(s);
		if (sl < 1e-9) return;
		s = s / sl * (pxScale * length(m));
		const VECTOR3 q0 = a - s, q1 = a + s, q2 = b - s, q3 = b + s;
		const Vtx v[6] = { { (float)q0.x, (float)q0.y, (float)q0.z }, { (float)q1.x, (float)q1.y, (float)q1.z }, { (float)q2.x, (float)q2.y, (float)q2.z },
						   { (float)q2.x, (float)q2.y, (float)q2.z }, { (float)q1.x, (float)q1.y, (float)q1.z }, { (float)q3.x, (float)q3.y, (float)q3.z } };
		vb.insert(vb.end(), v, v + 6);
	}
}

bool HudOverlay::TerrainOn(float* rgb, float* step)
{
	if (!Active() || !(hudFlags & 1)) return false;
	rgb[0] = hudRGB[0]; rgb[1] = hudRGB[1]; rgb[2] = hudRGB[2]; *step = hudStep;
	return true;
}

void AtmoFX_HudFrame(int flags, double r, double g, double b, double step)
{
	hudFlags = flags;
	hudRGB[0] = (float)r; hudRGB[1] = (float)g; hudRGB[2] = (float)b;
	hudStep = (float)(step > 0.5 ? step : 0.5);
	lastFrame = oapiGetSysTime();
}

void AtmoFX_HudMarker(int id, OBJHANDLE hBody, double lng, double lat, double alt, int type, double size, double heading)
{
	for (size_t i = 0; i < marks.size(); i++) {
		if (marks[i].id == id) {
			if (type < 0) marks.erase(marks.begin() + i);
			else marks[i] = { id, hBody, lng, lat, alt, size, heading, type };
			return;
		}
	}
	if (type >= 0) marks.push_back({ id, hBody, lng, lat, alt, size, heading, type });
}

void HudOverlay::Render(LPDIRECT3DDEVICE9 dev, Scene* scn)
{
	if (!scn || !Active() || !(hudFlags & 2) || marks.empty()) return;
	if (!Init(dev)) return;

	const VECTOR3 cam = scn->GetCameraGPos();
	D3DVIEWPORT9 vp; dev->GetViewport(&vp);
	const D3DXMATRIX& P = *scn->GetProjectionMatrix();
	const double px = 1.6 / (P._22 * 0.5 * vp.Height);		// half-width of a ~3 px line, per metre of distance
	vb.clear();
	for (const Mark& m : marks) {
		if (!m.hBody || oapiGetObjectType(m.hBody) != OBJTP_PLANET) continue;
		MATRIX3 Rp; oapiGetRotationMatrix(m.hBody, &Rp);
		VECTOR3 pp; oapiGetGlobalPos(m.hBody, &pp);
		const double R = oapiGetSize(m.hBody) + oapiSurfaceElevation(m.hBody, m.lng, m.lat) + m.alt;
		const double cl = cos(m.lat), sl = sin(m.lat), cg = cos(m.lng), sg = sin(m.lng);
		const VECTOR3 up = mul(Rp, _V(cl * cg, sl, cl * sg));
		const VECTOR3 east = mul(Rp, _V(-sg, 0, cg));
		const VECTOR3 north = mul(Rp, _V(-sl * cg, cl, -sl * sg));
		const VECTOR3 c = pp + up * R - cam;						// camera-relative
		if (length(c) > 50000.0) continue;
		const double s = m.size;
		auto L = [&](double e0, double n0, double u0, double e1, double n1, double u1) {
			Seg(c + east * e0 + north * n0 + up * u0, c + east * e1 + north * n1 + up * u1, px);
		};
		switch (m.type) {
		case 1: {																	// PAD: corner brackets and a cross, on the ground
			const double h = s * 0.5, k = s * 0.18;
			for (int i = 0; i < 4; i++) {
				const double se = (i & 1) ? h : -h, sn = (i & 2) ? h : -h;
				L(se, sn, 0.05, se - (se > 0 ? k : -k), sn, 0.05);
				L(se, sn, 0.05, se, sn - (sn > 0 ? k : -k), 0.05);
			}
			L(-k, 0, 0.05, k, 0, 0.05); L(0, -k, 0.05, 0, k, 0.05);
			break;
		}
		case 2: {																	// GATE: a frame across the heading
			const double ch = cos(m.heading), sh = sin(m.heading);
			const double w = s * 0.5, hh = s * 0.3, k = s * 0.15;
			const VECTOR3 side = east * ch - north * sh;
			for (int i = 0; i < 4; i++) {
				const double a = (i & 1) ? w : -w, b = (i & 2) ? hh : -hh;
				const VECTOR3 p0 = c + side * a + up * b;
				Seg(p0, p0 - side * (a > 0 ? k : -k), px);
				Seg(p0, p0 - up * (b > 0 ? k : -k), px);
			}
			break;
		}
		case 3: {																	// WAYPOINT: an upright diamond
			const double h = s * 0.5;
			L(0, 0, 0, h, 0, h); L(h, 0, h, 0, 0, 2 * h); L(0, 0, 2 * h, -h, 0, h); L(-h, 0, h, 0, 0, 0);
			break;
		}
		default: {																	// TARGET: a ground ring and a mast
			const int N = 24;
			for (int i = 0; i < N; i++) {
				const double a0 = i * 2 * PI / N, a1 = (i + 1) * 2 * PI / N;
				L(s * cos(a0), s * sin(a0), 0.05, s * cos(a1), s * sin(a1), 0.05);
			}
			L(0, 0, 0, 0, 0, 2 * s);
			break;
		}
		}
	}
	if (vb.empty()) return;
	const D3DXVECTOR4 col(hudRGB[0], hudRGB[1], hudRGB[2], 1.0f);
	fx->SetMatrix("gVP", scn->GetProjectionViewMatrix());
	fx->SetVector("gCol", &col);
	dev->SetFVF(D3DFVF_XYZ);
	UINT np = 0;
	for (const char* tech : { "Hidden", "Visible" }) {
		fx->SetTechnique(tech);
		fx->Begin(&np, 0); fx->BeginPass(0);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(vb.size() / 3), vb.data(), sizeof(Vtx));
		fx->EndPass(); fx->End();
	}
}

void HudOverlay::Release()
{
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
}

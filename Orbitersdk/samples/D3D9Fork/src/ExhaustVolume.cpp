// AtmoFX fork: volumetric exhaust - see ExhaustVolume.h and shaders\ExhaustVolume.fx
#include "ExhaustVolume.h"
#include "Scene.h"
#include "D3D9Surface.h"
#include "D3D9Util.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
	LPD3DXEFFECT fx = nullptr;
	bool failed = false;
	D3DXHANDLE hTech, hWVP, hCam, hR, hL, hMix, hTex, hSeed;
	const int SEG = 32;
	D3DXVECTOR3 tri[SEG * 12];   // the unit cylinder (x, y on the unit circle, z 0..1): the side and both caps
	int ntri = 0;

	void Build()
	{
		ntri = 0;
		for (int k = 0; k < SEG; ++k) {
			const float a0 = 2.0f * 3.14159265f * k / SEG, a1 = 2.0f * 3.14159265f * (k + 1) / SEG;
			const D3DXVECTOR3 p0(cosf(a0), sinf(a0), 0), p1(cosf(a1), sinf(a1), 0), q0(p0.x, p0.y, 1), q1(p1.x, p1.y, 1);
			const D3DXVECTOR3 c0(0, 0, 0), c1(0, 0, 1);
			const D3DXVECTOR3 t[12] = { p0, p1, q1, p0, q1, q0, c0, p1, p0, c1, q0, q1 };
			for (int j = 0; j < 12; ++j) tri[ntri * 3 + j] = t[j];
			ntri += 4;
		}
	}

	bool Init(LPDIRECT3DDEVICE9 dev)
	{
		if (fx) return true;
		if (failed) return false;
		// the precompiled .fxo first (fxc in build_client.bat), the .fx compiled here (slow) if it is missing
		const char* fxo = "Modules\\D3D9Client\\ExhaustVolume.fxo";
		const char* src = "Modules\\D3D9Client\\ExhaustVolume.fx";
		FILE* pre = fopen(fxo, "rb");
		if (pre) fclose(pre);
		LPD3DXBUFFER err = nullptr;
		HRESULT hr = D3DXCreateEffectFromFileA(dev, pre ? fxo : src, 0, 0, 0, 0, &fx, &err);
		if (FAILED(hr) || !fx) {
			char buf[512]; sprintf_s(buf, "ExhaustVolume: effect failed (%s)", err ? (const char*)err->GetBufferPointer() : "no file");
			oapiWriteLog(buf); if (err) err->Release(); failed = true; return false;
		}
		if (err) err->Release();
		hTech = fx->GetTechniqueByName("VolTech"); hWVP = fx->GetParameterByName(0, "gWVP"); hCam = fx->GetParameterByName(0, "gCam");
		hR = fx->GetParameterByName(0, "gR"); hL = fx->GetParameterByName(0, "gL"); hMix = fx->GetParameterByName(0, "gMix");
		hTex = fx->GetParameterByName(0, "gTex"); hSeed = fx->GetParameterByName(0, "gSeed");
		Build();
		oapiWriteLog(pre ? "ExhaustVolume: volumetric exhausts on (fxo)" : "ExhaustVolume: volumetric exhausts on (fx)");
		return true;
	}
}

bool ExhaustVolume::Render(LPDIRECT3DDEVICE9 dev, const D3DXMATRIX* pW, const VECTOR3& camLocal, const EXHAUSTSPEC* es, SURFHANDLE def, Scene* scn)
{
	// the main scene only: shadows, environment maps and the like keep the stock ribbon
	if (!scn || scn->GetRenderPass() != RENDERPASS_MAINSCENE || !Init(dev)) return false;
	SURFHANDLE ts = es->tex ? es->tex : def;
	if (!ts || !SURFACE(ts)->GetTexture()) return false;

	double level = *es->level;
	if (es->modulate) level *= (1.0 - es->modulate) + (double)rand() * es->modulate / (double)RAND_MAX;

	// the exhaust's frame in the vessel's: origin at the exit, z down the jet (against ldir)
	VECTOR3 e = -(*es->ldir); normalise(e);
	const VECTOR3 o = (*es->lpos) - (*es->ldir) * es->lofs;
	VECTOR3 sx = crossp(e, fabs(e.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); normalise(sx);
	const VECTOR3 sy = crossp(e, sx);
	const D3DXMATRIX E(float(sx.x), float(sx.y), float(sx.z), 0, float(sy.x), float(sy.y), float(sy.z), 0,
	                   float(e.x), float(e.y), float(e.z), 0, float(o.x), float(o.y), float(o.z), 1);   // row vectors: p_vessel = p_e * E
	const D3DXMATRIX WVP = E * (*pW) * (*scn->GetViewMatrix()) * (*scn->GetProjectionMatrix());
	// the camera in the vessel's frame: the client's world is camera-centred, so it is the inverse world matrix's origin
	VECTOR3 camV = camLocal;
	D3DXMATRIX Wi; D3DXVECTOR3 c0(0, 0, 0), cv;
	if (D3DXMatrixInverse(&Wi, nullptr, pW)) { D3DXVec3TransformCoord(&cv, &c0, &Wi); camV = _V(cv.x, cv.y, cv.z); }
	const VECTOR3 dc = camV - o;
	const D3DXVECTOR3 cam(float(dotp(dc, sx)), float(dotp(dc, sy)), float(dotp(dc, e)));

	fx->SetTechnique(hTech);
	fx->SetMatrix(hWVP, &WVP); fx->SetValue(hCam, &cam, sizeof(cam));
	fx->SetFloat(hR, float(es->wsize)); fx->SetFloat(hL, float(es->lsize)); fx->SetFloat(hMix, float(level));
	fx->SetFloat(hSeed, float(rand()) / RAND_MAX);
	fx->SetTexture(hTex, SURFACE(ts)->GetTexture());
	UINT np = 0;
	fx->Begin(&np, 0);   // (0: the effect saves and restores the device's states)
	fx->BeginPass(0);
	dev->SetFVF(D3DFVF_XYZ);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, ntri, tri, sizeof(D3DXVECTOR3));
	fx->EndPass(); fx->End();
	return true;
}

void ExhaustVolume::Release()
{
	if (fx) { fx->Release(); fx = nullptr; }
	failed = false;
}

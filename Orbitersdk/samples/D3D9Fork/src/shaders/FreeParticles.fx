// AtmoFX fork: the free particles' sprites (see FreeParticles.h). Colour is lit on the CPU (sun, sky, forward scattering).
// Four looks, by the vertex seed: [0, 1) a soft, irregular puff of fine grains (as the jets' ejecta, JetEjecta.fx);
// [2, 3) a clod - a solid irregular lump of damp soil, shaded from above; [4, 5) a cluster of liquid drops - a thin veil
// with glints; [6, 7) a wet spot lying on the ground - it darkens it. Premultiplied, depth-tested, no depth writes.

float4x4 gVP;
float    gTime;
texture  tNoise;
sampler3D sNoise = sampler_state {
	Texture = <tNoise>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};

struct VSIn  { float3 p : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; float seed : TEXCOORD2; };
struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; float seed : TEXCOORD2; };

VSOut FrVS(VSIn i)
{
	VSOut o;
	o.pos = mul(float4(i.p, 1.0), gVP);
	o.uv = i.uv; o.c = i.c; o.seed = i.seed;
	return o;
}

float4 FrPS(VSOut i) : COLOR
{
	float2 d = i.uv * 2.0 - 1.0;
	float r2 = dot(d, d);
	if (r2 > 1.0) discard;
	if (i.seed >= 6.0) {
		// a wet spot lying on the ground: it only darkens what is under it, its edge irregular
		float s = i.seed - 6.0;
		float n = tex3Dlod(sNoise, float4(i.uv * 0.8 + s * 7.3, s * 3.1, 0)).r;
		float a = i.c.a * saturate((0.70 + 0.35 * n - r2) * 4.0);
		return float4(0, 0, 0, a);
	}
	if (i.seed >= 4.0) {
		// a cluster of drops: a thin veil of clear liquid, glints where single drops catch the light
		float s = i.seed - 4.0;
		float4 nz = tex3Dlod(sNoise, float4(i.uv * 2.2 + s * 7.3, s * 3.1 + gTime * 0.05, 0));
		float shape = saturate(1.0 - r2 * (0.7 + 0.6 * nz.r));
		shape *= shape;
		float glint = saturate((nz.b - 0.62) * 8.0);
		float a = i.c.a * shape * (0.45 + 0.9 * glint);
		return float4(i.c.rgb * (0.85 + 0.9 * glint) * a, a);
	}
	if (i.seed >= 2.0) {
		// a clod: its outline irregular by the noise, a crisp edge, darker below and on the far side from the light
		float s = i.seed - 2.0;
		float n = tex3Dlod(sNoise, float4(i.uv * 0.9 + s * 7.3, s * 3.1, 0)).r;
		float edge = 0.40 + 0.50 * n;
		float a = i.c.a * saturate((edge - r2) * 10.0);
		float shade = 0.50 + 0.50 * saturate(0.55 + 0.65 * d.y - 0.25 * d.x);
		return float4(i.c.rgb * shade * a, a);
	}
	// a puff: irregular edge and inner clumps, noise per sprite, drifting slowly
	float4 nz = tex3Dlod(sNoise, float4(i.uv * 0.6 + i.seed * 7.3, i.seed * 3.1 + gTime * 0.03, 0));
	float n = nz.r, m = nz.b;
	float shape = saturate(1.0 - r2 * (0.8 + 0.9 * n));
	shape *= shape;
	float a = i.c.a * shape * saturate(0.35 + 1.3 * m);
	return float4(i.c.rgb * a, a);
}

// ---- ground marks: ruts, pits (FreeParticles.cpp, AtmoFX_Mark). The colour is worked out per vertex on the CPU from the
// relief and the light, premultiplied: rgb 0 with alpha darkens the ground, rgb with alpha 0 lightens it; the noise here
// gives it the grain of turned soil (uv in metres)
struct MkIn  { float3 p : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; };
struct MkOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; };

MkOut MkVS(MkIn i)
{
	MkOut o;
	o.pos = mul(float4(i.p, 1.0), gVP);
	o.uv = i.uv; o.c = i.c;
	return o;
}

float4 MkPS(MkOut i) : COLOR
{
	float n1 = tex3Dlod(sNoise, float4(i.uv * 0.45, 0.37, 0)).r;		// clumps of ~1 m
	float n2 = tex3Dlod(sNoise, float4(i.uv * 3.1, 0.71, 0)).b;		// lumps of ~15 cm
	float k = 0.55 + 0.6 * n1 * (0.6 + 0.8 * n2);
	return i.c * k;
}

technique Mark
{
	pass P0
	{
		VertexShader = compile vs_3_0 MkVS();
		PixelShader = compile ps_3_0 MkPS();
		ZEnable = true;
		ZWriteEnable = false;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
		FogEnable = false;
	}
}

technique Free
{
	pass P0
	{
		VertexShader = compile vs_3_0 FrVS();
		PixelShader = compile ps_3_0 FrPS();
		ZEnable = true;
		ZWriteEnable = false;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
		FogEnable = false;
	}
}

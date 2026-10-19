// AtmoFX fork: volumetric exhaust (OrbiterCrew «Модуль полёта», 2026-10-18 - the user: «плоские потоки»).
// The stock exhaust is a ribbon turned to the camera about the jet's axis: seen along the axis it is a line. Opted in by
// EXHAUSTSPEC.flags & 0x100, the jet is a cylinder (radius wsize, length lsize) whose far faces are drawn; each pixel
// marches the view ray through it. The density comes from the exhaust texture's strip as the stock layout has it
// (u 0.01..0.24 across = r from -wsize to +wsize, v 0..1 along): the art made for the ribbon becomes the jet's radial
// profile, so it reads the same from the side and fills out end-on.
// Frame: the exhaust's own - origin at the exit, z down the jet, x/y across; the camera given in it.

float4x4 gWVP;          // exhaust frame -> clip
float3   gCam;          // the camera in the exhaust frame
float    gR;            // radius (wsize)
float    gL;            // length (lsize)
float    gMix;          // level (with the flicker)
float    gSeed;         // per frame: the march's start is jittered by white noise (Bayer/IGN gave moire rings)
texture  gTex;

sampler TexS = sampler_state {
	Texture = <gTex>;
	MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = LINEAR;
	AddressU = CLAMP; AddressV = CLAMP;
};

struct VSOut { float4 pos : POSITION; float3 p : TEXCOORD0; };
float hash(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233)) + gSeed * 43.17) * 43758.5453); }

VSOut VolVS(float3 unit : POSITION)
{
	VSOut o;
	o.p = float3(unit.x * gR, unit.y * gR, unit.z * gL);
	o.pos = mul(float4(o.p, 1.0), gWVP);
	return o;
}

#define STEPS 28

float4 VolPS(VSOut i, float2 sp : VPOS) : COLOR
{
	float3 d = i.p - gCam; float tp = length(d); d /= tp;
	// the ray against the cylinder x^2 + y^2 <= R^2, 0 <= z <= L
	float a = dot(d.xy, d.xy), b = dot(gCam.xy, d.xy), c = dot(gCam.xy, gCam.xy) - gR * gR;
	float t0 = -1e9, t1 = 1e9;
	if (a > 1e-8) {
		float disc = b * b - a * c;
		if (disc <= 0) discard;
		float s = sqrt(disc); t0 = (-b - s) / a; t1 = (-b + s) / a;
	} else if (c > 0) discard;
	if (abs(d.z) > 1e-8) {
		float z0 = (0 - gCam.z) / d.z, z1 = (gL - gCam.z) / d.z;
		t0 = max(t0, min(z0, z1)); t1 = min(t1, max(z0, z1));
	} else if (gCam.z < 0 || gCam.z > gL) discard;
	t0 = max(t0, 0);
	if (t1 <= t0) discard;
	// only the far face of the cylinder draws (the near one would count the jet twice): this pixel must be the exit point
	// (the mesh is a 32-gon inside the circle: up to 0.5 % of R off it)
	if (abs(tp - t1) > 0.25 * (t1 - t0) + 0.01 * gR) discard;

	float dt = (t1 - t0) / STEPS, sum = 0, jit = hash(sp); float3 col = 0;
	[loop] for (int k = 0; k < STEPS; k++) {
		float3 q = gCam + d * (t0 + (k + jit) * dt);
		float r = length(q.xy) / gR, v = q.z / gL;
		float4 tx = tex2Dlod(TexS, float4(0.01 + 0.23 * (0.5 + 0.5 * r), v, 0, 0));
		float w = tx.a * dt; sum += w; col += tx.rgb * w;
	}
	// normalised so that a chord through the axis side-on gives about the ribbon's own alpha there
	float alpha = 1 - exp(-sum / (0.9 * gR));
	return float4(col / max(sum, 1e-6), alpha * gMix);
}

technique VolTech
{
	pass P0
	{
		vertexShader = compile vs_3_0 VolVS();
		pixelShader  = compile ps_3_0 VolPS();
		AlphaBlendEnable = true;
		BlendOp = Add;
		SrcBlend = SrcAlpha;
		DestBlend = InvSrcAlpha;
		ZWriteEnable = false;
		ZEnable = true;
		CullMode = NONE;
	}
}

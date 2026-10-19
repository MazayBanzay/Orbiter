// AtmoFX fork: ground ejecta sprites (see JetEjecta.h). Colour is lit on the CPU (sun, sky, forward scattering);
// the sprite is a soft, irregular puff (3D noise per sprite), premultiplied, depth-tested, no depth writes.

float4x4 gVP;
float    gTime;
texture  tNoise;
sampler3D sNoise = sampler_state {
	Texture = <tNoise>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};

struct VSIn  { float3 p : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; float seed : TEXCOORD2; };
struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; float seed : TEXCOORD2; };

VSOut EjVS(VSIn i)
{
	VSOut o;
	o.pos = mul(float4(i.p, 1.0), gVP);
	o.uv = i.uv; o.c = i.c; o.seed = i.seed;
	return o;
}

float4 EjPS(VSOut i) : COLOR
{
	float2 d = i.uv * 2.0 - 1.0;
	float r2 = dot(d, d);
	if (r2 > 1.0) discard;
	// irregular edge and inner clumps: noise per sprite, drifting slowly
	float4 nz = tex3Dlod(sNoise, float4(i.uv * 0.6 + i.seed * 7.3, i.seed * 3.1 + gTime * 0.03, 0));	// one fetch: r edge, b clumps
	float n = nz.r, m = nz.b;
	float shape = saturate(1.0 - r2 * (0.8 + 0.9 * n));
	shape *= shape;
	float a = abs(i.c.a) * shape * saturate(0.35 + 1.3 * m);
	return float4(i.c.rgb * a, i.c.a < 0 ? 0.0 : a);		// alpha < 0: additive glow (no cover)
}

technique Ejecta
{
	pass P0
	{
		VertexShader = compile vs_3_0 EjVS();
		PixelShader = compile ps_3_0 EjPS();
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

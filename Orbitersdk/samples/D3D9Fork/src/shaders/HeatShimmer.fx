// AtmoFX fork: heat shimmer (see HeatShimmer.h). Camera-facing quads over warm air resample the scene with a small,
// animated offset; depth-tested against the scene, so a closer hull hides the shimmer behind it.

float4x4 gVP;			// view-projection (camera-relative world, metres)
float2   gInvView;		// 1 / render target size
float    gTime;			// simulation time [s]
float4   gPrm;			// x amplitude [px], y rise speed [quad lengths / s], z kind (0 exhaust, 1 hot node), w phase

texture tScene;
texture tNoise;
sampler2D sScene = sampler_state {
	Texture = <tScene>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Clamp; AddressV = Clamp;
};
sampler3D sNoise = sampler_state {
	Texture = <tNoise>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};

struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VSOut ShimmerVS(float3 p : POSITION, float2 uv : TEXCOORD0)
{
	VSOut o;
	o.pos = mul(float4(p, 1.0), gVP);
	o.uv = uv;
	return o;
}

float4 ShimmerPS(VSOut i, float2 vpos : VPOS) : COLOR
{
	float u = i.uv.x, v = i.uv.y;
	float across = 1.0 - (2.0 * u - 1.0) * (2.0 * u - 1.0);		// 1 on the axis, 0 at the edges
	across *= across;
	float along = gPrm.z < 0.5
		? smoothstep(0.0, 0.08, v) * (1.0 - smoothstep(0.45, 1.0, v))	// exhaust: from the exit, fading down the wake
		: smoothstep(0.0, 0.15, v) * (1.0 - smoothstep(0.35, 1.0, v));	// hot node: a plume of warm air above it
	float mask = across * along;
	if (mask < 0.003) discard;

	// two decorrelated noise channels, rising with the warm air; standardised (r: mean .231 sd .217, g: mean .389 sd .147)
	float3 q = float3(u * 1.3 + gPrm.w, v * 2.2 - gTime * gPrm.y, gTime * 0.35 + gPrm.w * 3.1);
	float4 n = tex3Dlod(sNoise, float4(q, 0));
	float2 d = float2((n.r - 0.231) / 0.217, (n.g - 0.389) / 0.147) * 0.45;
	float2 uv = (vpos + 0.5) * gInvView + d * gPrm.x * mask * gInvView;
	return float4(tex2D(sScene, uv).rgb, smoothstep(0.0, 0.25, mask));
}

technique Shimmer
{
	pass P0
	{
		VertexShader = compile vs_3_0 ShimmerVS();
		PixelShader = compile ps_3_0 ShimmerPS();
		ZEnable = true;
		ZWriteEnable = false;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = SrcAlpha;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
		FogEnable = false;
	}
}

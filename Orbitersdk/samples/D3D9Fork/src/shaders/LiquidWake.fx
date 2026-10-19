// AtmoFX fork: wakes on a liquid surface (see LiquidWake.h). Each patch lies on the liquid level, computes the wave
// slope there and re-shades the surface: Fresnel sky reflection over the deep liquid colour; opacity follows the slope,
// so flat liquid around keeps the terrain shader's look.

float4x4 gVP;
int      gMode;			// 0 Kelvin wake, 1 rings around a bobbing part, 2 expanding packet after an impulse
float4   gA;			// mode 0: wavelength [m], slope amplitude, part radius;  1: ring wavelength, amplitude, radius, time;
						// 2: wavelength, amplitude, front radius, time since the impulse
float4   gB;			// mode 0: direction of motion; 1, 2: centre (camera-relative)
float4   gU;			// local up
float4   gSkyHor, gSkyZen, gDeep;
texture  tKelvin;		// linear-theory Kelvin wake: rg = slope (along the motion, lateral), b = height; x = lateral, y = behind
sampler2D sKelvin = sampler_state {
	Texture = <tKelvin>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Clamp; AddressV = Clamp;
};

struct VOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float3 w : TEXCOORD1; };
VOut WakeVS(float3 p : POSITION, float2 uv : TEXCOORD0)
{
	VOut o;
	o.pos = mul(float4(p, 1.0), gVP);
	o.uv = uv; o.w = p;
	return o;
}

float4 WakePS(VOut i) : COLOR
{
	float3 up = gU.xyz;
	float3 sv = 0;							// slope vector in the tangent plane
	float fade = 1.0;
	if (gMode == 0) {
		float lam = gA.x;
		float2 tc = float2(i.uv.y / (13.0 * lam) * 0.5 + 0.5, i.uv.x / (32.0 * lam));
		float3 k = tex2D(sKelvin, tc).rgb;
		float2 sl = (k.rg * 2.0 - 1.0) * gA.y;
		float3 f = gB.xyz, side = cross(up, f);
		sv = f * sl.x + side * sl.y;
		fade = saturate(i.uv.x / max(gA.z, 0.3)) * saturate((1.0 - tc.y) / 0.25);
	}
	else {
		float3 d = i.w - gB.xyz;
		d -= up * dot(d, up);
		float r = length(d);
		float3 rad = d / max(r, 1e-3);
		float R0 = gA.z;
		if (gMode == 1) {
			float lam = gA.x;
			float ph = 6.2832 * (r - 0.25 * gA.w) / lam;			// ripples run outward at ~0.25 m/s
			float env = exp(-max(r - R0, 0.0) / (3.0 * R0 + 0.5)) * saturate((r - 0.8 * R0) / (0.2 * R0 + 0.05));
			sv = rad * (gA.y * env * cos(ph));
		}
		else {
			float lam = gA.x, rf = gA.z;
			float x = (r - rf) / lam;
			float env = exp(-x * x * 1.5);
			sv = rad * (gA.y * env * cos(6.2832 * x));
		}
	}
	float s = length(sv);
	float a = saturate(s * 12.0) * fade;
	if (a < 0.004) discard;
	float3 n = normalize(up - sv);
	float3 v = normalize(-i.w);
	float F = 0.016 + 0.984 * pow(1.0 - saturate(dot(n, v)), 5.0);	// liquid methane, n ~1.29
	float3 rr = reflect(-v, n);
	float3 sky = lerp(gSkyHor.rgb, gSkyZen.rgb, saturate(dot(rr, up)));
	float3 col = F * sky + (1.0 - F) * gDeep.rgb;
	return float4(col * a, a);
}

technique Wake
{
	pass P0
	{
		VertexShader = compile vs_3_0 WakeVS();
		PixelShader = compile ps_3_0 WakePS();
		ZEnable = true; ZWriteEnable = false; ZFunc = LessEqual;
		CullMode = None; AlphaBlendEnable = true; SrcBlend = One; DestBlend = InvSrcAlpha;
		StencilEnable = false; AlphaTestEnable = false; FogEnable = false;
	}
}

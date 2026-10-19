// AtmoFX fork: the view from under a liquid surface (see LiquidVolume.h).

float4x4 gVP;			// view-projection (camera-relative world, metres)
float3   gCentre;		// planet centre, camera-relative
float4   gFog;			// colour of the liquid column at the camera's depth
float4   gSky;			// sky light above the surface
float    gIndex;		// refractive index of the liquid
float    gExt;			// extinction [1/m]
float    gTime;
float4   gAlpha3;		// fog layer opacity per channel
// above the level: the surface sheet and the slices under it
float4   gDeep;			// light scattered back by the liquid body
float4   gSkyHor, gSkyZen;	// reflected sky (AtmoFX_SkyHorizon / Zenith), gSkyHor.w = sun-side boost
float4   gSunDir, gSunCol;	// unit direction to the Sun (world), its colour at the surface
float    gThick;		// slice thickness [m]
texture  tNoise;
sampler3D sNoise = sampler_state {
	Texture = <tNoise>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};

// ---------------------------------------------------------------- the surface seen from below
struct SOut { float4 pos : POSITION; float3 w : TEXCOORD0; };
SOut SurfVS(float3 p : POSITION)
{
	SOut o;
	o.pos = mul(float4(p, 1.0), gVP);
	o.w = p;
	return o;
}

float4 SurfPS(SOut i) : COLOR
{
	float dist = length(i.w);
	float3 rd = i.w / dist;										// camera at the origin
	float3 n = normalize(i.w - gCentre);						// up
	// small ripples: tilt the normal with drifting noise (fades with distance)
	float3 q = i.w * 0.35 + float3(0.0, gTime * 0.08, gTime * 0.05);
	float4 nz = tex3Dlod(sNoise, float4(q * 0.05, 0));
	float3 t = float3(nz.r - 0.23, nz.g - 0.39, nz.b - 0.39) * 0.6 / (1.0 + dist * 0.05);
	n = normalize(n + t - n * dot(t, n));
	float cosI = saturate(dot(rd, n));							// looking up into the surface
	float sinI = sqrt(1.0 - cosI * cosI);
	float sinC = 1.0 / gIndex;									// edge of Snell's window
	float f0 = (gIndex - 1.0) / (gIndex + 1.0); f0 *= f0;
	float3 col;
	if (sinI < sinC) {
		// inside the window: the sky, squeezed into a cone; Fresnel grows towards the edge, which brightens
		float cosT = sqrt(1.0 - (gIndex * sinI) * (gIndex * sinI));
		float F = f0 + (1.0 - f0) * pow(1.0 - cosT, 5.0);
		float rim = smoothstep(0.75, 1.0, sinI / sinC);
		col = gSky.rgb * (1.0 - F) * (0.85 + 0.6 * rim) + gFog.rgb * F;
	}
	else {
		col = gFog.rgb * 1.15;									// total internal reflection: the column below, mirrored
	}
	return float4(col, 1.0);
}

technique Surface
{
	pass P0
	{
		VertexShader = compile vs_3_0 SurfVS();
		PixelShader = compile ps_3_0 SurfPS();
		ZEnable = true;
		ZWriteEnable = true;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = false;
		StencilEnable = false;
		AlphaTestEnable = false;
		FogEnable = false;
	}
}

// ---------------------------------------------------------------- the column: one fog layer
struct LOut { float4 pos : POSITION; };
LOut LayerVS(float4 p : POSITION) { LOut o; o.pos = p; return o; }
float4 LayerPS(LOut i) : COLOR { return float4(gFog.rgb * gAlpha3.rgb, 1.0); }	// dest*(1-a) + fog*a, a per channel

technique Layer
{
	pass P0
	{
		VertexShader = compile vs_3_0 LayerVS();
		PixelShader = compile ps_3_0 LayerPS();
		ZEnable = true;
		ZWriteEnable = false;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvBlendFactor;
		StencilEnable = false;
		AlphaTestEnable = false;
		FogEnable = false;
	}
}

// ---------------------------------------------------------------- camera above the level
float GGXd(float c, float r) { float r2 = r * r, d = c * c * (r2 - 1.0) + 1.0; return r2 / (3.14159 * d * d); }

// a slice of liquid under the surface: what lies deeper is seen through it (refracted path through the slice)
float4 SlicePS(SOut i) : COLOR
{
	float3 v = -normalize(i.w);
	float3 n = normalize(i.w - gCentre);
	float c = saturate(dot(v, n));
	float ct = sqrt(saturate(1.0 - (1.0 - c * c) / (gIndex * gIndex)));
	float a = 1.0 - exp(-gExt * gThick / max(ct, 0.05));
	return float4(gDeep.rgb * a, a);
}

// the surface: Fresnel reflection of the sky and the Sun's glint over everything below (premultiplied, alpha = F)
float4 TopPS(SOut i) : COLOR
{
	float dist = length(i.w);
	float3 v = -i.w / dist;
	float3 up = normalize(i.w - gCentre);
	float3 q = i.w * 0.6 + float3(0.0, gTime * 0.05, gTime * 0.03);
	float4 nz = tex3Dlod(sNoise, float4(q * 0.05, 0));
	float3 t = float3(nz.r - 0.23, nz.g - 0.39, nz.b - 0.39) * 0.04 / (1.0 + dist * 0.02);	// faint ripples (radar: mm-smooth)
	float3 n = normalize(up + t - up * dot(t, up));
	float c = saturate(dot(v, n));
	float f0 = (gIndex - 1.0) / (gIndex + 1.0); f0 *= f0;
	float F = f0 + (1.0 - f0) * pow(1.0 - c, 5.0);
	float3 r = reflect(-v, n);
	float3 sky = lerp(gSkyHor.rgb, gSkyZen.rgb, sqrt(saturate(dot(r, up))));
	sky *= 1.0 + gSkyHor.w * pow(saturate(dot(r, gSunDir.xyz)), 3.0);
	float3 h = normalize(v + gSunDir.xyz);
	float g = GGXd(saturate(dot(h, n)), 0.04) * (f0 + (1.0 - f0) * pow(1.0 - saturate(dot(v, h)), 5.0));
	float3 glint = gSunCol.rgb * min(g / (4.0 * max(saturate(dot(n, gSunDir.xyz)), c) + 1e-3), 30.0) * step(0.0, dot(up, gSunDir.xyz));
	return float4(sky * F + glint, F);
}

technique Slice
{
	pass P0
	{
		VertexShader = compile vs_3_0 SurfVS();
		PixelShader = compile ps_3_0 SlicePS();
		ZEnable = true; ZWriteEnable = false; ZFunc = LessEqual;
		CullMode = None; AlphaBlendEnable = true; SrcBlend = One; DestBlend = InvSrcAlpha;
		StencilEnable = false; AlphaTestEnable = false; FogEnable = false;
	}
}

technique Top
{
	pass P0
	{
		VertexShader = compile vs_3_0 SurfVS();
		PixelShader = compile ps_3_0 TopPS();
		ZEnable = true; ZWriteEnable = false; ZFunc = LessEqual;
		CullMode = None; AlphaBlendEnable = true; SrcBlend = One; DestBlend = InvSrcAlpha;
		StencilEnable = false; AlphaTestEnable = false; FogEnable = false;
	}
}

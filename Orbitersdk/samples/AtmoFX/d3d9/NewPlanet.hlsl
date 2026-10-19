// ============================================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// licensed under LGPL v2
// Copyright (C) 2022 Jarmo Nikkanen
// ============================================================================

#include "Scatter.hlsl"

struct _Light
{
	float3   position[4];         /* position in world space */
	float3   direction[4];        /* direction in world space */
	float3   diffuse[4];          /* diffuse color of light */
	float3   attenuation[4];      /* Attenuation */
	float4   param[4];            /* range, falloff, theta, phi */
};

#define Range   0
#define Falloff 1
#define Theta   2
#define Phi     3

#define ATMNOISE 0.25
#define GLARE_SIZE 5	// Larger value -> smaller

// ----------------------------------------------------------------------------
// Vertex input layouts from Vertex buffers to vertex shader
// ----------------------------------------------------------------------------

struct TILEVERTEX					// (VERTEX_2TEX) Vertex declaration used for surface tiles and cloud layer
{
	float3 posL     : POSITION0;
	float3 normalL  : NORMAL0;
	float2 tex0     : TEXCOORD0;
	float  elev     : TEXCOORD1;
};

// ----------------------------------------------------------------------------
// Vertex Shader to Pixel Shader datafeeds
// ----------------------------------------------------------------------------

struct TileVS
{
	float4 posH     : POSITION0;
	float2 texUV    : TEXCOORD0;  // Texture coordinate
	float4 camW		: TEXCOORD1;  // Radius in .w
	float3 nrmW		: TEXCOORD2;
#if defined(_SHDMAP)
	float4 shdH     : TEXCOORD3;
#endif
};

struct CldVS
{
	float4 posH     : POSITION0;
	float2 texUV    : TEXCOORD0;  // Texture coordinate
	float3 nrmW		: TEXCOORD1;
	float3 posW		: TEXCOORD2;
};

struct HazeVS
{
	float4 posH    : POSITION0;
	float2 texUV   : TEXCOORD0;
	float3 posW	   : TEXCOORD1;
	float  alpha   : COLOR0;
};


// Note: "bool" is 32-bits in a shaders (max count 16) 
//
struct FlowControlPS
{
	BOOL bInSpace;				// Camera in space (not in atmosphere)
	BOOL bBelowClouds;			// Camera is below cloud layer
	BOOL bOverlay;				// Overlay on/off	
	BOOL bShadows;				// Shadow Map on/off
	BOOL bLocals;				// Local Lights on/off
	BOOL bMicroNormals;			// Micro texture has normals
	BOOL bCloudShd;				// Cloud shadow textures valid and enabled
	BOOL bMask;					// Nightlights/water mask texture is enabled
	BOOL bRipples;				// Water riples texture is enabled
	BOOL bMicroTex;				// Micro textures exists and enabled
	BOOL bPlanetShadow;			// Use spherical approximation for shadow
	BOOL bEclipse;				// Eclipse is occuring
	BOOL bTexture;				// Surface texture exists
};

struct FlowControlVS
{
	BOOL bInSpace;				// Camera in space (not in atmosphere)
	BOOL bSpherical;			// Ignore elevation, render as sphere
	BOOL bElevOvrl;				// ElevOverlay on/off			
};

struct PerObjectParams
{
	float4x4 mWorld;			// World Matrix
	float4x4 mLVP;				// Light-View-Projection
	float4   vSHD;				// Shadow Map Parameters
	float4   vMSc[3];			// Micro Texture offset-scale
	float4	 vTexOff;			// Texture offset-scale
	float4   vCloudOff;			// Cloud texture offset-scale
	float4   vMicroOff;			// Micro texture offset-scale
	float4   vOverlayOff;       // Overlay texture offset-scale
	float4   vOverlayCtrl[4];
	float3	 vEclipse;			// Eclipse caster position (geocentric)
	float	 fEclipse;			// Eclipse data addressing scale factor. (to access tExlipse)
	float	 fAlpha;
	float	 fBeta;
	float	 fTgtScale;
};

uniform extern PerObjectParams Prm;
uniform extern FlowControlPS Flow;
uniform extern FlowControlVS FlowVS;
uniform extern _Light Lights;			// Note: DX9 doesn't tolerate structure arrays outside FX framework
uniform extern bool Spotlight[4];


sampler tDiff;				// Diffuse texture
sampler tMask;				// Nightlights / Specular mask texture
sampler tCloud;				// 1st Cloud shadow texture
sampler tCloud2;			// 2nd Cloud shadow texture
sampler tCloudMicro;		
sampler tCloudMicroNorm;
sampler tNoise;				//
sampler	tOcean;				// Ocean Normal Map Texture
sampler	tMicroA;
sampler	tMicroB;
sampler	tMicroC;
sampler tGlare;
sampler	tShadowMap;
sampler	tOverlay;
sampler	tMskOverlay;
sampler	tElvOverlay;
sampler	tEclipse;




// ---------------------------------------------------------------------------------------------------
//
float SampleShadows(float2 sp, float pd)
{
	if (sp.x < 0 || sp.y < 0) return 0.0f;	// If a sample is outside border -> fully lit
	if (sp.x > 1 || sp.y > 1) return 0.0f;

	if (pd < 0) pd = 0;
	if (pd > 2) pd = 2;

	float2 dx = float2(Prm.vSHD[1], 0) * 1.5f;
	float2 dy = float2(0, Prm.vSHD[1]) * 1.5f;
	float  va = 0;

	sp -= dy;
	if ((tex2D(tShadowMap, sp - dx).r) > pd) va++;
	if ((tex2D(tShadowMap, sp).r) > pd) va++;
	if ((tex2D(tShadowMap, sp + dx).r) > pd) va++;
	sp += dy;
	if ((tex2D(tShadowMap, sp - dx).r) > pd) va++;
	if ((tex2D(tShadowMap, sp).r) > pd) va++;
	if ((tex2D(tShadowMap, sp + dx).r) > pd) va++;
	sp += dy;
	if ((tex2D(tShadowMap, sp - dx).r) > pd) va++;
	if ((tex2D(tShadowMap, sp).r) > pd) va++;
	if ((tex2D(tShadowMap, sp + dx).r) > pd) va++;

	return va * 0.1111111f;
}

// -------------------------------------------------------------------------------------------------------------
// Local light sources
//
void LocalLights(
	out float3 diff_out,
	in float3 nrmW,
	in float3 posW)
{
	diff_out = 0;

	if (!Flow.bLocals) return;
	int i;

	// Relative positions
	float3 p[4];
	[unroll] for (i = 0; i < 4; i++) p[i] = posW - Lights.position[i];

	// Square distances
	float4 sd;
	[unroll] for (i = 0; i < 4; i++) sd[i] = dot(p[i], p[i]);

	// Normalize
	sd = rsqrt(sd);
	[unroll] for (i = 0; i < 4; i++) p[i] *= sd[i];

	// Distances
	float4 dst = rcp(sd);

	// Attennuation factors
	float4 att;
	[unroll] for (i = 0; i < 4; i++) att[i] = dot(Lights.attenuation[i].xyz, float3(1.0, dst[i], dst[i] * dst[i]));

	att = rcp(att);

	// Spotlight factors
	float4 spt = 1;
	
	[unroll] for (i = 0; i < 4; i++) {
		spt[i] = (dot(p[i], Lights.direction[i]) - Lights.param[i][Phi]) * Lights.param[i][Theta];
		if (!Spotlight[i]) spt[i] = 1.0f;
	}

	spt = saturate(spt);

	// Diffuse light factors
	float4 dif;
	[unroll] for (i = 0; i < 4; i++) dif[i] = dot(-p[i], nrmW);

	dif = saturate(dif);
	dif *= (att * spt);

	[unroll] for (i = 0; i < 4; i++) diff_out += Lights.diffuse[i].rgb * dif[i];
}


// Render Eclipse ------------------------------------------------------------
//
float GetEclipse(float3 vVrt)
{
	if (Flow.bEclipse)
	{
		float3 b = vVrt - Const.toSun * dot(vVrt, Const.toSun); // Flatten
		float  x = length(Prm.vEclipse - b) * Prm.fEclipse;
		return tex1D(tEclipse, saturate(x)).r;
	}
	return 1.0;
}
	


// ============================================================================
// Render SkyDome and Horizon
// ============================================================================

HazeVS HorizonVS(float3 posL : POSITION0)
{
	// Zero output.
	HazeVS outVS = (HazeVS)0;

	outVS.texUV = posL.xy*10.0;

	posL.xz *= lerp(Prm.vTexOff[0], Prm.vTexOff[1], posL.y);
	posL.y   = lerp(Prm.vTexOff[2], Prm.vTexOff[3], posL.y);

	outVS.posW = mul(float4(posL, 1.0f), Prm.mWorld).xyz;
	outVS.posH = mul(float4(outVS.posW, 1.0f), Const.mVP);

	return outVS;
}


// SkyDome Shader, Renders the sky from with-in atmosphere
//
float4 HorizonPS(HazeVS frg) : COLOR
{
	float fNoise = (tex2Dlod(tNoise, float4(frg.texUV, 0, 0)).r - 0.5f) * 0.03;

	float3 uDir = normalize(frg.posW);

	SkyOut sky = GetSkyColor(uDir);
	
	float ph = dot(uDir, Const.toSun);

	float2  guv = float2(dot(uDir, Const.ZeroAz), dot(uDir, Const.Up)) * GLARE_SIZE + 0.5f;
	float  cGlr = tex2D(tGlare, guv).r * saturate(ph) * Const.SunVis;
	
	float3 color = HDR(sky.ray.rgb * RayPhase(ph) + (sky.mie.rgb + 0.0008f) * MiePhase(ph) * (0.75f + cGlr * Const.cGlare));

	return float4(color + fNoise, sky.ray.a);
}


// Renders the horizon "ring" from space
//
float4 HorizonRingPS(HazeVS frg) : COLOR
{
	float3 uDir = normalize(frg.posW);
	float3 uOrt = normalize(uDir - Const.toCam * dot(uDir, Const.toCam));
	float3 vVrt = Const.CamPos + frg.posW;
	float d = dot(uDir, frg.posW);
	float x = dot(uOrt, Const.SunAz) * 0.5 + 0.5;
	float r = length(vVrt);
	float q = (r - Const.PlanetRad) / Const.AtmoAlt;

	float2 uv = float2(x, q > 0 ? sqrt(q) : 0);

	float4 cRay = tex2D(tSkyRayColor, uv).rgba;
	float3 cMie = tex2D(tSkyMieColor, uv).rgb;

	float ph = dot(uDir, Const.toSun);

	float3 color = HDR(cRay.rgb * RayPhase(ph) + cMie * MiePhase(ph));

	color *= GetEclipse(vVrt);

	return float4(color, cRay.a);
}



// ============================================================================
// Planet Surface Renderer
// ============================================================================

#define AUX_DIST		0	// Vertex distance
#define AUX_NIGHT		1	// Night lights intensity
#define AUX_SLOPE		2   // Terrain slope factor 0.0=flat, 1.0=sloped
#define AUX_RAYDEPTH	3   // Optical depth of a ray

TileVS TerrainVS(TILEVERTEX vrt)
{
	// Zero output.
	TileVS outVS = (TileVS)0;
	float4 vElev = 0;
	float3 vNrmW;
	
	// Apply a world transformation matrix
	float3 vPosW = mul(float4(vrt.posL, 1.0f), Prm.mWorld).xyz;
	float3 vVrt = Const.CamPos + vPosW;
	float3 vPlN = normalize(vVrt);

	if (FlowVS.bElevOvrl)
	{
		// ----------------------------------------------------------
		// Elevation Overlay
		//
		float2 vUVOvl = vrt.tex0.xy * Prm.vOverlayOff.zw + Prm.vOverlayOff.xy;

		// Sample Elevation Map
		vElev = tex2Dlod(tElvOverlay, float4(vUVOvl, 0, 0));

		// Construct world space normal
		vNrmW = float3(vElev.xy, sqrt(saturate(1.0f - dot(vElev.xy, vElev.xy))));
		vNrmW = mul(float4(vNrmW, 0.0f), Prm.mWorld).xyz;

		// Reconstruct Elevation
		vPosW += normalize(Const.CamPos + vPosW) * (vElev.z - vrt.elev) * vElev.w;	
	}
	else {
		vNrmW = mul(float4(vrt.normalL, 0.0f), Prm.mWorld).xyz;
	}

	// Disrecard elevation and make the surface spherical
	if (FlowVS.bSpherical) {
		vPosW = (normalize(Const.CamPos + vPosW) * Const.PlanetRad) - Const.CamPos;
		vNrmW = vPlN;
	}

	outVS.posH = mul(float4(vPosW, 1.0f), Const.mVP);

#if defined(_SHDMAP)
	outVS.shdH = mul(float4(vPosW, 1.0f), Prm.mLVP);
#endif

	outVS.texUV.xy = vrt.tex0.xy;
	outVS.camW = float4(-vPosW, dot(vVrt, vPlN));
	outVS.nrmW = vNrmW;
	
	return outVS;
}



bool InRange(float2 a)
{
	return (a.x > 0.0f && a.x < 1.0f) && (a.y > 0.0f && a.y < 1.0f);
}

float GGX_NDF(float dHN, float rgh)
{
	float r2 = rgh * rgh;
	float dHN2 = dHN * dHN;
	float d = (r2 * dHN2) + (1.0f - dHN2);
	return r2 / (3.14f * d * d);
}


float4 TerrainPS(TileVS frg) : COLOR
{

	float2 vUVSrf = frg.texUV.xy * Prm.vTexOff.zw + Prm.vTexOff.xy;
	float2 vUVWtr = frg.texUV.xy * Prm.vMicroOff.zw + Prm.vMicroOff.xy;
	float2 vUVCld = frg.texUV.xy * Prm.vCloudOff.zw + Prm.vCloudOff.xy;

	vUVWtr.x += Const.Time / 180.0f;

	float3 cNrm = float3(0.5, 0.5, 1.0);
	float fChA = 0.0f, fChB = 0.0f;

#if defined(_RIPPLES)
	if (Flow.bTexture) cNrm = tex2D(tOcean, vUVWtr).xyz;
#endif

	// Fetch Main Textures
	float4 cTex = float4(0.5, 0.5, 0.5, 1.0);
	if (Flow.bTexture) cTex = tex2D(tDiff, vUVSrf);

	float4 cMsk = float4(0, 0, 0, 1);
	if (Flow.bMask) cMsk = tex2D(tMask, vUVSrf);

#if defined(_DEVTOOLS)
	if (Flow.bOverlay) {
		float2 vUVOvl = frg.texUV.xy * Prm.vOverlayOff.zw + Prm.vOverlayOff.xy;
		if (InRange(vUVOvl)) {
			float4 cOvl = tex2D(tOverlay, vUVOvl);
			float4 cWtr = tex2D(tMskOverlay, vUVOvl);
			cTex.rgb = lerp(cTex.rgb, cOvl.rgb, cOvl.a * Prm.vOverlayCtrl[0].rgb);
			cMsk.rgb = lerp(cMsk.rgb, cWtr.rgb, cOvl.a * Prm.vOverlayCtrl[1].rgb);
			cMsk.a = lerp(cMsk.a, cWtr.a, Prm.vOverlayCtrl[1].a);
		}
	}
#endif

#if defined(_CLOUDSHD)
	if (Flow.bCloudShd) {
		fChA = tex2D(tCloud, vUVCld).a;
		fChB = tex2D(tCloud2, vUVCld - float2(1, 0)).a;
	}
#endif

	float fShadow = 1.0f;

#if defined(_SHDMAP)
	if (Flow.bShadows) {
		frg.shdH.xyz /= frg.shdH.w;
		frg.shdH.z = 1 - frg.shdH.z;
		float2 sp = frg.shdH.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
		float  pd = frg.shdH.z + 0.05f * Prm.vSHD[3];
		fShadow = 1.0f - SampleShadows(sp, pd);
	}
#endif

	float3 cFar, cMed, cLow;

#if defined(_MICROTEX)
	float2 UV = frg.texUV.xy;
	// Create normals
	if (Flow.bMicroTex)
	{
		if (Flow.bMicroNormals) {
			// Normal in .ag luminance in .b
			cFar = tex2D(tMicroC, UV * Prm.vMSc[2].zw + Prm.vMSc[2].xy).agb;	// High altitude micro texture C
			cMed = tex2D(tMicroB, UV * Prm.vMSc[1].zw + Prm.vMSc[1].xy).agb;	// Medimum altitude micro texture B
			cLow = tex2D(tMicroA, UV * Prm.vMSc[0].zw + Prm.vMSc[0].xy).agb;	// Low altitude micro texture A
		}
		else {
			// Color in .rgb no normals
			cFar = tex2D(tMicroC, UV * Prm.vMSc[2].zw + Prm.vMSc[2].xy).rgb;	// High altitude micro texture C
			cMed = tex2D(tMicroB, UV * Prm.vMSc[1].zw + Prm.vMSc[1].xy).rgb;	// Medimum altitude micro texture B
			cLow = tex2D(tMicroA, UV * Prm.vMSc[0].zw + Prm.vMSc[0].xy).rgb;	// Low altitude micro texture A
		}
	}
#endif

	float3 cRfl = 0;
	float3 nvrW = normalize(frg.nrmW);			// Per-pixel surface normal vector
	float3 vRay = normalize(frg.camW.xyz);		// Unit viewing ray
	float3 vVrt = Const.CamPos - frg.camW.xyz;	// Geo-centric pixel position
	float3 vPlN = normalize(vVrt);				// Planet mean normal
	float3 hlvW = normalize(vRay + Const.toSun);
	float   dst = dot(vRay, frg.camW.xyz);		// Pixel to camera distance
	float   rad = frg.camW.w;					// Pixel geo-distance
	float   alt = rad - Const.PlanetRad;		// Pixel altitude over mean radius
	float  fSrf = (1.0 - Const.CamSpace);		// Camera colse to surface ?
	float fMask = (1.0 - cMsk.a);				// Specular Mask
	float  fSpe = 0;
	float fAmpf = 1.0f;
	float fDRS = dot(vRay, Const.toSun);
	float fDPS = dot(vPlN, Const.toSun);		// Mean normal dot sun


#if defined(_WATER)
#if defined(_RIPPLES)

	// Compute world space normal for water rendering
	//
	cNrm.xy = (cNrm.xy - 0.5f) * 2.0f;
	cNrm.z *= Const.wNrmStr;
	cNrm = normalize(cNrm);

	float3 wnrmW = (Const.vTangent * cNrm.r) + (Const.vBiTangent * cNrm.g) + (vPlN * cNrm.b);
	wnrmW = lerp(nvrW, wnrmW, fMask);
	float fDWS = dot(wnrmW, Const.toSun); // Water normal dot sun

	// Render with specular ripples and fresnel water -------------------------
	//
	float fDCH = saturate(dot(vRay, hlvW));
	float fDCN = saturate(dot(vRay, wnrmW));
	float fDHN = dot(hlvW, wnrmW);

	float3 f = 1.0 - float3(fDCH, fDCN, fDWS);
	float3 fFresnel4 = f * f * f;
	float3 fF = (0.15f + fFresnel4 * 0.85f) * fMask * Const.wSpec;

	// Compute specular reflection intensity
	fSpe = GGX_NDF(fDHN, 0.1f + saturate(fDWS) * 0.1f) * fF.y;
	fSpe /= (4.0f * fDCH * max(fDWS, fDCN) + 1e-3);

	// Apply fresnel water only if close enough to a surface
	//
	if (!Flow.bInSpace)
	{
		cRfl = GetAmbient(reflect(-vRay, wnrmW)) * fF.y * fSrf;	
		// Attennuate diffuse texture for fresnel refl.
		cTex.rgb *= saturate(1.0f - f.y * fSrf * fMask) * saturate(1.0f - f.z * fSrf * fMask);
	}

	cTex.rgb = saturate(cTex.rgb + float3(0, 0.55, 1.0) * Const.wBrightness * fMask);

#else
	// Fallback to simple specular reflection
	float fDHN = dot(hlvW, nvrW);
	fSpe = pow(saturate(fDHN), 60.0f) * fMask * 5.0f;
#endif
#endif

	float3 nrmW = nvrW; // Micro normal defaults to vertex normal

	// Render with surface microtextures --------------------------------------
	//
#if defined(_MICROTEX)

	if (Flow.bMicroTex)
	{
		float step1 = smoothstep(15000, 3000, dst);
		step1 *= (step1 * step1);
		float3 cFnl = max(0, min(2, 1.333f * (cFar + cMed + cLow) - 1));

		// Create normals
		if (Flow.bMicroNormals)
		{
			cFnl = cFnl.bbb;

#if defined(_SOFT)
			float2 cMix = (cFar.rg + cMed.rg + cLow.rg) * 0.6666f;			// SOFT BLEND
#endif
#if defined(_MED)
			float2 cMix = (cFar.rg + 0.5f) * (cMed.rg + 0.5f) * (cLow.rg + 0.5f);	// MEDIUM BLEND
			fAmpf = 2.0f;
#endif
#if defined(_HARD)
			float2 cMix = cFar.rg * cMed.rg * cLow.rg * 8.0f;				// HARD BLEND
			fAmpf = 4.0f;
#endif

			float3 cNrm = float3((cMix - 1.0f) * 2.0f, 0) * step1;
			cNrm.z = cos(cNrm.x * cNrm.y * 1.57);

			// Approximate world space normal
			nrmW = normalize((Const.vTangent * cNrm.x) + (Const.vBiTangent * cNrm.y) + (nvrW * cNrm.z));

			// Bend the normal towards sun a bit
			nrmW = normalize(nrmW + Const.toSun * 0.06f);
		}

		// Apply luminance
		cTex.rgb *= lerp(1.0f, cFnl, step1);
	}
#endif


	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	float3 cDiffLocal = 0;

#if defined(_LOCALLIGHTS)
	LocalLights(cDiffLocal, nrmW, -frg.camW.xyz);
#endif

#if defined(_NO_ATMOSPHERE)

	float fDNS = saturate(dot(nvrW, Const.toSun));
	float fDCN = saturate(dot(nvrW, Const.toCam));
	float fLvl = 2.0f * fDNS / (fDNS + fDCN + 0.5f);
	float fSHD = 1.0f;

	// Shadowing by planet
	if (Flow.bPlanetShadow) {
		float palt = sqrt(saturate(1.0f - fDPS * fDPS)) * rad - Const.PlanetRad;
		fSHD = fDPS > 0 ? 1.0f : ilerp(Const.MinAlt, Const.MaxAlt, palt);
	}

	// Amplify light and shadows
	fLvl += dot(nvrW - vPlN, Const.toSun) * fLvl * Const.trLS;

	// Add opposition surge
	fLvl += pow(saturate(fDRS), 4.0f) * 0.3f * fDNS;

#if defined(_MICROTEX)
	fLvl += dot(nrmW - nvrW, Const.toSun) * ilerp(0.0, 0.03, fLvl) * fAmpf;
#endif

	fLvl *= fSHD;	// Apply planet shadow
	fLvl *= fECL;	// Apply eclipse

	float3 color = cTex.rgb * LightFX(max(fLvl, 0) * fShadow + cDiffLocal);
	return float4(pow(saturate(color * Const.TrExpo), Const.TrGamma), 1.0f);		// Gamma corrention
#else

	float fShd = 1.0f;

#if defined(_CLOUDSHD)
	// Do we render cloud shadows ?
	if (Flow.bCloudShd) {
		fShd = (vUVCld.x < 1.0 ? fChA : fChB);
		fShd = saturate(1.0 - fShd * Prm.fAlpha);
	}
#endif

	float3 cNgt = 0;
	float3 cNgt2 = 0;
	float fDNS = dot(nvrW, Const.toSun); // Vertex normal dot sun

#if defined(_NIGHTLIGHTS)

	// Night lights ?
	float fNgt = saturate(-fDPS * 4.0f + 0.05f) * Prm.fBeta; // Night lights intensity and 'on' time

	cMsk.b = (cMsk.b > 0.15f ? cMsk.b : 0.0f); // Blue dirt filter

	cNgt = cMsk.rgb * (1 - Const.CamSpace) * fNgt; // Nightlights surface texture illumination term
	cNgt2 = cMsk.rgb * Const.CamSpace * 4.0f * fNgt; // Nightlights orbital visibility
#endif

	float fNoise = (tex2Dlod(tNoise, float4(frg.texUV.xy * 4.0f * Prm.fTgtScale, 0, 0)).r - 0.5f) * ATMNOISE;

	// Terrain with gamma correction and attennuation
	cTex.rgb = pow(saturate(cTex.rgb), Const.TrGamma) * Const.TrExpo;

	// Evaluate ambient approximation
	float4 cAmb = AmbientApprox(vPlN, false);
	
	LandOut sct = GetLandView(rad, vPlN);

	// Get the color of sunlight and set maximum intensity to 1.0
	float3 cSun = GetSunColor(fDPS, alt);
	float3 cSF = cSun * Const.cSun;
	float fMx = max(max(cSF.r, cSF.g), cSF.b);
	cSF = fMx > 1.0 ? cSF / fMx : cSF;

	float  fL = Const.trLS * 0.3f;
	float  fZ = clamp(dot(nvrW - vPlN, Const.toSun) * Const.trLS, -fL, fL);
	float  fX = 1.0f - pow(1.0f - saturate(fDPS), 2.0f);

	fZ = fZ > 0 ? fZ * 2.0f : fZ;

#if defined(_MICROTEX)
	float  fG = dot(nrmW - nvrW, Const.toSun) * fAmpf;
#else
	float  fG = 0.0f;
#endif

	// Diffuse "lambertian" shading term
	float  fD = lerp(fX + (fG + fZ) * fX, fDPS * fDPS, fMask);

	// Water masking
	float  fM = 0.5f - fMask * 0.25f;

	// Ambient light for terrain
	//					  Color					   Distance				  Altitude factor		   Particle Density		
	float3 cA = normalize(cAmb.rgb + cSF * 4.0f) * cAmb.a * cAmb.g * fM * exp(-alt * Const.iH.r) * Const.rmI.r * 6e5 * Const.TW_Terrain;

	fShd = saturate(fShd + (1.0f - fX));

	// Bake light and shadow terms
	float3 cL = cSF * fD * fShadow * fShd;

	// Lit the texture with various things
	cTex.rgb *= cL * 2.0f + (cA + cDiffLocal + Const.cAmbient * Const.Ambient) * saturate(1.0f + fG + fZ) + cNgt;

	cTex.rgb = max(float3(0, 0, 0), cTex.rgb);

	// Add Reflection
	cTex.rgb += cRfl * 0.75f;

	// Add Specular component
	cTex.rgb += cSun * fSpe * smoothstep(-0.001f, 0.03f, fDPS);

	// Amplify cloud shadows for orbital views
	float fOrbShd = 1.0f - (1.0f - fShd) * Const.CamSpace * 0.5f;

	// Add Haze and night lights
	cTex.rgb *= sct.atn.rgb;
	cTex.rgb += (sct.ray.rgb * RayPhase(-fDRS) + sct.mie.rgb * MiePhase(-fDRS)) * fOrbShd * (1.0f + fNoise);

	cTex.rgb *= fECL;	// Apply eclipse
	cTex.rgb += cNgt2;

	return float4(HDR(cTex.rgb), 1.0f);
#endif
}






// ============================================================================
// Planet Cloud Renderer
// ============================================================================

CldVS CloudVS(TILEVERTEX vrt)
{
	// Zero output.
	CldVS outVS = (CldVS)0;

	// Apply a world transformation matrix
	float3 vPosW = mul(float4(vrt.posL, 1.0f), Prm.mWorld).xyz;
	float3 vNrmW = mul(float4(vrt.normalL, 0.0f), Prm.mWorld).xyz;

	outVS.posH = mul(float4(vPosW, 1.0f), Const.mVP);
	outVS.nrmW = vNrmW;
	outVS.posW = vPosW;
	outVS.texUV.xy = vrt.tex0.xy;						// Note: vrt.tex0 is un-used (hardcoded in Tile::CreateMesh and varies per tile)

	return outVS;
}


// ============================================================================
// 
float4 CloudPS(CldVS frg) : COLOR
{
	float2 vUVTex = frg.texUV.xy;
	float4 cTex = tex2D(tDiff, vUVTex);
	float3 vRay;
	float3 vPxl;
	float  dRC;
	float  fNrm = 1.0f;

	if (Flow.bBelowClouds) {
		float  rRef = Const.PlanetRad + Const.smi * 0.5f;	// Reference altitude
		float3 vRef = Const.toCam * rRef;
		vRay = normalize(Const.toCam * (Const.CamRad - rRef) + frg.posW); // Viewing ray to the pixel
		dRC = dot(vRay, Const.toCam);
		float  fEca2 = 1.0f - dRC * dRC;			// Ray horizon angle^2
		float  fD = Const.smi * rsqrt(1.0f - Const.ecc * Const.ecc * fEca2); // Distance to ellipse threshold
		vPxl = vRef + vRay * fD;					// Pretend the pixel being closer and lower
	}
	else {
		vRay = normalize(frg.posW);					// Viewing ray to the pixel
		dRC = dot(vRay, Const.toCam);
		vPxl = Const.CamPos + frg.posW;				// Pixel's geocentric location
	}

	float3 vPlN = normalize(vPxl);					// Mean Normal at pixel's locatin
	float3 vVrt = Const.CamPos + frg.posW.xyz;	// Geo-centric pixel position
	float3 nrm = vPlN;

	float dRS = dot(vRay, Const.toSun);
	float dMNus = dot(vPlN, Const.toSun);
	float dMN = saturate(dMNus);					// Mean normal sun angle
	float fPxR = dot(vPxl, vPlN);					// Pixel geo distance
	float fPxA = fPxR - Const.PlanetRad;			// Pixel altitude

	if (!Flow.bBelowClouds) fPxA = Const.CloudAlt;


	// -----------------------------------------------
	// Cloud layer rendering for Earth
	// -----------------------------------------------

#if defined(_CLOUDMICRO)
	float2 vUVMic = frg.texUV.xy * Prm.vMicroOff.zw + Prm.vMicroOff.xy;
	float4 cMic = tex2D(tCloudMicro, vUVMic);
#endif


#if defined(_CLOUDNORMALS)
#if defined(_CLOUDMICRO)

	float4 cMicNorm = tex2D(tCloudMicroNorm, vUVMic);  // Filename "cloud1_norm.dds"

	// Extract normal from transparency (height) data
	// Filter width
	float d = 2.0 / 512.0;

	float x1 = tex2D(tDiff, vUVTex + float2(-d, 0)).a;
	float x2 = tex2D(tDiff, vUVTex + float2(+d, 0)).a;
	nrm.x = (x1 * x1 - x2 * x2);

	float y1 = tex2D(tDiff, vUVTex + float2(0, -d)).a;
	float y2 = tex2D(tDiff, vUVTex + float2(0, +d)).a;
	nrm.y = (y1 * y1 - y2 * y2);

	// Blend in cloud normals only on moderately thick clouds, allowing the highest cloud tops to be smooth.
	nrm.xy = (nrm.xy + saturate((cTex.a * 10.0f) - 3.0f) * saturate(((1.0f - cTex.a) * 10.0f) - 1.0f) * (cMicNorm.rg - 0.5f)); // new

	// Increase normals contrast based on sun-earth angle.
	nrm.xyz = nrm.xyz * (1.0f + (0.5f * dMN));

	nrm.z = sqrt(1.0f - saturate(nrm.x * nrm.x + nrm.y * nrm.y));

	// Approximate world space normal from local tangent space
	nrm = normalize((Const.vTangent * nrm.x) + (Const.vBiTangent * nrm.y) + (vPlN * nrm.z));

	float dCS = dot(nrm, Const.toSun); // Cloud normal sun angle

	// Brighten the lighting model for clouds, based on sun-earth angle. Twice is better.
	// Low sun angles = greater effect. No modulation leads to washed out normals at high sun angles.
	dCS = saturate((1.0f - dMN) * (dCS * (1.0f - dCS)) + dCS);
	dCS = saturate((1.0f - dMN) * (dCS * (1.0f - dCS)) + dCS);

	// With a high sun angle, don't let the dCS go below 0.2 to avoid unnaturally dark edges.
	dCS = lerp(0.2f * dMN, 1.0f, dCS);

	// Effect of normal/sun angle to color
	// Add some brightness (borrowing red channel from sunset attenuation)
	// Adding it to the sun illumination factor, taking care to keep from saturating
	fNrm = dCS +((1.0f - dCS) * 0.2f);
#endif
#endif

#if defined(_CLOUDMICRO)
	float f = cTex.a;
	float g = lerp(1.0f, cMic.a, 1.0f - abs(dot(Const.vPolarAxis, vPlN)));
	float h = (g + 4.0f) * 0.2f;
	cTex.a = saturate(lerp(g, h, f) * f);
#endif

	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	if (Flow.bBelowClouds)
	{
		// Get sunlight color
		float3 cSun = GetSunColor(dMNus, fPxA);

		// Get ambient information
		float4 cMlt = AmbientApprox(vPlN);

		cSun *= saturate(dRS + 1.3f);
		float fPh = pow(saturate(1.0f - dRC), 32.0f) * pow(saturate(dRS), 10.0f); // Boost near horizon and close the sun
		cSun *= 1.0f + fPh * 8.0f;

		cSun *= Const.cSun * fNrm;
		cSun *= Const.Clouds;
		cSun += cMlt.rgb * cMlt.a * 0.2f;

		LandOut sct = GetLandView(fPxA + Const.PlanetRad, vPlN);

		cTex.rgb *= cSun;
		cTex.rgb *= sct.atn.rgb;
		cTex.rgb += sct.ray.rgb * 2.0f;
		cTex.rgb *= fECL;

		return float4(HDR(cTex.rgb), saturate(cTex.a));
	}
	else {

		// Get sunlight color
		float3 cSun = GetSunColor(dMN, fPxA);
	
		// Get ambient information
		float4 cAmb = AmbientApprox(dMNus);
		float3 cMSC = Const.RayWave * Const.RayWave * Const.Clouds; // Multiscatter color

		cSun = sqrt(cMSC * cMSC + cSun * cSun * fNrm) * cAmb.a;
		
		LandOut sct = GetLandView(fPxA + Const.PlanetRad, vPlN);

		cTex.rgb *= cSun;
		cTex.rgb *= sct.atn.rgb;
		cTex.rgb += sct.ray.rgb;
		cTex.rgb *= fECL;

		return float4(sqr(HDR(cTex.rgb * 4.0f)), cTex.a * cAmb.a * cAmb.a);
	}	
}








// ============================================================================
// AtmoFX volumetric cloud deck (prototype, Jupiter only)
// One deck between VD_TOP - VD_THICK and VD_TOP above the reference radius, its top surface a
// procedural flow-like height field (zonal streaks, domain-warped swirls, convective towers) that
// drifts with latitude-dependent zonal winds. Ray-marched in the cloud-layer pass (rays that meet
// the stock cloud shell) and in the surface pass (rays going down while the camera is under the
// deck top). Frame: geocentric world axes; latitude from the polar axis, longitude from the camera's
// meridian (tile world matrices carry a per-tile longitude rotation, so they cannot give a common
// planet frame in the shader; anchored to the camera the deck stays put under a hovering observer).
// ============================================================================

#define VD_TOP     480.0e3		// deck top altitude [m] (Jupiter.cfg CloudAlt)
#define VD_THICK   55.0e3		// deck thickness [m]
#define VD_DEEP    160.0e3		// murk below the deck that is marched [m]
#define VD_STEPS   40
#define VD_FEAT    70.0			// main feature size [km]

bool VD_IsTarget() { return abs(Const.PlanetRad - 69911.0e3) < 2.0e5 && Const.CamAlt < 2.0e7; }	// Jupiter, camera within 20000 km

float vd_hash(float2 p)
{
	p = frac(p * float2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return frac((p.x + p.y) * p.x);
}

float vd_noise(float2 x)
{
	float2 i = floor(x), f = frac(x);
	float2 u = f * f * (3.0 - 2.0 * f);
	float a = vd_hash(i), b = vd_hash(i + float2(1, 0)), c = vd_hash(i + float2(0, 1)), d = vd_hash(i + float2(1, 1));
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float vd_fbm(float2 x, int oct)
{
	float s = 0, a = 0.5;
	[loop] for (int i = 0; i < oct; i++) {
		s += a * vd_noise(x);
		x = float2(1.6 * x.x + 1.2 * x.y, -1.2 * x.x + 1.6 * x.y) + 17.0;
		a *= 0.5;
	}
	return s;
}

// geocentric point [m] -> flow coordinates [km] (x east, y north), advected by zonal winds
float2 vd_flow(float3 p)
{
	float3 pole = Const.vPolarAxis;
	float3 e0 = normalize(Const.toCam - pole * dot(Const.toCam, pole));
	float3 e1 = cross(pole, e0);
	float r = length(p);
	float lat = asin(clamp(dot(p, pole) / r, -1, 1));
	float lng = atan2(dot(p, e1), dot(p, e0));
	float t = Const.Time * 180.0;
	float wind = 110.0 * sin(lat * 7.0) + 45.0 * sin(lat * 13.0 + 1.0);	// m/s, alternating jets
	float cl = max(cos(lat), 0.05);
	lng -= wind * t / (Const.PlanetRad * cl);
	return float2(lng * Const.PlanetRad * cl, lat * Const.PlanetRad) * 0.001;
}

// deck top as a fraction of the thickness (0..1) at a geocentric point
float vd_top(float3 p, out float tow)
{
	float2 xy = vd_flow(p);
	float2 q = float2(xy.x * 0.33, xy.y) / VD_FEAT;					// streaks along the wind
	float2 w = float2(vd_fbm(q * 0.45, 3), vd_fbm(q * 0.45 + 5.2, 3)) - 0.5;
	float n = vd_fbm(q + w * 3.0, 4);
	tow = smoothstep(0.60, 0.78, vd_fbm(q * 2.7 + w * 5.0 + 11.0, 3));
	float top = saturate((n - 0.30) * 2.4);
	return saturate(top * 0.78 + tow * 0.35 * (0.4 + top));
}

// march [t0, t1] along a world-space ray from ro; returns (premultiplied colour, opacity)
float4 vd_march(float3 roW, float3 rdW, float t0, float t1, float3 albedo, float jitter)
{
	float3 ro = roW, rd = rdW, sn = Const.toSun;
	float Rbot = Const.PlanetRad + VD_TOP - VD_THICK;
	float dt = (t1 - t0) / VD_STEPS;
	float T = 1.0;
	float3 col = 0;
	float3 hazeC = albedo * float3(0.95, 0.92, 0.88);
	float3 deepC = albedo * float3(0.35, 0.27, 0.22);
	[loop] for (int i = 0; i < VD_STEPS; i++) {
		float t = t0 + (i + jitter) * dt;
		float3 p = ro + rd * t;
		float r = length(p);
		float h = (r - Rbot) / VD_THICK;									// 0 at the deck base, 1 at the top
		float sunUp = dot(p / r, sn);
		float day = saturate((sunUp + 0.08) * 5.0);
		float sigma, light;
		float3 c;
		if (h >= 0.0) {
			float tow;
			float top = vd_top(p, tow);
			float d = saturate((top - h) * 14.0) * saturate(h * 6.0 + 0.3);
			// self shadow: how far the deck top rises over this point towards the sun
			float tow2;
			float topS = vd_top(p + sn * 9.0e3, tow2);
			float occl = saturate((topS - h) * 4.0);
			light = day * (1.0 - 0.75 * occl) * (0.55 + 0.45 * saturate(h * 1.3));
			sigma = d * 4.0e-4 + 1.2e-5;										// cloud + clear-air haze [1/m]
			c = lerp(hazeC, albedo * (0.80 + 0.35 * h + 0.25 * tow), saturate(d * 3.0));
		}
		else {
			light = day * 0.35 * exp(h * 2.0);
			sigma = 2.5e-5;													// murk under the deck
			c = deepC;
		}
		float a = 1.0 - exp(-sigma * dt);
		col += T * a * c * LightFX(light + 0.18 * Const.Ambient);
		T *= 1.0 - a;
		if (T < 0.01) break;
	}
	return float4(col, 1.0 - T);
}

// distances along a ray to a sphere of radius R (x = near, y = far; y < 0 if missed)
float2 vd_sphere(float3 ro, float3 rd, float R)
{
	float b = dot(ro, rd), c = dot(ro, ro) - R * R;
	float D = b * b - c;
	if (D < 0) return float2(-1, -1);
	float s = sqrt(D);
	return float2(-b - s, -b + s);
}

// ============================================================================
// Gas Giant Renderer
// ============================================================================

TileVS GiantVS(TILEVERTEX vrt)
{
	// Zero output.
	TileVS outVS = (TileVS)0;
	
	// Apply a world transformation matrix
	float3 vPosW = mul(float4(vrt.posL, 1.0f), Prm.mWorld).xyz;
	float3 vNrmW = mul(float4(vrt.normalL, 0.0f), Prm.mWorld).xyz;
	
	outVS.posH = mul(float4(vPosW, 1.0f), Const.mVP);
	outVS.texUV.xy = vrt.tex0.xy;
	outVS.camW = float4(-vPosW, 0);
	outVS.nrmW = vNrmW;

	return outVS;
}

// ============================================================================
//
float4 GiantPS(TileVS frg, float2 vpos : VPOS) : COLOR
{

	float2 vUVSrf = frg.texUV.xy * Prm.vTexOff.zw + Prm.vTexOff.xy;
	
	// Fetch Main Textures
	float4 cTex = tex2D(tDiff, vUVSrf);
	
	float3 nrmW = normalize(frg.nrmW);			// Per-pixel surface normal vector
	float3 vRay = normalize(frg.camW.xyz);		// Unit viewing ray
	float3 vVrt = Const.CamPos - frg.camW.xyz;	// Geo-centric pixel position
	float3 vPlN = normalize(vVrt);				// Planet mean normal
	float  fDPS = dot(vPlN, Const.toSun);
	float3 cSun = saturate((fDPS + 0.1) * 5.0);


	// Render Eclipse ------------------------------------------------------------
	//
	cSun *= GetEclipse(vVrt);

	// Terrain with gamma correction and attennuation
	cTex.rgb = pow(saturate(cTex.rgb), Const.TrGamma) * Const.TrExpo;

	float3 color = cTex.rgb * LightFX(cSun + float3(0.9, 0.9, 1.0) * Const.Ambient);

	// AtmoFX: camera under the deck top - march the deck and the murk below it down to this pixel
	float Rtop = Const.PlanetRad + VD_TOP;
	if (VD_IsTarget() && Const.CamRad < Rtop) {
		float dist = length(frg.camW.xyz);
		float3 rd = -vRay;											// camera -> pixel
		float t1 = dist;
		float t0 = 0;
		float2 hb = vd_sphere(Const.CamPos, rd, Const.PlanetRad + VD_TOP - VD_THICK - VD_DEEP);
		if (hb.x > 0) t1 = min(t1, hb.x);
		float4 v = vd_march(Const.CamPos, rd, t0, t1, cTex.rgb, vd_hash(vpos));
		color = v.rgb + color * (1.0 - v.a) * 0.15;					// the "surface" far under the murk barely shows
	}

	return float4(HDR(color), 1.0f);
}


// ============================================================================
// Gas giant cloud layer renderer
// ============================================================================

float4 GiantCloudPS(CldVS frg, float2 vpos : VPOS) : COLOR
{
	float4 cTex = tex2D(tDiff, frg.texUV.xy);
	if (VD_IsTarget()) {
		// AtmoFX: the shell is only a carrier for the rays; the deck is marched under it
		float3 albedo = pow(saturate(cTex.rgb), Const.TrGamma) * Const.TrExpo;
		float3 rd = normalize(frg.posW);
		float dist = length(frg.posW);
		float Rtop = length(Const.CamPos + frg.posW);
		float t0, t1;
		if (Const.CamRad >= Rtop) {									// from above: shell point downwards
			t0 = dist;
			float2 hb = vd_sphere(Const.CamPos, rd, Const.PlanetRad + VD_TOP - VD_THICK - VD_DEEP);
			float2 ht = vd_sphere(Const.CamPos, rd, Rtop);
			t1 = (hb.x > t0) ? hb.x : max(ht.y, t0);
			t1 = min(t1, t0 + 3.0e6);
		}
		else {														// from below: camera up to the shell
			t0 = 0;
			t1 = dist;
		}
		float4 v = vd_march(Const.CamPos, rd, t0, t1, albedo, vd_hash(vpos));
		float fECLv = GetEclipse(Const.CamPos + frg.posW.xyz);
		float a = saturate(v.a);
		return float4(HDR(v.rgb * fECLv / max(a, 1e-3)), a);
	}
	float3 vPlN = normalize(frg.nrmW);
	float3 vRay = normalize(frg.posW);
	float3 vVrt = Const.CamPos + frg.posW.xyz;	// Geo-centric pixel position
	float  fDPS = dot(vPlN, Const.toSun);    // Planet mean normal sun angle

	float3 cSun = saturate((fDPS + 0.1) * 5.0);

	// Render Eclipse ------------------------------------------------------------
	//
	float fECL = GetEclipse(vVrt);

	cTex.rgb *= LightFX(cSun + float3(1.0, 1.0, 1.0) * Const.Ambient);
	cTex.rgb = pow(saturate(cTex.rgb), Const.TrGamma) * Const.TrExpo;
	cTex.rgb *= fECL;

	return float4(HDR(cTex.rgb), saturate(cTex.a));
}

// AtmoFX fork: Titan's haze and methane clouds, ray-marched in a reduced-resolution pass (see TitanVolume.h)
//
// Haze: the photochemical haze is optically thick (Huygens DISR: vertical optical depth ~3-5 in the visible).
//   Two exponential components (troposphere ~60 km scale, main haze ~65 km scale); the light inside comes from
//   above, reddened and dimmed with the optical depth over the point. Diving in, contrast and outlines are lost.
// Clouds (Cassini ISS/VIMS):
//   * northern summer 2016-2017: long E-W streaks between ~45 and ~55 N, hundreds of km long, drifting
//     7-10 m/s, forming and fading in hours; cover is low (global cloud fraction ~0.2-0.6 %);
//   * convective storms (e.g. the 2010 equatorial "arrow", ~1200-1500 km): towers through tens of km,
//     anvils at the top. Off by default; a preset in Config\TitanFX_Volume.cfg.
// Units: km, planet frame (y = north pole). Output: premultiplied colour, alpha = what the haze and clouds hide.

float3 gCam;			// camera [km]
float3 gSun;			// unit sun direction
float  gR;				// mean radius [km]
float  gGround;			// liquid/ground reference level [km above gR] (ray end)
float  gTop;			// top of the marched atmosphere [km]
float4 gHaze;			// kLow [1/km at h=0], HLow [km], kUp [1/km at h=0], HUp [km]
float3 gHazeLight;		// light at the top of the haze (colour * intensity)
float3 gHazeAbs;		// reddening per unit optical depth above the point
float4 gHazeP;			// brightness, forward-scatter share, twilight width, reddening cap
float4 gHazeLow;		// below ~80 km: brightness scale, forward-scatter share (x <= 0: off)
// streaks
float4 gStA;			// latMin, latMax [deg], base, top [km]
float4 gStB;			// coverage, size across [km], E-W stretch, lifetime [h]
// convective cells
float4 gCuA;			// latMin, latMax [deg], base, top [km]
float4 gCuB;			// coverage, cell size [km], anvil spread, -
float4 gStorm;			// storm centre (unit vector), radius [km] (0 = no storm)
float4 gWx;				// density [1/km], fine size [km], wind [km/s eastward], fine share
float3 gCloudAlb;		// cloud albedo
float  gSunGain;		// direct sunlight on clouds relative to the diffuse light
float  gExpo;
float  gTime;			// sim time [s]
float  gPixAng;			// pixel angle [rad] in the pass
float  gFade;			// whole-pass fade (camera altitude)
float2 gLowTexel;
// near haze in depth-tested slices (the haze up to gNear is drawn by Slice, the march starts at gNear)
float  gNear;			// [km] view depth where the march starts (0: no slices, e.g. from orbit)
float3 gCamFwd;			// camera view axis, planet frame
float2 gSlice;			// view depth of the slice's near and far edge [km]
float  gLayerZ;			// projected depth of the quad (slice far edge, or the march start for the composite)
// fog over evaporating methane at the summer pole (Cassini VIMS, Brown et al. 2009: optical depth ~0.25,
// scattering near 0.75 km, tops above 3 km excluded; seen to come and go between observations)
float4 gFogA;			// extinction [1/km] inside, top [km], polar edge latitude [deg], patch coverage
float4 gFogB;			// patch size [km], life [h], summer hemisphere sign (+1 north, -1 south, 0 none), -
int    gSteps;

texture tNoise3D;
texture tLow;

sampler3D sNoise = sampler_state {
	Texture = <tNoise3D>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};
sampler2D sLow = sampler_state {
	Texture = <tLow>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Clamp; AddressV = Clamp;
};

struct VSIn  { float3 pos : POSITION; float3 ray : TEXCOORD0; };
struct VSOut { float4 pos : POSITION; float3 ray : TEXCOORD0; };

VSOut CloudVS(VSIn i)
{
	VSOut o;
	o.pos = float4(i.pos.xy, 0.5, 1.0);
	o.ray = i.ray;
	return o;
}

float2 Sphere(float3 ro, float3 rd, float R)
{
	float b = dot(ro, rd), c = dot(ro, ro) - R * R;
	float D = b * b - c;
	if (D < 0) return float2(-1, -1);
	float s = sqrt(D);
	return float2(-b - s, -b + s);
}

float Lod(float dist, float tile) { return max(0.0, log2(max(dist * gPixAng * 128.0 / tile, 1e-4))); }
float Remap(float v, float a, float b) { return saturate((v - a) / max(b - a, 1e-4)); }
float HG(float c, float g) { float g2 = g * g; return (1.0 - g2) / (12.566 * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5)); }

// ---------------------------------------------------------------- haze
float HazeK(float h) { h = max(h, 0.0); return gHaze.x * exp(-h / gHaze.y) + gHaze.z * exp(-h / gHaze.w); }

// fog patches at the summer pole: low (top gFogA.y), sporadic, drifting slowly (surface winds < 1 m/s, Huygens)
float FogK(float3 p)
{
	if (gFogA.x <= 0.0 || gFogB.z == 0.0) return 0.0;
	float r = length(p), h = r - gR;
	if (h > gFogA.y * 1.3) return 0.0;
	float lat = degrees(asin(clamp(p.y / r, -1.0, 1.0))) * gFogB.z;		// latitude towards the summer pole
	float band = smoothstep(gFogA.z - 5.0, gFogA.z + 3.0, lat);
	if (band <= 0.0) return 0.0;
	float lon = atan2(p.z, p.x);
	float3 q = float3(gR * cos(radians(lat)) * lon, gR * radians(lat), gTime / (gFogB.y * 3600.0)) / float3(gFogB.x, gFogB.x, 1.0);
	float n = tex3Dlod(sNoise, float4(q + 0.41, 0)).r * 0.7 + 0.3 * tex3Dlod(sNoise, float4(q * float3(2.7, 2.7, 1.0) + 0.17, 0)).r;
	float patch = Remap(n * band, 1.0 - gFogA.w, 1.0 - gFogA.w * 0.4);
	float prof = 1.0 - smoothstep(gFogA.y * 0.7, gFogA.y * 1.3, h);
	return gFogA.x * patch * prof;
}
float AllK(float3 p) { float h = length(p) - gR; return HazeK(h) + FogK(p); }
float TauAbove(float h) { h = max(h, 0.0); return gHaze.x * gHaze.y * exp(-h / gHaze.y) + gHaze.z * gHaze.w * exp(-h / gHaze.w); }

// diffuse light inside the haze at altitude h, local sun elevation mu
float3 HazeJ(float h, float mu)
{
	float day = smoothstep(-gHazeP.z, 0.12, mu);					// the high haze stays lit past the terminator
	float slant = 0.6 + 0.4 * saturate(mu * 2.0);
	// near the ground the diffuse light stays pale (Huygens DISR: the surface sky has the same tan hue as the
	// ground, R/G ~1.34, G/B ~1.67): multiple scattering keeps it from reddening further - cap the colour shift
	// below ~80 km (gHazeP.w = cap in optical depth; 0 = off); the upper layers and the approach stay as they were
	float tau = TauAbove(h);
	float tr = tau;
	if (gHazeP.w > 0) tr = min(tau, lerp(gHazeP.w, 1e3, smoothstep(30.0, 80.0, h)));
	return gHazeLight * exp(-tr * gHazeAbs * 0.75) * exp(-(tau - tr) * 0.2) * (day * slant * gHazeP.x);
}

// direct sunlight reaching altitude h at local sun elevation mu
float3 SunAt(float h, float mu)
{
	float m = max(mu, 0.03);
	return gHazeLight * exp(-TauAbove(h) / m * (0.5 + gHazeAbs)) * smoothstep(-0.02, 0.05, mu);
}

// ---------------------------------------------------------------- clouds
// lon/lat coordinates on the sphere [km]: x east, y north, z up (seam at 180 deg, far from the sites in use)
float3 Geo(float3 p, out float latDeg)
{
	float r = length(p);
	float lat = asin(clamp(p.y / r, -1.0, 1.0));
	float lon = atan2(p.z, p.x);
	latDeg = degrees(lat);
	return float3(gR * cos(lat) * lon, gR * lat, r - gR);
}

float Band(float lat, float a, float b) { return smoothstep(a - 3.0, a + 2.0, lat) * (1.0 - smoothstep(b - 2.0, b + 3.0, lat)); }

// x = streaks, y = convective cells
float2 Density(float3 p, float dist, bool detail)
{
	float lat;
	float3 g = Geo(p, lat);
	float h = g.z;
	float2 d = 0;
	float2 drift = float2(gWx.z * gTime, 0.0);						// eastward drift
	// streaks: E-W bands, hundreds of km long; the pattern slowly re-forms (slides through the noise in z)
	if (h > gStA.z && h < gStA.w) {
		float band = Band(lat, gStA.x, gStA.y);
		if (band > 0.0) {
			float s = gStB.y;
			float evol = gTime / (gStB.w * 3600.0);
			float3 q = float3((g.x - drift.x) / (s * gStB.z), g.y / s, evol * 0.35 + g.z / (s * 0.8));
			float n = tex3Dlod(sNoise, float4(q + 0.21, Lod(dist, s))).r;
			n = n * 0.75 + 0.25 * tex3Dlod(sNoise, float4(q * float3(2.3, 2.1, 1.0) + 0.63, Lod(dist, s * 0.45))).r;
			float hf = (h - gStA.z) / (gStA.w - gStA.z);
			float prof = saturate(hf * 3.0) * saturate((1.0 - hf) * 2.0);
			float v = Remap(n * prof, 1.0 - gStB.x * band, 1.0);
			if (detail && v > 0.0) {
				float f = tex3Dlod(sNoise, float4(float3(g.x - drift.x, g.y, g.z * 3.0) / gWx.y + 0.37, Lod(dist, gWx.y))).g;
				v = Remap(v, f * gWx.w, 1.0);
			}
			d.x = v * gWx.x;
		}
	}
	// convective cells: a latitude belt and/or a storm cluster; tops grow with the strength, anvils spread
	if (h > gCuA.z && h < gCuA.w + 2.0) {
		float cov = gCuB.x * Band(lat, gCuA.x, gCuA.y);
		if (gStorm.w > 0.0) {
			float ds = gR * acos(clamp(dot(normalize(p), gStorm.xyz), -1.0, 1.0));
			cov = max(cov, 0.7 * exp(-ds * ds / (gStorm.w * gStorm.w)));
		}
		if (cov > 0.0) {
			float s = gCuB.y;
			float3 q = float3(g.x - drift.x * 0.7, g.y, g.z * 1.4) / s;
			float c = tex3Dlod(sNoise, float4(q + 0.31, Lod(dist, s))).r;
			c = c * 0.7 + 0.3 * tex3Dlod(sNoise, float4(q.yzx * 2.2 + 0.77, Lod(dist, s * 0.45))).r;
			float strength = Remap(c, 1.0 - cov, 1.0);					// where a cell is, how strong
			float top = lerp(gCuA.z + 3.0, gCuA.w, strength);
			float hf = (h - gCuA.z) / max(top - gCuA.z, 0.5);
			float prof = saturate(hf * 5.0) * saturate((1.0 - hf) * 3.0);
			float anvil = gCuB.z * smoothstep(0.6, 0.95, hf) * saturate((1.0 - hf) * 8.0) * strength;
			float v = saturate(strength * prof * 1.6 + anvil);
			if (detail && v > 0.0) {
				float f = tex3Dlod(sNoise, float4(float3(g.x, g.y, g.z * 2.0) / gWx.y + 0.53, Lod(dist, gWx.y))).g;
				v = Remap(v, f * gWx.w, 1.0);
			}
			d.y = v * gWx.x;
		}
	}
	return d;
}

float4 CloudPS(VSOut i, float2 vpos : VPOS) : COLOR
{
	float3 rd = normalize(i.ray);
	float3 ro = gCam;
	float jit = frac(sin(dot(vpos, float2(12.9898, 78.233))) * 43758.5453);
	float cosT = dot(rd, gSun);

	// ray end: the ground reference sphere or the top of the marched atmosphere
	float2 tTop = Sphere(ro, rd, gR + gTop);
	if (tTop.y < 0) return 0;
	float tA = max(tTop.x, 0.0), tE = tTop.y;
	if (gNear > 0.0) tA = max(tA, gNear / max(dot(rd, gCamFwd), 0.05));		// the slices cover the near part
	float2 tG = Sphere(ro, rd, gR + gGround);
	if (tG.x > 0) tE = min(tE, tG.x);
	if (tE <= tA) return 0;

	// ---- haze: 48 steps; near the ground denser at the camera, from high up/orbit even (the dense part is far away)
	const int NH = 48;
	float pw = lerp(2.0, 1.0, saturate((length(ro) - gR - 30.0) / 120.0));
	float T = 1.0;
	float3 hz = 0;
	float hgv = HG(cosT, 0.6) * 12.566;
	float phase = lerp(1.0, hgv, gHazeP.y);
	// near the ground (Huygens): the multiply scattered sky is brighter away from the Sun and less peaked towards it
	float phaseLow = gHazeLow.x > 0 ? lerp(1.0, hgv, gHazeLow.y) * gHazeLow.x : phase;
	float tPrev = tA;
	[loop] for (int k = 0; k < NH; k++) {
		float u = (k + 1.0) / NH;
		float tN = tA + (tE - tA) * pow(u, pw);
		float tm = lerp(tPrev, tN, jit);
		float dt = tN - tPrev;
		float3 p = ro + rd * tm;
		float r = length(p);
		float h = r - gR;
		float mu = dot(p / r, gSun);
		float kx = HazeK(h) + FogK(p);
		float a = 1.0 - exp(-kx * dt);
		hz += T * a * HazeJ(h, mu) * lerp(phaseLow, phase, smoothstep(30.0, 80.0, h));
		T *= 1.0 - a;
		tPrev = tN;
	}

	// ---- clouds in their slab
	float lo = gR + min(gStA.z, gCuA.z), hi = gR + max(gStA.w, gCuA.w + 2.0);
	float3 cc = 0;
	float A = 0;
	float2 tHi = Sphere(ro, rd, hi);
	if (tHi.y > 0) {
		float camR = length(ro);
		float t0 = camR > hi ? tHi.x : 0.0, t1 = tHi.y;
		float2 tLo = Sphere(ro, rd, lo);
		if (camR < lo) t0 = max(t0, tLo.y);
		else if (tLo.x > 0) t1 = min(t1, tLo.x);
		t1 = min(t1, tE);
		if (t1 > t0) {
			int N = gSteps;
			float dt = (t1 - t0) / N;
			float Tc = 1.0;
			// haze optical depth from the camera to the slab entry (8-point midpoint)
			float tau = 0;
			[unroll] for (int m = 0; m < 8; m++) tau += HazeK(length(ro + rd * (t0 * (m + 0.5) / 8.0)) - gR);
			tau *= t0 / 8.0;
			if (tau > 7.0) t1 = t0;										// hidden by the haze (e.g. from orbit): skip the clouds
			float cph = lerp(HG(cosT, 0.65), HG(cosT, -0.2), 0.3) * 12.566;
			[loop] for (int k = 0; k < 96; k++) {
				if (k >= N || Tc < 0.02) break;
				float t = t0 + (k + jit) * dt;
				float3 p = ro + rd * t;
				float r = length(p);
				float h = r - gR;
				tau += HazeK(h) * dt;
				float2 d2 = Density(p, t, true);
				float dens = d2.x + d2.y;
				if (dens > 0.001) {
					float mu = dot(p / r, gSun);
					float od = 0;
					[unroll] for (int s = 1; s <= 3; s++) {
						float2 ds = Density(p + gSun * (1.2 * s * s), t, false);
						od += (ds.x + ds.y) * 1.2 * (2 * s - 1);
					}
					float Ts = exp(-od * 0.8);
					float powder = 1.0 - exp(-dens * 3.0);
					float hf = saturate((h - gCuA.z) / max(gCuA.w - gCuA.z, 1.0));
					float3 L = gCloudAlb * (HazeJ(h, mu) * lerp(0.55, 1.15, hf) + SunAt(h, mu) * gSunGain * Ts * cph * powder);
					float a = 1.0 - exp(-dens * dt);
					float th = exp(-tau);
					cc += Tc * a * L * th;
					A += Tc * a * th;
					Tc *= 1.0 - a;
				}
			}
		}
	}

	// clouds (already dimmed by the haze in front of them) + the haze, over what the client drew
	float3 col = cc + hz * (1.0 - A);
	float alpha = 1.0 - (1.0 - A) * T;
	return float4(col * gExpo, alpha);
}

// ---------------------------------------------------------------- near haze slice: [gSlice.x, gSlice.y] view depth
VSOut SliceVS(VSIn i)
{
	VSOut o;
	o.pos = float4(i.pos.xy, gLayerZ, 1.0);
	o.ray = i.ray;
	return o;
}

float4 SlicePS(VSOut i) : COLOR
{
	float3 rd = normalize(i.ray);
	float c = max(dot(rd, gCamFwd), 0.05);
	float t0 = gSlice.x / c, t1 = gSlice.y / c;					// along the ray [km]
	float cosT = dot(rd, gSun);
	float hgv = HG(cosT, 0.6) * 12.566;
	float phase = lerp(1.0, hgv, gHazeP.y);
	float phaseLow = gHazeLow.x > 0 ? lerp(1.0, hgv, gHazeLow.y) * gHazeLow.x : phase;
	float3 col = 0;
	float tau = 0;
	[unroll] for (int k = 0; k < 2; k++) {
		float tm = lerp(t0, t1, (k + 0.5) * 0.5);
		float3 p = gCam + rd * tm;
		float r = length(p), h = r - gR;
		float kx = HazeK(h) + FogK(p);
		float dt = (t1 - t0) * 0.5;
		float a = 1.0 - exp(-kx * dt);
		col += exp(-tau) * a * HazeJ(h, dot(p / r, gSun)) * lerp(phaseLow, phase, smoothstep(30.0, 80.0, h));
		tau += kx * dt;
	}
	return float4(col * gExpo * gFade, (1.0 - exp(-tau)) * gFade);
}

struct CompOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

CompOut CompVS(VSIn i)
{
	CompOut o;
	o.pos = float4(i.pos.xy, gLayerZ, 1.0);
	o.uv = float2(i.pos.x * 0.5 + 0.5, 0.5 - i.pos.y * 0.5) + gLowTexel;
	return o;
}

// 3x3 tent: hides the per-pixel jitter without the blur of a wide kernel
float4 CompPS(CompOut i) : COLOR
{
	float2 o = gLowTexel * 2.0;
	float4 c = 0;
	[unroll] for (int y = -1; y <= 1; y++)
		[unroll] for (int x = -1; x <= 1; x++)
			c += tex2D(sLow, i.uv + float2(x, y) * o) * ((x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0));
	return c / 16.0 * gFade;
}

technique Clouds
{
	pass P0
	{
		VertexShader = compile vs_3_0 CloudVS();
		PixelShader = compile ps_3_0 CloudPS();
		ZEnable = false;
		ZWriteEnable = false;
		CullMode = None;
		AlphaBlendEnable = false;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

technique Slice
{
	pass P0
	{
		VertexShader = compile vs_3_0 SliceVS();
		PixelShader = compile ps_3_0 SlicePS();
		ZEnable = true;
		ZWriteEnable = false;
		ZFunc = LessEqual;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

technique Composite
{
	pass P0
	{
		VertexShader = compile vs_3_0 CompVS();
		PixelShader = compile ps_3_0 CompPS();
		ZEnable = true;
		ZFunc = LessEqual;
		ZWriteEnable = false;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

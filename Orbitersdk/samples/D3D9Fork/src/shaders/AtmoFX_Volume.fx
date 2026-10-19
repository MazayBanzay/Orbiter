// ============================================================================
// AtmoFX fork of D3D9Client: Jupiter's cloud layers, ray-marched.
// Full-screen pass (half resolution) after the planet; rays in the planet frame [km], camera at gCam.
// Layers (radii in gHaze/gNH3/gNH4/gH2O: x base, y top, z density scale), after the Galileo probe and
// equilibrium-condensation models:
//   haze   0.03-0.3 bar  thin photochemical haze, yellow-brown; the limb and the colours of the belts
//   NH3    0.4-0.9 bar   the visible deck: thick and high in zones, thin in belts (flow-map coverage)
//   NH4SH  1.5-2.5 bar   brown deck seen through the belts
//   H2O    4-7 bar       deep sparse towers, lightning
// below: dark murk. Sunlight reaching a point is dimmed by the column of the layers above it
// (analytic, slant by the sun elevation) and by a short local march (self-shadowing).
// Weather map: rgb colour, a coverage (zones high, belts low, hot spots ~0), advected by zonal jets.
// ============================================================================

#define VD_MAXSTEPS 128

float3 gCam;		// camera, planet frame [km]
float3 gSun;		// sun direction, planet frame
float  gR;			// planet radius [km]
float4 gHaze;		// x base radius, y top radius, z extinction [1/km at base]
float4 gNH3;		// x base radius, y top radius, z density scale
float4 gNH4;
float4 gH2O;
float  gDeep;		// radius below which only murk is seen
float  gTime;		// simulation time [s]
float  gMapKm;		// weather map tile [km]
float  gNoiseKm;	// shape noise tile [km]
float  gDetailKm;	// detail noise tile [km]
float  gMaxDist;	// march range [km]
float  gExpo;
int    gSteps;
float  gNearKm;		// cloud density fades in over this distance from the camera (no smeared puffs at the lens)
float  gCloudRot;	// stock cloud layer rotation [rad]: the global map is drawn in its phase
float  gGlobalFlip;	// 1: mirror the global map in longitude (if it shows mirrored against the stock layer)
float  gGlobalMix;
float  gRealTime;	// real time [s] for lightning (time acceleration must not turn it into flicker)
float  gLightning;	// 0 off, 1 on
float4 gGRS;		// Great Red Spot: centre longitude, latitude [rad, planet frame], semi-axes east-west and north-south [km]
float  gFade;		// fades the whole pass out towards MaxCamAlt (no hard switch to the stock view)	// how much of the colour and coverage comes from the global map
float  gPixAng;		// angular size of one pass pixel [rad]: mip level of the noise by distance

texture tNoise3D;
texture tWeather;
texture tFlowDir;		// streak direction of the real map: rg = cos/sin of twice the angle, b = coherence
texture tGlobal;		// stock Jupiter cloud map, equirectangular: rgb colour, a coverage
texture tLow;			// half-resolution result (premultiplied colour, opacity)
float2  gLowTexel;		// 0.5 / low-res size (D3D9 texel centre)
texture tBlue;			// 64x64 blue noise (void-and-cluster): step jitter without coarse grain or rings
float   gBlueOn;
sampler2D sBlue = sampler_state {
	Texture = <tBlue>; MinFilter = Point; MagFilter = Point; MipFilter = None;
	AddressU = Wrap; AddressV = Wrap;
};
float Jitter(float2 vpos)
{
	if (gBlueOn > 0.5) return tex2Dlod(sBlue, float4((vpos + 0.5) / 64.0, 0, 0)).r;
	return frac(sin(dot(vpos, float2(12.9898, 78.233))) * 43758.5453);
}

sampler3D sNoise = sampler_state {
	Texture = <tNoise3D>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Wrap; AddressV = Wrap; AddressW = Wrap;
};
sampler2D sWeather = sampler_state {
	Texture = <tWeather>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Wrap; AddressV = Wrap;
};
sampler2D sFlowDir = sampler_state {
	Texture = <tFlowDir>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Wrap; AddressV = Clamp;
};
sampler2D sGlobal = sampler_state {
	Texture = <tGlobal>; MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;
	AddressU = Wrap; AddressV = Clamp;
};
sampler2D sLow = sampler_state {
	Texture = <tLow>; MinFilter = Linear; MagFilter = Linear; MipFilter = None;
	AddressU = Clamp; AddressV = Clamp;
};

struct VSIn  { float3 pos : POSITION; float3 ray : TEXCOORD0; };
struct VSOut { float4 pos : POSITION; float3 ray : TEXCOORD0; };

VSOut DeckVS(VSIn i)
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

float HG(float c, float g) { float g2 = g * g; return (1 - g2) / (12.566 * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5)); }

float Hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }

// planet-frame point [km] -> flow coordinates [km]: x east, y south, z altitude.
// The cloud cells (not the real map, which stays in step with the stock layer) drift with an approximate
// zonal wind profile of Jupiter and turn counter-clockwise inside the Great Red Spot (~5 day period at its rim).
float3 FlowKm(float3 p)
{
	float r = length(p);
	float lat = asin(clamp(p.y / r, -1, 1));
	float lng = atan2(p.z, p.x);
	float latd = lat * 57.29578;
	// km/s: equatorial jet, the strong 23.5 N jet, alternating jets ~12 deg apart (approximation, not the measured curve)
	float wind = 0.100 * exp(-latd * latd / 64.0) + 0.150 * exp(-(latd - 23.5) * (latd - 23.5) / 6.25) + 0.040 * cos(latd * 0.5236);
	float cl = max(cos(lat), 0.05);
	// Great Red Spot: rotate around its centre, fading out at its rim
	float dl = lng - gGRS.x; dl -= 6.2831853 * floor(dl / 6.2831853 + 0.5);
	float2 d = float2(dl * gR * cos(gGRS.y), -(lat - gGRS.y) * gR);
	float rn = length(d / gGRS.zw);
	float inGRS = 1.0 - smoothstep(0.7, 1.15, rn);
	float a = 1.5e-5 * gTime * inGRS;
	float2 dr = float2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a));
	// the turn is added as an offset to the ordinary coordinates: it fades to zero at the rim, so there is no seam
	float x = lng * gR * cl + (dr.x - d.x) - wind * (1.0 - inGRS) * gTime;
	float y = -lat * gR + (dr.y - d.y);
	return float3(x, y, r - gR);
}

// global map uv in the phase of the stock cloud layer (Orbiter tiles: u = (lng + pi) / 2pi, v from the north)
float2 GlobalUV(float3 p)
{
	float r = length(p);
	float lat = asin(clamp(p.y / r, -1, 1));
	float lng = atan2(p.z, p.x) + gCloudRot;
	float u = frac(lng / 6.2831853 + 0.5);
	return float2(gGlobalFlip > 0.5 ? 1.0 - u : u, 0.5 - lat / 3.1415927);
}

#define HAZEC float3(0.70, 0.55, 0.36)										// photochemical haze
#define DEEPC float3(0.10, 0.07, 0.05)

// cloud colour: the real map's colour (cream zones, ochre belts, blue-grey festoons, the red Spot),
// only slightly darker and warmer lower in the deck
float3 CloudAlbedo(float3 map, float h)
{
	float3 c = pow(saturate(map), 1.15) * 1.05;
	return saturate(c * lerp(float3(0.82, 0.74, 0.66), float3(1.0, 1.0, 1.0), saturate(h)));
}

float Band(float r, float4 L, float lo, float hi)							// 0..1 inside a layer with soft base and top
{
	float h = (r - L.x) / (L.y - L.x);
	return smoothstep(0.0, lo, h) * (1.0 - smoothstep(1.0 - hi, 1.0, h));
}

// mip level for a noise tiled every 'tile' km, seen 'dist' km away (one pixel = dist * gPixAng km; 128 texels a tile)
float Lod(float dist, float tile) { return max(0.0, log2(max(dist * gPixAng * 128.0 / tile, 1e-4))); }

// all layers at a point: x NH3, y NH4SH, z H2O (cloud densities), w haze extinction [1/km]; dist: from the camera
float4 Layers(float3 q, float r, float cov, float fine, float2 sm, bool detail, float dist)
{
	float4 d = 0;
	float lodN = Lod(dist, gNoiseKm);
	// NH3: the visible deck; zones reach higher
	if (r > gNH3.x && r < gNH3.y) {
		float n;
		if (detail) {		// cells smeared along the map's streaks (zonal in the belts, around the Spot)
			float3 o = float3(sm * gNoiseKm * 0.8, 0);
			n = (tex3Dlod(sNoise, float4((q - 2.0 * o) / gNoiseKm, lodN)).r + tex3Dlod(sNoise, float4((q - o) / gNoiseKm, lodN)).r
			   + tex3Dlod(sNoise, float4(q / gNoiseKm, lodN)).r
			   + tex3Dlod(sNoise, float4((q + o) / gNoiseKm, lodN)).r + tex3Dlod(sNoise, float4((q + 2.0 * o) / gNoiseKm, lodN)).r) * 0.2;
			n = saturate(0.5 + (n - 0.5) * (1.0 + 0.8 * dot(sm, sm)));		// keep the contrast the averaging takes away
		}
		else n = tex3Dlod(sNoise, float4(q / gNoiseKm, lodN)).r;
		float3 qr = float3(0.8 * q.x - 0.6 * q.y, 0.6 * q.x + 0.8 * q.y, q.z);		// rotated ~37 deg: periods do not line up
		n = n * 0.6 + 0.4 * tex3Dlod(sNoise, float4(qr / (gNoiseKm * 2.37) + 0.29, max(lodN - 1.25, 0))).r;	// breaks the noise tile
		float h = (r - gNH3.x) / (gNH3.y - gNH3.x);
		float topH = clamp(0.30 + 0.60 * cov + (n - 0.5) * 0.25, 0.05, 1.0);
		float prof = smoothstep(0.0, 0.10, h) * (1.0 - smoothstep(topH - 0.22, topH, h));
		float farK = 1.0 - 0.8 * smoothstep(400.0, 1800.0, dist);			// far away the real map alone draws the pattern
		float shape = 0.30 + cov * 0.55 + ((n - 0.5) * 0.35 + (fine - 0.5) * 0.15) * farK;
		// near the camera: two fine octaves (~24 and ~9 km), gone by a few hundred km where they would only alias
		float nearW = 1.0 - smoothstep(60.0, 450.0, dist);
		if (detail && nearW > 0.01) {
			float f1 = tex3Dlod(sNoise, float4(q.yzx / (gDetailKm * 0.6) + 0.71, Lod(dist, gDetailKm * 0.6))).r;
			float f2 = tex3Dlod(sNoise, float4(q / (gDetailKm * 0.23) + 0.17, Lod(dist, gDetailKm * 0.23))).g;
			shape += ((f1 - 0.5) * 0.30 + (f2 - 0.5) * 0.16) * nearW;
		}	// a continuous deck; the map sets its height and colour, the noise its cells
		d.x = saturate(shape * 2.5) * prof;
		float lodD = Lod(dist, gDetailKm);
		float dw = 0.45 * saturate(1.0 - lodD / 3.0);						// edge erosion fades out where it would only alias
		if (detail && d.x > 0.0 && dw > 0.0) {
			float3 n2 = tex3Dlod(sNoise, float4(q / gDetailKm, lodD)).gba;
			float er = dot(n2, float3(0.625, 0.25, 0.125));
			d.x = saturate((d.x - er * dw * (1.0 - d.x * 0.7)) / (1.0 - er * dw));
		}
		d.x *= gNH3.z;
	}
	// NH4SH: broad, smoother brown deck; gone in the hot spots
	if (r > gNH4.x && r < gNH4.y) {
		float n = tex3Dlod(sNoise, float4(q / (gNoiseKm * 1.8) + 0.37, max(lodN - 0.85, 0))).r;
		float hole = smoothstep(0.08, 0.22, cov);
		d.y = saturate((n * 0.9 - 0.10) * 2.2) * Band(r, gNH4, 0.15, 0.35) * hole * gNH4.z;
	}
	// H2O: deep, sparse convective towers
	if (r > gH2O.x && r < gH2O.y) {
		float n = tex3Dlod(sNoise, float4(q / (gNoiseKm * 1.3) + 0.71, max(lodN - 0.38, 0))).r;
		float hole = smoothstep(0.05, 0.18, cov);
		d.z = saturate((n - 0.48) * 3.0) * Band(r, gH2O, 0.10, 0.45) * hole * gH2O.z;
	}
	d.xyz *= smoothstep(gNearKm * 0.15, gNearKm, dist);
	// haze: thins upwards with a ~30 km scale height, thicker over belts
	if (r > gHaze.x) d.w = gHaze.z * exp(-(r - gHaze.x) / 30.0) * (1.3 - 0.6 * cov);
	return d;
}

// vertical optical depth of everything above radius r (for the light that reaches it)
float TauAbove(float r, float cov)
{
	float t = 0;
	t += gHaze.z * 30.0 * exp(-max(r - gHaze.x, 0) / 30.0) * (1.3 - 0.6 * cov);
	// only the layers above this one: its own layer shades by the local march
	if (r < gNH3.x) t += (0.3 + 2.5 * cov) * gNH3.z;
	if (r < gNH4.x) t += 2.0 * smoothstep(0.08, 0.22, cov) * gNH4.z;
	if (r < gH2O.x) t += 1.5 * smoothstep(0.05, 0.18, cov) * gH2O.z;
	return t;
}

float3 Sky(float3 rd, float3 up, float day, float r)
{
	float e = saturate(dot(rd, up));
	float above = saturate((r - gNH3.y) / (gHaze.y - gNH3.y));					// higher: darker zenith
	float3 zen = lerp(float3(0.36, 0.30, 0.24), float3(0.10, 0.11, 0.16), above);
	return lerp(HAZEC, zen, pow(e, 0.5)) * day;
}

float4 DeckPS(VSOut i, float2 vpos : VPOS) : COLOR
{
	float3 rd = normalize(i.ray);
	float3 ro = gCam;
	float camR = length(ro);
	if (camR < gDeep) return float4(DEEPC * gExpo, 1);
	float top = gHaze.y;
	float2 tt = Sphere(ro, rd, top);
	if (tt.y < 0) return float4(0, 0, 0, 0);									// ray never meets the atmosphere
	float2 td = Sphere(ro, rd, gDeep);

	float t0 = (camR > top) ? tt.x : 0.0;
	float t1;
	int exitKind;																// 0: range, 1: depths, 2: out through the top
	if (td.x > 0) { t1 = td.x; exitKind = 1; } else { t1 = tt.y; exitKind = 2; }
	if (t1 - t0 > gMaxDist) { t1 = t0 + gMaxDist; exitKind = 0; }

	float jit = Jitter(vpos);	// blue-noise per-pixel jitter (IGN made moire rings, white noise coarse grain)
	float cosT = dot(rd, gSun);
	float phaseC = lerp(HG(cosT, 0.25), HG(cosT, -0.15), 0.3) * 12.566;		// clouds: mild forward scattering, no glare sheet
	float phaseH = lerp(HG(cosT, 0.35), HG(cosT, 0.0), 0.5) * 12.566;			// haze: mildly forward
	float T = 1.0;
	float3 col = 0;
	float stepK = 96.0 / max(gSteps, 16);
	float baseStep = gNoiseKm * 0.006 * stepK;
	float t = t0;
	bool prevCloud = false;

	[loop] for (int k = 0; k < VD_MAXSTEPS; k++) {
		if (k >= gSteps || t >= t1 || T < 0.01) break;
		float dt = (prevCloud ? 0.6 : 1.6) * (baseStep + 0.03 * stepK * (t - t0));	// fine in cloud, coarser in clear air
		float3 p = ro + rd * (t + jit * dt);									// jitter over the whole step actually taken
		float r = length(p);
		float3 up = p / r;
		float sunUp = dot(up, gSun);
		float day = saturate((sunUp + 0.06) * 6.0);
		float3 fk = FlowKm(p);
		float dist = t;
		float4 w = tex2Dlod(sWeather, float4(fk.xy / gMapKm, 0, Lod(dist, gMapKm) - 3.0));
		float4 g = tex2Dlod(sGlobal, float4(GlobalUV(p), 0, Lod(dist, 6.2831853 * gR) - 4.0));
		w = lerp(w, g, gGlobalMix);												// belts, zones, the Great Red Spot: the real map
		float4 fdir = tex2Dlod(sFlowDir, float4(GlobalUV(p), 0, Lod(dist, 6.2831853 * gR) - 4.0));
		float ang = 0.5 * atan2(fdir.g * 2.0 - 1.0, fdir.r * 2.0 - 1.0);
		float2 sm = float2(cos(ang), sin(ang)) * fdir.b;
		float3 q = fk;
		float wq = tex3Dlod(sNoise, float4(q / (gNoiseKm * 4.3) + 0.13, Lod(t, gNoiseKm * 4.3))).r;
		q.xy += (wq - 0.5) * gNoiseKm * float2(1.4, 0.7);						// large warp: breaks the rows of equal ridges
		float fine = tex3Dlod(sNoise, float4(q.zxy / (gNoiseKm * 0.37) + 0.53, Lod(dist, gNoiseKm * 0.37))).r;	// non-repeating fine cells (axes swapped, odd scale)
		float4 L = Layers(q, r, w.a, fine, sm, true, dist);
		float cloud = L.x + L.y + L.z;
		prevCloud = cloud > 0.002;
		float sig = cloud * 0.9 + L.w + 0.0008;
		// sunlight that reaches this depth (slant column above)
		float slant = 1.0 / max(sunUp, 0.06);
		float sunT = exp(-TauAbove(r, w.a) * slant * 0.5);
		float3 c = 0;
		if (cloud > 0.002) {
			// local self-shadowing: 3 growing steps towards the sun (same flow cell)
			float od = 0, ls = gNoiseKm * 0.02;
			[loop] for (int j = 0; j < 3; j++) {
				float3 ps = p + gSun * ls;
				float rs = length(ps);
				float3 qs = q + float3(0, 0, rs - r);
				float4 Ls = Layers(qs, rs, w.a, fine, sm, false, dist + ls);
				od += (Ls.x + Ls.y + Ls.z) * 0.9 * ls * 0.7;
				ls *= 3.0;
			}
			float beer = exp(-od);
			float powder = 1.0 - exp(-od * 2.0 - cloud * 1.5);
			float sunL = day * sunT * beer * lerp(1.0, powder, 0.55) * phaseC;
			float3 alb = (L.x * CloudAlbedo(w.rgb, (r - gNH3.x) / (gNH3.y - gNH3.x))
						+ L.y * CloudAlbedo(w.rgb, 0.0) * float3(0.78, 0.58, 0.40)
						+ L.z * float3(0.55, 0.50, 0.45)) / cloud;
			float3 amb = 1.4 * lerp(float3(0.30, 0.21, 0.14), HAZEC, saturate(sunT)) * day * sqrt(sunT + 0.02);
			c = alb * (sunL * 0.95 * float3(1.0, 0.95, 0.86) + amb * 0.60) * (cloud * 0.9 / sig);
			// lightning: only in a few storm cells (~7 % of 1500 km cells), brief flashes inside the water clouds
			if (gLightning > 0.5 && L.z > 0.01) {
				float2 cell = floor(fk.xy / 1500.0);
				float ph = Hash(cell);
				float slot = floor(gRealTime / 0.15 + ph * 97.0);					// 0.15 s slots, a flash in ~1 of 250: one per ~40 s per storm
				float flash = step(0.95, ph) * step(0.996, Hash(cell + float2(slot, slot * 0.37)));
				float2 dc = fk.xy - (cell + 0.5) * 1500.0 - (frac(ph * float2(31.0, 57.0)) - 0.5) * 600.0;
				c += float3(0.75, 0.82, 1.0) * flash * 2.0 * exp(-dot(dc, dc) / 3600.0) * (L.z / cloud);
			}
		}
		// haze and clear air: single scattering of the light that gets there
		c += HAZEC * day * sunT * phaseH * 0.45 * ((L.w + 0.0008) / sig);
		float a = 1.0 - exp(-sig * dt);
		col += T * a * c;
		T *= 1.0 - a;
		t += dt;
	}

	float3 pe = ro + rd * t1;
	float3 upe = normalize(pe);
	float daye = saturate((dot(upe, gSun) + 0.06) * 6.0);
	if (exitKind == 1) { col += T * DEEPC * daye; T = 0; }
	else if (exitKind == 2 && camR < gNH3.y + 30.0) { col += T * Sky(rd, normalize(ro), daye, camR); T = 0; }
	return float4(col * gExpo, 1.0 - T);
}

struct CompIn  { float3 pos : POSITION; float3 uv : TEXCOORD0; };
struct CompOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
CompOut CompVS(CompIn i)
{
	CompOut o;
	o.pos = float4(i.pos.xy, 0.5, 1.0);
	o.uv = float2(i.pos.x * 0.5 + 0.5, 0.5 - i.pos.y * 0.5) + gLowTexel;
	return o;
}
// 3x3 tent (1 2 1): the blue-noise jitter is fine-grained, a narrow kernel removes it without the blur of a 5x5
// (falls back to the 5x5 binomial with the white jitter)
float4 CompPS(CompOut i) : COLOR
{
	float2 o = gLowTexel * 2.0;													// one texel
	float4 c = 0;
	float wsum = 0;
	int R = gBlueOn > 0.5 ? 1 : 2;
	[unroll] for (int y = -2; y <= 2; y++) {
		[unroll] for (int x = -2; x <= 2; x++) {
			if (abs(x) > R || abs(y) > R) continue;
			float w = R == 1 ? (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0)
				: (x == 0 ? 6.0 : (abs(x) == 1 ? 4.0 : 1.0)) * (y == 0 ? 6.0 : (abs(y) == 1 ? 4.0 : 1.0));
			c += tex2D(sLow, i.uv + float2(x, y) * o) * w;
			wsum += w;
		}
	}
	return c / wsum * gFade;
}

// pass 1: the layers into the half-resolution target (no blending)
technique Deck
{
	pass P0
	{
		VertexShader = compile vs_3_0 DeckVS();
		PixelShader = compile ps_3_0 DeckPS();
		ZEnable = false;
		ZWriteEnable = false;
		CullMode = None;
		AlphaBlendEnable = false;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

// pass 2: upsample over the scene (premultiplied)
technique Composite
{
	pass P0
	{
		VertexShader = compile vs_3_0 CompVS();
		PixelShader = compile ps_3_0 CompPS();
		ZEnable = false;
		ZWriteEnable = false;
		CullMode = None;
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = InvSrcAlpha;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

// ============================================================================
// Io: the Prometheus plume (AtmoFX). A thin SO2 frost umbrella: the canopy is the envelope of ballistic
// trajectories z = H - r^2 / 4H, reaching the ground at r = 2H; material sits in a thin shell at the envelope,
// in the column over the vent and in the falling curtain over the deposition ring. Single scattering of sunlight,
// forward-peaked (HG g ~ 0.5); a point is lit only while the sun is above its own horizon, so at dawn the canopy
// lights up before the ground. Planet frame [km].
// ============================================================================
float4 gPlume;		// xyz vent direction (unit, planet frame), w body radius [km]
float4 gPlumeP;		// x canopy height H [km], y peak extinction [1/km], z shell half-width [km], w brightness
float4 gPlumeC;		// xyz frost colour, w column top [km]

// plume material at a point [km, planet frame]; n0 vent direction, en north at the vent
// standardised noise channels (r: perlin-worley mean .231 sd .217; g: worley mean .389 sd .147)
float PzR(float3 q) { return (tex3Dlod(sNoise, float4(q, 0)).r - 0.231) / 0.217; }
float PzG(float3 q) { return (tex3Dlod(sNoise, float4(q, 0)).g - 0.389) / 0.147; }
float3 PRot(float a, float3 q) { float c = cos(a), s = sin(a); return float3(c * q.x - s * q.y, s * q.x + c * q.y, q.z); }

// full = false: the large shape only (for the ground shadow, cheap)
float PlumeDensityEx(float3 p, float3 n0, float3 en, float3 ee, bool full)
{
	float R = gPlume.w, H = gPlumeP.x;
	float rl = length(p);
	float h = rl - R;
	if (h < -1.0) return 0.0;
	float3 up = p / rl;
	float2 g = float2(dot(up - n0, ee), dot(up - n0, en)) * R;				// local east, north [km]
	// large-scale field, wider than the plume (no visible tiling): the dome's asymmetry
	float big = PzR(float3(g / 220.0 + float2(0.41, 0.17), 0.33));
	float big2 = PzG(float3(g / 260.0 + float2(0.07, 0.59), 0.71));
	float2 gc = g - float2(6.0, -4.0);										// dome centre drifted off the vent
	float rr = length(gc);
	float Hd = H * (1.0 + 0.12 * tanh(big));								// canopy height varies around the dome
	if (rr > 2.0 * Hd) return 0.0;
	float zenv = Hd - rr * rr / (4.0 * Hd);
	// fountain from a 6 x 18 km strip along the flow front, spreading ~24 deg, mass flux conserved
	float hp = max(h, 0.0);
	float wx = 3.0 + 0.45 * hp, wy = 9.0 + 0.45 * hp;
	float col = 60.0 * (27.0 / (wx * wy)) * exp(-(g.x * g.x / (wx * wx) + g.y * g.y / (wy * wy)))
	          * smoothstep(-0.5, 2.0, h) * (1.0 - smoothstep(0.8 * H, H, h));
	float shell = gPlumeP.z * (1.0 + 0.3 * tanh(big2));
	float dz = (h - zenv) / shell;
	float rays = 0.55, turb = 0.3;
	if (full) {
		// warped turbulence, octaves rotated, non-commensurate scales; stretched along the trajectories (vertical)
		float3 q = float3(g, h * 0.35);
		float3 qw = q + float3(PzR(q / 150.0 + 0.23), PzG(q / 150.0 + 0.61), 0.0) * 18.0;
		float za = PzR(PRot(0.6, qw) / 55.0 + 0.37), zb = PzG(PRot(1.9, qw) / 17.0 + 0.71), zc = PzG(PRot(-1.1, qw) / 5.3 + 0.19);
		turb = saturate((0.5 * za + 0.3 * zb + 0.2 * zc + 0.671) * 0.369);
		// canopy filaments along the radius (arc-length coordinate); seams on opposite sides, blended away
		float az = atan2(gc.y, gc.x), az2 = atan2(-gc.y, -gc.x);
		float r1 = PzG(float3(rr / 70.0 + 0.11, az * rr / 126.0 + 0.3, 0.45));
		float r2 = PzG(float3(rr / 70.0 + 0.53, az2 * rr / 126.0 + 0.7, 0.85));
		rays = saturate(0.55 + 0.35 * lerp(r1, r2, smoothstep(0.7 * 3.14159, 3.14159, abs(az))));
	}
	float can = 0.45 * exp(-dz * dz) * rays * (0.5 + 0.25 * rr / Hd) + (h < zenv ? 0.02 : 0.0);
	// curtain of falling frost over the deposition ring
	float rcur = 1.86 * Hd;
	float zc2 = Hd - rcur * rcur / (4.0 * Hd);
	float cur = 0.45 * exp(-(rr - rcur) * (rr - rcur) / 25.0) * (1.0 - smoothstep(zc2, zc2 + 4.0, h)) * rays;
	return (col + can + cur) * (0.1 + 1.8 * turb);
}
float PlumeDensity(float3 p, float3 n0, float3 en, float3 ee) { return PlumeDensityEx(p, n0, en, ee, true); }

float4 PlumePS(VSOut i, float2 vpos : VPOS) : COLOR
{
	float3 rd = normalize(i.ray);
	float3 ro = gCam;
	float R = gPlume.w, H = gPlumeP.x, Rring = 2.0 * H;
	float3 n0 = gPlume.xyz;
	float3 en = normalize(float3(0, 1, 0) - n0 * n0.y);						// north at the source
	float3 ee = cross(en, n0);												// east
	float3 C = n0 * (R + 0.4 * H);
	float Rb = 1.15 * Rring;
	float2 tb = Sphere(ro - C, rd, Rb);
	if (tb.y < 0) return 0;
	float t0 = max(tb.x, 0.0), t1 = tb.y;
	float2 tg = Sphere(ro, rd, R);
	bool ground = tg.x > 0 && tg.x < t1;
	if (ground) t1 = tg.x;													// the ground hides what is behind it
	if (t1 <= t0) return 0;

	float cosT = dot(rd, gSun);
	float phase = lerp(HG(cosT, 0.7), HG(cosT, 0.1), 0.45) * 12.566;	// fine dust and SO2 frost: strongly forward-scattering (bright with the sun behind the plume)
	float jit = frac(sin(dot(vpos, float2(12.9898, 78.233))) * 43758.5453);
	const int N = 80;
	float dt = (t1 - t0) / N;
	float T = 1.0;
	float3 col = 0;
	[loop] for (int k = 0; k < N; k++) {
		float3 p = ro + rd * (t0 + (k + jit) * dt);
		float sig = PlumeDensity(p, n0, en, ee) * gPlumeP.y;
		if (sig > 1e-6) {
			float rl = length(p);
			float3 up = p / rl;
			float horizon = -sqrt(saturate(1.0 - (R / rl) * (R / rl)));	// the sun still lights a high point after ground sunset
			float lit = smoothstep(horizon - 0.01, horizon + 0.01, dot(up, gSun));
			float a = 1.0 - exp(-sig * dt);
			col += T * a * gPlumeC.rgb * lit * phase * gPlumeP.w;
			T *= 1.0 - a;
		}
	}
	// shadow of the plume on the ground: sunlight from the hit point through the plume
	float Ts = 1.0;
	if (ground) {
		float3 pg = ro + rd * t1;
		float2 ts = Sphere(pg - C, gSun, Rb);
		if (ts.y > 0 && dot(normalize(pg), gSun) > 0) {
			float s1 = ts.y, ds = s1 / 24.0, od = 0;
			[loop] for (int m = 0; m < 24; m++) od += PlumeDensityEx(pg + gSun * ((m + 0.5) * ds), n0, en, ee, false) * gPlumeP.y * ds;
			Ts = exp(-od * 1.5);											// the frost removes the blue more: a touch darker than grey
		}
	}
	return float4(col * gExpo, 1.0 - T * Ts);
}

technique Plume
{
	pass P0
	{
		VertexShader = compile vs_3_0 DeckVS();
		PixelShader = compile ps_3_0 PlumePS();
		ZEnable = false;
		ZWriteEnable = false;
		CullMode = None;
		AlphaBlendEnable = false;
		StencilEnable = false;
		AlphaTestEnable = false;
	}
}

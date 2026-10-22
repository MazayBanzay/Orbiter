// D3D11Client: real-time radiosity (src\Radiosity.cpp) - Enlighten's scheme as Frostbite runs it (Martin & Einarsson,
// "A Real-Time Radiosity Architecture for Video Games", SIGGRAPH 2010): the light transport between a vessel's surface
// patches is precomputed (Tantra_Design/blender/gi_techpoint.py: form factors, the lamps' visibility, the rooms' probes),
// the light itself is computed here every frame - the lamps as they are switched, the Sun through the openings, the other
// vessels' lamps with their shadow maps - and bounced between the patches (a Jacobi step per pass, the result carried
// from frame to frame: it converges in a few frames, as Enlighten's). The probes' ambient cubes (+x -x +y -y +z -z of
// the vessel's frame) go to a 3D texture the scene's shaders read (lights.hlsli RoomSky).
// Units: the client's (the Sun's illuminance = 1). B: a patch's radiosity (exitance) = albedo x its irradiance.
#include "common.hlsli"

struct Patch { float4 p; float4 n; float4 a; uint4 r; };     // p.w area, n.w room, a.rgb albedo; r: pp ofs/cnt, lv ofs/cnt
StructuredBuffer<Patch> tPatch : register(t0);
StructuredBuffer<uint2> tPP : register(t1);                  // (cluster | tag << 16, asuint(form factor))
StructuredBuffer<uint2> tLV : register(t2);                  // (lamp | tag << 16, asuint(visibility))
StructuredBuffer<LocalLightG> tGILamp : register(t3);        // the vessel's lamps this frame (camera-centred; c.rgb 0: off)
StructuredBuffer<uint4> tProbe : register(t4);               // (entries ofs, cnt, valid, 0)
StructuredBuffer<uint3> tPE : register(t5);                  // (cluster | tag << 16, octahedral direction, asuint(weight))
StructuredBuffer<float4> tEdir : register(t6);
StructuredBuffer<float4> tBin : register(t10);            // the clusters' radiosity (t7 the Sun's shadow, t8 the FX atmosphere's: common.hlsli)
StructuredBuffer<uint2> tClus : register(t11);               // a cluster's patches: first, count (the patches sorted by cluster)
StructuredBuffer<float4> tBp : register(t12);                // the patches' radiosity
RWStructuredBuffer<float4> uC : register(u3);
RWStructuredBuffer<float4> uEdir : register(u0);
RWStructuredBuffer<float4> uBout : register(u1);
RWTexture3D<float4> uProbe : register(u2);

cbuffer GI : register(b10)
{
    float4 gCnt;            // x patches, y lamps, z probes, w the local lights to test (the frame's list)
    float4 gCnt2;           // x clusters
    float4 gPortal[2];      // the portals' open fractions 0..1 (8)
    float4 gO;              // the vessel's origin (camera-centred); w: its scale of the probes' cube (1)
    float4 gX, gY, gZ;      // its axes (global)
    float4 gOpenB[32];      // up to 16 openings (vessel frame): lo.xyz + room, hi.xyz
    float4 gOpenN;          // x: openings
    float4 gGrid[24];       // 8 probe grids: lo.xyz + the texture's x0, cell.xyz + the first probe, dims.xyz + room
    float4 gGridN;          // x: grids, y: the texture's depth per face
};

float3 ToG(float3 v) { return v.x * gX.xyz + v.y * gY.xyz + v.z * gZ.xyz; }       // vessel -> global (a direction)
float3 ToL(float3 v) { return float3(dot(v, gX.xyz), dot(v, gY.xyz), dot(v, gZ.xyz)); }

float PortalOpen(uint i) { return i < 4 ? gPortal[0][i] : gPortal[1][i - 4]; }
// a tag's weight: up to two conditions (portal + 1, open bit) - what lies behind a portal counts while it is open, its
// leaf while it is shut
float TagWeight(uint tag)
{
    float w = 1;
    [unroll] for (uint k = 0; k < 2; k++) {
        uint c = (tag >> (4 * k)) & 15;
        if (c == 0) continue;
        float o = PortalOpen((c >> 1) - 1);
        w *= (c & 1) ? o : 1 - o;
    }
    return w;
}

// the Sun on a patch: inside a room only along rays leaving by one of that room's openings (as lights.hlsli InteriorSun),
// the cascades' shadow for what stands in the way (outside them: lit)
float SunVis(float3 pl, float3 pg, float3 ng, float room)
{
    float s = ShadowAtN(pg, ng);
    if (room < 0.5) return s;
    float3 d = ToL(gSunDir.xyz);
    float through = 0;
    [loop] for (int i = 0; i < (int)gOpenN.x; i++) {
        float4 lo = gOpenB[i * 2], hi = gOpenB[i * 2 + 1];
        if (lo.w > 0.5 && abs(lo.w - room) > 0.5) continue;
        float t0 = 0, t1 = 1e5;
        [unroll] for (int k = 0; k < 3; k++) {
            if (abs(d[k]) < 1e-6) { if (pl[k] < lo[k] || pl[k] > hi[k]) t1 = -1; continue; }
            float ta = (lo[k] - pl[k]) / d[k], tb = (hi[k] - pl[k]) / d[k];
            t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
        }
        if (t1 >= t0) { through = 1; break; }
    }
    return s * through;
}

// a lamp's light on the patch, its shadow left out (the precomputed visibility takes its place)
float3 LampE(LocalLightG L, float3 pg, float3 ng)
{
    if (dot(L.c.rgb, 1) <= 0) return 0;
    if (LightIsRect(L)) {
        float3 v = L.p.xyz - pg;
        float r2 = L.p.w * L.p.w, d2 = dot(v, v);
        if (d2 >= r2) return 0;
        float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
        return L.c.rgb * (max(dot(ng, RectFormVector(L, pg)), 0) / 3.14159265 * w * RectSide(L, pg));
    }
    float3 v = L.p.xyz - pg;
    float d2 = dot(v, v), r2 = L.p.w * L.p.w;
    if (d2 >= r2) return 0;
    float3 l = v * rsqrt(max(d2, 1e-12));
    float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
    float3 E = L.c.rgb * w / max(d2, L.a.y * L.a.y) * saturate(dot(ng, l));
    if (LightIsSpot(L)) {
        float cd = dot(-l, L.d.xyz);
        if (cd <= L.d.w) return 0;
        float e = smoothstep(L.d.w, L.a.x, cd); E *= e * e;
    }
    return E;
}

// is a camera-centred point inside the room 'room' of the vessel (its probe grid's box, 0.3 m to spare)
bool InRoom(float3 pg, float room)
{
    float3 pl = ToL(pg - gO.xyz);
    [loop] for (int g = 0; g < (int)gGridN.x; g++) {
        if (abs(gGrid[g * 3 + 2].w - room) > 0.5) continue;
        float3 lo = gGrid[g * 3].xyz, hi = lo + gGrid[g * 3 + 1].xyz * gGrid[g * 3 + 2].xyz;
        if (all(pl > lo - 0.3) && all(pl < hi + 0.3)) return true;
    }
    return false;
}

[numthreads(64, 1, 1)]
void CSDirect(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= (uint)gCnt.x) return;
    Patch P = tPatch[i];
    float3 pg = gO.xyz + ToG(P.p.xyz), ng = ToG(P.n.xyz);
    float3 pgo = pg + ng * 0.02;
    float3 E = 0;
    // the vessel's own lamps x their precomputed visibility
    [loop] for (uint k = 0; k < P.r.w; k++) {
        uint2 e = tLV[P.r.z + k];
        float tw = TagWeight(e.x >> 16);
        if (tw <= 0) continue;
        E += LampE(tGILamp[e.x & 0xFFFF], pgo, ng) * (asfloat(e.y) * tw);
    }
    // the Sun
    float sl = saturate(dot(ng, gSunDir.xyz));
    if (sl > 0) E += SunLight(pg) * sl * SunVis(P.p.xyz + P.n.xyz * 0.02, pgo, ng, P.n.w);
    // the other vessels' lamps (headlights, the suits' lamps) with their shadow maps; inside a room only those in it (a lamp
    // without a map would shine through the walls)
    uint nl = min((uint)gCnt.w, 64);
    [loop] for (uint j = 0; j < nl; j++) {
        LocalLightG L = tLights[j];
        uint fl = (uint)L.c.w;
        if ((fl & 32) != 0 || (fl & 4) != 0) continue;      // the vessel's own (above), a cockpit's
        float3 lv = L.p.xyz - pgo;
        if (dot(lv, lv) >= L.p.w * L.p.w) continue;          // out of its reach (before the room test)
        if (P.n.w > 0.5 && !InRoom(L.p.xyz, P.n.w)) continue;
        float3 Ld;
        if (LightIsRect(L)) E += RectIrradiance(L, pgo, ng);
        else { float3 e = LocalLightE(L, pgo, ng, Ld); E += e * saturate(dot(ng, Ld)); }
    }
    if (NotFinite(E)) E = 0;
    uEdir[i] = float4(max(E, 0), 0);
}

// one bounce (Jacobi): B = albedo x (Edir + sum F C_prev), C the clusters' radiosity
[numthreads(64, 1, 1)]
void CSBounce(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= (uint)gCnt.x) return;
    Patch P = tPatch[i];
    float3 E = tEdir[i].rgb;
    [loop] for (uint k = 0; k < P.r.y; k++) {
        uint2 e = tPP[P.r.x + k];
        uint tag = e.x >> 16;
        float w = asfloat(e.y) * (tag ? TagWeight(tag) : 1);
        E += tBin[e.x & 0xFFFF].rgb * w;
    }
    float3 B = P.a.rgb * E;
    if (NotFinite(B)) B = 0;
    uBout[i] = float4(B, 0);
}

// a cluster's radiosity: its patches' area-weighted mean
[numthreads(64, 1, 1)]
void CSCluster(uint3 id : SV_DispatchThreadID)
{
    uint c = id.x;
    if (c >= (uint)gCnt2.x) return;
    uint2 r = tClus[c];
    float3 s = 0; float a = 0;
    [loop] for (uint k = 0; k < r.y; k++) { float w = tPatch[r.x + k].p.w; s += tBp[r.x + k].rgb * w; a += w; }
    uC[c] = float4(a > 0 ? s / a : 0, 0);
}

float3 OctDir(uint q)
{
    float2 f = float2(q & 0xFFFF, q >> 16) / 65535.0 * 2 - 1;
    float3 n = float3(f.x, 1 - abs(f.x) - abs(f.y), f.y);
    if (n.y < 0) n.xz = (1 - abs(n.zx)) * float2(n.x >= 0 ? 1 : -1, n.z >= 0 ? 1 : -1);
    return normalize(n);
}

// a probe's ambient cube: what each of its six faces receives from the clusters it sees (E_f = sum w C max(n_f . d, 0))
[numthreads(64, 1, 1)]
void CSProbe(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= (uint)gCnt.z) return;
    uint4 pr = tProbe[i];
    float3 f[6] = { (float3)0, (float3)0, (float3)0, (float3)0, (float3)0, (float3)0 };
    [loop] for (uint k = 0; k < pr.y; k++) {
        uint3 e = tPE[pr.x + k];
        uint tag = e.x >> 16;
        float w = asfloat(e.z) * (tag ? TagWeight(tag) : 1);
        if (w <= 0) continue;
        float3 B = tBin[e.x & 0xFFFF].rgb * w, d = OctDir(e.y);
        f[0] += B * max(d.x, 0); f[1] += B * max(-d.x, 0);
        f[2] += B * max(d.y, 0); f[3] += B * max(-d.y, 0);
        f[4] += B * max(d.z, 0); f[5] += B * max(-d.z, 0);
    }
    // its place in the texture: its grid, x along the grids' row, each face a slab of the depth
    int g = 0;
    [loop] for (int k2 = 1; k2 < (int)gGridN.x; k2++) if (i >= (uint)gGrid[k2 * 3 + 1].w) g = k2;
    uint li = i - (uint)gGrid[g * 3 + 1].w;
    uint3 dm = (uint3)gGrid[g * 3 + 2].xyz;
    uint3 c = uint3(li % dm.x, (li / dm.x) % dm.y, li / (dm.x * dm.y));
    c.x += (uint)gGrid[g * 3].w;
    uint D = (uint)gGridN.y;
    [unroll] for (uint q = 0; q < 6; q++) {
        float3 v = f[q];
        if (NotFinite(v)) v = 0;
        uProbe[uint3(c.x, c.y, c.z + q * D)] = float4(v, 1);
    }
}

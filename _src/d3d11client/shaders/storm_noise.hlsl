// D3D11Client: Earth's storm - the tiling 3D noises, made once on the GPU (Storm.cpp). Kept apart from storm.hlsl: an
// edit of the clouds does not recompile these (the shader cache is keyed on the source).
//   CSShape (128^3): r Perlin-Worley, g b a Worley octaves; CSDetail (32^3): Worley octaves.

uint Hash3(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}
float3 Hash33(uint3 c, uint seed)
{
    uint h = Hash3(c + seed * 7919u);
    return float3(h & 1023u, (h >> 10) & 1023u, (h >> 20) & 1023u) / 1023.0;
}
float Remap(float x, float a, float b, float c, float d) { return c + (x - a) * (d - c) / max(b - a, 1e-5); }
// tiling Worley (F1, inverted: 1 at the cells' seeds), period 'cells' over [0,1)^3
float WorleyT(float3 p, uint cells, uint seed)
{
    float3 f = p * cells;
    float3 i = floor(f);
    float best = 1e9;
    [loop] for (uint k = 0; k < 27; k++) {
        float3 o = float3(uint3(k % 3u, (k / 3u) % 3u, k / 9u)) - 1;
        float3 c = i + o;
        uint3 w = (uint3)(c + cells) % cells;          // c >= -1: non-negative before the modulus
        float3 d = c + Hash33(w, seed) - f;
        best = min(best, dot(d, d));
    }
    return 1 - saturate(sqrt(best));
}
// tiling gradient noise, period 'per'
float GradT(float3 c, uint per, float3 f)
{
    uint3 w = (uint3)(c + per) % per;
    float3 g = Hash33(w, 3u) * 2 - 1;
    return dot(normalize(g + 1e-4), f);
}
float PerlinT(float3 p, uint per)
{
    float3 x = p * per;
    float3 i = floor(x);
    float3 f = x - i;
    float3 u = f * f * f * (f * (f * 6 - 15) + 10);
    float n[8];
    [unroll] for (int k = 0; k < 8; k++) { float3 o = float3(k & 1, (k >> 1) & 1, k >> 2); n[k] = GradT(i + o, per, f - o); }
    return lerp(lerp(lerp(n[0], n[1], u.x), lerp(n[2], n[3], u.x), u.y), lerp(lerp(n[4], n[5], u.x), lerp(n[6], n[7], u.x), u.y), u.z);
}

RWTexture3D<unorm float4> uNoise : register(u0);
[numthreads(4, 4, 4)]
void CSShape(uint3 id : SV_DispatchThreadID)
{
    float3 p = (id + 0.5) / 128.0;
    float pf = 0, amp = 1, nrm = 0;
    [loop] for (uint o = 0; o < 4; o++) { pf += amp * PerlinT(p, 4u << o); nrm += amp; amp *= 0.5; }
    pf = saturate(pf / nrm * 0.9 + 0.5);
    float w1 = WorleyT(p, 4, 11u), w2 = WorleyT(p, 8, 23u), w3 = WorleyT(p, 16, 37u), w4 = WorleyT(p, 32, 41u);
    float wf = w1 * 0.625 + w2 * 0.25 + w3 * 0.125;
    // Perlin-Worley: the Perlin's billows remapped by the Worley cells - round heaps with dark seams between them
    float pw = saturate(Remap(pf, wf - 1, 1, 0, 1));
    uNoise[id] = float4(pw, w2, w3, w4);
}
[numthreads(4, 4, 4)]
void CSDetail(uint3 id : SV_DispatchThreadID)
{
    float3 p = (id + 0.5) / 32.0;
    uNoise[id] = float4(WorleyT(p, 2, 51u), WorleyT(p, 4, 53u), WorleyT(p, 8, 57u), 1);
}

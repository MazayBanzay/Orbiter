// D3D11Client: ambient occlusion from the scene's depth (Ssao.cpp), after the opaque terrain and vessels, before the
// liquid and the air. CSAO: at half resolution, the view-space point and its normal from the depth, 6 directions x 3
// steps within a world radius (horizon-style: how much of the hemisphere over the normal the near geometry hides).
// PSApply: back to full resolution with a depth-aware 3 x 3 filter, multiplied into the scene (the ground under a
// vessel, a foot in the soil, the folds of the relief darken; the air and the liquid, drawn after, are not touched).
// Pixels left unoccluded are not written (the sky, the far ground): at MSAA 8x the writes are the cost.
cbuffer Ssao : register(b0)
{
    float4 sP;     // x, y: tan half-fov x, y; z: near plane; w: radius [m]
    float4 sS;     // x, y: the scene's size; z, w: the AO texture's size
    float4 sK;     // x: strength; y: the largest radius on screen [pixels]; z, w: -
};
Texture2D<float> tDepth : register(t0);    // reverse Z, infinite far: depth = near / view z (0: nothing drawn)
Texture2D<float> tAO : register(t1);
RWTexture2D<float> uAO : register(u0);

float3 ViewPos(int2 p)
{
    p = clamp(p, int2(0, 0), int2(sS.xy) - 1);
    float d = tDepth.Load(int3(p, 0));
    if (d <= 0) return float3(0, 0, -1);
    float z = sP.z / d;
    float2 ndc = (p + 0.5) / sS.xy * float2(2, -2) + float2(-1, 1);
    return float3(ndc.x * sP.x * z, ndc.y * sP.y * z, z);
}

[numthreads(8, 8, 1)]
void CSAO(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)sS.z || id.y >= (uint)sS.w) return;
    int2 p = int2(id.xy) * 2;
    float3 P = ViewPos(p);
    if (P.z <= 0) { uAO[id.xy] = 1; return; }
    // the normal from the nearer neighbour on each axis (no smearing over silhouettes)
    float3 px1 = ViewPos(p + int2(2, 0)), px0 = ViewPos(p - int2(2, 0));
    float3 py1 = ViewPos(p + int2(0, 2)), py0 = ViewPos(p - int2(0, 2));
    float3 dx = abs(px1.z - P.z) < abs(P.z - px0.z) ? px1 - P : P - px0;
    float3 dy = abs(py1.z - P.z) < abs(P.z - py0.z) ? py1 - P : P - py0;
    float3 n = normalize(cross(dx, dy));
    if (dot(n, -P) < 0) n = -n;
    float rpx = sP.w * (sS.y * 0.5) / (sP.y * P.z);     // the radius on screen
    if (rpx < 1.5) { uAO[id.xy] = 1; return; }          // far away: the radius is under a pixel
    float fadeFar = saturate((rpx - 1.5) / 4);
    rpx = min(rpx, sK.y);
    // interleaved gradient noise: each pixel its own rotation (the 3 x 3 filter evens it out)
    float rot = frac(52.9829189 * frac(dot(float2(id.xy), float2(0.06711056, 0.00583715)))) * 6.2831853;
    float jit = frac(rot * 7.13);
    const int ND = 6, NS = 3;
    float r2 = sP.w * sP.w, occ = 0;
    [unroll] for (int k = 0; k < ND; k++) {
        float a = rot + k * (6.2831853 / ND);
        float2 dv = float2(cos(a), sin(a));
        [unroll] for (int s = 0; s < NS; s++) {
            float t = (s + jit) / NS;
            float2 q = p + dv * rpx * max(t * t, 2.0 / rpx);   // more samples near the point
            float3 S = ViewPos(int2(q));
            if (S.z <= 0) continue;
            float3 v = S - P;
            float vv = dot(v, v);
            occ += saturate(dot(n, v) * rsqrt(vv + 1e-6) - 0.1) * saturate(1 - vv / r2);
        }
    }
    float ao = saturate(1 - sK.x * occ / (ND * NS) * 2.5);
    uAO[id.xy] = lerp(1, ao, fadeFar);
}

float4 PSApply(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy);
    float d0 = tDepth.Load(int3(p, 0));
    if (d0 <= 0) discard;
    float z0 = sP.z / d0;
    int2 c = p / 2;
    float sum = 0, ws = 0;
    [unroll] for (int j = -1; j <= 1; j++) {
        [unroll] for (int i = -1; i <= 1; i++) {
            int2 q = clamp(c + int2(i, j), int2(0, 0), int2(sS.zw) - 1);
            float dq = tDepth.Load(int3(min(q * 2, int2(sS.xy) - 1), 0));
            float zq = dq > 0 ? sP.z / dq : 1e9;
            float w = (i == 0 && j == 0 ? 2.0 : 1.0) / (1e-3 + abs(zq - z0) / z0 * 40);
            sum += tAO.Load(int3(q, 0)) * w; ws += w;
        }
    }
    float ao = ws > 0 ? sum / ws : 1;
    if (ao > 0.995) discard;
    return float4(ao, ao, ao, 1);
}

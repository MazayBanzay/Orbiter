// D3D11Client: anti-aliasing passes
//   ResolveDepthPS - the multisampled scene depth to a single-sampled R32F (sample 0: the nearest of a reverse-Z pixel
//                    would be max(), but sample 0 keeps the depth consistent with the resolved colour's centre)
//   TAAPS          - temporal: this frame (jittered) + the history reprojected through the depth, clamped to the
//                    3x3 neighbourhood in tone-mapped space
//   FXAAPS         - post: luma edge detection, a directional blend along the edge (FXAA-class)
Texture2DMS<float> tDepthMS : register(t0);
Texture2D tCur : register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D tHist : register(t2);
Texture2D<float2> tMV : register(t3);
SamplerState sClamp : register(s1);

cbuffer AA : register(b1)
{
    float4x4 gInvVP;     // clip -> camera-centred global (this frame, jittered)
    float4x4 gPrevVP;    // camera-centred global of the last frame -> clip (no jitter)
    float4 gCamDelta;    // camera now minus camera then (global)
    float4 gP;           // x: history weight, y: history valid, zw: 1/size
    float4 gScene;       // the scene's size (render scale) in pixels; zw: this frame's jitter in scene pixels
};

struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float ResolveDepthPS(VSOut i) : SV_Target
{
    return tDepthMS.Load(int2(i.pos.xy), 0);
}

float3 Tm(float3 c) { return c / (1 + max(c.r, max(c.g, c.b))); }
float3 InvTm(float3 c) { return c / max(1 - max(c.r, max(c.g, c.b)), 1e-4); }

// TAA with upsampling (TAAU): the scene may be rendered smaller (render scale); each output pixel gathers the 3x3
// scene samples around it, weighted by their distance to the pixel centre (each sample sits where the jitter put it),
// and blends with the history reprojected through the depth or the vessel's motion vector, clamped to those samples
float4 TAAPS(VSOut i) : SV_Target
{
    float2 lp = i.uv * gScene.xy;                     // the output pixel's centre in scene pixels
    int2 q0 = int2(floor(lp));
    float3 sum = 0, mn = 1e9, mx = -1e9; float sw = 0, wmax = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        int2 q = clamp(q0 + int2(x, y), int2(0, 0), int2(gScene.xy) - 1);
        float3 s = tCur.Load(int3(q, 0)).rgb;
        float2 d = (q + 0.5 - gScene.zw) - lp;         // where this sample really is
        float w = exp(-2.29 * dot(d, d));
        sum += s * w; sw += w; wmax = max(wmax, w);
        float3 t = Tm(s); mn = min(mn, t); mx = max(mx, t);
    }
    float3 c = sum / max(sw, 1e-5);
    float d = tDepth.Load(int3(q0, 0));
    float2 ndc = i.uv * float2(2, -2) + float2(-1, 1);
    float4 w = mul(float4(ndc, max(d, 1e-9), 1), gInvVP);
    float3 wp = w.xyz / w.w;
    float4 pc = mul(float4(wp + (d > 0 ? gCamDelta.xyz : 0), 1), gPrevVP);    // the sky: direction only, no parallax
    float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
    float2 mv = tMV.Load(int3(q0, 0));
    if (mv.x < 100) puv = i.uv - mv;                  // a vessel: its own motion
    if (gP.y < 0.5 || any(puv < 0) || any(puv > 1)) return float4(c, 1);
    float3 h = clamp(Tm(tHist.SampleLevel(sClamp, puv, 0).rgb), mn, mx);
    // the nearer a sample fell to this pixel this frame, the more it counts
    float a = lerp(0.03, 1 - gP.x, saturate(wmax));
    return float4(InvTm(lerp(h, Tm(c), a)), 1);
}

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 FXAAPS(VSOut i) : SV_Target
{
    float2 t = gP.zw, uv = i.uv;
    float3 cM = tCur.SampleLevel(sClamp, uv, 0).rgb;
    float lM = Luma(cM);
    float lNW = Luma(tCur.SampleLevel(sClamp, uv + float2(-0.5, -0.5) * t, 0).rgb);
    float lNE = Luma(tCur.SampleLevel(sClamp, uv + float2(0.5, -0.5) * t, 0).rgb);
    float lSW = Luma(tCur.SampleLevel(sClamp, uv + float2(-0.5, 0.5) * t, 0).rgb);
    float lSE = Luma(tCur.SampleLevel(sClamp, uv + float2(0.5, 0.5) * t, 0).rgb);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) return float4(cM, 1);      // no edge here
    float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float red = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128);
    float rcpMin = 1 / (min(abs(dir.x), abs(dir.y)) + red);
    dir = clamp(dir * rcpMin, -8, 8) * t;
    float3 a = 0.5 * (tCur.SampleLevel(sClamp, uv + dir * (1.0 / 3 - 0.5), 0).rgb + tCur.SampleLevel(sClamp, uv + dir * (2.0 / 3 - 0.5), 0).rgb);
    float3 b = a * 0.5 + 0.25 * (tCur.SampleLevel(sClamp, uv - dir * 0.5, 0).rgb + tCur.SampleLevel(sClamp, uv + dir * 0.5, 0).rgb);
    float lb = Luma(b);
    return float4((lb < lMin || lb > lMax) ? a : b, 1);
}

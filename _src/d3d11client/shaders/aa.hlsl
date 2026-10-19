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
    float4 gPM0, gPM1, gPM2;   // a point fixed to the nearest body, camera-centred now -> camera-centred last frame:
                               // rows of the body's rotation since then (xyz) and the offset (w) - the ground turns with it
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

float3 ToYCoCg(float3 c) { return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25))); }
float3 FromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// the history through a Catmull-Rom filter (five bilinear taps, the corners left out): sharp, no bilinear blur that
// would accumulate frame after frame
float3 HistoryCR(float2 uv)
{
    float2 sz = 1 / gP.zw, sp = uv * sz, tc = floor(sp - 0.5) + 0.5, f = sp - tc;
    float2 w0 = f * (-0.5 + f * (1 - 0.5 * f)), w1 = 1 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2, o12 = w2 / w12;
    float2 t0 = (tc - 1) * gP.zw, t3 = (tc + 2) * gP.zw, t12 = (tc + o12) * gP.zw;
    float3 r = tHist.SampleLevel(sClamp, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y)
             + tHist.SampleLevel(sClamp, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y)
             + tHist.SampleLevel(sClamp, float2(t12.x, t12.y), 0).rgb * (w12.x * w12.y)
             + tHist.SampleLevel(sClamp, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y)
             + tHist.SampleLevel(sClamp, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
    float ws = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / ws, 0);
}

// the history pulled towards the box of this frame's neighbourhood (along the line to its centre: no hue shift)
float3 ClipToBox(float3 h, float3 lo, float3 hi)
{
    float3 c = 0.5 * (hi + lo), e = 0.5 * (hi - lo) + 1e-5;
    float3 v = h - c, a = abs(v / e);
    float m = max(a.x, max(a.y, a.z));
    return m > 1 ? c + v / m : h;
}

// TAA with upsampling (TAAU): the scene may be rendered smaller (render scale); each output pixel gathers the 3x3
// scene samples around it, weighted by their distance to the pixel centre (each sample sits where the jitter put it).
// The history comes from where this point was a frame ago: a vessel by its motion vector, the ground and everything
// fixed to the nearest body through the depth with the camera's motion AND the body's rotation (Titan turns ~6 m/s at
// the site: without it the ground slid under the history and smeared), the sky by direction. It is read through a
// Catmull-Rom filter and clipped to the neighbourhood's variance box in YCoCg (tone-mapped): what was not there a
// frame ago (a moving leg, a splash, a disocclusion) cannot leave a trail
float4 TAAPS(VSOut i) : SV_Target
{
    float2 lp = i.uv * gScene.xy;                     // the output pixel's centre in scene pixels
    int2 q0 = int2(floor(lp));
    // this frame at the output pixel: the 3x3 samples through a Lanczos-2 kernel at their true (jittered) places - its
    // negative lobes keep the detail a Gaussian would blur - clamped to the samples' range (no ringing at edges)
    float3 sum = 0, m1 = 0, m2 = 0, nmin = 1e9, nmax = 0; float sw = 0, wmax = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        int2 q = clamp(q0 + int2(x, y), int2(0, 0), int2(gScene.xy) - 1);
        float3 s = max(tCur.Load(int3(q, 0)).rgb, 0);
        float2 d = (q + 0.5 - gScene.zw) - lp;         // where this sample really is
        float r = length(d) * (1 / gP.z) / gScene.x;     // in output pixels (a scene pixel is W / RW of them)
        float w = r < 1e-3 ? 1 : (r >= 2 ? 0 : sin(3.14159 * r) * sin(1.5708 * r) / (2.4674 * r * r));
        sum += s * w; sw += w; wmax = max(wmax, exp(-2.29 * dot(d, d)));
        nmin = min(nmin, s); nmax = max(nmax, s);
        float3 t = ToYCoCg(Tm(s)); m1 += t; m2 += t * t;
    }
    float3 c = clamp(sum / max(sw, 1e-5), nmin, nmax);
    float3 mu = m1 / 9, sg = sqrt(max(m2 / 9 - mu * mu, 0));
    // the nearest surface of the 3x3 (reverse Z: the largest depth) gives the motion: an edge moves with the object in
    // front, the background behind it does not drag the object's history along (no halo round a moving silhouette)
    int2 qn = q0; float d = tDepth.Load(int3(q0, 0));
    [unroll] for (int yy = -1; yy <= 1; yy++) [unroll] for (int xx = -1; xx <= 1; xx++) {
        int2 q = clamp(q0 + int2(xx, yy), int2(0, 0), int2(gScene.xy) - 1);
        float dq = tDepth.Load(int3(q, 0));
        if (dq > d) { d = dq; qn = q; }
    }
    float2 ndc = i.uv * float2(2, -2) + float2(-1, 1);
    float4 w = mul(float4(ndc, max(d, 1e-9), 1), gInvVP);
    float3 wp = w.xyz / w.w;
    float3 pw = d > 0 ? float3(dot(gPM0.xyz, wp), dot(gPM1.xyz, wp), dot(gPM2.xyz, wp)) + float3(gPM0.w, gPM1.w, gPM2.w) : wp;
    float4 pc = mul(float4(pw, 1), gPrevVP);
    float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
    float2 mv = tMV.Load(int3(qn, 0));
    if (mv.x < 100) puv = i.uv - mv;                  // a vessel: its own motion
    if (gP.y < 0.5 || any(puv < 0) || any(puv > 1) || any(isnan(puv))) return float4(c, 1);
    float3 h = ToYCoCg(Tm(HistoryCR(puv)));
    h = ClipToBox(h, mu - 1.25 * sg, mu + 1.25 * sg);
    // the nearer a sample fell to this pixel this frame, the more it counts; in motion the current frame counts more
    float speed = length((i.uv - puv) / gP.zw);        // output pixels moved since the last frame
    float a = lerp(0.08, 1 - gP.x, saturate(wmax));      // at least 8 % of this frame: the detail is not averaged away
    a = max(a, saturate(speed * 0.02));
    float3 o = InvTm(FromYCoCg(lerp(h, ToYCoCg(Tm(c)), a)));
    return float4(max(o, 0), 1);
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

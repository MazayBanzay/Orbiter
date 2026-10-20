// D3D11Client: the local lights seen in the air and as themselves (src\Lights.cpp), added after the air and the clouds.
// Beams: a headlight's light scattered in the air towards the eye - single scattering along the view ray through the
// spot's cone (or the point light's sphere), the light's own shadow (its spot map) cut into it: the bright halo round the
// lamp in a hazy air, the cone over the ground, the shafts behind what stands in it. The medium is the FX atmosphere's
// (b4: Rayleigh and aerosol at the camera's height, the aerosol's Henyey-Greenstein phase), or the client's simple haze.
// The integral of 1/r^2 along the ray is sampled equiangularly (Kulla & Fajardo 2012, "Importance sampling techniques
// for path tracing in participating media"): the samples crowd where the ray passes the lamp; 12 a light, jittered.
// Sprites: a spot's lens (its luminance: the intensity towards the eye over the lens' area) and the vessels' beacons
// (BEACONLIGHTSPEC), additive quads hidden by the scene's depth at their centre (3 x 3 depth loads in the VS).
#include "common.hlsli"

Texture2D<float> tDepth : register(t0);

struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float Ign(float2 px, float k) { px += k * 5.588238; return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))); }

// the part [ta, tb] of the ray rd (from the camera) inside the spot's forward cone (apex p, axis d, cos of the half angle c):
// the forward nappe is convex, so the line meets it in one interval - between the quadric's roots that bound a part inside
bool InCone(float3 x, float3 d, float c) { return dot(x, d) > length(x) * c; }
bool ConeSpan(float3 rd, float3 p, float3 d, float c, float tmax, out float ta, out float tb)
{
    float3 co = -p;
    float dv = dot(rd, d), cv = dot(co, d), c2 = c * c;
    float A = dv * dv - c2, B = 2 * (dv * cv - dot(rd, co) * c2), C = cv * cv - dot(co, co) * c2;
    float r0 = tmax, r1 = tmax;
    float disc = B * B - 4 * A * C;
    if (abs(A) > 1e-7) {
        if (disc >= 0) { float s = sqrt(disc); r0 = (-B - s) / (2 * A); r1 = (-B + s) / (2 * A); if (r0 > r1) { float x = r0; r0 = r1; r1 = x; } }
    } else if (abs(B) > 1e-9) r0 = r1 = -C / B;
    r0 = clamp(r0, 0, tmax); r1 = clamp(r1, r0, tmax);
    // three segments [0, r0], [r0, r1], [r1, tmax]: the inside ones
    bool i0 = r0 > 0 && InCone(co + rd * (0.5 * r0), d, c);
    bool i1 = r1 > r0 && InCone(co + rd * (0.5 * (r0 + r1)), d, c);
    bool i2 = tmax > r1 && InCone(co + rd * (0.5 * (r1 + tmax)), d, c);
    ta = i0 ? 0 : i1 ? r0 : r1;
    tb = i2 ? tmax : i1 ? r1 : r0;
    return (i0 || i1 || i2) && tb > ta;
}

float HG(float mu, float g) { float g2 = g * g; return (1 - g2) / (12.5663706 * pow(max(1 + g2 - 2 * g * mu, 1e-4), 1.5)); }

cbuffer Beam : register(b10) { float4 gBeam; };     // x: the light's index (one draw per light, scissored to its cone)

float4 PSBeams(VSOut i) : SV_Target
{
    int2 px = (int2)i.pos.xy;
    float z = tDepth.Load(int3(px, 0));
    float4 c = mul(float4(i.uv * float2(2, -2) + float2(-1, 1), max(z, 1e-7), 1), gInvVP);
    float3 wp = c.xyz / c.w;
    float tmax = z > 0 ? length(wp) : 1e5;
    float3 rd = normalize(wp);
    // the air at the camera: scattering by the gas and the aerosol, extinction, the aerosol's phase
    float3 sR, sM, ext; float g;
    if (FxActive()) {
        float h = length(mCentre.xyz) - mCentre.w;
        sR = mRay.rgb * FxRayDensity(h); sM = mMieSca.rgb * FxMieDensity(h); ext = FxExtinction(h);
        g = dot(mMieG.rgb, 1.0 / 3);
    } else {
        ext = gAmb.w * gHazeRGB.rgb * exp(-max(gUp.w, 0) / max(gSun.w, 1.0));
        sR = 0; sM = ext; g = 0.7;
    }
    float3 sum = 0;
    const int N = 10;
    float jit = Ign(i.pos.xy, gSpotK[0].w);
    {
        LocalLightG L = tLights[(uint)gBeam.x];
        float3 p = L.p.xyz;
        float R = L.p.w;
        // the ray in the light's sphere
        float Dl = dot(p, rd);
        float3 perp = p - rd * Dl;
        float D2 = dot(perp, perp);
        if (D2 >= R * R) discard;
        float hw = sqrt(R * R - D2);
        float ta = max(Dl - hw, 0), tb = min(Dl + hw, tmax);
        if (LightIsSpot(L)) {
            float ca, cb;
            if (!ConeSpan(rd, p, L.d.xyz, L.d.w, tmax, ca, cb)) discard;
            ta = max(ta, ca); tb = min(tb, cb);
        }
        if (tb <= ta) discard;
        float D = max(sqrt(D2), L.a.y);
        float th0 = atan((ta - Dl) / D), th1 = atan((tb - Dl) / D);
        float3 acc = 0;
        [loop] for (int k = 0; k < N; k++) {
            float th = lerp(th0, th1, (k + jit) / N);
            float t = Dl + D * tan(th);
            float3 x = rd * t;
            float3 v = x - p;
            float r2 = dot(v, v), r = sqrt(r2);
            float3 wi = v / max(r, 1e-6);
            float w = saturate(1 - (r2 / (R * R)) * (r2 / (R * R))); w *= w;
            float cone = 1;
            if (LightIsSpot(L)) { float cd = dot(wi, L.d.xyz); cone = smoothstep(L.d.w, L.a.x, cd); cone *= cone; }
            float sh = 1;
            if (L.a.z >= 0) {
                int st = (int)L.a.z;
                float4 q = mul(float4(x, 1), gSpotVP[st]);
                if (q.w > 1e-4) {
                    float3 cq = q.xyz / q.w;
                    float2 uv = cq.xy * float2(0.5, -0.5) + 0.5;
                    if (all(uv > 0.002) && all(uv < 0.998) && cq.z <= 1)
                        sh = tSpotShadow.SampleCmpLevelZero(sShadow, (float2(st & 1, st >> 1) + uv) * 0.5, cq.z);
                }
            }
            float mu = dot(wi, -rd);
            float3 ph = sR * (0.0596831 * (1 + mu * mu)) + sM * HG(mu, g);
            acc += ph * (w * cone * sh * smoothstep(0.05, 0.25, r)) * exp(-ext * (t + r)) * (r2 / max(r2, L.a.y * L.a.y));   // (inside the lamp: no air)
        }
        // the estimator: intensity x (theta1 - theta0) / D x the mean of the rest; x pi: the client's radiance
        sum += L.c.rgb * acc * ((th1 - th0) / (D * N)) * 3.14159265;
    }
    if (NotFinite(sum)) sum = 0;
    return float4(max(sum, 0), 1);
}

// ---- sprites: the lenses and the beacons
struct Sprite
{
    float4 p;   // centre (camera-centred) [m]; w: radius [m]
    float4 c;   // lens: the intensity at 1 m (as the light's c); beacon: its colour; w: 0 lens, 1 compact, 2 diffuse, 3 star
    float4 d;   // lens: the spot's axis, w cos of the penumbra's half angle; beacon: x the blink (0..1)
    float4 e;   // lens: x cos of the umbra's half angle
};
StructuredBuffer<Sprite> tSprites : register(t1);
cbuffer SpriteCB : register(b1)
{
    float4x4 gSVP;      // camera-centred global -> clip (this frame's, jittered)
    float4 gSView;      // x: render width, y: height, z: proj y scale, w: near plane
    float4 gSRight;     // the camera's right (global)
    float4 gSUp;        // its up
    float4 gSFwd;       // its forward
};
struct SOut { float4 pos : SV_Position; float2 q : Q; nointerpolation float4 col : COL; nointerpolation float2 shape : SHAPE; };

// The source itself, as engines draw it (Frostbite, UE: the lamp an emissive surface of the light's own luminance): the
// lens' disc with the intensity towards the eye over its area - far over white at night - and no painted glare: the HDR
// bloom (Bloom.cpp) spreads it as a lens does. (A drawn 1/theta^2 glare on top made white discs, 20.10.2026.)
SOut VSSprite(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    SOut o = (SOut)0;
    Sprite s = tSprites[inst];
    o.pos = float4(0, 0, -1, 1);     // culled unless set below
    float z = dot(s.p.xyz, gSFwd.xyz);
    if (z < 0.05) return o;
    // occlusion: the scene's depth round the centre against a point moved towards the eye by the sprite's size
    float4 cc = mul(float4(s.p.xyz, 1), gSVP);
    float2 sp = (cc.xy / cc.w * float2(0.5, -0.5) + 0.5) * gSView.xy;
    float zt = max(z - max(s.p.w * 2, 0.03), 0.02);
    float vis = 0;
    [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int i = -1; i <= 1; i++) {
        int2 q = clamp((int2)sp + int2(i, j) * 2, 0, (int2)gSView.xy - 1);
        vis += tDepth.Load(int3(q, 0)) <= gSView.w / zt ? 1 : 0;
    }
    vis /= 9;
    if (vis <= 0) return o;
    float3 col; float shape = s.c.w;
    float r = s.p.w;
    float3 toEye = normalize(-s.p.xyz);
    if (shape < 0.5) {
        // a lens: the intensity towards the eye over its area (I / r^2 in the client's units); outside the beam the lens
        // still glows by the light scattered in it (~3 % of the beam's), dark from behind
        float cd = dot(toEye, s.d.xyz);
        float cone = smoothstep(s.d.w, s.e.x, cd); cone *= cone;
        cone = max(cone, 0.03 * smoothstep(0.05, 0.5, cd));     // (from behind: the housing)
        col = s.c.rgb * cone / (r * r);
        if (FxActive()) { float h = length(mCentre.xyz) - mCentre.w; col *= exp(-FxExtinction(h) * length(s.p.xyz)); }
    } else {
        // a beacon: bright as a lamp is to the eye whatever the exposure
        col = s.c.rgb * 3 * s.d.x / max(gHazeRGB.w * gAdapt.rgb, 1e-4);
        if (shape > 1.5 && shape < 2.5) col *= 0.12;  // diffuse: a faint halo round the core, not a disc
        if (s.e.x > -1.5) {      // on a spot lamp: from in front of it full, from the side the lens' own glow
            float cd = dot(toEye, s.d.yzw);
            float f = smoothstep(s.e.x - 0.15, min(s.e.x + 0.25, 0.999), cd);
            col *= max(f, 0.05 * smoothstep(0.05, 0.5, cd));
        }
    }
    col *= vis;
    float pxm = gSView.z * 0.5 * gSView.y / z;        // pixels per metre at the sprite
    float rpx = r * pxm;
    if (rpx < 1.2) { col *= (rpx / 1.2) * (rpx / 1.2); rpx = 1.2; }   // a sub-pixel core: its light over 1.2 pixels
    // the glare: out to where k (rc / d)^2 x the core's screen value falls to 0.01
    const float kG = 0.02;
    float Ls = dot(col * gHazeRGB.w * gAdapt.rgb * 1.6, float3(0.2126, 0.7152, 0.0722));
    if (!(Ls > 1e-4)) return o;
    float rq = shape > 2.5 ? 3 : 1.5;                  // the quad in core radii: the core alone ...
    float2 corner = float2((vid == 1 || vid == 2 || vid == 4) ? 1 : -1, (vid == 2 || vid == 4 || vid == 5) ? 1 : -1);
    float rw = rpx * rq / pxm;
    float3 wpos = s.p.xyz + toEye * max(s.p.w * 2, 0.03) + (gSRight.xyz * corner.x + gSUp.xyz * corner.y) * rw;   // a little towards the eye: not cut by its own housing
    o.pos = mul(float4(wpos, 1), gSVP);
    o.q = corner * rq;
    o.col = float4(col, rq);
    o.shape = float2(shape, kG);
    return o;
}

float4 PSSprite(SOut i) : SV_Target
{
    float d2 = dot(i.q, i.q);                          // in core radii
    float shape = i.shape.x;
    float k;
    if (shape < 1.5) {
        float R2 = i.col.w * i.col.w;
        k = smoothstep(1.0, 0.7, sqrt(d2));   // the lens: a disc of its luminance (a Gaussian's tail at this brightness drew the quad's square); its glow is the bloom's
    }
    else if (shape < 2.5) k = exp(-d2 * 2.5);                                // diffuse
    else k = exp(-d2 * 6) + 0.5 * (exp(-abs(i.q.x) * 14) + exp(-abs(i.q.y) * 14)) * exp(-d2 * 0.6);   // star
    return float4(i.col.rgb * k, 1);
}

// ---- the probe for the eye's adaptation (Lights.cpp): 16 x 9 points of the view, each the visible surface's position
// (camera-centred; w 1) or the sky (w 0). The CPU adds up the local lights' illuminance there, two frames later
RWStructuredBuffer<float4> uProbe : register(u0);
[numthreads(16, 9, 1)]
void CSProbe(uint3 id : SV_DispatchThreadID)
{
    float w, h; tDepth.GetDimensions(w, h);
    float2 uv = (id.xy + 0.5) / float2(16, 9);
    float z = tDepth.Load(int3(uv * float2(w, h), 0));
    float4 o = 0;
    if (z > 0) { float4 c = mul(float4(uv * float2(2, -2) + float2(-1, 1), z, 1), gInvVP); o = float4(c.xyz / c.w, 1); }
    uProbe[id.y * 16 + id.x] = o;
}

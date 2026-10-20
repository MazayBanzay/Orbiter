// Device meshes from Orbiter's mesh templates, vessel visuals, animations, vessel rendering.
#include <mutex>
#include <map>
#include "Client.h"

static const DWORD SPEC_DEF = (DWORD)-1, SPEC_INH = (DWORD)-2;

bool DevMesh::Build(ID3D11Device* dev, MESHHANDLE src)
{
    DWORD ng = oapiMeshGroupCount(src);
    DWORD nt = oapiMeshTextureCount(src);
    tex.assign(nt + 1, nullptr);
    // a reference of its own on each texture: the template (or a temporary copy) may release its textures first
    for (DWORD i = 1; i <= nt; i++) { tex[i] = oapiGetTextureHandle(src, i); if (tex[i]) g_client->clbkIncrSurfaceRef(tex[i]); }
    DWORD nm = oapiMeshMaterialCount(src);
    mtrl.resize(nm);
    for (DWORD i = 0; i < nm; i++) { MATERIAL* m = oapiMeshMaterial(src, i); if (m) mtrl[i] = *m; }
    grp.resize(ng);
    std::vector<NTVERTEX> allV;
    std::vector<WORD> allI;
    DWORD prevTex = 0, prevMtrl = SPEC_DEF;
    for (DWORD i = 0; i < ng; i++) {
        MESHGROUP* g = oapiMeshGroup(src, i);
        DGroup& d = grp[i];
        if (!g) continue;
        d.vtx.assign(g->Vtx, g->Vtx + g->nVtx);
        d.idx.assign(g->Idx, g->Idx + g->nIdx);
        // texture: SPEC_DEFAULT none, SPEC_INHERIT the previous group's, else 0-based -> slot +1
        if (g->TexIdx == SPEC_DEF) d.tex = 0;
        else if (g->TexIdx == SPEC_INH) d.tex = prevTex;
        else d.tex = g->TexIdx + 1;
        if (d.tex >= tex.size()) d.tex = 0;
        if (g->MtrlIdx == SPEC_INH) d.mtrl = prevMtrl;
        else d.mtrl = g->MtrlIdx;
        if (d.mtrl != SPEC_DEF && d.mtrl >= nm) d.mtrl = SPEC_DEF;
        prevTex = d.tex; prevMtrl = d.mtrl;
        d.usrFlag = g->UsrFlag; d.flags = g->Flags; d.zBias = g->zBias;
        d.vb0 = (UINT)allV.size(); d.nv = g->nVtx; d.ib0 = (UINT)allI.size(); d.ni = g->nIdx;
        allV.insert(allV.end(), d.vtx.begin(), d.vtx.end());
        allI.insert(allI.end(), d.idx.begin(), d.idx.end());
        XMStoreFloat4x4(&d.xf, XMMatrixIdentity());
    }
    if (allV.empty() || allI.empty()) return false;
    if (allI.size() & 1) allI.push_back(0);
    { extern bool g_traceAlloc; if (g_traceAlloc) LOGC("trace: mesh %u vertices, %u indices (%.1f MB)", (UINT)allV.size(), (UINT)allI.size(), (allV.size() * sizeof(NTVERTEX) + allI.size() * 2) / 1048576.0); }
    D3D11_BUFFER_DESC bd = { (UINT)(allV.size() * sizeof(NTVERTEX)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA sd = { allV.data(), 0, 0 };
    if (FAILED(dev->CreateBuffer(&bd, &sd, &vb))) return false;
    bd = { (UINT)(allI.size() * 2), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    sd = { allI.data(), 0, 0 };
    return SUCCEEDED(dev->CreateBuffer(&bd, &sd, &ib));
}

DevMesh::~DevMesh()
{
    for (SURFHANDLE t : tex) if (t) g_client->clbkReleaseSurface(t);
}

void DevMesh::ResetTransforms()
{
    for (auto& g : grp) XMStoreFloat4x4(&g.xf, XMMatrixIdentity());
}

void DevMesh::UploadGroup(ID3D11DeviceContext* ctx, DWORD g)
{
    DGroup& d = grp[g];
    if (!vb || !d.nv) return;
    D3D11_BOX box = { d.vb0 * (UINT)sizeof(NTVERTEX), 0, 0, (d.vb0 + d.nv) * (UINT)sizeof(NTVERTEX), 1, 1 };
    ctx->UpdateSubresource(vb.Get(), 0, &box, d.vtx.data(), 0, 0);
}

// ------------------------------------------------------------------------------------------ vessel visual
VesselVis::~VesselVis() { for (DevMesh* m : mesh) delete m; }

void VesselVis::DeleteMesh(int idx)
{
    if (idx < 0) { for (DevMesh*& m : mesh) { delete m; m = nullptr; } return; }
    if ((size_t)idx < mesh.size()) { delete mesh[idx]; mesh[idx] = nullptr; }
}

void VesselVis::CreateMeshes()
{
    // animations back to the meshes' own (default) state before new meshes come in their default state
    ResetAnimations();
    UINT n = v->GetMeshCount();
    if (mesh.size() < n) mesh.resize(n, nullptr);
    for (UINT i = 0; i < n; i++) {
        if (mesh[i]) continue;
        MESHHANDLE tpl = v->GetMeshTemplate(i), tmp = nullptr;
        if (!tpl) { tmp = v->CopyMeshFromTemplate(i); tpl = tmp; }
        if (!tpl) continue;
        DevMesh* m = new DevMesh;
        if (m->Build(g_client->Dev(), tpl)) mesh[i] = m; else delete m;
        if (tmp) oapiDeleteMesh(tmp);
    }
}

// one component moved by the transform T (row-vector matrix): its groups, its local vertex list, and - carried along -
// its children with their reference points and axes (Orbiter's animation hierarchy)
static void TransformPoint(VECTOR3& p, const XMMATRIX& T)
{
    XMVECTOR v = XMVector3TransformCoord(XMVectorSet((float)p.x, (float)p.y, (float)p.z, 1), T);
    p = _V(XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v));
}
static void TransformDir(VECTOR3& d, const XMMATRIX& T, bool normalise)
{
    XMVECTOR v = XMVector3TransformNormal(XMVectorSet((float)d.x, (float)d.y, (float)d.z, 0), T);
    if (normalise) v = XMVector3Normalize(v);
    d = _V(XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v));
}

static void AnimateComponent(VesselVis* vv, ANIMATIONCOMP* comp, const XMMATRIX& T)
{
    MGROUP_TRANSFORM* tr = comp->trans;
    if (tr->mesh == LOCALVERTEXLIST) {
        VECTOR3* vtx = (VECTOR3*)tr->grp;
        for (UINT i = 0; i < tr->ngrp; i++) TransformPoint(vtx[i], T);
    } else if (tr->mesh < vv->mesh.size() && vv->mesh[tr->mesh]) {
        DevMesh* m = vv->mesh[tr->mesh];
        auto apply = [&](DGroup& g) { XMStoreFloat4x4(&g.xf, XMLoadFloat4x4(&g.xf) * T); };
        if (tr->grp) { for (UINT i = 0; i < tr->ngrp; i++) if (tr->grp[i] < m->grp.size()) apply(m->grp[tr->grp[i]]); }
        else for (auto& g : m->grp) apply(g);
    }
    for (UINT i = 0; i < comp->nchildren; i++) {
        ANIMATIONCOMP* ch = comp->children[i];
        AnimateComponent(vv, ch, T);
        switch (ch->trans->Type()) {
        case MGROUP_TRANSFORM::ROTATE: { auto* r = (MGROUP_ROTATE*)ch->trans; TransformPoint(r->ref, T); TransformDir(r->axis, T, true); } break;
        case MGROUP_TRANSFORM::TRANSLATE: { auto* t = (MGROUP_TRANSLATE*)ch->trans; TransformDir(t->shift, T, false); } break;
        case MGROUP_TRANSFORM::SCALE: { auto* s = (MGROUP_SCALE*)ch->trans; TransformPoint(s->ref, T); } break;
        default: break;
        }
    }
}

static void Animate(VesselVis* vv, ANIMATION& A, double from, double to)
{
    for (UINT ii = 0; ii < A.ncomp; ii++) {
        UINT i = to > from ? ii : A.ncomp - ii - 1;
        ANIMATIONCOMP* c = A.comp[i];
        double lo = std::min(c->state0, c->state1), hi = std::max(c->state0, c->state1);
        double s0 = std::clamp(from, lo, hi), s1 = std::clamp(to, lo, hi);
        double ds = s1 - s0;
        if (ds == 0 || c->state1 == c->state0) continue;
        ds /= (c->state1 - c->state0);
        XMMATRIX T = XMMatrixIdentity();
        switch (c->trans->Type()) {
        case MGROUP_TRANSFORM::ROTATE: {
            auto* r = (MGROUP_ROTATE*)c->trans;
            XMVECTOR ax = XMVectorSet((float)r->axis.x, (float)r->axis.y, (float)r->axis.z, 0);
            if (XMVectorGetX(XMVector3LengthSq(ax)) < 1e-12f) break;
            XMMATRIX rot = XMMatrixRotationAxis(ax, (float)(r->angle * ds));
            T = XMMatrixTranslation(-(float)r->ref.x, -(float)r->ref.y, -(float)r->ref.z) * rot * XMMatrixTranslation((float)r->ref.x, (float)r->ref.y, (float)r->ref.z);
        } break;
        case MGROUP_TRANSFORM::TRANSLATE: {
            auto* t = (MGROUP_TRANSLATE*)c->trans;
            T = XMMatrixTranslation((float)(t->shift.x * ds), (float)(t->shift.y * ds), (float)(t->shift.z * ds));
        } break;
        case MGROUP_TRANSFORM::SCALE: {
            auto* s = (MGROUP_SCALE*)c->trans;
            T = XMMatrixTranslation(-(float)s->ref.x, -(float)s->ref.y, -(float)s->ref.z) *
                XMMatrixScaling((float)((s->scale.x - 1) * ds + 1), (float)((s->scale.y - 1) * ds + 1), (float)((s->scale.z - 1) * ds + 1)) *
                XMMatrixTranslation((float)s->ref.x, (float)s->ref.y, (float)s->ref.z);
        } break;
        default: continue;
        }
        AnimateComponent(vv, c, T);
    }
}

void VesselVis::ResetAnimations()
{
    ANIMATION* an = nullptr;
    UINT na = v->GetAnimPtr(&an);
    if (animApplied.size() == na) {
        for (UINT i = 0; i < na; i++) if (animApplied[i] != an[i].defstate) Animate(this, an[i], animApplied[i], an[i].defstate);
    }
    animApplied.resize(na);
    for (UINT i = 0; i < na; i++) animApplied[i] = an[i].defstate;
    for (DevMesh* m : mesh) if (m) m->ResetTransforms();
}

void VesselVis::UpdateAnimations()
{
    ANIMATION* an = nullptr;
    UINT na = v->GetAnimPtr(&an);
    if (animApplied.size() != na) {
        size_t old = animApplied.size();
        animApplied.resize(na);
        for (UINT i = (UINT)old; i < na; i++) animApplied[i] = an[i].defstate;
    }
    for (UINT i = 0; i < na; i++) {
        if (!an[i].ncomp) continue;
        if (an[i].state != animApplied[i]) { Animate(this, an[i], animApplied[i], an[i].state); animApplied[i] = an[i].state; }
    }
}

// ------------------------------------------------------------------------------------------ rendering
struct MeshCB { XMFLOAT4X4 wvp, w; XMFLOAT4 sun, diff, amb, spec, emis, flags; XMFLOAT4X4 curNJ, prevNJ; XMFLOAT4 maps, maps2; };

// a texture's companion maps, named as D3D9Client names them next to it (<name>_norm.dds, ...), loaded once per texture
struct MatMaps { ComPtr<ID3D11ShaderResourceView> m[5]; };     // _norm, _spec, _rghn, _metl, _emis
static std::map<std::string, MatMaps> g_matMaps;
static ID3D11Device* g_matDev = nullptr;
static const MatMaps* MapsFor(ID3D11Device* dev, ID3D11DeviceContext* ctx, const Surf* s)
{
    if (!s || s->name.empty()) return nullptr;
    static int off = -1; if (off < 0) { char v[4] = {}; off = GetEnvironmentVariableA("D3D11CLIENT_NOMAPS", v, 4) && v[0] == '1'; }   // test runs: A/B
    if (off) return nullptr;
    if (dev != g_matDev) { g_matMaps.clear(); g_matDev = dev; }
    auto it = g_matMaps.find(s->name);
    if (it != g_matMaps.end()) return &it->second;
    MatMaps& mm = g_matMaps[s->name];
    std::string base = s->name;
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    static const char* suf[5] = { "_norm", "_spec", "_rghn", "_metl", "_emis" };
    static const bool srgb[5] = { false, true, false, false, true };
    int found = 0;
    for (int k = 0; k < 5; k++) {
        const std::string f = base + suf[k] + ".dds";
        for (const char* dir : { "Textures2\\", "Textures\\" }) {
            const std::string p = std::string(dir) + f;
            if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            mm.m[k] = LoadDDSFile(dev, ctx, p.c_str(), srgb[k]);
            if (mm.m[k]) { found++; break; }
        }
    }
    if (found) LOGC("materials: %s with %s%s%s%s%s", s->name.c_str(), mm.m[0] ? "normal " : "", mm.m[1] ? "specular " : "",
                    mm.m[2] ? "smoothness " : "", mm.m[3] ? "metalness " : "", mm.m[4] ? "emission" : "");
    return &mm;
}

static OBJHANDLE FindSun()
{
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) return h; }
    return nullptr;
}

// the world's dirt on a vessel (AtmoFX_SetDirt, the modules: amount 0..1, the dirt's albedo, topY in the vessel frame,
// +y up): drawn in mesh.hlsl (DirtCover) - thick below topY, ragged above it, a light dusting on the surfaces facing up
namespace {
    struct Dirt { double amount, r, g, b, topY; };
    std::mutex dirtMx;
    std::map<OBJHANDLE, Dirt> dirt;
    ComPtr<ID3D11Buffer> dirtCB; ID3D11Device* dirtDev = nullptr;
}
void DirtSet(OBJHANDLE h, double amount, double r, double g, double b, double topY)
{
    std::lock_guard<std::mutex> lk(dirtMx);
    if (!h) return;
    if (amount <= 0) { dirt.erase(h); return; }
    dirt[h] = { std::min(amount, 1.0), r, g, b, topY };
}

void D3D11Client::RenderVessel(VesselVis* vv, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, bool alphaPass, const XMMATRIX* lightVP)
{
    if (!vv->v) return;
    if (!alphaPass && !mirrorPass) vv->UpdateAnimations();
    const bool shadowPass = lightVP != nullptr;
    VECTOR3 gp; vv->v->GetGlobalPos(gp);
    MATRIX3 R; vv->v->GetRotationMatrix(R);
    VECTOR3 rel = gp - cp;
    double dist = length(rel);
    double size = vv->v->GetSize();
    if (!shadowPass && size / std::max(dist, 1e-3) * H < 0.5) return;     // smaller than a pixel
    const OBJHANDLE hSun = FindSun();     // per call: a static kept the first session's Sun - freed when the next scenario loads (a crash in Orbiter.exe)
    VECTOR3 sp = _V(0, 0, 0); if (hSun) oapiGetGlobalPos(hSun, &sp);
    VECTOR3 sd = sp - gp; sd = sd / length(sd);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    XMMATRIX VP = shadowPass ? *lightVP : mirrorPass ? mirrorM * V * proj : V * proj;   // the liquid's mirror: the view reflected in its plane
    bool internal = oapiCameraInternal() && oapiGetFocusObject() == vv->hObj;
    bool vc = oapiCockpitMode() == COCKPIT_VIRTUAL;
    if (internal && !shadowPass && diagFrames > 0) {
        VECTOR3 cpos; oapiCameraGlobalPos(&cpos);
        VECTOR3 lc = tmul(R, cpos - gp);
        LOGC("diag cockpit: vessel %s camera at %.3f %.3f %.3f (vessel frame), cockpit mode %d, meshes %zu", vv->v->GetName(), lc.x, lc.y, lc.z, oapiCockpitMode(), vv->mesh.size());
    }

    ctx->IASetInputLayout(ilMesh.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsMesh.Get(), nullptr, 0);
    ctx->PSSetShader(shadowPass ? nullptr : psMesh.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->RSSetState(shadowPass ? rsShadow.Get() : mirrorPass ? rsMirror.Get() : rsBack.Get());   // mirrored: the winding turns
    if (!shadowPass) {     // this vessel's dirt (b7), or none
        if (dirtDev != dev.Get()) { dirtCB.Reset(); dirtDev = dev.Get(); }
        if (!dirtCB) { D3D11_BUFFER_DESC bd = { 80, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &dirtCB); }
        XMFLOAT4 dc[5] = {};
        {
            std::lock_guard<std::mutex> lk(dirtMx);
            auto it = dirt.find(vv->hObj);
            if (it != dirt.end()) {
                const Dirt& d = it->second;
                dc[0] = { (float)rel.x, (float)rel.y, (float)rel.z, (float)d.amount };
                dc[1] = { (float)R.m11, (float)R.m21, (float)R.m31, 0 };
                dc[2] = { (float)R.m12, (float)R.m22, (float)R.m32, (float)d.topY };
                dc[3] = { (float)R.m13, (float)R.m23, (float)R.m33, 0 };
                dc[4] = { (float)d.r, (float)d.g, (float)d.b, 0 };
            }
        }
        D3D11_MAPPED_SUBRESOURCE dm;
        if (dirtCB && SUCCEEDED(ctx->Map(dirtCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &dm))) { memcpy(dm.pData, dc, sizeof dc); ctx->Unmap(dirtCB.Get(), 0); }
        ctx->PSSetConstantBuffers(7, 1, dirtCB.GetAddressOf());
    }
    ctx->OMSetBlendState(bsAlpha.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(shadowPass ? dsShadow.Get() : alphaPass ? dsRead.Get() : dsWrite.Get(), 0);

    for (UINT mi = 0; mi < vv->mesh.size(); mi++) {
        DevMesh* m = vv->mesh[mi];
        if (!m || !m->vb) continue;
        WORD mode = vv->v->GetMeshVisibilityMode(mi);
        bool show = internal ? ((mode & MESHVIS_COCKPIT) || ((mode & MESHVIS_VC) && vc)) : (mode & MESHVIS_EXTERNAL) != 0;
        if (shadowPass || mirrorPass) show = (mode & MESHVIS_EXTERNAL) != 0;     // the shadow and the reflection: the outside
        if (!show) continue;
        VECTOR3 ofs = _V(0, 0, 0); vv->v->GetMeshOffset(mi, ofs);
        VECTOR3 t = rel + mul(R, ofs);
        XMMATRIX W0 = RotToXM(R);
        W0.r[3] = XMVectorSet((float)t.x, (float)t.y, (float)t.z, 1);
        UINT stride = sizeof(NTVERTEX), off = 0;
        ctx->IASetVertexBuffers(0, 1, m->vb.GetAddressOf(), &stride, &off);
        ctx->IASetIndexBuffer(m->ib.Get(), DXGI_FORMAT_R16_UINT, 0);
        for (DGroup& g : m->grp) {
            if (!g.ni || (g.usrFlag & 2)) continue;
            MATERIAL mt = { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 0 }, 0 };
            if (g.mtrl != SPEC_DEF && g.mtrl < m->mtrl.size()) mt = m->mtrl[g.mtrl];
            bool transparent = mt.diffuse.a < 0.99f;
            if (transparent != alphaPass) continue;
            XMMATRIX W = XMLoadFloat4x4(&g.xf) * W0;
            MeshCB c;
            XMStoreFloat4x4(&c.wvp, XMMatrixTranspose(W * VP));
            if (!shadowPass && !mirrorPass) {
                XMMATRIX cur = W * XMLoadFloat4x4(&curVPNoJit);
                XMStoreFloat4x4(&c.curNJ, XMMatrixTranspose(cur));
                if (g.hasPrev) XMStoreFloat4x4(&c.prevNJ, XMMatrixTranspose(XMLoadFloat4x4(&g.prevWVP))); else c.prevNJ = c.curNJ;
                XMStoreFloat4x4(&g.prevWVP, cur); g.hasPrev = true;
            } else { c.curNJ = c.wvp; c.prevNJ = c.wvp; }
            XMStoreFloat4x4(&c.w, XMMatrixTranspose(W));
            c.sun = { (float)sd.x, (float)sd.y, (float)sd.z, 0 };
            c.diff = { mt.diffuse.r, mt.diffuse.g, mt.diffuse.b, mt.diffuse.a };
            c.amb = { mt.ambient.r, mt.ambient.g, mt.ambient.b, mt.ambient.a };
            c.spec = { mt.specular.r, mt.specular.g, mt.specular.b, mt.power };
            c.emis = { mt.emissive.r, mt.emissive.g, mt.emissive.b, mt.emissive.a };
            Surf* s = g.tex && g.tex < m->tex.size() ? (Surf*)m->tex[g.tex] : nullptr;
            ID3D11ShaderResourceView* srv = s && s->srv ? s->srv.Get() : whiteTex.Get();
            // the mesh file's FLAG (UsrFlag) 4: not lit. In the cockpit such a group (a display, the helmet's light layer) is
            // seen by the eye as it is: the eye's adaptation to a dim scene (the FX exposure) must not brighten it
            const bool unlit = (g.usrFlag & 4) != 0;
            c.flags = { s && s->srv ? 1.0f : 0.0f, (float)internal, unlit ? 1.0f : 0.0f, internal ? 1.0f : 0.0f };
            // the texture's material maps (t1..t5)
            const MatMaps* mm = (!shadowPass && s && s->srv) ? MapsFor(dev.Get(), ctx.Get(), s) : nullptr;
            ID3D11ShaderResourceView* msrv[5] = {};
            if (mm) for (int k = 0; k < 5; k++) msrv[k] = mm->m[k].Get();
            c.maps = { msrv[0] ? 1.0f : 0.0f, msrv[1] ? 1.0f : 0.0f, msrv[2] ? 1.0f : 0.0f, msrv[3] ? 1.0f : 0.0f };
            // skin: the texture's name (a head, a face, a hand) - its light scatters under the surface (mesh.hlsl)
            bool skin = false;
            if (s && !s->name.empty()) { std::string ln = s->name; for (char& ch : ln) ch = (char)tolower((unsigned char)ch); skin = ln.find("skin") != std::string::npos; }
            const bool filter = (g.usrFlag & 0x100) != 0;      // a filter group (the visor's sun spot): Dest x transmittance
            if (filter && !shadowPass) {   // ... over the Sun: the glare in the eye goes through it too (its transmittance, mesh.hlsl)
                extern bool g_sunFilterSeen; extern float g_sunFilterT; extern int g_sunFilterCount; extern float g_sunCoreD;
                g_sunFilterSeen = true; g_sunFilterCount++;
                g_sunFilterT = std::min(g_sunFilterT, exp2f(-g_sunCoreD * std::clamp(mt.diffuse.a / 0.7f, 0.0f, 1.0f)));
            }
            if (filter && (g.usrFlag & 0x200) && !shadowPass) { extern bool g_sunHAlphaSeen; g_sunHAlphaSeen = true; }   // ... an H-alpha one: Sun.cpp draws the H-alpha Sun
            c.maps2 = { msrv[4] ? 1.0f : 0.0f, skin ? 1.0f : 0.0f, filter ? 1.0f : 0.0f, 0 };
            if (filter) {   // the filter's own: y its density over the Sun's disc [log2], w the disc's angular radius (Sun.cpp, the frame before)
                extern float g_sunCoreD, g_sunDiscRad;
                c.maps2.y = g_sunCoreD; c.maps2.w = g_sunDiscRad;
            }
            if (!shadowPass) ctx->PSSetShaderResources(1, 5, msrv);
            D3D11_MAPPED_SUBRESOURCE ms;
            ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
            if (!shadowPass) {     // the material's emission set by a module (oapiSetMaterialEx Emission), b8
                static ComPtr<ID3D11Buffer> emCB; static ID3D11Device* emDev = nullptr;
                if (emDev != dev.Get()) { emCB.Reset(); emDev = dev.Get(); }
                if (!emCB) { D3D11_BUFFER_DESC bd = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &emCB); }
                const XMFLOAT4 e2 = (g.mtrl != SPEC_DEF && g.mtrl < m->emis2.size()) ? m->emis2[g.mtrl] : XMFLOAT4(0, 0, 0, 0);
                D3D11_MAPPED_SUBRESOURCE em;
                if (emCB && SUCCEEDED(ctx->Map(emCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &em))) { memcpy(em.pData, &e2, sizeof e2); ctx->Unmap(emCB.Get(), 0); }
                ctx->PSSetConstantBuffers(8, 1, emCB.GetAddressOf());
            }
            ctx->PSSetShaderResources(0, 1, &srv);
            if (!shadowPass) {
                static ComPtr<ID3D11BlendState> bsFilt; static ID3D11Device* bfDev = nullptr;
                if (bfDev != dev.Get()) { bfDev = dev.Get(); bsFilt.Reset(); D3D11_BLEND_DESC b = {}; b.RenderTarget[0] = { TRUE, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE }; dev->CreateBlendState(&b, &bsFilt); }
                ctx->OMSetBlendState((g.usrFlag & 0x100) ? bsFilt.Get() : (g.usrFlag & 8) ? bsAdd.Get() : bsAlpha.Get(), nullptr, ~0u);   // FLAG 8: added; 0x100: a filter
            }
            // diagnostics (test runs): in the cockpit view, the groups whose box covers much of the screen near the camera
            if (internal && !shadowPass && diagFrames > 0 && !g.vtx.empty()) {
                XMMATRIX WVPd = W * VP;
                float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f, mnz = 1e9f; int behind = 0;
                for (size_t k = 0; k < g.vtx.size(); k += std::max<size_t>(1, g.vtx.size() / 64)) {
                    XMVECTOR c = XMVector4Transform(XMVectorSet(g.vtx[k].x, g.vtx[k].y, g.vtx[k].z, 1), WVPd);
                    float w = XMVectorGetW(c);
                    if (w <= 0) { behind++; continue; }
                    float x = XMVectorGetX(c) / w, y = XMVectorGetY(c) / w;
                    mnx = std::min(mnx, x); mxx = std::max(mxx, x); mny = std::min(mny, y); mxy = std::max(mxy, y); mnz = std::min(mnz, w);
                }
                float cov = std::max(0.0f, std::min(mxx, 1.0f) - std::max(mnx, -1.0f)) * std::max(0.0f, std::min(mxy, 1.0f) - std::max(mny, -1.0f)) / 4.0f;
                if (cov > 0.3f || behind > 0)
                    LOGC("diag cockpit: vessel %s mesh %u grp %u nv %u cover %.2f nearest %.3f m behind %d mtrl %d diff %.2f %.2f %.2f a %.2f tex %s usr 0x%x",
                         vv->v->GetName(), mi, (UINT)(&g - &m->grp[0]), g.nv, cov, mnz, behind, (int)g.mtrl, mt.diffuse.r, mt.diffuse.g, mt.diffuse.b, mt.diffuse.a,
                         s ? s->name.c_str() : "-", g.usrFlag);
            }
            if (ownPass && (g.usrFlag & 4)) {     // an overlay layer of the cockpit (the visor's plates): seen, not in the way
                ctx->OMSetDepthStencilState(dsRead.Get(), 0);
                ctx->DrawIndexed(g.ni, g.ib0, g.vb0);
                ctx->OMSetDepthStencilState(alphaPass ? dsRead.Get() : dsWrite.Get(), 0);
            } else ctx->DrawIndexed(g.ni, g.ib0, g.vb0);
        }
    }
}

// Device meshes from Orbiter's mesh templates, vessel visuals, animations, vessel rendering.
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
struct MeshCB { XMFLOAT4X4 wvp, w; XMFLOAT4 sun, diff, amb, spec, emis, flags; XMFLOAT4X4 curNJ, prevNJ; };

static OBJHANDLE FindSun()
{
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) return h; }
    return nullptr;
}

void D3D11Client::RenderVessel(VesselVis* vv, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, bool alphaPass, const XMMATRIX* lightVP)
{
    if (!vv->v) return;
    if (!alphaPass) vv->UpdateAnimations();
    const bool shadowPass = lightVP != nullptr;
    VECTOR3 gp; vv->v->GetGlobalPos(gp);
    MATRIX3 R; vv->v->GetRotationMatrix(R);
    VECTOR3 rel = gp - cp;
    double dist = length(rel);
    double size = vv->v->GetSize();
    if (!shadowPass && size / std::max(dist, 1e-3) * H < 0.5) return;     // smaller than a pixel
    static OBJHANDLE hSun = FindSun();
    VECTOR3 sp = _V(0, 0, 0); if (hSun) oapiGetGlobalPos(hSun, &sp);
    VECTOR3 sd = sp - gp; sd = sd / length(sd);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    XMMATRIX VP = shadowPass ? *lightVP : V * proj;
    bool internal = oapiCameraInternal() && oapiGetFocusObject() == vv->hObj;
    bool vc = oapiCockpitMode() == COCKPIT_VIRTUAL;

    ctx->IASetInputLayout(ilMesh.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsMesh.Get(), nullptr, 0);
    ctx->PSSetShader(shadowPass ? nullptr : psMesh.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->RSSetState(shadowPass ? rsShadow.Get() : rsBack.Get());
    ctx->OMSetBlendState(bsAlpha.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(shadowPass ? dsShadow.Get() : alphaPass ? dsRead.Get() : dsWrite.Get(), 0);

    for (UINT mi = 0; mi < vv->mesh.size(); mi++) {
        DevMesh* m = vv->mesh[mi];
        if (!m || !m->vb) continue;
        WORD mode = vv->v->GetMeshVisibilityMode(mi);
        bool show = internal ? ((mode & MESHVIS_COCKPIT) || ((mode & MESHVIS_VC) && vc)) : (mode & MESHVIS_EXTERNAL) != 0;
        if (shadowPass) show = (mode & MESHVIS_EXTERNAL) != 0;     // the shadow is cast by the outside
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
            if (!shadowPass) {
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
            c.flags = { s && s->srv ? 1.0f : 0.0f, (float)internal, 0, 0 };
            D3D11_MAPPED_SUBRESOURCE ms;
            ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->DrawIndexed(g.ni, g.ib0, g.vb0);
        }
    }
}

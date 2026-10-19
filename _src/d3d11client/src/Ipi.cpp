// gcCore image processing (gcIPInterface): a module's own pixel shader run over its surfaces, as D3D9Client offers it.
// The modules' shaders are written for D3D9 (sampler + tex2D, ': COLOR'): they compile here for ps_4_0 in the
// compiler's backwards-compatibility mode, unchanged. The vertex shader is made to match each pixel shader's inputs:
// TEXCOORD0 carries u and TEXCOORD1 carries v (as D3D9Client's IPI.hlsl gives them), a float2 TEXCOORD0 gets (u, v).
// The surfaces are read and written in gamma space (UNORM views), as D3D9 does.
#include "Client.h"
#include "gcCoreAPI.h"
#include <d3d11shader.h>
#pragma comment(lib, "dxguid.lib")

class ImageProcessing
{
public:
    struct Var { int cb; UINT off, size; };
    struct Slot { SURFHANDLE tex = nullptr; DWORD flags = 0; };
    struct Shader {
        ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11VertexShader> vs;
        std::map<std::string, Var> vars;
        std::map<std::string, UINT> texSlot, smpSlot;
        std::vector<std::vector<BYTE>> cbData; std::vector<ComPtr<ID3D11Buffer>> cb; std::vector<bool> cbDirty;
        Slot slots[16];
    };
    std::string file, ppf;
    std::map<std::string, Shader> shaders;
    Shader* cur = nullptr;
    SURFHANDLE out[4] = {};
    float rw = 1, rh = 1, rx = 0, ry = 0;
    std::map<DWORD, ComPtr<ID3D11SamplerState>> samplers;
    ComPtr<ID3D11BlendState> bsBlend, bsNone;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> ds;

    ImageProcessing(const char* f, const char* p) : file(f ? f : ""), ppf(p ? p : "") {}

    bool Compile(const char* entry)
    {
        D3D11Client* c = g_client; if (!c || !entry) return false;
        ID3D11Device* dev = c->Dev();
        // the defines: "A;B=1" (D3D9Client's ppf)
        std::vector<std::string> names, vals; std::vector<D3D_SHADER_MACRO> mac;
        for (size_t i = 0; i < ppf.size();) {
            size_t j = ppf.find_first_of(";, ", i); if (j == std::string::npos) j = ppf.size();
            std::string d = ppf.substr(i, j - i); i = j + 1;
            if (d.empty()) continue;
            size_t e = d.find('=');
            names.push_back(e == std::string::npos ? d : d.substr(0, e)); vals.push_back(e == std::string::npos ? "1" : d.substr(e + 1));
        }
        for (size_t i = 0; i < names.size(); i++) mac.push_back({ names[i].c_str(), vals[i].c_str() });
        mac.push_back({ "D3D11CLIENT", "1" }); mac.push_back({ nullptr, nullptr });
        std::wstring wp(file.begin(), file.end());
        ComPtr<ID3DBlob> code, err;
        HRESULT hr = D3DCompileFromFile(wp.c_str(), mac.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, "ps_4_0",
                                        D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
        if (err) LOGC("IPInterface %s %s: %s", file.c_str(), entry, (const char*)err->GetBufferPointer());
        if (FAILED(hr)) return false;
        Shader sh;
        if (FAILED(dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &sh.ps))) return false;
        ComPtr<ID3D11ShaderReflection> rf;
        if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)rf.GetAddressOf()))) return false;
        D3D11_SHADER_DESC sd; rf->GetDesc(&sd);
        // the vertex shader for these inputs, in their order
        std::string vs = "struct O {\n", body;
        bool hasPos = false;
        for (UINT i = 0; i < sd.InputParameters; i++) {
            D3D11_SIGNATURE_PARAMETER_DESC p; rf->GetInputParameterDesc(i, &p);
            int n = 0; for (int b = 0; b < 4; b++) if (p.Mask & (1 << b)) n++;    // packed inputs: x in .x, y in .y of one register
            char ln[160];
            if (p.SystemValueType == D3D_NAME_POSITION) { vs += "  float4 pos : SV_Position;\n"; hasPos = true; continue; }
            if (p.SystemValueType != D3D_NAME_UNDEFINED) continue;
            const char* ty = n <= 1 ? "float" : n == 2 ? "float2" : n == 3 ? "float3" : "float4";
            sprintf_s(ln, "  %s v%u : %s%u;\n", ty, i, p.SemanticName, p.SemanticIndex); vs += ln;
            const char* val = "0";
            if (!_stricmp(p.SemanticName, "TEXCOORD") && p.SemanticIndex == 0) val = n <= 1 ? "uv.x" : "float4(uv, 0, 0)";
            else if (!_stricmp(p.SemanticName, "TEXCOORD") && p.SemanticIndex == 1) val = n <= 1 ? "uv.y" : "float4(uv, 0, 0)";
            sprintf_s(ln, "  o.v%u = (%s)%s;\n", i, ty, val); body += ln;
        }
        if (!hasPos) vs += "  float4 pos : SV_Position;\n";
        vs += "};\nO VS(uint id : SV_VertexID) {\n  float2 uv = float2((id << 1) & 2, id & 2);\n  O o = (O)0;\n"
              "  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n" + body + "  return o;\n}\n";
        ComPtr<ID3DBlob> vcode;
        err.Reset();
        if (FAILED(D3DCompile(vs.data(), vs.size(), "ipi_vs", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vcode, &err))) {
            LOGC("IPInterface vertex shader for %s %s: %s", file.c_str(), entry, err ? (const char*)err->GetBufferPointer() : "?");
            return false;
        }
        dev->CreateVertexShader(vcode->GetBufferPointer(), vcode->GetBufferSize(), nullptr, &sh.vs);
        // the constants and the textures by name
        for (UINT i = 0; i < sd.ConstantBuffers; i++) {
            ID3D11ShaderReflectionConstantBuffer* cb = rf->GetConstantBufferByIndex(i);
            D3D11_SHADER_BUFFER_DESC bd; cb->GetDesc(&bd);
            if (bd.Type != D3D_CT_CBUFFER) continue;
            D3D11_SHADER_INPUT_BIND_DESC bind; if (FAILED(rf->GetResourceBindingDescByName(bd.Name, &bind))) continue;
            int idx = (int)sh.cb.size();
            sh.cbData.emplace_back((bd.Size + 15) & ~15u, 0); sh.cbDirty.push_back(true);
            D3D11_BUFFER_DESC b = { (bd.Size + 15) & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
            ComPtr<ID3D11Buffer> buf; dev->CreateBuffer(&b, nullptr, &buf);
            if ((int)bind.BindPoint != idx) LOGC("IPInterface %s: constant buffer %s at b%u", file.c_str(), bd.Name, bind.BindPoint);
            sh.cb.push_back(buf);
            for (UINT v = 0; v < bd.Variables; v++) {
                D3D11_SHADER_VARIABLE_DESC vd; cb->GetVariableByIndex(v)->GetDesc(&vd);
                sh.vars[vd.Name] = { idx, vd.StartOffset, vd.Size };
            }
        }
        for (UINT i = 0; i < sd.BoundResources; i++) {
            D3D11_SHADER_INPUT_BIND_DESC bind; rf->GetResourceBindingDesc(i, &bind);
            if (bind.Type == D3D_SIT_TEXTURE) sh.texSlot[bind.Name] = bind.BindPoint;
            else if (bind.Type == D3D_SIT_SAMPLER) sh.smpSlot[bind.Name] = bind.BindPoint;
        }
        shaders[entry] = std::move(sh);
        cur = &shaders[entry];
        if (!bsBlend) {
            D3D11_BLEND_DESC b = {};
            b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            dev->CreateBlendState(&b, &bsNone);
            b.RenderTarget[0].BlendEnable = TRUE;
            b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA; b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            dev->CreateBlendState(&b, &bsBlend);
            D3D11_RASTERIZER_DESC r = {}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = TRUE;
            dev->CreateRasterizerState(&r, &rs);
            D3D11_DEPTH_STENCIL_DESC d = {}; dev->CreateDepthStencilState(&d, &ds);
        }
        return true;
    }

    ID3D11SamplerState* Sampler(DWORD f)
    {
        auto& s = samplers[f];
        if (s) return s.Get();
        D3D11_SAMPLER_DESC d = {};
        auto mode = [&](DWORD cl, DWORD mi) { return (f & cl) ? D3D11_TEXTURE_ADDRESS_CLAMP : (f & mi) ? D3D11_TEXTURE_ADDRESS_MIRROR : D3D11_TEXTURE_ADDRESS_WRAP; };
        d.AddressU = mode(IPF_CLAMP_U, IPF_MIRROR_U); d.AddressV = mode(IPF_CLAMP_V, IPF_MIRROR_V); d.AddressW = mode(IPF_CLAMP_W, IPF_MIRROR_W);
        d.Filter = (f & IPF_ANISOTROPIC) ? D3D11_FILTER_ANISOTROPIC : (f & (IPF_LINEAR | IPF_PYRAMIDAL | IPF_GAUSSIAN)) ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT;
        d.MaxAnisotropy = 8; d.MaxLOD = 0; d.ComparisonFunc = D3D11_COMPARISON_NEVER;
        g_client->Dev()->CreateSamplerState(&d, &s);
        return s.Get();
    }

    void Set(const char* var, const void* p, int bytes)
    {
        if (!cur || !var) return;
        auto it = cur->vars.find(var);
        if (it == cur->vars.end()) return;
        const Var& v = it->second;
        UINT n = std::min<UINT>((UINT)bytes, v.size);
        memcpy(cur->cbData[v.cb].data() + v.off, p, n);
        cur->cbDirty[v.cb] = true;
    }

    // the texture's sampler slot: the texture of that name, else the one the compatibility mode made for the sampler
    int TexSlot(const std::string& var) const
    {
        auto it = cur->texSlot.find(var);
        if (it != cur->texSlot.end()) return (int)it->second;
        auto is = cur->smpSlot.find(var);
        if (is != cur->smpSlot.end()) return (int)is->second;
        for (auto& kv : cur->texSlot) if (kv.first.find(var) != std::string::npos) return (int)kv.second;
        return -1;
    }

    bool Execute(DWORD blendop)
    {
        D3D11Client* c = g_client;
        if (!c || !cur || !out[0]) return false;
        return c->RunIPI(this, blendop);
    }
};

// the GPU part, with the client's internals
bool D3D11Client::RunIPI(ImageProcessing* p, DWORD blendop)
{
    ImageProcessing::Shader* sh = p->cur;
    ID3D11RenderTargetView* rtv[4] = {}; UINT nrt = 0;
    Surf* o0 = (Surf*)p->out[0];
    for (int i = 0; i < 4 && p->out[i]; i++) {
        Surf* s = (Surf*)p->out[i];
        if (!SurfGpuViews(s) || !s->rtvLin) return false;
        rtv[nrt++] = s->rtvLin.Get();
    }
    // the inputs: their drawing goes to the GPU first
    ID3D11ShaderResourceView* srv[16] = {}; ID3D11SamplerState* smp[16] = {};
    UINT nsrv = 0;
    for (int k = 0; k < 16; k++) {
        ImageProcessing::Slot& sl = sh->slots[k];
        if (!sl.tex) continue;
        Surf* s = (Surf*)sl.tex;
        if (dirty.count(s)) { UploadSurface(s); dirty.erase(s); }
        SurfGpuViews(s);
        srv[k] = s->srvLin ? s->srvLin.Get() : s->srv.Get();
        smp[k] = p->Sampler(sl.flags);
        nsrv = std::max(nsrv, (UINT)k + 1);
    }
    // the pipeline as it was (this may run in the middle of a frame)
    ComPtr<ID3D11RenderTargetView> oRtv[4]; ComPtr<ID3D11DepthStencilView> oDsv;
    ctx->OMGetRenderTargets(4, oRtv[0].GetAddressOf(), &oDsv);
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT ovp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ctx->RSGetViewports(&nvp, ovp);
    ComPtr<ID3D11VertexShader> oVs; ComPtr<ID3D11PixelShader> oPs; ctx->VSGetShader(&oVs, nullptr, nullptr); ctx->PSGetShader(&oPs, nullptr, nullptr);
    ComPtr<ID3D11InputLayout> oIl; ctx->IAGetInputLayout(&oIl);
    D3D11_PRIMITIVE_TOPOLOGY oTop; ctx->IAGetPrimitiveTopology(&oTop);
    ComPtr<ID3D11BlendState> oBs; float oBf[4]; UINT oMask; ctx->OMGetBlendState(&oBs, oBf, &oMask);
    ComPtr<ID3D11DepthStencilState> oDs; UINT oRef; ctx->OMGetDepthStencilState(&oDs, &oRef);
    ComPtr<ID3D11RasterizerState> oRs; ctx->RSGetState(&oRs);
    ID3D11ShaderResourceView* oSrv[16] = {}; ctx->PSGetShaderResources(0, 16, oSrv);
    ID3D11SamplerState* oSmp[16] = {}; ctx->PSGetSamplers(0, 16, oSmp);
    ID3D11Buffer* oCb[4] = {}; ctx->PSGetConstantBuffers(0, 4, oCb);

    ID3D11ShaderResourceView* nul16[16] = {};
    ctx->PSSetShaderResources(0, 16, nul16);
    ctx->OMSetRenderTargets(nrt, rtv, nullptr);
    D3D11_VIEWPORT vp = { p->rx * o0->w, p->ry * o0->h, p->rw * o0->w, p->rh * o0->h, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(blendop ? p->bsBlend.Get() : p->bsNone.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(p->ds.Get(), 0);
    ctx->RSSetState(p->rs.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(sh->vs.Get(), nullptr, 0);
    ctx->PSSetShader(sh->ps.Get(), nullptr, 0);
    for (size_t i = 0; i < sh->cb.size(); i++) {
        if (sh->cbDirty[i]) { ctx->UpdateSubresource(sh->cb[i].Get(), 0, nullptr, sh->cbData[i].data(), 0, 0); sh->cbDirty[i] = false; }
        ctx->PSSetConstantBuffers((UINT)i, 1, sh->cb[i].GetAddressOf());
    }
    if (nsrv) { ctx->PSSetShaderResources(0, nsrv, srv); ctx->PSSetSamplers(0, nsrv, smp); }
    ctx->Draw(3, 0);

    // back as it was
    ctx->PSSetShaderResources(0, 16, nul16);
    ID3D11RenderTargetView* ort[4] = { oRtv[0].Get(), oRtv[1].Get(), oRtv[2].Get(), oRtv[3].Get() };
    ctx->OMSetRenderTargets(4, ort, oDsv.Get());
    if (nvp) ctx->RSSetViewports(nvp, ovp);
    ctx->VSSetShader(oVs.Get(), nullptr, 0); ctx->PSSetShader(oPs.Get(), nullptr, 0);
    ctx->IASetInputLayout(oIl.Get()); ctx->IASetPrimitiveTopology(oTop);
    ctx->OMSetBlendState(oBs.Get(), oBf, oMask); ctx->OMSetDepthStencilState(oDs.Get(), oRef); ctx->RSSetState(oRs.Get());
    ctx->PSSetShaderResources(0, 16, oSrv); ctx->PSSetSamplers(0, 16, oSmp); ctx->PSSetConstantBuffers(0, 4, oCb);
    for (auto* x : oSrv) if (x) x->Release();
    for (auto* x : oSmp) if (x) x->Release();
    for (auto* x : oCb) if (x) x->Release();
    for (int i = 0; i < 4 && p->out[i]; i++) { Surf* s = (Surf*)p->out[i]; s->gpuNewer = true; dirty.erase(s); }
    // diagnostics (test runs, once): the first input and the output as files
    static bool dumped = false;
    if (diagFrames && !dumped) {
        dumped = true;
        for (int k = 0; k < 16; k++) if (sh->slots[k].tex) { DumpSurface((Surf*)sh->slots[k].tex, "Modules/D3D11Client/shots/ipi_in.bmp"); break; }
        DumpSurface(o0, "Modules/D3D11Client/shots/ipi_out.bmp");
    }
    return true;
}

// ------------------------------------------------------------------------------------------ gcIPInterface (the module's side)
gcIPInterface::~gcIPInterface() { delete pIPI; }
bool gcIPInterface::CompileShader(const char* PSEntry) { return pIPI->Compile(PSEntry); }
bool gcIPInterface::Activate(const char* Shader)
{
    if (!Shader) return pIPI->cur != nullptr;
    auto it = pIPI->shaders.find(Shader);
    if (it == pIPI->shaders.end()) return false;
    pIPI->cur = &it->second; return true;
}
void gcIPInterface::SetFloat(const char* var, float val) { pIPI->Set(var, &val, 4); }
void gcIPInterface::SetInt(const char* var, int val) { pIPI->Set(var, &val, 4); }
void gcIPInterface::SetBool(const char* var, bool val) { int v = val ? 1 : 0; pIPI->Set(var, &v, 4); }
void gcIPInterface::SetFloat(const char* var, const void* val, int bytes) { pIPI->Set(var, val, bytes); }
void gcIPInterface::SetInt(const char* var, const int* val, int bytes) { pIPI->Set(var, val, bytes); }
void gcIPInterface::SetBool(const char* var, const bool* val, int bytes)
{
    std::vector<int> v(bytes > 0 ? bytes : 0); for (int i = 0; i < bytes; i++) v[i] = val[i] ? 1 : 0;
    pIPI->Set(var, v.data(), bytes * 4);
}
void gcIPInterface::SetStruct(const char* var, const void* val, int bytes) { pIPI->Set(var, val, bytes); }
void gcIPInterface::SetVSFloat(const char*, float) {}
void gcIPInterface::SetVSInt(const char*, int) {}
void gcIPInterface::SetVSBool(const char*, bool) {}
void gcIPInterface::SetVSFloat(const char*, const void*, int) {}
void gcIPInterface::SetVSInt(const char*, const int*, int) {}
void gcIPInterface::SetVSBool(const char*, const bool*, int) {}
void gcIPInterface::SetVSStruct(const char*, const void*, int) {}
void gcIPInterface::SetTexture(const char* var, SURFHANDLE hTex, DWORD flags)
{
    if (!pIPI->cur || !var) return;
    int k = pIPI->TexSlot(var);
    if (k < 0 || k >= 16) { LOGC("IPInterface::SetTexture: no sampler [%s] in %s", var, pIPI->file.c_str()); return; }
    pIPI->cur->slots[k] = { hTex, flags };
}
void gcIPInterface::SetOutput(int id, SURFHANDLE hSrf)
{
    if (id < 0 || id > 3) return;
    pIPI->out[id] = hSrf;
    if (!hSrf) for (int i = id + 1; i < 4; i++) pIPI->out[i] = nullptr;
}
void gcIPInterface::SetOutputRegion(float w, float h, float x, float y) { pIPI->rw = w; pIPI->rh = h; pIPI->rx = x; pIPI->ry = y; }
bool gcIPInterface::IsOK() { return pIPI->cur && pIPI->cur->ps && pIPI->cur->vs; }
void gcIPInterface::SetMesh(const MESHHANDLE, const char*, ipicull) {}
bool gcIPInterface::Execute(bool) { return pIPI->Execute(0); }
bool gcIPInterface::Execute(DWORD blendop, bool, ipitemplate) { return pIPI->Execute(blendop); }
int gcIPInterface::FindDefine(const char*) { return 0; }

gcIPInterface* __cdecl IpiCreate(const char* file, const char* PSEntry, const char* VSEntry, const char* ppf)
{
    if (!file || !PSEntry) return nullptr;
    ImageProcessing* p = new ImageProcessing(file, ppf);
    if (!p->Compile(PSEntry)) { LOGC("IPInterface %s %s: not compiled", file, PSEntry); delete p; return nullptr; }
    if (VSEntry) LOGC("IPInterface %s: own vertex shader %s ignored (a generated one is used)", file, VSEntry);
    LOGC("IPInterface %s %s: ok", file, PSEntry);
    return new gcIPInterface(p);
}
void __cdecl IpiRelease(gcIPInterface* p) { delete p; }

// a surface's pixels to a .bmp (diagnostics): read back from the GPU through a staging copy of its texture
void D3D11Client::DumpSurface(Surf* s, const char* path)
{
    if (!s || !s->tex) return;
    D3D11_TEXTURE2D_DESC td; s->tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &st))) return;
    ctx->CopyResource(st.Get(), s->tex.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms))) return;
    FILE* f = fopen(path, "wb");
    if (f) {
        BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
        ih.biSize = sizeof ih; ih.biWidth = (LONG)s->w; ih.biHeight = -(LONG)s->h; ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + s->w * s->h * 4;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
        for (UINT y = 0; y < s->h; y++) fwrite((BYTE*)ms.pData + (size_t)y * ms.RowPitch, 4, s->w, f);
        fclose(f);
        LOGC("diag: surface %ux%u written to %s", s->w, s->h, path);
    }
    ctx->Unmap(st.Get(), 0);
}

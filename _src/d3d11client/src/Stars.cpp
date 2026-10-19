// The stars: Orbiter's own catalogue (Config\CSphere\Data\star.bin) through its CelestialSphere loader, drawn as small
// additive quads behind everything; inside an atmosphere dimmed by the day sky and by the haze's vertical optical depth.
#include "Client.h"
#include "CelSphereAPI.h"

namespace {
class StarLoader : public oapi::CelestialSphere
{
public:
    StarLoader(oapi::GraphicsClient* gc) : oapi::CelestialSphere(gc) {}
    std::vector<StarRenderRec> Get() const { return LoadStars(); }
    bool EclDir2WindowPos(const VECTOR3&, int&, int&) const override { return false; }
};
struct StarGPU { XMFLOAT3 dir; float b; XMFLOAT3 col; float pad; };
}

void D3D11Client::CreateStars()
{
    std::vector<StarGPU> s;
    {
        StarLoader L(this);
        auto recs = L.Get();
        s.reserve(recs.size());
        for (auto& r : recs) s.push_back({ { (float)r.pos.x, (float)r.pos.y, (float)r.pos.z }, (float)r.brightness, { (float)r.col.x, (float)r.col.y, (float)r.col.z }, 0 });
    }
    nStars = (UINT)s.size();
    LOGC("stars: %u", nStars);
    if (!nStars) return;
    D3D11_BUFFER_DESC bd = { (UINT)(s.size() * sizeof(StarGPU)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(StarGPU) };
    D3D11_SUBRESOURCE_DATA sd = { s.data(), 0, 0 };
    ComPtr<ID3D11Buffer> b;
    if (FAILED(dev->CreateBuffer(&bd, &sd, &b))) { nStars = 0; return; }
    dev->CreateShaderResourceView(b.Get(), nullptr, &starSRV);
}

void D3D11Client::RenderStars(const MATRIX3& cr, const XMMATRIX& proj, float visibility)
{
    if (!nStars || visibility < 0.01f) return;
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 vp; XMFLOAT4 misc; } c;
    XMStoreFloat4x4(&c.vp, XMMatrixTranspose(V * proj));
    c.misc = { 2.0f / RW, 2.0f / RH, visibility, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
    ctx->OMSetBlendState(bsAdd.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsStars.Get(), nullptr, 0);
    ctx->PSSetShader(psStars.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->VSSetShaderResources(0, 1, starSRV.GetAddressOf());
    ctx->Draw(nStars * 6, 0);
    ID3D11ShaderResourceView* nul = nullptr; ctx->VSSetShaderResources(0, 1, &nul);
}

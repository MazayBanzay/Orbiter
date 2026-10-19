// Effects hooks (see Fx.h): the atmosphere seen from space (FxAtmo.cpp), waves on liquids, spray and mist from jets.
#include "Fx.h"
#include "FxInternal.h"

ComPtr<ID3DBlob> FxCompile(const char* file, const char* entry, const char* target)
{
    std::string path = std::string("Modules\\D3D11Client\\") + file;
    std::wstring wp(path.begin(), path.end());
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (err) LOGC("shader %s %s: %s", path.c_str(), entry, (const char*)err->GetBufferPointer());
    if (FAILED(hr)) return nullptr;
    return code;
}

static bool atmoOk = false;

void FxInit(D3D11Client* gc)
{
    atmoOk = FxAtmoInit(gc);
    LOGC("effects: atmosphere from space %s", atmoOk ? "on" : "FAILED");
}

void FxFrame(D3D11Client*, const VECTOR3&, const MATRIX3&, const XMMATRIX&, double) {}

void FxRenderTransparent(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj)
{
    if (atmoOk) FxAtmoRender(gc, camPos, camRot, proj);
}

void FxRelease()
{
    FxAtmoRelease();
    atmoOk = false;
}

// D3D11Client: compiled shaders kept on disk (Modules\D3D11Client\shadercache). The key is a hash of the source with
// every file it #includes (read again each start: an edited include recompiles what uses it), the entry, the target,
// the flags and the defines; a hit loads the bytecode instead of compiling (the HLSL compiler took ~30 s a start).
#include "Client.h"
#include <set>

namespace {
void Fnv(uint64_t& h, const void* p, size_t n)
{
    const BYTE* b = (const BYTE*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
}

// the file's bytes and, recursively, those of the files it includes ("..." relative to its folder)
bool HashSource(const std::wstring& path, uint64_t& h, std::set<std::wstring>& seen, int depth)
{
    if (depth > 16 || !seen.insert(path).second) return true;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return depth > 0;
    std::string s; char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    Fnv(h, s.data(), s.size());
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/") + 1);
    for (size_t p = s.find("#include"); p != std::string::npos; p = s.find("#include", p + 8)) {
        const size_t q0 = s.find('"', p), nl = s.find('\n', p);
        if (q0 == std::string::npos || (nl != std::string::npos && q0 > nl)) continue;
        const size_t q1 = s.find('"', q0 + 1);
        if (q1 == std::string::npos) continue;
        const std::string inc = s.substr(q0 + 1, q1 - q0 - 1);
        if (!HashSource(dir + std::wstring(inc.begin(), inc.end()), h, seen, depth + 1)) return false;
    }
    return true;
}
}

HRESULT CachedCompileFromFile(const wchar_t* path, const D3D_SHADER_MACRO* defs, const char* entry, const char* target,
                              UINT flags, ID3DBlob** code, ID3DBlob** err)
{
    uint64_t h = 1469598103934665603ull;
    std::set<std::wstring> seen;
    const bool keyed = HashSource(path, h, seen, 0);
    Fnv(h, entry, strlen(entry)); Fnv(h, target, strlen(target)); Fnv(h, &flags, sizeof flags);
    for (const D3D_SHADER_MACRO* d = defs; d && d->Name; d++) { Fnv(h, d->Name, strlen(d->Name)); if (d->Definition) Fnv(h, d->Definition, strlen(d->Definition)); }
    wchar_t cpath[MAX_PATH];
    swprintf_s(cpath, L"Modules\\D3D11Client\\shadercache\\%016llx.cso", (unsigned long long)h);
    if (keyed && SUCCEEDED(D3DReadFileToBlob(cpath, code)) && *code && (*code)->GetBufferSize() > 0) {
        if (err) *err = nullptr;
        return S_OK;
    }
    HRESULT hr = D3DCompileFromFile(path, defs, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, flags, 0, code, err);
    if (SUCCEEDED(hr) && keyed && *code) {
        CreateDirectoryA("Modules\\D3D11Client\\shadercache", nullptr);
        D3DWriteBlobToFile(*code, cpath, TRUE);
    }
    return hr;
}
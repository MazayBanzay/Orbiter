// Planet surfaces: Orbiter's tile tree (level 4 = two hemispheres, each level splits a tile in four), textures from
// loose folders Textures\<planet>\Surf\<lvl>\<ilat>\<ilng>.dds or Textures\<planet>\Archive\Surf.tree, elevation from
// Elev (*.elv, 259x259 with a one-node border) the same ways. A tile without its own data takes its parent's texture
// (a sub-rectangle) and elevation (interpolated). Tiles load on the loader thread (D3D11 devices are free-threaded).
#include "Client.h"
#include <functional>
#include <DirectXPackedVector.h>

static const double PI_ = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------ .tree archives
bool TreeArchive::Open(const std::string& path)
{
    f = fopen(path.c_str(), "rb");
    if (!f) return false;
    DWORD hdr[6]; __int64 dlen; DWORD rest[6];
    if (fread(hdr, 4, 4, f) != 4 || hdr[0] != MAKEFOURCC('T', 'X', 1, 0) || hdr[1] != 48) { fclose(f); f = nullptr; return false; }
    fread(&dlen, 8, 1, f);
    fread(rest, 4, 6, f);                     // nodeCount, root1..3, root4[2]
    dataOfs = hdr[3]; dataLen = dlen;
    DWORD n = rest[0];
    root[0] = rest[1]; root[1] = rest[2]; root[2] = rest[3]; root[3] = rest[4]; root[4] = rest[5];
    toc.resize(n);
    for (DWORD i = 0; i < n; i++) {
        BYTE b[32];
        if (fread(b, 32, 1, f) != 1) { toc.clear(); return false; }
        memcpy(&toc[i].pos, b, 8); memcpy(&toc[i].size, b + 8, 4); memcpy(toc[i].child, b + 12, 16);
    }
    return true;
}

DWORD TreeArchive::Index(int lvl, int ilat, int ilng)
{
    if (lvl < 4) return lvl >= 1 ? root[lvl - 1] : (DWORD)-1;
    if (lvl == 4) return (ilng & 1) ? root[4] : root[3];
    DWORD p = Index(lvl - 1, ilat / 2, ilng / 2);
    if (p == (DWORD)-1 || p >= toc.size()) return (DWORD)-1;
    return toc[p].child[((ilat & 1) << 1) | (ilng & 1)];
}

bool TreeArchive::Read(int lvl, int ilat, int ilng, std::vector<BYTE>& out)
{
    std::vector<BYTE> z; DWORD size = 0;
    {   // the file is shared: only the read is locked, the loader threads inflate side by side
        std::lock_guard<std::mutex> lk(m);
        DWORD i = Index(lvl, ilat, ilng);
        if (i == (DWORD)-1 || i >= toc.size() || !toc[i].size) return false;
        __int64 end = i + 1 < toc.size() ? toc[i + 1].pos : dataLen;
        DWORD zs = (DWORD)(end - toc[i].pos);
        z.resize(zs);
        _fseeki64(f, toc[i].pos + dataOfs, SEEK_SET);
        if (fread(z.data(), 1, zs, f) != zs) return false;
        size = toc[i].size;
    }
    out.resize(size);
    return oapiInflate(z.data(), (DWORD)z.size(), out.data(), size) > 0;
}

// ------------------------------------------------------------------------------------------ planet
Planet::Planet(OBJHANDLE h) : hObj(h)
{
    char nm[256]; oapiGetObjectName(h, nm, 256);
    name = nm;
    R = oapiGetSize(h);
    texDir = "Textures\\" + name;
    surfTree.reset(new TreeArchive); if (!surfTree->Open(texDir + "\\Archive\\Surf.tree")) surfTree.reset();
    elevTree.reset(new TreeArchive); if (!elevTree->Open(texDir + "\\Archive\\Elev.tree")) elevTree.reset();
    const int* ml = (const int*)oapiGetObjectParam(h, OBJPRM_PLANET_SURFACEMAXLEVEL);
    if (ml) maxLevel = std::clamp(*ml, 4, 21);
    for (int k = 0; k < 2; k++) {
        Tile* t = new Tile; t->lvl = 4; t->ilat = 0; t->ilng = k;
        t->maxlat = PI_ / 2; t->minlat = -PI_ / 2; t->minlng = -PI_ + PI_ * k; t->maxlng = t->minlng + PI_;
        root[k] = t;
    }
    // the atmosphere: our own file first, else from Orbiter's constants and the haze colour
    if (oapiPlanetHasAtmosphere(h)) {
        const ATMCONST* ac = oapiGetPlanetAtmConstants(h);
        atm.has = true;
        if (ac) {
            double g = 6.67430e-11 * oapiGetMass(h) / (R * R);
            atm.rho0 = ac->rho0;
            atm.scaleH = ac->rho0 > 0 ? ac->p0 / (ac->rho0 * g) : 8000;
            atm.top = ac->altlimit > 0 ? ac->altlimit : 10 * atm.scaleH;
        }
        const VECTOR3* hc = (const VECTOR3*)oapiGetObjectParam(h, OBJPRM_PLANET_HAZECOLOUR);
        VECTOR3 c = hc ? *hc : _V(0.6, 0.7, 0.9);
        auto lin = [](double v) { return pow(std::max(v, 0.0), 2.2); };
        atm.horizon = _V(lin(c.x), lin(c.y), lin(c.z));
        atm.zenith = atm.horizon * 0.45;
        atm.ambient = atm.horizon * 0.4;
        atm.sun = _V(1, 1, 1);
        atm.hazeExt = 6.5e-5 * atm.rho0 / 1.2;      // Earth: visibility ~60 km at 1.2 kg/m3
        FILE* f = fopen(("Config\\D3D11Client\\" + name + ".cfg").c_str(), "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                char key[64]; double a, b, c3;
                char* sc = strchr(line, ';'); if (sc) *sc = 0;
                int n = sscanf(line, " %63[A-Za-z0-9] = %lf %lf %lf", key, &a, &b, &c3);
                if (n < 2) continue;
                std::string k = key;
                VECTOR3 v = _V(a, n > 2 ? b : a, n > 3 ? c3 : a);
                if (k == "SkyHorizon") atm.horizon = v; else if (k == "SkyZenith") atm.zenith = v;
                else if (k == "Ambient") atm.ambient = v; else if (k == "Sun") atm.sun = v;
                else if (k == "HazeRGB") atm.hazeRGB = v; else if (k == "HazeExt") atm.hazeExt = a;
                else if (k == "ScaleHeight") atm.scaleH = a; else if (k == "Top") atm.top = a;
            }
            fclose(f);
        }
    }
    // a liquid: AtmoFX keys in Config\GC\<planet>.atm.cfg (the fork's), then our own file may override
    auto readLiquid = [&](const std::string& path, const char* prefix) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return;
        char line[256];
        size_t pl = strlen(prefix);
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char key[64]; double a, b, c3;
            int n = sscanf(line, " %63[A-Za-z0-9_] = %lf %lf %lf", key, &a, &b, &c3);
            if (n < 2 || strncmp(key, prefix, pl) != 0) continue;
            std::string k = key + pl;
            if (k == "Level") { liq.level = a; liq.has = true; }
            else if (k == "Ext") liq.ext = a;
            else if (k == "Index") liq.index = a;
            else if (k == "Color" || k == "Colour") liq.colour = _V(a, n > 2 ? b : a, n > 3 ? c3 : a);
            else if (k == "Weights") liq.wts = _V(a, n > 2 ? b : a, n > 3 ? c3 : a);
        }
        fclose(f);
    };
    readLiquid("Config\\GC\\" + name + ".atm.cfg", "AtmoFX_Liquid");
    readLiquid("Config\\D3D11Client\\" + name + ".cfg", "Liquid");
    // micro textures: this body's block in Config\MicroTex.cfg (BODY, NORMALS, LEVEL 0..2 <texture> <pixels per metre>)
    if (FILE* f = fopen("Config\\MicroTex.cfg", "r")) {
        char line[512];
        bool mine = false; int found = 0;
        while (fgets(line, sizeof line, f)) {
            char* cm = strstr(line, "//"); if (cm) *cm = 0;
            char kw[32], a[256]; int i; double r;
            if (sscanf(line, " %31s", kw) != 1) continue;
            if (!strcmp(kw, "BODY")) { mine = sscanf(line, " %*s %255s", a) == 1 && !_stricmp(a, name.c_str()); continue; }
            if (!mine) continue;
            if (!strcmp(kw, "NORMALS") && sscanf(line, " %*s %d", &i) == 1) micNormals = i != 0;
            else if (!strcmp(kw, "LEVEL") && sscanf(line, " %*s %d %255s %lf", &i, a, &r) == 3 && i >= 0 && i < 3 && r > 0) {
                mic[i].file = a; mic[i].reso = r; found |= 1 << i;
            }
        }
        fclose(f);
        micCfg = found == 7;
    }
    LOGC("planet %s: R %.0f km, max level %d, archives: surf %s, elev %s%s%s%s", name.c_str(), R / 1000, maxLevel,
         surfTree ? "yes" : "no", elevTree ? "yes" : "no", atm.has ? ", atmosphere" : "", liq.has ? ", liquid" : "",
         micCfg ? ", micro textures" : "");
}

Planet::~Planet()
{
    if (micThread.joinable()) micThread.join();
    for (Tile* t : root) FreeTile(t);
}

// ------------------------------------------------------------------------------------------ micro textures
// The mean texel of a block-compressed DDS (BC1/BC3), from its first mip of 64x64 or less: the shader makes the detail
// relative to it, so the ground keeps its average colour. Linear when the texture is sampled as sRGB.
static XMFLOAT4 DdsMean(const std::vector<BYTE>& f, bool srgb)
{
    XMFLOAT4 r = { 0.5f, 0.5f, 0.5f, 0.5f };
    if (f.size() < 148 || memcmp(f.data(), "DDS ", 4) != 0) return r;
    auto U32 = [&](size_t o) { uint32_t v; memcpy(&v, f.data() + o, 4); return v; };
    auto U16 = [&](size_t o) { uint16_t v; memcpy(&v, f.data() + o, 2); return (uint32_t)v; };
    UINT H = U32(12), W = U32(16), mips = (U32(8) & 0x20000) && U32(28) ? U32(28) : 1;
    uint32_t fcc = U32(84);
    size_t off = 128; int bc = 0;
    if (U32(80) & 4) {
        if (fcc == MAKEFOURCC('D', 'X', 'T', '1')) bc = 1;
        else if (fcc == MAKEFOURCC('D', 'X', 'T', '4') || fcc == MAKEFOURCC('D', 'X', 'T', '5')) bc = 3;
        else if (fcc == MAKEFOURCC('D', 'X', '1', '0')) {
            uint32_t x = U32(128); off = 148;
            if (x == DXGI_FORMAT_BC1_UNORM || x == DXGI_FORMAT_BC1_UNORM_SRGB) bc = 1;
            else if (x == DXGI_FORMAT_BC3_UNORM || x == DXGI_FORMAT_BC3_UNORM_SRGB) bc = 3;
        }
    }
    if (!bc) return r;
    const size_t bb = bc == 1 ? 8 : 16;
    auto lin = [&](double c) { return srgb ? (c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)) : c; };
    UINT w = W, h = H;
    for (UINT m = 0; m < mips; m++) {
        size_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4), sz = bw * bh * bb;
        if (off + sz > f.size()) break;
        if (m + 1 < mips && (w > 64 || h > 64)) { off += sz; w = std::max(1u, w / 2); h = std::max(1u, h / 2); continue; }
        double sum[4] = {}; size_t n = 0;
        for (size_t k = 0; k < bw * bh; k++) {
            size_t o = off + k * bb;
            double al[8] = { 1, 1, 1, 1, 1, 1, 1, 1 }; uint64_t ai = 0;
            if (bc == 3) {
                double a0 = f[o] / 255.0, a1 = f[o + 1] / 255.0;
                al[0] = a0; al[1] = a1;
                if (f[o] > f[o + 1]) for (int j = 1; j < 7; j++) al[j + 1] = ((7 - j) * a0 + j * a1) / 7;
                else { for (int j = 1; j < 5; j++) al[j + 1] = ((5 - j) * a0 + j * a1) / 5; al[6] = 0; al[7] = 1; }
                for (int j = 0; j < 6; j++) ai |= (uint64_t)f[o + 2 + j] << (8 * j);
                o += 8;
            }
            uint32_t c0 = U16(o), c1 = U16(o + 2), idx = U32(o + 4);
            double p[4][4];
            auto unpack = [](uint32_t c, double* d) { d[0] = ((c >> 11) & 31) / 31.0; d[1] = ((c >> 5) & 63) / 63.0; d[2] = (c & 31) / 31.0; d[3] = 1; };
            unpack(c0, p[0]); unpack(c1, p[1]);
            bool four = bc == 3 || c0 > c1;
            for (int ch = 0; ch < 3; ch++) {
                p[2][ch] = four ? (2 * p[0][ch] + p[1][ch]) / 3 : (p[0][ch] + p[1][ch]) / 2;
                p[3][ch] = four ? (p[0][ch] + 2 * p[1][ch]) / 3 : 0;
            }
            p[2][3] = 1; p[3][3] = four ? 1 : 0;
            for (int t = 0; t < 16; t++) {
                const double* c = p[(idx >> (2 * t)) & 3];
                for (int ch = 0; ch < 3; ch++) sum[ch] += lin(c[ch]);
                sum[3] += bc == 3 ? al[(ai >> (3 * t)) & 7] : c[3];
                n++;
            }
        }
        if (n) r = { (float)(sum[0] / n), (float)(sum[1] / n), (float)(sum[2] / n), (float)(sum[3] / n) };
        break;
    }
    return r;
}

// a relief height layer: DDS with a DX10 header, R16G16B16A16_UNORM, its mip chain stored (export_titan_heights.py)
static ComPtr<ID3D11ShaderResourceView> LoadHeight16(ID3D11Device* dev, const std::vector<BYTE>& f)
{
    if (f.size() < 148 || memcmp(f.data(), "DDS ", 4) != 0) return nullptr;
    auto U32 = [&](size_t o) { uint32_t v; memcpy(&v, f.data() + o, 4); return v; };
    DXGI_FORMAT fmt = (DXGI_FORMAT)U32(128);
    if (U32(84) != MAKEFOURCC('D', 'X', '1', '0') || (fmt != DXGI_FORMAT_R16G16B16A16_UNORM && fmt != DXGI_FORMAT_R8G8B8A8_UNORM)) return nullptr;
    const UINT bpp = fmt == DXGI_FORMAT_R8G8B8A8_UNORM ? 4 : 8;
    UINT H = U32(12), W = U32(16), mips = std::max(1u, U32(28));
    std::vector<D3D11_SUBRESOURCE_DATA> sd;
    size_t o = 148; UINT w = W, h = H;
    for (UINT m = 0; m < mips; m++) {
        size_t sz = (size_t)w * h * bpp;
        if (o + sz > f.size()) return nullptr;
        sd.push_back({ f.data() + o, w * bpp, 0 });
        o += sz; w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    D3D11_TEXTURE2D_DESC td = { W, H, mips, 1, fmt, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(dev->CreateTexture2D(&td, sd.data(), &tex))) return nullptr;
    dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    return srv;
}

// ground classes of a detailed site (gen_titan_ground.py): a header cfg and one byte per cell (north to south rows,
// west to east columns, square cells around CenterLat/Lon). Made into an RGBA8 weights texture with its mips, sand,
// cobbles, ice bedrock, wet margin in r, g, b, a (liquid counts as sand: nothing of the relief is drawn under it)
void Planet::LoadGround()
{
    std::string base = "Config\\OrbiterCrew\\Ground\\" + name;
    FILE* f = fopen((base + ".cfg").c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* sc = strchr(line, ';'); if (sc) *sc = 0;
        char k[64]; double v;
        if (sscanf(line, " %63[A-Za-z] = %lf", k, &v) != 2) continue;
        if (!strcmp(k, "CenterLat")) gLat = v * PI_ / 180; else if (!strcmp(k, "CenterLon")) gLon = v * PI_ / 180;
        else if (!strcmp(k, "StepM")) gStep = v; else if (!strcmp(k, "W")) gW = (int)v; else if (!strcmp(k, "H")) gH = (int)v;
    }
    fclose(f);
    std::vector<BYTE> b;
    if (gW <= 0 || gH <= 0 || gStep <= 0 || !ReadFileBytes((base + ".bin").c_str(), b) || b.size() < (size_t)gW * gH) { gW = 0; return; }
    std::vector<std::vector<BYTE>> lv(1, std::vector<BYTE>((size_t)gW * gH * 4, 0));
    for (size_t i = 0; i < (size_t)gW * gH; i++) {
        int c = b[i]; int ch = c == 1 ? 1 : c == 2 ? 2 : c == 3 ? 3 : 0;
        lv[0][i * 4 + ch] = 255;
    }
    int w = gW, h = gH;
    while (w > 1 || h > 1) {
        int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
        std::vector<BYTE> n((size_t)w2 * h2 * 4);
        const std::vector<BYTE>& s = lv.back();
        for (int y = 0; y < h2; y++) for (int x = 0; x < w2; x++) for (int c = 0; c < 4; c++) {
            int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
            n[((size_t)y * w2 + x) * 4 + c] = (BYTE)((s[((size_t)y0 * w + x0) * 4 + c] + s[((size_t)y0 * w + x1) * 4 + c] +
                                                      s[((size_t)y1 * w + x0) * 4 + c] + s[((size_t)y1 * w + x1) * 4 + c] + 2) / 4);
        }
        lv.push_back(std::move(n)); w = w2; h = h2;
    }
    std::vector<D3D11_SUBRESOURCE_DATA> sd;
    w = gW;
    for (auto& l : lv) { sd.push_back({ l.data(), (UINT)w * 4, 0 }); w = std::max(1, w / 2); }
    D3D11_TEXTURE2D_DESC td = { (UINT)gW, (UINT)gH, (UINT)lv.size(), 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(g_client->Dev()->CreateTexture2D(&td, sd.data(), &tex))) { gW = 0; return; }
    g_client->Dev()->CreateShaderResourceView(tex.Get(), nullptr, &groundTex);
    LOGC("planet %s: ground classes %dx%d cells of %.0f m around %.4f %.4f", name.c_str(), gW, gH, gStep, gLat * 180 / PI_, gLon * 180 / PI_);
}

// the micro loader thread's work: the three levels (sRGB colour, or linear when they carry normals in .ag)
void Planet::MicroLoad()
{
    ID3D11Device* dev = g_client->Dev();
    int ok = 0;
    for (MicroLevel& l : mic) {
        std::vector<BYTE> b;
        std::string p = "Textures\\" + l.file;
        UINT w = 0, h = 0;
        if (ReadFileBytes(p.c_str(), b)) l.srv = LoadDDSMemory(dev, b.data(), b.size(), !micNormals, &w, &h);
        if (!l.srv || !w) { LOGC("planet %s: micro texture %s not loaded", name.c_str(), p.c_str()); continue; }
        l.L = w / l.reso;
        l.mean = DdsMean(b, !micNormals);
        LOGC("planet %s: micro texture %s, %u px, repeats every %.1f m, mean %.3f %.3f %.3f %.3f", name.c_str(), l.file.c_str(),
             w, l.L, l.mean.x, l.mean.y, l.mean.z, l.mean.w);
        ok++;
    }
    micState = ok == 3 ? 2 : 3;
}

void Planet::FreeTile(Tile* t)
{
    if (!t) return;
    for (Tile*& c : t->child) { FreeTile(c); c = nullptr; }
    delete t;
}

bool Planet::FileExists(const std::string& p)
{
    std::lock_guard<std::mutex> lk(fileMx);
    auto it = fileCache.find(p);
    if (it != fileCache.end()) return it->second;
    bool e = GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    fileCache[p] = e;
    return e;
}

std::string Planet::LoosePath(const char* layer, int lvl, int ilat, int ilng, const char* ext)
{
    char b[MAX_PATH];
    sprintf_s(b, "%s\\%s\\%02d\\%06d\\%06d%s", texDir.c_str(), layer, lvl, ilat, ilng, ext);
    return b;
}

bool Planet::HasSurf(int lvl, int ilat, int ilng)
{
    if (FileExists(LoosePath("Surf", lvl, ilat, ilng, ".dds"))) return true;
    return surfTree && surfTree->Has(lvl, ilat, ilng);
}

bool Planet::HasElev(int lvl, int ilat, int ilng)
{
    if (FileExists(LoosePath("Elev", lvl, ilat, ilng, ".elv"))) return true;
    return elevTree && elevTree->Has(lvl, ilat, ilng);
}

Tile* Planet::Child(Tile* t, int k)
{
    if (t->child[k]) return t->child[k];
    Tile* c = new Tile;
    c->lvl = t->lvl + 1;
    c->ilat = t->ilat * 2 + (k >> 1);
    c->ilng = t->ilng * 2 + (k & 1);
    c->parent = t;
    double mlat = 0.5 * (t->minlat + t->maxlat), mlng = 0.5 * (t->minlng + t->maxlng);
    if (k >> 1) { c->minlat = t->minlat; c->maxlat = mlat; } else { c->minlat = mlat; c->maxlat = t->maxlat; }   // ilat grows southwards
    if (k & 1) { c->minlng = mlng; c->maxlng = t->maxlng; } else { c->minlng = t->minlng; c->maxlng = mlng; }
    t->child[k] = c;
    return c;
}

// ---- elevation tile: header (scale, offset, data type) + 259x259 samples, row 0 = south
static bool ParseElev(const std::vector<BYTE>& b, std::vector<float>& out)
{
    if (b.size() < 100 || memcmp(b.data(), "ELE\x01", 4) != 0) return false;
    int hdrsize, dtype; double scale, offset;
    memcpy(&hdrsize, b.data() + 4, 4); memcpy(&dtype, b.data() + 8, 4);
    memcpy(&scale, b.data() + 28, 8); memcpy(&offset, b.data() + 36, 8);
    const size_t n = 259 * 259;
    out.assign(n, (float)offset);
    const BYTE* p = b.data() + hdrsize;
    if (dtype == -16 && b.size() >= hdrsize + n * 2) { const INT16* e = (const INT16*)p; for (size_t i = 0; i < n; i++) out[i] = (float)(e[i] * scale + offset); }
    else if (dtype == 8 && b.size() >= hdrsize + n) { for (size_t i = 0; i < n; i++) out[i] = (float)(p[i] * scale + offset); }
    return true;
}

// ---- the coast as a signed distance field (the liquid's shore, planet.hlsl): from the masks' alpha, this tile's and its
// eight neighbours' (the field runs on across the tile edges, no seams), + on land, - in the liquid, in mask texels

// the alpha of a DXT5 DDS's first mip
static bool DecodeBC3Alpha(const std::vector<BYTE>& b, int& w, int& h, std::vector<uint8_t>& a)
{
    if (b.size() < 128 || memcmp(b.data(), "DDS ", 4) || memcmp(b.data() + 84, "DXT5", 4)) return false;
    memcpy(&h, b.data() + 12, 4); memcpy(&w, b.data() + 16, 4);
    if (w <= 0 || h <= 0 || (w & 3) || (h & 3) || b.size() < 128 + (size_t)(w / 4) * (h / 4) * 16) return false;
    a.assign((size_t)w * h, 0);
    const BYTE* p = b.data() + 128;
    for (int by = 0; by < h / 4; by++) for (int bx = 0; bx < w / 4; bx++, p += 16) {
        int a0 = p[0], a1 = p[1]; uint8_t pal[8] = { (uint8_t)a0, (uint8_t)a1 };
        if (a0 > a1) for (int k = 1; k < 7; k++) pal[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
        else { for (int k = 1; k < 5; k++) pal[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5); pal[6] = 0; pal[7] = 255; }
        uint64_t bits = 0;
        for (int k = 0; k < 6; k++) bits |= (uint64_t)p[2 + k] << (8 * k);
        for (int k = 0; k < 16; k++) a[(size_t)(by * 4 + k / 4) * w + bx * 4 + (k & 3)] = pal[(bits >> (3 * k)) & 7];
    }
    return true;
}

// squared Euclidean distance transform, one line (Felzenszwalb & Huttenlocher): f in, d out, n samples
static void Edt1(const float* f, float* d, int n, int* v, float* z)
{
    auto S = [&](int q, int p) { return ((f[q] + (float)q * q) - (f[p] + (float)p * p)) / (2.0f * q - 2.0f * p); };
    int k = 0; v[0] = 0; z[0] = -1e30f; z[1] = 1e30f;      // (|s| stays under ~1e20: the first boundary never goes)
    for (int q = 1; q < n; q++) {
        float s = S(q, v[k]);
        while (s <= z[k]) { k--; s = S(q, v[k]); }
        k++; v[k] = q; z[k] = s; z[k + 1] = 1e30f;
    }
    k = 0;
    for (int q = 0; q < n; q++) { while (z[k + 1] < q) k++; float dq = (float)(q - v[k]); d[q] = dq * dq + f[v[k]]; }
}
// the squared distance from each cell of an n x n grid to the nearest cell where feat is set
static void Edt2(const std::vector<uint8_t>& feat, int n, std::vector<float>& out)
{
    std::vector<float> f(n), d(n), z(n + 1); std::vector<int> v(n);
    out.resize((size_t)n * n);
    for (size_t i = 0; i < out.size(); i++) out[i] = feat[i] ? 0.0f : 1e20f;
    for (int x = 0; x < n; x++) {       // columns
        for (int y = 0; y < n; y++) f[y] = out[(size_t)y * n + x];
        Edt1(f.data(), d.data(), n, v.data(), z.data());
        for (int y = 0; y < n; y++) out[(size_t)y * n + x] = d[y];
    }
    for (int y = 0; y < n; y++) {       // rows
        float* row = &out[(size_t)y * n];
        for (int x = 0; x < n; x++) f[x] = row[x];
        Edt1(f.data(), d.data(), n, v.data(), z.data());
        memcpy(row, d.data(), n * sizeof(float));
    }
}

// a mask tile's alpha, decoded once (the neighbours are asked for again by each of theirs)
std::shared_ptr<std::vector<uint8_t>> Planet::MaskAlpha(int lvl, int ilat, int ilng, int& w)
{
    const uint64_t key = ((uint64_t)lvl << 48) | ((uint64_t)(uint32_t)ilat << 24) | (uint32_t)ilng;
    {
        std::lock_guard<std::mutex> lk(sdfMx);
        auto it = maskCache.find(key);
        if (it != maskCache.end()) { w = it->second.second; return it->second.first; }
    }
    std::shared_ptr<std::vector<uint8_t>> a;
    std::vector<BYTE> b; int h = 0; w = 0;
    std::string pm = LoosePath("Mask", lvl, ilat, ilng, ".dds");
    if (FileExists(pm) && ReadFileBytes(pm.c_str(), b)) {
        auto v = std::make_shared<std::vector<uint8_t>>();
        if (DecodeBC3Alpha(b, w, h, *v) && w == h) a = v; else w = 0;
    }
    std::lock_guard<std::mutex> lk(sdfMx);
    if (maskCache.size() > 96) maskCache.clear();      // ~25 MB at 512 x 512: dropped wholesale, the loader refills it
    maskCache[key] = { a, w };
    return a;
}

// the tile's field (null: its mask and the neighbours' are all land or all liquid within reach)
ComPtr<ID3D11ShaderResourceView> Planet::CoastField(Tile* t, int& cls)
{
    int W = 0; cls = 0;
    auto c = MaskAlpha(t->lvl, t->ilat, t->ilng, W);
    if (!c || W < 16) return nullptr;
    const int B = 32, N = W + 2 * B;                    // reach: 32 texels round the tile (16 m at level 18)
    const int nlat = 1 << std::max(t->lvl - 4, 0), nlng = 2 * nlat;
    std::shared_ptr<std::vector<uint8_t>> nb[3][3];
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int la = t->ilat + dy, ln = ((t->ilng + dx) % nlng + nlng) % nlng, w2 = 0;
        if (la < 0 || la >= nlat) continue;
        auto a = (dx || dy) ? MaskAlpha(t->lvl, la, ln, w2) : c;
        if (a && (dx || dy) && w2 != W) a = nullptr;
        nb[dy + 1][dx + 1] = a;
    }
    std::vector<uint8_t> land((size_t)N * N), liq((size_t)N * N);
    bool anyL = false, anyQ = false;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int gx = x - B, gy = y - B;
        int tx = gx < 0 ? 0 : gx >= W ? 2 : 1, ty = gy < 0 ? 0 : gy >= W ? 2 : 1;
        uint8_t a;
        if (nb[ty][tx]) a = (*nb[ty][tx])[(size_t)(gy - (ty - 1) * W) * W + (gx - (tx - 1) * W)];
        else a = (*c)[(size_t)std::clamp(gy, 0, W - 1) * W + std::clamp(gx, 0, W - 1)];   // no neighbour: the edge goes on
        bool l = a >= 128;
        land[(size_t)y * N + x] = l; liq[(size_t)y * N + x] = !l;
        anyL |= l; anyQ |= !l;
    }
    if (!anyL || !anyQ) { cls = anyL ? 1 : -1; return nullptr; }
    std::vector<float> dLand, dLiq;
    Edt2(land, N, dLand); Edt2(liq, N, dLiq);
    std::vector<float> f((size_t)N * N), g((size_t)N * N);
    for (size_t i = 0; i < f.size(); i++) f[i] = std::clamp(0.5f * (sqrtf(dLiq[i]) - sqrtf(dLand[i])), -(float)B, (float)B);   // midway between the last land and the first liquid texel
    // the masks' coast comes in steps of a few metres (drawn from a coarse grid): the field smoothed over ~4 m (three box
    // passes each way, a near-Gaussian) - its zero is a smooth curve through the steps, at every level alike
    const double texM = (t->maxlat - t->minlat) * R / W;
    const int r = std::clamp((int)lround(4.0 / texM), 1, B / 4);
    for (int pass = 0; pass < 6; pass++) {
        const bool rows = pass & 1;
        for (int a = 0; a < N; a++) {
            auto at = [&](int k) -> float& { return rows ? f[(size_t)a * N + k] : f[(size_t)k * N + a]; };
            auto to = [&](int k) -> float& { return rows ? g[(size_t)a * N + k] : g[(size_t)k * N + a]; };
            double sum = 0;
            for (int k = -r; k <= r; k++) sum += at(std::clamp(k, 0, N - 1));
            for (int k = 0; k < N; k++) {
                to(k) = (float)(sum / (2 * r + 1));
                sum += at(std::min(k + r + 1, N - 1)) - at(std::max(k - r, 0));
            }
        }
        f.swap(g);
    }
    std::vector<uint16_t> out((size_t)W * W);
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++)
        out[(size_t)y * W + x] = PackedVector::XMConvertFloatToHalf(f[(size_t)(y + B) * N + (x + B)]);
    D3D11_TEXTURE2D_DESC td = {}; td.Width = td.Height = W; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R16_FLOAT;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = { out.data(), (UINT)(W * 2), 0 };
    ComPtr<ID3D11Texture2D> tx; ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(g_client->Dev()->CreateTexture2D(&td, &sd, &tx)) || FAILED(g_client->Dev()->CreateShaderResourceView(tx.Get(), nullptr, &srv))) return nullptr;
    return srv;
}

// where the tile loads spend their time: each stage's microseconds summed (the loader threads add, the 10 s log line
// takes and clears them) - the surface's read and decode (its mips made here), the mask and the coast's field, the
// elevation, the mesh
static std::atomic<long long> g_ldUs[5];
static std::atomic<int> g_ldN{ 0 };
static double g_ldT0 = 0;
static double NowUs() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
std::string Planet::LoadStats()
{
    const int n = g_ldN.exchange(0);
    long long us[5]; for (int k = 0; k < 5; k++) us[k] = g_ldUs[k].exchange(0);
    const double now = NowUs(), span = g_ldT0 > 0 ? (now - g_ldT0) * 1e-6 : 0; g_ldT0 = now;
    if (!n) return "";
    char b[256];
    sprintf_s(b, "tile loads: %d in %.0f s (%.1f/s) | ms each: surface read %.1f, decode + mips %.1f, mask + coast %.1f, elevation %.1f, mesh %.1f",
              n, span, span > 0 ? n / span : 0.0, us[0] / 1e3 / n, us[1] / 1e3 / n, us[2] / 1e3 / n, us[3] / 1e3 / n, us[4] / 1e3 / n);
    return b;
}

void Planet::Load(Tile* t)
{
    ID3D11Device* dev = g_client->Dev();
    std::vector<BYTE> b;
    double tk = NowUs();
    auto stage = [&](int k) { double n = NowUs(); g_ldUs[k] += (long long)(n - tk); tk = n; };
    // surface texture
    std::string ps = LoosePath("Surf", t->lvl, t->ilat, t->ilng, ".dds");
    bool got = (FileExists(ps) && ReadFileBytes(ps.c_str(), b)) || (surfTree && surfTree->Read(t->lvl, t->ilat, t->ilng, b));
    stage(0);
    if (got) t->tex = LoadDDSMemory(dev, b.data(), b.size(), true);
    stage(1);
    // the liquid mask (bodies with a liquid): the coastline per pixel, as the D3D9 client draws it
    if (liq.has) {
        std::string pm = LoosePath("Mask", t->lvl, t->ilat, t->ilng, ".dds");
        if (FileExists(pm) && ReadFileBytes(pm.c_str(), b)) t->mask = LoadDDSMemory(dev, b.data(), b.size(), false);
        if (t->mask && t->lvl >= 14) {      // (coarser tiles are never drawn within the coast's 300 m: no field for them)
            int w = 0;
            MaskAlpha(t->lvl, t->ilat, t->ilng, w);
            int cls = 0;
            if (w > 0 && (t->sdf = CoastField(t, cls))) t->sdfTexel = (float)((t->maxlat - t->minlat) * R / w);
            t->maskClass = cls;
        }
    }
    stage(2);
    // elevation: own, else the parent's quadrant interpolated
    auto e = std::make_shared<std::vector<float>>();
    bool own = false;
    std::string pe = LoosePath("Elev", t->lvl, t->ilat, t->ilng, ".elv");
    if (FileExists(pe) && ReadFileBytes(pe.c_str(), b)) own = ParseElev(b, *e);
    else if (elevTree && elevTree->Read(t->lvl, t->ilat, t->ilng, b)) own = ParseElev(b, *e);
    if (!own) {
        Tile* p = t->parent;
        if (p && p->elev) {
            const std::vector<float>& pe2 = *p->elev;
            int a = (t->ilat & 1), c = (t->ilng & 1);     // a 0: northern half (upper rows), c 0: western half
            double r0 = a ? 0.0 : 0.5, c0 = c ? 0.5 : 0.0;
            e->resize(259 * 259);
            for (int i = 0; i < 259; i++) for (int j = 0; j < 259; j++) {
                double pr = 1 + 256.0 * (r0 + 0.5 * (i - 1) / 256.0), pc = 1 + 256.0 * (c0 + 0.5 * (j - 1) / 256.0);
                pr = std::clamp(pr, 0.0, 257.999); pc = std::clamp(pc, 0.0, 257.999);
                int i0 = (int)pr, j0 = (int)pc; double fr = pr - i0, fc = pc - j0;
                double v = (pe2[i0 * 259 + j0] * (1 - fc) + pe2[i0 * 259 + j0 + 1] * fc) * (1 - fr) +
                           (pe2[(i0 + 1) * 259 + j0] * (1 - fc) + pe2[(i0 + 1) * 259 + j0 + 1] * fc) * fr;
                (*e)[i * 259 + j] = (float)v;
            }
        } else e.reset();
    }
    // at the liquid's edge the elevation data steps 0.1 m: on a flat shore each step is a terrace metres wide, and through
    // the clear shallow liquid its edges show as teeth the size of a mesh cell (the mesh takes every 4th sample). There
    // the heights are smoothed by a wide Gaussian (sigma 5 samples, ~10 m at level 18: wider than a terrace) within 1 m of
    // the level, moving no sample more than 6 cm (units standing there stay on the physics height within that). Towards
    // the tile's edges the smoothing fades out (edge samples stay the data's: the neighbour tile's mesh meets them)
    if (own && e && liq.has) {
        const std::vector<float>& src = *e;
        bool atLevel = false;
        for (float v : src) if (fabs(v - liq.level) < 1.0) { atLevel = true; break; }
        if (atLevel) {
            const int K = 12;
            float wk[2 * K + 1], ws = 0;
            for (int k = -K; k <= K; k++) { wk[k + K] = (float)exp(-0.5 * k * k / 25.0); ws += wk[k + K]; }
            for (float& w : wk) w /= ws;
            std::vector<float> tmp(259 * 259), sm(src);
            for (int r = 0; r < 259; r++) for (int c = 0; c < 259; c++) {       // along the rows
                float acc = 0;
                for (int k = -K; k <= K; k++) acc += wk[k + K] * src[r * 259 + std::clamp(c + k, 0, 258)];
                tmp[r * 259 + c] = acc;
            }
            for (int r = 1; r < 258; r++) for (int c = 1; c < 258; c++) {       // down the columns, blended in
                float v0 = src[r * 259 + c];
                float dl = (float)fabs(v0 - liq.level);
                if (dl >= 1.0f) continue;
                float acc = 0;
                for (int k = -K; k <= K; k++) acc += wk[k + K] * tmp[std::clamp(r + k, 0, 258) * 259 + c];
                int edge = std::min(std::min(r - 1, c - 1), std::min(257 - r, 257 - c));
                float f = std::clamp(edge / 8.0f, 0.0f, 1.0f) * std::clamp((1.0f - dl) / 0.3f, 0.0f, 1.0f);
                sm[r * 259 + c] = v0 + f * std::clamp(acc - v0, -0.06f, 0.06f);
            }
            *e = std::move(sm);
        }
    }
    t->elev = e; t->ownElev = own;
    t->elevLvl = own ? t->lvl : (t->parent ? t->parent->elevLvl : 0);
    stage(3);

    // the mesh: 65x65 nodes from the elevation (every 4th sample), positions relative to the tile centre
    const int G = 64;
    double mlat = 0.5 * (t->minlat + t->maxlat), mlng = 0.5 * (t->minlng + t->maxlng);
    auto H = [&](int i, int j) -> double {     // node (i, j) on the 65 grid, may step one node outside
        if (!t->elev) return 0.0;
        int r = std::clamp(1 + i * 4, 0, 258), c = std::clamp(1 + j * 4, 0, 258);
        return (*t->elev)[r * 259 + c];
    };
    // the tile's centre at the height of its middle (at the mean radius a tile on ground 600 m below it got a 600 m
    // bounding sphere: every tile within that range counted as under the camera and split)
    double hmid = 0.125 * (H(0, 0) + H(0, G) + H(G, 0) + H(G, G)) + 0.5 * H(G / 2, G / 2);
    t->centre = _V(cos(mlat) * cos(mlng), sin(mlat), cos(mlat) * sin(mlng)) * (R + hmid);
    auto P = [&](int i, int j) -> VECTOR3 {
        double lat = t->minlat + (t->maxlat - t->minlat) * i / G, lng = t->minlng + (t->maxlng - t->minlng) * j / G;
        double r = R + H(i, j);
        return _V(r * cos(lat) * cos(lng), r * sin(lat), r * cos(lat) * sin(lng));
    };
    std::vector<TileVertex> v((G + 1) * (G + 1) + 4 * (G + 1));
    double rad = 0, emin = 1e9, emax = -1e9;
    for (int i = 0; i <= G; i++) for (int j = 0; j <= G; j++) {
        VECTOR3 p = P(i, j);
        VECTOR3 dn = P(std::min(i + 1, G + 1), j) - P(std::max(i - 1, -1), j);
        VECTOR3 de = P(i, std::min(j + 1, G + 1)) - P(i, std::max(j - 1, -1));
        VECTOR3 nn = crossp(de, dn);
        VECTOR3 up = p / length(p);
        if (length(nn) < 1e-9) nn = up;
        nn = nn / length(nn);
        if (dotp(nn, up) < 0) nn = -nn;
        VECTOR3 q = p - t->centre;
        rad = std::max(rad, length(q));
        double h = H(i, j); emin = std::min(emin, h); emax = std::max(emax, h);
        TileVertex& tv = v[i * (G + 1) + j];
        tv.p = { (float)q.x, (float)q.y, (float)q.z };
        tv.n = { (float)nn.x, (float)nn.y, (float)nn.z };
        tv.uv = { (float)j / G, 1.0f - (float)i / G };
    }
    // skirts hang below the edges (they hide the cracks against coarser neighbours)
    double drop = (t->maxlat - t->minlat) * R * 0.02 + 20.0;
    int base = (G + 1) * (G + 1);
    auto skirt = [&](int dst, int i, int j) {
        TileVertex s = v[i * (G + 1) + j];
        VECTOR3 q = _V(s.p.x, s.p.y, s.p.z) + t->centre;
        q = q * ((length(q) - drop) / length(q)) - t->centre;
        s.p = { (float)q.x, (float)q.y, (float)q.z };
        v[dst] = s;
    };
    for (int k = 0; k <= G; k++) {
        skirt(base + k, 0, k); skirt(base + (G + 1) + k, G, k);
        skirt(base + 2 * (G + 1) + k, k, 0); skirt(base + 3 * (G + 1) + k, k, G);
    }
    t->radius = rad + drop; t->emin = emin; t->emax = emax;
    D3D11_BUFFER_DESC bd = { (UINT)(v.size() * sizeof(TileVertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA sd = { v.data(), 0, 0 };
    ComPtr<ID3D11Buffer> vb;
    if (FAILED(dev->CreateBuffer(&bd, &sd, &vb))) { t->state = 3; return; }
    t->vb = vb;
    tilesLoaded++;
    stage(4); g_ldN++;
    t->state = 2;
}

// ------------------------------------------------------------------------------------------ rendering
struct PlanetCB { XMFLOAT4X4 wvp; XMFLOAT4 sunL, uv, misc; XMFLOAT4X4 w, rot; XMFLOAT4 mic[3], micM[3], micF, micC;
                  XMFLOAT4 gm, el, ground[3], maskUV, shore, shore2; };
static_assert(sizeof(PlanetCB) <= 1024, "cbObj");

void Planet::Render(ID3D11DeviceContext* ctx, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH)
{
    tilesDrawn = 0;
    frame = g_client->frameNo;
    oapiGetGlobalPos(hObj, &Pp);
    oapiGetRotationMatrix(hObj, &Rp);
    camGlobal = camPos; camR = camRot; P = proj;
    VECTOR3 rel = Pp - camPos;
    double d = length(rel);
    if (R / d * screenH / (2 * tanHalfY) < 1.0) return;    // under a pixel
    // the camera and the Sun in the planet frame
    VECTOR3 cpl = tmul(Rp, camPos - Pp);
    VECTOR3 sunL = _V(1, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
        OBJHANDLE h = oapiGetGbodyByIndex(i);
        if (oapiGetObjectType(h) == OBJTP_STAR) { VECTOR3 sp; oapiGetGlobalPos(h, &sp); sunL = tmul(Rp, sp - Pp); sunL = sunL / length(sunL); break; }
    }
    // micro textures: loaded when the camera first comes down to 100 km; each level fades out where its texels shrink
    // to 1/16 pixel (the mip chain has flattened it by then), the coarsest within 25 km
    for (double& fd : micFade) fd = 0;
    if (micCfg && g_cfg.microTex) {
        if (micState == 0 && length(cpl) - R < 100e3) { micState = 1; micThread = std::thread([this] { MicroLoad(); }); }
        if (micState == 2) {
            double pxs = screenH / (2.0 * tanHalfY);
            for (int k = 0; k < 3; k++) micFade[k] = std::min(16.0 * pxs / mic[k].reso, 25e3);
            ID3D11ShaderResourceView* ms[3] = { mic[0].srv.Get(), mic[1].srv.Get(), mic[2].srv.Get() };
            ctx->PSSetShaderResources(1, 3, ms);
        }
    }
    // the ground material near the surface (loaded once, the first time the camera comes down to 30 km)
    if (gmatState == 0 && g_cfg.groundDetail && length(cpl) - R < 30e3) {
        gmatState = 3;
        for (int k = 0; k < 2; k++) {
            std::vector<BYTE> b;
            std::string p = "Textures\\" + name + (k ? "_ground_sand.dds" : "_ground_cobbles.dds");
            if (ReadFileBytes(p.c_str(), b)) gmat[k] = LoadHeight16(g_client->Dev(), b);
        }
        if (gmat[0] && gmat[1]) { gmatState = 2; LoadGround(); LOGC("planet %s: ground material loaded", name.c_str()); }
    }
    if (gmatState == 2) {
        ID3D11ShaderResourceView* gs[3] = { gmat[0].Get(), gmat[1].Get(), groundTex.Get() };
        ctx->PSSetShaderResources(4, 3, gs);
        static ComPtr<ID3D11SamplerState> lin; static ID3D11Device* linDev = nullptr;
        if (linDev != g_client->Dev()) {
            linDev = g_client->Dev(); lin.Reset();
            D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX;
            linDev->CreateSamplerState(&sd, &lin);
        }
        ctx->PSSetSamplers(3, 1, lin.GetAddressOf());
    }
    g_client->Ctx()->IASetInputLayout(g_client->ilPlanet.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetIndexBuffer(g_client->tileIB.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->VSSetShader(g_client->vsPlanet.Get(), nullptr, 0);
    ctx->PSSetShader(g_client->psPlanet.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, g_client->cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, g_client->cbObj.GetAddressOf());
    ctx->RSSetState(g_client->rsNone.Get());     // the skirts face both ways
    ctx->OMSetBlendState(g_client->bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(g_client->dsWrite.Get(), 0);
    for (Tile* t : root) Traverse(t, ctx, cpl, camRot, proj, tanHalfY, screenH, sunL);
    if (frame % 300 == 150 && getenv("D3D11CLIENT_TEST")) ElevCheck(camPos);
}

// how much a tile not loaded yet matters to the loaders: its distance from the camera over its size (the one under the
// camera ~0, the small far ones large) - its centre taken on the reference sphere (the elevation comes with the load)
float Planet::LoadPrio(const Tile* t, const VECTOR3& cpl) const
{
    double mlat = 0.5 * (t->minlat + t->maxlat), mlng = 0.5 * (t->minlng + t->maxlng);
    double edge = (t->maxlat - t->minlat) * R;
    VECTOR3 c = _V(cos(mlat) * cos(mlng), sin(mlat), cos(mlat) * sin(mlng)) * R;
    return (float)(std::max(length(c - cpl) - edge * 0.75, edge * 0.02) / edge);
}

void Planet::Traverse(Tile* t, ID3D11DeviceContext* ctx, const VECTOR3& cpl, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH, const VECTOR3& sunL)
{
    t->lastUsed = frame;
    if (t->state != 2) { t->prio = LoadPrio(t, cpl); if (t->state == 0) g_client->QueueTile(this, t); return; }
    VECTOR3 toTile = t->centre - cpl;
    double dist = length(toTile);
    // horizon: a tile wholly beyond the planet's limb is hidden
    double dc = length(cpl);
    if (t->lvl > 5 && dc > R) {
        double ang = acos(std::clamp(dotp(t->centre / length(t->centre), cpl / dc), -1.0, 1.0));
        double hor = acos(std::clamp((R + std::min(t->emin, 0.0)) / dc, -1.0, 1.0)) + acos(std::clamp(R / (R + std::max(t->emax, 0.0) + 1.0), -1.0, 1.0));
        double half = std::max(t->maxlat - t->minlat, (t->maxlng - t->minlng) * cos(std::min(fabs(t->minlat), fabs(t->maxlat)))) * 0.75;
        if (ang - half > hor) return;
    }
    // view frustum (bounding sphere in camera axes)
    VECTOR3 cg = mul(Rp, toTile);                    // global axes
    VECTOR3 cc = tmul(camRot, cg);                   // camera axes: x right, y up, z forward
    double aspect = (double)XMVectorGetY(P.r[1]) / (double)XMVectorGetX(P.r[0]);
    double ty = tanHalfY, txa = tanHalfY * aspect;
    if (cc.z < -t->radius) return;
    if (cc.x - cc.z * txa > t->radius * sqrt(1 + txa * txa)) return;
    if (-cc.x - cc.z * txa > t->radius * sqrt(1 + txa * txa)) return;
    if (cc.y - cc.z * ty > t->radius * sqrt(1 + ty * ty)) return;
    if (-cc.y - cc.z * ty > t->radius * sqrt(1 + ty * ty)) return;
    // level of detail: the tile's 512 texels against its size on screen
    double edge = (t->maxlat - t->minlat) * R;
    double near_ = std::max(dist - t->radius, edge * 0.02);
    double px = edge / near_ * screenH / (2 * tanHalfY);
    bool split = px > 600.0 * pow(2.0, -g_cfg.lodBias) && t->lvl < maxLevel;
    if (split) {
        bool any = false;
        for (int k = 0; k < 4; k++) {
            int cl = t->lvl + 1, ci = t->ilat * 2 + (k >> 1), cj = t->ilng * 2 + (k & 1);
            if (HasSurf(cl, ci, cj) || HasElev(cl, ci, cj)) { any = true; break; }
        }
        // past the deepest elevation data two more levels: the mesh (every 4th sample) then reaches the data's full
        // resolution, so the ground lies where Orbiter's physics puts it (vessels neither float nor sink)
        split = any || (t->elev && t->lvl < t->elevLvl + 2);
    }
    if (split) {
        bool ready = true;
        for (int k = 0; k < 4; k++) {
            Tile* c = Child(t, k);
            if (c->state != 2) { ready = false; c->prio = LoadPrio(c, cpl); if (c->state == 0) g_client->QueueTile(this, c); }
        }
        if (ready) {
            for (int k = 0; k < 4; k++) Traverse(t->child[k], ctx, cpl, camRot, proj, tanHalfY, screenH, sunL);
            return;
        }
        for (int k = 0; k < 4; k++) t->child[k]->lastUsed = frame;
    }
    Draw(t, ctx, cpl, sunL);
}

double FxTestNum(const char* key);

void Planet::Draw(Tile* t, ID3D11DeviceContext* ctx, const VECTOR3& cpl, const VECTOR3& sunL)
{
    // texture: own, else the nearest ancestor's sub-rectangle
    Tile* src = t;
    while (src && !src->tex) src = src->parent;
    ID3D11ShaderResourceView* srv = src ? src->tex.Get() : g_client->whiteTex.Get();
    XMFLOAT4 uv = { 1, 1, 0, 0 };
    if (src && src != t) {
        double su = (t->maxlng - t->minlng) / (src->maxlng - src->minlng), sv = (t->maxlat - t->minlat) / (src->maxlat - src->minlat);
        double ou = (t->minlng - src->minlng) / (src->maxlng - src->minlng), ov = (src->maxlat - t->maxlat) / (src->maxlat - src->minlat);
        uv = { (float)su, (float)sv, (float)ou, (float)ov };
    }
    // world: planet rotation, translation of the tile centre from the camera (double precision)
    VECTOR3 tc = mul(Rp, t->centre) + Pp - camGlobal;
    XMMATRIX Wm = RotToXM(Rp);
    Wm.r[3] = XMVectorSet((float)tc.x, (float)tc.y, (float)tc.z, 1);
    const MATRIX3& cr = camR;
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    PlanetCB c;
    XMStoreFloat4x4(&c.wvp, XMMatrixTranspose(Wm * V * P));
    c.sunL = { (float)sunL.x, (float)sunL.y, (float)sunL.z, 0 };
    c.uv = uv;
    static const float tileDbg = (float)FxTestNum("TileDbg");     // test runs: the tiles in their levels' colours
    c.misc = { src ? 1.0f : 0.0f, (float)t->lvl, 0, tileDbg };
    XMStoreFloat4x4(&c.w, XMMatrixTranspose(Wm));
    XMMATRIX Rx = RotToXM(Rp);
    XMStoreFloat4x4(&c.rot, XMMatrixTranspose(Rx));
    // micro textures: the tile centre in repeats of each level (double, only the fraction goes to the GPU), so the
    // shader's tile-local position continues the same planet-fixed pattern across all tiles
    double fmax = std::max({ micFade[0], micFade[1], micFade[2] });
    bool mOn = fmax > 0 && length(t->centre - cpl) - t->radius < fmax;
    for (int k = 0; k < 3; k++) {
        double inv = mOn ? 1.0 / mic[k].L : 0;
        VECTOR3 q = t->centre * inv;
        c.mic[k] = { (float)(q.x - floor(q.x)), (float)(q.y - floor(q.y)), (float)(q.z - floor(q.z)), (float)inv };
        c.micM[k] = mic[k].mean;
    }
    c.micF = { (float)micFade[0], (float)micFade[1], (float)micFade[2], mOn ? (micNormals ? 2.0f : 1.0f) : 0.0f };
    c.micC = { (float)t->centre.x, (float)t->centre.y, (float)t->centre.z, 0 };
    // the ground material: 10.24 m repeats fixed to the planet (the tile centre's fraction, the rest is tile-local),
    // within 60 m of the camera; the height above the radius for the liquid; the site's ground classes
    double near_ = length(t->centre - cpl) - t->radius, rc = length(t->centre);
    bool gOn = gmatState == 2 && g_cfg.groundDetail && near_ < 40;
    VECTOR3 gq = t->centre * (1.0 / 10.24);
    c.gm = { (float)(gq.x - floor(gq.x)), (float)(gq.y - floor(gq.y)), (float)(gq.z - floor(gq.z)), gOn ? (float)(1.0 / 10.24) : 0.0f };
    c.el = { (float)(rc - R), (float)rc, (float)liq.level, liq.has ? 1.0f : 0.0f };
    // the liquid mask: own or the nearest ancestor's sub-rectangle
    Tile* msrc = t;
    while (msrc && !msrc->mask) msrc = msrc->parent;
    ID3D11ShaderResourceView* mskv = msrc ? msrc->mask.Get() : nullptr;
    c.maskUV = { 1, 1, 0, 0 };
    if (msrc && msrc != t)
        c.maskUV = { (float)((t->maxlng - t->minlng) / (msrc->maxlng - msrc->minlng)), (float)((t->maxlat - t->minlat) / (msrc->maxlat - msrc->minlat)),
                     (float)((t->minlng - msrc->minlng) / (msrc->maxlng - msrc->minlng)), (float)((msrc->maxlat - t->maxlat) / (msrc->maxlat - msrc->minlat)) };
    c.misc.z = mskv && liq.has ? 1.0f : 0.0f;
    ctx->PSSetShaderResources(15, 1, &mskv);
    // the coast, from the masks' smoothed distance field (within 300 m: farther the coarse masks would move it): the
    // beach laid by the vertex shader, the shore's colours by the pixel shader (planet.hlsl CoastElev, CoastAlbedo)
    ID3D11ShaderResourceView* sdfv = msrc && liq.has && near_ < 300 ? msrc->sdf.Get() : nullptr;
    const double drop = (t->maxlat - t->minlat) * R * 0.02 + 20.0;        // as the mesh's skirts hang (Planet::Load)
    c.shore = { sdfv ? msrc->sdfTexel : 0.0f, 0.04f, (float)drop, 0 };
    c.shore2 = { 0, !sdfv && msrc && liq.has && near_ < 300 ? msrc->maskClass * 1e3f : 0.0f, 0, 0 };   // no field: all land / all liquid
    ctx->VSSetShaderResources(16, 1, &sdfv);
    ctx->PSSetShaderResources(16, 1, &sdfv);
    const bool shore = sdfv != nullptr;
    c.ground[0] = c.ground[1] = c.ground[2] = { 0, 0, 0, 0 };
    if (gOn && groundTex) {
        double lat = asin(t->centre.y / rc), lon = atan2(t->centre.z, t->centre.x);
        const double Rg = 2575e3;
        double dl = lon - gLon; dl -= 2 * PI_ * floor((dl + PI_) / (2 * PI_));
        double gx = Rg * cos(gLat) * dl, gy = Rg * (lat - gLat);
        double ex = Rg * cos(gLat) / (rc * cos(lat)), ny = Rg / rc;
        VECTOR3 e = _V(-sin(lon), 0, cos(lon)) * ex, n = _V(-sin(lat) * cos(lon), cos(lat), -sin(lat) * sin(lon)) * ny;
        c.ground[0] = { (float)gx, (float)gy, (float)(1.0 / (gW * gStep)), (float)(1.0 / (gH * gStep)) };
        c.ground[1] = { (float)e.x, (float)e.y, (float)e.z, 1 };
        c.ground[2] = { (float)n.x, (float)n.y, (float)n.z, 0 };
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(g_client->cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(g_client->cbObj.Get(), 0);
    UINT stride = sizeof(TileVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, t->vb.GetAddressOf(), &stride, &off);
    ctx->PSSetShaderResources(0, 1, &srv);
    // the half-density grid where a fine cell would span under 2.5 pixels at the tile's nearest point (never for the
    // ground under the camera: the ground material, the coastline)
    static const bool fineOnly = getenv("D3D11CLIENT_FINETILES") != nullptr;    // measuring: always the fine grid
    static DWORD vw = 0, vh = 0;
    if (!vh) oapiGetViewportSize(&vw, &vh);
    double cell = (t->maxlat - t->minlat) * R / 64, pxRad = 2 * tan(oapiCameraAperture()) / (double)std::max<DWORD>(vh, 1);
    bool coarse = !fineOnly && !gOn && !shore && cell < 2.5 * pxRad * std::max(near_, 1.0);
    if (coarse) ctx->DrawIndexed(g_client->tileIdxCount2, g_client->tileIdxCount, 0);
    else ctx->DrawIndexed(g_client->tileIdxCount, 0, 0);
    tilesDrawn++;
}

// test runs: the surface under the focus object and the camera as Orbiter's physics has it against our drawn mesh
double Planet::MeshElev(double lng, double lat, int* lvl, double* fine, bool* own)
{
    Tile* t = root[lng < 0 ? 0 : 1];
    if (!t || t->state != 2) return 0;
    for (;;) {
        int k = (lat < 0.5 * (t->minlat + t->maxlat) ? 2 : 0) | (lng < 0.5 * (t->minlng + t->maxlng) ? 0 : 1);
        Tile* c = t->child[k];
        if (!c || c->state != 2 || c->lastUsed != frame) break;
        t = c;
    }
    *lvl = t->lvl; *own = t->ownElev;
    if (!t->elev) { *fine = 0; return 0; }
    const std::vector<float>& e = *t->elev;
    double fi = (lat - t->minlat) / (t->maxlat - t->minlat), fj = (lng - t->minlng) / (t->maxlng - t->minlng);
    auto bil = [&](double r, double c, int step) {
        int r0 = std::clamp((int)r, 0, 256 / step - 1), c0 = std::clamp((int)c, 0, 256 / step - 1);
        double a = r - r0, b = c - c0;
        auto E = [&](int i, int j) { return (double)e[(1 + i * step) * 259 + 1 + j * step]; };
        return (E(r0, c0) * (1 - b) + E(r0, c0 + 1) * b) * (1 - a) + (E(r0 + 1, c0) * (1 - b) + E(r0 + 1, c0 + 1) * b) * a;
    };
    *fine = bil(fi * 256, fj * 256, 1);
    return bil(fi * 64, fj * 64, 4);     // the mesh takes every 4th sample
}

void Planet::ElevCheck(const VECTOR3& camPos)
{
    OBJHANDLE hf = oapiGetFocusObject();
    if (!hf) return;
    VECTOR3 gp; oapiGetGlobalPos(hf, &gp);
    double lng, lat, rad;
    oapiGlobalToEqu(hObj, gp, &lng, &lat, &rad);
    if (rad - R > 50e3) return;
    int lvl = 0; double fine = 0; bool own = false;
    double mesh = MeshElev(lng, lat, &lvl, &fine, &own);
    double phys = oapiSurfaceElevation(hObj, lng, lat);
    double clng, clat, crad; int cl = 0; double cf = 0; bool co = false;
    oapiGlobalToEqu(hObj, camPos, &clng, &clat, &crad);
    double cm = MeshElev(clng, clat, &cl, &cf, &co);
    LOGC("elev check %s: focus at %.5f %.5f, %.2f m over R | physics surface %.2f m | drawn tile lvl %d (%s elev): full-res %.2f m, mesh %.2f m | camera %.2f m over R, %.2f m over our mesh (lvl %d), physics there %.2f m",
         name.c_str(), lng * 180 / PI_, lat * 180 / PI_, rad - R, phys, lvl, own ? "own" : "interpolated", fine, mesh,
         crad - R, crad - R - cm, cl, oapiSurfaceElevation(hObj, clng, clat));
    // along the view, level with the ground: our drawn surface against the physics one
    VECTOR3 cd; oapiCameraGlobalDir(&cd);
    VECTOR3 up = camPos - Pp; up = up / length(up);
    VECTOR3 hd = cd - up * dotp(cd, up);
    if (length(hd) < 1e-6) return;
    hd = hd / length(hd);
    char buf[1024]; int n = sprintf_s(buf, "elev view %s:", name.c_str());
    for (double d : { 1e3, 2e3, 4e3, 8e3, 16e3, 32e3, 64e3 }) {
        double l2, b2, r2; int lv = 0; double fn = 0; bool ow = false;
        oapiGlobalToEqu(hObj, camPos + hd * d, &l2, &b2, &r2);
        double m = MeshElev(l2, b2, &lv, &fn, &ow);
        n += sprintf_s(buf + n, sizeof buf - n, " | %.0f km: phys %.0f, ours %.0f (lvl %d%s)", d / 1000, oapiSurfaceElevation(hObj, l2, b2), m, lv, ow ? "" : " i");
    }
    LOGC("%s", buf);
}

// tiles unused for a while go (never one the loader still holds)
void Planet::Purge(DWORD now)
{
    std::function<bool(Tile*)> busy = [&](Tile* t) -> bool {
        if (!t) return false;
        if (t->state == 1) return true;
        for (Tile* c : t->child) if (busy(c)) return true;
        return false;
    };
    std::function<void(Tile*)> walk = [&](Tile* t) {
        for (Tile*& c : t->child) {
            if (!c) continue;
            if (now - c->lastUsed > 600 && !busy(c)) { FreeTile(c); c = nullptr; }
            else walk(c);
        }
    };
    if ((now & 63) == 0) for (Tile* t : root) walk(t);
}

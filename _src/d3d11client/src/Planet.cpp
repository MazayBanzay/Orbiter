// Planet surfaces: Orbiter's tile tree (level 4 = two hemispheres, each level splits a tile in four), textures from
// loose folders Textures\<planet>\Surf\<lvl>\<ilat>\<ilng>.dds or Textures\<planet>\Archive\Surf.tree, elevation from
// Elev (*.elv, 259x259 with a one-node border) the same ways. A tile without its own data takes its parent's texture
// (a sub-rectangle) and elevation (interpolated). Tiles load on the loader thread (D3D11 devices are free-threaded).
#include "Client.h"
#include <functional>

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
    std::lock_guard<std::mutex> lk(m);
    DWORD i = Index(lvl, ilat, ilng);
    if (i == (DWORD)-1 || i >= toc.size() || !toc[i].size) return false;
    __int64 end = i + 1 < toc.size() ? toc[i + 1].pos : dataLen;
    DWORD zs = (DWORD)(end - toc[i].pos);
    std::vector<BYTE> z(zs);
    _fseeki64(f, toc[i].pos + dataOfs, SEEK_SET);
    if (fread(z.data(), 1, zs, f) != zs) return false;
    out.resize(toc[i].size);
    return oapiInflate(z.data(), zs, out.data(), (DWORD)out.size()) > 0;
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
    LOGC("planet %s: R %.0f km, max level %d, archives: surf %s, elev %s%s%s", name.c_str(), R / 1000, maxLevel,
         surfTree ? "yes" : "no", elevTree ? "yes" : "no", atm.has ? ", atmosphere" : "", liq.has ? ", liquid" : "");
}

Planet::~Planet() { for (Tile* t : root) FreeTile(t); }

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

void Planet::Load(Tile* t)
{
    ID3D11Device* dev = g_client->Dev();
    std::vector<BYTE> b;
    // surface texture
    std::string ps = LoosePath("Surf", t->lvl, t->ilat, t->ilng, ".dds");
    if (FileExists(ps) && ReadFileBytes(ps.c_str(), b)) t->tex = LoadDDSMemory(dev, b.data(), b.size(), true);
    else if (surfTree && surfTree->Read(t->lvl, t->ilat, t->ilng, b)) t->tex = LoadDDSMemory(dev, b.data(), b.size(), true);
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
    t->elev = e; t->ownElev = own;

    // the mesh: 65x65 nodes from the elevation (every 4th sample), positions relative to the tile centre
    const int G = 64;
    double mlat = 0.5 * (t->minlat + t->maxlat), mlng = 0.5 * (t->minlng + t->maxlng);
    t->centre = _V(R * cos(mlat) * cos(mlng), R * sin(mlat), R * cos(mlat) * sin(mlng));
    auto H = [&](int i, int j) -> double {     // node (i, j) on the 65 grid, may step one node outside
        if (!t->elev) return 0.0;
        int r = std::clamp(1 + i * 4, 0, 258), c = std::clamp(1 + j * 4, 0, 258);
        return (*t->elev)[r * 259 + c];
    };
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
    t->state = 2;
}

// ------------------------------------------------------------------------------------------ rendering
struct PlanetCB { XMFLOAT4X4 wvp; XMFLOAT4 sunL, uv, misc; XMFLOAT4X4 w, rot; };

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
}

void Planet::Traverse(Tile* t, ID3D11DeviceContext* ctx, const VECTOR3& cpl, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH, const VECTOR3& sunL)
{
    t->lastUsed = frame;
    if (t->state != 2) { if (t->state == 0) g_client->QueueTile(this, t); return; }
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
        split = any;
    }
    if (split) {
        bool ready = true;
        for (int k = 0; k < 4; k++) { Tile* c = Child(t, k); if (c->state != 2) { ready = false; if (c->state == 0) g_client->QueueTile(this, c); } }
        if (ready) {
            for (int k = 0; k < 4; k++) Traverse(t->child[k], ctx, cpl, camRot, proj, tanHalfY, screenH, sunL);
            return;
        }
        for (int k = 0; k < 4; k++) t->child[k]->lastUsed = frame;
    }
    Draw(t, ctx, cpl, sunL);
}

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
    c.misc = { src ? 1.0f : 0.0f, (float)t->lvl, 0, 0 };
    XMStoreFloat4x4(&c.w, XMMatrixTranspose(Wm));
    XMMATRIX Rx = RotToXM(Rp);
    XMStoreFloat4x4(&c.rot, XMMatrixTranspose(Rx));
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(g_client->cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(g_client->cbObj.Get(), 0);
    UINT stride = sizeof(TileVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, t->vb.GetAddressOf(), &stride, &off);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->DrawIndexed(g_client->tileIdxCount, 0, 0);
    tilesDrawn++;
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

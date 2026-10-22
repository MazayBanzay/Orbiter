// D3D11Client - a Direct3D 11 graphics client for Orbiter 2024 (our own; written from the Orbiter SDK API, not derived
// from D3D9Client). Stage 1-2: device and window, the camera, vessels with their meshes and animations, planets with
// surface tiles and elevation (loose tile folders and .tree archives).
#pragma once
#define STRICT 1

#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "OrbiterAPI.h"
#include "VesselAPI.h"
#include "GraphicsAPI.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <condition_variable>
#include <set>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#define LOGC(fmt, ...) oapiWriteLogV("D3D11Client: " fmt, __VA_ARGS__)

class D3D11Client;
extern D3D11Client* g_client;

// the client's own settings (Launchpad > Video > Advanced; Config\D3D11Client\Client.cfg)
struct ClientCfg
{
    float lodBias = 0, haze = 1, exposure = 1;
    int aniso = 16;
    int aa = 2;          // 0 off, 1-3 MSAA 2/4/8x, 4 FXAA, 5 TAA
    float renderScale = 1;   // TAA only: the scene's resolution / the window's (TAAU upsamples)
    bool liquids = true, shadows = true, statsLog = true;
    bool microTex = false;   // planet micro textures (Config\MicroTex.cfg): close-up surface detail
    bool groundDetail = true;     // ground material from the Huygens data near the camera (normal, parallax, shadows)
    bool tess = false;       // the ground relief's tessellation (the detail is baked into the tiles now)
    bool terrainNormals = true;   // per-pixel normals from the full elevation data (off: the mesh's vertex normals)
    bool geomorph = true;         // terrain LOD levels blend smoothly (CDLOD-style morphing; off: they pop)
    bool driverCacheOff = false; // the driver's disk shader cache kept out of the process (Client.cpp DriverCacheGuardBefore)
    bool driverThreads = true;   // false: the device is made with PREVENT_INTERNAL_THREADING_OPTIMIZATIONS - the driver compiles
                                 // shaders on our thread, one at a time (its own worker threads raced in its compiler on one
                                 // tester's machine, 2026-10-21)
    int driverCacheMB = 600;     // above this the cache is set aside (1 GB ran Orbiter out of memory; a cold cache makes the start slow)
    bool ao = false;              // ambient occlusion (Ssao.cpp, GTAO: the sky's light only)
    bool visorDrops = true;       // drops on the suits' visors (VisorDrops.cpp)
    bool bloom = true;            // the bright parts glow (Bloom.cpp)
    float sharpen = 0.0f;         // adaptive sharpening after the tone curve (post.hlsl), 0 off .. 1 (MSAA needs none: it lifted the fabric grain)      // the video driver's shader cache file for Orbiter: set aside at start when bigger (0: never)
    void Load();
    void Save() const;
};
extern ClientCfg g_cfg;

// what our modules hand over through the AtmoFX exports and gcCore (Ext.cpp); read by the effects
struct ExtState
{
    struct Key { OBJHANDLE h; int id; bool operator<(const Key& o) const { return h != o.h ? h < o.h : id < o.id; } };
    struct Jet { VECTOR3 pos, dir; double thrust = 0, ve = 0, T = 0, wet = 0, rExit = 0, t = 0; int ground = 0; };
    struct Wake { VECTOR3 pos; double r = 0, depth = 0, t = 0; };
    struct Heat { VECTOR3 pos; double r = 0, dT = 0; };
    struct RenderProc { void* proc; DWORD id; void* param; };
    std::mutex mx;
    std::map<Key, Jet> jets;
    std::map<Key, Wake> wakes;
    std::map<Key, Heat> heat;
    std::vector<RenderProc> renderProcs;
};
extern ExtState g_ext;

// the FX atmosphere (FxAtmo.cpp): when active it draws the sky and the aerial perspective itself (after the opaque
// scene, from the depth) and gives the light at each point through fx_atmo_light.hlsli (t8, b4, set in FxFrame)
struct FxAtmoState { bool active = false; float starVis = 1; float exposure = 1; float white[3] = { 1, 1, 1 }; float light[3] = { 1, 1, 1 }; };   // exposure, white: the eye's adaptation (FxFrame); light: on a level surface at the camera
inline FxAtmoState g_fxAtmo;      // one instance for the whole DLL (C++17 inline variable); filled in FxAtmo.cpp (FxFrame)

// ---------------------------------------------------------------- surfaces and textures (SURFHANDLE)
struct Surf
{
    UINT w = 0, h = 0;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    int refs = 1;
    std::string name;          // file name for cached textures
    signed char isSkin = -1;   // Mesh.cpp: the name says skin (a face, a hand); -1 not looked yet (was lower-cased every draw: 12 % of a frame)
    signed char alpha = -1;    // a file's texture: 1 see-through texels (Dds.cpp g_ddsAlpha), 0 none - its groups go into the depth prepass; -1 not known
    DWORD attrib = 0;
    bool immutable = false;    // loaded from a file (an immutable GPU texture)
    DWORD ckey = 0; bool hasKey = false;   // colour key for blits
    // the CPU side for 2D drawing (Draw.cpp)
    HBITMAP dib = nullptr; HDC dc = nullptr; void* bits = nullptr; HGDIOBJ oldBmp = nullptr;
    // gamma-space views for the modules' image processing (Ipi.cpp); gpuNewer: the GPU wrote it after the DIB
    ComPtr<ID3D11ShaderResourceView> srvLin; ComPtr<ID3D11RenderTargetView> rtvLin;
    bool gpuNewer = false;
    bool cpuPref = false;      // GDI asked for it (a DC): its Sketchpads stay on the DIB
    bool counted = false;      // in the client's surface count (Client.cpp: LogMemory)
    void FreeDIB();
    ~Surf() { FreeDIB(); }
};

HRESULT CachedCompileFromFile(const wchar_t* path, const D3D_SHADER_MACRO* defs, const char* entry, const char* target, UINT flags, ID3DBlob** code, ID3DBlob** err);   // ShaderCache.cpp
ComPtr<ID3D11ShaderResourceView> LoadDDSFile(ID3D11Device* dev, ID3D11DeviceContext* ctx, const char* path, bool srgb, UINT* w = nullptr, UINT* h = nullptr);
ComPtr<ID3D11ShaderResourceView> LoadDDSMemory(ID3D11Device* dev, const BYTE* data, size_t size, bool srgb, UINT* w = nullptr, UINT* h = nullptr, bool genMips = true);
bool ReadFileBytes(const char* path, std::vector<BYTE>& out);

// the liquid's level radius as drawn for a camera at distance dc from the centre: a camera on the level or up to 0.5 m
// under it sees the surface 5 cm below its eye (Orbiter keeps the camera on the surface over the liquid)
inline double LiquidLevelFor(double Rl, double dc) { return (dc < Rl + 0.05 && dc > Rl - 0.5) ? dc - 0.05 : Rl; }

// the pipeline as it was, put back when this goes out of scope (2D work runs in the middle of the frame; GpuPad.cpp)
struct PipeState
{
    ID3D11DeviceContext* c;
    ID3D11RenderTargetView* rtv[4] = {}; ID3D11DepthStencilView* dsv = nullptr;
    UINT nvp = 0; D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11InputLayout* il = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY top = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOff = 0; ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0;
    ID3D11BlendState* bs = nullptr; float bf[4] = {}; UINT mask = 0; ID3D11DepthStencilState* dss = nullptr; UINT ref = 0; ID3D11RasterizerState* rs = nullptr;
    ID3D11ShaderResourceView* srv[16] = {}; ID3D11SamplerState* smp[16] = {}; ID3D11Buffer* pcb[8] = {}; ID3D11Buffer* vcb[8] = {};
    explicit PipeState(ID3D11DeviceContext* ctx);
    ~PipeState();
};

// ---------------------------------------------------------------- particle streams (Particles.cpp)
struct PVert { XMFLOAT3 p; XMFLOAT2 uv; XMFLOAT4 c; };
class PStream : public oapi::ParticleStream
{
public:
    PStream(oapi::GraphicsClient* gc, PARTICLESTREAMSPEC* pss);
    struct P { VECTOR3 pos, vel, air; double size, age, life, alpha, rho, tIn = 0; };   // tIn: the time it takes to condense (shows after it)
    PARTICLESTREAMSPEC spec;
    std::deque<P> ps;
    double acc = 0;
    unsigned flameNext = 0;     // the next of its thruster's flames to emit from (Particles.cpp)
    VECTOR3 lastSrc = { 0, 0, 0 }; bool hasLast = false;   // the source's place at the last frame's emission (a trail without gaps)
    bool engine = false;        // an exhaust stream (AddExhaustStream): hidden while the 3D flow carries its vessel's exhaust
    OBJHANDLE Ref() const { return hRef; }
    void Update(double dt);
    double LevelAlpha(double L) const;
    bool Expired() const { return !level && ps.empty(); }
};

// ---------------------------------------------------------------- device meshes (DEVMESHHANDLE)
struct DGroup
{
    UINT vb0 = 0, nv = 0, ib0 = 0, ni = 0;
    DWORD tex = 0;             // 0 = none, else 1-based texture slot
    DWORD mtrl = (DWORD)-1;    // (DWORD)-1 = default material
    DWORD usrFlag = 0, flags = 0;
    WORD zBias = 0;
    XMFLOAT4X4 xf;             // animation transform of the group (mesh frame, row vectors)
    XMFLOAT4X4 prevWVP;        // last frame's world-view-projection (no jitter): motion vectors
    bool hasPrev = false;
    std::vector<NTVERTEX> vtx; // CPU copy (edits, requests)
    std::vector<WORD> idx;
};

struct DevMesh
{
    std::vector<DGroup> grp;
    std::vector<MATERIAL> mtrl;
    std::vector<XMFLOAT4> emis2;   // oapiSetMaterialEx(Emission) per material: output = max(lit, it), as D3D9 (a module's glow)
    std::vector<SURFHANDLE> tex;   // [0] = none
    ComPtr<ID3D11Buffer> vb, ib;
    bool Build(ID3D11Device* dev, MESHHANDLE src);
    ~DevMesh();                    // releases its references on the textures
    void ResetTransforms();
    void UploadGroup(ID3D11DeviceContext* ctx, DWORD g);
};

// ---------------------------------------------------------------- vessel visuals (VISHANDLE)
struct VesselVis
{
    OBJHANDLE hObj = nullptr;
    VESSEL* v = nullptr;
    std::vector<DevMesh*> mesh;         // per mesh index (nullptr = none)
    std::vector<double> animApplied;    // animation state the meshes are in
    bool registered = false;
    void CreateMeshes();
    void DeleteMesh(int idx);
    void ResetAnimations();
    void UpdateAnimations();
    ~VesselVis();
};

// ---------------------------------------------------------------- planets: surface tiles
struct TreeArchive
{
    FILE* f = nullptr;
    struct Node { __int64 pos; DWORD size; DWORD child[4]; };
    std::vector<Node> toc;
    __int64 dataOfs = 0, dataLen = 0;
    DWORD root[5] = { (DWORD)-1, (DWORD)-1, (DWORD)-1, (DWORD)-1, (DWORD)-1 };   // levels 1, 2, 3, level 4 west/east
    std::mutex m;
    bool Open(const std::string& path);
    DWORD Index(int lvl, int ilat, int ilng);
    bool Has(int lvl, int ilat, int ilng) { DWORD i = Index(lvl, ilat, ilng); return i != (DWORD)-1 && toc[i].size > 0; }
    bool HasNode(int lvl, int ilat, int ilng) { return Index(lvl, ilat, ilng) != (DWORD)-1; }
    bool Read(int lvl, int ilat, int ilng, std::vector<BYTE>& out);
    ~TreeArchive() { if (f) fclose(f); }
};

// the device's generation: +1 each time the client makes its D3D11 device (each scenario: the render window and the
// device are made anew). A module keeping device objects in statics remakes them when this changed - an old device's
// shaders and buffers handed to a new device's context made a GPU exception (Xid 13) and lost the device (2026-10-20)
extern unsigned g_devGen;

struct Tile;
class Planet;

struct TileVertex { XMFLOAT3 p; XMFLOAT3 n; XMFLOAT2 uv; };

// a tile's 259x259 heights kept after its load, in 16 bits over the tile's own range (base + step * q): half the bytes
// of floats (134 KB a tile), a step of millimetres on the near, fine tiles (Planet::Load keeps them only where a child
// may still need them to interpolate from)
struct ElevQ
{
    float base = 0, step = 1;
    std::vector<uint16_t> q;
    float at(size_t i) const { return base + step * q[i]; }
    static std::shared_ptr<const ElevQ> From(const std::vector<float>& e)
    {
        auto r = std::make_shared<ElevQ>();
        float lo = e[0], hi = e[0];
        for (float v : e) { lo = std::min(lo, v); hi = std::max(hi, v); }
        r->base = lo; r->step = std::max((hi - lo) / 65535.0f, 1e-6f);
        r->q.resize(e.size());
        const float k = 1.0f / r->step;
        for (size_t i = 0; i < e.size(); i++) r->q[i] = (uint16_t)std::min(65535.0f, (e[i] - lo) * k + 0.5f);
        return r;
    }
};

struct Tile
{
    int lvl, ilat, ilng;
    Tile* parent = nullptr;
    Tile* child[4] = {};
    std::atomic<int> state{ 0 };        // 0 new, 1 queued, 2 ready, 3 failed
    std::atomic<float> prio{ 1e9f };    // while queued: how much it matters, smaller first (Planet::LoadPrio, each frame)
    ComPtr<ID3D11ShaderResourceView> tex;   // own surface texture (null: use an ancestor's)
    ComPtr<ID3D11ShaderResourceView> mask;  // own liquid mask (alpha 0 = liquid; null: an ancestor's)
    ComPtr<ID3D11ShaderResourceView> sdf;   // with it: the coast's signed distance (+land, -liquid) in mask texels, R16F;
                                            // null where no coast lies within reach (Planet::Load)
    float sdfTexel = 0;                     // its texel [m]
    int maskClass = 0;                      // without a field: the mask round the tile all land (1), all liquid (-1)
    ComPtr<ID3D11ShaderResourceView> nmap;  // normals from its own elevation (257x257, planet frame; null: an ancestor's)
    ComPtr<ID3D11ShaderResourceView> hor[2];  // horizon sines in 8 directions (E, NE, N, NW / W, SW, S, SE), 65x65, with nmap
    std::shared_ptr<const ElevQ> elev;  // 259x259 [m], its own data's; null once no child needs it (and on tiles without
                                        // data of their own: those keep only a reference to the owner's, elevSrc)
    std::shared_ptr<const ElevQ> elevSrc;   // where its heights come from: its own (= elev) or the nearest ancestor's with
                                            // data (shared, not copied: interpolated copies of 134 KB a tile filled the
                                            // 32-bit address space on a zoom, 2026-10-21); null once no child needs it
    float esR = 1, esC = 1, esS = 1;        // sample (i, j) of this tile is the owner's (esR + esS*(i-1), esC + esS*(j-1))
    bool ownElev = false;
    bool hasElev = false;               // it had heights at its load (kept or not)
    std::shared_ptr<const ElevQ> nodes; // the mesh's own 65x65 node heights (every 4th sample), always kept (8 KB): what
                                        // the drawn ground is, for the elevation checks when the full heights are dropped
    bool hasDetail = false;             // the ground detail is baked into its mesh (Planet::Load)
    ComPtr<ID3D11ShaderResourceView> mat;   // the layered ground material baked once (albedo factor, wetness, detail normal)
    int elevLvl = 0;                    // level of the elevation data this tile's mesh comes from (own or an ancestor's)
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> vbM;           // per vertex: the offset to where the parent's coarser mesh has it (LOD morphing)
    VECTOR3 centre;                     // planet frame [m], origin of the vertices
    double radius = 0;                  // bounding sphere
    double minlat, maxlat, minlng, maxlng;
    double emin = 0, emax = 0;
    float skirtDrop = 20.0f;                // how far its skirts hang [m] (Planet::Load; the VS needs it too)
    DWORD lastUsed = 0;
};

// the atmosphere as the client draws it (Config\D3D11Client\<planet>.cfg, or derived from Orbiter's parameters)
struct AtmCfg
{
    bool has = false;
    VECTOR3 horizon = { 0, 0, 0 }, zenith = { 0, 0, 0 }, ambient = { 0, 0, 0 }, sun = { 1, 1, 1 }, hazeRGB = { 1, 1, 1 };
    double hazeExt = 0, scaleH = 8000, top = 100000, rho0 = 1;
};

// a liquid on the surface (Titan's seas): level above the mean radius, extinction, colour, channel weights, index
struct LiquidCfg
{
    bool has = false;
    double level = 0, ext = 0.3, index = 1.29;
    VECTOR3 colour = { 0.40, 0.30, 0.14 }, wts = { 0.6, 1.0, 1.8 };
};

class Planet
{
public:
    Planet(OBJHANDLE h);
    AtmCfg atm;
    LiquidCfg liq;
    // micro textures (Config\MicroTex.cfg, D3D9Client's format): three detail levels tiled in metres over the ground;
    // loaded on their own thread the first time the camera comes near the surface
    struct MicroLevel { std::string file; double reso = 0; double L = 0; ComPtr<ID3D11ShaderResourceView> srv, h; XMFLOAT4 mean = { 0.5f, 0.5f, 0.5f, 0.5f }; };
    MicroLevel mic[3];
    bool micCfg = false, micNormals = false;
    bool relief = false;                // height layers <level>_h.dds for levels A and B: tessellated ground relief
    // ground classes (Config\OrbiterCrew\Ground\<name>.cfg/.bin, what suits and wheels stand on): weights texture
    ComPtr<ID3D11ShaderResourceView> groundTex; double gLat = 0, gLon = 0, gStep = 0; int gW = 0, gH = 0;
    std::vector<BYTE> groundCpu;        // the class weights (RGBA8, gW x gH) for the tile geometry
    struct HLayer { UINT N = 0; std::vector<std::vector<uint16_t>> mip; };   // a relief layer on the CPU (RGBA16 mips)
    HLayer relCpu[2];
    double ReliefCpu(Tile* t, const VECTOR3& lp, double elev, double slope, int mipA, int mipB);
    std::atomic<int> micState{ 0 };    // 0 not loaded, 1 loading, 2 ready, 3 failed
    std::thread micThread;
    ~Planet();
    void Render(ID3D11DeviceContext* ctx, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH);
    OBJHANDLE hObj;
    std::string name;
    double R;
    int maxLevel = 18;
    int tilesDrawn = 0;
    std::atomic<int> tilesLoaded{ 0 };  // (the loader threads count)
    float LoadPrio(const Tile* t, const VECTOR3& cpl) const;
    bool deepOk = true;     // this frame may split past the data (the last frame's tiles and the load queue within budget)
    // the loader (worker thread)
    void Load(Tile* t);
    static std::string LoadStats();     // the tile loads since the last call: how many, the time of each stage (the log)
    bool HasSurf(int lvl, int ilat, int ilng);
    bool HasElev(int lvl, int ilat, int ilng);
    void Purge(DWORD frame);
private:
    // the coast's distance field (Planet::CoastField): the masks' alpha decoded, kept for the neighbours' fields
    std::mutex sdfMx;
    std::unordered_map<uint64_t, std::pair<std::shared_ptr<std::vector<uint8_t>>, int>> maskCache;
    std::shared_ptr<std::vector<uint8_t>> MaskAlpha(int lvl, int ilat, int ilng, int& w);
    ComPtr<ID3D11ShaderResourceView> CoastField(Tile* t, int& cls);
    std::unique_ptr<TreeArchive> surfTree, elevTree;
    std::string texDir;                 // Textures\<name>
    std::unordered_map<std::string, bool> fileCache;
    std::mutex fileMx;
    Tile* root[2] = {};
    bool FileExists(const std::string& p);
    std::string LoosePath(const char* layer, int lvl, int ilat, int ilng, const char* ext);
    Tile* Child(Tile* t, int k);
    void Traverse(Tile* t, ID3D11DeviceContext* ctx, const VECTOR3& cpl, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH, const VECTOR3& sunL);
    void Draw(Tile* t, ID3D11DeviceContext* ctx, const VECTOR3& cpl, const VECTOR3& sunL);
    void FreeTile(Tile* t);
    // per frame
    MATRIX3 Rp; VECTOR3 Pp; VECTOR3 camGlobal; MATRIX3 camR; XMMATRIX P;
    DWORD frame = 0;
    double micFade[3] = {};             // micro levels' fade-out distances this frame [m] (0: off)
    void MicroLoad();
    ComPtr<ID3D11ShaderResourceView> gmat[2];   // ground material: <body>_ground_cobbles.dds, _sand.dds
    int gmatState = 0;                  // 0 not tried, 2 loaded, 3 none
    void BakeTile(Tile* t, ID3D11DeviceContext* ctx);
    int bakesThisFrame = 0;
    void LoadGround();
    XMFLOAT4 units[4] = {}; int nUnits = 0;    // landed units near the camera this frame (camera-centred, flat radius)
    double MeshElev(double lng, double lat, int* lvl, double* fine, bool* own);   // test runs: our surface there
    void ElevCheck(const VECTOR3& camPos);
};

// ---------------------------------------------------------------- the client
class D3D11Client : public oapi::GraphicsClient
{
public:
    const std::map<OBJHANDLE, VesselVis*>& Visuals() const { return visuals; }   // the vessels drawn (VesselExhaust.cpp)
    D3D11Client(HINSTANCE h);
    ~D3D11Client();

    bool clbkInitialise() override;
    bool clbkFullscreenMode() const override { return const_cast<D3D11Client*>(this)->GetVideoData()->fullscreen; }
    void clbkGetViewportSize(DWORD* w, DWORD* h) const override { *w = W; *h = H; }
    bool clbkGetRenderParam(DWORD prm, DWORD* value) const override;
    bool clbkUseLaunchpadVideoTab() const override { return true; }
    void clbkRefreshVideoData() override;
    INT_PTR LaunchpadVideoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    void InitVideoTab();
    void OpenAdvanced(HWND parent);

    SURFHANDLE clbkLoadTexture(const char* fname, DWORD flags = 0) override;
    SURFHANDLE clbkLoadSurface(const char* fname, DWORD attrib, bool bPath = false) override;
    void clbkReleaseTexture(SURFHANDLE hTex) override;
    bool clbkSetMeshTexture(DEVMESHHANDLE hMesh, DWORD texidx, SURFHANDLE tex) override;
    int clbkSetMeshMaterial(DEVMESHHANDLE hMesh, DWORD matidx, const MATERIAL* mat) override;
    int clbkMeshMaterial(DEVMESHHANDLE hMesh, DWORD matidx, MATERIAL* mat) override;
    int clbkSetMeshMaterialEx(DEVMESHHANDLE hMesh, DWORD matidx, MatProp prp, const oapi::FVECTOR4* in) override;
    int clbkMeshMaterialEx(DEVMESHHANDLE hMesh, DWORD matidx, MatProp prp, oapi::FVECTOR4* out) override;
    bool clbkSetMeshProperty(DEVMESHHANDLE hMesh, DWORD property, DWORD value) override { return false; }
    int clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context) override;
    MESHHANDLE clbkGetMesh(VISHANDLE vis, UINT idx) override;
    int clbkGetMeshGroup(DEVMESHHANDLE hMesh, DWORD grpidx, GROUPREQUESTSPEC* grs) override;
    int clbkEditMeshGroup(DEVMESHHANDLE hMesh, DWORD grpidx, GROUPEDITSPEC* ges) override;

    // particle streams: Orbiter keeps their state (attachment, level); drawing them comes with the effects stage
    oapi::ParticleStream* clbkCreateParticleStream(PARTICLESTREAMSPEC* pss) override;
    oapi::ParticleStream* clbkCreateExhaustStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel, const double* lvl, const VECTOR3* ref, const VECTOR3* dir) override;
    oapi::ParticleStream* clbkCreateExhaustStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel, const double* lvl, const VECTOR3& ref, const VECTOR3& dir) override;
    oapi::ParticleStream* clbkCreateReentryStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel) override;

    // 2D (Draw.cpp)
    oapi::Sketchpad* clbkGetSketchpad(SURFHANDLE surf) override;
    void clbkReleaseSketchpad(oapi::Sketchpad* sp) override;
    oapi::Font* clbkCreateFont(int height, bool prop, const char* face, FontStyle style = FontStyle::FONT_NORMAL, int orientation = 0) const override;
    oapi::Font* clbkCreateFontEx(int height, char* face, int width = 0, int weight = 400, FontStyle style = FontStyle::FONT_NORMAL, float spacing = 0.0f) const override;
    void clbkReleaseFont(oapi::Font* font) const override;
    oapi::Pen* clbkCreatePen(int style, int width, DWORD col) const override;
    void clbkReleasePen(oapi::Pen* pen) const override;
    oapi::Brush* clbkCreateBrush(DWORD col) const override;
    void clbkReleaseBrush(oapi::Brush* brush) const override;
    HDC clbkGetSurfaceDC(SURFHANDLE surf) override;
    void clbkReleaseSurfaceDC(SURFHANDLE surf, HDC hDC) override;
    bool clbkFillSurface(SURFHANDLE surf, DWORD col) const override;
    bool clbkFillSurface(SURFHANDLE surf, DWORD tgtx, DWORD tgty, DWORD w, DWORD h, DWORD col) const override;
    void UploadSurface(Surf* s);
    bool clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD flag = 0) const override;
    bool clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD w, DWORD h, DWORD flag = 0) const override;
    bool clbkScaleBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, DWORD tgtw, DWORD tgth, SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch, DWORD flag = 0) const override;
    int clbkBeginBltGroup(SURFHANDLE tgt) override;
    int clbkEndBltGroup() override;
    bool clbkSetSurfaceColourKey(SURFHANDLE surf, DWORD ckey) override;
    void clbkRender2DPanel(SURFHANDLE* hSurf, MESHHANDLE hMesh, MATRIX3* T, bool additive = false) override;
    void clbkRender2DPanel(SURFHANDLE* hSurf, MESHHANDLE hMesh, MATRIX3* T, float alpha, bool additive = false) override;
    bool EnsureDIB(Surf* s);
    void FixGdiAlpha(Surf* s);
    void Readback(Surf* s);
    void MarkDirty(Surf* s);
    void FlushDirty();
    Surf* Target(SURFHANDLE h) const;
    bool BltImpl(SURFHANDLE tgt, int tx, int ty, int tw, int th, SURFHANDLE src, int sx, int sy, int sw, int sh, DWORD flag);
    void BeginScreenLayer();
    void CompositeScreenLayer();

    SURFHANDLE clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib) override;
    SURFHANDLE clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE hTemplate = NULL) override;
    SURFHANDLE clbkCreateTexture(DWORD w, DWORD h) override;
    void clbkIncrSurfaceRef(SURFHANDLE surf) override;
    bool clbkReleaseSurface(SURFHANDLE surf) override;
    bool clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h) override;

    ID3D11Device* Dev() { return dev.Get(); }
    HWND RenderWnd() const { return hWnd; }
    DevMesh* VesselMesh(OBJHANDLE h, DWORD idx);
    void CallRenderProcs();
    void LoadShim();
    ID3D11DeviceContext* Ctx() { return ctx.Get(); }
    bool RunIPI(class ImageProcessing* p, DWORD blendop);    // Ipi.cpp
    bool SurfGpuViews(Surf* s);
    bool SurfToGpu(Surf* s);            // its DIB drawing sent up first; the gamma-space views made
    void SurfGpuWritten(Surf* s) { dirty.erase(s); }
    const std::string& ShaderDir() const { return shaderDir; }
    bool ShadersOk() const { return shadersOk; }
    bool IsScreen(const Surf* s) const { return s == screen; }
    bool GpuSurface(Surf* s) const;
    void TestScript();      // test runs: D3D11CLIENT_TEST_SCRIPT actions (Client.cpp)
    // the liquid's mirror (Liquid.cpp): the vessels drawn reflected in its plane into their own target
    bool mirrorPass = false; XMMATRIX mirrorM = XMMatrixIdentity();
    bool ownPass = false;      // the camera's own vessel from inside, drawn last; its overlay layers write no depth
    // the depth prepass (Client.cpp, before the planets where no liquid needs their bed): RenderVessel writes only the
    // depth of the opaque groups with opaque textures - the colour pass then lights each visible pixel once (early depth
    // rejection; psMeshMain has no discard, which would turn it off)
    bool prepass = false;
    // the view from inside a liquid (Liquid.cpp)
    ComPtr<ID3D11VertexShader> uwVS; ComPtr<ID3D11PixelShader> uwPS; ComPtr<ID3D11BlendState> uwBlend; ComPtr<ID3D11Buffer> uwCB;
    void RenderUnderwater(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);
    ComPtr<ID3DBlob> CompileShaderFile(const char* file, const char* entry, const char* target);
    void RenderAO(const XMMATRIX& proj);     // Ssao.cpp: ambient occlusion over the opaque scene
    ID3D11RenderTargetView* AOShareTarget();     // Ssao.cpp: the sky's share of the light, written by the opaque pass (target 2); null without AO
    // Lights.cpp: the vessels' local lights - gathered, clustered, spot shadows (after the Sun's shadows); b9 + t17..t20
    // bound for the scene's shaders; the beams in the air, the lenses and the beacons (after the air)
    void LightsFrame(const VECTOR3& cp);
    void LightsBind();
    void LightsRenderAir(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);
    void LightsProbe(ID3D11ShaderResourceView* img);     // ... measured on the finished scene (its depth, its image)
    void LightsAdaptEye(XMFLOAT4& adapt, float& exposure);   // the eye adapts to the local lights where they light what it sees
    // Radiosity.cpp: the real-time radiosity of a vessel that sent its light transport (D3D11Lights SetRadiosity)
    OBJHANDLE GIVessel(const VECTOR3& cp);                          // the nearest such vessel (its file loaded), null: none
    void GILamps(const std::vector<float>& own, int nLights);       // its lamps this frame (Lights.cpp), the frame's list's length
    void GIShade(XMFLOAT4* out);                                    // b9's 29 constants (lights.hlsli gGI*)
    void GIRun();                                                   // the compute passes (LightsBind)
    ID3D11ShaderResourceView* GIProbeSRV();                         // the probes (t27), null: none
    ID3D11ShaderResourceView* RenderBloom(ID3D11ShaderResourceView* src);   // Bloom.cpp: the glow for the post pass
    ComPtr<ID3D11RasterizerState> rsMirror;
    ComPtr<ID3D11Texture2D> reflTex, reflDepth; ComPtr<ID3D11RenderTargetView> reflRTV; ComPtr<ID3D11ShaderResourceView> reflSRV;
    ComPtr<ID3D11DepthStencilView> reflDSV; ComPtr<ID3D11Buffer> mirrorCB; UINT reflW = 0, reflH = 0;
    bool RenderMirror(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, const VECTOR3& base, const VECTOR3& up);
    ComPtr<ID3D11Buffer> wakeCB; ComPtr<ID3D11ShaderResourceView> kelvinSRV;   // the liquid's wakes (Liquid.cpp, Wake.cpp)
    void DumpSurface(Surf* s, const char* path);    // diagnostics (Ipi.cpp)
    void RenderAr(const VECTOR3& cam, const MATRIX3& cr, const XMMATRIX& proj);   // Ar.cpp: markers and paths in the scene
    bool DiagFrame() const { return diagFrames != 0; }
    // shared render state for the scene parts
    ComPtr<ID3D11VertexShader> vsMesh, vsPlanet;
    ComPtr<ID3D11PixelShader> psMesh, psPlanet, psMeshMain;
    ComPtr<ID3D11InputLayout> ilMesh, ilPlanet;
    ComPtr<ID3D11Buffer> cbObj, cbGrp;
    ComPtr<ID3D11SamplerState> sampAniso, sampClamp;
    ComPtr<ID3D11RasterizerState> rsBack, rsNone;
    ComPtr<ID3D11DepthStencilState> dsWrite, dsRead;
    ComPtr<ID3D11BlendState> bsOpaque, bsAlpha, bsAdd;
    ComPtr<ID3D11VertexShader> vsBlit, vsPanel;
    ComPtr<ID3D11PixelShader> psBlit, psPanel;
    ComPtr<ID3D11Buffer> tileIB;
    UINT tileIdxCount = 0, tileIdxCount2 = 0;     // the fine grid, then the half-density one after it in tileIB
    ComPtr<ID3D11ShaderResourceView> whiteTex;
    // the tile loader
    void QueueTile(Planet* p, Tile* t);
    size_t LoadQueueSize() { std::lock_guard<std::mutex> lk(loadMx); return loadQ.size(); }
    std::mutex loadMx;
    std::condition_variable loadCv;
    std::deque<std::pair<Planet*, Tile*>> loadQ;
    std::atomic<bool> loaderRun{ false };
    std::vector<std::thread> loaders;
    DWORD frameNo = 0;
    ComPtr<ID3D11Buffer> cbFrame;
    ComPtr<ID3D11VertexShader> vsSky;
    ComPtr<ID3D11PixelShader> psSky;
    bool skyOn = false;
    float starVis = 1;
    // stars (Stars.cpp)
    void CreateStars();
    void RenderStars(const MATRIX3& cr, const XMMATRIX& proj, float visibility);
    ComPtr<ID3D11ShaderResourceView> starSRV;
    UINT nStars = 0;
    ComPtr<ID3D11VertexShader> vsStars;
    ComPtr<ID3D11PixelShader> psStars;
    // particle streams
    std::vector<PStream*> streams;
    std::vector<PVert> pverts;
    ComPtr<ID3D11Buffer> partVB;
    UINT partVBSize = 0;
    ComPtr<ID3D11ShaderResourceView> partTex;
    ComPtr<ID3D11VertexShader> vsPart;
    ComPtr<ID3D11PixelShader> psPart;
    ComPtr<ID3D11InputLayout> ilPart;
    void UpdateRenderParticles(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, double dt);
    void CreateParticleTexture();
    // liquids (Liquid.cpp)
    void CreateLiquid();
    void CopySceneColour(ID3D11Texture2D* dst);   // single-sampled copy of the scene colour so far; resolves depthSRV (AA.cpp)
    // anti-aliasing (AA.cpp)
    UINT SceneSamples() const;
    void CreateSceneTargets();
    void ResolveDepth();
    XMFLOAT2 Jitter() const;
    ID3D11ShaderResourceView* FinishScene(const VECTOR3& cp, const XMMATRIX& vpJit, const XMMATRIX& vpNoJit);
    void ToneMapAndAA(ID3D11ShaderResourceView* src);
    UINT samples = 1;
    UINT RW = 0, RH = 0;     // the scene's size (smaller than the window with the TAA render scale)
public:
    UINT SceneW() const { return RW; }     // size the scene-resolution targets and viewports with these
    UINT SceneH() const { return RH; }
    ComPtr<ID3D11ShaderResourceView> depthMSSRV;
    ComPtr<ID3D11Texture2D> depthRes, outTex[2], ldrTex;
    ComPtr<ID3D11RenderTargetView> depthResRTV, outRTV[2], ldrRTV, bbRTVLin;
    ComPtr<ID3D11ShaderResourceView> outSRV[2], ldrSRV;
    ComPtr<ID3D11VertexShader> vsAA;
    ComPtr<ID3D11PixelShader> psResolveDepth, psTAA, psFXAA;
    XMFLOAT4X4 taaPrevVP;
    VECTOR3 taaPrevCam = { 0, 0, 0 };
    bool taaHasHistory = false;
    // motion vectors (TAA): vessels write theirs, the rest reprojects through the depth relative to the nearest planet
    ComPtr<ID3D11Texture2D> mvTex;
    ComPtr<ID3D11RenderTargetView> mvRTV;
    ComPtr<ID3D11ShaderResourceView> mvSRV;
    XMFLOAT4X4 curVPNoJit;
    OBJHANDLE taaRefBody = nullptr;
    VECTOR3 taaPrevRel = { 0, 0, 0 };
    void RenderLiquid(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, float tanY);
    void SaveLiquidBed();
    void RenderClouds(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // volumetric cloud layers (Clouds.cpp)
    void RenderCloudShadow(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // Earth: the cloud layer's shadows, the cyclone's map (CloudLayer.cpp)
    void RenderCloudLayer(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);    // Earth: the 2D cloud layer from afar (CloudLayer.cpp)
    void RenderEarthClouds(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // Earth: the volumetric clouds near (EarthClouds.cpp)
    void RenderStormShadow(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // Earth's cyclone: its shadow on what is drawn (Storm.cpp)
    void RenderStorm(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);         // Earth's cyclone: the volumetric clouds (Storm.cpp)
    void RenderStormRain(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);     // Earth's cyclone: its rain round the camera (StormRain.cpp)
    void RenderSun(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);    // the Sun: disc, chromosphere, corona (Sun.cpp)
    void RenderSunGlare();     // ... its glare in the eye over the finished scene (Sun.cpp)
    void RenderVisorDropsOutside(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // rain drops on the suits' glass seen from outside (VisorDrops.cpp)
    void RenderVisorDropsInside(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);    // ... on the own visor's glass: simulated, seen from the eye (VisorDrops.cpp)
    void RenderRain(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj);   // Titan's methane rain (Rain.cpp)     // the terrain's depth alone (after the planets, before the vessels): the liquid's bed (Liquid.cpp)
    ComPtr<ID3D11Buffer> liqVB, liqIB;
    UINT liqIdx = 0;
    ComPtr<ID3D11Texture2D> hdrCopy;
    ComPtr<ID3D11ShaderResourceView> hdrCopySRV, depthSRV;
    ComPtr<ID3D11DepthStencilView> dsvRO;
    ComPtr<ID3D11VertexShader> vsLiquid;
    ComPtr<ID3D11PixelShader> psLiquid;
    ComPtr<ID3D11InputLayout> ilLiquid;
    void UpdateFrameCB(const VECTOR3& cp, const XMMATRIX& invVP);

protected:
    HWND clbkCreateRenderWindow() override;
    LRESULT RenderWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    void clbkPostCreation() override;
    void clbkCloseSession(bool fastclose) override;
    void clbkDestroyRenderWindow(bool fastclose) override;
    bool clbkSplashLoadMsg(const char* msg, int line) override;
    void clbkRenderScene() override;
    bool clbkDisplayFrame() override;
    void clbkPreStep(double simt, double simdt, double mjd) override { void StormPreStep(double, double, double); StormPreStep(simt, simdt, mjd); }   // the storm's turbulence (Storm.cpp)

private:
    HWND hWnd = nullptr;
    DWORD W = 1280, H = 800;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain1> sc;
    ComPtr<ID3D11RenderTargetView> bbRTV;
    ComPtr<ID3D11Texture2D> bbTex;
    ComPtr<ID3D11Texture2D> hdrTex, depthTex;
    ComPtr<ID3D11RenderTargetView> hdrRTV;
    ComPtr<ID3D11ShaderResourceView> hdrSRV;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vsPost;
    ComPtr<ID3D11PixelShader> psPost;
    std::map<std::string, Surf*> texCache;
    std::map<OBJHANDLE, VesselVis*> visuals;
    std::vector<Planet*> planets;
    std::string shaderDir;
    // timing
    ComPtr<ID3D11Query> qDis[4], qTs[4][4];
    double passMs[3] = {};
    // the frame's GPU time pass by pass (GpuMark: a timestamp after each; the stats line every 10 s)
    static const int kMarks = 12;
    ComPtr<ID3D11Query> qMk[4][kMarks];
    double markMs[kMarks] = {};
    void GpuMark(UINT fi, int k) { if (qMk[fi][k]) ctx->End(qMk[fi][k].Get()); QueryPerformanceCounter(&cpuMk[k]); }
    // the CPU's time by the same marks (where each GpuMark is set) and round the frame: Orbiter and the modules between
    // two frames, the HUD drawn through the Sketchpad after clbkRenderScene, Present (a long Present: the CPU waits for
    // the GPU - the frame is the GPU's; a short one with a long frame: the CPU's). The stats line "CPU ms ..."
    LARGE_INTEGER cpuMk[kMarks] = {};
    double cpuMs[kMarks] = {};
    LARGE_INTEGER tSceneBeg = {}, tSceneEnd = {}, tDispBeg = {}, tPresentEnd = {};
    double cpuOutMs = 0, cpuPreMs = 0, cpuHudMs = 0, cpuDispMs = 0, cpuPresentMs = 0, cpuFrameMs = 0;
    // marks of their own, GPU and CPU: 0 after the Sun's shadows (the rest to mark 2: the local lights), 1 and 2 round the
    // vessels' depth prepass
    static const int kEx = 3;
    ComPtr<ID3D11Query> qEx[4][kEx];
    LARGE_INTEGER cpuEx[kEx] = {};
    double exMs[2] = {}, cpuExMs[2] = {};     // the Sun's shadows / the local lights split of mark 2; the prepass
    double exLightsMs = 0, cpuExLightsMs = 0;
    void GpuEx(UINT fi, int k) { if (qEx[fi][k]) ctx->End(qEx[fi][k].Get()); QueryPerformanceCounter(&cpuEx[k]); }
    // finer marks within two of them (the log's second line): before the AO, then after each pass of the transparent
    // part - the Sun, the transparent vessels, the storm's shadow, the air, the storm, the clouds (the lights' last)
    static const int kSub = 7;
    ComPtr<ID3D11Query> qSub[4][kSub];
    double subMs[kSub + 1] = {};
    void GpuSub(UINT fi, int k) { if (qSub[fi][k]) ctx->End(qSub[fi][k].Get()); }
    LARGE_INTEGER qpf, tStart, tLog;
    bool shotDone = false;
    // the 2D layer
    Surf* screen = nullptr;
    ComPtr<ID3D11Texture2D> screenTex;
    ComPtr<ID3D11ShaderResourceView> screenSRV;
    std::set<Surf*> dirty;
    RECT screenPrev = { 0, 0, 0, 0 };
    std::vector<DWORD> screenTmp;
    ComPtr<ID3D11Buffer> panelVB, panelIB;
    UINT panelVBSize = 0, panelIBSize = 0;
    // the loading screen (Direct2D on the swap chain)
    ComPtr<ID2D1Factory> d2f;
    ComPtr<IDWriteFactory> dwf;
    ComPtr<ID2D1RenderTarget> d2rt;
    ComPtr<IDWriteTextFormat> loadFont, loadFontBig;
    int loadSteps = 0, loadExpected = 120;
    std::string loadGroup;
    bool testMode = false;
    bool shadersOk = false;
    int diagFrames = 0;      // test runs: log the cockpit view's covering groups for a few frames

    bool CreateDevice();
    bool CreateShaders();
    void ScanVessels();
    void AddVisual(OBJHANDLE h);
    void RemoveVisual(OBJHANDLE h);
public:
    // vessels come and go at once (a suit putting on its jet pack deletes the pack's vessel in the middle of a frame):
    // their visuals and everything kept for them go with them, never a pointer left to a deleted VESSEL
    void clbkNewVessel(OBJHANDLE hVessel) override;
    void clbkDeleteVessel(OBJHANDLE hVessel) override;
    // the Sun's shadow map and its comparison sampler (t7, s2), for passes late in the frame whose slots others reused
    ID3D11ShaderResourceView* ShadowSRV() const { return shadowSRV.Get(); }
    ID3D11SamplerState* ShadowSampler() const { return sampShadow.Get(); }
private:
    void RenderVessel(VesselVis* vv, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, bool alphaPass, const XMMATRIX* lightVP = nullptr);
    // shadows: one map from the Sun around the focus object
    void RenderShadows(const VECTOR3& cp);
    ComPtr<ID3D11Texture2D> shadowTex;
    ComPtr<ID3D11DepthStencilView> shadowDSV;
    ComPtr<ID3D11ShaderResourceView> shadowSRV;
    ComPtr<ID3D11SamplerState> sampShadow;
    ComPtr<ID3D11DepthStencilState> dsShadow;
    ComPtr<ID3D11RasterizerState> rsShadow;
    XMFLOAT4X4 lightVP, lightVP2;     // (the first two cascades; the shaders read casc)
    XMFLOAT4X4 casc[4] = {}; float cascFar[4] = {}, cascTex[4] = {}, cascRange[4] = {}; float sunDiam = 0.0093f;   // the shadow cascades: light VP, far end [m], texel [m]
    bool shadowOn = false;
    static const UINT SHADOW_N = 2048;
    // the static casters' Sun shadows kept (RenderShadows): what stands still (a building, a parked vehicle) is drawn into
    // its own atlas only when a tile's key changes, and copied into the atlas each frame under the people and what moves
    struct ShadowTileKey { bool valid = false; long long iu = 0, iv = 0, iL = 0; double r = 0; uint64_t sig = 0; };
    ShadowTileKey shKey[4];
    ComPtr<ID3D11Texture2D> shadowStatTex;
    ComPtr<ID3D11DepthStencilView> shadowStatDSV;
    ComPtr<ID3D11ShaderResourceView> shadowStatSRV;
    ComPtr<ID3D11VertexShader> vsShTile; ComPtr<ID3D11PixelShader> psShCopy, psShClear;
    ComPtr<ID3D11DepthStencilState> dsShAlways;
    int shStatDrawn = 0, shStatKept = 0;     // tiles of static casters drawn / taken from the cache (the stats line)
    void SaveShot(const char* path);
    void LoaderMain();
};

// helpers
inline XMMATRIX RotToXM(const MATRIX3& R)   // Orbiter column-vector rotation -> row-vector XMMATRIX
{
    return XMMATRIX((float)R.m11, (float)R.m21, (float)R.m31, 0,
                    (float)R.m12, (float)R.m22, (float)R.m32, 0,
                    (float)R.m13, (float)R.m23, (float)R.m33, 0,
                    0, 0, 0, 1);
}

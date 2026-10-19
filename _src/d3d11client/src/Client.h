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
struct FxAtmoState { bool active = false; float starVis = 1; };
extern FxAtmoState g_fxAtmo;      // defined and filled in FxAtmo.cpp (FxFrame)

// ---------------------------------------------------------------- surfaces and textures (SURFHANDLE)
struct Surf
{
    UINT w = 0, h = 0;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    int refs = 1;
    std::string name;          // file name for cached textures
    DWORD attrib = 0;
    bool immutable = false;    // loaded from a file (an immutable GPU texture)
    DWORD ckey = 0; bool hasKey = false;   // colour key for blits
    // the CPU side for 2D drawing (Draw.cpp)
    HBITMAP dib = nullptr; HDC dc = nullptr; void* bits = nullptr; HGDIOBJ oldBmp = nullptr;
    void FreeDIB();
    ~Surf() { FreeDIB(); }
};

ComPtr<ID3D11ShaderResourceView> LoadDDSFile(ID3D11Device* dev, ID3D11DeviceContext* ctx, const char* path, bool srgb, UINT* w = nullptr, UINT* h = nullptr);
ComPtr<ID3D11ShaderResourceView> LoadDDSMemory(ID3D11Device* dev, const BYTE* data, size_t size, bool srgb, UINT* w = nullptr, UINT* h = nullptr);
bool ReadFileBytes(const char* path, std::vector<BYTE>& out);

// ---------------------------------------------------------------- particle streams (Particles.cpp)
struct PVert { XMFLOAT3 p; XMFLOAT2 uv; XMFLOAT4 c; };
class PStream : public oapi::ParticleStream
{
public:
    PStream(oapi::GraphicsClient* gc, PARTICLESTREAMSPEC* pss);
    struct P { VECTOR3 pos, vel, air; double size, age, life, alpha, rho; };
    PARTICLESTREAMSPEC spec;
    std::deque<P> ps;
    double acc = 0;
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

struct Tile;
class Planet;

struct TileVertex { XMFLOAT3 p; XMFLOAT3 n; XMFLOAT2 uv; };

struct Tile
{
    int lvl, ilat, ilng;
    Tile* parent = nullptr;
    Tile* child[4] = {};
    std::atomic<int> state{ 0 };        // 0 new, 1 queued, 2 ready, 3 failed
    ComPtr<ID3D11ShaderResourceView> tex;   // own surface texture (null: use an ancestor's)
    std::shared_ptr<std::vector<float>> elev;   // 259x259 [m], own or interpolated from the parent
    bool ownElev = false;
    ComPtr<ID3D11Buffer> vb;
    VECTOR3 centre;                     // planet frame [m], origin of the vertices
    double radius = 0;                  // bounding sphere
    double minlat, maxlat, minlng, maxlng;
    double emin = 0, emax = 0;
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
    ~Planet();
    void Render(ID3D11DeviceContext* ctx, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, float tanHalfY, int screenH);
    OBJHANDLE hObj;
    std::string name;
    double R;
    int maxLevel = 18;
    int tilesDrawn = 0, tilesLoaded = 0;
    // the loader (worker thread)
    void Load(Tile* t);
    bool HasSurf(int lvl, int ilat, int ilng);
    bool HasElev(int lvl, int ilat, int ilng);
    void Purge(DWORD frame);
private:
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
};

// ---------------------------------------------------------------- the client
class D3D11Client : public oapi::GraphicsClient
{
public:
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
    // shared render state for the scene parts
    ComPtr<ID3D11VertexShader> vsMesh, vsPlanet;
    ComPtr<ID3D11PixelShader> psMesh, psPlanet;
    ComPtr<ID3D11InputLayout> ilMesh, ilPlanet;
    ComPtr<ID3D11Buffer> cbObj, cbGrp;
    ComPtr<ID3D11SamplerState> sampAniso, sampClamp;
    ComPtr<ID3D11RasterizerState> rsBack, rsNone;
    ComPtr<ID3D11DepthStencilState> dsWrite, dsRead;
    ComPtr<ID3D11BlendState> bsOpaque, bsAlpha, bsAdd;
    ComPtr<ID3D11VertexShader> vsBlit, vsPanel;
    ComPtr<ID3D11PixelShader> psBlit, psPanel;
    ComPtr<ID3D11Buffer> tileIB;
    UINT tileIdxCount = 0;
    ComPtr<ID3D11ShaderResourceView> whiteTex;
    // the tile loader
    void QueueTile(Planet* p, Tile* t);
    std::mutex loadMx;
    std::condition_variable loadCv;
    std::deque<std::pair<Planet*, Tile*>> loadQ;
    std::atomic<bool> loaderRun{ false };
    std::thread loader;
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
    void clbkPostCreation() override;
    void clbkCloseSession(bool fastclose) override;
    void clbkDestroyRenderWindow(bool fastclose) override;
    bool clbkSplashLoadMsg(const char* msg, int line) override;
    void clbkRenderScene() override;
    bool clbkDisplayFrame() override;

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

    bool CreateDevice();
    bool CreateShaders();
    void ScanVessels();
    void RenderVessel(VesselVis* vv, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, bool alphaPass, const XMMATRIX* lightVP = nullptr);
    // shadows: one map from the Sun around the focus object
    void RenderShadows(const VECTOR3& cp);
    ComPtr<ID3D11Texture2D> shadowTex;
    ComPtr<ID3D11DepthStencilView> shadowDSV;
    ComPtr<ID3D11ShaderResourceView> shadowSRV;
    ComPtr<ID3D11SamplerState> sampShadow;
    ComPtr<ID3D11DepthStencilState> dsShadow;
    ComPtr<ID3D11RasterizerState> rsShadow;
    XMFLOAT4X4 lightVP;
    bool shadowOn = false;
    static const UINT SHADOW_N = 2048;
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

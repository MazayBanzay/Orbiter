// Minimal DDS loader: DXT1/3/5, ATI1/ATI2 (BC4/BC5), DX10 headers, 32/24-bit uncompressed. Mip chains as stored;
// textures without mips get them generated (uncompressed: box filtered; DXT1/3/5: decoded, filtered, encoded again).
#include "Client.h"
#include <climits>
#include <array>

namespace {
#pragma pack(push, 1)
struct PixFmt { uint32_t size, flags, fourCC, bits, rmask, gmask, bmask, amask; };
struct Header { uint32_t size, flags, height, width, pitch, depth, mips, res1[11]; PixFmt pf; uint32_t caps[4], res2; };
struct HeaderDX10 { uint32_t fmt, dim, misc, arraySize, misc2; };
#pragma pack(pop)

constexpr uint32_t FCC(char a, char b, char c, char d) { return a | (b << 8) | (c << 16) | (d << 24); }

DXGI_FORMAT ToSRGB(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return f;
    }
}

size_t BlockBytes(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_SNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC7_UNORM_SRGB: return 16;
    default: return 0;
    }
}
// ---- the mips a DXT texture was saved without (D3D9's D3DX makes them on load; without them a texture shimmers and looks
// dirty at a distance): the top level decoded, halved by 2x2 boxes, each level encoded again (bounding-box endpoints)
void Rgb565(uint16_t c, int* o) { o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31; }
uint16_t To565(const int* c) { return (uint16_t)(((c[0] * 31 + 127) / 255) << 11 | ((c[1] * 63 + 127) / 255) << 5 | ((c[2] * 31 + 127) / 255)); }

void DecodeColour(const BYTE* b, BYTE px[16][4], bool bc1)
{
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    int p[4][4]; Rgb565(c0, p[0]); Rgb565(c1, p[1]); p[0][3] = p[1][3] = 255;
    for (int k = 0; k < 3; k++) {
        if (c0 > c1 || !bc1) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
        else { p[2][k] = (p[0][k] + p[1][k]) / 2; p[3][k] = 0; }
    }
    p[2][3] = 255; p[3][3] = (c0 > c1 || !bc1) ? 255 : 0;
    uint32_t idx = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    for (int i = 0; i < 16; i++) { const int* q = p[(idx >> (2 * i)) & 3]; for (int k = 0; k < 4; k++) px[i][k] = (BYTE)q[k]; }
}

void EncodeColour(const BYTE px[16][4], BYTE* out, bool bc1Alpha)
{
    // the endpoints on the colours' principal axis (not the bounding box's diagonal): a block's colours lie along a
    // line in RGB, and the axis of their spread follows it - gradients and two-tone details keep their hues
    bool holes = false; int n = 0; float mean[3] = {};
    for (int i = 0; i < 16; i++) {
        if (bc1Alpha && px[i][3] < 128) { holes = true; continue; }
        n++; for (int k = 0; k < 3; k++) mean[k] += px[i][k];
    }
    if (!n) { memset(out, 0, 4); memset(out + 4, 0xFF, 4); return; }
    for (float& m : mean) m /= n;
    float cov[6] = {};      // rr rg rb gg gb bb
    for (int i = 0; i < 16; i++) {
        if (bc1Alpha && px[i][3] < 128) continue;
        const float d[3] = { px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2] };
        cov[0] += d[0] * d[0]; cov[1] += d[0] * d[1]; cov[2] += d[0] * d[2]; cov[3] += d[1] * d[1]; cov[4] += d[1] * d[2]; cov[5] += d[2] * d[2];
    }
    float ax[3] = { 1, 1, 1 };
    for (int it = 0; it < 8; it++) {         // power iteration: the dominant eigenvector
        const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
        const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
        const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
        const float l = sqrtf(x * x + y * y + z * z);
        if (l < 1e-6f) break;
        ax[0] = x / l; ax[1] = y / l; ax[2] = z / l;
    }
    { const float l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); for (float& a : ax) a /= std::max(l, 1e-6f); }
    float tmin = 1e9f, tmax = -1e9f;
    for (int i = 0; i < 16; i++) {
        if (bc1Alpha && px[i][3] < 128) continue;
        const float t = (px[i][0] - mean[0]) * ax[0] + (px[i][1] - mean[1]) * ax[1] + (px[i][2] - mean[2]) * ax[2];
        tmin = std::min(tmin, t); tmax = std::max(tmax, t);
    }
    const float inset = (tmax - tmin) / 16;      // the extremes pulled in a little: the four-step palette fits better
    tmin += inset; tmax -= inset;
    int mn[3], mx[3];
    for (int k = 0; k < 3; k++) {
        mn[k] = std::clamp((int)lrintf(mean[k] + ax[k] * tmin), 0, 255);
        mx[k] = std::clamp((int)lrintf(mean[k] + ax[k] * tmax), 0, 255);
    }
    uint16_t c0 = To565(mx), c1 = To565(mn);
    const bool three = bc1Alpha && holes;
    if (three ? c0 > c1 : c0 < c1) std::swap(c0, c1);
    int p[4][3]; Rgb565(c0, p[0]); Rgb565(c1, p[1]);
    for (int k = 0; k < 3; k++) {
        if (!three) { p[2][k] = (2 * p[0][k] + p[1][k]) / 3; p[3][k] = (p[0][k] + 2 * p[1][k]) / 3; }
        else { p[2][k] = (p[0][k] + p[1][k]) / 2; p[3][k] = 0; }
    }
    uint32_t idx = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, bd = INT_MAX;
        if (three && px[i][3] < 128) best = 3;
        else if (c0 != c1) for (int j = 0; j < (three ? 3 : 4); j++) {
            int d = 0; for (int k = 0; k < 3; k++) { int e = px[i][k] - p[j][k]; d += e * e; }
            if (d < bd) { bd = d; best = j; }
        }
        idx |= (uint32_t)best << (2 * i);
    }
    out[0] = (BYTE)c0; out[1] = (BYTE)(c0 >> 8); out[2] = (BYTE)c1; out[3] = (BYTE)(c1 >> 8);
    out[4] = (BYTE)idx; out[5] = (BYTE)(idx >> 8); out[6] = (BYTE)(idx >> 16); out[7] = (BYTE)(idx >> 24);
}

void DecodeAlphaBC3(const BYTE* b, BYTE px[16][4])
{
    int a[8]; a[0] = b[0]; a[1] = b[1];
    if (a[0] > a[1]) for (int k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a[0] + k * a[1]) / 7;
    else { for (int k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a[0] + k * a[1]) / 5; a[6] = 0; a[7] = 255; }
    uint64_t bits = 0; for (int k = 0; k < 6; k++) bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int i = 0; i < 16; i++) px[i][3] = (BYTE)a[(bits >> (3 * i)) & 7];
}

void EncodeAlphaBC3(const BYTE px[16][4], BYTE* out)
{
    int a0 = 0, a1 = 255;
    for (int i = 0; i < 16; i++) { a0 = std::max(a0, (int)px[i][3]); a1 = std::min(a1, (int)px[i][3]); }
    out[0] = (BYTE)a0; out[1] = (BYTE)a1;
    uint64_t bits = 0;
    if (a0 > a1) {
        int a[8]; a[0] = a0; a[1] = a1; for (int k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a0 + k * a1) / 7;
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = INT_MAX;
            for (int j = 0; j < 8; j++) { int d = abs(px[i][3] - a[j]); if (d < bd) { bd = d; best = j; } }
            bits |= (uint64_t)best << (3 * i);
        }
    }
    for (int k = 0; k < 6; k++) out[2 + k] = (BYTE)(bits >> (8 * k));
}

// the average of four texels; a colour channel of an sRGB texture averaged as light (in linear), as DirectXTex does:
// the mips keep the texture's brightness instead of darkening with the distance
BYTE Avg4(BYTE a, BYTE b, BYTE c, BYTE d, bool srgb)
{
    if (!srgb) return (BYTE)((a + b + c + d + 2) / 4);
    static const std::array<float, 256> lin = [] {      // (a function's static: made once, safe for the loader threads)
        std::array<float, 256> t;
        for (int i = 0; i < 256; i++) { float v = i / 255.0f; t[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
        return t;
    }();
    static const std::array<BYTE, 4097> enc = [] {      // linear -> sRGB byte in 4096 steps (within one count of powf)
        std::array<BYTE, 4097> t;
        for (int i = 0; i <= 4096; i++) {
            float l = i / 4096.0f, v = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f;
            t[i] = (BYTE)std::clamp((int)(v * 255 + 0.5f), 0, 255);
        }
        return t;
    }();
    float l = 0.25f * (lin[a] + lin[b] + lin[c] + lin[d]);
    return enc[std::min((int)(l * 4096 + 0.5f), 4096)];
}

// the full chain for a BC1/2/3 top level stored alone; false: not such a format (the caller keeps the one level)
bool BuildBlockMips(DXGI_FORMAT fmt, const BYTE* top, UINT W, UINT H, std::vector<std::vector<BYTE>>& levels)
{
    const bool bc1 = fmt == DXGI_FORMAT_BC1_UNORM || fmt == DXGI_FORMAT_BC1_UNORM_SRGB;
    const bool bc2 = fmt == DXGI_FORMAT_BC2_UNORM || fmt == DXGI_FORMAT_BC2_UNORM_SRGB;
    const bool bc3 = fmt == DXGI_FORMAT_BC3_UNORM || fmt == DXGI_FORMAT_BC3_UNORM_SRGB;
    if (!bc1 && !bc2 && !bc3) return false;
    const bool srgbF = fmt == DXGI_FORMAT_BC1_UNORM_SRGB || fmt == DXGI_FORMAT_BC2_UNORM_SRGB || fmt == DXGI_FORMAT_BC3_UNORM_SRGB;
    static const bool off = [] { char v[4] = {}; return GetEnvironmentVariableA("D3D11CLIENT_NOMIPGEN", v, 4) && v[0] == '1'; }();   // test runs: A/B
    if (off) return false;
    const size_t bb = bc1 ? 8 : 16;
    const UINT bw = (W + 3) / 4, bh = (H + 3) / 4;
    // the work images are each thread's own, kept from texture to texture (the tile loaders make thousands: a fresh 1 MB
    // and 256 KB per tile churned the heap and shredded the 32-bit address space); levels keep their buffers too
    static thread_local std::vector<BYTE> tImg, tNext;
    std::vector<BYTE>& img = tImg;
    img.assign((size_t)W * H * 4, 0);
    for (UINT by = 0; by < bh; by++) for (UINT bx = 0; bx < bw; bx++) {
        const BYTE* b = top + ((size_t)by * bw + bx) * bb;
        BYTE px[16][4];
        DecodeColour(b + (bb - 8), px, bc1);
        if (bc2) for (int i = 0; i < 16; i++) px[i][3] = (BYTE)(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
        if (bc3) DecodeAlphaBC3(b, px);
        for (int i = 0; i < 16; i++) {
            UINT x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
            if (x < W && y < H) memcpy(&img[((size_t)y * W + x) * 4], px[i], 4);
        }
    }
    UINT w = W, h = H;
    size_t nl = 0;
    auto next = [&]() -> std::vector<BYTE>& { if (levels.size() <= nl) levels.emplace_back(); return levels[nl++]; };
    next().assign(top, top + (size_t)bw * bh * bb);
    while (w > 1 || h > 1) {
        UINT nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<BYTE>& n = tNext;
        n.resize((size_t)nw * nh * 4);
        for (UINT y = 0; y < nh; y++) for (UINT x = 0; x < nw; x++) for (int c = 0; c < 4; c++) {
            UINT x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1), y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
            const BYTE a0 = img[((size_t)y0 * w + x0) * 4 + c], a1 = img[((size_t)y0 * w + x1) * 4 + c], a2 = img[((size_t)y1 * w + x0) * 4 + c], a3 = img[((size_t)y1 * w + x1) * 4 + c];
            n[((size_t)y * nw + x) * 4 + c] = Avg4(a0, a1, a2, a3, srgbF && c < 3);
        }
        img.swap(n); w = nw; h = nh;
        const UINT cw = (w + 3) / 4, ch = (h + 3) / 4;
        std::vector<BYTE>& enc = next();
        enc.resize((size_t)cw * ch * bb);
        for (UINT by = 0; by < ch; by++) for (UINT bx = 0; bx < cw; bx++) {
            BYTE px[16][4];
            for (int i = 0; i < 16; i++) {
                UINT x = std::min(w - 1, bx * 4 + (i & 3)), y = std::min(h - 1, by * 4 + (i >> 2));
                memcpy(px[i], &img[((size_t)y * w + x) * 4], 4);
            }
            BYTE* o = &enc[((size_t)by * cw + bx) * bb];
            if (bc2) { for (int i = 0; i < 8; i++) o[i] = (BYTE)(((px[2 * i][3] + 8) / 17) | (((px[2 * i + 1][3] + 8) / 17) << 4)); }
            if (bc3) EncodeAlphaBC3(px, o);
            EncodeColour(px, o + (bb - 8), bc1);
        }
    }
    levels.resize(nl);      // (a smaller texture than the last: the extra levels go)
    return true;
}
}  // namespace

static ComPtr<ID3D11ShaderResourceView> CreateTexture2D(ID3D11Device* dev, UINT W, UINT H, DXGI_FORMAT fmt, const void* data, UINT pitch);

// the mips made for a DXT texture saved without them, written once to Modules\D3D11Client\mipcache (the file's path,
// size and time in the name): the next start reads them ready instead of making them again (4096 textures take ~0.1 s)
static thread_local std::vector<BYTE>* t_mipOut = nullptr;
static std::string MipCachePath(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return {};
    std::string n = path;
    for (char& ch : n) if (ch == '\\' || ch == '/' || ch == ':' || ch == ' ') ch = '_';
    char tail[64]; sprintf_s(tail, "_%lu_%08lx%08lx.dds", fa.nFileSizeLow, fa.ftLastWriteTime.dwHighDateTime, fa.ftLastWriteTime.dwLowDateTime);
    return "Modules\\D3D11Client\\mipcache\\" + n + tail;
}

ComPtr<ID3D11ShaderResourceView> LoadDDSFile(ID3D11Device* dev, ID3D11DeviceContext*, const char* path, bool srgb, UINT* ow, UINT* oh)
{
    std::vector<BYTE> f;
    const std::string cache = MipCachePath(path);
    if (!cache.empty() && ReadFileBytes(cache.c_str(), f)) {
        if (auto srv = LoadDDSMemory(dev, f.data(), f.size(), srgb, ow, oh, false)) return srv;
    }
    if (!ReadFileBytes(path, f)) return nullptr;
    std::vector<BYTE> made;
    t_mipOut = cache.empty() ? nullptr : &made;
    auto srv = LoadDDSMemory(dev, f.data(), f.size(), srgb, ow, oh, true);   // a texture saved without mips gets them (as D3DX gives D3D9: no shimmer)
    t_mipOut = nullptr;
    if (srv && !made.empty()) {
        CreateDirectoryA("Modules\\D3D11Client\\mipcache", nullptr);
        if (FILE* o = fopen(cache.c_str(), "wb")) { fwrite(made.data(), 1, made.size(), o); fclose(o); }
    }
    return srv;
}

// whether the last texture this thread loaded has see-through texels at its top level (alpha under 250): 1 yes, 0 no,
// -1 not known (BC7, BC6H). A vessel's group with an opaque texture goes into the depth prepass (Mesh.cpp): one with
// holes or a soft edge must not hide what lies behind it before it is blended
thread_local int g_ddsAlpha = -1;
static int TopAlpha(DXGI_FORMAT fmt, const BYTE* top, UINT W, UINT H)
{
    const size_t nb = (size_t)std::max<UINT>(1, (W + 3) / 4) * std::max<UINT>(1, (H + 3) / 4);
    switch (fmt) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
        for (size_t i = 0; i < nb; i++) {     // the 1-bit alpha: colour0 <= colour1 and an index 3 in the block
            const BYTE* b = top + i * 8;
            if ((b[0] | b[1] << 8) > (b[2] | b[3] << 8)) continue;
            const uint32_t ix = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
            for (int k = 0; k < 16; k++) if (((ix >> (2 * k)) & 3) == 3) return 1;
        }
        return 0;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
        for (size_t i = 0; i < nb; i++) { const BYTE* b = top + i * 16; for (int k = 0; k < 8; k++) if (b[k] < 0xF0 || (b[k] & 0x0F) < 0x0F) return 1; }
        return 0;
    case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
        for (size_t i = 0; i < nb; i++) {
            const BYTE* b = top + i * 16;
            if (b[0] >= 250 && b[1] >= 250) continue;
            BYTE px[16][4]; DecodeAlphaBC3(b, px);
            for (int k = 0; k < 16; k++) if (px[k][3] < 250) return 1;
        }
        return 0;
    case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: return 0;
    default: return -1;
    }
}

ComPtr<ID3D11ShaderResourceView> LoadDDSMemory(ID3D11Device* dev, const BYTE* fdata, size_t fsize, bool srgb, UINT* ow, UINT* oh, bool genMips)
{
    g_ddsAlpha = -1;
    struct View { const BYTE* p; size_t n; const BYTE* data() const { return p; } size_t size() const { return n; } } f{ fdata, fsize };
    if (f.size() < 128 || memcmp(f.data(), "DDS ", 4) != 0) return nullptr;
    const Header& h = *(const Header*)(f.data() + 4);
    size_t off = 128;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    bool swizzle24 = false;
    if (h.pf.flags & 4) {
        switch (h.pf.fourCC) {
        case FCC('D', 'X', 'T', '1'): fmt = DXGI_FORMAT_BC1_UNORM; break;
        case FCC('D', 'X', 'T', '2'): case FCC('D', 'X', 'T', '3'): fmt = DXGI_FORMAT_BC2_UNORM; break;
        case FCC('D', 'X', 'T', '4'): case FCC('D', 'X', 'T', '5'): fmt = DXGI_FORMAT_BC3_UNORM; break;
        case FCC('A', 'T', 'I', '1'): case FCC('B', 'C', '4', 'U'): fmt = DXGI_FORMAT_BC4_UNORM; break;
        case FCC('A', 'T', 'I', '2'): case FCC('B', 'C', '5', 'U'): fmt = DXGI_FORMAT_BC5_UNORM; break;
        case FCC('D', 'X', '1', '0'): {
            const HeaderDX10& x = *(const HeaderDX10*)(f.data() + 128);
            fmt = (DXGI_FORMAT)x.fmt; off += sizeof(HeaderDX10);
            break;
        }
        default: return nullptr;
        }
    } else if (h.pf.bits == 16 || h.pf.bits == 8) {
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM;    // converted by the channel masks below
    } else if (h.pf.bits == 32) {
        fmt = h.pf.rmask == 0x00ff0000 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (!(h.pf.flags & 1)) { /* no alpha: force opaque below */ }
    } else if (h.pf.bits == 24) {
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM; swizzle24 = true;
    } else return nullptr;
    if (srgb) fmt = ToSRGB(fmt);

    UINT W = h.width, H = h.height;
    if (ow) *ow = W;
    if (oh) *oh = H;
    UINT mips = (h.flags & 0x20000) && h.mips ? h.mips : 1;
    size_t bb = BlockBytes(fmt);
    bool noAlpha32 = h.pf.bits == 32 && !(h.pf.flags & 1) && !(h.pf.flags & 4);

    if (bb) {
        // drop mips below 4x4 that some writers leave out
        std::vector<D3D11_SUBRESOURCE_DATA> sd;
        size_t o = off;
        UINT w = W, hh = H, used = 0;
        for (UINT m = 0; m < mips; m++) {
            size_t pitch = std::max<UINT>(1, (w + 3) / 4) * bb;
            size_t sz = pitch * std::max<UINT>(1, (hh + 3) / 4);
            if (o + sz > f.size()) break;
            sd.push_back({ f.data() + o, (UINT)pitch, 0 });
            o += sz; used++;
            w = std::max(1u, w / 2); hh = std::max(1u, hh / 2);
        }
        if (!used) return nullptr;
        // block formats need sizes in whole blocks at the top level
        if ((W & 3) || (H & 3)) { if (W >= 4 && H >= 4) return nullptr; }
        g_ddsAlpha = TopAlpha(fmt, f.data() + off, W, H);
        static thread_local std::vector<std::vector<BYTE>> gen;   // a DXT texture saved without mips: they are made here (the buffers kept per thread)
        if (genMips && used == 1 && W >= 8 && H >= 8 && BuildBlockMips(fmt, f.data() + off, W, H, gen)) {
            sd.clear();
            UINT w2 = W;
            for (auto& l : gen) { sd.push_back({ l.data(), (UINT)(std::max<UINT>(1, (w2 + 3) / 4) * bb), 0 }); w2 = std::max(1u, w2 / 2); }
            used = (UINT)gen.size();
            if (t_mipOut) {         // the file with its mips, for the cache (LoadDDSFile): the header told of them
                t_mipOut->assign(f.data(), f.data() + off);
                Header& ch = *(Header*)(t_mipOut->data() + 4);
                ch.flags |= 0x20000; ch.mips = used; ch.caps[0] |= 0x400008;
                for (auto& l : gen) t_mipOut->insert(t_mipOut->end(), l.begin(), l.end());
            }
        }
        D3D11_TEXTURE2D_DESC td = { W, H, used, 1, fmt, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(dev->CreateTexture2D(&td, sd.data(), &tex))) return nullptr;
        ComPtr<ID3D11ShaderResourceView> srv;
        dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
        return srv;
    }
    // uncompressed: top level, mips generated on the GPU
    std::vector<uint8_t> px((size_t)W * H * 4);
    const uint8_t* s = f.data() + off;
    if (h.pf.bits == 16 || h.pf.bits == 8) {
        // 16-bit (A4R4G4B4, R5G6B5, A1R5G5B5, ...) or 8-bit luminance: expand each channel by its mask
        size_t bpp = h.pf.bits / 8;
        if (off + (size_t)W * H * bpp > f.size()) return nullptr;
        auto chan = [](uint32_t v, uint32_t mask) -> uint8_t {
            if (!mask) return 0;
            int sh = 0; while (!((mask >> sh) & 1)) sh++;
            uint32_t m = mask >> sh, x = (v & mask) >> sh;
            return (uint8_t)((x * 255 + m / 2) / m);
        };
        bool lum = (h.pf.flags & 0x20000) != 0;
        for (size_t i = 0; i < (size_t)W * H; i++) {
            uint32_t v = bpp == 2 ? (uint32_t)(s[i * 2] | (s[i * 2 + 1] << 8)) : s[i];
            if (lum) { uint8_t l = chan(v, h.pf.rmask ? h.pf.rmask : 0xFF); px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = l; px[i * 4 + 3] = 255; continue; }
            px[i * 4] = chan(v, h.pf.rmask); px[i * 4 + 1] = chan(v, h.pf.gmask); px[i * 4 + 2] = chan(v, h.pf.bmask);
            px[i * 4 + 3] = (h.pf.flags & 1) && h.pf.amask ? chan(v, h.pf.amask) : 255;
        }
    } else if (swizzle24) {
        for (size_t i = 0; i < (size_t)W * H; i++) { px[i * 4] = s[i * 3 + 2]; px[i * 4 + 1] = s[i * 3 + 1]; px[i * 4 + 2] = s[i * 3]; px[i * 4 + 3] = 255; }
    } else {
        if (off + px.size() > f.size()) return nullptr;
        memcpy(px.data(), s, px.size());
        if (noAlpha32) for (size_t i = 0; i < (size_t)W * H; i++) px[i * 4 + 3] = 255;
    }
    g_ddsAlpha = 0;
    for (size_t i = 0; i < (size_t)W * H; i++) if (px[i * 4 + 3] < 250) { g_ddsAlpha = 1; break; }
    return CreateTexture2D(dev, W, H, fmt, px.data(), W * 4);
}

static ComPtr<ID3D11ShaderResourceView> CreateTexture2D(ID3D11Device* dev, UINT W, UINT H, DXGI_FORMAT fmt, const void* data, UINT pitch)
{
    // the mip chain by 2x2 box filtering on the CPU (the loader thread has no device context)
    std::vector<std::vector<BYTE>> lv;
    std::vector<D3D11_SUBRESOURCE_DATA> sd;
    lv.emplace_back((const BYTE*)data, (const BYTE*)data + (size_t)pitch * H);
    UINT w = W, h = H;
    while (w > 1 || h > 1) {
        UINT nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        const std::vector<BYTE>& a = lv.back();
        std::vector<BYTE> b((size_t)nw * nh * 4);
        for (UINT y = 0; y < nh; y++) for (UINT x = 0; x < nw; x++) for (int c = 0; c < 4; c++) {
            UINT x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1), y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
            int s4 = a[((size_t)y0 * w + x0) * 4 + c] + a[((size_t)y0 * w + x1) * 4 + c] + a[((size_t)y1 * w + x0) * 4 + c] + a[((size_t)y1 * w + x1) * 4 + c];
            b[((size_t)y * nw + x) * 4 + c] = (BYTE)((s4 + 2) / 4);
        }
        lv.push_back(std::move(b));
        w = nw; h = nh;
    }
    w = W;
    for (auto& l : lv) { sd.push_back({ l.data(), w * 4, 0 }); w = std::max(1u, w / 2); }
    D3D11_TEXTURE2D_DESC td = { W, H, (UINT)lv.size(), 1, fmt, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&td, sd.data(), &tex))) return nullptr;
    ComPtr<ID3D11ShaderResourceView> srv;
    dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    return srv;
}

bool ReadFileBytes(const char* path, std::vector<BYTE>& out)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    _fseeki64(fp, 0, SEEK_END);
    long long n = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
    out.resize((size_t)n);
    size_t r = n ? fread(out.data(), 1, (size_t)n, fp) : 0;
    fclose(fp);
    return r == (size_t)n;
}

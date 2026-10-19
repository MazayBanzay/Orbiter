// Minimal DDS loader: DXT1/3/5, ATI1/ATI2 (BC4/BC5), DX10 headers, 32/24-bit uncompressed. Mip chains as stored;
// uncompressed textures without mips get them generated.
#include "Client.h"

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
}  // namespace

static ComPtr<ID3D11ShaderResourceView> CreateTexture2D(ID3D11Device* dev, UINT W, UINT H, DXGI_FORMAT fmt, const void* data, UINT pitch);

ComPtr<ID3D11ShaderResourceView> LoadDDSFile(ID3D11Device* dev, ID3D11DeviceContext*, const char* path, bool srgb, UINT* ow, UINT* oh)
{
    std::vector<BYTE> f;
    if (!ReadFileBytes(path, f)) return nullptr;
    return LoadDDSMemory(dev, f.data(), f.size(), srgb, ow, oh);
}

ComPtr<ID3D11ShaderResourceView> LoadDDSMemory(ID3D11Device* dev, const BYTE* fdata, size_t fsize, bool srgb, UINT* ow, UINT* oh)
{
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

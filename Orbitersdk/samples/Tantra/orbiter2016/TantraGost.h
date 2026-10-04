// TantraGost: the lettering of the bridge's glass keys - GOST 2.304 type A (the drawing office's font, as in the helmet; the
// bridge mockup's choice, Tantra_Design/bridge_variants/v7.js). Windows' "GOST type A" through the sketchpad (TextW: Cyrillic);
// its widths are the font's own advances (to fit a label into its key, and to align it: the D3D9 pad does not).
#pragma once
#include "orbitersdk.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace tantra {

class GostFont {
public:
    GostFont() = default;
    GostFont(const GostFont&) = delete;
    GostFont& operator=(const GostFont&) = delete;
    ~GostFont() { for (auto& f : fonts_) if (f.second) oapiReleaseFont(f.second); }

    // (x, baseline y) in the surface's pixels, em the letter size (px), align 0 left / 1 centre / 2 right; rgb 0xRRGGBB
    void Text(oapi::Sketchpad* skp, int x, int y, const std::wstring& s, int em, unsigned rgb, int align) {
        if (s.empty() || em < 2) return;
        const bool gdi = skp->GetDC() != nullptr;                    // as TantraScreenFont: GDI wants the cell and BGR
        oapi::Font* f = For(gdi ? int(std::lround(em * 1.33)) : em, gdi);
        if (!f) return;
        skp->SetFont(f);
        skp->SetTextColor(gdi ? DWORD(((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu)) : DWORD(rgb & 0xFFFFFFu));
        skp->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
        if (!gdi && align) { x -= int(std::lround(Width(s, em) * (align == 1 ? 0.5 : 1.0))); align = 0; }   // the D3D9 pad ignores the alignment
        skp->SetTextAlign(align == 1 ? oapi::Sketchpad::CENTER : align == 2 ? oapi::Sketchpad::RIGHT : oapi::Sketchpad::LEFT, oapi::Sketchpad::BASELINE);
        std::wstring b = s;
        skp->TextW(x, y, &b[0], int(b.size()));
    }
    // the width of s at the letter size em: the font's own advances (GOST.TTF, 1/1000 em)
    static double Width(const std::wstring& s, double em) {
        static const unsigned short kAdv[][2] = {{0x0020, 293}, {0x0021, 146}, {0x0025, 830}, {0x0028, 195}, {0x0029, 195}, {0x002B, 342}, {0x002C, 146}, {0x002D, 391}, {0x002E, 122}, {0x002F, 537}, {0x0030, 439}, {0x0031, 293}, {0x0032, 439}, {0x0033, 391}, {0x0034, 439}, {0x0035, 391}, {0x0036, 439}, {0x0037, 439}, {0x0038, 439}, {0x0039, 439}, {0x003A, 144}, {0x003C, 391}, {0x003D, 342}, {0x003E, 391}, {0x003F, 415}, {0x0041, 488}, {0x0042, 439}, {0x0043, 391}, {0x0044, 439}, {0x0045, 391}, {0x0046, 391}, {0x0047, 439}, {0x0048, 439}, {0x0049, 146}, {0x004A, 342}, {0x004B, 439}, {0x004C, 342}, {0x004D, 537}, {0x004E, 439}, {0x004F, 439}, {0x0050, 439}, {0x0051, 488}, {0x0052, 439}, {0x0053, 439}, {0x0054, 439}, {0x0055, 439}, {0x0056, 488}, {0x0057, 684}, {0x0058, 488}, {0x0059, 488}, {0x005A, 439}, {0x00B0, 269}, {0x00B1, 391}, {0x00D7, 439}, {0x00F7, 391}, {0x0401, 391}, {0x0410, 488}, {0x0411, 439}, {0x0412, 439}, {0x0413, 391}, {0x0414, 488}, {0x0415, 391}, {0x0416, 537}, {0x0417, 439}, {0x0418, 439}, {0x0419, 439}, {0x041A, 439}, {0x041B, 488}, {0x041C, 537}, {0x041D, 439}, {0x041E, 439}, {0x041F, 439}, {0x0420, 439}, {0x0421, 391}, {0x0422, 439}, {0x0423, 439}, {0x0424, 635}, {0x0425, 488}, {0x0426, 488}, {0x0427, 439}, {0x0428, 537}, {0x0429, 586}, {0x042A, 537}, {0x042B, 488}, {0x042C, 439}, {0x042D, 439}, {0x042E, 488}, {0x042F, 439}, {0x0430, 391}, {0x0431, 391}, {0x0432, 391}, {0x0433, 391}, {0x0434, 391}, {0x0435, 391}, {0x0436, 488}, {0x0437, 366}, {0x0438, 391}, {0x0439, 391}, {0x043A, 391}, {0x043B, 391}, {0x043C, 439}, {0x043D, 391}, {0x043E, 391}, {0x043F, 391}, {0x0440, 391}, {0x0441, 391}, {0x0442, 537}, {0x0443, 391}, {0x0444, 537}, {0x0445, 391}, {0x0446, 439}, {0x0447, 391}, {0x0448, 537}, {0x0449, 586}, {0x044A, 488}, {0x044B, 439}, {0x044C, 391}, {0x044D, 391}, {0x044E, 439}, {0x044F, 391}, {0x0451, 391}, {0x0061, 391}, {0x0062, 391}, {0x0063, 391}, {0x0064, 391}, {0x0065, 391}, {0x0066, 293}, {0x0067, 391}, {0x0068, 391}, {0x0069, 146}, {0x006A, 146}, {0x006B, 391}, {0x006C, 244}, {0x006D, 537}, {0x006E, 391}, {0x006F, 391}, {0x0070, 391}, {0x0071, 391}, {0x0072, 342}, {0x0073, 391}, {0x0074, 293}, {0x0075, 391}, {0x0076, 391}, {0x0077, 586}, {0x0078, 391}, {0x0079, 391}, {0x007A, 391}};
        double a = 0.0;
        for (wchar_t c : s) {
            int w = 480;
            for (const auto& q : kAdv) if (q[0] == c) { w = q[1]; break; }
            a += w;
        }
        return a * em / 1000.0;
    }

private:
    oapi::Font* For(int h, bool gdi) {
        const int k = h * 2 + (gdi ? 1 : 0);
        for (auto& f : fonts_) if (f.first == k) return f.second;
        oapi::Font* f = oapiCreateFontEx(h, const_cast<char*>("GOST type A"), 0, 400);
        fonts_.push_back({k, f});
        return f;
    }
    std::vector<std::pair<int, oapi::Font*>> fonts_;
};

}  // namespace tantra

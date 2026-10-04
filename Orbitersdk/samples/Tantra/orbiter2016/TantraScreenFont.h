// TantraScreenFont: the text and the drawing objects of the bridge screens carried over from the HTML mockups (TantraPlantScreen
// and the screens made the same way). Segoe UI as the mockups' canvas draws it ("<weight> <size>px Segoe UI"): Windows fonts
// through the sketchpad (TextW - Cyrillic, the exact colour), the widths from the metrics tools/gen_plant_font.py writes
// (Config/Tantra/PlantFont.txt). Orbiter 2024's D3D9 client draws neither QuickPen / QuickBrush nor textured copies
// (StretchRect, a glyph atlas) on these surfaces - checked in the game, 2026-10-04 - so the brushes, pens and fonts are objects,
// kept here (one per ship) and released with it.
#pragma once
#include "orbitersdk.h"
#include "SkpCompat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tantra {

class ScreenFont {
public:
    ScreenFont() = default;
    ScreenFont(const ScreenFont&) = delete;
    ScreenFont& operator=(const ScreenFont&) = delete;
    ~ScreenFont() { ReleaseAll(); }

    bool Ok() { if (!tried_) Load(); return !specs_.empty(); }
    int Spec(int px, int weight) {
        Ok();
        int best = -1; double bd = 1e18;
        for (size_t i = 0; i < specs_.size(); ++i) {
            const double d = std::fabs(double(specs_[i].first - px)) * 1000.0 + std::fabs(double(specs_[i].second - weight));
            if (d < bd) { bd = d; best = int(i); }
        }
        return best;
    }
    double Width(const std::wstring& s, int spec) const {
        if (spec < 0 || spec >= int(map_.size())) return s.size() * 7.0;
        double a = 0.0;
        for (wchar_t c : s) { auto it = map_[spec].find(c); a += it != map_[spec].end() ? it->second : specs_[spec].first * 0.5; }
        return a;
    }
    int SpecPx(int spec) const { return spec >= 0 && spec < int(specs_.size()) ? specs_[spec].first : 0; }
    static DWORD Bgr(unsigned rgb) { return DWORD(((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu)); }
    // text: (x, baseline y) in the surface's pixels, px the CSS size, weight the CSS weight, align 0 left / 1 centre / 2 right
    void Text(oapi::Sketchpad* skp, int x, int y, const std::wstring& s, int px, int weight, unsigned rgb, int align) {
        // two kinds of pad in Orbiter 2024: a GDI one (a DC: COLORREF, the font height is the cell) and the D3D9 one (no DC:
        // the colour as 0xRRGGBB, the height is the em) - seen on the screens, 2026-10-04
        const bool gdi = skp->GetDC() != nullptr;
        oapi::Font* f = FontFor(gdi ? px : -px, weight);
        if (!f || s.empty()) return;
        skp->SetFont(f);
        skp->SetTextColor(gdi ? Bgr(rgb) : DWORD(rgb & 0xFFFFFFu));
        skp->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
        if (!gdi && align) {                                             // the D3D9 pad ignores the alignment (centred and right-aligned
            const int sp = Spec(px, weight), sp_px = SpecPx(sp);        // text ran from its anchor: the game's dump, 2026-10-04): by the metrics
            const double w = Width(s, sp) * (sp_px > 0 ? double(px) / sp_px : 1.0);
            x -= int(std::lround(align == 1 ? w / 2 : w)); align = 0;
        }
        skp->SetTextAlign(align == 1 ? oapi::Sketchpad::CENTER : align == 2 ? oapi::Sketchpad::RIGHT : oapi::Sketchpad::LEFT, oapi::Sketchpad::BASELINE);
        std::wstring b = s;
        skp->TextW(x, y, &b[0], int(b.size()));
    }
    // Segoe UI at a CSS size (the em; the cell is 1.33 em) and weight (500 -> regular, 800 -> black: the browser's choice)
    oapi::Font* FontFor(int px, int weight) {
        const int w = weight >= 800 ? 900 : weight >= 700 ? 700 : weight >= 600 ? 600 : 400;
        const long long k = (long long)px * 10000 + w;
        for (auto& f : fonts_) if (f.first == k) return f.second;
        oapi::Font* f = oapiCreateFontEx(px > 0 ? int(std::lround(px * 1.33)) : -px, const_cast<char*>("Segoe UI"), 0, w);   // < 0: the em
        fonts_.push_back({k, f});
        return f;
    }
    oapi::Brush* Brush(unsigned rgb) {
        auto it = brushes_.find(rgb);
        if (it != brushes_.end()) return it->second;
        oapi::Brush* b = oapiCreateBrush(Bgr(rgb));
        brushes_[rgb] = b;
        return b;
    }
    oapi::Pen* Pen(unsigned rgb, int w) {
        const unsigned long long k = (unsigned long long)rgb | ((unsigned long long)w << 32);
        auto it = pens_.find(k);
        if (it != pens_.end()) return it->second;
        oapi::Pen* p = oapiCreatePen(1, w, Bgr(rgb));
        pens_[k] = p;
        return p;
    }
    // at the start of a draw pass (nothing selected): the colour caches do not grow without end (blended shades)
    void BeginPass() {
        if (brushes_.size() > 600) { for (auto& b : brushes_) if (b.second) oapiReleaseBrush(b.second); brushes_.clear(); }
        if (pens_.size() > 600) { for (auto& p : pens_) if (p.second) oapiReleasePen(p.second); pens_.clear(); }
    }

private:
    void ReleaseAll() {
        for (auto& b : brushes_) if (b.second) oapiReleaseBrush(b.second);
        for (auto& p : pens_) if (p.second) oapiReleasePen(p.second);
        for (auto& f : fonts_) if (f.second) oapiReleaseFont(f.second);
        brushes_.clear(); pens_.clear(); fonts_.clear();
    }
    void Load() {
        tried_ = true;
        FILE* f = std::fopen("Config\\Tantra\\PlantFont.txt", "r");
        if (!f) { oapiWriteLog(const_cast<char*>("Tantra: Config\\Tantra\\PlantFont.txt missing - the screens' text widths are guessed")); return; }
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#') continue;
            if (line[0] == 'S') {
                int i, px, wt;
                if (std::sscanf(line + 1, "%d %d %d", &i, &px, &wt) == 3 && i >= 0 && i < 64) {
                    if (int(specs_.size()) <= i) { specs_.resize(i + 1); map_.resize(i + 1); }
                    specs_[i] = {px, wt};
                }
                continue;
            }
            int si, cp, x, y, w, h, ox, oy; double adv;
            if (std::sscanf(line, "%d %d %d %d %d %d %d %d %lf", &si, &cp, &x, &y, &w, &h, &ox, &oy, &adv) != 9) continue;
            if (si < 0 || si >= int(map_.size())) continue;
            map_[si][wchar_t(cp)] = adv;
        }
        std::fclose(f);
    }
    bool tried_ = false;
    std::vector<std::pair<int, int>> specs_;
    std::vector<std::unordered_map<wchar_t, double>> map_;   // advances
    std::vector<std::pair<long long, oapi::Font*>> fonts_;
    std::unordered_map<unsigned, oapi::Brush*> brushes_;
    std::unordered_map<unsigned long long, oapi::Pen*> pens_;
};

}  // namespace tantra

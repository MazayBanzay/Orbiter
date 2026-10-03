// TantraGlyphs: text on the ship's screens from the glyph atlas the crew's helmet display uses
// (Textures/Tantra/HudFont.dds + Config/Tantra/HudFont.txt, Jura, SIL OFL). D3D9Client draws system fonts on a surface with the
// Western charset only, so Cyrillic came out as "ÊÀÁÈÍÀ"; the atlas has the glyphs themselves. Text is given in cp1251.
// Atlas colours: 0 cyan, 1 orange, 2 white, 3 red, 4 dim blue, 5 brown, 6 grey. Sizes 0..3.
#pragma once
#include "orbitersdk.h"
#include "SkpCompat.h"   // Sketchpad2 on Orbiter 2016 and 2024

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

class TantraGlyphs {
public:
    bool Ok() { if (!tried_) Load(); return tex_ != nullptr; }

    // (x, top) in pixels; px = the wanted height of a capital letter in pixels; returns the width drawn
    double Draw(oapi::Sketchpad* skp, double x, double top, const char* cp1251, int size, int colour, double px) {
        if (!Ok() || !cp1251) return 0.0;
        auto* s2 = tantra::AsSkp2(skp);
        if (!s2) return 0.0;
        const double f = px / capH_[size];
        const double base = top + px;                                    // the baseline under the capitals
        wchar_t w[256];
        const int n = MultiByteToWideChar(1251, 0, cp1251, -1, w, 256);
        double pen = x;
        for (int i = 0; i < n - 1; i++) {
            const Glyph* g = Find(size, colour, unsigned(w[i]));
            if (!g) continue;
            if (g->w > 0) {
                RECT src = {g->x, g->y, g->x + g->w, g->y + g->h};
                const double dx = pen + g->ox * f, dy = base + g->oy * f;
                RECT dst = {LONG(std::lround(dx)), LONG(std::lround(dy)), LONG(std::lround(dx + g->w * f)), LONG(std::lround(dy + g->h * f))};
                s2->StretchRect(tex_, &src, &dst);
            }
            pen += g->adv * f;
        }
        return pen - x;
    }

    double Width(const char* cp1251, int size, double px) {
        if (!Ok() || !cp1251) return 0.0;
        wchar_t w[256];
        const int n = MultiByteToWideChar(1251, 0, cp1251, -1, w, 256);
        double a = 0.0;
        for (int i = 0; i < n - 1; i++) if (const Glyph* g = Find(size, 0, unsigned(w[i]))) a += g->adv;
        return a * px / capH_[size];
    }
    // align 0 left, 1 centre, 2 right at x
    double DrawA(oapi::Sketchpad* skp, double x, double top, const char* t, int size, int colour, double px, int align) {
        const double w = align ? Width(t, size, px) : 0.0;
        return Draw(skp, x - (align == 1 ? w / 2 : align == 2 ? w : 0.0), top, t, size, colour, px);
    }
    // the atlas colour nearest to a Sketchpad colour (0xBBGGRR)
    static int Colour(DWORD bgr) {
        static const int pal[7][3] = {{70, 226, 255}, {255, 148, 40}, {242, 248, 255}, {255, 70, 70}, {94, 142, 165}, {163, 123, 82}, {125, 141, 156}};
        const int r = bgr & 0xFF, g = (bgr >> 8) & 0xFF, b = (bgr >> 16) & 0xFF;
        int best = 2; long bd = 1L << 30;
        for (int i = 0; i < 7; i++) {
            const long d = long(r - pal[i][0]) * (r - pal[i][0]) + long(g - pal[i][1]) * (g - pal[i][1]) + long(b - pal[i][2]) * (b - pal[i][2]);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

private:
    struct Glyph { int x, y, w, h, ox, oy; float adv; };
    static unsigned long long Key(int s, int c, unsigned cp) { return (static_cast<unsigned long long>(s) << 40) | (static_cast<unsigned long long>(c) << 32) | cp; }
    const Glyph* Find(int s, int c, unsigned cp) const {
        const auto k = Key(s, c, cp);
        auto it = std::lower_bound(index_.begin(), index_.end(), std::make_pair(k, -1));
        return (it == index_.end() || it->first != k) ? nullptr : &glyphs_[it->second];
    }
    void Load() {
        tried_ = true;
        FILE* f = std::fopen("Config\\Tantra\\HudFont.txt", "r");
        if (!f) { oapiWriteLog(const_cast<char*>("Tantra: Config\\Tantra\\HudFont.txt missing - screen text falls back")); return; }
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == 'A') continue;
            int si, ci, x, y, w, h, ox, oy; unsigned cp; float adv;
            if (std::sscanf(line, "%d %d %u %d %d %d %d %d %d %f", &si, &ci, &cp, &x, &y, &w, &h, &ox, &oy, &adv) != 10) continue;
            index_.emplace_back(Key(si, ci, cp), int(glyphs_.size()));
            glyphs_.push_back({x, y, w, h, ox, oy, adv});
            if (cp == 'H' && ci == 0 && si >= 0 && si < 4) capH_[si] = double(-oy);   // the height of a capital above the baseline
        }
        std::fclose(f);
        std::sort(index_.begin(), index_.end());
        tex_ = oapiLoadTexture("Tantra\\HudFont.dds");
    }
    bool tried_ = false;
    SURFHANDLE tex_ = nullptr;
    std::vector<Glyph> glyphs_;
    std::vector<std::pair<unsigned long long, int>> index_;
    double capH_[4] = {16, 18, 20, 22};
};

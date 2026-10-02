// OrbiterCrew - the suit computer's helmet display (see SuitHud.h and Tantra_Design/suit_computer_mfd.html).
// Everything is laid out on 1280x720 display units: left parts hang on the left edge, right parts on the right edge,
// the rest on the centre, so any aspect ratio works. The MFD pages use their own 340x300 units, as in the mockup.
#include "SuitHud.h"
#include <Sketchpad2.h>
#include <gcAPI.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <map>
#include <cctype>
#include <windows.h>

namespace ocrew
{
	namespace
	{
		// ---- colours: three palettes over seven atlas colours ----
		enum Col { CP, CA, CW, CD, CR, CK };   // primary, accent (autopilot, target, caution), white, dim, red, black
		struct Palette { DWORD col[6]; int atlas[6]; };   // 0xAABBGGRR
		const Palette PALS[3] = {
			{ { 0xFFFFE246, 0xFF2894FF, 0xFFFFF8F2, 0xFFA58E5E, 0xFF4646FF, 0xFF000000 }, { 0, 1, 2, 4, 3, 4 } },   // cyan + orange
			{ { 0xFF2894FF, 0xFFFFE246, 0xFFFFF8F2, 0xFF527BA3, 0xFF4646FF, 0xFF000000 }, { 1, 0, 2, 5, 3, 5 } },   // orange + cyan
			{ { 0xFFFFF8F2, 0xFF2894FF, 0xFFFFFFFF, 0xFF9C8D7D, 0xFF4646FF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } } }; // white + orange
		// light-built: cold blue-white light; electroluminescent: flat blue-green. Accent and red stay as they are.
		const Palette LOOK_PAL[3] = {
			{},
			{ { 0xFFFFF2C4, 0xFF6ACFFF, 0xFFFFFEF4, 0xFFA8986F, 0xFF5A6AFF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } },
			{ { 0xFFC8FF5C, 0xFF4AB8FF, 0xFFF0FFC9, 0xFF728A2B, 0xFF4A5AFF, 0xFF000000 }, { 2, 1, 2, 6, 3, 6 } } };
		// the GRI ring: neon orange gas-discharge digits and lamps in dark helmet hardware (its own iron, not the light display)
		const Palette GRI_PAL = { { 0xFF3A8AFF, 0xFF4AC0FF, 0xFF70B0FF, 0xFF223A5A, 0xFF2A3BFF, 0xFF080A0B }, { 1, 1, 1, 5, 3, 5 } };
		// per role (primary, accent, white, dim, red, black): multiply the atlas glyph colour, or {0} = as baked
		const float LOOK_TINT[3][6][3] = {
			{},
			{ { 0.80f, 0.95f, 1.0f }, {}, { 0.95f, 1.0f, 1.0f }, { 0.75f, 0.95f, 1.1f }, {}, {} },
			{ { 0.36f, 1.0f, 0.78f }, {}, { 0.80f, 1.0f, 0.94f }, { 0.35f, 0.90f, 0.75f }, {}, {} } };
		const char* LOOK_NAME[3] = { "ГОЛО", "СВЕТОПОСТР", "ЭЛ" };
		const char* PAL_NAME[3] = { "ЦИАН + ОРАНЖ", "ОРАНЖ + ЦИАН", "БЕЛЫЙ + ОРАНЖ" };
		const char* PAL_NOTE[3] = { "основной циан, автопилот оранжевый", "основной оранжевый, автопилот циан", "основной белый, автопилот оранжевый" };
		const double SIZE_BASE[4] = { 9.5, 11, 13, 15 };
		const double ZOOM[10] = { 50, 100, 250, 500, 1000, 2500, 5000, 25000, 100000, 500000 };   // local map: outer ring, m

		using V2 = std::pair<double, double>;

		std::vector<unsigned> Decode(const std::string& s)
		{
			std::vector<unsigned> out;
			for (size_t i = 0; i < s.size();)
			{
				const unsigned char c = s[i];
				unsigned cp; int n;
				if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; } else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; } else { cp = c & 0x07; n = 4; }
				for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
				out.push_back(cp); i += n;
			}
			return out;
		}
		std::vector<std::string> Chars(const std::string& s)
		{
			std::vector<std::string> out;
			for (size_t i = 0; i < s.size();)
			{
				const unsigned char c = s[i];
				const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
				out.push_back(s.substr(i, n)); i += n;
			}
			return out;
		}
		unsigned long long Key(int size, int colour, unsigned cp) { return (static_cast<unsigned long long>(size) << 40) | (static_cast<unsigned long long>(colour) << 32) | cp; }

		std::string Upper(const std::string& utf8)
		{
			const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
			if (wn <= 1) return utf8;
			std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
			CharUpperBuffW(&w[0], wn - 1);
			const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
			std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
			if (!s.empty() && s.back() == '\0') s.pop_back();
			return s;
		}
		// Russian number: decimal comma, a real minus
		std::string Num(double v, int dec = 1, bool plus = false)
		{
			char b[48]; snprintf(b, sizeof b, "%.*f", dec, v);
			std::string s = b;
			for (char& c : s) if (c == '.') c = ',';
			if (s == "-0" || s == "-0,0" || s == "-0,00") s = s.substr(1);
			if (!s.empty() && s[0] == '-') s = "−" + s.substr(1);
			else if (plus && v > 0) s = "+" + s;
			return s;
		}
		std::string Dist(double m)
		{
			if (m < 100) return Num(m, 1) + " м";
			if (m < 1000) return Num(m, 0) + " м";
			if (m < 100000) return Num(m / 1000, m < 10000 ? 2 : 1) + " км";
			return Num(m / 1000, 0) + " км";
		}
		std::string Clock(double s)
		{
			if (s < 0 || s > 359999) return "—";
			char b[32];
			if (s < 3600) snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
			else snprintf(b, sizeof b, "%d ч %02d мин", static_cast<int>(s) / 3600, (static_cast<int>(s) / 60) % 60);
			return b;
		}
		std::string Hours(double h) { return h > 99 ? "> 99 ч" : Num(h, h < 10 ? 1 : 0) + " ч"; }
		const char* BodyRu(const std::string& n)
		{
			if (n == "Earth") return "Земля"; if (n == "Moon") return "Луна"; if (n == "Mars") return "Марс";
			if (n == "Venus") return "Венера"; if (n == "Titan") return "Титан"; if (n == "Mercury") return "Меркурий";
			return nullptr;
		}
		double Wrap(double a) { while (a > PI) a -= PI2; while (a < -PI) a += PI2; return a; }
		VECTOR3 Unit(const VECTOR3& v) { const double l = length(v); return l > 0 ? v / l : v; }
	}

	std::string Ru(const std::string& utf8)
	{
		if (utf8.empty()) return utf8;
		const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
		const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0'); WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
		if (!s.empty() && s.back() == '\0') s.pop_back();
		return s;
	}

	// ================= glyph atlas =================
	bool HudText::Load()
	{
		FILE* f = fopen("Config\\Tantra\\HudFont.txt", "r");
		if (!f) { oapiWriteLog(const_cast<char*>("OrbiterCrew: Config\\Tantra\\HudFont.txt missing - helmet display falls back to system text")); return false; }
		char line[256];
		while (fgets(line, sizeof line, f))
		{
			if (line[0] == '#') continue;
			int aw, ah, u;
			if (sscanf(line, "ATLAS %d %d %d", &aw, &ah, &u) == 3) { unit = u; continue; }
			int si, ci, x_, y_, w_, h_, ox, oy; unsigned cp; float adv;
			if (sscanf(line, "%d %d %u %d %d %d %d %d %d %f", &si, &ci, &cp, &x_, &y_, &w_, &h_, &ox, &oy, &adv) == 10)
			{
				index.emplace_back(Key(si, ci, cp), static_cast<int>(glyphs.size()));
				glyphs.push_back({ x_, y_, w_, h_, ox, oy, adv });
			}
		}
		fclose(f);
		std::sort(index.begin(), index.end());
		tex = oapiLoadTexture("Tantra\\HudFont.dds");
		if (!tex) oapiWriteLog(const_cast<char*>("OrbiterCrew: Textures\\Tantra\\HudFont.dds missing - helmet display falls back to system text"));
		return tex != nullptr;
	}

	const HudText::Glyph* HudText::Find(int size, int colour, unsigned cp) const
	{
		const auto k = Key(size, colour, cp);
		auto it = std::lower_bound(index.begin(), index.end(), std::make_pair(k, -1));
		if (it == index.end() || it->first != k) return nullptr;   // not in the atlas: nothing, never a '?'
		return &glyphs[it->second];
	}

	double HudText::Width(const std::string& utf8, int size, double scale) const
	{
		double w = 0;
		for (unsigned cp : Decode(utf8)) if (const Glyph* g = Find(size, 0, cp)) w += g->adv;
		return w * scale / unit;
	}

	bool HudText::Draw(oapi::Sketchpad* skp, double x, double y, const std::string& utf8, int size, int colour, int align, double scale) const
	{
		auto* s2 = dynamic_cast<oapi::Sketchpad2*>(skp);
		if (!tex || !s2) return false;
		const double f = scale / unit;
		double px = x - (align == 1 ? 0.5 : align == 2 ? 1.0 : 0.0) * Width(utf8, size, scale);
		for (unsigned cp : Decode(utf8))
		{
			const Glyph* g = Find(size, colour, cp);
			if (!g) continue;
			if (g->w > 0)
			{
				RECT src = { g->x, g->y, g->x + g->w, g->y + g->h };
				const double dx = px + g->ox * f, dy = y + g->oy * f;
				RECT dst = { static_cast<LONG>(std::lround(dx)), static_cast<LONG>(std::lround(dy)), static_cast<LONG>(std::lround(dx + g->w * f)), static_cast<LONG>(std::lround(dy + g->h * f)) };
				s2->StretchRect(tex, &src, &dst);
			}
			px += g->adv * f;
		}
		return true;
	}

	// ================= drawing =================
	class Gfx
	{
	public:
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, const Palette& p)   // a fixed palette (the GRI ring)
			: skp(skp), s2(dynamic_cast<oapi::Sketchpad2*>(skp)), o(o), txt(t), P(p), look(0) {}
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, int pal, int look = 0)
			: skp(skp), s2(dynamic_cast<oapi::Sketchpad2*>(skp)), o(o), txt(t), P(look ? LOOK_PAL[look] : PALS[pal]), look(look)
		{
			if (look && !o.gcTried) { o.gcTried = true; o.gcOk = gcInitialize(); }
			if (look && o.gcOk && gcSketchpadVersion(skp) == 2) s3 = static_cast<oapi::Sketchpad3*>(skp);
		}
		oapi::Sketchpad* skp; oapi::Sketchpad2* s2; SuitHud& o; const HudText& txt; const Palette& P;
		int look{}; oapi::Sketchpad3* s3{}; double shimmer{ 1 };   // light-built: a fine flicker of coherent light
		double ox{}, oy{}, u{ 1 }, boost{};   // boost 0..1: brighter surroundings, bolder lines
		// a shift of the whole display (pixels; 0 - it rides the view). world = true: signs projected through the camera
		double hx{}, hy{}; bool world{};
		double clipL{ -1e9 }, clipR{ 1e9 };   // the panel's inner edges in frame units: text is fitted inside
		void Frame(double x0, double y0, double scale) { ox = x0; oy = y0; u = scale; }
		double X(double x) const { return ox + x * u + (world ? 0.0 : hx); }
		double Y(double y) const { return oy + y * u + (world ? 0.0 : hy); }
		int IX(double x) const { return static_cast<int>(std::lround(X(x))); }
		int IY(double y) const { return static_cast<int>(std::lround(Y(y))); }
		DWORD C(int c, double a) const { return (P.col[c] & 0xFFFFFF) | (static_cast<DWORD>(std::clamp(a, 0.0, 1.0) * 255) << 24); }

		void Pen(int c, double w, double a, bool dash)
		{
			const double px = (std::max)(1.0, w * u * 0.8 * (1 + 0.6 * boost));
			if (s2) { s2->QuickPen(C(c, a), static_cast<float>(px), dash ? 2 : 1); return; }
			const unsigned long long key = (static_cast<unsigned long long>(P.col[c] & 0xFFFFFF) << 16) | (static_cast<int>(px) << 1) | (dash ? 1 : 0);
			oapi::Pen*& p = o.pens[key];
			if (!p) p = oapiCreatePen(dash ? 2 : 1, static_cast<int>(px), P.col[c] & 0xFFFFFF);
			skp->SetPen(p);
		}
		void NoPen() { if (s2) s2->QuickPen(0); else skp->SetPen(nullptr); }
		bool Brush(int c, double a)
		{
			if (s2) { s2->QuickBrush(C(c, a)); return true; }
			if (a < 0.5) { skp->SetBrush(nullptr); return false; }
			oapi::Brush*& b = o.brushes[P.col[c] & 0xFFFFFF];
			if (!b) b = oapiCreateBrush(P.col[c] & 0xFFFFFF);
			skp->SetBrush(b); return true;
		}
		void NoBrush() { if (s2) s2->QuickBrush(0); else skp->SetBrush(nullptr); }

		void Line(double x1, double y1, double x2, double y2, int c = CP, double w = 1.2, double a = 1, bool dash = false, bool glow = true)
		{
			NoBrush();
			if (look == 1)
			{
				a *= shimmer;
				if (glow && s2 && !dash) { Pen(c, w + 5, a * 0.10, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); Pen(c, w + 1.6, a * 0.35, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); }
				Pen(c, w * 0.85, a, dash); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2));
				return;
			}
			if (glow && s2 && !dash && look == 0) { Pen(c, w + 2.4, a * 0.2, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); }
			Pen(c, w, a, dash); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2));
		}
		void Rect(double x, double y, double w, double h, int sc, double sw = 1, double sa = 1, int fc = -1, double fa = 0)
		{
			if (look == 1 && fc >= 0 && fc != CK && fa >= 0.99)
			{
				// light-built, a solid fill (a button that is on): a double line of light round it, the inside left clear -
				// hatching it hid the label
				NoBrush(); Pen(fc, 1.8, shimmer, false); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h));
				const double in = 2.5 / u; Pen(fc, 1.0, 0.8 * shimmer, false); skp->Rectangle(IX(x + in), IY(y + in), IX(x + w - in), IY(y + h - in));
				NoBrush(); return;
			}
			if (look == 1 && fc >= 0 && fc != CK && fa > 0)
			{
				if (fa >= 0.15)
				{
					const double step = 3.2 / u;
					for (double xx = x + step / 2; xx < x + w; xx += step) Line(xx, y + 0.5, xx, y + h - 0.5, fc, 0.9, (std::min)(1.0, fa * 1.2), false, false);
				}
			}
			else if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			if (sc >= 0) { NoBrush(); Pen(sc, sw, sa * (look == 1 ? shimmer : 1), false); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			NoBrush();
		}
		void Circle(double cx, double cy, double r, int sc, double sw = 1.2, double sa = 1, int fc = -1, double fa = 0, bool dash = false)
		{
			if (fc >= 0 && fa > 0 && look == 1) { if (sc < 0) { sc = fc; sa = fa; } }   // light-built: the outline, not a disc
			else if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Ellipse(IX(cx - r), IY(cy - r), IX(cx + r), IY(cy + r)); }
			if (sc >= 0) { NoBrush(); Pen(sc, sw, sa, dash); skp->Ellipse(IX(cx - r), IY(cy - r), IX(cx + r), IY(cy + r)); }
			NoBrush();
		}
		void Poly(const std::vector<V2>& pts, int c, double w = 1.2, double a = 1, bool closed = false, bool dash = false)
		{
			if (pts.size() < 2) return;
			std::vector<oapi::IVECTOR2> v;
			for (const V2& p : pts) { oapi::IVECTOR2 q; q.x = IX(p.first); q.y = IY(p.second); v.push_back(q); }
			if (closed) v.push_back(v.front());
			NoBrush(); Pen(c, w, a, dash);
			skp->Polyline(v.data(), static_cast<int>(v.size()));
		}
		void Fill(const std::vector<V2>& pts, int c, double a)
		{
			if (look == 1 && pts.size() >= 3) { Poly(pts, c, 1.2, a, true); return; }   // light-built: drawn, not filled
			if (pts.size() < 3 || !Brush(c, a)) return;
			std::vector<oapi::IVECTOR2> v;
			for (const V2& p : pts) { oapi::IVECTOR2 q; q.x = IX(p.first); q.y = IY(p.second); v.push_back(q); }
			NoPen(); skp->Polygon(v.data(), static_cast<int>(v.size())); NoBrush();
		}
		static int SizeIdx(double sz) { return sz <= 10 ? 0 : sz <= 11.9 ? 1 : sz <= 13.9 ? 2 : 3; }
		void T(double x, double y, const std::string& s0, int c = CP, double sz = 12, int align = 0)
		{
			if (s0.empty()) return;
			std::string s = s0;
			if (clipR < 1e8)
			{
				const double avail = align == 0 ? clipR - x : align == 2 ? x - clipL : 2 * (std::min)(x - clipL, clipR - x);
				double w = TW(s, sz);
				if (w > avail && avail > 4)
				{
					if (w * 0.75 <= avail) sz *= avail / w;
					else
					{
						sz *= 0.75;
						while (s.size() > 1 && TW(s + "…", sz) > avail) { s.pop_back(); while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back(); if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0xC0) s.pop_back(); }
						s += "…";
					}
				}
			}
			const int si = SizeIdx(sz);
			if (look == 1 && c == CK) c = CA;
			if (boost > 1.05 && c == CD) c = CP;   // the maximum brightness: the dim labels are lit as the rest
			const float* tn = look ? LOOK_TINT[look][c] : nullptr;
			const bool tint = s3 && tn && (tn[0] > 0 || tn[1] > 0);
			if (tint) { const float k = look == 1 ? static_cast<float>(shimmer) : 1.0f; const oapi::FVECTOR4 b(tn[0] * k, tn[1] * k, tn[2] * k, 1.0f); s3->SetBrightness(&b); }
			const bool ok = txt.Draw(skp, X(x), Y(y), s, si, P.atlas[c], align, u * sz / SIZE_BASE[si]);
			if (tint) s3->SetBrightness(nullptr);
			if (ok) return;
			const int ph = (std::max)(8, static_cast<int>(sz * u * 1.3));
			oapi::Font*& f = o.fonts[ph];
			if (!f) f = oapiCreateFont(ph, true, const_cast<char*>("Arial"));
			skp->SetFont(f); skp->SetTextColor(P.col[c] & 0xFFFFFF);
			skp->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
			const std::string a = Ru(s); skp->Text(IX(x), IY(y - sz * 0.95), a.c_str(), static_cast<int>(a.size()));
		}
		double TW(const std::string& s, double sz) const
		{
			const int si = SizeIdx(sz);
			if (txt.Ok()) return txt.Width(s, si, sz / SIZE_BASE[si]);
			return Decode(s).size() * sz * 0.55;
		}
		// a label above a value
		void Cell(double x, double y, const std::string& lab, const std::string& val, int c = CW, int align = 0)
		{
			T(x, y, lab, CD, 9.5, align); T(x, y + 17, val, c, 15, align);
		}
		void Bar(double x, double y, double w, double h, double f, int c)
		{
			Rect(x, y, w, h, CD, 1, 0.6);
			f = std::clamp(f, 0.0, 1.0);
			if (f > 0.002) Rect(x + 1, y + 1, (w - 2) * f, h - 2, -1, 0, 0, c, 0.9);
		}
		void Hit(double x, double y, double w, double h, int kind, int arg = 0) { o.hits.push_back({ X(x), Y(y), X(x + w), Y(y + h), kind, arg }); }
		void Arrow(double x0, double y0, double x1, double y1, int c, double w = 1.6, double a = 1, bool dash = false)
		{
			const double dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
			if (l < 2) return;
			Line(x0, y0, x1, y1, c, w, a, dash);
			const double ux = dx / l, uy = dy / l;
			Tri(x1, y1, -8 * ux - 4 * uy, -8 * uy + 4 * ux, -8 * ux + 4 * uy, -8 * uy - 4 * ux, c, a);
		}
		void Tri(double x, double y, double dx1, double dy1, double dx2, double dy2, int c, double a = 1)
		{
			Fill({ { x, y }, { x + dx1, y + dy1 }, { x + dx2, y + dy2 } }, c, a);
		}
	};

	// everything a page needs
	struct Ctx
	{
		Gfx& g; const HudData& d; SuitHud& h; VESSEL* v; double W, H, k; bool blink;
	};

	// ================= state =================
	SuitHud::~SuitHud()
	{
		for (auto& p : pens) if (p.second) oapiReleasePen(p.second);
		for (auto& b : brushes) if (b.second) oapiReleaseBrush(b.second);
		for (auto& f : fonts) if (f.second) oapiReleaseFont(f.second);
		if (nvCam) gcDeleteCustomCamera(nvCam);   // cameras first, then their surface
		if (nvSrf) oapiDestroySurface(nvSrf);
	}

	void SuitHud::Detach()
	{
		if (nvCam) { gcDeleteCustomCamera(nvCam); nvCam = nullptr; }
	}

	void SuitHud::NightVision(oapi::Sketchpad* skp, VESSEL* v, double W, double H)
	{
		if (!gcTried) { gcTried = true; gcOk = gcInitialize(); }
		if (!gcOk || gcSketchpadVersion(skp) != 2) { nvNote = "ПНВ недоступен: нужен клиент D3D9"; return; }
		const int w = (std::max)(64, static_cast<int>(W) / 2), h = (std::max)(64, static_cast<int>(H) / 2);   // half resolution: grain hides it
		if (!nvSrf || w != nvW || h != nvH)
		{
			if (nvCam) { gcDeleteCustomCamera(nvCam); nvCam = nullptr; }
			if (nvSrf) oapiDestroySurface(nvSrf);
			nvSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
			nvW = w; nvH = h;
			if (nvSrf) oapiClearSurface(nvSrf);
		}
		if (!nvSrf) { nvNote = "ПНВ недоступен: нет поверхности"; return; }
		// the same eye as the main camera, in her own frame
		VECTOR3 cg, cd; oapiCameraGlobalPos(&cg); oapiCameraGlobalDir(&cd);
		MATRIX3 cr; oapiCameraRotationMatrix(&cr);
		MATRIX3 R; v->GetRotationMatrix(R);
		VECTOR3 pos, dir = tmul(R, cd), up = tmul(R, _V(cr.m12, cr.m22, cr.m32));
		v->Global2Local(cg, pos);
		up = up - dir * dotp(up, dir); { const double l = length(up); if (l > 1e-6) up = up / l; }
		nvCam = gcSetupCustomCamera(nvCam, v->GetHandle(), pos, dir, up, oapiCameraAperture(), nvSrf, 0xFF);
		if (!nvCam) { nvNote = "ПНВ недоступен: включите CustomCamMode в D3D9Client.cfg"; return; }
		gcCustomCameraOnOff(nvCam, true);
		// the gain suits the night side; in daylight the tube is washed out and throttles itself
		const double gain = sunElev > 0.05 && surface ? 1.2 : 4.0;
		nvNote = gain > 2 ? "ИК" : "ИК · засветка";
		auto* s3 = static_cast<oapi::Sketchpad3*>(skp);
		// short-wave infrared, as cameras see it today: a neutral grey picture, mild gamma, fine grain - not the green tube of the last century
		const oapi::FVECTOR4 bright(static_cast<float>(gain), static_cast<float>(gain), static_cast<float>(gain * 1.04), 1.0f);
		const oapi::FVECTOR4 gamma(0.55f, 0.55f, 0.55f, 1.0f), noise(0.10f, 0.10f, 0.10f, 0.0f);
		s3->SetBrightness(&bright); s3->SetRenderParam(SKP3_PRM_GAMMA, &gamma); s3->SetRenderParam(SKP3_PRM_NOISE, &noise);
		RECT src = { 0, 0, nvW, nvH }, dst = { 0, 0, static_cast<LONG>(W), static_cast<LONG>(H) };
		s3->StretchRect(nvSrf, &src, &dst);
		s3->SetBrightness(nullptr); s3->SetRenderParam(SKP3_PRM_GAMMA, nullptr); s3->SetRenderParam(SKP3_PRM_NOISE, nullptr);
	}

	std::string SuitHud::Save() const
	{
		char b[96];
		snprintf(b, sizeof b, "%d %d %d %d %d %d %d %d %d %.2f %d %d", pal, mode, autoMode ? 1 : 0, lpage, rpage, openL ? 1 : 0, openR ? 1 : 0, zoom, brightAuto ? 1 : 0, brightManual, look, mapMono ? 1 : 0);
		return b;
	}

	void SuitHud::Load(const std::string& line)
	{
		std::istringstream ss(line);
		int a[8] = { pal, mode, autoMode, lpage, rpage, openL, openR, zoom };
		for (int& x : a) ss >> x;
		pal = std::clamp(a[0], 0, 2); mode = std::clamp(a[1], 0, 3); autoMode = a[2] != 0; lpage = std::clamp(a[3], 0, L_COUNT - 1);
		rpage = std::clamp(a[4], 0, static_cast<int>(R_FLIGHT)); openL = a[5] != 0; openR = a[6] != 0; zoom = std::clamp(a[7], 0, 9);
		int ba = 1; double bm = brightManual;
		if (ss >> ba >> bm) { brightAuto = ba != 0; brightManual = std::clamp(bm, 0.0, 1.5); }
		int lk = 0; if (ss >> lk) look = std::clamp(lk, 0, 2);
		int mm = 0; if (ss >> mm) mapMono = mm != 0;
	}

	void SuitHud::SetMode(int m, bool manual)
	{
		mode = m;
		if (manual) autoMode = false;
		static const int LP[4] = { L_LOCAL, L_LOCAL, L_ORBIT, L_POWER };
		static const int RPs[4] = { R_TARGETS, R_TRANSFER, R_APPROACH, R_BODY };
		lpage = LP[m]; rpage = RPs[m];
		if (m == RDV && surface) rpage = R_TARGETS;
		if (m == FLIGHT && space) rpage = R_APPROACH;
	}

	std::vector<SuitHud::RPage> SuitHud::RPages() const
	{
		if (surface) return { R_TARGETS, R_FLIGHT, R_TRANSFER, R_LANDING, R_BODY };
		return { R_TARGETS, R_APPROACH, R_DOCK, R_BODY };
	}

	const SuitHud::Target* SuitHud::Selected() const
	{
		for (const Target& t : targets) if (t.h == sel) return &t;
		return nullptr;
	}

	void SuitHud::Click(double x, double y)
	{
		for (auto it = hits.rbegin(); it != hits.rend(); ++it)
		{
			if (x < it->x0 || x > it->x1 || y < it->y0 || y > it->y1) continue;
			switch (it->kind)
			{
			case H_LTAB: lpage = it->arg; openL = true; break;
			case H_RTAB: rpage = it->arg; openR = true; break;
			case H_LFOLD: openL = !openL; break;
			case H_RFOLD: openR = !openR; break;
			case H_TGT: if (it->arg >= 0 && it->arg < static_cast<int>(targets.size())) { sel = targets[it->arg].h == sel ? nullptr : targets[it->arg].h; rrHist.clear(); prof.t = -1; } break;   // again: let go
			case H_AP: request = it->arg; break;
			case H_PAL: pal = std::clamp(it->arg, 0, 2); break;
			case H_ZOOM: zoom = std::clamp(zoom + it->arg, 0, 9); grid.t = -1; break;
			case H_MODE: if (!autoMode && it->arg == mode) autoMode = true; else SetMode(it->arg, true); break;
			case H_NVG: nvg = !nvg; if (!nvg && nvCam) gcCustomCameraOnOff(nvCam, false); break;
			case H_ACK:
				if (it->arg < 0) { for (auto& a : alerts) if (a.active) a.ack = true; }
				else if (it->arg < static_cast<int>(alerts.size())) alerts[it->arg].ack = true;
				break;
			case H_LOOK: if (it->arg == 10) { mapMono = !mapMono; grid.t = -1; } else look = std::clamp(it->arg, 0, 2); break;
			case H_BRIGHT:
				if (it->arg == 0) brightAuto = !brightAuto;
				else { if (brightAuto) brightManual = ambient; brightAuto = false; brightManual = std::clamp(brightManual + 0.1 * it->arg, 0.0, 1.5); }   // above 1: the maximum (labels at full light)
				break;
			case H_NEXT:
				if (targets.empty()) break;
				{ size_t k = 0; for (; k < targets.size(); ++k) if (targets[k].h == sel) break; sel = k >= targets.size() ? targets[0].h : k + 1 < targets.size() ? targets[k + 1].h : nullptr; }
				rrHist.clear(); prof.t = -1; break;
			}
			return;
		}
	}

	// ================= caution & warning =================
	int SuitHud::Trend(int i, double thr) const
	{
		const auto& q = hist.v[i];
		if (q.size() < 7) return 0;
		const double dv = q.back() - q[q.size() - 7];   // over the last 30 s
		return dv > thr ? 1 : dv < -thr ? -1 : 0;
	}

	void SuitHud::Alerts(const HudData& d)
	{
		const double t = oapiGetSimTime();
		// radiation where she is: under an atmosphere almost nothing; low Earth orbit inside the field; the Moon and open space more
		char bn[64] = ""; if (hBody) oapiGetObjectName(hBody, bn, 64);
		const std::string b = bn;
		if (d.radValid) { doseRate = d.radRate * 1e6; dose = d.radDose * 1e6; }   // the radiation model (Sv -> uSv)
		else
		{
			// until the model is in: a rough rate by place
			if (!d.vacuum && d.airKPa > 20) doseRate = 0.15;
			else if (b == "Earth") doseRate = alt < 2.0e6 ? 20 : 80;
			else if (b == "Mars") doseRate = 25;
			else if (b == "Moon") doseRate = 55;
			else doseRate = 75;
			if (lastDoseT >= 0 && t > lastDoseT && t - lastDoseT < 60 && d.suit) dose += doseRate * (t - lastDoseT) / 3600.0;
		}
		lastDoseT = t;
		if (hist.t < 0 || t - hist.t >= 5 || t < hist.t)
		{
			hist.t = t;
			const double vals[5] = { d.o2, d.batt, d.sorbent, d.ppCO2, d.tInC };
			for (int i = 0; i < 5; ++i) { hist.v[i].push_back(vals[i]); if (hist.v[i].size() > 13) hist.v[i].erase(hist.v[i].begin()); }
		}

		struct C { const char* key; const char* tile; int level; std::string text; };
		std::vector<C> now;
		if (d.suit)
		{
			if (d.o2Vent) {}   // breathing the air around: the tank is a reserve
			else if (d.o2 < 0.10) now.push_back({ "o2", "O2", 2, "КИСЛОРОД " + Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours) });
			else if (d.o2 < 0.25) now.push_back({ "o2", "O2", 1, "КИСЛОРОД " + Num(100 * d.o2, 0) + " % · " + Hours(d.o2Hours) });
			const double pWarn = d.o2Vent ? 14 : 15, pCaut = d.o2Vent ? 17 : 25;   // in breathable air the helmet holds the air around (~21 kPa O2)
			if (d.ppO2 < pWarn) now.push_back({ "p", "ДАВЛ", 2, "ДАВЛЕНИЕ O2 " + Num(d.ppO2, 1) + " кПа" });
			else if (d.ppO2 < pCaut) now.push_back({ "p", "ДАВЛ", 1, "ДАВЛЕНИЕ O2 " + Num(d.ppO2, 1) + " кПа" });
			if (d.ppCO2 > 5) now.push_back({ "co2", "CO2", 2, "CO2 " + Num(d.ppCO2, 2) + " кПа" });
			else if (d.ppCO2 > 1) now.push_back({ "co2", "CO2", 1, "CO2 " + Num(d.ppCO2, 2) + " кПа" });
			if (d.sorbent < 0.10) now.push_back({ "sorb", "CO2", 2, "ПОГЛОТИТЕЛЬ " + Num(100 * d.sorbent, 0) + " %" });
			else if (d.sorbent < 0.25) now.push_back({ "sorb", "CO2", 1, "ПОГЛОТИТЕЛЬ " + Num(100 * d.sorbent, 0) + " %" });
			if (!d.powered || d.batt < 0.10) now.push_back({ "batt", "БАТ", 2, "БАТАРЕЯ " + Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) });
			else if (d.batt < 0.25) now.push_back({ "batt", "БАТ", 1, "БАТАРЕЯ " + Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) });
			if (!d.inSpec) now.push_back({ "env", "ТЕПЛО", 2, "СРЕДА ВНЕ ДОПУСКА " + Num(d.envC, 0, true) + " °C" });
			// the outside pressure against the shell's rating: amber from 75 %, red above; crushed - the shell is open
			if (d.suitBreached) now.push_back({ "shell", "ДАВЛ", 2, "ОБОЛОЧКА РАЗРУШЕНА · СРЕДА ВНУТРИ" });
			else if (!d.vacuum && d.airKPa > d.suitPMax) now.push_back({ "pout", "ДАВЛ", 2, "ДАВЛЕНИЕ СНАРУЖИ " + Num(d.airKPa, 0) + " кПа · ПРЕДЕЛ " + Num(d.suitPMax, 0) });
			else if (!d.vacuum && d.airKPa > 0.75 * d.suitPMax) now.push_back({ "pout", "ДАВЛ", 1, "ДАВЛЕНИЕ СНАРУЖИ " + Num(d.airKPa, 0) + " кПа · у предела " + Num(d.suitPMax, 0) });
			if (d.tInC > 30 || d.tInC < 12) now.push_back({ "tin", "ТЕПЛО", 2, "В СКАФАНДРЕ " + Num(d.tInC, 1) + " °C" });
			else if (std::abs(d.residualW) > 30) now.push_back({ "heat", "ТЕПЛО", 1, d.residualW > 0 ? "ОХЛАЖДЕНИЕ НЕ СПРАВЛЯЕТСЯ" : "ОБОГРЕВ НЕ СПРАВЛЯЕТСЯ" });
			if (d.coreC > 39.5 || d.coreC < 35) now.push_back({ "core", "ТЕПЛО", 2, "ТЕЛО " + Num(d.coreC, 1) + " °C" });
			else if (d.coreC > 38.5 || d.coreC < 35.8) now.push_back({ "core", "ТЕПЛО", 1, "ТЕЛО " + Num(d.coreC, 1) + " °C" });
			// the ground: a lunar day of +100..+120 C is normal for the suit; warn only near and above its rating
			if (d.hasGround && d.landed && d.groundC > d.ratedMaxC) now.push_back({ "ground", "ТЕПЛО", 2, "ГРУНТ " + Num(d.groundC, 0, true) + " °C · ВЫШЕ ДОПУСКА" });
			else if (d.hasGround && d.landed && d.groundC > d.ratedMaxC - 20) now.push_back({ "ground", "ТЕПЛО", 1, "ГРУНТ " + Num(d.groundC, 0, true) + " °C · у предела" });
			// rate: caution above 1 mSv/h, warning above 10 mSv/h (belts, flares); the dose of the outing: 100 / 500 mSv
			const std::string hint = d.fieldOn ? "" : " · ВКЛЮЧИТЕ ПОЛЕ";
			if (doseRate > 10000) now.push_back({ "rad", "РАДИАЦ", 2, "РАДИАЦИЯ " + Num(doseRate / 1000, 1) + " мЗв/ч" + hint });
			else if (doseRate > 1000) now.push_back({ "rad", "РАДИАЦ", 1, "РАДИАЦИЯ " + Num(doseRate / 1000, 2) + " мЗв/ч" + hint });
			if (dose > 2.5e5) now.push_back({ "dose", "РАДИАЦ", 2, "ДОЗА " + Num(dose / 1000, 0) + " мЗв" });
			else if (dose > 1e5) now.push_back({ "dose", "РАДИАЦ", 1, "ДОЗА " + Num(dose / 1000, 0) + " мЗв" });
			if (d.jet && d.jetFuel < 0.05) now.push_back({ "fuel", "РЕЗЕРВ", 2, "ТОПЛИВО РАНЦА " + Num(100 * d.jetFuel, 0) + " % · " + Num(d.jetDv, 0) + " м/с" });
			else if (d.jet && d.jetFuel < 0.15) now.push_back({ "fuel", "РЕЗЕРВ", 1, "ТОПЛИВО РАНЦА " + Num(100 * d.jetFuel, 0) + " % · " + Num(d.jetDv, 0) + " м/с" });
			// coming down too fast for the height left (the pack's own guard line): a warning, not a hint
			if (d.jet && d.jetFlying && d.alt < 25 && d.vs < -(1.0 + 0.8 * d.alt) - 1.5) now.push_back({ "sink", "", 2, "БЫСТРОЕ СНИЖЕНИЕ " + Num(-d.vs, 1) + " м/с" });
			if (d.jet && d.jetProtect) now.push_back({ "near", "", 1, "ЗЕМЛЯ БЛИЗКО" });
			if (d.jet && d.jetTerrain) now.push_back({ "terr", "", 1, "РЕЛЬЕФ ВПЕРЕДИ" });
			if (const Target* s = Selected(); s && space && s->dist < 30 && -s->rate > 0.25) now.push_back({ "zone", "", 1, "ЗОНА 30 м · СБЛИЖЕНИЕ БЫСТРЕЕ 0,25 м/с" });
		}
		if (!d.warning.empty()) now.push_back({ "bio", "", 2, d.warning });

		const double mjd = oapiGetSimMJD(), day = (mjd - std::floor(mjd)) * 86400;
		for (auto& a : alerts) a.seen = false;
		for (const C& c : now)
		{
			Alert* f = nullptr;
			for (auto& a : alerts) if (a.active && a.key == c.key) { f = &a; break; }
			if (f) { if (c.level > f->level) f->ack = false; f->level = c.level; f->text = c.text; f->tile = c.tile; f->seen = true; }
			else alerts.insert(alerts.begin(), Alert{ c.key, c.tile, c.text, c.level, day, false, true, true });
		}
		for (auto& a : alerts) if (a.active && !a.seen) a.active = false;
		while (alerts.size() > 40) alerts.pop_back();
	}

	// ================= navigation =================
	void SuitHud::Nav(VESSEL* v, const HudData& d)
	{
		const double t = oapiGetSimTime();
		hBody = v->GetSurfaceRef(); if (!hBody) hBody = v->GetGravityRef();
		if (!hBody) return;
		bodyR = oapiGetSize(hBody);
		VECTOR3 gp, bp; v->GetGlobalPos(gp); oapiGetGlobalPos(hBody, &bp);
		double rad; oapiGlobalToEqu(hBody, gp, &lng, &lat, &rad);
		alt = v->GetAltitude(ALTMODE_GROUND);
		oapiGetHeading(v->GetHandle(), &hdg); pitch = v->GetPitch(); bank = v->GetBank();
		v->GetGroundspeedVector(FRAME_HORIZON, gsH);
		surface = d.landed || alt < 5000; space = !surface;
		const VECTOR3 up = Unit(gp - bp);
		VECTOR3 sp; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp);
		sunElev = std::asin(std::clamp(dotp(up, Unit(sp - gp)), -1.0, 1.0));

		// trail on the ground
		if (t - lastTrail > 0.5 || t < lastTrail)
		{
			lastTrail = t;
			if (trail.empty() || std::hypot((lat - trail.back().first) * bodyR, (lng - trail.back().second) * bodyR * std::cos(lat)) > 0.4)
			{
				trail.emplace_back(lat, lng);
				if (trail.size() > 16) trail.erase(trail.begin());
			}
		}

		// everything around: vessels, dropped packs, other crew
		targets.clear();
		int packs = 0;
		const DWORD n = oapiGetVesselCount();
		for (DWORD i = 0; i < n; ++i)
		{
			OBJHANDLE h = oapiGetVesselByIndex(i);
			if (!h || h == v->GetHandle()) continue;
			VESSEL* o = oapiGetVesselInterface(h);
			if (!o) continue;
			Target tg; tg.h = h;
			v->GetRelativePos(h, tg.rel); v->GetRelativeVel(h, tg.relV);
			tg.dist = length(tg.rel);
			if (tg.dist > 50e3) continue;
			tg.rate = tg.dist > 0 ? dotp(tg.rel, tg.relV) / tg.dist : 0;
			tg.landed = (o->GetFlightStatus() & 1) != 0;
			std::string cls = o->GetClassName() ? o->GetClassName() : "";
			std::transform(cls.begin(), cls.end(), cls.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			const std::string crew = CrewDisplayName(h);
			if (cls.find("jetpack") != std::string::npos)
			{
				tg.pack = true; ++packs;
				double fuel = 0;
				if (o->GetPropellantCount() > 0) { PROPELLANT_HANDLE ph = o->GetPropellantHandleByIndex(0); const double mx = o->GetPropellantMaxMass(ph); fuel = mx > 0 ? o->GetPropellantMass(ph) / mx : 0; }
				tg.name = "РАНЕЦ";
				tg.kind = std::string(tg.landed ? "лежит · " : "плывёт · ") + "топливо " + Num(100 * fuel, 0) + " %";
			}
			else if (!crew.empty()) { tg.crew = true; tg.name = Upper(crew); tg.kind = "экипаж"; }
			else { tg.name = o->GetName(); tg.kind = tg.landed ? "аппарат · на грунте" : "аппарат"; }
			VECTOR3 og; oapiGetGlobalPos(h, &og);
			double olng, olat, orad; oapiGlobalToEqu(hBody, og, &olng, &olat, &orad);
			tg.north = (olat - lat) * bodyR; tg.east = Wrap(olng - lng) * bodyR * std::cos(lat); tg.up = orad - rad;
			targets.push_back(tg);
		}
		if (v->GetSurfaceRef() == hBody)
			for (DWORD b = 0; b < oapiGetBaseCount(hBody); ++b)
			{
				OBJHANDLE hb = oapiGetBaseByIndex(hBody, b);
				double blng, blat; oapiGetBaseEquPos(hb, &blng, &blat);
				Target tg; tg.h = hb; tg.base = tg.landed = true;
				tg.north = (blat - lat) * bodyR; tg.east = Wrap(blng - lng) * bodyR * std::cos(lat);
				if (std::hypot(tg.north, tg.east) > 15e3) continue;
				v->GetRelativePos(hb, tg.rel); v->GetRelativeVel(hb, tg.relV);
				tg.dist = length(tg.rel); tg.rate = tg.dist > 0 ? dotp(tg.rel, tg.relV) / tg.dist : 0;
				tg.up = -(rad - bodyR);
				char nm[256] = ""; oapiGetObjectName(hb, nm, 255);
				tg.name = Upper(nm); tg.pads = static_cast<int>(oapiGetBasePadCount(hb));
				tg.kind = tg.pads ? "база · площадок " + std::to_string(tg.pads) : "база";
				targets.push_back(tg);
			}
		std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.dist < b.dist; });
		if (targets.size() > 8) targets.resize(8);
		if (packs > 1)   // several packs: number them by distance
		{
			int k = 0;
			for (Target& tg : targets) if (tg.pack) tg.name = "РАНЕЦ " + std::to_string(++k);
		}
		if (sel && !Selected()) { sel = nullptr; rrHist.clear(); }   // out of reach: let go (none is ever chosen for her)

		// orbit
		orbitOk = tgtOrbitOk = false;
		if (space)
		{
			OBJHANDLE ref = v->GetGravityRef();
			orbitOk = v->GetElements(ref, el, &op, 0, FRAME_EQU) && el.e < 1;
			if (const Target* s = Selected())
			{
				VESSEL* o = s->base ? nullptr : oapiGetVesselInterface(s->h);
				tgtOrbitOk = o && o->GetElements(ref, tel, &top, 0, FRAME_EQU) && tel.e < 1;
				if (tgtOrbitOk)
				{
					VECTOR3 r1, v1, r2, v2; v->GetRelativePos(ref, r1); v->GetRelativeVel(ref, v1); o->GetRelativePos(ref, r2); o->GetRelativeVel(ref, v2);
					const VECTOR3 h1 = crossp(r1, v1), h2 = crossp(r2, v2);
					relInc = std::acos(std::clamp(dotp(Unit(h1), Unit(h2)), -1.0, 1.0));
				}
			}
		}

		// histories
		if (const Target* s = Selected())
			if (t - lastRR > 2 || t < lastRR) { lastRR = t; rrHist.emplace_back(s->dist, -s->rate); if (rrHist.size() > 200) rrHist.erase(rrHist.begin()); }
		if (t - trend.t > 10 || t < trend.t)
		{
			trend.t = t;
			auto push = [](std::vector<double>& q, double x) { q.push_back(x); if (q.size() > 61) q.erase(q.begin()); };
			push(trend.pulse, d.pulse); push(trend.breath, d.breath); push(trend.core, d.coreC);
		}

		if (surface)
		{
			const double half = ZOOM[zoom] * 1.2;
			const double moved = std::hypot((lat - grid.lat) * bodyR, (lng - grid.lng) * bodyR * std::cos(lat));
			if (grid.t < 0 || t - grid.t > 3 || t < grid.t || grid.half != half || moved > half / 6) Terrain(v);
			TerrainRows(*this, hBody, bodyR);
			if (sel && (prof.tgt != sel || t - prof.t > 1 || t < prof.t)) Profile(v);
		}
		lastNav = t;
	}

	void SuitHud::Terrain(VESSEL* v)
	{
		// start a new grid; the rows are filled over the next frames and it replaces the shown one when complete
		if (nextRow >= 0) return;
		next.n = 25; next.half = ZOOM[zoom] * 1.2; next.lat = lat; next.lng = lng; next.t = oapiGetSimTime();
		next.h.assign(next.n * next.n, 0);
		nextRow = 0;
		grid.t = next.t;   // no new request until this one is done
	}

	void TerrainRows(SuitHud& h, OBJHANDLE body, double bodyR)
	{
		auto& n = h.next;
		if (h.nextRow < 0) return;
		const double step = 2 * n.half / (n.n - 1), cl = (std::max)(0.05, std::cos(n.lat));
		for (int k = 0; k < 3 && h.nextRow < n.n; ++k, ++h.nextRow)   // 3 rows (75 queries) per frame
			for (int i = 0; i < n.n; ++i)
			{
				const double north = -n.half + h.nextRow * step, east = -n.half + i * step;
				n.h[h.nextRow * n.n + i] = oapiSurfaceElevation(body, n.lng + east / (bodyR * cl), n.lat + north / bodyR);
			}
		if (h.nextRow >= n.n) { h.grid = n; h.nextRow = -1; }
	}

	void SuitHud::Profile(VESSEL* v)
	{
		const Target* s = Selected();
		prof.tgt = sel; prof.t = oapiGetSimTime(); prof.h.clear();
		if (!s) return;
		prof.dist = std::hypot(s->north, s->east);
		prof.brg = std::atan2(s->east, s->north);
		const double cl = (std::max)(0.05, std::cos(lat));
		for (int i = 0; i <= 40; ++i)
		{
			const double f = i / 40.0;
			prof.h.push_back(oapiSurfaceElevation(hBody, lng + f * s->east / (bodyR * cl), lat + f * s->north / bodyR));
		}
	}

	// ================= MFD pages =================
	namespace
	{
		void Grid(Gfx& g)
		{
			for (int x = 20; x < 340; x += 20) g.Line(x, 0, x, 300, CP, 0.6, 0.07, false, false);
			for (int y = 20; y < 300; y += 20) g.Line(0, y, 340, y, CP, 0.6, 0.07, false, false);
		}
		void Msg(Gfx& g, const std::string& a, const std::string& b = "")
		{
			g.T(170, 140, a, CD, 12, 1); g.T(170, 160, b, CD, 9.5, 1);
		}
	}

	namespace pages
	{
		// heading-up map coordinates: centre (170,170)

		// ---- a base's layout for the map (Orbiter's API gives only the pads' centres): read once from its config ----
		// footprints (BLOCK, HANGAR*, TANK, LPAD*) and lines (RUNWAY, TRAIN*) in the base's own frame, metres;
		// the frame's axes are matched to east/north on the pads the API does give
		struct BaseLayout
		{
			struct Box { double x, z, w, d, rot; bool round, pad; int padNo; };
			struct Seg { double x1, z1, x2, z2, w; };
			std::vector<Box> boxes; std::vector<Seg> segs;
			double ex[2]{ 1, 0 }, nz[2]{ 0, 1 };   // east = ex[0]*x + ex[1]*z, north = nz[0]*x + nz[1]*z
			bool ok{};
		};
		const BaseLayout& LayoutOf(OBJHANDLE base, OBJHANDLE planet)
		{
			static std::map<OBJHANDLE, BaseLayout> cache;
			if (auto it = cache.find(base); it != cache.end()) return it->second;
			BaseLayout& L = cache[base];
			char bname[256] = "", pname[256] = ""; oapiGetObjectName(base, bname, 255); oapiGetObjectName(planet, pname, 255);
			const std::string dir = std::string("Config\\") + pname + "\\Base\\";
			WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA((dir + "*.cfg").c_str(), &fd);
			if (fh == INVALID_HANDLE_VALUE) return L;
			std::vector<std::pair<double, double>> padPos;
			do
			{
				std::ifstream f(dir + fd.cFileName); std::string line, type; bool ours = false, inList = false;
				double pos[3]{}, scl[3]{ 1, 1, 1 }, rot = 0, e1[3]{}, e2[3]{}, width = 0; bool hasScl = false;
				BaseLayout tmp; std::vector<std::pair<double, double>> pads;
				auto flush = [&]()
				{
					if (type.empty()) return;
					const bool pad = type.rfind("LPAD", 0) == 0;
					if (pad) { const double half = 40 * (hasScl ? scl[0] : 1); tmp.boxes.push_back({ pos[0], pos[2], 2 * half, 2 * half, rot, type == "LPAD1", true, static_cast<int>(pads.size()) + 1 }); pads.push_back({ pos[0], pos[2] }); }
					else if (type == "BLOCK" || type.rfind("HANGAR", 0) == 0) tmp.boxes.push_back({ pos[0], pos[2], scl[0], scl[2], rot, false, false, 0 });
					else if (type == "TANK") tmp.boxes.push_back({ pos[0], pos[2], scl[0], scl[2], 0, true, false, 0 });
					else if (type == "RUNWAY" || type.rfind("TRAIN", 0) == 0) tmp.segs.push_back({ e1[0], e1[2], e2[0], e2[2], type == "RUNWAY" ? width : 0 });
					type.clear();
				};
				while (std::getline(f, line))
				{
					std::istringstream ss(line); std::string w; ss >> w;
					if (w.empty() || w[0] == ';') continue;
					if (w == "Name") { std::string eq, rest; ss >> eq; std::getline(ss >> std::ws, rest); while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back(); ours = rest == bname; }
					else if (w == "BEGIN_OBJECTLIST") inList = true;
					else if (w == "END_OBJECTLIST") { flush(); inList = false; }
					else if (!inList) continue;
					else if (w == "END") flush();
					else if (w == "POS") ss >> pos[0] >> pos[1] >> pos[2];
					else if (w == "SCALE") { ss >> scl[0]; if (!(ss >> scl[1] >> scl[2])) scl[1] = scl[2] = scl[0]; hasScl = true; }
					else if (w == "ROT") ss >> rot;
					else if (w == "END1") ss >> e1[0] >> e1[1] >> e1[2];
					else if (w == "END2") ss >> e2[0] >> e2[1] >> e2[2];
					else if (w == "WIDTH") ss >> width;
					else if (std::isupper(static_cast<unsigned char>(w[0])) && w.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos)
					{ flush(); type = w; pos[0] = pos[1] = pos[2] = 0; scl[0] = scl[1] = scl[2] = 1; hasScl = false; rot = 0; width = 0; }
				}
				if (ours) { L.boxes = tmp.boxes; L.segs = tmp.segs; padPos = pads; L.ok = true; break; }
			} while (FindNextFileA(fh, &fd));
			FindClose(fh);
			// the frame's axes: of the four ways x/z can lie on east/north, the one that puts the pads where Orbiter has them
			const double R = oapiGetSize(planet); double blng, blat; oapiGetBaseEquPos(base, &blng, &blat);
			const double cand[8][4] = { { 1, 0, 0, 1 }, { 1, 0, 0, -1 }, { -1, 0, 0, 1 }, { -1, 0, 0, -1 }, { 0, 1, 1, 0 }, { 0, 1, -1, 0 }, { 0, -1, 1, 0 }, { 0, -1, -1, 0 } };
			double best = 1e18; int bi = 0;
			for (int ci = 0; ci < 8; ++ci)
			{
				double err = 0; int n = 0;
				for (DWORD k = 0; k < oapiGetBasePadCount(base) && k < padPos.size(); ++k)
				{
					double plng, plat, prad; if (!oapiGetBasePadEquPos(base, k, &plng, &plat, &prad)) continue;
					const double e = (plng - blng) * R * std::cos(blat), nn = (plat - blat) * R;
					const double x = padPos[k].first, z = padPos[k].second;
					err += std::hypot(cand[ci][0] * x + cand[ci][1] * z - e, cand[ci][2] * x + cand[ci][3] * z - nn); ++n;
				}
				if (n && err < best) { best = err; bi = ci; }
			}
			L.ex[0] = cand[bi][0]; L.ex[1] = cand[bi][1]; L.nz[0] = cand[bi][2]; L.nz[1] = cand[bi][3];
			return L;
		}

		V2 MapXY(double north, double east, double hdg, double s)
		{
			const double c = std::cos(hdg), sn = std::sin(hdg);
			return { 170 + (east * c - north * sn) * s, 170 - (east * sn + north * c) * s };
		}
		bool In(const V2& p) { return p.first > 2 && p.first < 338 && p.second > 2 && p.second < 298; }

		void Local(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.space)
			{
				// plan of the neighbourhood in her orbital frame: along the velocity to the right, the orbit normal up
				OBJHANDLE ref = c.v->GetGravityRef();
				VECTOR3 r, vv; c.v->GetRelativePos(ref, r); c.v->GetRelativeVel(ref, vv);
				const VECTOR3 vb = Unit(vv), nb = Unit(crossp(r, vv));
				double far_ = 20;
				for (const auto& t : h.targets) far_ = (std::max)(far_, (std::min)(t.dist, 2000.0));
				const double s = 120 / (far_ * 1.1);
				g.T(14, 18, "ПЛАН · ПО ХОДУ →", CD, 9.5); g.T(330, 18, "МАСШТАБ " + Dist(far_ * 1.1), CD, 9.5, 2);
				for (double f : { 0.5, 1.0 }) g.Circle(170, 160, 120 * f, CP, 1, 0.3, -1, 0, true);
				for (const auto& t : h.targets)
				{
					const VECTOR3 rel = -t.rel;   // target minus her
					double x = 170 + dotp(rel, vb) * s, y = 160 - dotp(rel, nb) * s;
					const bool on = t.h == h.sel;
					if (!In({ x, y })) continue;
					g.Hit(x - 10, y - 10, 20, 20, SuitHud::H_TGT, static_cast<int>(&t - &h.targets[0]));
					if (t.pack) g.Rect(x - 4, y - 4, 8, 8, on ? CA : CP, 1.4);
					else g.Circle(x, y, 5, on ? CA : CP, 1.4);
					g.T(x + 8, y + 4, t.name + " " + Dist(t.dist), on ? CA : CP, 10);
				}
				g.Tri(170, 152, 6, 14, -6, 14, CW);
				return;
			}
			const double range = ZOOM[h.zoom], s = 128 / range;
			// navigation display (default, user's choice: variant B, white): one colour of light - white - for the whole
			// map, the meaning in brightness, weight and dash; track/heading up with a heading arc on top. The colour map
			// keeps the accents as before
			const int cA = h.mapMono ? CW : CA, cW = CW, cP = h.mapMono ? CW : CP;
			const auto& G = h.grid;
			// contours (marching squares) and steep ground
			if (G.n > 1 && !G.h.empty())
			{
				double lo = 1e9, hi = -1e9; for (double z : G.h) { lo = (std::min)(lo, z); hi = (std::max)(hi, z); }
				const double step = 2 * G.half / (G.n - 1);
				const double nice[] = { 0.25, 0.5, 1, 2, 5, 10, 20, 50, 100, 200 };
				double ci = nice[9]; for (double q : nice) if ((hi - lo) / q <= 7) { ci = q; break; }
				const double dN = (G.lat - h.lat) * h.bodyR, dE = Wrap(G.lng - h.lng) * h.bodyR * std::cos(h.lat);
				auto P = [&](double i, double j) { return MapXY(-G.half + j * step + dN, -G.half + i * step + dE, h.hdg, s); };
				if (h.mapMono && hi - lo > 0.3)   // brightness bands: four levels of the ground, higher = brighter
					for (int j = 0; j + 1 < G.n; ++j)
						for (int i = 0; i + 1 < G.n; ++i)
						{
							const double zc = 0.25 * (G.h[j * G.n + i] + G.h[j * G.n + i + 1] + G.h[(j + 1) * G.n + i] + G.h[(j + 1) * G.n + i + 1]);
							const double band = std::floor(std::clamp((zc - lo) / (hi - lo), 0.0, 0.999) * 4) / 3;
							const V2 q[4] = { P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1) };
							if (In(q[0]) || In(q[2])) g.Fill({ q[0], q[1], q[2], q[3] }, CW, 0.02 + 0.07 * band);
						}
				if (!h.mapMono && hi - lo > 0.3)
					for (double lv = std::ceil(lo / ci) * ci; lv < hi; lv += ci)
						for (int j = 0; j + 1 < G.n; ++j)
							for (int i = 0; i + 1 < G.n; ++i)
							{
								const double z[4] = { G.h[j * G.n + i], G.h[j * G.n + i + 1], G.h[(j + 1) * G.n + i + 1], G.h[(j + 1) * G.n + i] };
								const double ex[4][4] = { { 0, 0, 1, 0 }, { 1, 0, 1, 1 }, { 1, 1, 0, 1 }, { 0, 1, 0, 0 } };   // edges: (i0,j0)-(i1,j1)
								std::vector<V2> cut;
								for (int e = 0; e < 4; ++e)
								{
									const double a = z[e], b = z[(e + 1) % 4];
									if ((a < lv) != (b < lv))
									{
										const double f = (lv - a) / (b - a);
										cut.push_back(P(i + ex[e][0] + f * (ex[e][2] - ex[e][0]), j + ex[e][1] + f * (ex[e][3] - ex[e][1])));
									}
								}
								for (size_t k = 0; k + 1 < cut.size(); k += 2)
									if (In(cut[k]) && In(cut[k + 1])) g.Line(cut[k].first, cut[k].second, cut[k + 1].first, cut[k + 1].second, CD, 1, 0.6, false, false);
							}
				for (int j = 0; j + 1 < G.n; ++j)
					for (int i = 0; i + 1 < G.n; ++i)
					{
						const double gz = std::hypot(G.h[j * G.n + i + 1] - G.h[j * G.n + i], G.h[(j + 1) * G.n + i] - G.h[j * G.n + i]) / step;
						if (gz < std::tan(15 * RAD)) continue;
						const V2 a = P(i, j), b = P(i + 1, j + 1), a2 = P(i + 0.5, j), b2 = P(i + 1, j + 0.5), a3 = P(i, j + 0.5), b3 = P(i + 0.5, j + 1);
						if (h.mapMono) { const V2 m = P(i + 0.5, j + 0.5); if (In(m)) g.Circle(m.first, m.second, 0.9, -1, 0, 0, CW, 0.55); }
						else if (In(a) && In(b)) { g.Line(a.first, a.second, b.first, b.second, cA, 1, 0.55, false, false); g.Line(a2.first, a2.second, b2.first, b2.second, cA, 1, 0.55, false, false); g.Line(a3.first, a3.second, b3.first, b3.second, cA, 1, 0.55, false, false); }
					}
			}
			// bases of the body: within 5 km of scale - their layout (footprints, pads with numbers, runways and tracks);
			// beyond - a symbol; every base within 500 km is on the map (on the ring, with its distance, if beyond it)
			if (h.hBody)
				for (DWORD bi = 0; bi < oapiGetBaseCount(h.hBody); ++bi)
				{
					OBJHANDLE hb = oapiGetBaseByIndex(h.hBody, bi);
					double blng, blat; oapiGetBaseEquPos(hb, &blng, &blat);
					const double bn = (blat - h.lat) * h.bodyR, be = Wrap(blng - h.lng) * h.bodyR * std::cos(h.lat), bd = std::hypot(bn, be);
					if (bd > 500e3) continue;
					const BaseLayout& Lb = range <= 5000 && bd < range * 2 + 3000 ? LayoutOf(hb, h.hBody) : BaseLayout{};
					if (Lb.ok)
					{
						auto at = [&](double x, double z) { return MapXY(bn + Lb.nz[0] * x + Lb.nz[1] * z, be + Lb.ex[0] * x + Lb.ex[1] * z, h.hdg, s); };
						for (const auto& sg : Lb.segs)
						{
							const V2 p1 = at(sg.x1, sg.z1), p2 = at(sg.x2, sg.z2);
							g.Line(p1.first, p1.second, p2.first, p2.second, CW, sg.w > 0 ? (std::max)(1.0, sg.w * s) : 1.0, sg.w > 0 ? 0.5 : 0.6, sg.w <= 0, false);
						}
						for (const auto& bx : Lb.boxes)
						{
							const double rr = bx.rot * RAD, cr = std::cos(rr), sr = std::sin(rr);
							auto corner = [&](double u, double v) { return at(bx.x + u * cr - v * sr, bx.z + u * sr + v * cr); };
							if (bx.round && !bx.pad) { const V2 cc = at(bx.x, bx.z); if (In(cc)) g.Circle(cc.first, cc.second, (std::max)(1.0, bx.w / 2 * s), CW, 1.2, 0.9, CW, 0.18); continue; }
							std::vector<V2> poly;
							if (bx.pad && bx.round) for (int kk = 0; kk < 8; ++kk) { const double t = kk * PI / 4 + PI / 8, q = bx.w / 2 / std::cos(PI / 8); poly.push_back(corner(q * std::cos(t), q * std::sin(t))); }
							else poly = { corner(-bx.w / 2, -bx.d / 2), corner(bx.w / 2, -bx.d / 2), corner(bx.w / 2, bx.d / 2), corner(-bx.w / 2, bx.d / 2) };
							bool vis = false; for (const V2& q : poly) vis = vis || In(q);
							if (!vis) continue;
							if (!bx.pad) g.Fill(poly, CW, 0.18);
							g.Poly(poly, CW, bx.pad ? 1.4 : 1.1, 0.9, true);
							if (bx.pad) { const V2 cc = at(bx.x, bx.z); if (In(cc) && bx.w * s > 10) g.T(cc.first, cc.second + 4, std::to_string(bx.padNo), CW, bx.w * s > 24 ? 12.5 : 9.5, 1); }
						}
						continue;
					}
					bool listed = false; for (const auto& t : h.targets) listed = listed || t.h == hb;
					if (listed) continue;   // a target already: drawn with the targets below
					V2 p = MapXY(bn, be, h.hdg, s);
					const double r = std::hypot(p.first - 170, p.second - 170); const bool edge = r > 134 || !In(p);
					if (edge) { const double f = 128 / (std::max)(1.0, r); p = { 170 + (p.first - 170) * f, 170 + (p.second - 170) * f }; }
					g.Rect(p.first - 5, p.second - 5, 10, 10, CW, 1.3, edge ? 0.6 : 1); g.Line(p.first - 5, p.second, p.first + 5, p.second, CW, 1, edge ? 0.6 : 1); g.Line(p.first, p.second - 5, p.first, p.second + 5, CW, 1, edge ? 0.6 : 1);
					char bnm[256] = ""; oapiGetObjectName(hb, bnm, 255);
					const bool left = p.first > 250;
					g.T(p.first + (left ? -9 : 9), p.second + 4, Upper(bnm) + " " + Dist(bd), edge ? CD : CW, 9.5, left ? 2 : 0);
				}
			// range rings
			for (double f : { 0.25, 0.5, 1.0 })
			{
				g.Circle(170, 170, 128 * f, cP, 1, 0.35, -1, 0, true);
				g.T(170 + 128 * f * 0.707 + 3, 170 + 128 * f * 0.707 + 10, Dist(range * f), CD, 9.5);
			}
			// trail and the next 10 s
			for (size_t i = 0; i < h.trail.size(); ++i)
			{
				const V2 p = MapXY((h.trail[i].first - h.lat) * h.bodyR, Wrap(h.trail[i].second - h.lng) * h.bodyR * std::cos(h.lat), h.hdg, s);
				if (In(p)) g.Circle(p.first, p.second, 1.3, -1, 0, 0, cP, 0.25 + 0.7 * i / h.trail.size());
			}
			{
				// vectors from her: velocity over the ground, the wind, the autopilot's push (1 m/s = 14 units)
				auto vec = [&](double north, double east, double k_) { const V2 p = MapXY(north, east, h.hdg, 1.0); return V2{ 170 + (p.first - 170) * k_, 170 + (p.second - 170) * k_ }; };
				const double gv = std::hypot(h.gsH.x, h.gsH.z);
				if (gv > 0.1) { const double kk = (std::min)(14.0, 110 / gv); const V2 p = vec(h.gsH.z, h.gsH.x, kk); g.Arrow(170, 170, p.first, p.second, cW, 1.6); g.T(p.first + 6, p.second - 4, "V " + Num(gv, 1), cW, 9.5); }
				const double wv = std::hypot(c.d.wind.x, c.d.wind.z);
				if (!c.d.vacuum && wv > 0.1) { const double kk = (std::min)(14.0, 110 / wv); const V2 p = vec(c.d.wind.z, c.d.wind.x, kk); g.Arrow(170, 170, p.first, p.second, cP, 1.2, 0.8, true); g.T(p.first + 6, p.second + 10, "ветер " + Num(wv, 1), cP, 9.5); }
				const double av = std::hypot(c.d.apCmd.x, c.d.apCmd.z);
				if (c.d.apMode && av > 0.05) { const double kk = (std::min)(30.0, 90 / av); const V2 p = vec(c.d.apCmd.z, c.d.apCmd.x, kk); g.Arrow(170, 170, p.first, p.second, cA, 1.8); g.T(p.first + 6, p.second + 10, "АП", cA, 9.5); }
			}
			// what is around
			for (const auto& t : h.targets)
			{
				const bool on = t.h == h.sel;
				const int idx = static_cast<int>(&t - &h.targets[0]);
				V2 p = MapXY(t.north, t.east, h.hdg, s);
				const double r = std::hypot(p.first - 170, p.second - 170);
				const bool edge = r > 134 || !In(p);
				if (edge) { const double f = 128 / (std::max)(1.0, r); p = { 170 + (p.first - 170) * f, 170 + (p.second - 170) * f }; }
				const int col = on ? cA : cP;
				if (on) g.Line(170, 170, p.first, p.second, cA, 1, 0.7, true);
				if (t.pack) { g.Rect(p.first - 5, p.second - 4, 10, 8, col, 1.5); g.Line(p.first - 5, p.second - 1, p.first + 5, p.second - 1, col, 1, 1, false, false); }
				else if (t.crew) g.Circle(p.first, p.second, 5, col, 1.4);
				else if (t.base) { g.Rect(p.first - 6, p.second - 6, 12, 12, col, 1.5); g.Line(p.first - 6, p.second, p.first + 6, p.second, col, 1); g.Line(p.first, p.second - 6, p.first, p.second + 6, col, 1); }
				else g.Poly({ { p.first, p.second - 7 }, { p.first + 7, p.second }, { p.first, p.second + 7 }, { p.first - 7, p.second } }, col, 1.6, 1, true);
				g.Hit(p.first - 10, p.second - 10, 20, 20, SuitHud::H_TGT, idx);
				const std::string lab = (edge ? "◂ " : "") + t.name + " " + Dist(t.dist);
				const bool left = p.first > 250;
				g.T(p.first + (left ? -9 : 9), p.second + 4, lab, col, 10, left ? 2 : 0);
			}
			// her
			g.Fill({ { 170, 161 }, { 177, 177 }, { 170, 173 }, { 163, 177 } }, cW, 1);
			// north arrow, scale
			const double nx = -std::sin(h.hdg), ny = -std::cos(h.hdg);
			g.Line(24, 34, 24 + nx * 12, 34 + ny * 12, cP, 1.6); g.T(24 + nx * 20, 34 + ny * 20 + 4, "С", cP, 10, 1);
			char hd[16]; snprintf(hd, sizeof hd, "%03.0f°", std::fmod(h.hdg * DEG + 360, 360));
			g.T(330, 18, std::string("КУРС ") + hd, CD, 9.5, 2);
			// heading arc: +-60 deg of the course, ticks every 10, numbers every 30, the course under the pointer
			for (int dd = -60; dd <= 60; dd += 10)
			{
				const double a = -PI05 + dd * RAD, r1 = 134, r2 = dd % 30 ? 138 : 142;
				const double x1 = 170 + r1 * std::cos(a), y1 = 170 + r1 * std::sin(a), x2 = 170 + r2 * std::cos(a), y2 = 170 + r2 * std::sin(a);
				if (y2 < 24) continue;
				g.Line(x1, y1, x2, y2, CW, 1, 0.8, false, false);
				const int deg = static_cast<int>(std::lround(std::fmod(h.hdg * DEG + dd + 720, 360)));
				if (dd % 30 == 0 && dd != 0) { char nb[8]; snprintf(nb, sizeof nb, "%02d", deg / 10); g.T(170 + 150 * std::cos(a), 170 + 150 * std::sin(a) + 4, nb, CD, 9.5, 1); }
			}
			g.Tri(170, 28, -5, -8, 5, -8, CW);
			g.Line(14, 288, 78, 288, cP, 1.4); g.Line(14, 284, 14, 292, cP, 1); g.Line(78, 284, 78, 292, cP, 1);
			g.T(46, 282, Dist(range / 2), CD, 9.5, 1);
			g.Line(238, 285, 250, 285, cA, 1, 0.7); g.T(254, 289, "склон > 15°", cA, 9.5);
			for (int zi = 0; zi < 2; ++zi)
			{
				const double bx = 296 + zi * 22, by = 244;
				g.Rect(bx, by, 18, 18, cP, 1, 0.7, cP, 0.1); g.T(bx + 9, by + 14, zi ? "−" : "+", cP, 13, 1);
				g.Hit(bx, by, 18, 18, SuitHud::H_ZOOM, zi ? 1 : -1);
			}
		}

		void Orbit(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.surface)
			{
				const double cx = 170, cy = 112, r = 82;
				g.Circle(cx, cy, r, CP, 1.3, 1, CP, 0.05);
				for (double l : { 30.0, 60.0 })
				{
					const double w = r * std::cos(l * RAD);
					g.Line(cx - w, cy - r * std::sin(l * RAD), cx + w, cy - r * std::sin(l * RAD), CP, 0.8, 0.35, false, false);
					g.Line(cx - w, cy + r * std::sin(l * RAD), cx + w, cy + r * std::sin(l * RAD), CP, 0.8, 0.35, false, false);
				}
				g.Line(cx - r, cy, cx + r, cy, CP, 0.8, 0.35, false, false); g.Line(cx, cy - r, cx, cy + r, CP, 0.8, 0.35, false, false);
				// her position, seen from above her meridian
				const double x = cx, y = cy - r * std::sin(h.lat);
				g.Circle(x, y, 4, CW, 1.5, 1, CW, 1); g.Circle(x, y, 9, CA, 1.2);
				char bn[64] = ""; if (h.hBody) oapiGetObjectName(h.hBody, bn, 64);
				const char* ru = BodyRu(bn);
				g.T(14, 18, Upper(std::string(ru ? ru : bn)) + " · НА ПОВЕРХНОСТИ", CD, 9.5);
				g.Cell(14, 214, "ШИРОТА", Num(std::abs(h.lat * DEG), 1) + (h.lat >= 0 ? "° с." : "° ю."));
				g.Cell(120, 214, "ДОЛГОТА", Num(std::abs(h.lng * DEG), 1) + (h.lng >= 0 ? "° в." : "° з."));
				g.Cell(226, 214, "СОЛНЦЕ", h.sunElev > 0 ? Num(h.sunElev * DEG, 0) + "° · день" : "ночь", h.sunElev > 0 ? CW : CA);
				// what the body asks of anyone who would leave it, and what it gives by turning
				if (h.hBody)
				{
					const double R = h.bodyR, GM = GGRAV * oapiGetMass(h.hBody), gs = GM / (R * R);
					const double vc = std::sqrt(GM / R), ve = vc * std::sqrt(2.0);
					const double Tsid = std::abs(oapiGetPlanetPeriod(h.hBody)), vrot = Tsid > 0 ? PI2 * R * std::cos(h.lat) / Tsid : 0;
					double lng_, lat_, rad_; c.v->GetEquPos(lng_, lat_, rad_);
					g.Cell(14, 250, "g", Num(gs, 2) + " м/с²");
					g.Cell(120, 250, "1-Я КОСМ.", Num(vc, 0) + " м/с");
					g.Cell(226, 250, "2-Я КОСМ.", Num(ve, 0) + " м/с");
					g.Cell(14, 286, "ВРАЩЕНИЕ", Num(vrot, 1) + " м/с");
					g.Cell(120, 286, "СУТКИ (ЗВЁЗД.)", Tsid > 2 * 86400 ? Num(Tsid / 86400, 1) + " сут" : Num(Tsid / 3600, 1) + " ч");
					g.Cell(226, 286, "ОТ СРЕДН. R", Num(oapiSurfaceElevation(h.hBody, lng_, lat_), 0, true) + " м");
				}
				return;
			}
			if (!h.orbitOk)
			{
				// open path (escape) or no elements: what there is
				OBJHANDLE ref = c.v->GetGravityRef(); VECTOR3 rr, vv; c.v->GetRelativePos(ref, rr); c.v->GetRelativeVel(ref, vv);
				const double GM = GGRAV * oapiGetMass(ref), r0 = length(rr), v0 = length(vv), ve = std::sqrt(2 * GM / r0);
				Msg(g, "НЕЗАМКНУТАЯ ТРАЕКТОРИЯ", (Num(v0, 0) + " м/с · 2-я косм. здесь " + Num(ve, 0) + " м/с").c_str());
				return;
			}
			char bn[64] = ""; oapiGetObjectName(c.v->GetGravityRef(), bn, 64);
			const char* ru = BodyRu(bn);
			const double R = oapiGetSize(c.v->GetGravityRef());
			const double apd = h.op.ApD, s = 118 / (std::max)(apd, R * 1.05);
			const double cx = 170, cy = 140;
			g.Circle(cx, cy, R * s, CP, 1.3, 1, CP, 0.07);
			g.T(cx, cy + 4, Upper(std::string(ru ? ru : bn)), CD, 9.5, 1);
			auto ell = [&](const ELEMENTS& e, double rot, int col, bool dash)
			{
				std::vector<V2> pts;
				const double p = e.a * (1 - e.e * e.e);
				for (int i = 0; i <= 72; ++i)
				{
					const double th = i * PI2 / 72, r = p / (1 + e.e * std::cos(th));
					pts.push_back({ cx + r * std::cos(th + rot) * s, cy - r * std::sin(th + rot) * s });
				}
				g.Poly(pts, col, dash ? 1.2 : 1.5, 1, false, dash);
			};
			auto at = [&](const ELEMENTS& e, double th, double rot) { const double r = e.a * (1 - e.e * e.e) / (1 + e.e * std::cos(th)); return V2{ cx + r * std::cos(th + rot) * s, cy - r * std::sin(th + rot) * s }; };
			ell(h.el, 0, CP, false);
			double drot = 0;
			if (h.tgtOrbitOk) { drot = Wrap(h.tel.omegab - h.el.omegab); ell(h.tel, drot, CA, true); const V2 q = at(h.tel, h.top.TrA, drot); g.Circle(q.first, q.second, 3.5, CA, 1.2, 1, CA, 1); }
			const V2 me = at(h.el, h.op.TrA, 0); g.Circle(me.first, me.second, 4.5, CW, 1.5, 1, CW, 1); g.T(me.first + 8, me.second + 14, "Я", CW, 10);
			const V2 ap = at(h.el, PI, 0), pe = at(h.el, 0, 0);
			g.Circle(ap.first, ap.second, 2.5, CP, 1, 1, CP, 1); g.T(ap.first - 6, ap.second - 6, "Ап " + Num((h.op.ApD - R) / 1000, 0), CP, 10, 2);
			g.Circle(pe.first, pe.second, 2.5, CP, 1, 1, CP, 1); g.T(pe.first + 6, pe.second - 6, "Пе " + Num((h.op.PeD - R) / 1000, 0), CP, 10);
			g.T(14, 18, "ОРБИТА · " + Upper(std::string(ru ? ru : bn)), CD, 9.5); g.T(330, 18, "— моя   - - цель", CD, 9.5, 2);
			const bool hitsGround = h.op.PeD < R;   // a ballistic arc: it meets the surface before the periapsis
			g.Cell(14, 250, "ПЕРИОД", Num(h.op.T / 60, 1) + " мин");
			g.Cell(120, 250, "НАКЛОН", Num(h.el.i * DEG, 2) + "°");
			g.Cell(226, 250, "ОТН. НАКЛОН", h.tgtOrbitOk ? Num(h.relInc * DEG, 2) + "°" : "—", CA);
			{
				VECTOR3 vv; c.v->GetRelativeVel(c.v->GetGravityRef(), vv);
				g.Cell(14, 286, "СКОРОСТЬ", Num(length(vv), 0) + " м/с");
				g.Cell(120, 286, "ДО АПОЦЕНТРА", Clock(h.op.ApT));
				g.Cell(226, 286, hitsGround ? "ДУГА" : "ДО ПЕРИЦЕНТРА", hitsGround ? std::string("до грунта") : Clock(h.op.PeT), hitsGround ? CA : CW);
			}
		}

		void Power(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; Grid(g);
			if (!d.suit) { Msg(g, "КОМБИНЕЗОН", "питания нет"); return; }
			g.T(14, 22, "БАТАРЕЯ", CD, 9.5);
			g.T(326, 22, Num(d.battKWh, 1) + " кВт·ч · " + Hours(d.battHours), CW, 11, 2);
			g.Bar(14, 30, 312, 16, d.batt, d.batt < 0.1 ? CR : d.batt < 0.25 ? CA : CP);
			const bool cooling = d.heatW >= 0;
			struct Row { const char* n; double w; } rows[] = {
				{ "Жизнеобеспечение", d.lifeW }, { cooling ? "Охлаждение (насос)" : "Обогрев", d.thermalW }, { "Сервоприводы", d.driveW }, { "Фонари", d.lampW } };
			for (int i = 0; i < 4; ++i)
			{
				const double y = 72 + i * 25, w = rows[i].w;
				g.T(14, y + 9, rows[i].n, w > 0.5 ? CP : CD, 11);
				g.Bar(150, y, 120, 11, w / 200, CP);
				g.T(326, y + 9, w > 0.5 ? Num(w, 0) + " Вт" : "выкл", w > 0.5 ? CW : CD, 11, 2);
			}
			g.Line(14, 180, 326, 180, CD, 1, 0.5, false, false);
			g.T(14, 196, "ИТОГО", CD, 9.5); g.T(326, 196, Num(d.powerW, 0) + " Вт", CW, 13, 2);
			const double x0 = 20, x1 = 320;
			auto X = [&](double t) { return x0 + std::clamp((t - d.ratedMinC) / (d.ratedMaxC - d.ratedMinC), 0.0, 1.0) * (x1 - x0); };
			g.T(14, 226, "ТЕПЛО · ДОПУСК " + Num(d.ratedMinC, 0) + "…" + Num(d.ratedMaxC, 0, true) + " °C", CD, 9.5);
			g.Line(x0, 252, x1, 252, CP, 3, 0.5); g.Line(x0, 244, x0, 260, CR, 2); g.Line(x1, 244, x1, 260, CR, 2);
			g.Tri(X(d.envC), 248, -5, -8, 5, -8, d.inSpec ? CP : CR); g.T(X(d.envC), 238, "среда " + Num(d.envC, 0, true), d.inSpec ? CP : CR, 9.5, 1);
			if (d.hasGround) { g.Tri(X(d.groundC), 256, -5, 8, 5, 8, CA); g.T(X(d.groundC), 278, "грунт " + Num(d.groundC, 0, true), CA, 9.5, 1); }
			g.T(14, 296, cooling ? "тепло уходит насосом на радиатор (КПД 3)" : "обогрев от батареи", CD, 9.5);
		}

		void Opts(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			g.T(14, 16, "ОБЛИК ЭКРАНА", CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const double x = 10 + i * 108, w = 104; const bool on = i == h.look;
				if (on) { g.Rect(x, 22, w, 22, -1, 0, 0, CA, 1); g.T(x + w / 2, 37, LOOK_NAME[i], CK, 11, 1); }
				else { g.Rect(x, 22, w, 22, CP, 1, 0.7, CP, 0.08); g.T(x + w / 2, 37, LOOK_NAME[i], CP, 11, 1); }
				g.Hit(x, 22, w, 22, SuitHud::H_LOOK, i);
			}
			g.T(14, 62, h.look ? "ЦВЕТ — только для голо" : "ЦВЕТ", CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const double y = 68 + i * 46; const bool on = i == h.pal;
				g.Rect(10, y, 320, 42, on ? CA : CD, on ? 1.6 : 1, 1, on ? CA : -1, 0.1);
				g.Hit(10, y, 320, 42, SuitHud::H_PAL, i);
				// swatches: the palette's own colours
				Gfx sw(g.skp, g.o, g.txt, i); sw.Frame(g.ox, g.oy, g.u);
				sw.Rect(22, y + 9, 24, 24, -1, 0, 0, CP, 1); sw.Rect(50, y + 9, 24, 24, -1, 0, 0, CA, 1);
				g.T(88, y + 19, std::string(on ? "▶ " : "") + PAL_NAME[i], on ? CA : CW, 12.5);
				g.T(88, y + 34, PAL_NOTE[i], CD, 9.5);
			}
			g.Line(14, 212, 326, 212, CD, 1, 0.5, false, false);
			g.T(14, 230, "ЯРКОСТЬ ЭКРАНА", CD, 9.5);
			g.T(326, 230, "свет вокруг " + Num(h.ambient, 2), CD, 9.5, 2);
			{
				const double y = 238; const bool au = h.brightAuto;
				if (au) { g.Rect(14, y, 62, 20, -1, 0, 0, CA, 1); g.T(45, y + 14, "АВТО", CK, 11, 1); }
				else { g.Rect(14, y, 62, 20, CP, 1, 0.7, CP, 0.08); g.T(45, y + 14, "АВТО", CP, 11, 1); }
				g.Hit(14, y, 62, 20, SuitHud::H_BRIGHT, 0);
				g.Rect(84, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(94, y + 15, "−", CP, 13, 1); g.Hit(84, y, 20, 20, SuitHud::H_BRIGHT, -1);
				g.Bar(110, y + 6, 180, 8, au ? h.ambient : h.brightManual / 1.5, au ? CD : h.brightManual > 1.05 ? CA : CP);
				g.Line(110 + 180 / 1.5, y + 3, 110 + 180 / 1.5, y + 17, CD, 1, 0.8, false, false);   // 1.0; beyond it - the maximum
				if (!au && h.brightManual > 1.05) g.T(200, y - 2, "МАКСИМУМ", CA, 8.5, 1);
				g.Rect(296, y, 20, 20, CP, 1, 0.7, CP, 0.08); g.T(306, y + 15, "+", CP, 13, 1); g.Hit(296, y, 20, 20, SuitHud::H_BRIGHT, 1);
			}
			g.T(14, 280, "АВТО — по солнцу и грунту; щиток V добавляет тень", CD, 9.5);
			// the local map: technical monochrome or with colour accents
			g.T(14, 300, "КАРТА", CD, 9.5);
			for (int i = 0; i < 2; ++i)
			{
				const bool on = (i == 0) == h.mapMono; const double x = 70 + i * 128;
				if (on) g.Rect(x, 288, 120, 18, -1, 0, 0, CA, 1); else g.Rect(x, 288, 120, 18, CP, 1, 0.7, CP, 0.08);
				g.T(x + 60, 301, i == 0 ? "МОНОХРОМ" : "ЦВЕТ", on ? CK : CP, 10, 1);
				if (!on) g.Hit(x, 288, 120, 18, SuitHud::H_LOOK, 10);
			}
		}

		void Targets(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.targets.empty()) { Msg(g, "В РАДИУСЕ 50 км НИКОГО"); return; }
			g.T(14, 20, "ОБЪЕКТ", CD, 9.5); g.T(250, 20, "ДАЛЬН", CD, 9.5, 2); g.T(326, 20, "V СБЛ", CD, 9.5, 2);
			const size_t rows = (std::min)(h.targets.size(), static_cast<size_t>(5));
			for (size_t i = 0; i < rows; ++i)
			{
				const auto& t = h.targets[i]; const double y = 28 + i * 32; const bool on = t.h == h.sel;
				if (on) g.Rect(8, y, 324, 29, CA, 1.2, 1, CA, 0.12);
				g.Hit(8, y, 324, 29, SuitHud::H_TGT, static_cast<int>(i));
				g.T(16, y + 13, (on ? "▶ " : "") + t.name, on ? CA : CW, 12.5);
				g.T(16, y + 25, t.kind, CD, 9.5);
				g.T(250, y + 18, Dist(t.dist), on ? CA : CW, 12.5, 2);
				g.T(326, y + 18, Num(t.rate, 2, true), CP, 11, 2);
			}
			const SuitHud::Target* s = h.Selected();
			if (!s) return;
			g.Line(14, 196, 326, 196, CD, 1, 0.5, false, false);
			g.T(14, 212, "ВЫБРАНО: " + s->name, CA, 12);
			double brg = std::atan2(s->east, s->north) * DEG; if (brg < 0) brg += 360;
			const double elev = std::atan2(s->up, std::hypot(s->north, s->east)) * DEG;
			char b[16]; snprintf(b, sizeof b, "%03.0f°", brg);
			g.Cell(14, 228, "ПЕЛЕНГ", b); g.Cell(100, 228, "УГОЛ МЕСТА", Num(elev, 0, true) + "°");
			g.Cell(14, 264, "ОТН. СКОРОСТЬ", Num(length(s->relV), 2) + " м/с");
			if (s->base) g.Cell(130, 264, "ПЛОЩАДКИ", std::to_string(s->pads));
			else g.Cell(130, 264, "СТЫК. УЗЛЫ", [&] { VESSEL* o = oapiGetVesselInterface(s->h); return o ? std::to_string(o->DockCount()) : std::string("—"); }());
			// the target's bearing relative to her heading
			const double rb = std::atan2(s->east, s->north) - h.hdg;
			g.Circle(288, 250, 30, CP, 1, 0.6);
			g.Line(288 + 22 * std::sin(rb), 250 - 22 * std::cos(rb), 288, 250, CA, 2);
			g.Circle(288 + 22 * std::sin(rb), 250 - 22 * std::cos(rb), 3, CA, 1, 1, CA, 1);
			g.T(288, 216, "курс ↑", CD, 9.5, 1);
		}

		void Transfer(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s || h.prof.h.size() < 2) { Msg(g, "ВЫБЕРИТЕ ЦЕЛЬ", "на странице ЦЕЛИ или на карте"); return; }
			g.T(14, 18, "ПРОФИЛЬ ПЕРЕЛЁТА · РЕЛЬЕФ ПО ПУТИ", CD, 9.5);
			const double hereGround = h.prof.h.front();
			const double cruise = d.apMode == 2 && d.jetMode == 1 ? d.jetAltHold : std::clamp(h.prof.dist / 40, 3.0, 60.0);
			double top = (std::max)(cruise, d.alt) + 2, bot = 0;
			for (double z : h.prof.h) { top = (std::max)(top, z - hereGround + 2); bot = (std::min)(bot, z - hereGround); }
			const double D = (std::max)(1.0, h.prof.dist);
			auto X = [&](double f) { return 22 + 296 * f; };
			auto Y = [&](double z) { return 150 - 110 * (z - bot) / (std::max)(1.0, top - bot); };
			std::vector<V2> ground;
			double minClear = 1e9;
			for (size_t i = 0; i < h.prof.h.size(); ++i)
			{
				const double f = static_cast<double>(i) / (h.prof.h.size() - 1), z = h.prof.h[i] - hereGround;
				ground.push_back({ X(f), Y(z) });
				minClear = (std::min)(minClear, cruise - z);
			}
			std::vector<V2> area = ground; area.push_back({ X(1), 150 }); area.push_back({ X(0), 150 });
			g.Fill(area, CD, 0.25); g.Poly(ground, CD, 1, 0.8);
			const double zEnd = h.prof.h.back() - hereGround;
			g.Poly({ { X(0), Y(d.alt) }, { X(0.08), Y(cruise) }, { X(0.85), Y(cruise) }, { X(1), Y(zEnd) } }, CA, 1.6, 1, false, true);
			g.Circle(X(0), Y(d.alt), 4, CW, 1.5, 1, CW, 1); g.T(X(0) + 6, Y(d.alt) - 6, "Я", CW, 10);
			g.Line(X(1), Y(zEnd), X(1), Y(zEnd) - 22, CA, 1.4); g.Tri(X(1), Y(zEnd) - 22, 12, 4, 0, 8, CA);
			g.T(X(1) - 4, Y(zEnd) - 26, s->name, CA, 9.5, 2);
			g.T(160, 164, "зазор мин. " + Num(minClear, 1) + " м", minClear < 1.5 ? CR : CP, 9.5, 1);
			double brg = h.prof.brg * DEG; if (brg < 0) brg += 360; char b[16]; snprintf(b, sizeof b, "%03.0f°", brg);
			const double g0 = h.hBody ? GGRAV * oapiGetMass(h.hBody) / (h.bodyR * h.bodyR) : 1.62;
			const bool hop = h.hBody && !oapiPlanetHasAtmosphere(h.hBody) && D > 400;
			const double vCruise = hop ? std::sqrt(D * g0 / std::sin(50 * RAD)) : std::clamp(std::sqrt(g0 * D / 2), 3.0, 40.0);
			const double time = hop ? 2 * vCruise * std::sin(25 * RAD) / g0 + 20 : D / vCruise;
			const double dv = hop ? 2.6 * std::sqrt(D * g0) + 80 : g0 * time + 2 * vCruise;
			g.Cell(14, 180, "ДАЛЬНОСТЬ", Dist(D)); g.Cell(120, 180, "ПЕЛЕНГ", b); g.Cell(226, 180, "ВРЕМЯ ≈", Clock(time));
			g.Cell(14, 216, "ВЫСОТА", Num(cruise, 1) + " м", CA); g.Cell(120, 216, "Δv ≈", Num(dv, 0) + " м/с");
			g.Cell(226, 216, "ЗАПАС ПОСЛЕ", Num(d.jetDv - dv, 0) + " м/с", d.jetDv - dv < 20 ? CR : CW);
			const size_t n = h.prof.h.size();
			const double slope = std::atan(std::abs(h.prof.h[n - 1] - h.prof.h[n - 2]) / (D / (n - 1))) * DEG;
			g.Line(14, 254, 326, 254, CD, 1, 0.5, false, false);
			g.T(14, 272, "у цели склон " + Num(slope, 0) + "° · " + (slope < 10 ? "садиться можно" : "круто, ищите ровнее"), slope < 10 ? CP : CA, 10.5);
			g.T(14, 290, (hop ? "прыжком до " : "крейсер ") + Num(vCruise, 0) + " м/с · " + (s->base ? "посадка на площадку" : "посадка рядом с целью"), CD, 9.5);
		}

		void Flight(Ctx& c)
		{
			Gfx& g = c.g; const HudData& d = c.d; Grid(g);
			if (!d.jet) { Msg(g, "РАНЕЦ НЕ НАДЕТ"); return; }
			const bool hold = d.jetMode == 1 && !d.apMode;
			g.T(14, 18, "КРУИЗ · ВЫСОТУ И СКОРОСТЬ ДЕРЖИТ ПОМОЩНИК", CD, 9.5);
			auto block = [&](double y, const char* name, const std::string& set, const std::string& now, int req0, const std::vector<const char*>& lab)
			{
				g.T(14, y, name, CD, 9.5);
				g.T(14, y + 28, set, hold ? CA : CD, 22);
				g.T(326, y + 28, "сейчас " + now, CW, 12, 2);
				const double bw = 312.0 / lab.size() - 4;
				for (size_t i = 0; i < lab.size(); ++i)
				{
					const double bx = 14 + i * (bw + 4), by = y + 40;
					g.Hit(bx, by, bw, 22, SuitHud::H_AP, req0 + static_cast<int>(i));
					g.Rect(bx, by, bw, 22, CA, 1, 0.8, CA, 0.08);
					g.T(bx + bw / 2, by + 15, lab[i], CA, 10.5, 1);
				}
			};
			block(42, "ВЫСОТА НАД ГРУНТОМ", Num(d.jetAltHold, 1) + " м", Num(d.alt, 1) + " м", SuitHud::AP_CRUISE, { "-10", "-1", "+1", "+10" });
			block(130, "СКОРОСТЬ ВПЕРЁД", Num(hold ? d.jetSpd : d.jetVf, 1) + " м/с", Num(d.jetVf, 1) + " м/с", SuitHud::AP_CRUISE + 10, { "-5", "-1", "СТОП", "+1", "+5" });
			g.Line(14, 214, 326, 214, CD, 1, 0.5, false, false);
			g.T(14, 232, hold ? "круиз включён · снос вбок гасится сам" : "круиз выключен · кнопка включит с текущими", hold ? CP : CD, 10.5);
			g.T(14, 252, "W / S - скорость, 3 м/с за секунду", CD, 9.5);
			g.T(14, 268, "A / D - курс · пробел / Ctrl - высота", CD, 9.5);
			g.T(14, 284, "Shift - полный вектор вручную, круиз ждёт", CD, 9.5);
		}

		void Landing(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			const double cx = 110, cy = 128;
			g.T(14, 18, "ПОД НОГАМИ · ВИД СВЕРХУ", CD, 9.5);
			for (double r : { 20.0, 45.0, 70.0 }) g.Circle(cx, cy, r, CP, 1, 0.4, -1, 0, true);
			g.Line(cx - 10, cy, cx + 10, cy, CA, 1.6); g.Line(cx, cy - 10, cx, cy + 10, CA, 1.6); g.Circle(cx, cy, 16, CA, 1.4);
			// drift, heading-up: 1 m/s = 40 units
			const V2 dv = MapXY(h.gsH.z, h.gsH.x, h.hdg, 40);
			const double ex = cx + (dv.first - 170), ey = cy + (dv.second - 170);
			g.Line(cx, cy, ex, ey, CW, 1.6); g.Circle(ex, ey, 3, CW, 1, 1, CW, 1);
			g.T(cx, cy + 88, "снос " + Num(d.gs, 1) + " м/с", CW, 10, 1);
			// vertical speed with the injury limits
			auto Yv = [](double v) { return 30 + (2 - v) * 11.1; };
			g.Rect(236, Yv(0), 18, Yv(-1.5) - Yv(0), -1, 0, 0, CP, 0.3);
			g.Rect(236, Yv(-1.5), 18, Yv(-7) - Yv(-1.5), -1, 0, 0, CA, 0.3);
			g.Rect(236, Yv(-7), 18, Yv(-14) - Yv(-7), -1, 0, 0, CR, 0.3);
			g.Rect(236, Yv(-14), 18, Yv(-16) - Yv(-14), -1, 0, 0, CR, 0.6);
			g.Rect(236, Yv(2), 18, Yv(-16) - Yv(2), CD, 1, 0.8);
			for (double v : { 2.0, 0.0, -1.5, -7.0, -14.0 }) { g.Line(254, Yv(v), 262, Yv(v), CP, 1); g.T(266, Yv(v) + 4, Num(v, v == -1.5 ? 1 : 0, true), CD, 9.5); }
			g.T(300, Yv(-10.5), "травма", CA, 9.5); g.T(300, Yv(-15) + 3, "гибель", CR, 9.5);
			const double vs = std::clamp(d.vs, -16.0, 2.0);
			g.Tri(232, Yv(vs), -10, -7, -10, 7, CW); g.T(245, 22, "ВЕРТ м/с", CD, 9.5, 1);
			g.Cell(14, 214, "ВЫСОТА", Num(d.alt, 1) + " м"); g.Cell(84, 214, "ВЕРТ", Num(d.vs, 1, true) + " м/с", d.vs < -1.5 ? CA : CW);
			g.Cell(160, 214, "КАСАНИЕ", d.vs < -0.05 ? "≈ " + Num(d.alt / -d.vs, 0) + " с" : "—");
			// slope under her from the map grid
			double slope = 0;
			const auto& G = h.grid;
			if (G.n > 2 && !G.h.empty())
			{
				const int m = G.n / 2; const double step = 2 * G.half / (G.n - 1);
				slope = std::atan(std::hypot(G.h[m * G.n + m + 1] - G.h[m * G.n + m - 1], G.h[(m + 1) * G.n + m] - G.h[(m - 1) * G.n + m]) / (2 * step)) * DEG;
			}
			g.Line(14, 252, 222, 252, CD, 1, 0.5, false, false);
			g.T(14, 270, "уклон под ногами " + Num(slope, 0) + "° " + (slope < 10 ? "✓" : "— круто"), slope < 10 ? CP : CA, 10.5);
			g.T(14, 288, "касание ≤ 1,5 м/с · травма > 7 · гибель > 14", CD, 9.5);
		}

		void Approach(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s) { Msg(g, "ВЫБЕРИТЕ ЦЕЛЬ", "на странице ЦЕЛИ или на карте"); return; }
			if (s->base) { Msg(g, "ЦЕЛЬ — БАЗА", "к базе только ПЕРЕЛЁТ"); return; }
			VESSEL* o = oapiGetVesselInterface(s->h);
			OBJHANDLE ref = c.v->GetGravityRef();
			VECTOR3 rt, vt; o->GetRelativePos(ref, rt); o->GetRelativeVel(ref, vt);
			const VECTOR3 rb = -Unit(rt), hb = Unit(crossp(rt, vt)), vb = Unit(crossp(hb, -rb));   // R-bar down, V-bar along the motion
			const double x = dotp(s->rel, vb), z = dotp(s->rel, rb), vx = dotp(s->relV, vb), vz = dotp(s->relV, rb);
			const double tx = 200, ty = 86, sc = 100 / (std::max)(10.0, s->dist * 1.3);
			g.T(14, 18, "ОТНОСИТЕЛЬНОЕ ДВИЖЕНИЕ · " + s->name, CD, 9.5);
			g.Line(10, ty, 330, ty, CD, 1, 0.6, true); g.Line(tx, 26, tx, 150, CD, 1, 0.6, true);
			g.T(326, ty - 5, "+V по ходу", CD, 9, 2); g.T(tx + 4, 148, "вниз ↓", CD, 9);
			const double side = x < 0 ? -1 : 1;
			g.Line(tx, ty, tx + side * 180, ty - 48, CA, 1, 0.45, true); g.Line(tx, ty, tx + side * 180, ty + 48, CA, 1, 0.45, true);
			if (30 * sc < 150) { g.Circle(tx, ty, 30 * sc, CR, 1, 0.7, -1, 0, true); g.T(tx + 30 * sc * 0.7 + 2, ty - 30 * sc * 0.7 - 2, "30 м", CR, 9); }
			g.Rect(tx - 10, ty - 3, 20, 6, CW, 1.2, 1, CW, 0.2);
			const double mx = tx + x * sc, my = ty + z * sc;
			g.Line(mx, my, mx + vx * 60 * sc, my + vz * 60 * sc, CA, 1.6, 1, true);
			g.Fill({ { mx, my - 8 }, { mx + 6, my + 6 }, { mx, my + 2 }, { mx - 6, my + 6 } }, CW, 1);
			const double closing = -s->rate, tgo = closing > 0.01 ? s->dist / closing : -1;
			g.Cell(14, 162, "ДАЛЬН", Dist(s->dist)); g.Cell(96, 162, "V СБЛИЖ", Num(s->rate, 2, true), CA);
			g.Cell(186, 162, "ДО ЦЕЛИ", Clock(tgo)); g.Cell(262, 162, "ΔV СИНХР", Num(length(s->relV), 2));
			// range - closing rate corridor (log range)
			auto X = [](double r) { return 36 + 96 * std::log10(std::clamp(r, 1.0, 1000.0)); };
			auto Y = [](double v) { return 288 - 88 * std::clamp(v, 0.0, 1.0); };
			g.Line(36, 288, 326, 288, CD, 1, 0.7, false, false); g.Line(36, 196, 36, 288, CD, 1, 0.7, false, false);
			std::vector<V2> up, lo;
			for (double e = 0; e <= 3.0001; e += 0.1) { const double r = std::pow(10, e); up.push_back({ X(r), Y(r / 60) }); lo.push_back({ X(r), Y(r / 200) }); }
			std::vector<V2> band = up; band.insert(band.end(), lo.rbegin(), lo.rend());
			g.Fill(band, CP, 0.16); g.Poly(up, CP, 0.8, 0.5);
			std::vector<V2> hist; for (const auto& p : h.rrHist) hist.push_back({ X(p.first), Y(p.second) });
			g.Poly(hist, CW, 1.2, 0.7);
			g.Circle(X(s->dist), Y(closing), 4, CA, 1.6, 1, CA, 1);
			for (double r : { 1.0, 10.0, 100.0, 1000.0 }) { g.Line(X(r), 288, X(r), 292, CD, 1); g.T(X(r), 300, r >= 1000 ? "1 км" : Num(r, 0), CD, 8.5, 1); }
			for (double v : { 0.0, 0.5, 1.0 }) g.T(32, Y(v) + 3, Num(v, v > 0 ? 1 : 0), CD, 8.5, 2);
			g.T(326, 206, "КОРИДОР СКОРОСТИ", CP, 9.5, 2);
		}

		void Dock(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			const SuitHud::Target* s = h.Selected();
			if (!s) { Msg(g, "ВЫБЕРИТЕ ЦЕЛЬ", "на странице ЦЕЛИ или на карте"); return; }
			if (s->base) { Msg(g, "ЦЕЛЬ — БАЗА", "к базе только ПЕРЕЛЁТ"); return; }
			VESSEL* o = oapiGetVesselInterface(s->h);
			VECTOR3 me; c.v->GetGlobalPos(me);
			VECTOR3 lp; o->Global2Local(me, lp);
			// the nearest port; without ports, the target's centre along the line to her
			VECTOR3 pos = _V(0, 0, 0), dir = Unit(lp), rot = _V(0, 1, 0);
			bool port = false; double best = 1e18;
			for (UINT i = 0; i < o->DockCount(); ++i)
			{
				VECTOR3 p, dd, r; o->GetDockParams(o->GetDockHandle(i), p, dd, r);
				const double dist = length(lp - p);
				if (dist < best) { best = dist; pos = p; dir = dd; rot = r; port = true; }
			}
			if (std::abs(dotp(dir, rot)) > 0.99) rot = std::abs(dir.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
			const VECTOR3 side = Unit(crossp(rot, dir)), upv = Unit(crossp(dir, side));
			const VECTOR3 rel = lp - pos;
			const double dx = dotp(rel, side), dy = dotp(rel, upv), dz = dotp(rel, dir);
			// her facing in the target's frame: she should look down the port axis (-dir)
			VECTOR3 fg; c.v->GlobalRot(_V(0, 0, 1), fg);
			MATRIX3 R; o->GetRotationMatrix(R);
			const VECTOR3 fl = tmul(R, fg);
			const double yaw = std::atan2(dotp(fl, side), -dotp(fl, dir)) * DEG, pit = std::atan2(dotp(fl, upv), -dotp(fl, dir)) * DEG;
			g.T(14, 18, port ? "УЗЕЛ ЦЕЛИ · " + s->name : "У ЦЕЛИ НЕТ УЗЛОВ · ЦЕНТР", port ? CA : CD, 9.5);
			const double cx = 140, cy = 132, sc = 60 / (std::max)(1.0, (std::min)(10.0, std::hypot(dx, dy) * 1.5 + 0.5));
			g.Circle(cx, cy, 100, CD, 1, 0.5); g.Circle(cx, cy, 60, CD, 1, 0.5); g.Circle(cx, cy, 20, CP, 1, 0.6, -1, 0, true);
			g.Line(cx - 100, cy, cx - 24, cy, CP, 1); g.Line(cx + 24, cy, cx + 100, cy, CP, 1); g.Line(cx, cy - 100, cx, cy - 24, CP, 1); g.Line(cx, cy + 24, cx, cy + 100, CP, 1);
			const double px = cx + std::clamp(-dx * sc, -95.0, 95.0), py = cy + std::clamp(dy * sc, -95.0, 95.0);
			g.Circle(px, py, 24, CA, 1.8); g.Line(px - 8, py, px + 8, py, CA, 1.4); g.Line(px, py - 8, px, py + 8, CA, 1.4);
			g.Line(cx, cy, px, py, CW, 1, 0.8, true);
			// range along the axis (log)
			auto Yr = [](double r) { return 246 - (std::log10(std::clamp(r, 0.1, 100.0)) + 1) * 54; };
			g.Line(290, Yr(100), 290, Yr(0.1), CD, 1, 0.8);
			for (double r : { 100.0, 10.0, 1.0, 0.1 }) { g.Line(290, Yr(r), 298, Yr(r), CP, 1); g.T(302, Yr(r) + 4, Num(r, r < 1 ? 1 : 0), CD, 9); }
			const double ax = std::abs(dz);
			g.Tri(288, Yr(ax), -10, -7, -10, 7, CA); g.T(274, Yr(ax) + 4, Num(ax, ax < 10 ? 2 : 1), CW, 11, 2);
			g.T(274, Yr(ax) + 20, Num(s->rate, 2, true) + " м/с", std::abs(s->rate) > 0.3 && ax < 5 ? CR : CA, 10, 2);
			g.T(290, 26, "Д, м", CD, 9.5, 1);
			g.Cell(14, 250, "ΔX", Num(dx, 2, true) + " м"); g.Cell(84, 250, "ΔY", Num(dy, 2, true) + " м");
			g.Cell(154, 250, "ТАНГАЖ", Num(pit, 1, true) + "°"); g.Cell(224, 250, "РЫСК", Num(yaw, 1, true) + "°");
		}

		void Body(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; const HudData& d = c.d; Grid(g);
			auto graph = [&](double y, const std::string& name, const std::string& val, const std::vector<double>& q, double lo, double hi, double nlo, double nhi, int col)
			{
				g.T(14, y, name, CD, 9.5); g.T(326, y, val, col, 12, 2);
				g.Rect(14, y + 6, 312, 34, CD, 1, 0.4);
				auto Y = [&](double v) { return y + 40 - 34 * std::clamp((v - lo) / (hi - lo), 0.0, 1.0); };
				g.Rect(15, Y(nhi), 310, Y(nlo) - Y(nhi), -1, 0, 0, CP, 0.1);
				std::vector<V2> pts;
				for (size_t i = 0; i < q.size(); ++i) pts.push_back({ 326 - (q.size() - 1 - i) * 5.2, Y(q[i]) });
				g.Poly(pts, col, 1.4);
			};
			graph(18, "ПУЛЬС · 10 мин", Num(d.pulse, 0) + " уд/мин", h.trend.pulse, 40, 200, 55, 120, d.pulse > 170 ? CA : CP);
			graph(64, "ДЫХАНИЕ", Num(d.breath, 0) + " в мин", h.trend.breath, 5, 50, 10, 25, CP);
			graph(110, "ТЕМПЕРАТУРА ТЕЛА", Num(d.coreC, 1) + " °C", h.trend.core, 34, 41, 36.2, 37.8, d.coreC > 38.5 || d.coreC < 35.8 ? CA : CP);
			// below the graphs: radiation, the state, what is wrong
			g.Line(14, 160, 326, 160, CD, 1, 0.5, false, false);
			{
				// radiation: the rate now and the dose of this outing against the 30-day limit (250 mSv); time left to it.
				// Colours by the same marks as the alerts: rate amber from 1 mSv/h, red from 10; dose amber from 100 mSv, red from 250
				const double rate = d.radRate * 1000, dose = d.radDose * 1000, L30 = 250;   // mSv/h, mSv
				const int rc = rate >= 10 ? CR : rate >= 1 ? CA : CW, dc = dose >= L30 ? CR : dose >= 100 ? CA : CW;
				g.T(14, 176, "РАДИАЦИЯ", CD, 9.5);
				g.T(326, 176, (rate < 1 ? Num(rate * 1000, 0) + " мкЗв/ч" : Num(rate, 2) + " мЗв/ч") + (d.fieldOn ? " · поле" : ""), rc, 11, 2);
				g.T(14, 194, "ДОЗА ЗА ВЫХОД", CD, 9.5);
				g.T(326, 194, Num(dose, dose < 10 ? 2 : 0) + " мЗв из " + Num(L30, 0), dc, 10.5, 2);
				g.Bar(14, 200, 312, 7, dose / L30, dc == CW ? CP : dc);
				const double left = rate > 1e-4 ? (L30 - dose) / rate : 1e9;
				if (left < 1e5) g.T(14, 220, "до предела: " + (left > 48 ? Num(left / 24, 0) + " сут" : Num(left, 1) + " ч"), left < 2 ? CR : left < 24 ? CA : CD, 9.5);
			}
			{
				// the state in words; injuries only if there are any; water and food only near their marks
				static const char* PART[4] = { "голова", "корпус", "руки", "ноги" };
				std::string hurts; bool bad = false;
				for (int i = 0; i < 4; ++i) if (d.hurt[i] > 0.05) { hurts += std::string(hurts.empty() ? "" : ", ") + PART[i] + (d.hurt[i] > 0.5 ? " (тяжело)" : ""); bad = bad || d.hurt[i] > 0.5; }
				const double dehyd = d.bodyMass > 0 ? d.waterDef / d.bodyMass : 0;
				std::string st = d.state == 2 ? "ГИБЕЛЬ" : d.state == 1 ? "БЕЗ СОЗНАНИЯ" : !hurts.empty() ? (bad ? "ТЯЖЁЛЫЕ ТРАВМЫ" : "ТРАВМЫ") : d.stamina < 0.15 ? "ИСТОЩЕНА" : dehyd > 0.04 || d.fastDays > 3 ? "ОСЛАБЛЕНА" : "В НОРМЕ";
				const int sc = d.state ? CR : bad ? CR : !hurts.empty() || st != "В НОРМЕ" ? CA : CP;
				const double keepR = g.clipR;
				g.Line(14, 228, 326, 228, CD, 1, 0.4, false, false);
				g.T(14, 240, "СОСТОЯНИЕ", CD, 9.5); g.T(326, 240, st, sc, 12, 2);
				double y = 258;
				if (!hurts.empty()) { g.T(14, y, hurts, bad ? CR : CA, 10); y += 16; }
				g.T(14, y, "нагрузка " + Num(100 * d.effort, 0) + " % · CO2 " + Num(d.ppCO2, 2) + " кПа", d.ppCO2 > 1 ? CA : CD, 9.5); y += 16;
				std::string need;
				if (d.suit && d.water < 0.3) need += "вода в скафандре " + Num(d.water, 1) + " л";
				if (dehyd > 0.02) need += std::string(need.empty() ? "" : " · ") + "обезвоживание " + Num(100 * dehyd, 1) + " %";
				if (d.fastDays > 2) need += std::string(need.empty() ? "" : " · ") + "без еды " + Num(d.fastDays, 1) + " сут";
				if (!need.empty()) g.T(14, y, need, dehyd > 0.04 || d.fastDays > 3 ? CA : CD, 9.5);
				g.clipR = keepR;
			}
		}
	}

	// ================= the display =================
	void SuitHud::Draw(oapi::Sketchpad* skp, const HUDPAINTSPEC* hps, const HudData& d, VESSEL* v)
	{
		if (!glyphsTried) { glyphsTried = true; glyphs.Load(); }
		const double W = hps->W, H = hps->H, k = H / 720.0;
		const double L0 = 0, C0 = W / 2 - 640 * k, R0 = W - 1280 * k;
		hits.clear();
		Gfx g(skp, *this, glyphs, pal, look);
		if (look == 1) { const double r = std::sin(oapiGetSimTime() * 1234.567 + oapiGetSysTime() * 789.1) * 43758.5453; g.shimmer = 0.94 + 0.06 * (r - std::floor(r)); }
		Ctx c{ g, d, *this, v, W, H, k, std::fmod(d.simt, 1.0) < 0.62 };
		Nav(v, d);
		Alerts(d);

		// automatic mode: what she is doing decides the pages until a mode key is pressed
		if (autoMode)
		{
			int m = EVA;
			const Target* s = Selected();
			if (d.jetFlying && surface) m = FLIGHT;
			else if (space && s && s->dist < 5000) m = RDV;
			if (m != mode) SetMode(m, false);
		}
		const bool fly = d.jet && d.jetFlying;

		// how bright it is around her: in the sun, over a lit ground, looking towards the sun
		{
			const double t = oapiGetSimTime();
			double amb = 0.1;
			if (d.sunlit)
			{
				VECTOR3 cd, cp, sp; oapiCameraGlobalDir(&cd); oapiCameraGlobalPos(&cp); oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp);
				const double toSun = (std::max)(0.0, dotp(cd, Unit(sp - cp)));
				amb = surface ? 0.45 + 0.4 * std::clamp(std::sin(sunElev) * 2, 0.0, 1.0) + 0.15 * toSun : 0.35 + 0.65 * toSun * toSun;
			}
			const double dt = lastAmbT < 0 || t < lastAmbT ? 1.0 : t - lastAmbT; lastAmbT = t;
			ambient += (std::clamp(amb, 0.0, 1.0) - ambient) * (std::min)(1.0, dt / 1.5);   // eyes and sensor adapt in a second or two
			g.boost = brightAuto ? ambient : brightManual;
		}
		if (d.suit && d.firstPerson)
		{
			if (nvg) NightVision(skp, v, W, H);
			if (nvg && surface && hBody)
			{
				// the ranger's relief: rings and spokes of the ground round her, drawn into the view through the camera;
				// brighter near, fading out by ~300 m. The image amplifier above needs some light; this needs none
				Relief& rl = relief;
				const double t = oapiGetSimTime(), R = bodyR;
				if (rl.body != hBody || rl.t < 0 || t - rl.t > 2 || t < rl.t || std::hypot((lat - rl.lat) * R, (lng - rl.lng) * R * std::cos(lat)) > 3)
				{
					rl.body = hBody; rl.lat = lat; rl.lng = lng; rl.t = t; rl.nr = 22; rl.na = 40;
					rl.lat_.assign(rl.nr * rl.na, 0); rl.lng_.assign(rl.nr * rl.na, 0); rl.rad_.assign(rl.nr * rl.na, 0);
					for (int k = 0; k < rl.nr; ++k)
						for (int a = 0; a < rl.na; ++a)
						{
							const double r = 3 * std::pow(1.25, k), az = a * PI2 / rl.na;
							const double pl = lat + r * std::cos(az) / R, pg = lng + r * std::sin(az) / (R * (std::max)(0.05, std::cos(lat)));
							const int i = k * rl.na + a;
							rl.lat_[i] = pl; rl.lng_[i] = pg; rl.rad_[i] = R + oapiSurfaceElevation(hBody, pg, pl) + 0.05;
						}
				}
				VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
				const double fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
				std::vector<V2> sp(rl.lat_.size()); std::vector<char> ok(rl.lat_.size());
				for (size_t i = 0; i < sp.size(); ++i)
				{
					VECTOR3 gp; oapiEquToGlobal(hBody, rl.lng_[i], rl.lat_[i], rl.rad_[i], &gp);
					const VECTOR3 q = tmul(Rc, gp - cp);
					ok[i] = q.z > 0.3; if (ok[i]) sp[i] = { W / 2 + q.x / q.z * fpx, H / 2 - q.y / q.z * fpx };
				}
				g.Frame(0, 0, 1);
				for (int k = 0; k < rl.nr; ++k)
				{
					const double al = 0.85 * std::pow(1.0 - static_cast<double>(k) / rl.nr, 0.8);
					for (int a = 0; a < rl.na; ++a)
					{
						const int i = k * rl.na + a, j = k * rl.na + (a + 1) % rl.na, o = (k + 1) * rl.na + a;
						if (ok[i] && ok[j]) g.Line(sp[i].first, sp[i].second, sp[j].first, sp[j].second, CP, 1, al, false, false);
						if (k + 1 < rl.nr && ok[i] && ok[o]) g.Line(sp[i].first, sp[i].second, sp[o].first, sp[o].second, CP, 1, al * 0.7, false, false);
					}
				}
			}
			// the sun shade: a gold filter against bright sources; the brighter it is around, the more it takes
			if (d.shade > 0.02 && g.s2)
			{
				const DWORD a = static_cast<DWORD>((0x40 + 0x50 * g.boost) * std::clamp(d.shade, 0.0, 1.0));
				g.s2->QuickPen(0); g.s2->QuickBrush((a << 24) | 0x00081830);   // 0xAABBGGRR: dark amber
				skp->Rectangle(0, 0, static_cast<int>(W), static_cast<int>(H)); g.s2->QuickBrush(0);
			}
		}
		else if (nvCam && nvg) gcCustomCameraOnOff(nvCam, false);

		// a dark tint behind the display: soft bands along the visor's upper and lower edge, and feathered backings under
		// the two side panels - no hard boxes. The brighter around, the denser (lines stay readable on a sunlit ground)
		if (d.suit && g.boost > 0.05)
		{
			const double a = 0.10 + 0.40 * (std::min)(1.0, g.boost);
			g.Frame(0, 0, 1); g.world = true;
			// thin strips (a few pixels each), cosine fall-off: no visible steps
			const int N = 48;
			const double topH = 140 * k, botH = 115 * k;
			for (int i = 0; i < N; ++i)
			{
				const double x = (i + 0.5) / N, f = 0.5 + 0.5 * std::cos(PI * x);           // 1 at the edge .. 0 inside
				g.Rect(0, std::floor(i * topH / N), W, std::ceil(topH / N) + 1, -1, 0, 0, CK, 0.85 * a * f);
				g.Rect(0, H - std::floor((i + 1) * botH / N), W, std::ceil(botH / N) + 1, -1, 0, 0, CK, 0.7 * a * f);
			}
			auto soft = [&](double x, double y, double w, double h)
			{
				for (int i = 7; i >= 0; --i) g.Rect(x - i * 2, y - i * 2, w + i * 4, h + i * 4, -1, 0, 0, CK, a * 0.13);
			};
			g.world = false;
			g.Frame(L0, 0, k); soft(18, 166, 242, 170);
			g.Frame(R0, 0, k); soft(1020, 166, 242, 192);
		}
		g.Frame(C0, 0, k);
		if (d.state == 2) { g.T(640, 360, "ГИБЕЛЬ", CR, 15, 1); return; }

		// ---- the rim: body and suit in one line each, on the visor's edge ----
		{
			auto arcLine = [&](double a0, double a1)
			{
				std::vector<V2> pts;
				for (int i = 0; i <= 24; ++i) { const double a = a0 + (a1 - a0) * i / 24; pts.push_back({ 640 + 1492 * std::cos(a), 1560 + 1492 * std::sin(a) }); }
				g.Poly(pts, CP, 1.2, 0.55);
			};
			// the rim's readouts: fixed slots - each label stays where it is, each value is right-aligned in room kept for its
			// widest form, so nothing slides along the arc as the numbers change
			struct Slot { std::string label, value, room; int col; };
			auto charsOn = [&](double s0, const std::string& txt, int col, double amid)
			{
				double sArc = s0;
				for (const std::string& ch : Chars(txt))
				{
					const double w = g.TW(ch, 13), a = amid + (sArc + w / 2) / 1500;
					g.T(640 + 1500 * std::cos(a), 1560 + 1500 * std::sin(a), ch, col, 13, 1);
					sArc += w;
				}
			};
			auto arcSlots = [&](double amid, const std::vector<Slot>& slots)
			{
				const double sep = g.TW(" · ", 13);
				double total = 0;
				for (size_t i = 0; i < slots.size(); ++i) total += g.TW(slots[i].label + " ", 13) + g.TW(slots[i].room, 13) + (i + 1 < slots.size() ? sep : 0);
				double sArc = -total / 2;
				for (size_t i = 0; i < slots.size(); ++i)
				{
					const Slot& sl = slots[i];
					charsOn(sArc, sl.label + " ", CD, amid); sArc += g.TW(sl.label + " ", 13);
					const double room = g.TW(sl.room, 13);
					charsOn(sArc + room - g.TW(sl.value, 13), sl.value, sl.col, amid); sArc += room;
					if (i + 1 < slots.size()) { charsOn(sArc, " · ", CD, amid); sArc += sep; }
				}
			};
			arcLine(-1.925, -1.672); arcLine(-1.470, -1.217);
			arcSlots(-1.798, { { "ПУЛЬС", Num(d.pulse, 0), "888", CW }, { "ДЫХ", Num(d.breath, 0), "88", CW },
				{ "O2", Num(d.ppO2, 0) + " кПа", "888 кПа", d.ppO2 < 14 ? CR : d.ppO2 < 17 ? CA : CW }, { "ТЕЛО", Num(d.coreC, 1) + "°", "88,8°", d.coreC > 38.5 || d.coreC < 35.8 ? CA : CW } });
			if (d.suit)
				arcSlots(-1.344, { { "O2", Num(100 * d.o2, 0) + " %", "100 %", d.o2 < 0.25 ? CA : CW }, { "БАТ", Num(100 * d.batt, 0) + " %", "100 %", d.batt < 0.25 ? CA : CW },
					{ "", Num(d.powerW, 0) + " Вт", "8888 Вт", CW }, { "СРЕДА", Num(d.envC, 0, true) + "°", "+888°", d.inSpec ? CW : CR } });
			else
				arcSlots(-1.344, { { "КОМБИНЕЗОН ·", d.breathable ? "воздух пригоден" : "дышать нельзя", "воздух пригоден", d.breathable ? CW : CR } });
		}

		// ---- corners ----
		g.Frame(L0, 0, k);
		g.T(24, 32, Upper(d.name), CW, 15); g.T(24, 49, d.role + " · «Тантра»", CD, 9.5);
		g.Frame(R0, 0, k);
		{
			const double mjd = oapiGetSimMJD(), day = (mjd - std::floor(mjd)) * 86400;
			char b[32]; snprintf(b, sizeof b, "UTC %02d:%02d:%02d", static_cast<int>(day) / 3600, (static_cast<int>(day) / 60) % 60, static_cast<int>(day) % 60);
			g.T(1256, 32, b, CW, 15, 2);
			char bn[64] = ""; if (hBody) oapiGetObjectName(hBody, bn, 64);
			const char* ru = BodyRu(bn);
			std::string where = std::string(ru ? ru : bn) + (surface ? (sunElev > 0 ? " · день" : " · ночь") : " · орбита");
			where += d.vacuum ? " · вакуум" : " · " + Num(d.airKPa, 0) + " кПа";
			g.T(1256, 49, where, CD, 9.5, 2);
			if (d.suit)
			{
				// the suit's switches on the right; the pack's control block (assistant, limiter) apart on their left, framed
				struct Tg { const char* n; bool on; int kind, arg; };
				const Tg suitTg[4] = { { "ПОЛЕ", d.fieldOn, H_AP, AP_FIELD }, { "СВЕТ", d.lampsOn, H_AP, AP_LAMP }, { "ЩИТОК", d.shadeDown, H_AP, AP_SHADE }, { "ПНВ", nvg, H_NVG, 0 } };
				const Tg jetTg[2] = { { "ПОМОЩНИК", d.jetAssist, H_AP, AP_MANUAL }, { "ОГРАНИЧ.", d.jetFine, H_AP, AP_FINE } };
				double bx = 1256;
				auto button = [&](const Tg& t)
				{
					const double w = g.TW(t.n, 11) + 18; bx -= w;
					if (t.on) { g.Rect(bx, 58, w, 20, -1, 0, 0, CA, 1); g.T(bx + w / 2, 72, t.n, CK, 11, 1); }
					else { g.Rect(bx, 58, w, 20, CP, 1, 0.6, CP, 0.08); g.T(bx + w / 2, 72, t.n, CP, 11, 1); }
					g.Hit(bx, 58, w, 20, t.kind, t.arg);
					bx -= 5;
				};
				for (int i = 3; i >= 0; --i) button(suitTg[i]);
				if (d.jet)
				{
					// the pack's block: its own row under the suit's switches, right-aligned (the rim's numbers stay clear)
					bx = 1256;
					auto button2 = [&](const Tg& t)
					{
						const double w = g.TW(t.n, 11) + 18; bx -= w;
						if (t.on) { g.Rect(bx, 104, w, 20, -1, 0, 0, CA, 1); g.T(bx + w / 2, 118, t.n, CK, 11, 1); }
						else { g.Rect(bx, 104, w, 20, CP, 1, 0.6, CP, 0.08); g.T(bx + w / 2, 118, t.n, CP, 11, 1); }
						g.Hit(bx, 104, w, 20, t.kind, t.arg);
						bx -= 5;
					};
					for (int i = 1; i >= 0; --i) button2(jetTg[i]);
				}
				if (nvg && d.firstPerson && nvNote != "ПНВ") g.T(1256, 94, nvNote, CA, 10, 2);
			}
		}

		// ---- modes and compass ----
		g.Frame(C0, 0, k);
		{
			static const char* NAMES[4] = { "ВКД", "ПОЛЁТ", "СБЛИЖЕНИЕ", "СИСТЕМЫ" };
			const double xs[4] = { 490, 580, 690, 800 };
			for (int i = 0; i < 4; ++i)
			{
				const bool on = i == mode; const double w = g.TW(NAMES[i], 12.5) + 16;
				if (on) g.Rect(xs[i] - w / 2, 12, w, 22, CP, 1.2, 1, CP, 0.14);
				g.T(xs[i], 28, NAMES[i], on ? CP : CD, 12.5, 1);
				g.T(xs[i] - w / 2 + 3, 10, std::to_string(i + 1), CD, 8.5);
				g.Hit(xs[i] - w / 2, 8, w, 28, H_MODE, i);
			}
			g.T(868, 28, autoMode ? "АВТО" : "РУЧН", autoMode ? CD : CA, 9.5);
			const double hd = std::fmod(hdg * DEG + 360, 360), y = 72;
			for (int dd = static_cast<int>(std::ceil((hd - 30) / 5)) * 5; dd <= hd + 30; dd += 5)
			{
				const double x = 640 + (dd - hd) * 4; const int a = ((dd % 360) + 360) % 360; const bool big = a % 10 == 0;
				g.Line(x, y, x, y - (big ? 8 : 4), CP, 1, 0.8, false, false);
				if (big)
				{
					char b[8]; snprintf(b, sizeof b, "%03d", a);
					g.T(x, y - 12, a == 0 ? "С" : a == 90 ? "В" : a == 180 ? "Ю" : a == 270 ? "З" : b, CD, 9.5, 1);
				}
			}
			g.Line(520, y, 760, y, CP, 1, 0.5, false, false);
			g.Rect(616, y + 4, 48, 18, CP, 1.2, 1, CK, 0.5);
			char b[8]; snprintf(b, sizeof b, "%03.0f°", hd); g.T(640, y + 18, b, CW, 13, 1);
			const double ws = std::hypot(d.wind.x, d.wind.z);
			if (!d.vacuum && ws > 0.1)
			{
				// where the wind blows to, relative to her heading
				const double wa = std::atan2(d.wind.x, d.wind.z) - hdg; double from = std::fmod((std::atan2(-d.wind.x, -d.wind.z)) * DEG + 360, 360);
				g.Arrow(700 - 9 * std::sin(wa), y + 13 + 9 * std::cos(wa), 700 + 9 * std::sin(wa), y + 13 - 9 * std::cos(wa), CP, 1.4);
				char wb[48]; snprintf(wb, sizeof wb, "%03.0f°", from);
				g.T(714, y + 18, "ВЕТЕР " + Num(ws, 1) + " м/с · с " + wb, CD, 9.5);
			}
		}

		// ---- caution & warning: the master strips at the edges of view, the system lamps, the message ribbon ----
		{
			bool anyW = false, anyC = false, unacked = false;
			for (const auto& a : alerts) if (a.active) { (a.level == 2 ? anyW : anyC) = true; if (!a.ack) unacked = true; }
			const bool lit = anyW || anyC, on = lit && (!unacked || c.blink);
			const int mc = anyW ? CR : CA;
			// the side vision catches a blink at the very edge; a click on either strip acknowledges everything
			for (int sdx = 0; sdx < 2; ++sdx)
			{
				g.Frame(sdx ? R0 : L0, 0, k);
				const double x = sdx ? 1268 : 6;
				if (on) g.Rect(x, 250, 6, 190, mc, 1, 1, mc, 1); else g.Rect(x, 250, 6, 190, lit ? mc : CD, 1, lit ? 0.6 : 0.3);
				g.Hit(x - 6, 240, 18, 210, H_ACK, -1);
			}
			g.Frame(C0, 0, k);
			size_t shown = 0;   // lines of the ribbon on show (the message goes under them)
			static const char* TILE[8] = { "O2", "CO2", "ДАВЛ", "БАТ", "ТЕПЛО", "РАДИАЦ", "СВЯЗЬ", "РЕЗЕРВ" };
			static const char* SHORT[8] = { "O2", "CO2", "ДАВЛ", "БАТ", "ТЕПЛ", "РАД", "СВЯЗ", "РЕЗ" };
			if (d.suit)
			{
				for (int i = 0; i < 8; ++i)
				{
					int lvl = 0; bool un = false;
					for (const auto& a : alerts) if (a.active && a.tile == TILE[i]) { lvl = (std::max)(lvl, a.level); un = un || !a.ack; }
					const double x = 640 - 217 + i * 62;
					const int col = lvl == 2 ? CR : CA;
					if (lvl && (!un || c.blink)) g.Circle(x, 104, 4, col, 1.2, 1, col, 1); else g.Circle(x, 104, 4, lvl ? col : CD, 1, lvl ? 0.7 : 0.4);
					g.T(x + 8, 108, SHORT[i], lvl ? col : CD, 8.5);
				}
				g.Hit(640 - 230, 96, 500, 16, H_ACK, -1);
				// the three newest: what is on now first, then what has cleared
				std::vector<int> order;
				for (int i = 0; i < static_cast<int>(alerts.size()); ++i) if (alerts[i].active) order.push_back(i);
				for (int i = 0; i < static_cast<int>(alerts.size()); ++i) if (!alerts[i].active) order.push_back(i);
				shown = (std::min)(order.size(), static_cast<size_t>(3));
				for (size_t n = 0; n < order.size() && n < 3; ++n)
				{
					const Alert& a = alerts[order[n]];
					const double y = 148 + n * 15;
					char tb[16]; snprintf(tb, sizeof tb, "%02d:%02d:%02d  ", static_cast<int>(a.t0) / 3600, (static_cast<int>(a.t0) / 60) % 60, static_cast<int>(a.t0) % 60);
					const std::string line = tb + std::string(a.level == 2 ? "! " : "▲ ") + a.text + (a.active ? (a.ack ? "" : "  ·  подтвердите щелчком") : "  ·  снято");
					const int col = !a.active ? CD : a.level == 2 ? CR : CA;
					if (a.active && !a.ack && !c.blink) continue;
					g.T(640, y, line, col, 10.5, 1);
					g.Hit(400, y - 11, 480, 14, H_ACK, order[n]);
				}
			}
			if (d.suit && lit)
			{
				// the word itself, under the lamps: ОПАСНОСТЬ (red) / ВНИМАНИЕ (amber); blinks until acknowledged, a click acknowledges
				const std::string word = anyW ? "ОПАСНОСТЬ" : "ВНИМАНИЕ";
				if (!unacked || c.blink) { const double w = g.TW(word, 13) + 24; g.Rect(640 - w / 2, 112, w, 20, mc, 1.4, 1, mc, unacked ? 0.25 : 0.1); g.T(640, 127, word, mc, 13, 1); }
				g.Hit(640 - 70, 110, 140, 24, H_ACK, -1);
			}
			if (!d.message.empty()) g.T(640, 148 + shown * 15, d.message, d.messageLevel >= 2 ? CR : d.messageLevel == 1 ? CA : CW, 12.5, 1);
		}

		// ---- thermal control (left) and consumption (right) ----
		if (d.suit)
		{
			g.Frame(L0, 0, k);
			const double x = 24, y = 180, w = 230;
			const bool holds = std::abs(d.residualW) < 30;
			g.T(x, y, "ТЕРМОКОНТРОЛЬ", CP, 12.5); g.T(x + w, y, !d.powered ? "НЕТ ПИТАНИЯ" : holds ? "НОРМА" : "НЕ ДЕРЖИТ", holds && d.powered ? CP : CR, 11, 2);
			g.Line(x, y + 6, x + w, y + 6, CP, 1, 0.5, false, false);
			struct R { std::string n, v; int c; };
			std::vector<R> rows = {
				{ "В скафандре", Num(d.tInC, 1) + " °C · " + (holds && d.powered ? "держится" : d.residualW > 0 ? "растёт" : "падает") + (Trend(4, 0.3) > 0 ? " ↑" : Trend(4, 0.3) < 0 ? " ↓" : ""), d.tInC > 30 || d.tInC < 12 ? CR : holds && d.powered ? CW : CA },
				{ "Тело", Num(d.coreC, 1) + " °C", d.coreC > 38.5 || d.coreC < 35.8 ? CA : CW },
				{ "Снаружи", d.vacuum ? std::string("вакуум") : Num(d.airKPa, d.airKPa < 10 ? 2 : 1) + " кПа · " + (d.breathable ? "воздух" : "атмосфера"),
					d.suitBreached || d.airKPa > d.suitPMax ? CR : d.airKPa > 0.75 * d.suitPMax ? CA : CW },
				{ d.vacuum ? "Среда (излучение)" : "Воздух", d.vacuum ? Num(d.envC, 0, true) + " °C" + (d.sunlit ? " · солнце" : " · тень") : Num(d.airC, 0, true) + " °C" + (d.sunlit ? " · солнце" : " · тень"), d.inSpec ? CW : CR } };
			if (d.hasGround) rows.push_back({ "Грунт", Num(d.groundC, 0, true) + " °C", d.groundC > 80 || d.groundC < -100 ? CA : CW });
			rows.push_back({ "Теплообмен", std::string(d.heatW >= 0 ? "ОХЛАЖДЕНИЕ " : "ОБОГРЕВ ") + Num(std::abs(d.heatW), 0) + " Вт", CP });
			rows.push_back({ std::string("Радиация") + (d.fieldOn ? " · поле" : ""),
				(doseRate >= 1000 ? Num(doseRate / 1000, 2) + " мЗв/ч" : Num(doseRate, doseRate < 1 ? 2 : 0) + " мкЗв/ч") + " · " + Num(dose / 1000, 2) + " мЗв",
				doseRate > 10000 ? CR : doseRate > 1000 ? CA : CW });
			for (size_t i = 0; i < rows.size(); ++i) { g.T(x, y + 24 + i * 17, rows[i].n, CD, 11); g.T(x + w, y + 24 + i * 17, rows[i].v, rows[i].c, 11.5, 2); }
			// the rated range with the environment and the ground on it
			const double yr = y + 24 + rows.size() * 17 + 8;
			auto X = [&](double t) { return x + std::clamp((t - d.ratedMinC) / (d.ratedMaxC - d.ratedMinC), 0.0, 1.0) * w; };
			g.Line(x, yr + 3, x + w, yr + 3, CP, 2, 0.5); g.Line(x, yr - 3, x, yr + 9, CR, 2); g.Line(x + w, yr - 3, x + w, yr + 9, CR, 2);
			g.Tri(X(d.envC), yr - 1, -4, -7, 4, -7, d.inSpec ? CP : CR);
			if (d.hasGround) g.Tri(X(d.groundC), yr + 7, -4, 7, 4, 7, CA);
			g.T(x, yr + 22, Num(d.ratedMinC, 0), CD, 9.5); g.T(x + w / 2, yr + 22, "допуск снаружи", CD, 9.5, 1); g.T(x + w, yr + 22, Num(d.ratedMaxC, 0, true), CD, 9.5, 2);

			g.Frame(R0, 0, k);
			const double rx = 1256 - w;
			const double lim = (std::min)({ d.battHours, d.o2Hours > 0 ? d.o2Hours : 1e9, d.sorbHours > 0 ? d.sorbHours : 1e9 });
			const char* limName = lim == d.battHours ? "батарея" : lim == d.o2Hours ? "кислород" : "поглотитель";
			g.T(rx, y, "РАСХОД", CP, 12.5); g.T(rx + w, y, "хватит на " + Hours(lim) + " · " + limName, lim < 1 ? CR : CA, 11, 2);
			g.Line(rx, y + 6, rx + w, y + 6, CP, 1, 0.5, false, false);
			g.T(rx, y + 24, "Питание", CD, 11); g.T(rx + w, y + 24, Num(d.powerW, 0) + " Вт", CW, 11.5, 2);
			struct Part { const char* n; double v; double a; } parts[] = { { "ЖО", d.lifeW, 1 }, { "ТЕПЛО", d.thermalW, 0.75 }, { "ПРИВ", d.driveW, 0.5 }, { "ПОЛЕ", d.fieldW, 0.4 }, { "СВЕТ", d.lampW, 0.3 } };
			double px = rx;
			for (const Part& p : parts)
			{
				if (p.v < 0.5 || d.powerW < 1) continue;
				const double pw = w * p.v / d.powerW;
				g.Rect(px, y + 31, (std::max)(1.0, pw - 2), 7, -1, 0, 0, CP, p.a);
				if (pw > 34) g.T(px + (pw - 2) / 2, y + 50, std::string(p.n) + " " + Num(p.v, 0), CD, 8.5, 1);
				px += pw;
			}
			struct Row { std::string n, v; double f; bool warn; } rows2[] = {
				{ "Батарея", Num(100 * d.batt, 0) + " % · " + Hours(d.battHours) + (Trend(1, 0.003) < 0 ? " ↓" : ""), d.batt, lim == d.battHours || Trend(1, 0.003) < 0 },
				{ "Кислород", d.o2Vent ? "из воздуха · баллон " + Num(100 * d.o2, 0) + " %" : Num(d.o2Flow * 60000, 2) + " г/мин · " + Hours(d.o2Hours) + (Trend(0, 0.003) < 0 ? " ↓" : ""), d.o2, lim == d.o2Hours || Trend(0, 0.003) < 0 },
				{ "Поглотитель CO2", Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours) + (Trend(2, 0.003) < 0 ? " ↓" : ""), d.sorbent, lim == d.sorbHours || Trend(2, 0.003) < 0 },
				{ "Мет. водород", d.jet ? (fly ? Num(d.jetFlow * 1000, 0) + " г/с · " : Num(100 * d.jetFuel, 0) + " % · ") + Num(d.jetDv, 0) + " м/с" + (d.jetHover > 0 ? " · висение " + Clock(d.jetHover) : "") : "ранец снят", d.jet ? d.jetFuel : 0, false },
				{ "Азот РСУ", Num(100 * d.n2, 0) + " % · " + Num(d.n2Dv, 1) + " м/с", d.n2, false } };
			for (int i = 0; i < 5; ++i)
			{
				const double yy = y + 70 + i * 24; const Row& r = rows2[i];
				g.T(rx, yy, r.n, CD, 11); g.T(rx + w, yy, r.v, r.warn || (i == 3 && fly) ? CA : CW, 11.5, 2);
				g.Bar(rx, yy + 5, w, 6, r.f, r.f < 0.1 ? CR : r.f < 0.25 || r.warn ? CA : CP);
			}
		}

		// ---- side scales, low beside the MFDs ----
		auto vscale = [&](double x, double f, int col, const std::string& name, const std::string& val)
		{
			const double y0 = 476, hh = 196;
			g.Rect(x, y0, 10, hh, CD, 1, 0.8);
			f = std::clamp(f, 0.0, 1.0);
			if (f > 0.002) g.Rect(x + 1, y0 + 1 + (hh - 2) * (1 - f), 8, (hh - 2) * f, -1, 0, 0, col, 0.9);
			g.T(x + 5, y0 + hh + 16, name, CD, 9.5, 1); g.T(x + 5, y0 - 8, val, CW, 11, 1);
		};
		auto lvl = [](double f) { return f < 0.1 ? CR : f < 0.25 ? CA : CP; };
		if (d.suit)
		{
			g.Frame(L0, 0, k);
			vscale(30, d.o2, lvl(d.o2), "O2", Num(100 * d.o2, 0)); vscale(58, d.sorbent, lvl(d.sorbent), "CO2", Num(100 * d.sorbent, 0)); vscale(86, d.batt, lvl(d.batt), "БАТ", Num(100 * d.batt, 0));
			g.Frame(R0, 0, k);
			if (d.jet) { vscale(1184, d.jetFuel, lvl(d.jetFuel), "ТОПЛ", Num(100 * d.jetFuel, 0)); vscale(1212, d.jetThrottle, CA, "ТЯГА", Num(100 * d.jetThrottle, 0)); }
			else vscale(1212, d.n2, lvl(d.n2), "N2", Num(100 * d.n2, 0));
			vscale(1240, d.stamina, lvl(d.stamina), "СИЛЫ", Num(100 * d.stamina, 0));
		}

		// ---- the middle: horizon, reticle, flight path, tapes ----
		g.Frame(C0, 0, k);
		{
			const double f = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture())) / k;   // units per radian
			const double cx = 640, cy = 360;
			if (surface && mode != SYS && d.suit)
			{
				const double off = std::clamp(std::tan(pitch) * f, -250.0, 250.0), ca = std::cos(bank), sa = std::sin(bank);
				auto seg = [&](double a, double b, double dy, bool dash)
				{
					const double y = cy + off + dy;
					g.Line(cx + a * ca, y + a * sa, cx + b * ca, y + b * sa, CP, 1.2, dash ? 0.6 : 1, dash, !dash);
				};
				seg(-100, -28, 0, false); seg(28, 100, 0, false);
				const double tenDeg = std::tan(10 * RAD) * f;
				seg(-50, -24, -tenDeg, false); seg(24, 50, -tenDeg, false); seg(-50, -24, tenDeg, true); seg(24, 50, tenDeg, true);
				g.T(cx - 56, cy + off - tenDeg + 4, "10", CD, 9.5, 2); g.T(cx - 56, cy + off + tenDeg + 4, "−10", CD, 9.5, 2);
			}
			if (d.suit)
			{
				g.Circle(cx, cy, 3.5, CP, 1.3);
				g.Line(cx - 12, cy, cx - 6, cy, CP, 1); g.Line(cx + 6, cy, cx + 12, cy, CP, 1); g.Line(cx, cy - 12, cx, cy - 6, CP, 1);
			}
			// the velocity vector (flight path marker) in the view: where she is going, projected through the camera as it
			// really looks (any tilt, any bank). Near a surface: over the ground; in space: against the selected target or,
			// without one, the body she orbits. Behind her: the anti-velocity marker (a circle with a cross) where she comes from.
			if (d.suit && mode != SYS)
			{
				g.world = true;
				VECTOR3 vg{};
				if (surface) v->GetGroundspeedVector(FRAME_GLOBAL, vg);
				else if (const Target* s = Selected()) v->GetRelativeVel(s->h, vg);
				else v->GetRelativeVel(v->GetGravityRef(), vg);
				const double sp = length(vg);
				if (sp > 0.3)
				{
					MATRIX3 R; oapiCameraRotationMatrix(&R);
					const VECTOR3 c = tmul(R, vg / sp);
					auto put = [&](const VECTOR3& dir, double& x, double& y)   // false: outside the view
					{
						if (dir.z < 0.05) return false;
						x = cx + dir.x / dir.z * f; y = cy - dir.y / dir.z * f;
						return std::abs(x - cx) < 300 && std::abs(y - cy) < 220;
					};
					double x, y;
					if (put(c, x, y))
					{
						g.Circle(x, y, 7, CW, 1.6); g.Line(x - 20, y, x - 7, y, CW, 1.6); g.Line(x + 7, y, x + 20, y, CW, 1.6); g.Line(x, y - 7, x, y - 14, CW, 1.6);
						g.T(x + 24, y + 4, Num(sp, sp < 10 ? 1 : 0) + " м/с", CW, 9.5);
					}
					else if (put(-c, x, y))
					{
						g.Circle(x, y, 7, CA, 1.6); g.Line(x - 5, y - 5, x + 5, y + 5, CA, 1.4); g.Line(x - 5, y + 5, x + 5, y - 5, CA, 1.4);
					}
					else   // neither in the view: a mark on the ring towards the velocity
					{
						const double a = std::atan2(-c.y, c.x), r = 200;
						const double px = cx + r * std::cos(a), py = cy + r * std::sin(a) * 0.8;
						g.Circle(px, py, 5, CW, 1.4, 0.8); g.Line(px - 11, py, px - 5, py, CW, 1.4); g.Line(px + 5, py, px + 11, py, CW, 1.4);
					}
				}
			}
			g.world = false;
			auto tape = [&](double x, double val, double step, int maj, double px, int sgn, int dec, const std::string& label, bool hasTgt, double tgt)
			{
				const double hh = 150, top = cy - hh / 2, bot = cy + hh / 2;
				g.Line(x, top, x, bot, CD, 1, 0.7, false, false);
				for (int kk = static_cast<int>(std::ceil((val - hh / 2 / px) / step)); kk <= static_cast<int>(std::floor((val + hh / 2 / px) / step)); ++kk)
				{
					const double vv = kk * step, y = cy - (vv - val) * px; const bool big = std::abs(kk) % maj == 0;
					g.Line(x, y, x + sgn * (big ? 8 : 4), y, CP, 1, 0.8, false, false);
					if (big) g.T(x + sgn * 12, y + 4, Num(vv, dec), CD, 9.5, sgn > 0 ? 0 : 2);
				}
				g.Poly({ { x, cy }, { x + sgn * 8, cy - 10 }, { x + sgn * 54, cy - 10 }, { x + sgn * 54, cy + 10 }, { x + sgn * 8, cy + 10 } }, CP, 1.2, 1, true);
				g.T(x + sgn * 31, cy + 5, Num(val, dec), CW, 13, 1);
				if (hasTgt) { const double y = std::clamp(cy - (tgt - val) * px, top, bot); g.Tri(x - sgn, y, -sgn * 8, -6, -sgn * 8, 6, CA); }
				g.T(x, top - 8, label, CD, 9.5, 1);
			};
			if (mode == FLIGHT && d.jet && surface)
			{
				tape(510, d.gs, 0.5, 2, 20, -1, 1, "ГОРИЗ м/с", false, 0);
				tape(770, d.alt, d.alt > 30 ? 5 : 0.5, 2, d.alt > 30 ? 2 : 14, 1, 1, "ВЫСОТА м", d.jetMode == 1, d.jetAltHold);
				// over the tapes: the two speeds that land her - horizontal (with the track and the drift) and vertical
				auto box = [&](double x0, const std::string& title, const std::string& val, int vc, const std::string& sub, int sc)
				{
					g.Rect(x0, 214, 124, 54, CP, 1.2, 0.8, CK, 0.35);
					g.T(x0 + 62, 227, title, CD, 9.5, 1);
					g.T(x0 + 62, 249, val, vc, 15, 1);
					g.T(x0 + 62, 263, sub, sc, 9, 1);
				};
				double track = std::atan2(gsH.x, gsH.z) * DEG; if (track < 0) track += 360;
				const double drift = Wrap(std::atan2(gsH.x, gsH.z) - hdg) * DEG;
				char tb[48]; snprintf(tb, sizeof tb, "ПУТЬ %03.0f° · СНОС %+.0f°", track, d.gs > 0.3 ? drift : 0.0);
				box(424, "V ГОРИЗ", Num(d.gs, 1) + " м/с", CW, d.gs > 0.3 ? std::string(tb) : std::string("на месте"), CD);
				const bool sinkHard = d.vs < -(1.0 + 0.8 * d.alt) && d.alt < 25;   // the same limit as the pack's ground guard
				box(716, "V ВЕРТ", Num(d.vs, 1, true) + " м/с", sinkHard ? CR : CW,
					d.vs > 0.2 ? "НАБОР ↑" : d.vs < -0.2 ? (sinkHard ? "СНИЖЕНИЕ ↓ БЫСТРО" : "СНИЖЕНИЕ ↓") : "ВИСЕНИЕ", sinkHard ? CR : CD);
			}
			if (mode == RDV)
				if (const Target* s = Selected())
				{
					tape(510, -s->rate, 0.05, 2, 220, -1, 2, "СБЛИЖ м/с", true, (std::min)(1.0, s->dist / 100));
					tape(770, s->dist, s->dist > 200 ? 50 : 5, 2, s->dist > 200 ? 0.3 : 3, 1, 1, "ДАЛЬН м", false, 0);
				}
		}

		// ---- the selected target in the view: a box, or an arrow at the edge ----
		if (const Target* s = Selected(); s && d.suit && mode != SYS)
		{
			g.world = true;   // the box is where the target is in the view
			VECTOR3 cp, tp; oapiCameraGlobalPos(&cp); oapiGetGlobalPos(s->h, &tp);
			MATRIX3 R; oapiCameraRotationMatrix(&R);
			const VECTOR3 cam = tmul(R, tp - cp);
			const double fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
			g.Frame(0, 0, 1);
			const std::string lab = s->name + " · " + Dist(s->dist);
			const double sx = cam.z > 0 ? W / 2 + cam.x / cam.z * fpx : 0, sy = cam.z > 0 ? H / 2 - cam.y / cam.z * fpx : 0;
			if (cam.z > 0 && sx > 40 * k && sx < W - 40 * k && sy > 90 * k && sy < H - 40 * k)
			{
				// a thin ring of light round it and a hairline up to its name, dim: drawn into the scene, not over it
				const double rr = std::clamp((s->base ? 30.0 : oapiGetSize(s->h)) / cam.z * fpx, 9 * k, 70 * k);
				g.Circle(sx, sy, rr, CA, 1.0, 0.55);
				const double lx = sx + rr * 0.7 + 18 * k, ly = sy - rr * 0.7 - 18 * k;
				g.Line(sx + rr * 0.7, sy - rr * 0.7, lx, ly, CA, 0.8, 0.45, false, false);
				g.Frame(lx - 640 * k, ly - 360 * k, k);
				g.T(644, 357, lab, CA, 10, 0);
			}
			else
			{
				// off the view: an arrow on a ring around the centre
				double ax = cam.x, ay = cam.y; if (cam.z < 0 && std::hypot(ax, ay) < 1e-6) ax = 1;
				const double a = std::atan2(-ay, ax);
				g.Frame(C0, 0, k);
				const double r = 230, px = 640 + r * std::cos(a), py = 360 + r * std::sin(a) * 0.8;
				g.Tri(px + 12 * std::cos(a), py + 12 * std::sin(a), -12 * std::cos(a) - 7 * std::sin(a), -12 * std::sin(a) + 7 * std::cos(a), -12 * std::cos(a) + 7 * std::sin(a), -12 * std::sin(a) - 7 * std::cos(a), CA);
				g.T(px - 16 * std::cos(a), py - 16 * std::sin(a) + 4, lab, CA, 10, std::cos(a) > 0.3 ? 2 : std::cos(a) < -0.3 ? 0 : 1);
			}
		}

		g.world = false;
		// ---- bottom: autopilot line and the numbers ----
		g.Frame(C0, 0, k);
		if (d.suit)
		{
			std::vector<V2> pts;
			for (int i = 0; i <= 24; ++i) { const double a = PI * (0.385 + 0.23 * i / 24); pts.push_back({ 640 + 1000 * std::cos(a), -260 + 1000 * std::sin(a) }); }
			g.Poly(pts, CP, 1.2, 0.5);
			std::string ann;
			if (d.jet && d.jetMode == 1) ann = "АП · УДЕРЖАНИЕ ВЫСОТЫ " + Num(d.jetAltHold, 1) + " м";
			else if (d.jet && d.jetMode == 2) ann = "АП · АВТОПОСАДКА";
			if (d.apMode) ann = "АП · " + d.apStatus + (ann.empty() ? "" : "   ▸   " + ann.substr(ann.find("·") + 3));
			if (d.jet && d.boost) ann += ann.empty() ? "ПОЛНЫЙ ВЕКТОР" : "   ▸   ПОЛНЫЙ ВЕКТОР";
			if (!ann.empty()) { const double w = g.TW(ann, 12.5) + 24; g.Rect(640 - w / 2, 618, w, 22, CA, 1.2, 1, CA, 0.12); g.T(640, 634, ann, CA, 12.5, 1); }
			std::vector<std::tuple<std::string, std::string, int>> cells;
			const Target* s = Selected();
			if (mode == FLIGHT && d.jet)
				cells = { { "ВЫСОТА", Num(d.alt, 1) + " м", CW }, { "ВЕРТ", Num(d.vs, 1, true) + " м/с", d.vs < -3 ? CA : CW }, { "ГОРИЗ", Num(d.gs, 1) + " м/с", CW },
				          { "ТЯГА", Num(100 * d.jetThrottle, 0) + " %", CW }, { "ЗАПАС Δv", Num(d.jetDv, 0) + " м/с", d.jetDv < 30 ? CA : CW } };
			else if (mode == RDV && s)
				cells = { { "ДАЛЬН", Dist(s->dist), CW }, { "СБЛИЖ", Num(s->rate, 2, true), CA }, { "ΔV СИНХР", Num(length(s->relV), 2), CW },
				          { "ЦЕЛЬ", s->name, CA }, { "ЗАПАС Δv", Num(d.jet ? d.jetDv : d.n2Dv, d.jet ? 0 : 1) + " м/с", CW } };
			else if (mode == SYS)
				cells = { { "БАТАРЕЯ", Num(100 * d.batt, 0) + " %", CW }, { "РАСХОД", Num(d.powerW, 0) + " Вт", CW }, { "ХВАТИТ", Hours(d.battHours), CW },
				          { "O2", d.o2Vent ? std::string("воздух") : Hours(d.o2Hours), CW }, { "ТЕПЛО", Num(-d.heatW, 0, true) + " Вт", CW } };
			else
				cells = { { "СКОРОСТЬ", Num(surface ? d.speed : std::hypot(gsH.x, gsH.z), 1) + " м/с", CW }, { "ПОХОДКА", d.servo ? "серво" : d.speed > 2.2 ? "бег" : d.speed > 0.1 ? "шаг" : "стоит", d.servo ? CA : CW },
				          { "ЩИТОК", d.shadeDown ? "опущен" : "поднят", CW }, { "ФОНАРИ", d.lampsOn ? "вкл" : "выкл", CW },
				          { d.hasGround ? "ГРУНТ" : "СРЕДА", Num(d.hasGround ? d.groundC : d.envC, 0, true) + " °C", d.hasGround && d.groundC > 80 ? CA : CW } };
			for (size_t i = 0; i < cells.size(); ++i) g.Cell(470 + i * 85.0, 664, std::get<0>(cells[i]), std::get<1>(cells[i]), std::get<2>(cells[i]), 1);
			// the pods, seen from her left side
			if (fly && surface)
				for (int i = 0; i < 2; ++i)
				{
					const double x = i == 0 ? 425 : 855, y = 664, t = d.jetTilt[i];
					g.Circle(x, y, 7, CP, 2);
					g.Line(x, y, x - 22 * std::sin(t), y + 22 * std::cos(t), d.jetThrottle > 0.01 ? CA : CP, 2.5);
					g.T(x, y + 40, std::string(i == 0 ? "П " : "Л ") + Num(t * DEG, 0, true) + "°", CW, 10, 1);
				}
		}

		// ---- the keys ----
		{
			std::string keys;
			if (!d.suit) keys = "W/S ход  A/D поворот  Q/E шаг  Shift бег  Пробел прыжок  K скафандр";
			else if (mode == FLIGHT && d.jet) keys = std::string(d.jetAssist ? "Пробел / Ctrl — выше / ниже" : "Пробел / Ctrl — тяга больше / меньше") + "   Shift+Пробел — полная   Shift+Ctrl — выкл   W/S — вектор   Q/E — вбок   A/D — поворот";
			else if (mode == RDV) keys = "РСУ: цифровой блок   ·   цель и автопилот — мышью на правом МФД";
			else if (mode == SYS) keys = "режим ещё раз — снова автоматически   ·   заголовок МФД — свернуть";
			else keys = "WASD — шаг   Shift — серво   B — ранец   V — щиток   L — фонари   K — скафандр";
			g.T(640, 712, keys, CD, 10.5, 1);
		}
		if (!d.suit) return;

		// ---- the two MFDs, low in the corners, each folds on its own key ----
		const double ms = 0.78;   // both MFDs the same size, clear of the panels above
		auto mfd = [&](bool left)
		{
			g.clipL = 3; g.clipR = 337;   // nothing written past the panel's edges
			const auto rp = RPages();
			if (!left && std::find(rp.begin(), rp.end(), static_cast<RPage>(rpage)) == rp.end()) rpage = R_TARGETS;
			const double ph = 400;
			const double x0 = left ? L0 + 112 * k : R0 + (1168 - 340 * ms) * k, y0 = (686 - ph * ms) * k;
			g.Frame(x0, y0, k * ms);
			g.Rect(0, 0, 340, ph, -1, 0, 0, CK, 0.38 + 0.4 * (std::min)(1.0, g.boost)); g.Rect(0, 0, 340, ph, CP, 1, 0.32, CP, 0.06);
			for (int i = 0; i < 4; ++i)
			{
				const double bx = i & 1 ? 340 : 0, by = i & 2 ? ph : 0, dx = i & 1 ? -12 : 12, dy = i & 2 ? -12 : 12;
				g.Line(bx, by, bx + dx, by, CP, 2); g.Line(bx, by, bx, by + dy, CP, 2);
			}
			static const char* LT[] = { "КАРТА", "ОРБИТА", "ПИТАНИЕ И ТЕПЛО", "ОПЦИИ" };
			static const char* LTAB[] = { "МЕСТН", "ОРБИТА", "ПИТАНИЕ", "ОПЦИИ" };
			static const char* RT[] = { "ЦЕЛИ", "ПЕРЕЛЁТ", "ПОСАДКА", "СБЛИЖЕНИЕ", "СТЫКОВКА", "ОРГАНИЗМ", "ПОЛЁТ" };
			static const char* RTAB[] = { "ЦЕЛИ", "ПЕРЕЛЁТ", "ПОСАДКА", "СБЛИЖ", "СТЫК", "ОРГАНИЗМ", "ПОЛЁТ" };
			const Target* s = Selected();
			g.T(9, 17, left ? LT[lpage] : RT[rpage], CP, 13);
			g.Hit(0, 0, 200, 20, left ? H_LFOLD : H_RFOLD);
			g.Tri(200, 15, -4, -6, 4, -6, CD);   // fold mark
			if (left && lpage == L_LOCAL && surface) g.T(331, 16, "МАСШТАБ " + Dist(ZOOM[zoom]), CA, 9.5, 2);
			if (!left) { g.T(331, 16, s ? "ЦЕЛЬ: " + s->name + "  ▸" : "ЦЕЛИ НЕТ", CA, 9.5, 2); g.Hit(205, 0, 135, 20, H_NEXT); }
			double tx = 7;
			const int nt = left ? L_COUNT : static_cast<int>(rp.size());
			for (int i = 0; i < nt; ++i)
			{
				const int id = left ? i : rp[i]; const bool on = left ? id == lpage : id == rpage;
				const std::string nm = left ? LTAB[id] : RTAB[id];
				const double w = g.TW(nm, 9.5) + 14;
				if (on) g.Rect(tx, 22, w, 15, CP, 1, 0.55, CP, 0.13);
				g.Hit(tx, 20, w, 19, left ? H_LTAB : H_RTAB, id);
				g.T(tx + w / 2, 33, nm, on ? CP : CD, 9.5, 1);
				tx += w + 3;
			}
			g.Line(0, 40, 340, 40, CP, 1, 0.28, false, false);
			g.Frame(x0, y0 + 40 * k * ms, k * ms);
			if (left) { switch (lpage) { case L_LOCAL: pages::Local(c); break; case L_ORBIT: pages::Orbit(c); break; case L_POWER: pages::Power(c); break; default: pages::Opts(c); } }
			else
			{
				switch (rpage)
				{
				case R_TARGETS: pages::Targets(c); break; case R_TRANSFER: pages::Transfer(c); break; case R_LANDING: pages::Landing(c); break; case R_FLIGHT: pages::Flight(c); break;
				case R_APPROACH: pages::Approach(c); break; case R_DOCK: pages::Dock(c); break; default: pages::Body(c);
				}
			}
			g.Frame(x0, y0, k * ms);
			double fy = 394;
			if (!left)
			{
				// the autopilot: filled = on, framed = armed, dim = not available here
				struct B { const char* n; int st; int req; };   // 0 off, 1 armed, 2 on, -1 not available
				std::vector<B> b;
				const bool hasSel = Selected() != nullptr, jetOk = d.jet && d.jetFuel > 0;
				if (surface) b = { { "УДЕРЖ ВЫС", jetOk ? (d.jetMode == 1 && !d.apMode ? 2 : 0) : -1, AP_ALT },
				                   { "ПЕРЕЛЁТ", jetOk && hasSel ? (d.apMode == 2 ? 2 : rpage == R_TRANSFER ? 1 : 0) : -1, 2 },
				                   { "ПОСАДКА", jetOk ? (d.jetMode == 2 ? 2 : rpage == R_LANDING ? 1 : 0) : -1, AP_LAND },
				                   { "СТОП", jetOk ? (d.jetBraking ? 2 : 0) : -1, AP_STOP } };
				else
				{
					const bool rcsOk = d.rcs == 1;
					b = { { "СБЛИЖ", rcsOk && hasSel ? (d.apMode == 5 ? 2 : rpage == R_APPROACH ? 1 : 0) : -1, 5 },
					      { "СИНХР", rcsOk && hasSel ? (d.apMode == 3 ? 2 : 0) : -1, 3 },
					      { "УДЕРЖ", rcsOk && hasSel ? (d.apMode == 4 ? 2 : 0) : -1, 4 },
					      { "СТЫК", rcsOk && hasSel ? (d.apMode == 6 ? 2 : rpage == R_DOCK ? 1 : 0) : -1, 6 } };
				}
				if (!d.apStatus.empty()) g.T(9, 353, d.apStatus, d.apMode ? CA : CD, 9.5);
				const double bw = (340 - 14 - 12) / 4.0;
				for (int i = 0; i < 4; ++i)
				{
					const double bx = 7 + i * (bw + 4), by = 360;
					if (b[i].st >= 0) g.Hit(bx, by, bw, 20, H_AP, b[i].req);
					if (b[i].st == 2) { g.Rect(bx, by, bw, 20, -1, 0, 0, CA, 1); g.T(bx + bw / 2, by + 14, b[i].n, CK, 9.5, 1); }
					else { g.Rect(bx, by, bw, 20, b[i].st == 1 ? CA : CD, 1, b[i].st < 0 ? 0.35 : 0.8); g.T(bx + bw / 2, by + 14, b[i].n, b[i].st == 1 ? CA : CD, 9.5, 1); }
				}
				fy = 394;
			}
			g.T(9, fy, "заголовок — свернуть", CD, 9, 0);
			g.clipL = -1e9; g.clipR = 1e9;
		};
		auto tab = [&](bool left)
		{
			const double x0 = left ? L0 + 112 * k : R0 + (1168 - 252) * k;
			g.Frame(x0, 662 * k, k);
			g.Rect(0, 0, 252, 24, CP, 1, 0.4, CP, 0.08);
			g.Hit(0, 0, 252, 24, left ? H_LFOLD : H_RFOLD);
			const Target* s = Selected();
			std::string t = left ? std::string("M · КАРТА") : std::string("N · ") + (s ? "ЦЕЛЬ: " + s->name : "ЦЕЛИ НЕТ");
			g.T(10, 16, t, CP, 11);
			if (!left && (d.apMode || (d.jet && d.jetMode))) g.T(244, 16, d.apMode ? std::string("АП: ") + (d.apStatus.substr(0, d.apStatus.find(" "))) : d.jetMode == 1 ? "АП: УДЕРЖ" : "АП: ПОСАДКА", CA, 10, 2);
		};
		if (openL) mfd(true); else tab(true);
		if (openR) mfd(false); else tab(false);
	}
}

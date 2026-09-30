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
		const char* PAL_NAME[3] = { "ЦИАН + ОРАНЖ", "ОРАНЖ + ЦИАН", "БЕЛЫЙ + ОРАНЖ" };
		const char* PAL_NOTE[3] = { "основной циан, автопилот оранжевый", "основной оранжевый, автопилот циан", "основной белый, автопилот оранжевый" };
		const double SIZE_BASE[4] = { 9.5, 11, 13, 15 };
		const double ZOOM[5] = { 50, 100, 250, 500, 1000 };   // local map: outer ring, m

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
		if (it == index.end() || it->first != k) return cp != '?' ? Find(size, colour, '?') : nullptr;
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
		Gfx(oapi::Sketchpad* skp, SuitHud& o, const HudText& t, int pal) : skp(skp), s2(dynamic_cast<oapi::Sketchpad2*>(skp)), o(o), txt(t), P(PALS[pal]) {}
		oapi::Sketchpad* skp; oapi::Sketchpad2* s2; SuitHud& o; const HudText& txt; const Palette& P;
		double ox{}, oy{}, u{ 1 };
		void Frame(double x0, double y0, double scale) { ox = x0; oy = y0; u = scale; }
		double X(double x) const { return ox + x * u; }
		double Y(double y) const { return oy + y * u; }
		int IX(double x) const { return static_cast<int>(std::lround(X(x))); }
		int IY(double y) const { return static_cast<int>(std::lround(Y(y))); }
		DWORD C(int c, double a) const { return (P.col[c] & 0xFFFFFF) | (static_cast<DWORD>(std::clamp(a, 0.0, 1.0) * 255) << 24); }

		void Pen(int c, double w, double a, bool dash)
		{
			const double px = (std::max)(1.0, w * u * 0.8);
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
			if (glow && s2 && !dash) { Pen(c, w + 2.4, a * 0.2, false); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2)); }
			Pen(c, w, a, dash); skp->Line(IX(x1), IY(y1), IX(x2), IY(y2));
		}
		void Rect(double x, double y, double w, double h, int sc, double sw = 1, double sa = 1, int fc = -1, double fa = 0)
		{
			if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			if (sc >= 0) { NoBrush(); Pen(sc, sw, sa, false); skp->Rectangle(IX(x), IY(y), IX(x + w), IY(y + h)); }
			NoBrush();
		}
		void Circle(double cx, double cy, double r, int sc, double sw = 1.2, double sa = 1, int fc = -1, double fa = 0, bool dash = false)
		{
			if (fc >= 0 && fa > 0 && Brush(fc, fa)) { NoPen(); skp->Ellipse(IX(cx - r), IY(cy - r), IX(cx + r), IY(cy + r)); }
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
			if (pts.size() < 3 || !Brush(c, a)) return;
			std::vector<oapi::IVECTOR2> v;
			for (const V2& p : pts) { oapi::IVECTOR2 q; q.x = IX(p.first); q.y = IY(p.second); v.push_back(q); }
			NoPen(); skp->Polygon(v.data(), static_cast<int>(v.size())); NoBrush();
		}
		static int SizeIdx(double sz) { return sz <= 10 ? 0 : sz <= 11.9 ? 1 : sz <= 13.9 ? 2 : 3; }
		void T(double x, double y, const std::string& s, int c = CP, double sz = 12, int align = 0)
		{
			if (s.empty()) return;
			const int si = SizeIdx(sz);
			if (txt.Draw(skp, X(x), Y(y), s, si, P.atlas[c], align, u * sz / SIZE_BASE[si])) return;
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
		nvNote = gain > 2 ? "ПНВ" : "ПНВ · засветка";
		auto* s3 = static_cast<oapi::Sketchpad3*>(skp);
		const oapi::FVECTOR4 bright(static_cast<float>(0.30 * gain / 4), static_cast<float>(gain), static_cast<float>(0.45 * gain / 4), 1.0f);
		const oapi::FVECTOR4 gamma(0.5f, 0.5f, 0.5f, 1.0f), noise(0.0f, 0.25f, 0.0f, 0.0f);
		s3->SetBrightness(&bright); s3->SetRenderParam(SKP3_PRM_GAMMA, &gamma); s3->SetRenderParam(SKP3_PRM_NOISE, &noise);
		RECT src = { 0, 0, nvW, nvH }, dst = { 0, 0, static_cast<LONG>(W), static_cast<LONG>(H) };
		s3->StretchRect(nvSrf, &src, &dst);
		s3->SetBrightness(nullptr); s3->SetRenderParam(SKP3_PRM_GAMMA, nullptr); s3->SetRenderParam(SKP3_PRM_NOISE, nullptr);
	}

	std::string SuitHud::Save() const
	{
		char b[96];
		snprintf(b, sizeof b, "%d %d %d %d %d %d %d %d", pal, mode, autoMode ? 1 : 0, lpage, rpage, openL ? 1 : 0, openR ? 1 : 0, zoom);
		return b;
	}

	void SuitHud::Load(const std::string& line)
	{
		std::istringstream ss(line);
		int a[8] = { pal, mode, autoMode, lpage, rpage, openL, openR, zoom };
		for (int& x : a) ss >> x;
		pal = std::clamp(a[0], 0, 2); mode = std::clamp(a[1], 0, 3); autoMode = a[2] != 0; lpage = std::clamp(a[3], 0, L_COUNT - 1);
		rpage = std::clamp(a[4], 0, static_cast<int>(R_BODY)); openL = a[5] != 0; openR = a[6] != 0; zoom = std::clamp(a[7], 0, 4);
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
		if (surface) return { R_TARGETS, R_TRANSFER, R_LANDING, R_BODY };
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
			case H_TGT: if (it->arg >= 0 && it->arg < static_cast<int>(targets.size())) { sel = targets[it->arg].h; rrHist.clear(); prof.t = -1; } break;
			case H_AP: request = it->arg; break;
			case H_PAL: pal = std::clamp(it->arg, 0, 2); break;
			case H_ZOOM: zoom = std::clamp(zoom + it->arg, 0, 4); grid.t = -1; break;
			case H_MODE: if (!autoMode && it->arg == mode) autoMode = true; else SetMode(it->arg, true); break;
			case H_NVG: nvg = !nvg; if (!nvg && nvCam) gcCustomCameraOnOff(nvCam, false); break;
			case H_NEXT:
				if (targets.empty()) break;
				{ size_t k = 0; for (; k < targets.size(); ++k) if (targets[k].h == sel) break; sel = targets[k < targets.size() ? (k + 1) % targets.size() : 0].h; }
				rrHist.clear(); prof.t = -1; break;
			}
			return;
		}
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
		std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.dist < b.dist; });
		if (targets.size() > 8) targets.resize(8);
		if (packs > 1)   // several packs: number them by distance
		{
			int k = 0;
			for (Target& tg : targets) if (tg.pack) tg.name = "РАНЕЦ " + std::to_string(++k);
		}
		if (!Selected()) { sel = targets.empty() ? nullptr : targets.front().h; rrHist.clear(); }

		// orbit
		orbitOk = tgtOrbitOk = false;
		if (space)
		{
			OBJHANDLE ref = v->GetGravityRef();
			orbitOk = v->GetElements(ref, el, &op, 0, FRAME_EQU) && el.e < 1;
			if (const Target* s = Selected())
			{
				VESSEL* o = oapiGetVesselInterface(s->h);
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
			if (sel && (prof.tgt != sel || t - prof.t > 1 || t < prof.t)) Profile(v);
		}
		lastNav = t;
	}

	void SuitHud::Terrain(VESSEL* v)
	{
		grid.n = 25; grid.half = ZOOM[zoom] * 1.2; grid.lat = lat; grid.lng = lng; grid.t = oapiGetSimTime();
		grid.h.assign(grid.n * grid.n, 0);
		const double step = 2 * grid.half / (grid.n - 1), cl = (std::max)(0.05, std::cos(lat));
		for (int j = 0; j < grid.n; ++j)
			for (int i = 0; i < grid.n; ++i)
			{
				const double north = -grid.half + j * step, east = -grid.half + i * step;
				grid.h[j * grid.n + i] = oapiSurfaceElevation(hBody, lng + east / (bodyR * cl), lat + north / bodyR);
			}
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
				if (hi - lo > 0.3)
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
						if (In(a) && In(b)) { g.Line(a.first, a.second, b.first, b.second, CA, 1, 0.55, false, false); g.Line(a2.first, a2.second, b2.first, b2.second, CA, 1, 0.55, false, false); g.Line(a3.first, a3.second, b3.first, b3.second, CA, 1, 0.55, false, false); }
					}
			}
			// range rings
			for (double f : { 0.25, 0.5, 1.0 })
			{
				g.Circle(170, 170, 128 * f, CP, 1, 0.35, -1, 0, true);
				g.T(170 + 128 * f * 0.707 + 3, 170 + 128 * f * 0.707 + 10, Num(range * f, 0) + " м", CD, 9.5);
			}
			// trail and the next 10 s
			for (size_t i = 0; i < h.trail.size(); ++i)
			{
				const V2 p = MapXY((h.trail[i].first - h.lat) * h.bodyR, Wrap(h.trail[i].second - h.lng) * h.bodyR * std::cos(h.lat), h.hdg, s);
				if (In(p)) g.Circle(p.first, p.second, 1.3, -1, 0, 0, CP, 0.25 + 0.7 * i / h.trail.size());
			}
			{
				// vectors from her: velocity over the ground, the wind, the autopilot's push (1 m/s = 14 units)
				auto vec = [&](double north, double east, double k_) { const V2 p = MapXY(north, east, h.hdg, 1.0); return V2{ 170 + (p.first - 170) * k_, 170 + (p.second - 170) * k_ }; };
				const double gv = std::hypot(h.gsH.x, h.gsH.z);
				if (gv > 0.1) { const double kk = (std::min)(14.0, 110 / gv); const V2 p = vec(h.gsH.z, h.gsH.x, kk); g.Arrow(170, 170, p.first, p.second, CW, 1.6); g.T(p.first + 6, p.second - 4, "V " + Num(gv, 1), CW, 9.5); }
				const double wv = std::hypot(c.d.wind.x, c.d.wind.z);
				if (!c.d.vacuum && wv > 0.1) { const double kk = (std::min)(14.0, 110 / wv); const V2 p = vec(c.d.wind.z, c.d.wind.x, kk); g.Arrow(170, 170, p.first, p.second, CP, 1.2, 0.8, true); g.T(p.first + 6, p.second + 10, "ветер " + Num(wv, 1), CP, 9.5); }
				const double av = std::hypot(c.d.apCmd.x, c.d.apCmd.z);
				if (c.d.apMode && av > 0.05) { const double kk = (std::min)(30.0, 90 / av); const V2 p = vec(c.d.apCmd.z, c.d.apCmd.x, kk); g.Arrow(170, 170, p.first, p.second, CA, 1.8); g.T(p.first + 6, p.second + 10, "АП", CA, 9.5); }
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
				const int col = on ? CA : CP;
				if (on) g.Line(170, 170, p.first, p.second, CA, 1, 0.7, true);
				if (t.pack) { g.Rect(p.first - 5, p.second - 4, 10, 8, col, 1.5); g.Line(p.first - 5, p.second - 1, p.first + 5, p.second - 1, col, 1, 1, false, false); }
				else if (t.crew) g.Circle(p.first, p.second, 5, col, 1.4);
				else g.Poly({ { p.first, p.second - 7 }, { p.first + 7, p.second }, { p.first, p.second + 7 }, { p.first - 7, p.second } }, col, 1.6, 1, true);
				g.Hit(p.first - 10, p.second - 10, 20, 20, SuitHud::H_TGT, idx);
				const std::string lab = (edge ? "◂ " : "") + t.name + " " + Dist(t.dist);
				const bool left = p.first > 250;
				g.T(p.first + (left ? -9 : 9), p.second + 4, lab, col, 10, left ? 2 : 0);
			}
			// her
			g.Fill({ { 170, 161 }, { 177, 177 }, { 170, 173 }, { 163, 177 } }, CW, 1);
			// north arrow, scale
			const double nx = -std::sin(h.hdg), ny = -std::cos(h.hdg);
			g.Line(24, 34, 24 + nx * 12, 34 + ny * 12, CP, 1.6); g.T(24 + nx * 20, 34 + ny * 20 + 4, "С", CP, 10, 1);
			char hd[16]; snprintf(hd, sizeof hd, "%03.0f°", std::fmod(h.hdg * DEG + 360, 360));
			g.T(330, 18, std::string("КУРС ") + hd, CD, 9.5, 2);
			g.Line(14, 288, 78, 288, CP, 1.4); g.Line(14, 284, 14, 292, CP, 1); g.Line(78, 284, 78, 292, CP, 1);
			g.T(46, 282, Num(range / 2, 0) + " м", CD, 9.5, 1);
			g.Line(238, 285, 250, 285, CA, 1, 0.7); g.T(254, 289, "склон > 15°", CA, 9.5);
			for (int zi = 0; zi < 2; ++zi)
			{
				const double bx = 296 + zi * 22, by = 244;
				g.Rect(bx, by, 18, 18, CP, 1, 0.7, CP, 0.1); g.T(bx + 9, by + 14, zi ? "−" : "+", CP, 13, 1);
				g.Hit(bx, by, 18, 18, SuitHud::H_ZOOM, zi ? 1 : -1);
			}
		}

		void Orbit(Ctx& c)
		{
			Gfx& g = c.g; SuitHud& h = c.h; Grid(g);
			if (h.surface)
			{
				const double cx = 170, cy = 140, r = 105;
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
				g.Cell(14, 262, "ШИРОТА", Num(std::abs(h.lat * DEG), 1) + (h.lat >= 0 ? "° с." : "° ю."));
				g.Cell(110, 262, "ДОЛГОТА", Num(std::abs(h.lng * DEG), 1) + (h.lng >= 0 ? "° в." : "° з."));
				g.Cell(210, 262, "СОЛНЦЕ", h.sunElev > 0 ? Num(h.sunElev * DEG, 0) + "° · день" : "ночь", h.sunElev > 0 ? CW : CA);
				return;
			}
			if (!h.orbitOk) { Msg(g, "НЕТ ЗАМКНУТОЙ ОРБИТЫ"); return; }
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
			g.Cell(14, 256, "ПЕРИОД", Num(h.op.T / 60, 1) + " мин");
			g.Cell(110, 256, "НАКЛОН", Num(h.el.i * DEG, 2) + "°");
			g.Cell(210, 256, "ОТН. НАКЛОН", h.tgtOrbitOk ? Num(h.relInc * DEG, 2) + "°" : "—", CA);
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
			g.T(14, 22, "ЦВЕТ ЭКРАНА", CD, 9.5);
			for (int i = 0; i < 3; ++i)
			{
				const double y = 34 + i * 58; const bool on = i == h.pal;
				g.Rect(10, y, 320, 50, on ? CA : CD, on ? 1.6 : 1, 1, on ? CA : -1, 0.1);
				g.Hit(10, y, 320, 50, SuitHud::H_PAL, i);
				// swatches: the palette's own colours
				Gfx sw(g.skp, g.o, g.txt, i); sw.Frame(g.ox, g.oy, g.u);
				sw.Rect(22, y + 13, 24, 24, -1, 0, 0, CP, 1); sw.Rect(50, y + 13, 24, 24, -1, 0, 0, CA, 1);
				g.T(88, y + 23, std::string(on ? "▶ " : "") + PAL_NAME[i], on ? CA : CW, 12.5);
				g.T(88, y + 39, PAL_NOTE[i], CD, 9.5);
			}
			g.Line(14, 214, 326, 214, CD, 1, 0.5, false, false);
			g.T(14, 236, "щелчок по строке — цвет", CP, 11);
			g.T(14, 258, "всё управление — мышью: вкладки, заголовки, кнопки", CD, 9.5);
			g.T(14, 276, "ПНВ, щиток, свет — кнопки справа вверху", CD, 9.5);
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
			g.Cell(130, 264, "СТЫК. УЗЛЫ", [&] { VESSEL* o = oapiGetVesselInterface(s->h); return o ? std::to_string(o->DockCount()) : std::string("—"); }());
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
			const double cruise = d.jetMode == 1 ? d.jetAltHold : (std::max)(d.alt, 3.0);
			double top = cruise + 2, bot = 0;
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
			const double vCruise = (std::max)(3.0, d.gs), time = D / vCruise, g0 = h.hBody ? GGRAV * oapiGetMass(h.hBody) / (h.bodyR * h.bodyR) : 1.62;
			const double dv = g0 * time + 2 * vCruise;
			g.Cell(14, 180, "ДАЛЬНОСТЬ", Dist(D)); g.Cell(120, 180, "ПЕЛЕНГ", b); g.Cell(226, 180, "ВРЕМЯ ≈", Clock(time));
			g.Cell(14, 216, "ВЫСОТА", Num(cruise, 1) + " м", CA); g.Cell(120, 216, "Δv ≈", Num(dv, 0) + " м/с");
			g.Cell(226, 216, "ЗАПАС ПОСЛЕ", Num(d.jetDv - dv, 0) + " м/с", d.jetDv - dv < 20 ? CR : CW);
			const size_t n = h.prof.h.size();
			const double slope = std::atan(std::abs(h.prof.h[n - 1] - h.prof.h[n - 2]) / (D / (n - 1))) * DEG;
			g.Line(14, 254, 326, 254, CD, 1, 0.5, false, false);
			g.T(14, 272, "у цели склон " + Num(slope, 0) + "° · " + (slope < 10 ? "садиться можно" : "круто, ищите ровнее"), slope < 10 ? CP : CA, 10.5);
			g.T(14, 290, "по " + Num(vCruise, 0) + " м/с · J — удержание высоты", CD, 9.5);
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
				g.Rect(14, y + 6, 312, 52, CD, 1, 0.4);
				auto Y = [&](double v) { return y + 58 - 52 * std::clamp((v - lo) / (hi - lo), 0.0, 1.0); };
				g.Rect(15, Y(nhi), 310, Y(nlo) - Y(nhi), -1, 0, 0, CP, 0.1);
				std::vector<V2> pts;
				for (size_t i = 0; i < q.size(); ++i) pts.push_back({ 326 - (q.size() - 1 - i) * 5.2, Y(q[i]) });
				g.Poly(pts, col, 1.4);
			};
			graph(18, "ПУЛЬС · 10 мин", Num(d.pulse, 0) + " уд/мин", h.trend.pulse, 40, 200, 55, 120, d.pulse > 170 ? CA : CP);
			graph(98, "ДЫХАНИЕ", Num(d.breath, 0) + " в мин", h.trend.breath, 5, 50, 10, 25, CP);
			graph(178, "ТЕМПЕРАТУРА ТЕЛА", Num(d.coreC, 1) + " °C", h.trend.core, 34, 41, 36.2, 37.8, d.coreC > 38.5 || d.coreC < 35.8 ? CA : CP);
			g.T(14, 270, "СИЛЫ", CD, 9.5); g.Bar(60, 262, 110, 10, d.stamina, d.stamina < 0.15 ? CR : d.stamina < 0.3 ? CA : CP); g.T(176, 271, Num(100 * d.stamina, 0) + " %", CW, 11);
			g.T(14, 290, "НАГРУЗКА " + Num(100 * d.effort, 0) + " % · " + (d.injury > 0.01 ? "ТРАВМА " + Num(100 * d.injury, 0) + " %" : "травм нет") + " · CO₂ " + Num(d.ppCO2, 2) + " кПа", d.injury > 0.01 ? CA : CP, 10.5);
		}
	}

	// ================= the display =================
	void SuitHud::Draw(oapi::Sketchpad* skp, const HUDPAINTSPEC* hps, const HudData& d, VESSEL* v)
	{
		if (!glyphsTried) { glyphsTried = true; glyphs.Load(); }
		const double W = hps->W, H = hps->H, k = H / 720.0;
		const double L0 = 0, C0 = W / 2 - 640 * k, R0 = W - 1280 * k;
		hits.clear();
		Gfx g(skp, *this, glyphs, pal);
		Ctx c{ g, d, *this, v, W, H, k, std::fmod(d.simt, 1.0) < 0.62 };
		Nav(v, d);

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

		if (d.suit && d.firstPerson)
		{
			if (nvg) NightVision(skp, v, W, H);
			// the sun shade: a light gold filter, only against bright sources (about a quarter of the light)
			if (d.shade > 0.02 && g.s2)
			{
				const DWORD a = static_cast<DWORD>(0x40 * std::clamp(d.shade, 0.0, 1.0));
				g.s2->QuickPen(0); g.s2->QuickBrush((a << 24) | 0x00081830);   // 0xAABBGGRR: dark amber
				skp->Rectangle(0, 0, static_cast<int>(W), static_cast<int>(H)); g.s2->QuickBrush(0);
			}
		}
		else if (nvCam && nvg) gcCustomCameraOnOff(nvCam, false);

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
			auto arcText = [&](double amid, const std::vector<std::pair<std::string, int>>& parts)
			{
				double total = 0;
				for (const auto& p : parts) total += g.TW(p.first, 13);
				double sArc = -total / 2;
				for (const auto& p : parts)
					for (const std::string& ch : Chars(p.first))
					{
						const double w = g.TW(ch, 13), a = amid + (sArc + w / 2) / 1500;
						g.T(640 + 1500 * std::cos(a), 1560 + 1500 * std::sin(a), ch, p.second, 13, 1);
						sArc += w;
					}
			};
			arcLine(-1.925, -1.672); arcLine(-1.470, -1.217);
			arcText(-1.798, { { "ПУЛЬС ", CD }, { Num(d.pulse, 0), CW }, { " · ДЫХ ", CD }, { Num(d.breath, 0), CW }, { " · O₂ ", CD }, { Num(d.ppO2, 0) + " кПа", d.ppO2 < 14 ? CA : CW }, { " · ТЕЛО ", CD }, { Num(d.coreC, 1) + "°", CW } });
			if (d.suit)
				arcText(-1.344, { { "O₂ ", CD }, { Num(100 * d.o2, 0) + " %", CW }, { " · БАТ ", CD }, { Num(100 * d.batt, 0) + " %", d.batt < 0.25 ? CA : CW }, { " · ", CD }, { Num(d.powerW, 0), CW }, { " Вт · СРЕДА ", CD }, { Num(d.envC, 0, true) + "°", d.inSpec ? CW : CR } });
			else
				arcText(-1.344, { { "КОМБИНЕЗОН · ", CD }, { d.breathable ? "воздух пригоден" : "дышать нельзя", d.breathable ? CW : CR } });
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
				struct Tg { const char* n; bool on; int kind, arg; } tg[3] = { { "СВЕТ", d.lampsOn, H_AP, AP_LAMP }, { "ЩИТОК", d.shadeDown, H_AP, AP_SHADE }, { "ПНВ", nvg, H_NVG, 0 } };
				double bx = 1256;
				for (int i = 2; i >= 0; --i)
				{
					const double w = g.TW(tg[i].n, 11) + 18; bx -= w;
					if (tg[i].on) { g.Rect(bx, 58, w, 20, -1, 0, 0, CA, 1); g.T(bx + w / 2, 72, tg[i].n, CK, 11, 1); }
					else { g.Rect(bx, 58, w, 20, CP, 1, 0.6, CP, 0.08); g.T(bx + w / 2, 72, tg[i].n, CP, 11, 1); }
					g.Hit(bx, 58, w, 20, tg[i].kind, tg[i].arg);
					bx -= 5;
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

		// ---- warning, cautions, message ----
		{
			std::string caut;
			if (d.jetProtect) caut = "▲ ЗЕМЛЯ БЛИЗКО";
			else if (d.jetTerrain) caut = "▲ РЕЛЬЕФ ВПЕРЕДИ";
			else if (d.jetLimited) caut = "▲ УВТ ОГРАНИЧЕН";
			else if (d.suit && std::abs(d.residualW) > 30) caut = d.residualW > 0 ? "▲ ОХЛАЖДЕНИЕ НЕ СПРАВЛЯЕТСЯ" : "▲ ОБОГРЕВ НЕ СПРАВЛЯЕТСЯ";
			else if (const Target* s = Selected(); s && space && s->dist < 30 && -s->rate > 0.25) caut = "▲ ЗОНА 30 м · СКОРОСТЬ СБЛИЖЕНИЯ ≤ 0,25 м/с";
			else if (d.suit && d.hasGround && d.groundC > 80 && d.landed) caut = "▲ ГРУНТ " + Num(d.groundC, 0, true) + " °C";
			if (!d.warning.empty()) { if (c.blink) g.T(640, 124, d.warning, CR, 15, 1); }
			else if (!caut.empty() && c.blink) g.T(640, 124, caut, CA, 13, 1);
			if (!d.message.empty()) g.T(640, 146, d.message, CW, 12.5, 1);
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
				{ "В скафандре", Num(d.tInC, 1) + " °C · " + (holds && d.powered ? "держится" : d.residualW > 0 ? "растёт" : "падает"), d.tInC > 30 || d.tInC < 12 ? CR : holds && d.powered ? CW : CA },
				{ "Тело", Num(d.coreC, 1) + " °C", d.coreC > 38.5 || d.coreC < 35.8 ? CA : CW },
				{ "Среда", Num(d.envC, 0, true) + " °C" + (d.sunlit ? " · солнце" : " · тень"), d.inSpec ? CW : CR } };
			if (d.hasGround) rows.push_back({ "Грунт", Num(d.groundC, 0, true) + " °C", d.groundC > 80 || d.groundC < -100 ? CA : CW });
			rows.push_back({ "Теплообмен", std::string(d.heatW >= 0 ? "ОХЛАЖДЕНИЕ " : "ОБОГРЕВ ") + Num(std::abs(d.heatW), 0) + " Вт", CP });
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
			struct Part { const char* n; double v; double a; } parts[] = { { "ЖО", d.lifeW, 1 }, { "ТЕПЛО", d.thermalW, 0.75 }, { "ПРИВ", d.driveW, 0.5 }, { "СВЕТ", d.lampW, 0.3 } };
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
				{ "Батарея", Num(100 * d.batt, 0) + " % · " + Hours(d.battHours), d.batt, lim == d.battHours },
				{ "Кислород", Num(d.o2Flow * 60000, 2) + " г/мин · " + Hours(d.o2Hours), d.o2, lim == d.o2Hours },
				{ "Поглотитель CO₂", Num(100 * d.sorbent, 0) + " % · " + Hours(d.sorbHours), d.sorbent, lim == d.sorbHours },
				{ "Топливо ранца", d.jet ? (fly ? Num(d.jetFlow * 1000, 0) + " г/с · " : Num(100 * d.jetFuel, 0) + " % · ") + Num(d.jetDv, 0) + " м/с" : "ранец снят", d.jet ? d.jetFuel : 0, false },
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
			vscale(30, d.o2, lvl(d.o2), "O₂", Num(100 * d.o2, 0)); vscale(58, d.sorbent, lvl(d.sorbent), "CO₂", Num(100 * d.sorbent, 0)); vscale(86, d.batt, lvl(d.batt), "БАТ", Num(100 * d.batt, 0));
			g.Frame(R0, 0, k);
			if (d.jet) { vscale(1184, d.jetFuel, lvl(d.jetFuel), "ТОПЛ", Num(100 * d.jetFuel, 0)); vscale(1212, d.jetThrottle, CA, "ТЯГА", Num(100 * d.jetThrottle, 0)); }
			else vscale(1212, d.n2, lvl(d.n2), "N₂", Num(100 * d.n2, 0));
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
			// flight path marker: where she is going
			const double gsp = std::hypot(gsH.x, gsH.z);
			if (fly && gsp > 0.3)
			{
				const double az = Wrap(std::atan2(gsH.x, gsH.z) - hdg), fpa = std::atan2(gsH.y, gsp);
				const double fx = cx + std::clamp(std::tan(std::clamp(az, -1.2, 1.2)) * f, -220.0, 220.0), fy = cy + std::clamp(-std::tan(fpa) * f + std::tan(pitch) * f, -200.0, 200.0);
				g.Circle(fx, fy, 7, CW, 1.6); g.Line(fx - 18, fy, fx - 7, fy, CW, 1.6); g.Line(fx + 7, fy, fx + 18, fy, CW, 1.6); g.Line(fx, fy - 7, fx, fy - 13, CW, 1.6);
			}
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
				g.T(800, cy + 104, "↑ " + Num(d.vs, 1, true) + " м/с", CW, 12, 1);
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
			VECTOR3 cp, tp; oapiCameraGlobalPos(&cp); oapiGetGlobalPos(s->h, &tp);
			MATRIX3 R; oapiCameraRotationMatrix(&R);
			const VECTOR3 cam = tmul(R, tp - cp);
			const double fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
			g.Frame(0, 0, 1);
			const std::string lab = s->name + " · " + Dist(s->dist);
			const double sx = cam.z > 0 ? W / 2 + cam.x / cam.z * fpx : 0, sy = cam.z > 0 ? H / 2 - cam.y / cam.z * fpx : 0;
			if (cam.z > 0 && sx > 40 * k && sx < W - 40 * k && sy > 90 * k && sy < H - 40 * k)
			{
				const double half = std::clamp(oapiGetSize(s->h) / cam.z * fpx, 16 * k, 120 * k), kk = 9 * k;
				for (int i = 0; i < 4; ++i)
				{
					const double bx = sx + (i & 1 ? half : -half), by = sy + (i & 2 ? half : -half), dx = i & 1 ? -kk : kk, dy = i & 2 ? -kk : kk;
					g.Line(bx, by, bx + dx, by, CA, 1.8, 1); g.Line(bx, by, bx, by + dy, CA, 1.8, 1);
				}
				g.Frame(sx - 640 * k, sy - 360 * k, k);
				g.T(640, 360 - half / k - 7, lab, CA, 12, 1);
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
				          { "O₂", Hours(d.o2Hours), CW }, { "ТЕПЛО", Num(-d.heatW, 0, true) + " Вт", CW } };
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
			else if (mode == FLIGHT && d.jet) keys = "Пробел / Ctrl — выше / ниже   W/S — вектор   Q/E — вбок   A/D — поворот   Shift — полный вектор   0/. — тяга   J — высота   C — посадка";
			else if (mode == RDV) keys = "РСУ: цифровой блок   ·   цель и автопилот — мышью на правом МФД";
			else if (mode == SYS) keys = "режим ещё раз — снова автоматически   ·   заголовок МФД — свернуть";
			else keys = "WASD — шаг   Shift — серво   B — ранец   V — щиток   L — фонари   K — скафандр";
			g.T(640, 712, keys, CD, 10.5, 1);
		}
		if (!d.suit) return;

		// ---- the two MFDs, low in the corners, each folds on its own key ----
		const double ms = 0.85;
		auto mfd = [&](bool left)
		{
			const auto rp = RPages();
			if (!left && std::find(rp.begin(), rp.end(), static_cast<RPage>(rpage)) == rp.end()) rpage = R_TARGETS;
			const double ph = left ? 360 : 400;
			const double x0 = left ? L0 + 112 * k : R0 + (1168 - 340 * ms) * k, y0 = (686 - ph * ms) * k;
			g.Frame(x0, y0, k * ms);
			g.Rect(0, 0, 340, ph, -1, 0, 0, CK, 0.38); g.Rect(0, 0, 340, ph, CP, 1, 0.32, CP, 0.06);
			for (int i = 0; i < 4; ++i)
			{
				const double bx = i & 1 ? 340 : 0, by = i & 2 ? ph : 0, dx = i & 1 ? -12 : 12, dy = i & 2 ? -12 : 12;
				g.Line(bx, by, bx + dx, by, CP, 2); g.Line(bx, by, bx, by + dy, CP, 2);
			}
			static const char* LT[] = { "КАРТА", "ОРБИТА", "ПИТАНИЕ И ТЕПЛО", "ОПЦИИ" };
			static const char* LTAB[] = { "МЕСТН", "ОРБИТА", "ПИТАНИЕ", "ОПЦИИ" };
			static const char* RT[] = { "ЦЕЛИ", "ПЕРЕЛЁТ", "ПОСАДКА", "СБЛИЖЕНИЕ", "СТЫКОВКА", "ОРГАНИЗМ" };
			static const char* RTAB[] = { "ЦЕЛИ", "ПЕРЕЛЁТ", "ПОСАДКА", "СБЛИЖ", "СТЫК", "ОРГАНИЗМ" };
			const Target* s = Selected();
			g.T(9, 17, left ? LT[lpage] : RT[rpage], CP, 13);
			g.Hit(0, 0, 200, 20, left ? H_LFOLD : H_RFOLD);
			g.T(200, 16, "▾", CD, 11, 1);
			if (left && lpage == L_LOCAL && surface) g.T(331, 16, "МАСШТАБ " + Num(ZOOM[zoom], 0) + " м", CA, 9.5, 2);
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
				case R_TARGETS: pages::Targets(c); break; case R_TRANSFER: pages::Transfer(c); break; case R_LANDING: pages::Landing(c); break;
				case R_APPROACH: pages::Approach(c); break; case R_DOCK: pages::Dock(c); break; default: pages::Body(c);
				}
			}
			g.Frame(x0, y0, k * ms);
			double fy = 354;
			if (!left)
			{
				// the autopilot: filled = on, framed = armed, dim = not available here
				struct B { const char* n; int st; int req; };   // 0 off, 1 armed, 2 on, -1 not available
				std::vector<B> b;
				const bool hasSel = Selected() != nullptr, jetOk = d.jet && d.jetFuel > 0;
				if (surface) b = { { "УДЕРЖ ВЫС", jetOk ? (d.jetMode == 1 && !d.apMode ? 2 : 0) : -1, AP_ALT },
				                   { "ПЕРЕЛЁТ", jetOk && hasSel ? (d.apMode == 2 ? 2 : rpage == R_TRANSFER ? 1 : 0) : -1, 2 },
				                   { "ПОСАДКА", jetOk ? (d.jetMode == 2 ? 2 : rpage == R_LANDING ? 1 : 0) : -1, AP_LAND },
				                   { "ЗАВИС", jetOk ? (d.apMode == 1 ? 2 : 0) : -1, 1 } };
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
			g.T(331, fy, left ? "" : (surface ? "J — удерж. · C — посадка" : ""), CD, 9, 2);
		};
		auto tab = [&](bool left)
		{
			const double x0 = left ? L0 + 112 * k : R0 + (1168 - 252) * k;
			g.Frame(x0, 662 * k, k);
			g.Rect(0, 0, 252, 24, CP, 1, 0.4, CP, 0.08);
			g.Hit(0, 0, 252, 24, left ? H_LFOLD : H_RFOLD);
			const Target* s = Selected();
			std::string t = left ? std::string("M ▴ КАРТА") : std::string("N ▴ ") + (s ? "ЦЕЛЬ: " + s->name : "ЦЕЛИ НЕТ");
			g.T(10, 16, t, CP, 11);
			if (!left && (d.apMode || (d.jet && d.jetMode))) g.T(244, 16, d.apMode ? std::string("АП: ") + (d.apStatus.substr(0, d.apStatus.find(" "))) : d.jetMode == 1 ? "АП: УДЕРЖ" : "АП: ПОСАДКА", CA, 10, 2);
		};
		if (openL) mfd(true); else tab(true);
		if (openR) mfd(false); else tab(false);
	}
}

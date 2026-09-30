// OrbiterCrew - the suit computer's helmet display (see SuitHud.h).
#include "SuitHud.h"
#include <Sketchpad2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <windows.h>

namespace ocrew
{
	namespace
	{
		const DWORD CYAN = 0xFFE040, DIM = 0x8A6A28, AMBER = 0x30B0FF, RED = 0x4848FF, WHITE = 0xFFF8F0;

		std::string Fmt(const char* f, double a) { char b[64]; snprintf(b, sizeof b, f, a); return b; }
		std::string Fmt2(const char* f, double a, double b_) { char b[96]; snprintf(b, sizeof b, f, a, b_); return b; }

		const char* BodyRu(const std::string& n)
		{
			if (n == "Earth") return "Земля"; if (n == "Moon") return "Луна"; if (n == "Mars") return "Марс";
			if (n == "Venus") return "Венера"; if (n == "Titan") return "Титан"; if (n == "Mercury") return "Меркурий";
			return nullptr;
		}
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

	void SuitHud::Release()
	{
		for (oapi::Font** f : { &fBig, &fMid, &fSmall }) if (*f) { oapiReleaseFont(*f); *f = nullptr; }
		for (oapi::Pen** p : { &pGlow, &pLine, &pDim, &pAmber, &pRed, &pThick }) if (*p) { oapiReleasePen(*p); *p = nullptr; }
		for (oapi::Brush** b : { &bFill, &bDim, &bAmber, &bRed }) if (*b) { oapiReleaseBrush(*b); *b = nullptr; }
	}

	SuitHud::~SuitHud() { Release(); }

	namespace
	{
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
		unsigned long long Key(int size, int colour, unsigned cp) { return (static_cast<unsigned long long>(size) << 40) | (static_cast<unsigned long long>(colour) << 32) | cp; }
	}

	bool HudText::Load()
	{
		FILE* f = fopen("Config\Tantra\HudFont.txt", "r");
		if (!f) return false;
		char line[256];
		while (fgets(line, sizeof line, f))
		{
			int si, ci, w_, h_; unsigned cp; int x_, y_; float adv;
			if (line[0] == '#') continue;
			if (sscanf(line, "ATLAS %*d %*d %d", &pad) == 1) continue;
			if (sscanf(line, "%d %d %u %d %d %d %d %f", &si, &ci, &cp, &x_, &y_, &w_, &h_, &adv) == 8)
			{
				index.emplace_back(Key(si, ci, cp), static_cast<int>(glyphs.size()));
				glyphs.push_back({ x_, y_, w_, h_, adv });
			}
		}
		fclose(f);
		std::sort(index.begin(), index.end());
		tex = oapiLoadTexture("Tantra\HudFont.dds");
		if (!tex) oapiWriteLog(const_cast<char*>("OrbiterCrew: Textures\Tantra\HudFont.dds missing - helmet display falls back to system text"));
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
		return w * scale;
	}

	bool HudText::Draw(oapi::Sketchpad* skp, int x, int y, const std::string& utf8, int size, int colour, int align, double scale) const
	{
		auto* s2 = dynamic_cast<oapi::Sketchpad2*>(skp);
		if (!tex || !s2) return false;
		double px = x - (align == 1 ? 0.5 : align == 2 ? 1.0 : 0.0) * Width(utf8, size, scale) - pad * scale;
		const double py = y - pad * scale;
		for (unsigned cp : Decode(utf8))
		{
			const Glyph* g = Find(size, colour, cp);
			if (!g) continue;
			RECT src = { g->x, g->y, g->x + g->w, g->y + g->h };
			RECT dst = { static_cast<LONG>(px), static_cast<LONG>(py), static_cast<LONG>(px + g->w * scale), static_cast<LONG>(py + g->h * scale) };
			if (cp != ' ') s2->StretchRect(tex, &src, &dst);
			px += g->adv * scale;
		}
		return true;
	}

	void SuitHud::Setup(int h)
	{
		if (fontH == h) return;
		Release();
		fontH = h;
		fBig = oapiCreateFont((h * 30) / 900, true, const_cast<char*>("Arial"), FONT_BOLD);
		fMid = oapiCreateFont((h * 21) / 900, true, const_cast<char*>("Arial"));
		fSmall = oapiCreateFont((h * 17) / 900, true, const_cast<char*>("Arial"));
		pGlow = oapiCreatePen(1, (std::max)(3, h / 300), 0x5A4418);
		pLine = oapiCreatePen(1, 1, CYAN);
		pThick = oapiCreatePen(1, (std::max)(2, h / 450), CYAN);
		pDim = oapiCreatePen(1, 1, DIM);
		pAmber = oapiCreatePen(1, (std::max)(2, h / 450), AMBER);
		pRed = oapiCreatePen(1, (std::max)(2, h / 450), RED);
		bFill = oapiCreateBrush(CYAN); bDim = oapiCreateBrush(0x3A2C10); bAmber = oapiCreateBrush(AMBER); bRed = oapiCreateBrush(RED);
	}

	void SuitHud::Draw(oapi::Sketchpad* skp, const HUDPAINTSPEC* hps, const HudData& d)
	{
		const int W = hps->W, H = hps->H;
		Setup(H);
		const double k = H / 900.0;
		auto S = [&](double v) { return static_cast<int>(v * k); };
		const bool blink = std::fmod(d.simt, 1.0) < 0.62;

		if (!glyphsTried) { glyphsTried = true; glyphs.Load(); }
		auto text = [&](int x, int y, const std::string& s, DWORD col, oapi::Font* f, int align = 0)
		{
			const int size = f == fBig ? 0 : f == fMid ? 1 : 2;
			const int colour = col == CYAN ? 0 : col == WHITE ? 1 : col == AMBER ? 2 : col == RED ? 3 : 4;
			if (glyphs.Draw(skp, x, y, s, size, colour, align, k / 1.4)) return;
			skp->SetFont(f); skp->SetTextColor(col);
			skp->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
			const std::string a = Ru(s); skp->Text(x, y, a.c_str(), static_cast<int>(a.size()));
		};
		auto glowLine = [&](int x0, int y0, int x1, int y1, oapi::Pen* p)
		{
			skp->SetPen(pGlow); skp->Line(x0, y0, x1, y1);
			skp->SetPen(p); skp->Line(x0, y0, x1, y1);
		};
		auto brackets = [&](int x0, int y0, int x1, int y1)
		{
			const int c = S(16);
			glowLine(x0, y0, x0 + c, y0, pThick); glowLine(x0, y0, x0, y0 + c, pThick);
			glowLine(x1, y0, x1 - c, y0, pThick); glowLine(x1, y0, x1, y0 + c, pThick);
			glowLine(x0, y1, x0 + c, y1, pThick); glowLine(x0, y1, x0, y1 - c, pThick);
			glowLine(x1, y1, x1 - c, y1, pThick); glowLine(x1, y1, x1, y1 - c, pThick);
			skp->SetPen(pDim); skp->Line(x0 + c + S(6), y0, x1 - c - S(6), y0);
		};
		auto bar = [&](int x, int y, int w, int h, double f, oapi::Brush* fill)
		{
			f = std::clamp(f, 0.0, 1.0);
			skp->SetPen(pDim); skp->SetBrush(bDim); skp->Rectangle(x, y, x + w, y + h);
			if (f > 0.002) { skp->SetPen(nullptr); skp->SetBrush(fill); skp->Rectangle(x + 1, y + 1, x + 1 + static_cast<int>((w - 2) * f), y + h - 1); }
			skp->SetBrush(nullptr);
			for (int t = 1; t < 10; ++t) { skp->SetPen(pDim); const int tx = x + (w * t) / 10; skp->Line(tx, y + h, tx, y + h + S(3)); }
		};
		auto level = [&](double f, double warn, double crit, bool low) -> oapi::Brush*
		{
			if (low) return f < crit ? bRed : f < warn ? bAmber : bFill;
			return f > crit ? bRed : f > warn ? bAmber : bFill;
		};
		const int rh = S(27);

		// ---- header ----
		const char* st = d.state == 2 ? "  ·  ГИБЕЛЬ" : d.state == 1 ? "  ·  БЕЗ СОЗНАНИЯ" : "";
		text(W / 2, S(70), d.name + "  ·  " + d.role + st, d.state ? RED : CYAN, fMid, 1);
		glowLine(W / 2 - S(180), S(96), W / 2 + S(180), S(96), pLine);
		if (d.state == 2) return;

		// ---- left: the body ----
		{
			const int x0 = S(24), y0 = S(130), w = S(300);
			brackets(x0 - S(10), y0 - S(12), x0 + w + S(10), y0 + rh * 9 + S(8));
			text(x0, y0, "ОРГАНИЗМ", CYAN, fBig);
			int y = y0 + rh + S(8);
			text(x0, y, "Пульс", CYAN, fMid); text(x0 + w, y, Fmt("%.0f", d.pulse) + " уд/мин", d.pulse > 170 ? AMBER : WHITE, fMid, 2); y += rh;
			text(x0, y, "Дыхание", CYAN, fMid); text(x0 + w, y, Fmt("%.0f", d.breath) + " в мин", WHITE, fMid, 2); y += rh;
			text(x0, y, "Нагрузка", CYAN, fMid); bar(x0 + S(135), y + S(5), S(115), S(12), d.effort, level(d.effort, 0.85, 1.0, false));
			text(x0 + w, y, Fmt("%.0f %%", 100 * d.effort), WHITE, fMid, 2); y += rh;
			text(x0, y, "Выносливость", CYAN, fMid); bar(x0 + S(135), y + S(5), S(115), S(12), d.stamina, level(d.stamina, 0.3, 0.15, true));
			text(x0 + w, y, Fmt("%.0f %%", 100 * d.stamina), WHITE, fMid, 2); y += rh;
			text(x0, y, "Кислород (вдох)", CYAN, fMid); text(x0 + w, y, Fmt("%.1f", d.ppO2) + " кПа", d.ppO2 < 10 ? RED : d.ppO2 < 14 ? AMBER : WHITE, fMid, 2); y += rh;
			text(x0, y, "CO2 (вдох)", CYAN, fMid); text(x0 + w, y, Fmt("%.2f", d.ppCO2) + " кПа", d.ppCO2 > 5 ? RED : d.ppCO2 > 1 ? AMBER : WHITE, fMid, 2); y += rh;
			text(x0, y, "Температура тела", CYAN, fMid);
			text(x0 + w, y, Fmt("%.1f", d.coreC) + " °C", d.coreC > 39.5 || d.coreC < 35 ? RED : d.coreC > 38.5 || d.coreC < 35.8 ? AMBER : WHITE, fMid, 2); y += rh;
			if (d.injury > 0.01)
			{
				text(x0, y, "Травма", AMBER, fMid); bar(x0 + S(135), y + S(5), S(115), S(12), d.injury, bRed);
				text(x0 + w, y, Fmt("%.0f %%", 100 * d.injury), AMBER, fMid, 2);
			}
		}

		// ---- right: the suit, or the air she breathes ----
		{
			const int w = S(330), x0 = W - S(24) - w, y0 = S(130);
			const int rows = d.suit ? 11 : 4;
			brackets(x0 - S(10), y0 - S(12), x0 + w + S(10), y0 + rh * rows + S(8));
			text(x0, y0, d.suit ? "СКАФАНДР" : "КОМБИНЕЗОН", CYAN, fBig);
			int y = y0 + rh + S(8);
			if (d.suit)
			{
				text(x0, y, "Кислород", CYAN, fMid); bar(x0 + S(120), y + S(5), S(110), S(12), d.o2, level(d.o2, 0.25, 0.1, true));
				text(x0 + w, y, Fmt2("%.0f %%  %.1f ч", 100 * d.o2, d.o2Hours), WHITE, fMid, 2); y += rh;
				text(x0, y, "Поглотитель", CYAN, fMid); bar(x0 + S(120), y + S(5), S(110), S(12), d.sorbent, level(d.sorbent, 0.25, 0.05, true));
				text(x0 + w, y, Fmt("%.0f %%", 100 * d.sorbent), WHITE, fMid, 2); y += rh;
				text(x0, y, "Батарея", CYAN, fMid); bar(x0 + S(120), y + S(5), S(110), S(12), d.batt, level(d.batt, 0.25, 0.1, true));
				text(x0 + w, y, Fmt2("%.0f %%  %.1f ч", 100 * d.batt, d.battHours), d.powered ? WHITE : RED, fMid, 2); y += rh;
				text(x0, y, "Потребление", CYAN, fMid); text(x0 + w, y, Fmt("%.0f", d.powerW) + " Вт", WHITE, fMid, 2); y += rh;
				text(x0 + S(12), y, Fmt("%.0f", d.lifeW) + " жизнеобесп.  ·  " + Fmt("%.0f", d.thermalW) + " тепло  ·  " + Fmt("%.0f", d.driveW) + " приводы", DIM + 0x303030, fSmall); y += rh;
				text(x0, y, "Среда", CYAN, fMid);
				text(x0 + w, y, Fmt("%.0f", d.envC) + " °C  (допуск " + Fmt("%.0f", d.ratedMinC) + "…" + Fmt("%.0f", d.ratedMaxC) + ")", d.inSpec ? WHITE : RED, fMid, 2); y += rh;
				text(x0 + S(12), y, std::string(d.sunlit ? "солнце" : "тень") + (d.hasGround ? "  ·  грунт " + Fmt("%.0f", d.groundC) + " °C" : ""), DIM + 0x303030, fSmall); y += rh;
			}
			std::string out;
			if (d.vacuum) out = "Снаружи: вакуум";
			else
			{
				const char* b = BodyRu(d.body);
				out = std::string("Снаружи: ") + (b ? b : d.body.c_str()) + "  " + Fmt("%.0f", d.airKPa) + " кПа  " + Fmt("%.0f", d.airC) + " °C";
			}
			text(x0, y, out, CYAN, fMid); y += rh;
			text(x0 + S(12), y, d.breathable ? "воздух пригоден для дыхания" : "дышать нельзя", d.breathable ? CYAN : AMBER, fSmall); y += rh;
			if (d.suit)
			{
				text(x0, y, "Азот РСУ", CYAN, fMid); bar(x0 + S(120), y + S(5), S(110), S(12), d.n2, level(d.n2, 0.25, 0.1, true));
				static const char* RCS[] = { "на грунте", "готов", "нет питания" };
				text(x0 + w, y, Fmt("%.1f", d.n2Dv) + " м/с  " + RCS[d.rcs], d.rcs == 2 ? RED : WHITE, fMid, 2); y += rh;
				text(x0, y, std::string("Щиток: ") + (d.shadeDown ? "опущен" : "поднят") + "   ·   Фонари: " + (d.lampsOn ? "вкл" : "выкл"), DIM + 0x303030, fSmall);
			}
		}

		// ---- centre: warnings and messages ----
		if (!d.warning.empty() && blink) text(W / 2, S(150), d.warning, RED, fBig, 1);
		if (!d.message.empty()) text(W / 2, S(190), d.message, WHITE, fMid, 1);

		// ---- bottom: the jet pack ----
		if (d.jet)
		{
			const int x0 = W / 2 - S(330), x1 = W / 2 + S(330), y0 = H - S(250), y1 = H - S(40);
			brackets(x0, y0, x1, y1);
			static const char* MODE[] = { "РУЧНОЕ УПРАВЛЕНИЕ", "УДЕРЖАНИЕ ВЫСОТЫ", "АВТОПОСАДКА" };
			text(x0 + S(14), y0 + S(10), "РАНЕЦ", CYAN, fBig);
			std::string mode = MODE[d.jetMode]; if (d.jetMode == 1) mode += "  " + Fmt("%.1f", d.jetAltHold) + " м";
			text(x1 - S(14), y0 + S(14), mode, d.jetMode ? AMBER : CYAN, fMid, 2);
			// throttle: a tall bar on the left
			const int tx = x0 + S(20), ty = y0 + S(52), th = y1 - ty - S(16);
			skp->SetPen(pDim); skp->SetBrush(bDim); skp->Rectangle(tx, ty, tx + S(22), ty + th);
			const int fh = static_cast<int>((th - 2) * std::clamp(d.jetThrottle, 0.0, 1.0));
			if (fh > 0) { skp->SetPen(nullptr); skp->SetBrush(d.jetProtect ? bAmber : bFill); skp->Rectangle(tx + 1, ty + th - 1 - fh, tx + S(21), ty + th - 1); }
			skp->SetBrush(nullptr);
			text(tx + S(11), ty + th + S(2), Fmt("%.0f %%", 100 * d.jetThrottle), WHITE, fSmall, 1);
			text(tx + S(34), ty - S(2), "ТЯГА", CYAN, fSmall);
			// fuel
			const int fx = x0 + S(80);
			text(fx, y0 + S(52), "Топливо", CYAN, fMid); bar(fx + S(110), y0 + S(57), S(170), S(12), d.jetFuel, level(d.jetFuel, 0.25, 0.1, true));
			text(fx + S(300), y0 + S(52), Fmt("%.0f %%", 100 * d.jetFuel) + "   запас " + Fmt("%.0f", d.jetDv) + " м/с", WHITE, fMid);
			// flight numbers
			text(fx, y0 + S(52) + rh, "Высота " + Fmt("%.1f", d.alt) + " м    верт. " + Fmt("%+.1f", d.vs) + " м/с    гориз. " + Fmt("%.1f", d.gs) + " м/с",
				d.jetProtect ? AMBER : WHITE, fMid);
			std::string booms = d.jetDeploy > 0.98 ? "раскрыты" : d.jetDeploy < 0.02 ? "сложены" : "раскрываются";
			text(fx, y0 + S(52) + 2 * rh, "Балки: " + booms, DIM + 0x303030, fSmall);
			// the pods, seen from her left side: a nacelle and its jet, tilted
			for (int i = 0; i < 2; ++i)
			{
				const int cx = x1 - S(170) + i * S(110), cy = y1 - S(80), L = S(34);
				const double t = d.jetTilt[i];
				skp->SetPen(pGlow); skp->SetBrush(nullptr); skp->Ellipse(cx - S(9), cy - S(9), cx + S(9), cy + S(9));
				skp->SetPen(pThick); skp->Ellipse(cx - S(9), cy - S(9), cx + S(9), cy + S(9));
				glowLine(cx, cy, cx - static_cast<int>(L * std::sin(t)), cy + static_cast<int>(L * std::cos(t)), d.jetThrottle > 0.01 ? pAmber : pThick);
				// the allowed range
				skp->SetPen(pDim);
				skp->Line(cx, cy, cx - static_cast<int>(L * std::sin(d.jetMaxTilt)), cy + static_cast<int>(L * std::cos(d.jetMaxTilt)));
				skp->Line(cx, cy, cx + static_cast<int>(L * std::sin(d.jetMaxTilt)), cy + static_cast<int>(L * std::cos(d.jetMaxTilt)));
				text(cx, cy + L + S(4), Fmt("%+.0f°", d.jetTilt[i] * DEG), WHITE, fSmall, 1);
				text(cx, cy - S(34), i == 0 ? "правая" : "левая", DIM + 0x303030, fSmall, 1);
			}
			std::string flags;
			if (d.boost) flags += "ПОЛНЫЙ ВЕКТОР   ";
			if (d.jetLimited) flags += "УВТ ОГРАНИЧЕН   ";
			if (d.jetTerrain) flags += "РЕЛЬЕФ ВПЕРЕДИ   ";
			if (d.jetProtect) flags += "ЗЕМЛЯ БЛИЗКО";
			if (!flags.empty()) text(W / 2, y0 - S(34), flags, AMBER, fMid, 1);

			// flying: a horizon line in the middle of the view, tilted with her
			if (d.jetFlying)
			{
				const int cx = W / 2, cy = H / 2, r = S(60);
				const double ca = std::cos(d.leanR), sa = std::sin(d.leanR), off = d.leanF * S(300);
				glowLine(cx - static_cast<int>(r * 2 * ca), cy + static_cast<int>(off + r * 2 * sa), cx - static_cast<int>(r * ca), cy + static_cast<int>(off + r * sa), pThick);
				glowLine(cx + static_cast<int>(r * ca), cy + static_cast<int>(off - r * sa), cx + static_cast<int>(r * 2 * ca), cy + static_cast<int>(off - r * 2 * sa), pThick);
				skp->SetPen(pLine); skp->Line(cx - S(8), cy, cx + S(8), cy); skp->Line(cx, cy - S(8), cx, cy + S(8));
			}
		}

		// ---- bottom left: the keys ----
		{
			const int x0 = S(24); int y = H - S(34) - (d.jet ? 3 : 2) * S(22);
			text(x0, y, "W/S ход  A/D поворот  Q/E шаг  Shift бег  Пробел прыжок", DIM + 0x303030, fSmall); y += S(22);
			text(x0, y, "K скафандр  V щиток  L фонари  B ранец", DIM + 0x303030, fSmall); y += S(22);
			if (d.jet) text(x0, y, "Ранец: Пробел вверх  Ctrl вниз  W/S вектор  Q/E вбок  A/D поворот  Shift полный вектор  0/. тяга  J высота  C посадка  G балки", DIM + 0x303030, fSmall);
		}
	}
}

// TantraPlantScreen: see TantraPlantScreen.h. Draw() follows draw() of Tantra_Design/tantra_plant_screen.html block by block;
// the coordinates are the mockup's, the rows below the title stretched to the riser's height (Y1 the upper blocks, Y2 the lower).
#include "TantraPlantScreen.h"
#include "SkpCompat.h"   // the extended Sketchpad (QuickPen/QuickBrush, StretchRect, SetBrightness)
#include "TantraScreenFont.h"
#include "TantraScreenCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

namespace tantra::plantscreen {

using tantra::ScreenFont;
namespace pl = tantra::plant;
using pl::Plant;

namespace {

constexpr double kPi = 3.14159265358979323846, kMu0 = 4e-7 * kPi;
constexpr double kPelletHz = 2000.0, kGainQ = 120.0, kCoilTurns = 200.0, kRMarch = 2.2;   // as the mockup
constexpr double kInf = 1e300;

// the mockup's palette (CSS 0xRRGGBB)
constexpr unsigned cBg = 0x0a1311, cFr = 0x1f3a35, cTx = 0x7fe0d0, cDim = 0x4f8f86, cOr = 0xee9a3a, cYe = 0xe8d35a, cRd = 0xff5a4a,
                   cGr = 0x5ad0a0, cWh = 0xe8fffa, cPipeF = 0xc8a24a, cPipeR = 0x4fa3d8, cPipeC = 0x5fd8ff, cPipeH = 0xe0703a,
                   cTrig = 0xb07cff, cEnv = 0x7fb8ff;

DWORD Skp(unsigned rgb, double a = 1.0) {   // 0xRRGGBB + alpha -> the sketchpad's 0xAABBGGRR
    const unsigned r = (rgb >> 16) & 255u, g = (rgb >> 8) & 255u, b = rgb & 255u;
    long A = std::lround((std::max)(0.0, (std::min)(1.0, a)) * 255.0);
    if (A < 1) A = 1;
    return (DWORD(A) << 24) | (b << 16) | (g << 8) | r;
}
unsigned Mix(unsigned c, unsigned bg, double a) {   // c over bg at alpha a (the canvas' globalAlpha over the screen's ground)
    auto ch = [&](int s) { return unsigned(std::lround(((c >> s) & 255u) * a + ((bg >> s) & 255u) * (1.0 - a))) << s; };
    return ch(16) | ch(8) | ch(0);
}
unsigned Rgb(int r, int g, int b) { return (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b); }
std::wstring W1251(const std::string& s) {   // the plant's messages are in the program's charset
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(1251, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(1251, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
// numbers as the mockup's ru-RU toLocaleString: a decimal comma, the thousands by a no-break space
std::wstring Fmt(double v, int d) {
    if (!std::isfinite(v)) return L"—";
    wchar_t b[64];
    std::swprintf(b, 64, L"%.*f", d, std::fabs(v));
    std::wstring s(b), ip = s, fp;
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) { ip = s.substr(0, dot); fp = s.substr(dot + 1); }
    std::wstring g;
    const int n = int(ip.size());
    for (int i = 0; i < n; ++i) { g += ip[i]; const int rest = n - 1 - i; if (rest > 0 && rest % 3 == 0) g += L' '; }
    bool neg = false;
    if (v < 0) for (wchar_t c : s) if (c >= L'1' && c <= L'9') neg = true;
    return (neg ? L"-" : L"") + g + (fp.empty() ? L"" : L"," + fp);
}
std::wstring fF(double F) { return F >= 1e9 ? Fmt(F / 1e9, 2) + L" ГН" : F >= 1e6 ? Fmt(F / 1e6, 0) + L" МН" : Fmt(F / 1e3, 0) + L" кН"; }
std::wstring fW(double W) {
    return W >= 1e12 ? Fmt(W / 1e12, 1) + L" ТВт" : W >= 1e9 ? Fmt(W / 1e9, 1) + L" ГВт" : W >= 1e6 ? Fmt(W / 1e6, 0) + L" МВт" : Fmt(W / 1e3, 0) + L" кВт";
}
std::wstring fM(double m) { return m >= 1000 ? Fmt(m / 1000, 2) + L" т/с" : m >= 1 ? Fmt(m, 1) + L" кг/с" : Fmt(m * 1000, 0) + L" г/с"; }
std::wstring fP(double p) { return p >= 0.1 ? Fmt(p * 100, 0) + L" %" : p >= 0.001 ? Fmt(p * 100, 2) + L" %" : p > 0 ? Fmt(p * 100, 4) + L" %" : L"0"; }
std::wstring Clock(double t) {   // the journal's time: m:ss, h:mm:ss
    const long s = long((std::max)(0.0, t));
    wchar_t b[32];
    if (s < 3600) std::swprintf(b, 32, L"%ld:%02ld", s / 60, s % 60);
    else std::swprintf(b, 32, L"%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

}  // namespace

namespace {

using tantra::scr::Pt;
using Canvas = tantra::scr::Canvas;

void Frame(Canvas& g, double x, double y, double w, double h, const std::wstring& title) {
    g.Stroke(x, y, w, h, cFr, 1.5);
    if (title.empty()) return;
    g.Fill(x + 10, y - 9, g.Width(title, 12, 700) + 30, 18, cBg);
    g.T(title, x + 18, y + 5, cDim, 12, 0, 700);
}
void Bar(Canvas& g, double x, double y, double w, double h, double f, unsigned col) {
    g.Fill(x, y, w, h, 0x14211e);
    g.Fill(x, y, w * (std::max)(0.0, (std::min)(1.0, f)), h, col);
    g.Stroke(x, y, w, h, cFr, 1);
}
void Gauge(Canvas& g, double x, double y, const std::wstring& label, const std::wstring& val, unsigned col = cTx) {
    g.T(label, x, y, cDim, 11);
    g.T(val, x, y + 17, col, 15, 0, 700);
}
void Node(Canvas& g, double x, double y, double w, double h, const std::wstring& title, unsigned col = cFr) {
    g.Fill(x, y, w, h, 0x0f1c19);
    g.Stroke(x, y, w, h, col, 1.5);
    g.T(title, x + w / 2, y + 16, cTx, 12, 1, 700);
}
// a pipe with flowing dots (speed ~ flow): the body at the canvas' 0.35 over the ground
void Pipe(Canvas& g, const std::vector<Pt>& p, unsigned col, double flow, double width, double phase) {
    g.Thick(p, width, col, 0.35);
    if (flow <= 0) return;
    std::vector<double> seg;
    double L = 0;
    for (size_t i = 1; i < p.size(); ++i) { seg.push_back(std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y)); L += seg.back(); }
    const double sp = 18, off = std::fmod(phase * 60 * (std::min)(3.0, 0.4 + flow), sp);
    for (double s = off; s < L; s += sp) {
        double acc = 0; size_t i = 0;
        while (i < seg.size() && acc + seg[i] < s) { acc += seg[i]; ++i; }
        if (i >= seg.size()) break;
        const double f = seg[i] > 0 ? (s - acc) / seg[i] : 0;
        g.Disc(p[i].x + (p[i + 1].x - p[i].x) * f, p[i].y + (p[i + 1].y - p[i].y) * f, width * 0.45, col);
    }
}
std::vector<Pt> Bezier(Pt a, Pt b, Pt c, Pt d, int n = 18) {
    std::vector<Pt> r;
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n, u = 1 - t;
        r.push_back({u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x, u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
    }
    return r;
}
std::vector<Pt> Quad(Pt a, Pt b, Pt c, int n = 14) {
    std::vector<Pt> r;
    for (int i = 0; i <= n; ++i) { const double t = double(i) / n, u = 1 - t; r.push_back({u * u * a.x + 2 * u * t * b.x + t * t * c.x, u * u * a.y + 2 * u * t * b.y + t * t * c.y}); }
    return r;
}
unsigned Zc(double Tk, double lim) { return Tk >= lim ? cRd : Tk >= 0.85 * lim ? cOr : Tk >= 0.6 * lim ? cYe : cGr; }

}  // namespace

Screen::Screen() : font_(new ScreenFont) {}
Screen::~Screen() { delete font_; }

int Screen::Hit(double x, double y, double* along) const {
    for (const Area& a : hits_) {
        const double m = a.cmd == kCmdThrottle ? 8.0 : 3.0;
        if (x >= a.x - m && x <= a.x + a.w + m && y >= a.y - m && y <= a.y + a.h + m) {
            if (along) *along = (std::max)(0.0, (std::min)(1.0, (x - a.x) / a.w));
            return a.cmd;
        }
    }
    return -1;
}

void Screen::Draw(oapi::Sketchpad* skp, int ox, int oy, int w, int h, const View& v, double dt) {
    hits_.clear();
    if (!skp || !v.plant) return;
    Canvas g(skp, *font_, ox, oy);
    const Plant& p = *v.plant;
    const pl::Output& o = v.out;
    const pl::Config& cfg = p.Cfg();
    const double W = w, H = (std::max)(800, h);
    // the riser is taller than the mockup: the upper blocks take 57 % of the rest, the lower 43 %
    const double ext = H - 800.0, dTop = std::round(ext * 0.57), dBot = ext - dTop;
    const double k1 = (470.0 + dTop) / 470.0, k2 = (240.0 + dBot) / 240.0;
    auto Y1 = [&](double y) { return 62.0 + (y - 62.0) * k1; };
    auto Y2 = [&](double y) { return 548.0 + dTop + (y - 548.0) * k2; };
    auto btn = [&](const std::wstring& s, double x, double y, double bw, double bh, bool on, int cmd, unsigned col) {
        g.Fill(x, y, bw, bh, on ? 0x3b2a14 : 0x121d1b);
        g.Stroke(x, y, bw, bh, on ? col : 0x5a4325, on ? 2 : 1);
        int size = 14;
        while (size > 10 && g.Width(s, size, 600) > bw - 10) --size;
        g.T(s, x + bw / 2, y + bh / 2 + 5, on ? 0xffd29a : col, size, 1, 600);
        if (cmd >= 0) hits_.push_back({x, y, bw, bh, cmd});
    };

    const int stage = p.StageNow();
    const bool run = stage == pl::kStRun;
    const double thr = run ? v.thr : 0.0;
    const double B = p.Field(), T = p.SternT(), coilT = p.CoilT();
    const int mass = o.mass;
    const double F = o.thrust, mdot = o.mdot, Pf = o.fusion, cv = o.exhaust, cF = o.cupThrust;
    const double TSAFE = cfg.tSafe, TBOIL = cfg.tBoil, TSOFT = cfg.tSoft, TBREACH = cfg.tBreach, TLOST = cfg.tLost;
    const double Cst = cfg.sternStore / 1400.0;
    const bool lost = stage == pl::kStGone || v.hullLost;
    flow_ += dt;
    pellet_ += dt * (run ? kPelletHz * Pf / Plant::kFusionMax : 0.0);
    double skin[7];
    for (int i = 0; i < 7; ++i) skin[i] = v.skin[i];
    skin[4] = (std::max)(skin[4], T);                                    // the stern's skin is the plant's stern

    g.Fill(0, 0, W, H, cBg);
    g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    // ---- title bar ----
    g.T(L"СИЛОВАЯ УСТАНОВКА · ИОННО-ТРИГГЕРНАЯ · МАРШЕВАЯ ЧАША + 4 ГОНДОЛЫ", 22, 36, cTx, 20, 0, 700);
    static const wchar_t* const kStage[8] = {L"ВЫКЛЮЧЕНА", L"ПРОВЕРКА КРИОГЕНИКИ", L"ПОДЪЁМ ПОЛЯ", L"ЗАРЯД ТРИГГЕРА", L"ПОДАЧА КАПСУЛ", L"НА РЕЖИМЕ",
                                             L"СРЫВ ПОЛЯ · ОХЛАЖДЕНИЕ", L"КОРАБЛЬ ПОТЕРЯН"};
    std::wstring stTxt = kStage[(std::max)(0, (std::min)(7, stage))];
    if (stage == pl::kStQuench && p.CoilsLost()) stTxt = L"ОБМОТКА ПОТЕРЯНА · ДО СТАНЦИИ";
    if (v.hullLost) stTxt = L"КОРАБЛЬ ПОТЕРЯН";
    const unsigned stCol = run ? cGr : stage == pl::kStQuench || lost ? cRd : stage == pl::kStOff ? cDim : cYe;
    g.Disc(1010, 30, 9, stCol);
    const double stEnd = 1026 + g.T(stTxt, 1026, 36, stCol, 16, 0, 700);
    static const wchar_t* const kEnvT[4] = {L"атмосфера", L"космос", L"перелёт", L"вход в атмосферу"};
    static const wchar_t* const kMassT[3] = {L"аргон", L"железо", L"продукты синтеза"};
    const int env = (std::max)(0, (std::min)(3, v.env));
    std::wstring envLine = std::wstring(L"среда: ") + (env == 0 && v.onGround ? L"атмосфера, грунт" : kEnvT[env]) + L" · рабочая масса: " + kMassT[mass];
    if (W - 22 - g.Width(envLine, 13, 500) < stEnd + 20) envLine = std::wstring(L"рабочая масса: ") + kMassT[mass];
    g.T(envLine, W - 22, 36, cDim, 13, 2);

    // ---- left: the mimic diagram ----
    const double mH = Y1(532) - 62;
    Frame(g, 15, 62, 995, mH, L"МНЕМОСХЕМА · КОРМА");
    const double ax = 640, ay = Y1(300);                                   // the cup throat on the diagram
    const double rF = Y1(90), rT = Y1(200), rR = Y1(330), rC = Y1(440);    // the rows: fuel, trigger, reaction mass, cryo
    // fuel: p-11B store -> pellet press -> injector -> throat
    Node(g, 30, rF, 150, 70, L"ТОПЛИВО p-¹¹B"); Gauge(g, 42, rF + 38, L"запас", Fmt(p.Fuel() / 1e6, 2) + L" кт");
    Node(g, 220, rF, 150, 70, L"ПРЕСС КАПСУЛ");
    const double pHz = run ? kPelletHz * p.PowerPct() / 100.0 * thr : 0.0;
    Gauge(g, 232, rF + 38, L"частота", Fmt(pHz, 0) + L" Гц");
    Node(g, 410, rF, 150, 70, L"ИНЖЕКТОР");
    const double pE = pHz > 0 ? Pf / pHz : 0.0, pm = pE / Plant::kEFus;
    Gauge(g, 422, rF + 38, L"капсула", pHz > 0 ? (pm >= 1e-3 ? Fmt(pm * 1000, 1) + L" г" : Fmt(pm * 1e6, 0) + L" мг") + L" · " + Fmt(pE / 1e9, 1) + L" ГДж" : L"—");
    const double yF = rF + 35;
    Pipe(g, {{180, yF}, {220, yF}}, cPipeF, run ? thr : 0, 6, flow_);
    Pipe(g, {{370, yF}, {410, yF}}, cPipeF, run ? thr : 0, 6, flow_);
    Pipe(g, {{560, yF}, {ax - 30, yF}, {ax - 30, ay - 40}, {ax - 6, ay - 6}}, cPipeF, run ? thr : 0, 6, flow_);
    // trigger: field store -> ion trigger -> throat
    Node(g, 30, rT, 150, 70, L"НАКОПИТЕЛЬ ПОЛЯ"); Gauge(g, 42, rT + 38, L"запас", Fmt((std::min)(1.0, 0.3 + B / 12.1 * 0.7) * 100, 0) + L" %");
    Node(g, 220, rT, 150, 70, L"ИОННЫЙ ТРИГГЕР"); Gauge(g, 232, rT + 38, L"мощность", fW(Pf / kGainQ));
    const double yT = rT + 35;
    Pipe(g, {{180, yT}, {220, yT}}, cPipeC, stage == pl::kStTrigger || run ? 1 : 0, 4, flow_);
    Pipe(g, {{370, yT}, {ax - 60, yT}, {ax - 8, ay - 4}}, cTrig, run ? thr : 0, 4, flow_);
    g.T(L"усиление каскада Q 120 · импульс " + (pHz > 0 ? Fmt(pE / kGainQ / 1e6, 0) + L" МДж" : std::wstring(L"—")), 232, rT + 90, cDim, 11);
    // reaction mass: tanks -> pumps -> jacket (cooling) -> curtain into the burn
    const std::wstring rmName = mass == pl::kArgon ? L"АРГОН (жидк., 87 К)" : mass == pl::kIron ? L"ЖЕЛЕЗО (заряды)" : L"—";
    Node(g, 30, rR, 150, 70, L"РАБОЧАЯ МАССА");
    Gauge(g, 42, rR + 38, mass == pl::kProducts ? L"не подаётся" : rmName,
          mass == pl::kArgon ? Fmt(v.argon / 1e6, 2) + L" кт" : mass == pl::kIron ? Fmt(v.iron / 1e6, 2) + L" кт" : L"—");
    Node(g, 220, rR, 150, 70, L"НАСОСЫ"); Gauge(g, 232, rR + 38, L"расход", mass == pl::kProducts ? L"—" : fM(mdot));
    Node(g, 410, rR, 150, 70, L"РУБАШКА ЧАШИ");
    const double jIn = mass == pl::kArgon ? 87 : 300;
    const double jOut = run && mdot > 0 ? jIn + (std::min)(2600.0, o.heatIn / (std::max)(1.0, mdot * (mass == pl::kArgon ? 520 : 450))) : jIn;
    Gauge(g, 422, rR + 38, L"вход → выход", mass == pl::kProducts ? L"—" : Fmt(jIn, 0) + L" → " + Fmt(jOut, 0) + L" К");
    const double rmFlow = run && mass != pl::kProducts ? thr : 0, yR = rR + 35;
    Pipe(g, {{180, yR}, {220, yR}}, cPipeR, rmFlow, 7, flow_);
    Pipe(g, {{370, yR}, {410, yR}}, cPipeR, rmFlow, 7, flow_);
    Pipe(g, {{560, yR}, {ax - 30, yR}, {ax - 30, ay + 40}, {ax - 6, ay + 6}}, cPipeR, rmFlow, 7, flow_);
    // cryo for the windings
    Node(g, 30, rC, 150, 70, L"КРИОГЕНИКА");
    const double margin0 = (std::max)(0.0, 1 - std::pow(B / 18.5, 1.3) - (coilT - 20) / 40);
    Gauge(g, 42, rC + 38, L"обмотка · запас по току", Fmt(coilT, 1) + L" К · " + fP(margin0),
          coilT > 26 || margin0 < 0.15 ? cRd : coilT > 22 || margin0 < 0.3 ? cYe : cTx);
    const double yC = rC + 35;
    Pipe(g, {{180, yC}, {ax - 70, yC}, {ax - 70, ay + 75}}, cPipeC, B > 0.5 ? 0.6 : 0.1, 4, flow_);
    // the cup: windings (cross-section), field lines, the throat and the burn
    const double Bq = (B / Plant::kBNom) * (B / Plant::kBNom);
    const unsigned fieldCol = B > 16 ? cRd : B > 12.15 ? cYe : cPipeC;
    for (int s = -1; s <= 1; s += 2) {
        g.Fill(ax - 18, ay + s * 58 - 14, 36, 28, stage == pl::kStQuench ? 0x5a1a14 : 0x203a52);
        g.Stroke(ax - 18, ay + s * 58 - 14, 36, 28, fieldCol, 2);
    }
    g.T(L"ОБМОТКА", ax, ay - 82, cDim, 11, 1, 700);
    const double fa = (std::min)(1.0, 0.15 + 0.6 * Bq * (B > 0.3 ? 1 : 0));   // field lines: a nozzle diverging aft (to the right)
    for (int k = 1; k <= 5; ++k)
        for (int s = -1; s <= 1; s += 2)
            g.Polyline(Bezier({ax - 40, ay + s * (58 - k * 7.0)}, {ax + 30, ay + s * (58 - k * 9.0)}, {ax + 90, ay + s * (36 + k * 12.0)},
                              {ax + 160, ay + s * (44 + k * 16.0)}), fieldCol, 1.4, fa);
    // burn flashes in the throat (the radial gradient as rings, the outer first)
    const double flash = run && thr > 0 ? 0.5 + 0.5 * std::fabs(std::sin(pellet_ * 0.031)) : 0.0;
    if (flash > 0) {
        const double r0 = 2, r1 = 26 + 30 * thr;
        auto at = [&](double t, unsigned* c) {   // the gradient's colour and alpha at t (0 the centre .. 1 the rim)
            if (t < 0.4) { const double u = t / 0.4; *c = Rgb(int(255 - 55 * u), int(255 - 75 * u), 255); return flash * (1 - 0.4 * u); }
            const double u = (t - 0.4) / 0.6; *c = Rgb(int(200 - 80 * u), int(180 - 90 * u), 255); return 0.6 * flash * (1 - u);
        };
        const int N = 14;
        for (int i = 0; i < N; ++i) {                                       // opaque rings, the outer first: each the gradient over the ground
            const double t = 1.0 - (i + 0.5) / N, r = r0 + (r1 - r0) * (1.0 - double(i) / N);
            unsigned c; const double A = at(t, &c);
            if (A > 0.02) g.Disc(ax + 46, ay, r, c, A);
        }
    }
    // the jet
    if (run && thr > 0) {
        const unsigned j0 = mass == pl::kArgon ? Rgb(255, 240, 220) : mass == pl::kIron ? Rgb(210, 220, 255) : Rgb(230, 215, 255);
        const unsigned j1 = mass == pl::kArgon ? Rgb(255, 150, 60) : mass == pl::kIron ? Rgb(110, 70, 255) : Rgb(140, 100, 255);
        const double len = 160, wid = mass == pl::kArgon ? 34 : mass == pl::kIron ? 18 : 6;
        const int N = 20;
        for (int i = 0; i < N; ++i) {
            const double t0 = double(i) / N, t1 = double(i + 1) / N, tm = (t0 + t1) / 2;
            const double h0 = wid * (0.3 + (1 + thr - 0.3) * t0), h1 = wid * (0.3 + (1 + thr - 0.3) * t1);
            const double x0 = ax + 50 + len * t0, x1 = ax + 50 + len * t1;
            g.Shape({{x0, ay - h0}, {x1, ay - h1}, {x1, ay + h1}, {x0, ay + h0}}, Mix(j1, j0, tm), 0.85 * thr * (1 - tm));
        }
    }
    // the cup shell
    {
        std::vector<Pt> a = Quad({ax - 6, ay - 44}, {ax + 28, ay - 40}, {ax + 34, ay - 14}), b = Quad({ax - 6, ay + 44}, {ax + 28, ay + 40}, {ax + 34, ay + 14});
        g.Polyline(a, cWh, 3); g.Polyline(b, cWh, 3);
    }
    // sensors around the cup
    const double yS = Y1(172), yS2 = Y1(460);
    Gauge(g, ax - 10, yS, L"ПОЛЕ", Fmt(B, 2) + L" Тл", fieldCol);
    Gauge(g, ax + 95, yS, L"ток обмотки", Fmt(2 * kRMarch * B / kMu0 / kCoilTurns / 1e3, 0) + L" кА");
    Gauge(g, ax + 190, yS, L"напряжение", fP(Bq), B > 16 ? cRd : B > 12.15 ? cYe : cTx);
    Gauge(g, ax + 280, yS, L"давление поля", Fmt(B * B / (2 * kMu0) / 1e6, 0) + L" МПа");
    Gauge(g, ax - 40, yS2, L"струя", run && thr > 0 ? Fmt(cv / 1e3, 0) + L" км/с" : L"—");
    Gauge(g, ax + 55, yS2, L"расход", run && thr > 0 ? fM(mdot) : L"—");
    Gauge(g, ax + 160, yS2, L"тяга", fF(F), cWh);
    // the stern and the crests
    const unsigned Tc = T > TBOIL ? cRd : T > TSAFE ? cYe : cTx;
    const double rK = Y1(250), rG = Y1(330), dRad = p.Damage(pl::kRadiators);
    Node(g, 830, rK, 165, 62, L"КОРМА", T > TSAFE ? cRd : cFr); Gauge(g, 842, rK + 34, L"температура", Fmt(T, 0) + L" К", Tc);
    Node(g, 830, rG, 165, 62, L"ГРЕБНИ-РАДИАТОРЫ");
    Gauge(g, 842, rG + 34, L"сброс", fW(o.radiated) + (dRad < 1 ? L" · " + fP(dRad) : std::wstring()), dRad < 1 ? cYe : cTx);
    Pipe(g, {{ax + 30, ay + 70}, {805, ay + 70}, {805, rG + 31}, {830, rG + 31}}, cPipeH, (std::min)(1.0, o.radiated / 7e8), 5, flow_);
    // pods
    for (int i = 0; i < 4; ++i) {
        const bool on = v.pods == 2;
        const double px = 640 + i * 90, py = 74;
        Node(g, px, py, 84, 50, L"ГОНДОЛА " + std::to_wstring(i + 1), on ? cOr : cFr);
        g.T(on ? L"3 × " + Fmt(v.podCup / 1e6, 0) + L" МН" : v.pods == 1 ? L"готова" : L"в отсеке", px + 42, py + 40, on ? cOr : cDim, 12, 1, 600);
    }
    g.T(L"гондолы — до М 0,8", 1000, 140, cDim, 11, 2);

    // ---- right: calculations ----
    Frame(g, 1025, 62, 560, mH, L"РАСЧЁТ");
    const bool limField = std::strcmp(o.limit, "поле") == 0, limPow = std::strcmp(o.limit, "мощность") == 0, limHeat = std::strcmp(o.limit, "тепло") == 0;
    g.T(L"БЮДЖЕТ ТЯГИ", 1045, Y1(92), cDim, 12, 0, 700);
    const double Ffield = o.fieldThrust, Fpow = o.powerThrust, Fmax = (std::max)({Ffield, Fpow, cF, 1.0});
    struct Row { const wchar_t* t; double v; bool on; };
    const Row rows[3] = {{L"поле (B²/2μ₀·A)", Ffield, limField}, {L"мощность (2P/v)", Fpow, limPow}, {L"тепло кормы", limHeat ? cF : kInf, limHeat}};
    for (int i = 0; i < 3; ++i) {
        const double y = Y1(108 + i * 30);
        const bool fin = rows[i].v < kInf;
        g.T(rows[i].t, 1045, y + 13, rows[i].on ? cYe : cDim, 12);
        Bar(g, 1190, y, 260, 16, fin ? rows[i].v / Fmax : 1, rows[i].on ? 0xb8691f : 0x2c5a50);
        g.T(fin ? fF(rows[i].v) : L"не ограничивает", 1570, y + 13, rows[i].on ? cYe : cTx, 12, 2);
    }
    g.T(std::wstring(L"ограничивает: ") + (limHeat ? L"ТЕПЛО" : limPow ? L"МОЩНОСТЬ" : L"ПОЛЕ"), 1045, Y1(214), cYe, 14, 0, 700);
    g.T(L"тяга сейчас " + fF(F) + L" из " + fF(cF), 1570, Y1(214), cWh, 14, 2, 700);
    // heat on the stern: sources and removal
    g.T(L"ТЕПЛО НА КОРМУ", 1045, Y1(248), cDim, 12, 0, 700);
    static const wchar_t* const kSrc[5] = {L"излучение реакции", L"прорыв плазмы", L"отражение от грунта", L"слой в воздухе", L"плазма на чашу"};
    for (int i = 0; i < 5; ++i) {
        const double y = Y1(268 + i * 18), q = o.sources[i];
        g.T(kSrc[i], 1045, y, cDim, 12);
        g.T(fW(q), 1290, y, q > 1e10 ? cRd : q > 1e9 ? cYe : cTx, 13, 2, 600);
    }
    g.T(L"унос: масса / гребни", 1045, Y1(364), cDim, 12);
    g.T(fW(o.regen) + L" / " + fW(o.radiated), 1290, Y1(364), cGr, 13, 2, 600);
    // the stern thermometer with its zones
    {
        const double tx0 = 1045, tw0 = 245, Tmax = TLOST + 100, ty = Y1(376);
        auto TX = [&](double t0) { return tx0 + tw0 * (std::max)(0.0, (std::min)(1.0, (t0 - 300) / (Tmax - 300))); };
        const struct { double a, b; unsigned c; } zn[5] = {{300, TSAFE, 0x2c7a5a}, {TSAFE, TBOIL, 0x9a8a2a}, {TBOIL, TSOFT, 0xb8691f}, {TSOFT, TBREACH, 0xa8322a}, {TBREACH, Tmax, 0x5a1010}};
        for (const auto& z : zn) g.Fill(TX(z.a), ty, TX(z.b) - TX(z.a), 10, z.c);
        g.Fill(TX(T) - 2, ty - 4, 4, 18, cWh);
        g.T(L"корма " + Fmt(T, 0) + L" К", 1290, Y1(402), Tc, 13, 2, 700);
    }
    // forecast
    g.T(L"ПРОГНОЗ", 1330, Y1(248), cDim, 12, 0, 700);
    {
        const double gl = v.g, podF = v.pods == 2 ? v.podThrust : 0.0, m = (std::max)(1.0, v.mass);
        const double tw = gl > 0 ? (F + podF) / (m * gl) : 0;
        const double prop = mass == pl::kArgon ? v.argon : mass == pl::kIron ? v.iron : p.Fuel();
        const double dvLeft = cv * std::log(m / (std::max)(1.0, m - prop));
        const double net = o.heatIn - o.regen - o.radiated;
        struct Fc { const wchar_t* t; std::wstring v; unsigned c; };
        const Fc fc[6] = {
            {L"тяга/вес", gl > 0 ? Fmt(tw, 2) : L"—", gl > 0 && tw < 1 ? cRd : cTx},
            {L"ускорение", Fmt(F / m / 9.81, 2) + L" g", cTx},
            {L"запас Δv (этой массы)", Fmt(dvLeft / 1e3, 0) + L" км/с", cTx},
            {L"хватит массы на", mdot > 0 ? Fmt(prop / mdot / 60, 1) + L" мин" : L"—", cTx},
            {L"корма до 800 К", net > 0 && T < TSAFE ? Fmt((TSAFE - T) * Cst / net, 0) + L" с" : T >= TSAFE ? L"ВЫШЕ" : L"не дойдёт", T >= TSAFE ? cRd : net > 0 ? cYe : cGr},
            {L"корма до прогара", net > 0 ? Fmt((TLOST - T) * Cst / net, 0) + L" с" : L"не грозит", net > 0 && T > TSAFE ? cRd : cGr}};
        for (int i = 0; i < 6; ++i) { const double y = Y1(268 + i * 18); g.T(fc[i].t, 1330, y, cDim, 12); g.T(fc[i].v, 1570, y, fc[i].c, 13, 2, 600); }
    }
    // risk: the excess of each part and its failure chance per minute
    g.T(L"ПРЕВЫШЕНИЕ · ОТКАЗ ЗА МИН", 1045, Y1(428), cDim, 12, 0, 700);
    static const wchar_t* const kPart[3] = {L"обмотка", L"мощность", L"корма"};
    double lamMax = 0;
    for (int i = 0; i < 3; ++i) {
        const double y = Y1(448 + i * 18), e = o.excess[i], lam = o.riskPerMin[i];
        lamMax = (std::max)(lamMax, lam);
        g.T(kPart[i], 1045, y, cDim, 12);
        g.T(e > 0 ? L"+" + Fmt(e * 100, 0) + L" %" : L"норма", 1190, y, e > 0 ? cYe : cTx, 13, 2, 600);
        g.T(fP((std::min)(1.0, lam)), 1290, y, lam > 0.05 ? cRd : lam > 0.001 ? cYe : cTx, 13, 2, 700);
    }
    // lasting damage
    g.T(L"ПОВРЕЖДЕНИЯ (до станции)", 1330, Y1(428), cDim, 12, 0, 700);
    {
        const struct { const wchar_t* t; int e; } dl[5] = {{L"обмотка", pl::kCoils}, {L"триггер", pl::kDrivers}, {L"рубашка", pl::kJacket}, {L"гребни", pl::kRadiators}, {L"криогеника", pl::kCryo}};
        for (int i = 0; i < 5; ++i) {
            const double y = Y1(448 + i * 16), d = p.Damage(dl[i].e);
            g.T(dl[i].t, 1330, y, cDim, 12);
            g.T(d < 1 ? fP(d) : L"цел", 1570, y, d < 0.75 ? cRd : d < 1 ? cYe : cGr, 12, 2, 600);
        }
    }
    {   // the lamp
        const double danger = (std::max)(lamMax, T > TSAFE ? 0.02 : 0.0);
        const unsigned lc = lost || danger > 0.01 ? cRd : danger > 1e-4 ? cYe : cGr;
        const std::wstring lt = lost ? L"ГИБЕЛЬ" : danger > 0.01 ? L"ОПАСНО" : danger > 1e-4 ? L"ПОВЫШЕННЫЙ" : L"НОРМА";
        const double lx = (std::min)(1500.0, 1576.0 - g.Width(lt, 13, 700) - 20);
        g.Disc(lx, Y1(236), 14, lc);
        g.T(lt, lx + 20, Y1(241), lc, 13, 0, 700);
    }

    // ---- bottom: the thermal map (skin zones and engines), trends, journal, controls ----
    const double bTop = Y2(548), bH = Y2(788) - bTop;
    Frame(g, 15, bTop, 745, bH, L"ТЕРМОКАРТА · ОБШИВКА И ДВИГАТЕЛИ");
    {   // the ship's side, belly-first at the angle of attack in the entry (nose right), the zones on it
        const double sx0 = 40, sy0 = Y2(690), sl = 270, aoaR = env == 3 ? -v.aoa * 0.5 : 0.0;
        const double ca = std::cos(aoaR), sa = std::sin(aoaR), cx = sx0 + sl / 2;
        auto HX = [&](double x) { return -sl / 2 + x / 178 * sl; };
        auto P = [&](double x, double y) { return Pt{cx + x * ca - y * sa, sy0 + x * sa + y * ca}; };
        g.Shape({P(HX(0), -10), P(HX(120), -10), P(HX(178), 0), P(HX(140), 11), P(HX(0), 11)}, 0x33424a, 1.0, 0x5a7080, 1.0);
        g.Shape({P(HX(4), -10), P(HX(48), -10), P(HX(37), -33), P(HX(6), -33)}, 0x3d5058, 1.0);
        g.Shape({P(HX(0), 2), P(HX(24), 2), P(HX(14), 8), P(HX(0), 8)}, 0x3d5058, 1.0);
        if (env == 3 && v.flux[1] > 2e4) {   // the plasma glow on the windward side
            const double a0 = (std::min)(0.8, v.flux[1] / 4e5);
            for (int i = 0; i < 8; ++i) {
                const double y0 = 11 + 29.0 * i / 8, y1 = 11 + 29.0 * (i + 1) / 8, t = (i + 0.5) / 8;
                g.Shape({P(HX(0), y0), P(HX(178), y0), P(HX(178), y1), P(HX(0), y1)}, Mix(Rgb(255, 90, 40), Rgb(255, 140, 60), t), a0 * (1 - t));
            }
        }
        const double zp[7][2] = {{178, 0}, {90, 12}, {10, 6}, {22, -34}, {0, 0}, {70, 14}, {60, 0}};
        for (int i = 0; i < 7; ++i) {
            const Pt q = P(HX(zp[i][0]), zp[i][1]);
            g.Disc(q.x, q.y, 6.75, cBg);
            g.Disc(q.x, q.y, 5.25, Zc(skin[i], v.skinLim[i]));
        }
    }
    if (env == 3) {
        g.T(L"высота " + Fmt(v.alt / 1e3, 1) + L" км · " + Fmt(v.vAir, 0) + L" м/с", 30, Y2(580), cTx, 13, 0, 700);
        g.T(L"напор " + Fmt(v.q / 1e3, 1) + L" кПа · " + Fmt(v.gLoad, 1) + L" g · атака " + Fmt(v.aoa * 180 / kPi, 0) + L"°", 30, Y2(598), cTx, 12);
        g.T(L"вход днищем вперёд", 30, Y2(616), cOr, 12);
    } else g.T(env == 0 ? (v.onGround ? L"на грунте: обшивка в покое" : L"в атмосфере: обшивка в потоке") : L"в космосе: обшивка остывает излучением", 30, Y2(580), cDim, 12);
    // the zones table
    g.T(L"ОБШИВКА", 350, Y2(580), cDim, 12, 0, 700); g.T(L"К / предел", 545, Y2(580), cDim, 11, 2);
    static const wchar_t* const kZone[7] = {L"нос (иридий)", L"днище", L"кромки крыльев", L"перо", L"корма", L"ноги", L"гондолы"};
    for (int i = 0; i < 7; ++i) {
        const double y = Y2(600 + i * 24), lim = v.skinLim[i] > 0 ? v.skinLim[i] : 2000;
        const unsigned c = Zc(skin[i], lim);
        g.Disc(356, y - 4, 5, c);
        g.T(kZone[i], 368, y, cTx, 12);
        g.T(Fmt(skin[i], 0) + L" / " + std::to_wstring(long(lim)), 545, y, c, 13, 2, 700);
        Bar(g, 368, y + 4, 177, 4, skin[i] / lim, c);
    }
    {   // the engines' temperatures
        const double cupWall = run ? T + 250 * thr : T;
        const bool podsOn = v.pods == 2;
        const double noseRetro = 290 + (std::max)(0.0, skin[0] - 290) * 0.35, sternAna = (std::max)(290.0, skin[4] * 0.9);
        const struct { const wchar_t* t; double v, lim; } eng[6] = {
            {L"чаша маршевая, стенка", cupWall, 2000}, {L"рубашка, выход", mass == pl::kProducts ? 0 : jOut, 1100}, {L"обмотка", coilT, 26},
            {L"гондолы 1–4, чаши", podsOn ? 600 + 500 * v.podLevel : (std::max)(290.0, skin[6]), 1300}, {L"камеры анамезона К1–К4", sternAna, 2000},
            {L"ретро-чаши носа Н1–Н2", noseRetro, 2000}};
        g.T(L"ДВИГАТЕЛИ", 565, Y2(580), cDim, 12, 0, 700);
        for (int i = 0; i < 6; ++i) {
            const double y = Y2(600 + i * 28);
            const bool ok = eng[i].v > 0;
            g.T(eng[i].t, 565, y, cTx, 12);
            g.T(ok ? Fmt(eng[i].v, eng[i].lim < 100 ? 1 : 0) + L" К" : L"—", 750, y, ok ? Zc(eng[i].v, eng[i].lim) : cDim, 13, 2, 700);
            if (ok) Bar(g, 565, y + 5, 185, 4, eng[i].v / eng[i].lim, Zc(eng[i].v, eng[i].lim));
        }
    }
    // trends and journal (narrow)
    {
        const double tTop = bTop, tH = Y2(660) - bTop;
        Frame(g, 775, tTop, 235, tH, L"ТРЕНДЫ · 60 С");
        const auto& tr = p.Trend();
        const double t1 = p.Clock(), x0 = 782, x1 = 1003, y0 = Y2(566), y1 = Y2(652);
        auto lineT = [&](int ch, unsigned col, double mx) {
            std::vector<Pt> pts;
            for (const pl::TrendPt& q : tr) {
                const double f = ch == 0 ? q.F : ch == 1 ? q.B : ch == 2 ? q.T - 300 : q.nose;
                pts.push_back({x1 - (t1 - q.t) / 60 * (x1 - x0), y1 - (std::min)(1.0, f / mx) * (y1 - y0)});
            }
            g.Polyline(pts, col, 1.4);
        };
        double fmx = 1.8e9;
        for (const pl::TrendPt& q : tr) fmx = (std::max)(fmx, q.F);
        lineT(0, cWh, fmx); lineT(1, cPipeC, Plant::BRupture()); lineT(2, cPipeH, TLOST - 300); lineT(3, cRd, 2400);
        g.T(L"тяга", 784, Y2(578), cWh, 10); g.T(L"поле", 816, Y2(578), cPipeC, 10); g.T(L"корма", 846, Y2(578), cPipeH, 10); g.T(L"нос", 884, Y2(578), cRd, 10);
        const double jTop = Y2(672), jH = Y2(788) - jTop;
        Frame(g, 775, jTop, 235, jH, L"ЖУРНАЛ");
        double y = jTop + 24;
        for (const pl::LogLine& a : p.Journal()) {
            if (y > jTop + jH - 6) break;
            const unsigned c = a.level >= 2 ? cRd : a.level == 0 ? cGr : cYe;
            g.T(g.Fit(Clock(a.t) + L" " + W1251(a.ru), 1003 - 782, 11, 500), 782, y, c, 11);
            y += 18;
        }
    }
    // controls
    {
        Frame(g, 1025, bTop, 560, bH, L"УПРАВЛЕНИЕ");
        const double bh = std::round(36 * (std::min)(k2, 1.25));
        const double r0 = Y2(566), r1 = Y2(612), r2 = Y2(658), r3 = Y2(704);
        btn(stage == pl::kStOff || stage == pl::kStQuench ? L"ПУСК" : L"СТОП", 1040, r0, 120, bh, stage != pl::kStOff, kCmdStart, stage == pl::kStOff ? cGr : cRd);
        static const wchar_t* const kMassK[4] = {L"АВТО", L"АРГОН", L"ЖЕЛЕЗО", L"ПРОДУКТЫ"};
        for (int i = 0; i < 4; ++i) btn(kMassK[i], 1170 + i * 103, r0, 96, bh, p.MassMode() == i - 1, kCmdMassAuto + i, cOr);
        btn(L"ПОЛЕ −", 1040, r1, 100, bh, false, kCmdFieldDown, cOr);
        btn(L"ПОЛЕ +", 1150, r1, 100, bh, false, kCmdFieldUp, cOr);
        const bool lim = p.Limiter(), armed = p.LimiterArmed(v.sysNow);
        btn(lim ? (armed ? L"ПОДТВЕРДИТЬ" : L"ОГРАНИЧИТЕЛЬ") : L"ОГР. СНЯТ", 1260, r1, 100, bh, !lim || armed, kCmdLimiter, lim ? (armed ? cRd : cGr) : cRd);
        if (lim && p.Clock() - p.HeldAt() < 2.5 && std::fmod(flow_, 0.5) < 0.3) g.Stroke(1257, r1 - 3, 106, bh + 6, cYe, 2);   // a ± held by it
        g.T(L"уставка " + Fmt(p.FieldSet(), 1) + L" Тл", 1570, r1 + bh / 2 + 6, cTx, 14, 2, 700);
        btn(L"МОЩН −", 1040, r2, 100, bh, false, kCmdPowerDown, cOr);
        btn(L"МОЩН +", 1150, r2, 100, bh, false, kCmdPowerUp, cOr);
        g.T(lim ? L"АВТОМАТ" : L"РУЧНОЙ", 1310, r2 + bh / 2 + 6, lim ? cGr : cRd, 15, 1, 700);
        g.T(L"мощность " + Fmt(p.PowerPct(), 0) + L" % ном.", 1570, r2 + bh / 2 + 6, p.PowerPct() > 100 ? cYe : cTx, 14, 2, 700);
        btn(L"ГОНДОЛЫ", 1040, r3, 100, bh, v.pods > 0, kCmdPods, cOr);
        static const wchar_t* const kEnvK[4] = {L"ГРУНТ", L"КОСМОС", L"ПЕРЕЛЁТ", L"ВХОД"};
        for (int i = 0; i < 4; ++i) btn(kEnvK[i], 1150 + i * 80, r3, 74, bh, env == i, -1, cEnv);   // lamps: the computer's own reading
        const double yb = Y2(758);
        g.T(v.thrOwn ? L"ТЯГА (рычаг)" : L"ТЯГА (анамезон)", 1040, yb + 13, cDim, 12);
        Bar(g, 1140, yb, 430, 16, v.thrOwn ? v.thr : 0.0, 0x3fa58a);
        if (v.thrOwn) hits_.push_back({1140, yb, 430, 16, kCmdThrottle});
    }
    if (lost) {
        g.Fill(15, 62, 995, mH, Rgb(90, 10, 6), 0.55);
        g.T(stage == pl::kStGone ? L"КОРМА ПРОГОРЕЛА" : L"КОРПУС РАЗРУШЕН", 512, Y1(270), 0xffd0c8, 46, 1, 800);
        g.T(L"КОРАБЛЬ ПОТЕРЯН", 512, Y1(330), 0xffd0c8, 30, 1, 700);
        g.T(stage == pl::kStGone ? L"Корма прогорела (" + std::to_wstring(long(TLOST)) + L" К)" : L"разрушение корпуса", 512, Y1(370), 0xffb0a0, 16, 1);
        g.T(L"ремонт — только на станции", 512, Y1(400), 0xffb0a0, 14, 1);
    }
}

}  // namespace tantra::plantscreen

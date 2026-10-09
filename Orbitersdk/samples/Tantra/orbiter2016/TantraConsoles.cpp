// TantraDisplays - the commander's side consoles (variant 7 of the bridge mockup: Tantra_Design/bridge_variants/v7.js, the user's
// choice, 2026-10-04): glass keys lit from inside in the helmet's blue photonics, GOST 2.304 lettering; they serve watching the
// main systems and reaching the screens from the seat - the screens keep their own touch.
//   screen 3, the right console's top (0.252 x 0.51 m): 4 x 10 keys - МФД 1..6 choose the MFD the keys drive (the chosen one is
//     framed in amber on its glass), ПИТ. / ВЫБ. / МЕНЮ and Л1..Л6 / П1..П6 its buttons (the MFD's own label shown on each), and
//     the computing machine's 0..9 , = + - × ÷ C ЗАП ВЫЗ;
//   screen 6, the machine's phosphor screen at the right console's front (the register, the operation, the journal, the cells);
//   screen 4, the left console's top (0.29 x 0.54 m): 3 x 5 keys of the screens (the left glass's page, the front glass's tab,
//     the ship's HUD, the side glasses), the throttle quadrant's two slots (МАРШ, ВЫДВ. БЛОКИ: where the thrust is set - a touch on
//     a slot sets it, as the bars of the screens do) and the pods' ВЫПУСК / УБОРКА key under its red guard cover.
// The layout is the mockup's in metres (the keys 56 x 42 mm at 64 x 52 mm), drawn at the console's own scale.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "TantraScreenCanvas.h"
#include "MeshLayout.h"
#include "InteriorLayout.h"
#include "../core/Spec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace {
using namespace tantra::scr;

// the helmet's blue photonics (the mockup's palette B)
constexpr unsigned kMain = 0xb8ecff, kDimB = 0x6f9fb8, kAcc = 0xffc46a, kRed = 0xff3b30, kWhite = 0xe6f6ff, kPlate = 0x061014, kGlassOff = 0x051017;
constexpr unsigned kPhos = 0x4dff88, kPhosD = 0x2fbf62, kPhosBg = 0x020803;   // the machine's P1 phosphor

enum Kind { kMfdSel, kMfdBtn, kCalc, kLMode, kFTab, kHud, kGlass, kRibbon, kOpq, kBand, kSideG };
struct CKey { const wchar_t* lab; int kind, arg; unsigned col; };

// the right console: rows from the far edge (the first row the farthest), 4 columns from the inboard side
const CKey kKeysR[40] = {
    {L"МФД 1", kMfdSel, 0, kMain}, {L"МФД 2", kMfdSel, 1, kMain}, {L"МФД 3", kMfdSel, 2, kMain}, {L"ПИТ.", kMfdBtn, 12, kMain},
    {L"МФД 4", kMfdSel, 3, kMain}, {L"МФД 5", kMfdSel, 4, kMain}, {L"МФД 6", kMfdSel, 5, kMain}, {L"ВЫБ.", kMfdBtn, 13, kMain},
    {L"Л1", kMfdBtn, 0, kMain}, {L"Л2", kMfdBtn, 1, kMain}, {L"Л3", kMfdBtn, 2, kMain}, {L"МЕНЮ", kMfdBtn, 14, kMain},
    {L"Л4", kMfdBtn, 3, kMain}, {L"Л5", kMfdBtn, 4, kMain}, {L"Л6", kMfdBtn, 5, kMain}, {L"ВЫЗ", kCalc, 0, kMain},
    {L"П1", kMfdBtn, 6, kMain}, {L"П2", kMfdBtn, 7, kMain}, {L"П3", kMfdBtn, 8, kMain}, {L"ЗАП", kCalc, 0, kMain},
    {L"П4", kMfdBtn, 9, kMain}, {L"П5", kMfdBtn, 10, kMain}, {L"П6", kMfdBtn, 11, kMain}, {L"C", kCalc, 0, kRed},
    {L"7", kCalc, 0, kMain}, {L"8", kCalc, 0, kMain}, {L"9", kCalc, 0, kMain}, {L"÷", kCalc, 0, kMain},
    {L"4", kCalc, 0, kMain}, {L"5", kCalc, 0, kMain}, {L"6", kCalc, 0, kMain}, {L"×", kCalc, 0, kMain},
    {L"1", kCalc, 0, kMain}, {L"2", kCalc, 0, kMain}, {L"3", kCalc, 0, kMain}, {L"-", kCalc, 0, kMain},
    {L"0", kCalc, 0, kMain}, {L",", kCalc, 0, kMain}, {L"=", kCalc, 0, kAcc}, {L"+", kCalc, 0, kMain}};
// the left console (the user, 2026-10-05: nearer him, compact, the new screen keys): 5 rows from the levers toward him, 4 keys
// a row from the outboard side, the main screen's bands 5 narrower ones (kRowL); '\n' splits a label into two lines; nullptr: none
constexpr int kKeysLN = 21;
constexpr int kRowL[5] = {4, 4, 4, 5, 4};
const CKey kKeysL[kKeysLN] = {
    {nullptr, 0, 0, kMain}, {L"ПОЛЁТ", kFTab, 0, kMain}, {L"ДВИГА-\nТЕЛИ", kFTab, 1, kMain}, {L"ПАРА-\nМЕТРЫ", kFTab, 2, kMain},
    {nullptr, 0, 0, kMain}, {L"МЕХАНИ-\nЗАЦИЯ", kLMode, 0, kMain}, {L"ТЕПЛО", kLMode, 1, kMain}, {L"АВТО-\nПИЛОТ", kLMode, 2, kMain},
    {L"HUD\nГОРИЗ.", kHud, HUD_SURFACE, kMain}, {L"HUD\nОРБИТА", kHud, HUD_ORBIT, kMain}, {L"HUD\nСТЫК.", kHud, HUD_DOCKING, kMain}, {L"HUD\nВЫКЛ", kHud, HUD_NONE, kMain},
    {L"ЭКРАН\nОПТ.", kBand, shipview::ViewScreen::kBandOptical, kMain}, {L"ЭКРАН\nИК", kBand, shipview::ViewScreen::kBandIR, kMain},
    {L"ЭКРАН\nУФ", kBand, shipview::ViewScreen::kBandUV, kMain}, {L"ЭКРАН\nДАННЫЕ", kBand, shipview::ViewScreen::kBandData, kMain},
    {L"ЭКРАН\nВЫКЛ", kBand, shipview::ViewScreen::kBandOff, kMain},
    {L"СТЕКЛО Л", kSideG, 0, kMain}, {L"СТЕКЛО П", kSideG, 1, kMain}, {L"НЕПРОЗР.\n-", kOpq, -1, kMain}, {L"НЕПРОЗР.\n+", kOpq, 1, kMain}};

// the consoles' pixels per metre (the fields' depths: 0.51 and 0.54 m - their textures as high as the screens' kH)
double ScaleR(int h) { return h / 0.51; }
double ScaleL(int h) { return h / 0.54; }
struct Rc { double x, y, w, h; };
Rc KeyR(int i, double S) { const int c = i % 4, r = i / 4; return {(0.028 + c * 0.064 - 0.028) * S, (0.021 + r * 0.052 - 0.021) * S, 0.056 * S, 0.042 * S}; }
Rc KeyL(int i, double S) {                                               // 4 keys a row 5.6 cm (6.4 cm apart), 5 keys 5.0 cm (5.7 cm apart)
    int r = 0; while (r < 4 && i >= kRowL[r]) i -= kRowL[r++];
    const double w = kRowL[r] > 4 ? 0.050 : 0.056, pitch = kRowL[r] > 4 ? 0.057 : 0.064, x0 = kRowL[r] > 4 ? 0.006 : 0.018;
    return {(x0 + i * pitch) * S, (0.27 + r * 0.052) * S, w * S, 0.042 * S};
}
Rc SlotL(int k, double S) { const double cx = k == 0 ? 0.19 : 0.105; return {(cx - 0.025) * S, 0.03 * S, 0.05 * S, 0.20 * S}; }   // 0 МАРШ, 1 ВЫДВ. БЛОКИ
Rc PodKey(double S) { return {(0.04 - 0.023) * S, (0.118 - 0.018) * S, 0.046 * S, 0.036 * S}; }   // outboard of the levers (gen_mesh BR_POD_KEY)
enum { kHitSlot = 100, kHitPods = 110 };

// a glass key lit from inside (the mockup's paintGlass): off - a dark glass with a faint glow of its colour, a lit edge, the label
// in its colour; on - the glass full of light (white at the top), a white edge and the helmet's double contour, the label dark;
// its light spills on the plate round it
void GlassKey(Canvas& g, tantra::GostFont& gf, const Rc& r, const std::wstring& lab, bool on, unsigned lc, const std::wstring& sub = L"") {
    if (on) for (int i = 4; i >= 1; --i) g.Fill(r.x - i * 3, r.y - i * 3, r.w + i * 6, r.h + i * 6, Mix(lc, kPlate, 0.10 + 0.07 * (4 - i)));
    if (on) {
        g.Fill(r.x, r.y, r.w, r.h, Mix(lc, kGlassOff, 0.84));
        g.Fill(r.x, r.y, r.w, r.h * 0.42, Mix(kWhite, lc, 0.55));
        g.Stroke(r.x + 4, r.y + 4, r.w - 8, r.h - 8, kWhite, 5);
        g.Stroke(r.x + 13, r.y + 13, r.w - 26, r.h - 26, Mix(0x03141c, lc, 0.5), 2);
    } else {
        g.Fill(r.x, r.y, r.w, r.h, kGlassOff);
        g.Fill(r.x + 6, r.y + 6, r.w - 12, r.h - 12, Mix(lc, kGlassOff, 0.08));
        g.Fill(r.x + r.w * 0.18, r.y + r.h * 0.2, r.w * 0.64, r.h * 0.55, Mix(lc, kGlassOff, 0.15));
        g.Stroke(r.x + 4, r.y + 4, r.w - 8, r.h - 8, Mix(lc, kGlassOff, 0.5), 3);
    }
    const unsigned fg = on ? 0x03141c : lc;
    if (lab == L"×" || lab == L"÷") {                                     // drawn: the GOST font has Ч / ч in those places (cp1251)
        const double cx = r.x + r.w / 2, cy = r.y + r.h / 2, a = (std::min)(r.w, r.h) * 0.18, lw = (std::max)(2.0, a * 0.22);
        if (lab == L"×") { g.Line(cx - a, cy - a, cx + a, cy + a, fg, lw); g.Line(cx - a, cy + a, cx + a, cy - a, fg, lw); }
        else { g.Line(cx - a * 1.2, cy, cx + a * 1.2, cy, fg, lw); g.Disc(cx, cy - a * 0.8, lw * 0.9, fg); g.Disc(cx, cy + a * 0.8, lw * 0.9, fg); }
        return;
    }
    std::wstring l1 = lab, l2;
    const size_t nl = lab.find(L'\n');
    if (nl != std::wstring::npos) { l1 = lab.substr(0, nl); l2 = lab.substr(nl + 1); }
    const double room = r.w - 18, lines = l2.empty() ? 1.0 : 2.0, top = sub.empty() ? 0.0 : r.h * 0.22;
    double em = (std::min)((r.h - top) * (lines > 1 ? 0.30 : 0.42), room / (std::max)(tantra::GostFont::Width(l1, 1.0), tantra::GostFont::Width(l2, 1.0)));
    em = (std::max)(9.0, em);
    oapi::Sketchpad* skp = g.Skp();
    const double cx = r.x + r.w / 2, cy = r.y + (r.h - top) / 2;
    if (l2.empty()) gf.Text(skp, int(cx), int(cy + em * 0.36), l1, int(em), fg, 1);
    else { gf.Text(skp, int(cx), int(cy - em * 0.18), l1, int(em), fg, 1); gf.Text(skp, int(cx), int(cy + em * 0.92), l2, int(em), fg, 1); }
    if (!sub.empty()) {                                                   // the MFD's own label of this button
        const double es = (std::min)(r.h * 0.2, (r.w - 12) / (std::max)(1.0, tantra::GostFont::Width(sub, 1.0)));
        gf.Text(skp, int(cx), int(r.y + r.h - 9), sub, int((std::max)(8.0, es)), on ? 0x03141c : kDimB, 1);
    }
}

std::wstring MachNum(double v) {                                          // the register: up to 12 places, a decimal comma
    if (!std::isfinite(v)) return L"ОШИБКА";
    wchar_t b[48];
    std::swprintf(b, 48, L"%.10g", v);
    std::wstring s = b;
    if (s.size() > 12) { std::swprintf(b, 48, L"%.5e", v); s = b; }
    for (wchar_t& c : s) if (c == L'.') c = L',';
    return s;
}
double MachVal(const std::wstring& s) {
    std::wstring t = s;
    for (wchar_t& c : t) if (c == L',') c = L'.';
    return std::wcstod(t.c_str(), nullptr);
}
}  // namespace

void TantraDisplays::PressMfd(int i, int b) {
    if (i < 0 || i >= 6) return;
    t_->interior_.MfdPress(i, b);
    if (b == 14) mfdMenu_[i] = !mfdMenu_[i];                              // МНУ: the button menu on / off
    else if (b == 12 || b == 13) mfdMenu_[i] = false;                     // off, the mode list: no menu
}

// the pods' thrust set by their lever (Tantra::PodVectoring drives the cups; nothing while they are in the bays)
double TantraDisplays::PodLevel() const { return t_->podCmd_; }

// ---- the right console ----------------------------------------------------------------------------------------------------
void TantraDisplays::DrawConsoleR() {
    SURFHANDLE s = s_[kRiserR];
    if (!s) return;
    int W = 0, H = 0; oapiGetSurfaceSize(s, &W, &H);
    const double S = ScaleR(H), now = oapiGetSysTime();
    oapiClearSurface(s, 0xFF000000 | ((kPlate & 0xFF) << 16) | (kPlate & 0xFF00) | ((kPlate >> 16) & 0xFF));
    std::vector<Hit>& hits = consHits_[0];
    hits.clear();
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, W, H, kPlate);
            const Rc a = KeyR(0, S), z = KeyR(39, S);
            g.Line(a.x, KeyR(23, S).y + KeyR(23, S).h + 0.005 * S, z.x + z.w, KeyR(23, S).y + KeyR(23, S).h + 0.005 * S, Mix(kDimB, kPlate, 0.45), 2);   // the MFDs' keys | the machine's
            for (int i = 0; i < 40; ++i) {
                const CKey& k = kKeysR[i];
                const Rc r = KeyR(i, S);
                bool on = keyLit_[0][i] > now;
                std::wstring sub;
                if (k.kind == kMfdSel) on = selMfd_ == k.arg;
                if (k.kind == kMfdBtn && k.arg < 12) sub = W1251(t_->interior_.MfdLabel(selMfd_, k.arg));
                GlassKey(g, gost_, r, k.lab, on, k.col, sub);
                hits.push_back({r.x, r.y, r.w, r.h, i});
            }
        }
        oapiReleaseSketchpad(skp);
    }
}

bool TantraDisplays::TouchConsoleR(double x, double y) {
    const int i = HitTest(consHits_[0], x, y, nullptr, 3.0);
    if (i < 0 || i >= 40) return false;
    const CKey& k = kKeysR[i];
    keyLit_[0][i] = oapiGetSysTime() + 0.25;
    switch (k.kind) {
        case kMfdSel: selMfd_ = k.arg; break;
        case kMfdBtn: PressMfd(selMfd_, k.arg); break;
        default: MachineKey(k.lab); break;
    }
    return true;
}

// ---- the computing machine: its keys and its phosphor screen --------------------------------------------------------------
void TantraDisplays::MachineKey(const std::wstring& L) {
    Machine& m = mach_;
    auto say = [&](const std::wstring& s) { m.log.insert(m.log.begin(), s); if (m.log.size() > 5) m.log.pop_back(); };
    const bool recall = L == L"ВЫЗ";
    if (m.err && L != L"C") return;                                       // after an error only C
    if (L.size() == 1 && L[0] >= L'0' && L[0] <= L'9') {
        if (m.fresh || m.disp == L"0") m.disp = L; else if (m.disp.size() < 12) m.disp += L;
        m.fresh = false;
    } else if (L == L",") {
        if (m.fresh) { m.disp = L"0,"; m.fresh = false; }
        else if (m.disp.find(L',') == std::wstring::npos && m.disp.size() < 11) m.disp += L",";
    } else if (L == L"C") {
        m.disp = L"0"; m.acc = 0.0; m.op = 0; m.fresh = true; m.err = false;
    } else if (L == L"+" || L == L"-" || L == L"×" || L == L"÷" || L == L"=") {
        const double v = MachVal(m.disp);
        if (m.op && !m.fresh) {
            if (m.op == L'÷' && v == 0.0) { m.disp = L"ДЕЛ. НА 0"; m.err = true; m.op = 0; say(L"ошибка: деление на 0"); m.lastRecall = false; return; }
            const double r = m.op == L'+' ? m.acc + v : m.op == L'-' ? m.acc - v : m.op == L'×' ? m.acc * v : m.acc / v;
            m.disp = MachNum(r);
        }
        m.acc = MachVal(m.disp); m.op = L == L"=" ? 0 : L[0]; m.fresh = true;
        if (L == L"=") say(L"= " + m.disp);
    } else if (L == L"ЗАП") {                                             // into the first free cell; all full: the oldest goes
        int i = 0; while (i < 8 && !m.cells[i].empty()) ++i;
        if (i == 8) { for (int j = 0; j < 7; ++j) m.cells[j] = m.cells[j + 1]; i = 7; }
        m.cells[i] = m.disp; m.recall = i; m.fresh = true;
        wchar_t b[16]; std::swprintf(b, 16, L"В ЯЧ %d: ", i + 1); say(b + m.disp);
    } else if (recall) {                                                  // the last stored; pressed again - the one before, round
        int i = m.lastRecall ? m.recall - 1 : 7, n = 0;
        for (; n < 8; ++n, --i) { if (i < 0) i = 7; if (!m.cells[i].empty()) break; }
        if (n < 8) { m.recall = i; m.disp = m.cells[i]; m.fresh = true; wchar_t b[16]; std::swprintf(b, 16, L"ИЗ ЯЧ %d: ", i + 1); say(b + m.disp); }
    }
    m.lastRecall = recall;
}

void TantraDisplays::DrawMachine() {
    SURFHANDLE s = s_[kWingL];
    if (!s) return;
    const Machine& m = mach_;
    int Wi = 0, Hi = 0; oapiGetSurfaceSize(s, &Wi, &Hi);
    const double W = Wi, H = Hi, k = H / 380.0;                           // the mockup's screen: 420 x 380
    oapiClearSurface(s, 0xFF000000 | 0x030802);
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, W, H, kPhosBg);
            gost_.Text(skp, int(24 * k), int(36 * k), L"РАСЧЁТНАЯ МАШИНА", int(24 * k), kPhosD, 0);
            const double em = (std::min)(78 * k, (W - 110 * k) / (std::max)(1.0, tantra::GostFont::Width(m.disp, 1.0)));
            gost_.Text(skp, int(W - 24 * k), int(120 * k), m.disp, int(em), m.err ? 0xff6a4a : kPhos, 2);
            if (m.op == L'×' || m.op == L'÷') {                           // drawn (the font's Ч / ч)
                const double cx = 48 * k, cy = 102 * k, a = 14 * k, lw = 4 * k;
                if (m.op == L'×') { g.Line(cx - a, cy - a, cx + a, cy + a, kPhos, lw); g.Line(cx - a, cy + a, cx + a, cy - a, kPhos, lw); }
                else { g.Line(cx - a * 1.2, cy, cx + a * 1.2, cy, kPhos, lw); g.Disc(cx, cy - a, lw, kPhos); g.Disc(cx, cy + a, lw, kPhos); }
            } else if (m.op) gost_.Text(skp, int(30 * k), int(120 * k), std::wstring(1, m.op), int(54 * k), kPhos, 0);
            for (size_t i = 0; i < m.log.size() && i < 2; ++i) gost_.Text(skp, int(24 * k), int((215 + i * 44) * k), m.log[i], int(30 * k), i ? kPhosD : kPhos, 0);
            for (int i = 0; i < 8; ++i) {                                 // the cells: lit when they hold a number, the recalled one bright
                const double x = (24 + i * 48) * k, y = H - 64 * k;
                g.Fill(x, y, 38 * k, 22 * k, m.cells[i].empty() ? 0x0c2414 : i == m.recall ? kPhos : kPhosD);
                wchar_t b[4]; std::swprintf(b, 4, L"%d", i + 1);
                gost_.Text(skp, int(x + 19 * k), int(H - 22 * k), b, int(20 * k), kPhosD, 1);
            }
        }
        oapiReleaseSketchpad(skp);
    }
}

// ---- the left console -----------------------------------------------------------------------------------------------------
void TantraDisplays::DrawConsoleL() {
    SURFHANDLE s = s_[kRiserL];
    if (!s) return;
    Tantra* t = t_;
    int W = 0, H = 0; oapiGetSurfaceSize(s, &W, &H);
    const double S = ScaleL(H), now = oapiGetSysTime();
    oapiClearSurface(s, 0xFF000000 | ((kPlate & 0xFF) << 16) | (kPlate & 0xFF00) | ((kPlate >> 16) & 0xFF));
    std::vector<Hit>& hits = consHits_[1];
    hits.clear();
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, W, H, kPlate);
            static const wchar_t* const kSideSt[3] = {L"РАБОТА", L"ЛЕНТА", L"ВНИЗ"};
            for (int i = 0; i < kKeysLN; ++i) {
                const CKey& k = kKeysL[i];
                if (!k.lab) continue;
                const Rc r = KeyL(i, S);
                bool on = keyLit_[1][i] > now;
                if (k.kind == kLMode) on = on || leftTab_ == k.arg;
                if (k.kind == kFTab) on = frontTab_ == k.arg;
                if (k.kind == kHud) on = hudMode_ == k.arg;
                if (k.kind == kBand) on = t->screen_.Band() == k.arg;
                if (k.kind == kSideG) {                                       // a side glass: РАБОТА -> ЛЕНТА -> ВНИЗ, its state under the name
                    const int st = (std::max)(0, (std::min)(2, t->interior_.SideFoldState(k.arg)));
                    GlassKey(g, gost_, r, k.lab, st < 2, k.col, kSideSt[st]);
                } else GlassKey(g, gost_, r, k.lab, on, k.col);
                hits.push_back({r.x, r.y, r.w, r.h, i});
            }
            // the throttle quadrant's slots: the scale (0 at the back, МАКС at the front), the thrust set; the pods' locked in the bays
            const double lv[2] = {t->GetThrusterGroupLevel(THGROUP_MAIN), PodLevel()};
            const bool locked[2] = {false, t->podOut_ < 0.99};
            static const wchar_t* const kSlot[2] = {L"МАРШ", L"ВЫДВ. БЛОКИ"};
            for (int j = 0; j < 2; ++j) {
                const Rc r = SlotL(j, S);
                g.Fill(r.x, r.y, r.w, r.h, 0x040a0d); g.Stroke(r.x, r.y, r.w, r.h, Mix(kDimB, kPlate, 0.4), 1.5);
                const double sx = r.x + r.w * 0.41, sw = r.w * 0.19, y0 = r.y + r.h * 0.04, y1 = r.y + r.h * 0.96;
                g.Fill(sx, y0, sw, y1 - y0, 0x000000);
                for (int i = 0; i <= 10; ++i) { const double y = y1 - (y1 - y0) * i / 10; g.Line(r.x + 6, y, r.x + (i % 5 ? r.w * 0.28 : r.w * 0.34), y, i % 5 ? kDimB : kMain, i % 5 ? 1.5 : 3); }
                const double f = (std::max)(0.0, (std::min)(1.0, lv[j])), yf = y1 - (y1 - y0) * f;
                g.Fill(sx, yf, sw, y1 - yf, Mix(kMain, 0x000000, 0.55));
                g.Line(sx - 6, yf, sx + sw + 6, yf, kWhite, 3);
                gost_.Text(skp, int(r.x + r.w - 4), int(y1 - 2), L"0", int(0.012 * S), kDimB, 2);
                gost_.Text(skp, int(r.x + r.w - 2), int(y0 + 0.012 * S), L"МАКС", int(0.008 * S), kAcc, 2);
                if (locked[j]) { g.Fill(sx - 4, y1 - 0.012 * S, sw + 8, 0.004 * S, kRed); gost_.Text(skp, int(r.x + r.w - 2), int(y1 - 0.03 * S), L"ЗАП", int(0.009 * S), kRed, 2); }
                gost_.Text(skp, int(r.x + r.w / 2), int(r.y + r.h + 0.016 * S), kSlot[j], int(0.011 * S), kDimB, 1);
                wchar_t b[16]; std::swprintf(b, 16, L"%.0f %%", f * 100);
                gost_.Text(skp, int(r.x + r.w / 2), int(r.y + r.h + 0.031 * S), b, int(0.011 * S), kMain, 1);
                hits.push_back({r.x, r.y, r.w, r.h, kHitSlot + j, true});
            }
            // the pods' key under its red guard cover (a touch opens the cover; it falls shut after 6 s)
            const Rc pk = PodKey(S);
            const bool open = now - podCoverT_ < 6.0;
            const wchar_t* st = t->podsWanted_ ? (t->podOut_ >= 0.99 ? L"ВЫП." : L"ВЫХОД") : (t->podOut_ <= 0.01 ? L"УБР." : L"УБОРКА");
            GlassKey(g, gost_, pk, L"ВЫДВ. БЛОКИ", t->podOut_ > 0.01, kMain, st);
            (void)open;                                                   // (its red guard cover: a mesh over the key, TantraYoke.cpp)
            hits.push_back({pk.x - 4, pk.y - 0.014 * S, pk.w + 8, pk.h + 0.018 * S, kHitPods});
        }
        oapiReleaseSketchpad(skp);
    }
}

bool TantraDisplays::TouchConsoleL(double x, double y) {
    Tantra* t = t_;
    double along = 0.0;
    const int i = HitTest(consHits_[1], x, y, &along, 3.0);
    if (i < 0) return false;
    const double now = oapiGetSysTime();
    if (i == kHitSlot) { SetMainLevel(along); return true; }
    if (i == kHitSlot + 1) {
        if (t->podOut_ < 0.99) { t->Message("Выдвижные блоки в отсеках: рукоять заперта", "The pods are in their bays: the lever is locked"); return true; }
        SetPodLevel(along); return true;
    }
    if (i == kHitPods) {   // (2026-10-09) a plain key, no guard cover; the folded wings do not hold the pods in (UpdatePods)
        if (t->podsWanted_) {                                             // stowing: only with their thrust at 0
            if (PodLevel() > 0.001) { t->Message("Уборка выдвижных блоков: сначала их тяга 0", "Stowing the pods: their thrust to 0 first"); return true; }
            t->podsWanted_ = false; t->podTarget_ = 0.0; t->Message("Выдвижные блоки: чаши в 0°, в отсеки", "Pods: cups aft, into the bays");
        } else t->ActPods(false);
        return true;
    }
    if (i >= kKeysLN || !kKeysL[i].lab) return false;
    const CKey& k = kKeysL[i];
    keyLit_[1][i] = now + 0.25;
    switch (k.kind) {
        case kLMode:
            if (k.arg == 0) leftTab_ = 0;
            else if (k.arg == 1) leftTab_ = 1;
            else leftTab_ = 2;
            break;
        case kFTab:
            frontTab_ = k.arg;
            break;
        case kHud: hudMode_ = k.arg; break;
        case kSideG: t->interior_.SideFold(k.arg, (t->interior_.SideFoldState(k.arg) + 1) % 3); break;   // РАБОТА -> ЛЕНТА -> ВНИЗ
        case kBand: t->screen_.SetBand(k.arg); break;                     // the main screen's band (TantraScreen)
        case kOpq: opq_ = (std::max)(0.3, (std::min)(1.0, opq_ + 0.1 * k.arg)); ApplyOpacity(); break;
        default: break;
    }
    return true;
}

// ---- the yoke's hub (screen 5, 2000 px/m): its face 0.26 x 0.088 m (520 x 176 px) with the orientation keys, 2 x 5 of 46 x 30 mm,
// and its top screen 0.20 x 0.05 m (400 x 100 px under the face in the texture): the speed, the altitude, the g, УВТ --------------
namespace {
struct HubKey { const wchar_t* lab; int mode; unsigned col; };   // mode: an Orbiter autopilot; -1 РУЧН. (all off); -2 / -3 ours: radial out / in
const HubKey kHubKey[10] = {{L"ПРОГРАД", NAVMODE_PROGRADE, kMain}, {L"РЕТРО-\nГРАД", NAVMODE_RETROGRADE, kMain}, {L"НОРМ. +", NAVMODE_NORMAL, kMain},
                            {L"НОРМ. -", NAVMODE_ANTINORMAL, kMain}, {L"ВЫСОТА", NAVMODE_HOLDALT, kMain}, {L"РАД. +", -2, kMain}, {L"РАД. -", -3, kMain},
                            {L"ГОРИ-\nЗОНТ", NAVMODE_HLEVEL, kMain}, {L"СТОП\nВРАЩ.", NAVMODE_KILLROT, kMain}, {L"РУЧН.", -1, kAcc}};
Rc HubKeyR(int i) { const int c = i % 5, r = i / 5; return {52.0 + c * 104.0 - 46.0, 52.0 + r * 72.0 - 30.0, 92.0, 60.0}; }
}  // namespace

void TantraDisplays::DrawHub() {
    SURFHANDLE s = s_[kKeys];
    if (!s) return;
    Tantra* t = t_;
    int W = 0, H = 0; oapiGetSurfaceSize(s, &W, &H);
    const double now = oapiGetSysTime(), faceH = 176;
    oapiClearSurface(s, 0xFF000000);
    hubHits_.clear();
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, W, faceH, 0x0b1215);                                 // the face
            bool any = radMode_ != 0;
            for (int i = 0; i < 10; ++i) if (kHubKey[i].mode >= 0 && t->GetNavmodeState(kHubKey[i].mode)) any = true;
            for (int i = 0; i < 10; ++i) {
                const HubKey& k = kHubKey[i];
                const bool on = hubLit_[i] > now || (k.mode >= 0 ? t->GetNavmodeState(k.mode) : k.mode == -1 ? !any : radMode_ == (k.mode == -2 ? 1 : -1));
                const Rc r = HubKeyR(i);
                GlassKey(g, gost_, r, k.lab, on, k.col);
                hubHits_.push_back({r.x, r.y, r.w, r.h, i});
            }
            // the screen: the speed, the altitude, the g | УВТ (on, waiting for the pods, off)
            const double y0 = faceH, x0 = (W - 400) / 2.0;
            g.Fill(0, y0, W, H - y0, 0x020906);
            wchar_t b[32];
            const double v = t->GetAirspeed(), alt = t->GetAltitude() / 1000.0;
            const wchar_t* lab[3] = {L"СКОР м/с", L"ВЫС км", L"g"};
            std::wstring val[3];
            std::swprintf(b, 32, L"%.0f", v); val[0] = b;
            std::swprintf(b, 32, alt < 100 ? L"%.2f" : L"%.0f", alt); val[1] = b;
            std::swprintf(b, 32, L"%.2f", t->accelG_); val[2] = b;
            for (auto& w : val) for (wchar_t& c : w) if (c == L'.') c = L',';
            for (int i = 0; i < 3; ++i) {
                const double x = x0 + 8 + i * 106;
                if (i) g.Line(x - 5, y0 + 11, x - 5, y0 + 89, kDimB, 2);
                gost_.Text(skp, int(x), int(y0 + 24), lab[i], 16, kDimB, 0);
                const double em = (std::min)(40.0, 92 / (std::max)(1.0, tantra::GostFont::Width(val[i], 1.0)));
                gost_.Text(skp, int(x + 97), int(y0 + 72), val[i], int(em), kMain, 2);
            }
            const bool uvt = t->interior_.UvtOn(), ready = t->podOut_ >= 0.99;
            g.Line(x0 + 322, y0 + 11, x0 + 322, y0 + 89, kDimB, 2);
            gost_.Text(skp, int(x0 + 361), int(y0 + 38), L"УВТ", 26, uvt ? kAcc : kDimB, 1);
            gost_.Text(skp, int(x0 + 361), int(y0 + 74), uvt ? (ready ? L"ВКЛ" : L"ЖДЁТ") : L"ОТКЛ", 20, uvt ? kAcc : kDimB, 1);
        }
        oapiReleaseSketchpad(skp);
    }
}

bool TantraDisplays::TouchHub(double x, double y) {
    const int i = HitTest(hubHits_, x, y, nullptr, 2.0);
    if (i < 0 || i >= 10) return false;
    Tantra* t = t_;
    const HubKey& k = kHubKey[i];
    hubLit_[i] = oapiGetSysTime() + 0.25;
    if (k.mode >= 0) { radMode_ = 0; t->ToggleNav(k.mode); }
    else if (k.mode == -1) { radMode_ = 0; for (const HubKey& o : kHubKey) if (o.mode >= 0 && t->GetNavmodeState(o.mode)) t->ToggleNav(o.mode); }   // РУЧН.: all off
    else {                                                                // ours: radial out / in (the same key again: off)
        const int m = k.mode == -2 ? 1 : -1;
        radMode_ = radMode_ == m ? 0 : m;
        if (radMode_) for (const HubKey& o : kHubKey) if (o.mode >= 0 && t->GetNavmodeState(o.mode)) t->ToggleNav(o.mode);
        radInit_ = false;
    }
    return true;
}

// The radial hold: the target direction (out from the planet's centre, or in) in the ship's frame gives the errors of the pitch
// (the nose up: +) and the yaw (the nose right: +); the turning toward it is their change (no sign convention of Orbiter's angular
// velocity needed); a wanted turning rate (0.5 /s of the error, at most 2 deg/s) is held by the RCS groups; the bank's change is
// damped. Every frame, while it is on.
void TantraDisplays::RadialStep(double dt) {
    Tantra* t = t_;
    static const THGROUP_TYPE kG[6] = {THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT};
    if (radMode_) for (const HubKey& o : kHubKey) if (o.mode >= 0 && t->GetNavmodeState(o.mode)) { radMode_ = 0; break; }   // an Orbiter autopilot took over
    OBJHANDLE ref = t->GetSurfaceRef();
    if (!radMode_ || !ref) {
        if (radWas_) { for (THGROUP_TYPE g : kG) t->SetThrusterGroupLevel(g, 0.0); radWas_ = false; }
        radInit_ = false;
        return;
    }
    radWas_ = true;
    VECTOR3 sp, pp, dir; t->GetGlobalPos(sp); oapiGetGlobalPos(ref, &pp);
    t->Global2Local(sp + unit(sp - pp) * double(radMode_), dir);
    dir = unit(dir);
    const double ep = std::atan2(dir.y, dir.z), ey = std::atan2(dir.x, dir.z);
    VECTOR3 rt; t->HorizonRot(_V(1, 0, 0), rt);
    const double bank = -std::asin((std::max)(-1.0, (std::min)(1.0, rt.y)));
    dt = (std::max)(dt, 1e-4);
    if (radInit_) {
        auto rate = [&](double now, double was, double& w, double sgn) {   // filtered; a jump over +-180 deg skipped
            const double d = now - was;
            if (std::fabs(d) < PI) w += (sgn * d / dt - w) * (std::min)(1.0, dt * 5);
        };
        rate(ep, radEp_, radWp_, -1.0); rate(ey, radEy_, radWy_, -1.0); rate(bank, radBank_, radWb_, 1.0);
    } else { radWp_ = radWy_ = radWb_ = 0.0; radInit_ = true; }
    radEp_ = ep; radEy_ = ey; radBank_ = bank;
    auto axis = [&](double err, double w, THGROUP_TYPE plus, THGROUP_TYPE minus) {
        const double wd = (std::max)(-2.0 * RAD, (std::min)(2.0 * RAD, 0.5 * err)), u = (std::max)(-1.0, (std::min)(1.0, (wd - w) * 60.0));
        t->SetThrusterGroupLevel(plus, (std::max)(0.0, u)); t->SetThrusterGroupLevel(minus, (std::max)(0.0, -u));
    };
    axis(ep, radWp_, THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN);
    axis(ey, radWy_, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_YAWLEFT);
    axis(0.0, radWb_, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_BANKLEFT);      // (the right wing going down: u < 0, bank left)
}

// ---- the pods' display (screen 7, a slab at the left console's front; the mockup's 400 x 260 drawn at the screen's own size):
// seen from above, the hull and the four pods - each its thrust and its cups' angle; their state, the thrust, the cups, УВТ -----
void TantraDisplays::DrawPods() {
    SURFHANDLE s = s_[kWingR];
    if (!s) return;
    Tantra* t = t_;
    namespace sp = tantra::spec;
    namespace m = tantra::mesh;
    int Wi = 0, Hi = 0; oapiGetSurfaceSize(s, &Wi, &Hi);
    const double k = Hi / 260.0, now = oapiGetSysTime();
    oapiClearSurface(s, 0xFF000000);
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, Wi, Hi, 0x040b10);
            g.Ellipse(110 * k, 130 * k, 22 * k, 112 * k, 0, kDimB, 2);            // the hull, the nose up
            const bool out = t->podOut_ > 0.01, full = t->podOut_ >= 0.99;
            double sum = 0.0;
            for (int p = 0; p < sp::kPodCount; ++p) {
                const double side = m::kPods[p].pivot.x < 0 ? -1.0 : 1.0, x = (110 + side * 74) * k, y = (m::kPods[p].s > 60 ? 62 : 196) * k;
                double lvl = 0.0;
                for (int c = 0; c < sp::kCupsPerPod; ++c) if (t->pod_[p * sp::kCupsPerPod + c]) lvl += t->GetThrusterLevel(t->pod_[p * sp::kCupsPerPod + c]);
                lvl /= sp::kCupsPerPod; sum += lvl;
                g.Line((110 + side * 20) * k, y, x - side * 24 * k, y, kDimB, 2);   // the arm
                g.Stroke(x - 22 * k, y - 30 * k, 44 * k, 60 * k, out ? kMain : kDimB, 2);
                if (lvl > 0.005) g.Fill(x - 19 * k, y + (27 - 54 * lvl) * k, 38 * k, 54 * lvl * k, Mix(kMain, 0x040b10, 0.7));   // its thrust
                const double a = PI - t->podAngle_ * RAD;                        // its cups (0 aft = left, 90 down)
                g.Line(x, y, x + 18 * k * std::cos(a), y + 18 * k * std::sin(a), kAcc, 4);
                wchar_t b[16]; std::swprintf(b, 16, L"%.0f°", t->podAngle_);
                gost_.Text(skp, int(x), int(y + 44 * k), b, int(15 * k), kDimB, 1);
            }
            const wchar_t* st = t->podsWanted_ ? (full ? L"ВЫПУЩЕНЫ" : L"ВЫПУСК…") : (t->podOut_ <= 0.01 ? L"УБРАНЫ" : t->podAngle_ > 1.0 ? L"ЧАШИ В 0°…" : L"УБОРКА…");
            const bool moving = (t->podsWanted_ && !full) || (!t->podsWanted_ && out), blink = std::fmod(now, 0.32) < 0.16;
            const double tx = 214 * k;
            gost_.Text(skp, int(tx), int(26 * k), L"ВЫДВ. БЛОКИ", int(22 * k), kDimB, 0);
            gost_.Text(skp, int(tx), int(58 * k), st, int(26 * k), !out && !t->podsWanted_ ? kDimB : moving ? (blink ? kAcc : kDimB) : kMain, 0);
            wchar_t b[48];
            std::swprintf(b, 48, L"ТЯГА %.0f %%", sum / sp::kPodCount * 100); gost_.Text(skp, int(tx), int(96 * k), b, int(22 * k), kMain, 0);
            std::swprintf(b, 48, L"ЧАШИ %.0f°, ЦЕЛЬ %.0f°", t->podAngle_, t->podTarget_); gost_.Text(skp, int(tx), int(126 * k), b, int(22 * k), kMain, 0);
            const bool uvt = t->interior_.UvtOn();
            gost_.Text(skp, int(tx), int(160 * k), uvt ? (full ? L"УВТ ВКЛ" : L"УВТ ЖДЁТ ВЫПУСКА") : L"УВТ ОТКЛ", int(22 * k), uvt ? kAcc : kDimB, 0);
            if (!full) gost_.Text(skp, int(tx), int(214 * k), L"РУКОЯТЬ ЗАПЕРТА", int(18 * k), kRed, 0);
        }
        oapiReleaseSketchpad(skp);
    }
}

// ---- ПАРАМЕТРЫ (the front glass's third tab): its keys -> the displays' settings, the HUD, the RCS ---------------------------------
void TantraDisplays::ParamsCommand(int cmd, double value) {
    namespace ps = tantra::paramsscreen;
    Tantra* t = t_;
    auto manual = [&]() { if (autoLum_) { mfdGain_ = MfdGain(); mfdGamma_ = MfdGamma(); hudGain_ = HudGain(); autoLum_ = false; } };   // a step: by hand
    switch (cmd) {
        case ps::kCmdHud: hudMode_ = int(value); break;
        case ps::kCmdLayer: { const int i = int(value); if (i >= 0 && i < 4) hudLayer_[i] = !hudLayer_[i]; break; }
        case ps::kCmdPalette:
            if (int(value) <= 1) palette_ = int(value);
            else t->Message("Палитра ИЗУМРУД — в HUD её пока нет", "The ИЗУМРУД palette is not in the HUD yet");
            break;
        case ps::kCmdHudGain: manual(); hudGain_ = ps::Stepped(cmd, hudGain_, value); break;
        case ps::kCmdMfdGain: manual(); mfdGain_ = ps::Stepped(cmd, mfdGain_, value); break;
        case ps::kCmdMfdContrast: manual(); mfdGamma_ = ps::Stepped(cmd, mfdGamma_, value); break;
        case ps::kCmdAutoLum: autoLum_ = !autoLum_; break;
        case ps::kCmdMfdRes: t->interior_.SetMfdRes(int(value)); break;
        case ps::kCmdRcs: t->SetAttitudeMode(int(value)); break;
        case ps::kCmdUnits: units_ = int(value); break;
        default: break;
    }
}

// НЕПРОЗР.: the glasses' own material (as the self-lit displays) with the opacity as its alpha
void TantraDisplays::ApplyOpacity() {
    if (!dm_ || tantra::interior::kGlassMat < 0) return;
    const float a = float(opq_);
    MATERIAL m = {{1, 1, 1, a}, {1, 1, 1, a}, {0, 0, 0, 1}, {1, 1, 1, 1}, 1.0f};
    oapiSetMaterial(dm_, DWORD(tantra::interior::kGlassMat), &m);
}

// ---- ТЕПЛО: the ship's state for the thermal page (as Tantra::UpdateDamage sees the hull) ----------------------------------------
void TantraDisplays::FillThermalView(tantra::thermalscreen::View& v) {
    namespace dm = tantra::damage;
    Tantra* t = t_;
    v.t = oapiGetSimTime();
    for (int z = 0; z < tantra::thermalscreen::kZones; ++z) { v.skin[z] = t->damage_.Temperature(z); v.lim[z] = t->damage_.Limit(z); v.flux[z] = t->damage_.HeatFlux(z); }
    v.edgeIntegrity = (std::min)(t->damage_.Integrity(dm::kCrestPort), t->damage_.Integrity(dm::kCrestStbd));
    v.hullLost = t->damage_.Destroyed();
    VECTOR3 hv; t->GetHorizonAirspeedVector(hv);
    tantra::thermalscreen::Path& p = v.path;
    p.h = t->GetAltitude(); p.v = t->GetAirspeed(); p.gamma = std::atan2(hv.y, (std::max)(std::hypot(hv.x, hv.z), 1.0));
    p.mass = t->GetMass(); p.aoa = t->GetAOA(); p.bank = t->GetBank();
    p.crestAvail = t->aeroCrest_; p.gearArea = t->aeroGearArea_;
    const tantra::CarriagePose& cp = t->carriage_.Pose();
    p.expo.crests = p.expo.fin = 1.0 - (std::max)(t->tuck_, cp.tuck);
    p.expo.pods = t->podOut_; p.expo.gear = t->carriage_.Gear(); p.expo.hangar = t->hangar_; p.expo.bays = t->bayDoors_;
    p.expo.sternCups = (std::max)(t->marchOut_, t->irisAna_);
    p.air = thermAir_;
    if (OBJHANDLE ref = t->GetSurfaceRef()) { p.body.R = oapiGetSize(ref); p.body.g0 = GGRAV * oapiGetMass(ref) / (p.body.R * p.body.R); }
    v.rho = t->GetAtmDensity(); v.mach = t->GetMachNumber(); v.q = t->GetDynPressure();
    v.gLoad = std::hypot(t->GetLift(), t->GetDrag()) / (std::max)(1.0, p.mass) / 9.81;
    v.gLimit = t->gLimitOn_ ? t->gLimit_ : 4.0;
    v.onGround = t->GroundContact(); v.wingMode = t->wingMode_; v.wingFold = cp.tuck;
    v.plantRun = t->plant_.Running(); v.plantLost = t->plant_.Lost();
    v.sternT = t->plant_.SternT(); v.coilT = t->plant_.CoilT(); v.tSafe = t->plant_.Cfg().tSafe; v.tLost = t->plant_.Cfg().tLost;
    v.thr = !t->AnaIsMain() && t->march_ ? t->GetThrusterLevel(t->march_) : 0.0;
    v.reactMass = int(t->plantOut_.mass); v.heatIn = t->plantOut_.heatIn; v.mdot = t->plantOut_.mdot;
    double lvl = 0.0; for (THRUSTER_HANDLE h : t->pod_) if (h) lvl = (std::max)(lvl, t->GetThrusterLevel(h));
    v.podLevel = lvl; v.pods = t->podOut_ < 0.99 ? 0 : lvl > 0.01 ? 2 : 1;
}

// every frame: the planet's density table (10 s), the page's tracking (1 s of sim time, whatever the glass shows), the entry's
// forecast while it goes on (about once a second of real time: 1..8 ms a run)
void TantraDisplays::ThermalStep() {
    Tantra* t = t_;
    const double simt = oapiGetSimTime(), syst = oapiGetSysTime();
    if (syst - thermAirT_ > 10.0) {
        thermAirT_ = syst;
        OBJHANDLE ref = t->GetSurfaceRef();
        double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
        for (int k = 0; k < tantra::thermalscreen::Air::kN; ++k) {
            ATMPARAM prm = {};
            thermAir_.rho[k] = ref && oapiPlanetHasAtmosphere(ref) ? (oapiGetPlanetAtmParams(ref, k * tantra::thermalscreen::Air::kStep, lng, lat, &prm), prm.rho) : 0.0;
        }
    }
    if (std::fabs(simt - thermTrackT_) < 1.0) return;
    thermTrackT_ = simt;
    tantra::thermalscreen::View v; FillThermalView(v);
    thermScr_.Track(v);
    if (thermScr_.Entry() && syst - thermFcT_ >= 1.0) {
        thermFcT_ = syst;
        tantra::thermalscreen::ForecastIn in;
        in.path = v.path; in.skin = t->damage_;
        thermScr_.SetForecast(tantra::thermalscreen::RunForecast(in));
    }
}

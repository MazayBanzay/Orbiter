// TVehicles.dll - the transport module: the shared registry (TVApi.h) and the charging / cell-exchange station.
// Station (class TVehicles\Station): a cabinet with a 15 m cable on a reel and a rack of six energy cells (80 kg, 250 kWh).
// Its input (300 kW: the Tantra's plant, a base) recharges the rack's cells and feeds the cable. People work it with F
// (through OrbiterCrew's entrance items until the context menu is there): take / return the cable, take / put a cell.
// The cable is drawn from the reel's guide to the carrier's hand, or to the vehicle's socket it is plugged into; beyond
// 15 m the plug pulls out and the cable winds back.
#define ORBITER_MODULE
#include "orbitersdk.h"
#include "OrbiterCrewApi.h"
#include "TVApi.h"
#include "StationGeo.h"
#include "TVThermal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr double kCellJ = 250.0 * 3.6e6;     // J, one energy cell (250 kWh, 80 kg)
constexpr double kCellMass = 80.0;
constexpr double kInputW = 300.0e3;          // W, the station's input (the cable and the rack share it)
constexpr double kCableLen = 15.0;           // m
double Clamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }

class Station;
std::vector<Station*>& Stations() { static std::vector<Station*> v; return v; }
std::map<int, TvCarry>& Carry() { static std::map<int, TvCarry> m; return m; }
std::map<OBJHANDLE, VECTOR3>& Sockets() { static std::map<OBJHANDLE, VECTOR3> m; return m; }
void Sync(int person);   // below: what she carries, as OrbiterCrew sees her hands
Station* PluggedStation(OBJHANDLE v);

class Station : public VESSEL4 {
public:
    Station(OBJHANDLE h, int fm) : VESSEL4(h, fm) { Stations().push_back(this); for (double& c : cell_) c = kCellJ; }
    ~Station() {
        auto& v = Stations(); v.erase(std::remove(v.begin(), v.end(), this), v.end());
        if (registered_ && api_.UnregisterInterior) api_.UnregisterInterior(GetHandle());
    }

    void clbkSetClassCaps(FILEHANDLE cfg) override {
        (void)cfg;
        SetSize(3.0);
        SetEmptyMass(3200.0 + st::kCells * kCellMass);                   // the 300 kW cabinet, the drum, the rack, the pad
        SetPMI(_V(1.6, 1.2, 1.4));
        SetCrossSections(_V(3.4, 9.0, 6.5));
        mesh_ = oapiLoadMeshGlobal("TVehicles\\Station");
        meshIdx_ = AddMesh(mesh_);
        const double m = GetEmptyMass(), k = m * 9.81 / 0.01, c = 2.0 * std::sqrt(k * m);
        // the pad's corners on the ground (it lies on it), the roof as the fourth point
        TOUCHDOWNVTX t[4] = {{_V(0.5 * (st::kPad[0] + st::kPad[1]), st::kGround, st::kPad[3]), k, c, 3.0, 3.0},
                             {_V(st::kPad[0], st::kGround, st::kPad[2]), k, c, 3.0, 3.0},
                             {_V(st::kPad[1], st::kGround, st::kPad[2]), k, c, 3.0, 3.0}, {_V(0.0, st::kGround + 2.9, 0.0), k, c, 3.0, 3.0}};
        SetTouchdownPoints(t, 4);
        att_ = CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");
        for (int g : {st::kCableGrp, st::kPlugGrp}) RestCopy(g);
        for (int k2 = 0; k2 < st::kCells; ++k2) RestCopy(st::kCellGrp[k2]);
    }

    void clbkPostCreation() override {
        if (!api_.Load() || !api_.RegisterInterior) return;
        fns_ = OcInterior{};
        fns_.Attach = [](void* c) { return static_cast<Station*>(c)->att_; };
        fns_.Ground = [](void*, const VECTOR3*, double, double*) { return 0; };
        fns_.Walls = [](void*, const VECTOR3*, VECTOR3*, double, double) {};
        fns_.Count = [](void*) { return 0; };                 // (the reel and the rack are context nodes now)
        fns_.Item = &Station::cItem;
        fns_.Use = &Station::cUse;
        api_.RegisterInterior(GetHandle(), &fns_, this);
        registered_ = true;
        if (api_.SetInteriorExt) {
            ext_ = OcInteriorExt{}; ext_.size = sizeof(OcInteriorExt);
            ext_.OuterWalls = [](void*, const VECTOR3* from, VECTOR3* to, double r, double) {   // the units on the pad: a box
                auto in = [r](double x, double z) { return x > st::kSolid[0] - r && x < st::kSolid[1] + r && z > st::kSolid[2] - r && z < st::kSolid[3] + r; };
                if (in(to->x, to->z)) { if (!in(from->x, to->z)) to->x = from->x; else if (!in(to->x, from->z)) to->z = from->z; else { to->x = from->x; to->z = from->z; } }
            };
            ext_.NodeCount = [](void*) { return 1 + st::kCells; };      // the drum, and each slot of the rack (the one she looks at)
            ext_.Node = &Station::cNode;
            ext_.Actions = &Station::cActions;
            ext_.Begin = [](void* c, int node, int act, int person) {
                OcAction a[4]; const int n = cActions(c, node, person, a, 4);
                for (int i = 0; i < n; ++i) if (a[i].id == act) return a[i].available;
                return 0;
            };
            ext_.End = [](void* c, int node, int act, int person, int done) {
                if (!done) return;
                OcAction a[4]; const int n = cActions(c, node, person, a, 4);
                for (int i = 0; i < n; ++i) if (a[i].id == act && a[i].available) { UseAt(c, node, act, person); return; }
            };
            api_.SetInteriorExt(GetHandle(), &ext_);
        }
    }

    void clbkLoadStateEx(FILEHANDLE scn, void* vs) override {
        char* line;
        while (oapiReadScenario_nextline(scn, line)) {
            if (!std::strncmp(line, "CELLS", 5)) {
                std::vector<double> v(st::kCells, -1.0);
                std::sscanf(line + 5, "%lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
                for (int k = 0; k < st::kCells; ++k) cell_[k] = v[k] < 0 ? -1.0 : Clamp(v[k], 0, 1) * kCellJ;
            } else if (!std::strncmp(line, "PLUGGED", 7)) {         // the vehicle the cable was in (Restore finds it)
                const char* n = line + 7; while (*n == ' ') ++n;
                pendingPlug_ = n;
            } else ParseScenarioLineEx(line, vs);
        }
    }
    void clbkSaveState(FILEHANDLE scn) override {
        VESSEL4::clbkSaveState(scn);
        char b[160] = "", t[24];
        for (int k = 0; k < st::kCells; ++k) { std::snprintf(t, sizeof t, " %.3f", cell_[k] < 0 ? -1.0 : cell_[k] / kCellJ); std::strcat(b, t); }
        oapiWriteScenario_string(scn, const_cast<char*>("CELLS"), b + 1);
        // the cable in a vehicle's socket; the cable in her hands she keeps herself (OrbiterCrew HELD_DATA "cable <us>")
        if (plugged_ && oapiIsVessel(plugged_)) oapiWriteScenario_string(scn, const_cast<char*>("PLUGGED"), oapiGetVesselInterface(plugged_)->GetName());
    }

    void clbkPreStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)mjd;
        if (simdt <= 0) return;
        Restore(simdt);
        // the input: first the cable, then the rack's cells (the emptiest first)
        double spare = kInputW - cableW_;
        cableW_ = 0.0;
        for (int pass = 0; pass < st::kCells && spare > 1.0; ++pass) {
            int lo = -1;
            for (int k = 0; k < st::kCells; ++k) if (cell_[k] >= 0 && cell_[k] < kCellJ && (lo < 0 || cell_[k] < cell_[lo])) lo = k;
            if (lo < 0) break;
            const double e = (std::min)(spare * simdt, kCellJ - cell_[lo]);
            cell_[lo] += e; spare -= e / simdt;
        }
        // the cable's far end: the carrier's hand, or the vehicle's socket; too far - out of the hand / the socket, back on the reel
        VECTOR3 end; bool out = false;
        if (cablePerson_) Sync(cablePerson_);                            // let go (X): back on the reel at once
        if (cablePerson_) {
            OcInfo in{};
            if (api_.Ok() && api_.Info(cablePerson_, &in) && in.vessel && oapiIsVessel(in.vessel)) {
                VECTOR3 bp, fwd; oapiGetGlobalPos(in.vessel, &bp);
                VESSEL* pv = oapiGetVesselInterface(in.vessel); pv->GlobalRot(_V(0.25, -0.05, 0.30), fwd);
                end = bp + fwd; out = true;
            } else cablePerson_ = 0;
        } else if (plugged_ && oapiIsVessel(plugged_) && Sockets().count(plugged_)) {
            oapiGetVesselInterface(plugged_)->Local2Global(Sockets()[plugged_], end); out = true;
        } else plugged_ = nullptr;
        if (out) {
            VECTOR3 ex; Local2Global(_V(st::kExit[0], st::kExit[1], st::kExit[2]), ex);
            if (length(end - ex) > kCableLen) {
                if (cablePerson_) { Carry().erase(cablePerson_); tvTakeBack(api_, cablePerson_); oapiWriteLogV("TVehicles: the cable pulled out of the hand (15 m)"); }
                if (plugged_) oapiWriteLogV("TVehicles: the plug pulled out of %s (15 m)", oapiGetVesselInterface(plugged_)->GetName());
                cablePerson_ = 0; plugged_ = nullptr; out = false;
            }
        }
        DrawCable(out ? &end : nullptr);
    }

    void clbkVisualCreated(VISHANDLE vis, int) override { dev_ = GetDevMesh(vis, meshIdx_); drawn_ = -1; }
    void clbkVisualDestroyed(VISHANDLE, int) override { dev_ = nullptr; }

    // After a load: the cable back into the vehicle's socket it was in (once that vehicle has registered its socket), and
    // back to the person whose hands hold our plug (OrbiterCrew restores what she held before the first step).
    void Restore(double dt) {
        if (restoreT_ > 5.0) return;
        restoreT_ += dt;
        if (!pendingPlug_.empty()) {
            OBJHANDLE v = oapiGetVesselByName(const_cast<char*>(pendingPlug_.c_str()));
            if (!v) pendingPlug_.clear();
            else if (Sockets().count(v)) {
                if (!plugged_ && !cablePerson_ && !PluggedStation(v)) { plugged_ = v; oapiWriteLogV("TVehicles: the cable back in %s", pendingPlug_.c_str()); }
                pendingPlug_.clear();
            }
        }
        if (adopted_ || !api_.Ok() || !api_.PersonOfBody || !api_.HeldOf) return;
        adopted_ = true;
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            const int p = api_.PersonOfBody(oapiGetVesselByIndex(i));
            if (!p) continue;
            OcHeld h{}; h.size = sizeof h;
            if (!api_.HeldOf(p, &h) || std::strcmp(h.owner, "TVehicles") || !tvIsPlugData(h.data) || h.data[5] != ' ' || std::strcmp(h.data + 6, GetName())) continue;
            if (!cablePerson_ && !plugged_ && !Carry().count(p)) {
                cablePerson_ = p; Carry()[p] = {TV_CABLE, GetHandle(), 0.0};
                oapiWriteLogV("TVehicles: the cable back in the hands of person %d", p);
            } else tvTakeBack(api_, p);                                  // our cable is elsewhere now: her plug is let go
        }
    }

    // ---- the registry's side (called by the exports) ----
    void ReelIn(int person) { if (cablePerson_ == person) { cablePerson_ = 0; oapiWriteLogV("TVehicles: the cable let go - back on the reel"); } }
    int TakeCable(int person) {
        if (cablePerson_ || plugged_) return 0;
        OcHeld h; tvHeldPlug(&h, GetName());
        if (!tvHandOver(api_, person, &h)) return 0;
        cablePerson_ = person; Carry()[person] = {TV_CABLE, GetHandle(), 0.0};
        return 1;
    }
    bool PlugInto(OBJHANDLE v, int person) {
        if (cablePerson_ != person || !Sockets().count(v)) return false;
        VECTOR3 ex, so; Local2Global(_V(st::kExit[0], st::kExit[1], st::kExit[2]), ex); oapiGetVesselInterface(v)->Local2Global(Sockets()[v], so);
        if (length(so - ex) > kCableLen) return false;
        plugged_ = v; cablePerson_ = 0; Carry().erase(person); tvTakeBack(api_, person);
        return true;
    }
    bool UnplugFrom(OBJHANDLE v, int person) {
        if (plugged_ != v) return false;
        plugged_ = nullptr;
        if (person) {                                                   // into her hands; no person: back on the reel
            OcHeld h; tvHeldPlug(&h, GetName());
            if (!tvHandOver(api_, person, &h)) { plugged_ = v; return false; }   // her hands are busy: it stays plugged
            cablePerson_ = person; Carry()[person] = {TV_CABLE, GetHandle(), 0.0};
        }
        return true;
    }
    OBJHANDLE Plugged() const { return plugged_; }
    double Give(double wantW, double dt) { const double w = Clamp(wantW, 0.0, kInputW); cableW_ = w; return w * dt; }

private:
    enum { kItemCable = 1, kItemTakeCell = 2, kItemPutCell = 3 };
    static int cItem(void* c, int i, OcItem* out) {
        Station* s = static_cast<Station*>(c);
        const bool en = s->api_.English();                                      // OrbiterCrew's language, asked each time
        *out = OcItem{};
        out->kind = OC_AIRLOCK; out->dir = _V(0, 0, -1); out->radius = 1.6;      // from outside OrbiterCrew offers the entrances only
        if (i == 0) {
            out->id = kItemCable; out->pos = _V(st::kHolder[0], st::kGround, st::kHolder[2] + 0.5);
            std::snprintf(out->label, sizeof out->label, "%s", s->plugged_ ? (en ? "the cable is plugged into a vehicle" : "кабель подключён к машине")
                                                              : s->cablePerson_ ? (en ? "put the cable back on the reel" : "вернуть кабель на катушку") : (en ? "take the cable (15 m)" : "взять кабель (15 м)"));
        } else if (i == 1) {
            const int k = s->Fullest();
            out->id = kItemTakeCell; out->pos = _V(-0.30, st::kGround, 0.95);
            if (k < 0) std::snprintf(out->label, sizeof out->label, "%s", en ? "no cells" : "ячеек нет");
            else std::snprintf(out->label, sizeof out->label, en ? "take a cell (%.0f %%)" : "взять ячейку (%.0f %%)", s->cell_[k] / kCellJ * 100.0);
        } else if (i == 2) {
            out->id = kItemPutCell; out->pos = _V(-0.30, st::kGround, 0.80); out->radius = 1.4;
            std::snprintf(out->label, sizeof out->label, "%s", en ? "put the cell on charge" : "поставить ячейку на зарядку");
        } else return 0;
        return 1;
    }
    static void cUse(void* c, int id, int person) {
        Station* s = static_cast<Station*>(c);
        Sync(person);
        auto& carry = Carry();
        const bool hands = !carry.count(person) || carry[person].kind == TV_NOTHING;
        if (id == kItemCable) {
            if (s->cablePerson_ == person) { s->cablePerson_ = 0; carry.erase(person); tvTakeBack(s->api_, person); oapiWriteLogV("TVehicles: the cable back on the reel"); }
            else if (hands && s->TakeCable(person)) oapiWriteLogV("TVehicles: person %d took the cable", person);
        } else if (id == kItemTakeCell) {
            const int k = s->Fullest();
            if (k < 0 || !hands) return;
            OcHeld h; tvHeldCell(&h, s->cell_[k] / kCellJ);
            if (!tvHandOver(s->api_, person, &h)) return;                    // too heavy here / hands busy: OrbiterCrew said so
            carry[person] = {TV_CELL, s->GetHandle(), s->cell_[k]}; s->cell_[k] = -1.0; s->drawn_ = -1;
            s->SetEmptyMass(s->GetEmptyMass() - kCellMass);
            oapiWriteLogV("TVehicles: person %d took a cell (%.0f %%)", person, carry[person].energyJ / kCellJ * 100.0);
        } else if (id == kItemPutCell) {
            if (!carry.count(person) || carry[person].kind != TV_CELL) return;
            for (int k = 0; k < st::kCells; ++k)
                if (s->cell_[k] < 0) { s->cell_[k] = carry[person].energyJ; carry.erase(person); tvTakeBack(s->api_, person); s->drawn_ = -1; s->SetEmptyMass(s->GetEmptyMass() + kCellMass); break; }
        }
    }
    // context nodes: the reel (take / return the cable) and the rack (take a cell / put one on charge); the action ids are the
    // old item ids, so cUse does the work
    // the slots' nodes: ids kNodeSlot0 + k, at the cartridge's front face - the one she looks at is the one she works
    enum { kNodeReel = 1, kNodeSlot0 = 10 };
    static int cNode(void* c, int i, OcNode* o) {
        const bool en = static_cast<Station*>(c)->api_.English();
        o->reach = 1.6; o->inside = 0;
        if (i == 0) { o->id = kNodeReel; o->pos = _V(st::kReel[0], st::kReel[1], st::kReel[2]); std::snprintf(o->label, sizeof o->label, "%s", en ? "cable drum (15 m)" : "кабельный барабан (15 м)"); return 1; }
        const int k = i - 1;
        if (k >= 0 && k < st::kCells) {
            o->id = kNodeSlot0 + k; o->reach = 1.4;
            o->pos = _V(st::kCellPos[k][0], st::kCellPos[k][1], st::kCellPos[k][2] + 0.14);
            std::snprintf(o->label, sizeof o->label, en ? "rack slot %d" : "гнездо стеллажа %d", k + 1);
            return 1;
        }
        return 0;
    }
    // a slot's action: the cell from this slot into her hands, or hers into this empty slot
    static void UseAt(void* c, int node, int act, int person) {
        Station* s = static_cast<Station*>(c);
        const int k = node - kNodeSlot0;
        if (node == kNodeReel || k < 0 || k >= st::kCells) { cUse(c, act, person); return; }
        Sync(person);
        auto& carry = Carry();
        if (act == kItemTakeCell && s->cell_[k] >= 0) {
            OcHeld h; tvHeldCell(&h, s->cell_[k] / kCellJ);
            if (!tvHandOver(s->api_, person, &h)) return;                    // too heavy here / hands busy: OrbiterCrew said so
            carry[person] = {TV_CELL, s->GetHandle(), s->cell_[k]}; s->cell_[k] = -1.0; s->drawn_ = -1;
            s->SetEmptyMass(s->GetEmptyMass() - kCellMass);
            oapiWriteLogV("TVehicles: person %d took the cell of slot %d (%.0f %%)", person, k + 1, carry[person].energyJ / kCellJ * 100.0);
        } else if (act == kItemPutCell && s->cell_[k] < 0 && carry.count(person) && carry[person].kind == TV_CELL) {
            s->cell_[k] = carry[person].energyJ; carry.erase(person); tvTakeBack(s->api_, person); s->drawn_ = -1;
            s->SetEmptyMass(s->GetEmptyMass() + kCellMass);
            oapiWriteLogV("TVehicles: person %d put a cell into slot %d", person, k + 1);
        }
    }
    static int cActions(void* c, int node, int person, OcAction* out, int max) {
        Station* s = static_cast<Station*>(c);
        int n = 0; char b[64];
        Sync(person);
        auto& carry = Carry();
        const bool carries = carry.count(person) && carry[person].kind != TV_NOTHING;
        const bool busy = carries || tvHolding(s->api_, person);
        if (node == kNodeReel) {
            const bool en = s->api_.English();
            const char* take = en ? "take the cable" : "взять кабель";
            if (s->cablePerson_ == person) tvAct(out, n, max, kItemCable, en ? "put the cable back on the reel" : "вернуть кабель на катушку", true, nullptr, 0.8);
            else if (s->plugged_) tvAct(out, n, max, kItemCable, take, false, en ? "plugged into a vehicle" : "подключён к машине", 0.8);
            else if (s->cablePerson_) tvAct(out, n, max, kItemCable, take, false, en ? "someone else has it" : "кабель у другого", 0.8);
            else tvAct(out, n, max, kItemCable, take, !busy, en ? "hands busy" : "руки заняты", 0.8);
            return n;
        }
        const int k = node - kNodeSlot0;
        if (k >= 0 && k < st::kCells) {
            const bool en = s->api_.English();
            if (s->cell_[k] >= 0) {
                std::snprintf(b, sizeof b, en ? "take the cell (%.0f %%)" : "взять ячейку (%.0f %%)", s->cell_[k] / kCellJ * 100.0);
                tvAct(out, n, max, kItemTakeCell, b, !busy, en ? "hands busy" : "руки заняты", 1.2);
            } else {
                const bool hasCell = carries && carry[person].kind == TV_CELL;
                tvAct(out, n, max, kItemPutCell, en ? "put the cell on charge" : "поставить ячейку на зарядку", hasCell, en ? "no cell in hand" : "в руках нет ячейки", 1.2);
            }
            return n;
        }
        return 0;
    }
    int Fullest() const { int b = -1; for (int k = 0; k < st::kCells; ++k) if (cell_[k] >= 0 && (b < 0 || cell_[k] > cell_[b])) b = k; return b; }

    struct Grp { int grp; std::vector<NTVERTEX> rest, work; };
    void RestCopy(int g) { Grp p{g, {}, {}}; if (MESHGROUP* m = oapiMeshGroup(mesh_, (DWORD)g)) { p.rest.assign(m->Vtx, m->Vtx + m->nVtx); p.work = p.rest; } grp_.push_back(p); }
    Grp* Find(int g) { for (Grp& p : grp_) if (p.grp == g) return &p; return nullptr; }
    void Push(Grp& p) { GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD; e.Vtx = p.work.data(); e.nVtx = (DWORD)p.work.size(); oapiEditMeshGroup(dev_, (DWORD)p.grp, &e); }

    // the cable from the guide's exit A to the end B (station frame), sagging; the plug at B; the rack's cells shown or hidden
    void DrawCable(const VECTOR3* endG) {
        if (!dev_) return;
        const VECTOR3 A = _V(st::kExit[0], st::kExit[1], st::kExit[2]);
        VECTOR3 B = _V(st::kHolder[0], st::kHolder[1], st::kHolder[2]);
        if (endG) Global2Local(*endG, B);
        const int state = endG ? 1 : 0;
        if (!state && drawn_ == 0) return;
        drawn_ = state;
        Grp* cab = Find(st::kCableGrp); Grp* plug = Find(st::kPlugGrp);
        // only the real vertices move: the rest are the corners of the culling box (build_station.py) and stay put
        const VECTOR3 d = B - A; const double L = (std::max)(length(d), 1e-3);
        const VECTOR3 e3 = d / L; VECTOR3 e1 = crossp(e3, _V(0, 1, 0)); if (length(e1) < 1e-3) e1 = _V(1, 0, 0); e1 = unit(e1);
        const VECTOR3 e2 = crossp(e3, e1);
        const double sag = state ? 0.06 * L : 0.0;
        for (size_t k = 0; k < (std::min)(cab->rest.size(), (size_t)st::kCableVtx); ++k) {
            const NTVERTEX& v0 = cab->rest[k]; NTVERTEX& v = cab->work[k];
            const double t = v0.z;
            const VECTOR3 p = state ? A + d * t + e1 * v0.x + e2 * v0.y - _V(0, sag * 4.0 * t * (1.0 - t), 0) : A;
            v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z;
        }
        Push(*cab);
        for (size_t k = 0; k < (std::min)(plug->rest.size(), (size_t)st::kPlugVtx); ++k) {
            const NTVERTEX& v0 = plug->rest[k]; NTVERTEX& v = plug->work[k];
            const VECTOR3 p = B + e1 * v0.x + e2 * v0.y + e3 * v0.z;
            v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z;
        }
        Push(*plug);
        for (int k = 0; k < st::kCells; ++k) {
            Grp* g = Find(st::kCellGrp[k]);
            const VECTOR3 c = _V(st::kCellPos[k][0], st::kCellPos[k][1], st::kCellPos[k][2]);
            for (size_t j = 0; j < g->rest.size(); ++j) {
                const NTVERTEX& v0 = g->rest[j]; NTVERTEX& v = g->work[j];
                const double s = cell_[k] >= 0 ? 1.0 : 0.0;                 // taken: shrunk into its bay
                v.x = (float)(c.x + (v0.x - c.x) * s); v.y = (float)(c.y + (v0.y - c.y) * s); v.z = (float)(c.z + (v0.z - c.z) * s);
            }
            Push(*g);
        }
    }

    MESHHANDLE mesh_ = nullptr; UINT meshIdx_ = 0; DEVMESHHANDLE dev_ = nullptr;
    std::vector<Grp> grp_;
    double cell_[st::kCells];
    int cablePerson_ = 0, drawn_ = -1;
    OBJHANDLE plugged_ = nullptr;
    std::string pendingPlug_;                       // PLUGGED from the scenario, until Restore finds the vehicle
    double restoreT_ = 0.0; bool adopted_ = false;
    double cableW_ = 0.0;
    OcApi api_; OcInterior fns_{}; OcInteriorExt ext_{}; ATTACHMENTHANDLE att_ = nullptr; bool registered_ = false;
};

Station* StationOf(OBJHANDLE h) { for (Station* s : Stations()) if (s->GetHandle() == h) return s; return nullptr; }
Station* PluggedStation(OBJHANDLE v) { for (Station* s : Stations()) if (s->Plugged() == v) return s; return nullptr; }
}  // namespace

namespace {
// OrbiterCrew's hands are the truth: a cell she put down (X) or threw is no longer carried (it lies as an OrbiterCrew item,
// its charge in our data); picked up again, it is a cell with that charge. The cable's plug let go: back on the reel
void Sync(int person) {
    static OcApi api;
    if (!person || !api.Load() || !api.HeldOf) return;
    OcHeld h{}; h.size = sizeof h;
    const bool ours = api.HeldOf(person, &h) && !std::strcmp(h.owner, "TVehicles");
    auto it = Carry().find(person);
    if (ours && !std::strncmp(h.data, "cell ", 5)) {
        Carry()[person] = {TV_CELL, it != Carry().end() ? it->second.station : nullptr, Clamp(std::atof(h.data + 5), 0.0, 1.0) * kCellJ};
        return;
    }
    if (ours && tvIsPlugData(h.data) && it != Carry().end() && it->second.kind == TV_CABLE) return;
    if (it == Carry().end()) return;
    if (it->second.kind == TV_CABLE) if (Station* s = StationOf(it->second.station)) s->ReelIn(person);
    Carry().erase(it);
}
}  // namespace

// ---------------- the exports (TVApi.h) ----------------
// the bodies' surface temperatures by the Sun's elevation (TVThermal.h): -> 1 if the body is in the table (Earth, Moon, Mars,
// Titan, Io); *groundK the surface, *airK the air at 1.5 m (0 for the airless ones)
extern "C" __declspec(dllexport) int tvSurfaceTemps(const char* body, double sinElev, double* groundK, double* airK) {
    const tvthermal::Temps t = tvthermal::SurfaceTemps(body, sinElev);
    if (groundK) *groundK = t.ground;
    if (airK) *airK = t.airT;
    return t.known ? 1 : 0;
}
extern "C" __declspec(dllexport) int tvCarryGet(int person, TvCarry* out) {
    Sync(person);
    auto it = Carry().find(person);
    if (it == Carry().end() || it->second.kind == TV_NOTHING) { if (out) *out = TvCarry{}; return 0; }
    if (out) *out = it->second;
    return 1;
}
extern "C" __declspec(dllexport) void tvCarrySet(int person, const TvCarry* c) {
    if (!c || c->kind == TV_NOTHING) Carry().erase(person); else Carry()[person] = *c;
}
extern "C" __declspec(dllexport) void tvSocketRegister(OBJHANDLE v, const VECTOR3* p) { if (v && p) Sockets()[v] = *p; }
extern "C" __declspec(dllexport) void tvSocketUnregister(OBJHANDLE v) {
    Sockets().erase(v);
    for (Station* s : Stations()) if (s->Plugged() == v) s->UnplugFrom(v, 0);
}
extern "C" __declspec(dllexport) int tvPlug(OBJHANDLE v, int person) {
    Sync(person);
    auto it = Carry().find(person);
    if (it == Carry().end() || it->second.kind != TV_CABLE) return 0;
    Station* s = StationOf(it->second.station);
    return s && s->PlugInto(v, person) ? 1 : 0;
}
extern "C" __declspec(dllexport) int tvUnplug(OBJHANDLE v, int person) {
    Station* s = PluggedStation(v);
    return s && s->UnplugFrom(v, person) ? 1 : 0;
}
extern "C" __declspec(dllexport) OBJHANDLE tvPluggedTo(OBJHANDLE v) { Station* s = PluggedStation(v); return s ? s->GetHandle() : nullptr; }
extern "C" __declspec(dllexport) double tvDraw(OBJHANDLE v, double wantW, double dt) { Station* s = PluggedStation(v); return s ? s->Give(wantW, dt) : 0.0; }

DLLCLBK VESSEL* ovcInit(OBJHANDLE h, int fm) { return new Station(h, fm); }
DLLCLBK void ovcExit(VESSEL* v) { delete static_cast<Station*>(v); }

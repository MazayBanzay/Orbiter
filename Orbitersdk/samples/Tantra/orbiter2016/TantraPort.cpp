// Tantra: anamezon port - trap cassettes and column lifts (Orbiter 2010 adapter).
//
// The four trap cassettes sit in two columns (lower + upper slot) over two belly doors. Each
// column has a lift: fork heads on telescopic masts in shafts beyond both ends of the column
// close on the cassette's axial trunnions (magnet + locking pins) and move it vertically -
// from the ground (or free space) under the door into its slot and back. No arm, no overhang:
// the load stays on the mast axes. Upper slot is filled first (through the empty lower one),
// the lower slot is emptied first. Outside the ship a cassette is a TantraTrap vessel; inside
// it is one of the ship's trap propellant resources.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Tantra.h"
#include "TantraGear.h"

namespace m = tantra::mesh;
using tantra::Vec3;

namespace {

VECTOR3 OV(const Vec3& v) { return _V(v.x, v.y, v.z); }
Vec3 TV(const VECTOR3& v) { return Vec3{v.x, v.y, v.z}; }
double StepTo(double v, double t, double d) { return v < t ? (std::min)(t, v + d) : (std::max)(t, v - d); }

const double kDoorRate = 0.1;          // doors: 10 s
const double kLiftSpeed = 1.2;         // m/s of the fork heads
const double kWindowX = 3.0, kWindowZ = 5.0, kWindowDeg = 15.0;  // capture window under the door
const double kBelly = -9.3;            // a cassette to be taken must be below the belly

int Column(int slot) { return slot & 1; }  // 0 starboard (slots 0, 2), 1 port (slots 1, 3)
double ColumnX(int c) { return m::kTrapXY[c][0]; }

ATTACHMENTHANDLE ContainerAttach(OBJHANDLE h) {
    VESSEL* v = oapiGetVesselInterface(h);
    return v && v->AttachmentCount(true) > 0 ? v->GetAttachmentHandle(true, 0) : nullptr;
}

}  // namespace

void Tantra::DefinePort() {
    for (int c = 0; c < 2; ++c) {
        liftY_[c] = liftYT_[c] = m::kLiftY0;
        grip_[c] = CreateAttachment(false, _V(ColumnX(c), m::kLiftY0, TrapZ()), _V(0, -1, 0), _V(0, 0, 1), "TTRAP");
    }
}

// Hangar payloads: the lander lies on the cradle (its magnetic pusher puts it out through the top doors), three MPU stand
// on the floor platform and beside it. A child vessel attaches with the ID "TLANDER" or "TMPU". MPU 1 and 2 ride the
// platform down to the ground (Shift+O); MPU 3 waits beside it for the second run.
void Tantra::DefineHangar() {
    // (2026-10-09) «Грань»: its mesh origin (TLANDER) at s 106.41, its bottom on the deck; the maglev lift moves the point
    hangarAtt_[0] = CreateAttachment(false, _V(0, m::kCradleTopY + m::kLanderOriginUp, Zf(m::kLanderS)), _V(0, 1, 0), _V(0, 0, 1), "TLANDER");
    for (int i = 0; i < 3; ++i)
        hangarAtt_[1 + i] = CreateAttachment(false, _V(m::kMpuX[i], m::kHangarDeckY, Zf(m::kHangarMidS)), _V(0, 1, 0), _V(0, 0, 1), "TMPU");
    hangarAttRov_ = -1.0;
    UpdateHangarAttach();
}

void Tantra::UpdateHangarAttach() {
    if (std::fabs(rovers_ - hangarAttRov_) < 1e-4) return;
    hangarAttRov_ = rovers_;
    const double y = m::kHangarDeckY + rovers_ * m::kRoverDrop;
    for (int i = 0; i < 2; ++i)
        if (hangarAtt_[1 + i]) SetAttachmentParams(hangarAtt_[1 + i], _V(m::kMpuX[i], y, Zf(m::kHangarMidS)), _V(0, 1, 0), _V(0, 0, 1));
}

// (2026-10-09) The lander's maglev lift (tantra-maglev-launch): the round superconducting blocks in the deck and the aft bulkhead
// act on the lander's own coils (its 6 lift cups below, its 2 marching cups behind). 0..1: up 13 m over the skin; 1..2: it turns
// about its stern to the 70 deg stele (nose up, forward); the throw runs along the stele axis. Back: a lander that comes into the
// field at the stele (2 m, 0.6 m/s) is taken and can be lowered.
namespace {
const double kLiftRise = 1.0 / 13.0, kLiftTurn = 1.0 / 14.0;   // per second: 1 m/s up, 5 deg/s over
}

void Tantra::UpdateLanderLift(double dt) {
    if (!hangarAtt_[0]) return;
    const double t = hangar_ >= 0.99 ? landerLiftT_ : 0.0;              // the doors shut: it stays (or goes) down
    const double rate = (landerLift_ < 1.0 || (landerLift_ == 1.0 && t < 1.0)) ? kLiftRise : kLiftTurn;
    landerLift_ = StepTo(landerLift_, t, rate * dt);
    const double r = (std::min)(1.0, landerLift_) * m::kLanderRise;
    const double th = (std::max)(0.0, landerLift_ - 1.0) * m::kLanderSteleDeg * RAD;
    const double c = std::cos(th), s = std::sin(th);
    const double oy = m::kLanderOriginUp, oz = m::kLanderS - m::kLanderSternS;   // the origin from the pivot (the stern on the deck)
    const VECTOR3 pv = _V(0, m::kCradleTopY + r, Zf(m::kLanderSternS));
    SetAttachmentParams(hangarAtt_[0], pv + _V(0, oy * c + oz * s, -oy * s + oz * c), _V(0, c, -s), _V(0, s, c));
    // taking it back at the stele
    if (landerLift_ >= 2.0 - 1e-6 && !GetAttachmentStatus(hangarAtt_[0])) {
        VECTOR3 ap, ad, ar;
        GetAttachmentParams(hangarAtt_[0], ap, ad, ar);
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            VESSEL* v = oapiGetVesselInterface(h);
            if (!v || h == GetHandle()) continue;
            for (DWORD k = 0; k < v->AttachmentCount(true); ++k) {
                ATTACHMENTHANDLE a = v->GetAttachmentHandle(true, k);
                if (_stricmp(v->GetAttachmentId(a), "TLANDER") != 0 || v->GetAttachmentStatus(a)) continue;
                VECTOR3 cp, cd, cr, g, loc, vr;
                v->GetAttachmentParams(a, cp, cd, cr);
                v->Local2Global(cp, g);
                Global2Local(g, loc);
                v->GetRelativeVel(GetHandle(), vr);
                if (length(loc - ap) < 2.0 && length(vr) < 0.6 && AttachChild(h, hangarAtt_[0], a))
                    Message("«Грань» в поле подъёмника - захвачена на стеле", "«Грань» in the lift's field - taken at the stele");
            }
        }
    }
}

void Tantra::ActLanderLift() {
    if (hangar_ < 0.99) { Message("Подъёмник «Грани»: сначала открыть ангар", "Lander lift: open the hangar first"); return; }
    landerLiftT_ = landerLiftT_ > 1.0 ? 0.0 : 2.0;
    Message(landerLiftT_ > 1.0 ? "Подъёмник: «Грань» вверх и на стелу 70°" : "Подъёмник: «Грань» на палубу",
            landerLiftT_ > 1.0 ? "Lift: the lander up to the 70 deg stele" : "Lift: the lander down to the deck");
}

void Tantra::ActLanderLaunch(bool emergency) {
    OBJHANDLE ch = hangarAtt_[0] ? GetAttachmentStatus(hangarAtt_[0]) : nullptr;
    if (!ch) { Message("На подъёмнике нет «Грани»", "No lander on the lift"); return; }
    if (landerLift_ < 2.0 - 1e-6) { Message("Выброс только со стелы (ГРАНЬ - вверх)", "Throw from the stele only (lift it up)"); return; }
    const double now = oapiGetSysTime();
    const bool air = GetAtmDensity() > 1e-5;
    if (emergency && now - launchArm_ > 3.0) {   // the emergency throw: armed by the first press, the second within 3 s throws
        launchArm_ = now;
        Message("АВАРИЙНЫЙ ВЫБРОС: подтвердите в течение 3 с", "EMERGENCY THROW: confirm within 3 s");
        return;
    }
    launchArm_ = -99.0;
    const double strokeA = (emergency ? prm_.landerEmergencyG : prm_.landerG) * tantra::G0;
    const double v = air || emergency ? std::sqrt(2.0 * strokeA * m::kLanderRise) : prm_.landerSpaceV;
    VECTOR3 ap, ad, ar, axis;
    GetAttachmentParams(hangarAtt_[0], ap, ad, ar);                    // ar: the lander's forward = the stele axis
    GlobalRot(ar, axis);
    DetachChild(hangarAtt_[0], 0.0);
    VESSEL* lv = oapiGetVesselInterface(ch);
    if (lv) {
        VESSELSTATUS2 vs;
        std::memset(&vs, 0, sizeof vs);
        vs.version = 2;
        lv->GetStatusEx(&vs);
        vs.status = 0;
        vs.flag = 0;
        vs.nfuel = 0; vs.fuel = nullptr; vs.nthruster = 0; vs.thruster = nullptr; vs.ndockinfo = 0; vs.dockinfo = nullptr;
        vs.rvel += axis * v;
        lv->DefSetStateEx(&vs);
    }
    if (emergency) Message("АВАРИЙНЫЙ ВЫБРОС «Грани»: %.0f g, %.0f м/с по оси стелы", "EMERGENCY lander throw: %.0f g, %.0f m/s along the stele",
                           prm_.landerEmergencyG, v);
    else Message("Выброс «Грани»: %.1f м/с по оси стелы", "Lander throw: %.1f m/s along the stele", v);
}

void Tantra::UpdateEmptyMass() {
    int n = 0;
    for (bool p : trapPresent_) n += p ? 1 : 0;
    SetEmptyMass(prm_.dryMass + n * prm_.trapStructMass);
}

void Tantra::SetTrapPresent(int slot, bool on) {
    trapPresent_[slot] = on;
    if (!on) SetPropellantMass(trap_[slot], 0.0);
    UpdateEmptyMass();
    SelectActiveTrap();
}

int Tantra::NextLoadSlot() const {
    static const int order[4] = {2, 3, 0, 1};  // upper first: it passes through the empty lower slot
    for (int s : order) {
        if (trapPresent_[s] || (s >= 2 && trapPresent_[s - 2])) continue;
        return s;
    }
    return -1;
}

int Tantra::NextDropSlot() const {
    static const int order[4] = {0, 1, 2, 3};  // lower first
    for (int s : order) {
        if (!trapPresent_[s] || (s >= 2 && trapPresent_[s - 2])) continue;
        return s;
    }
    return -1;
}

// A free TantraTrap cassette in the capture window under the door of column c.
OBJHANDLE Tantra::FindContainer(int c, Vec3& centre) const {
    MATRIX3 rs;
    GetRotationMatrix(rs);
    for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
        OBJHANDLE h = oapiGetVesselByIndex(i);
        VESSEL* v = oapiGetVesselInterface(h);
        if (!v || h == GetHandle() || _stricmp(v->GetClassName(), "TantraTrap") != 0) continue;
        ATTACHMENTHANDLE a = ContainerAttach(h);
        if (!a || v->GetAttachmentStatus(a)) continue;
        VECTOR3 gpos, loc;
        oapiGetGlobalPos(h, &gpos);
        Global2Local(gpos, loc);
        if (std::fabs(loc.x - ColumnX(c)) > kWindowX || std::fabs(loc.z - TrapZ()) > kWindowZ || loc.y > kBelly) continue;
        if (m::kLiftY0 - loc.y > m::kLiftTravel) continue;  // lower than the heads can go
        MATRIX3 rc;
        v->GetRotationMatrix(rc);
        const VECTOR3 axis = tmul(rs, mul(rc, _V(0, 0, 1)));  // container axis in the ship frame
        if (std::fabs(axis.z) < std::cos(kWindowDeg * RAD)) continue;
        centre = TV(loc);
        return h;
    }
    return nullptr;
}

void Tantra::ActPortLoad() {
    if (portStep_ != PortStep::Idle) { Message("Порт анамезона занят", "Anamezon port busy"); return; }
    const int slot = NextLoadSlot();
    if (slot < 0) { Message("Все гнёзда ловушек заняты", "All trap slots are occupied"); return; }
    Vec3 c;
    container_ = FindContainer(Column(slot), c);
    if (!container_) {
        Message("Под люком %s нет кассеты (окно: ±3 м вбок, ±5 м вдоль, перекос до 15°)",
                "No cassette under the %s door (window: 3 m side, 5 m along, 15 deg)",
                Column(slot) ? L("левым", "port") : L("правым", "starboard"));
        return;
    }
    portLoading_ = true;
    portSlot_ = slot;
    bayDoorsT_ = 1.0;
    portStep_ = PortStep::Open;
    Message("Порт анамезона: приём кассеты в гнездо %d", "Anamezon port: taking a cassette into slot %d", slot + 1);
}

void Tantra::ActPortDrop() {
    if (portStep_ != PortStep::Idle) { Message("Порт анамезона занят", "Anamezon port busy"); return; }
    const int slot = NextDropSlot();
    if (slot < 0) { Message("Ловушек на борту нет", "No traps aboard"); return; }
    portLoading_ = false;
    portSlot_ = slot;
    bayDoorsT_ = 1.0;
    portStep_ = PortStep::Open;
    Message("Порт анамезона: выгрузка ловушки %d", "Anamezon port: handing out trap %d", slot + 1);
}

void Tantra::ActPortLift() {
    // T9: lying on the blades the hull axis stands 31.6 m up - that is the loading height; nothing to lift.
    if (carriage_.AtLoadHeight()) Message("Высота загрузки: корабль лёжа на лопастях уже на ней (ось 31,6 м)", "Loading height: lying on the blades the ship is already there (axis 31.6 m)");
    else Message("Высота загрузки: только лёжа на грунте, шасси выпущено", "Loading height: lying on the ground, gear down");
}

void Tantra::ActPortStop() {
    if (container_) {
        Message("Кассета на вилках: операция доводится до конца", "Cassette on the forks: the operation completes");
        return;
    }
    portStep_ = PortStep::Home;
    Message("Порт анамезона: вилки домой", "Anamezon port: heads home");
}

void Tantra::UpdatePort(double simdt) {
    const double dt = (std::min)(simdt, 0.2);
    bayDoors_ = StepTo(bayDoors_, bayDoorsT_, kDoorRate * dt);
    const int s = portSlot_, c = s >= 0 ? Column(s) : 0;
    bool there = true;
    for (int k = 0; k < 2; ++k) {
        liftY_[k] = StepTo(liftY_[k], liftYT_[k], kLiftSpeed * dt);
        if (liftY_[k] != liftYT_[k]) there = false;
    }
    switch (portStep_) {
        case PortStep::Idle:
        case PortStep::Hold:
            break;
        case PortStep::Open:
            if (bayDoors_ < 1.0) break;
            if (portLoading_) {
                Vec3 ctr;
                if (!container_ || !oapiIsVessel(container_) || !FindContainer(c, ctr)) {
                    container_ = nullptr;
                    portStep_ = PortStep::Home;
                    break;
                }
                liftYT_[c] = ctr.y;  // forks down to the trunnions
                portStep_ = PortStep::Down;
            } else {
                liftYT_[c] = m::kTrapXY[s][1];  // forks to the slot
                portStep_ = PortStep::Engage;
            }
            break;
        case PortStep::Down:  // loading: forks at the trunnions -> lock
            if (!there) break;
            if (!container_ || !oapiIsVessel(container_) || !AttachChild(container_, grip_[c], ContainerAttach(container_))) {
                Message("Замки не сомкнулись", "Locks did not close");
                container_ = nullptr;
                portStep_ = PortStep::Home;
                break;
            }
            Message("Вилки замкнуты на цапфах кассеты", "Forks locked on the cassette trunnions");
            liftYT_[c] = m::kTrapXY[s][1];
            portStep_ = PortStep::Up;
            break;
        case PortStep::Up: {  // loading: into the slot; the cassette joins the ship's field circuit
            if (!there) break;
            VESSEL* cv = oapiGetVesselInterface(container_);
            double mass = 0.0;
            if (cv && cv->GetPropellantCount() > 0) mass = cv->GetPropellantMass(cv->GetPropellantHandleByIndex(0));
            DetachChild(grip_[c]);
            oapiDeleteVessel(container_, GetHandle());
            container_ = nullptr;
            trapPresent_[s] = true;
            UpdateEmptyMass();
            SetPropellantMass(trap_[s], (std::min)(mass, prm_.trapFuelMass));
            SelectActiveTrap();
            Message("Ловушка %d в гнезде, разъём замкнут на контур поля", "Trap %d seated, connector on the field circuit", s + 1);
            portStep_ = PortStep::Home;
            break;
        }
        case PortStep::Engage: {  // drop: forks at the slot -> the trap leaves the circuit on its own store
            if (!there) break;
            VESSELSTATUS2 vs;
            std::memset(&vs, 0, sizeof vs);
            vs.version = 2;
            GetStatusEx(&vs);
            vs.status = 0;
            vs.flag = 0;
            vs.nfuel = 0;
            vs.fuel = nullptr;
            vs.nthruster = 0;
            vs.thruster = nullptr;
            vs.ndockinfo = 0;
            vs.dockinfo = nullptr;
            VECTOR3 gpos, bpos;
            Local2Global(_V(ColumnX(c), liftY_[c], TrapZ()), gpos);
            oapiGetGlobalPos(vs.rbody, &bpos);
            vs.rpos = gpos - bpos;
            GetRelativeVel(vs.rbody, vs.rvel);
            vs.vrot = _V(0, 0, 0);
            static int serial = 0;
            char name[64];
            do std::snprintf(name, sizeof name, "TantraTrap%d", ++serial);
            while (oapiGetVesselByName(name));
            OBJHANDLE h = oapiCreateVesselEx(name, "TantraTrap", &vs);
            if (!h) { portStep_ = PortStep::Home; break; }
            VESSEL* cv = oapiGetVesselInterface(h);
            if (cv && cv->GetPropellantCount() > 0)
                cv->SetPropellantMass(cv->GetPropellantHandleByIndex(0), GetPropellantMass(trap_[s]));
            AttachChild(h, grip_[c], ContainerAttach(h));
            container_ = h;
            SetTrapPresent(s, false);
            Message("Ловушка %d на собственном накопителе поля", "Trap %d on its own field store", s + 1);
            // Down to the ground under the door (landed) or clear of the belly (space).
            liftYT_[c] = GroundContact() ? (std::max)(m::kLiftY0 - m::kLiftTravel, -carriage_.Pose().trunnionH + m::kCassW / 2 + 0.05)
                                         : m::kTrapMouthY - 4.0;
            portStep_ = PortStep::Place;
            break;
        }
        case PortStep::Place:
            if (!there) break;
            DetachChild(grip_[c], GroundContact() ? 0.0 : 0.3);
            container_ = nullptr;
            Message("Ловушка %d выгружена", "Trap %d handed out", s + 1);
            portStep_ = PortStep::Home;
            break;
        case PortStep::Home:
            for (int k = 0; k < 2; ++k) liftYT_[k] = m::kLiftY0;
            if (there && liftY_[0] == m::kLiftY0 && liftY_[1] == m::kLiftY0) portStep_ = PortStep::Close;
            break;
        case PortStep::Close:
            bayDoorsT_ = 0.0;
            if (bayDoors_ <= 0.0) portStep_ = PortStep::Idle;
            break;
    }
    for (int k = 0; k < 2; ++k)
        if (grip_[k]) SetAttachmentParams(grip_[k], _V(ColumnX(k), liftY_[k], TrapZ()), _V(0, -1, 0), _V(0, 0, 1));
}

// Trap columns are fixed in the mesh; the vessel frame follows the CG.
double Tantra::TrapZ() const { return tantra::mesh::kTrapZ + MeshDZ(); }

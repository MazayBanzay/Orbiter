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
    hangarAtt_[0] = CreateAttachment(false, _V(0, m::kCradleTopY, Zf(m::kHangarMidS)), _V(0, 1, 0), _V(0, 0, 1), "TLANDER");
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

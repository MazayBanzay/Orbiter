#include "TantraCrew.h"

#include <cstdio>
#include <cstring>

namespace {
const char* const kCrewClass = "OrbiterCrew\\Astronavigator";  // the only crew figure so far
const double kBoardRadius = 6.0;                              // m around the lift foot
}  // namespace

void TantraCrew::AddMember(const char* name, int age, int pulse, int weight, const char* role) {
    if (static_cast<int>(members_.size()) >= seats_) return;
    Member m;
    m.name = name;
    m.role = role;
    m.age = age;
    m.pulse = pulse;
    m.weight = weight;
    members_.push_back(m);
}

// CREW <aboard> <age> <pulse> <weight> <role> <vessel|-> <name...>
void TantraCrew::Save(FILEHANDLE scn) const {
    char buf[256];
    for (const Member& m : members_) {
        std::snprintf(buf, sizeof buf, "%d %d %d %d %s %s %s", m.aboard ? 1 : 0, m.age, m.pulse, m.weight, m.role.c_str(),
                      m.vessel.empty() ? "-" : m.vessel.c_str(), m.name.c_str());
        oapiWriteScenario_string(scn, const_cast<char*>("CREW"), buf);
    }
    oapiWriteScenario_int(scn, const_cast<char*>("AIRLOCK"), airlockOpen_ ? 1 : 0);
}

bool TantraCrew::LoadLine(const char* line) {
    if (!_strnicmp(line, "AIRLOCK", 7)) {
        int on = 0;
        std::sscanf(line + 7, "%d", &on);
        airlockOpen_ = on != 0;
        return true;
    }
    if (_strnicmp(line, "CREW", 4) != 0) return false;
    if (!loadedFromScenario_) {  // the scenario roster replaces the default one
        members_.clear();
        loadedFromScenario_ = true;
    }
    Member m;
    int aboard = 1, n = 0;
    char role[64] = {}, vessel[64] = {};
    if (std::sscanf(line + 4, "%d %d %d %d %63s %63s %n", &aboard, &m.age, &m.pulse, &m.weight, role, vessel, &n) < 6) return true;
    m.aboard = aboard != 0;
    m.role = role;
    m.vessel = std::strcmp(vessel, "-") ? vessel : "";
    m.name = line + 4 + n;
    while (!m.name.empty() && (m.name.back() == ' ' || m.name.back() == '\r' || m.name.back() == '\n')) m.name.pop_back();
    members_.push_back(m);
    return true;
}

std::string TantraCrew::VesselName(const std::string& name) {
    std::string v = name;
    for (char& c : v)
        if (c == ' ') c = '_';
    std::string out = v;
    for (int i = 2; oapiGetVesselByName(const_cast<char*>(out.c_str())); ++i) out = v + "_" + std::to_string(i);
    return out;
}

TantraCrew::EvaResult TantraCrew::Eva(int slot) {
    const Member* sm = Slot(slot);
    if (!sm) return EvaResult::NoOne;
    if (!airlockOpen_) return EvaResult::AirlockClosed;
    if (!ship_->GroundContact()) return EvaResult::NotLanded;
    Member& m = const_cast<Member&>(*sm);

    // Stand the figure at the lift foot, facing along the ship.
    VECTOR3 gpos;
    ship_->Local2Global(foot_, gpos);
    OBJHANDLE body = ship_->GetSurfaceRef();
    double lng = 0, lat = 0, rad = 0, hdg = 0;
    oapiGlobalToEqu(body, gpos, &lng, &lat, &rad);
    oapiGetHeading(ship_->GetHandle(), &hdg);

    VESSELSTATUS2 vs;
    std::memset(&vs, 0, sizeof vs);
    vs.version = 2;
    vs.rbody = body;
    vs.status = 1;  // landed
    vs.surf_lng = lng;
    vs.surf_lat = lat;
    vs.surf_hdg = hdg;
    const std::string vname = VesselName(m.name);
    OBJHANDLE h = oapiCreateVesselEx(vname.c_str(), kCrewClass, &vs);
    if (!h) return EvaResult::Failed;
    m.aboard = false;
    m.vessel = vname;
    last_ = m.name;
    return EvaResult::Ok;
}

TantraCrew::Event TantraCrew::Process(double dt) {
    scanTimer_ -= dt;
    if (scanTimer_ > 0.0 || !airlockOpen_) return Event::None;
    scanTimer_ = 0.5;
    VECTOR3 gfoot;
    ship_->Local2Global(foot_, gfoot);
    for (Member& m : members_) {
        if (m.aboard || m.vessel.empty()) continue;
        OBJHANDLE h = oapiGetVesselByName(const_cast<char*>(m.vessel.c_str()));
        if (!h) continue;  // not in this simulation (e.g. deleted by the user)
        VECTOR3 p;
        oapiGetGlobalPos(h, &p);
        if (length(p - gfoot) > kBoardRadius) continue;
        if (oapiGetFocusObject() == h) oapiSetFocusObject(ship_->GetHandle());
        oapiDeleteVessel(h);
        m.aboard = true;
        m.vessel.clear();
        last_ = m.name;
        return Event::Returned;
    }
    return Event::None;
}

const TantraCrew::Member* TantraCrew::Slot(int slot) const {
    int i = 0;
    for (const Member& m : members_) {
        if (!m.aboard) continue;
        if (i++ == slot) return &m;
    }
    return nullptr;
}

int TantraCrew::Total() const {
    int n = 0;
    for (const Member& m : members_) n += m.aboard ? 1 : 0;
    return n;
}
const char* TantraCrew::Name(int slot) const { const Member* m = Slot(slot); return m ? m->name.c_str() : ""; }
const char* TantraCrew::Role(int slot) const { const Member* m = Slot(slot); return m ? m->role.c_str() : ""; }
int TantraCrew::Age(int slot) const { const Member* m = Slot(slot); return m ? m->age : 0; }
int TantraCrew::Pulse(int slot) const { const Member* m = Slot(slot); return m ? m->pulse : 0; }

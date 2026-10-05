#include "TantraCrew.h"

#include <cstdio>
#include <cstring>

namespace {
const double kBoardRadius = 6.0;   // m around the lift foot (only with an older OrbiterCrew: no F - enter)
const double kLiftReach = 3.0;     // m: the lift is within reach for F
enum { kItemLift = 100, kItemCall = 101 };
}  // namespace

void TantraCrew::Init(VESSEL* ship, const VECTOR3& liftFoot, int seats) {
    ship_ = ship; foot_ = liftFoot; seats_ = seats;
    api_.Load();
    if (api_.Ok() && api_.RegisterInterior) {
        fns_ = OcInterior{};
        fns_.Count = &TantraCrew::ItemCount;
        fns_.Item = &TantraCrew::ItemAt;
        fns_.Use = &TantraCrew::Use;
        api_.RegisterInterior(ship_->GetHandle(), &fns_, this);
        registered_ = true;
    }
}

TantraCrew::~TantraCrew() {
    if (registered_ && api_.UnregisterInterior && ship_) api_.UnregisterInterior(ship_->GetHandle());
    api_.Unload();
}

int TantraCrew::ItemCount(void*) { return 2; }

int TantraCrew::ItemAt(void* ctx, int i, OcItem* out) {
    TantraCrew* c = static_cast<TantraCrew*>(ctx);
    if (i == 1 && out) {                                                 // the cabin is up: call it from the foot of the lift
        if (!c->callOn_) return 0;
        *out = OcItem{};
        out->id = kItemCall; out->kind = OC_LIFT; out->pos = c->callPos_; out->dir = _V(0, 0, 1); out->radius = kLiftReach;
        std::snprintf(out->label, sizeof out->label, "%s", "вызвать кабину лифта");
        return 1;
    }
    if (i != 0 || !out) return 0;
    *out = OcItem{};
    out->id = kItemLift;
    out->kind = OC_LIFT;
    out->pos = c->foot_;
    out->dir = _V(0, 0, 1);
    out->radius = kLiftReach;
    std::snprintf(out->label, sizeof out->label, "%s", c->airlockOpen_ ? "войти: лифт «Тантры»" : "лифт «Тантры» (шлюз закрыт)");
    return 1;
}

// F at the lift: the person boards (the body leaves the world, the person is aboard)
void TantraCrew::Use(void* ctx, int id, int personId) {
    TantraCrew* c = static_cast<TantraCrew*>(ctx);
    if (id == kItemCall) { if (c->callOn_ && c->callFn_) c->callFn_(c->callCtx_); return; }
    if (id != kItemLift || !c->airlockOpen_ || !c->api_.Ok()) return;
    OcInfo in;
    if (!c->api_.Info(personId, &in) || in.where != 1 || !in.vessel) return;
    // the user's rule: going in, the camera stays with the person - the same body walks in and stands inside
    auto board = [&]() -> bool {
        if (c->hasArrival_ && c->api_.EnterShip)
            return c->api_.EnterShip && c->api_.EnterShip(in.vessel, c->ship_->GetHandle(), &c->arrival_, &c->arrivalDir_) != 0;
        return c->api_.Board(in.vessel, c->ship_->GetHandle()) != 0;
    };
    for (Member& m : c->members_) {
        if (m.aboard) continue;
        if (m.person != personId && (m.vessel.empty() || oapiGetVesselByName(const_cast<char*>(m.vessel.c_str())) != in.vessel)) continue;
        if (!board()) return;
        m.person = personId; m.aboard = true; m.vessel.clear();
        c->Refresh(m);
        c->last_ = m.name;
        c->returned_ = true;
        return;
    }
    // not one of ours (a person from elsewhere): takes a free seat
    if (c->Total() >= c->seats_ || !board()) return;
    Member m; m.person = personId; m.aboard = true;
    c->members_.push_back(m);
    c->Refresh(c->members_.back());
    c->last_ = c->members_.back().name;
    c->returned_ = true;
}

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

void TantraCrew::Link(Member& m) {
    if (m.person || !api_.Ok()) return;
    if (m.aboard && !m.vessel.empty()) {   // aboard, walking inside: the person is his or her body's
        if (OBJHANDLE h = oapiGetVesselByName(const_cast<char*>(m.vessel.c_str()))) m.person = api_.PersonOfBody(h);
    } else if (m.aboard) {   // a member from an older scenario (or the default roster): becomes a person aboard
        m.person = api_.CreatePerson(m.name.c_str(), m.role.c_str(), m.age, m.weight);
        api_.SetAboard(m.person, ship_->GetHandle());
    } else if (!m.vessel.empty()) {   // outside: the person is his or her body's
        if (OBJHANDLE h = oapiGetVesselByName(const_cast<char*>(m.vessel.c_str()))) m.person = api_.PersonOfBody(h);
    }
}

void TantraCrew::Refresh(Member& m) const {
    OcInfo in;
    if (!m.person || !api_.Ok() || !api_.Info(m.person, &in)) return;
    m.name = in.name; m.role = in.role; m.age = static_cast<int>(in.age + 0.5); m.pulse = static_cast<int>(in.pulse + 0.5);
    m.weight = static_cast<int>(in.massKg + 0.5);
}

// CREW <aboard> <age> <pulse> <weight> <role> <vessel|-> <name...>; for a person aboard, then his or her own block
// (OC_PERSON ... OC_END, written by OrbiterCrew)
void TantraCrew::Save(FILEHANDLE scn) const {
    char buf[256];
    for (Member& m : members_) {
        Refresh(m);
        // aboard = 2: walking inside the ship with the body (the body's own block keeps the person)
        OcInfo in{};
        const bool inside = m.aboard && m.person && api_.Ok() && api_.Info(m.person, &in) && in.where == 3 && in.vessel;
        if (inside) m.vessel = oapiGetVesselInterface(in.vessel)->GetName();
        else if (m.aboard) m.vessel.clear();
        std::snprintf(buf, sizeof buf, "%d %d %d %d %s %s %s", inside ? 2 : m.aboard ? 1 : 0, m.age, m.pulse, m.weight, m.role.c_str(),
                      m.vessel.empty() ? "-" : m.vessel.c_str(), m.name.c_str());
        oapiWriteScenario_string(scn, const_cast<char*>("CREW"), buf);
        if (m.aboard && !inside && m.person && api_.Ok()) api_.SavePerson(m.person, scn);
    }
    oapiWriteScenario_int(scn, const_cast<char*>("AIRLOCK"), airlockOpen_ ? 1 : 0);
}

bool TantraCrew::LoadLine(const char* line) {
    if (inPerson_) {   // a person's own lines, for OrbiterCrew
        if (!_strnicmp(line, "OC_END", 6)) {
            inPerson_ = false;
            if (!members_.empty() && api_.Ok()) {
                Member& m = members_.back();
                m.person = api_.LoadPerson(personLines_.c_str());
                api_.SetAboard(m.person, ship_->GetHandle());
            }
        } else {
            personLines_ += line;
            personLines_ += '\n';
        }
        return true;
    }
    if (!_strnicmp(line, "OC_PERSON", 9)) { inPerson_ = true; personLines_.clear(); return true; }
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
    if (!api_.Ok()) return EvaResult::NoCrewModule;
    if (!airlockOpen_) return EvaResult::AirlockClosed;
    if (!ship_->GroundContact()) return EvaResult::NotLanded;
    Member& m = const_cast<Member&>(*sm);
    Link(m);
    if (!m.person) return EvaResult::Failed;

    // Stand the person at the lift foot, facing along the ship.
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
    vs.arot.x = 10.0;   // no attitude given: Orbiter stands the body on its own touchdown points (arot <= 4 = a rotation)
    vs.surf_lng = lng;
    vs.surf_lat = lat;
    vs.surf_hdg = hdg;
    Refresh(m);
    const std::string vname = VesselName(m.name);
    OBJHANDLE h = api_.Disembark(m.person, vname.c_str(), &vs);   // the body of that very person
    if (!h) return EvaResult::Failed;
    m.aboard = false;
    m.vessel = vname;
    last_ = m.name;
    return EvaResult::Ok;
}

TantraCrew::EvaResult TantraCrew::EvaPerson(int personId) {
    if (!api_.Ok() || !api_.ExitTo) return EvaResult::NoCrewModule;
    if (!airlockOpen_) return EvaResult::AirlockClosed;
    if (!ship_->GroundContact()) return EvaResult::NotLanded;
    OcInfo in{};
    if (!personId || !api_.Info(personId, &in)) return EvaResult::Failed;
    Member* mm = nullptr;
    for (Member& m : members_) if (m.person == personId) mm = &m;
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
    vs.arot.x = 10.0;   // no attitude given: Orbiter stands the body on its own touchdown points (arot <= 4 = a rotation)
    vs.surf_lng = lng;
    vs.surf_lat = lat;
    vs.surf_hdg = hdg;
    const std::string vname = VesselName(mm ? mm->name : std::string(in.name));
    OBJHANDLE h = api_.ExitTo(personId, vname.c_str(), &vs);       // inside: the body leaves the ship and stands at the foot
    if (!h) return EvaResult::Failed;
    if (mm) {
        mm->aboard = false;
        VESSEL* v = oapiGetVesselInterface(h);
        mm->vessel = v ? v->GetName() : vname;
        last_ = mm->name;
    } else last_ = in.name;
    return EvaResult::Ok;
}

int TantraCrew::PersonOf(int slot) {
    const Member* sm = Slot(slot);
    if (!sm || !api_.Ok()) return 0;
    Member& m = const_cast<Member&>(*sm);
    Link(m);
    OcInfo in{};
    if (!m.person || !api_.Info(m.person, &in) || in.where != 2) return 0;
    return m.person;
}

TantraCrew::Event TantraCrew::Process(double dt) {
    scanTimer_ -= dt;
    if (scanTimer_ > 0.0) return Event::None;
    scanTimer_ = 0.5;
    for (Member& m : members_) Link(m);   // older scenarios, outside members found again after loading
    // a ship without a crew in its scenario (made in the scenario editor): its astronavigator stands in the cabin behind
    // the seats from the start - her body there, the focus and the camera hers (OrbiterCrew ocEnterInterior)
    if (!loadedFromScenario_ && hasDefPlace_ && placeTries_ < 10 && api_.EnterInterior && !members_.empty() && members_[0].aboard &&
        members_[0].person) {
        ++placeTries_;
        if (api_.EnterInterior(members_[0].person, &defPos_, &defDir_)) {
            placeTries_ = 10;
            oapiWriteLogV("Tantra crew: %s stands in the cabin (a ship without a crew in its scenario)", members_[0].name.c_str());
        }
    }
    if (returned_) { returned_ = false; return Event::Returned; }   // boarded by F at the lift
    if (registered_) return Event::None;                           // entering is the person's own action (F)
    if (!airlockOpen_ || !api_.Ok()) return Event::None;
    VECTOR3 gfoot;
    ship_->Local2Global(foot_, gfoot);
    for (Member& m : members_) {
        if (m.aboard || m.vessel.empty()) continue;
        OBJHANDLE h = oapiGetVesselByName(const_cast<char*>(m.vessel.c_str()));
        if (!h) continue;  // not in this simulation (e.g. deleted by the user)
        VECTOR3 p;
        oapiGetGlobalPos(h, &p);
        if (length(p - gfoot) > kBoardRadius) continue;
        const int id = api_.Board(h, ship_->GetHandle());   // the body leaves the world, the person is aboard
        if (!id) continue;
        m.person = id;
        m.aboard = true;
        m.vessel.clear();
        Refresh(m);
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
const char* TantraCrew::Name(int slot) const { const Member* m = Slot(slot); if (m) Refresh(const_cast<Member&>(*m)); return m ? m->name.c_str() : ""; }
const char* TantraCrew::Role(int slot) const { const Member* m = Slot(slot); return m ? m->role.c_str() : ""; }
int TantraCrew::Age(int slot) const { const Member* m = Slot(slot); return m ? m->age : 0; }
int TantraCrew::Pulse(int slot) const { const Member* m = Slot(slot); if (m) Refresh(const_cast<Member&>(*m)); return m ? m->pulse : 0; }

// The focused body is a person inside this ship's interior: the ship then sounds as heard from inside (muffled).
bool TantraCrew::ListenerInside(OBJHANDLE focus) const {
    if (!focus || !ship_ || !api_.Ok() || !api_.PersonOfBody) return false;
    const int id = api_.PersonOfBody(focus);
    if (!id) return false;
    if (api_.ShipOf) return api_.ShipOf(id) == ship_->GetHandle();
    OcInfo in{};
    return api_.Info && api_.Info(id, &in) && in.where == 3;
}

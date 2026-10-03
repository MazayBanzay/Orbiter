// TantraCrew: the ship side of the crew for Orbiter 2016 (replaces UMmu).
// The people are OrbiterCrew's (include/OrbiterCrewApi.h): the ship does not own them and does not copy them. Aboard,
// a person has no body in the world and OrbiterCrew keeps him or her whole (organism, suit, pack); the ship saves its
// people in its own scenario block. Going out, the body of that very person is made at the foot of the airlock lift;
// coming back, the body is taken out of the world and the person is aboard again.
// The ship's roster line (CREW) is kept for the seats and as the seed of people from older scenarios.
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "../../../OrbiterCrew/include/OrbiterCrewApi.h"

#include <string>
#include <vector>

class TantraCrew {
public:
    enum class Event { None, Returned };
    enum class EvaResult { Ok, NoOne, AirlockClosed, NotLanded, Failed, NoCrewModule };

    ~TantraCrew();
    void Init(VESSEL* ship, const VECTOR3& liftFoot, int seats);
    void SetLiftFoot(const VECTOR3& p) { foot_ = p; }  // follows the gear (ground height)
    // where a person coming up the lift stands inside (interior frame): set by the interior; without it the person is
    // taken aboard without a body as before
    void SetArrival(const VECTOR3& pos, const VECTOR3& dir) { arrival_ = pos; arrivalDir_ = dir; hasArrival_ = true; }
    void AddMember(const char* name, int age, int pulse, int weight, const char* role);

    void Save(FILEHANDLE scn) const;
    bool LoadLine(const char* line);   // true if the line was a crew line

    Event Process(double dt);          // picks up members standing at the lift
    // the crew's own items for the interior provider (one OcInterior per ship: the provider appends these to its list
    // and passes F on them here). Ids from 100 up.
    int CrewItemCount() const { return 2; }
    // outside, at the foot of the lift while the cabin is up: F calls the cabin down (the ship's sequence)
    void SetLiftCall(const VECTOR3& pos, bool on, void (*fn)(void*), void* ctx) { callPos_ = pos; callOn_ = on; callFn_ = fn; callCtx_ = ctx; }
    int CrewItem(int i, OcItem* out) const { return ItemAt(const_cast<TantraCrew*>(this), i, out); }
    bool CrewUse(int id, int personId) { if (id < 100) return false; Use(this, id, personId); return true; }
    EvaResult Eva(int slot);
    EvaResult EvaPerson(int personId);   // a person walking inside (the lift cabin at the ground): the same body steps out at the foot of the lift
    // the OrbiterCrew person in this slot, if he or she is aboard without a body (can stand up into the interior); 0 if not
    int PersonOf(int slot);

    bool AirlockOpen() const { return airlockOpen_; }
    void SetAirlockOpen(bool open) { airlockOpen_ = open; }

    // Members aboard, by slot.
    int Total() const;
    const char* Name(int slot) const;
    const char* Role(int slot) const;
    int Age(int slot) const;
    int Pulse(int slot) const;
    const char* LastName() const { return last_.c_str(); }

private:
    struct Member {
        std::string name, role; int age = 30, pulse = 65, weight = 70; bool aboard = true; std::string vessel;
        int person = 0;                // OrbiterCrew person id (0: not linked yet)
    };
    const Member* Slot(int slot) const;
    static std::string VesselName(const std::string& name);
    void Link(Member& m);              // makes sure the member is an OrbiterCrew person
    void Refresh(Member& m) const;     // name, role, age, pulse from the person

    VESSEL* ship_ = nullptr;
    VECTOR3 foot_ = {};
    int seats_ = 20;
    mutable std::vector<Member> members_;
    bool loadedFromScenario_ = false;
    bool airlockOpen_ = false;
    double scanTimer_ = 0.0;
    std::string last_;
    OcApi api_;
    // the entrances OrbiterCrew shows to a person outside (F - enter); the interior walk is added to this later
    bool registered_ = false;
    bool returned_ = false;           // a person has boarded since the last Process
    VECTOR3 arrival_ = {}, arrivalDir_ = {0, 0, 1};
    bool hasArrival_ = false;
    OcInterior fns_{};
    static int ItemCount(void* ctx);
    static int ItemAt(void* ctx, int i, OcItem* out);
    static void Use(void* ctx, int id, int personId);
    VECTOR3 callPos_ = {}; bool callOn_ = false; void (*callFn_)(void*) = nullptr; void* callCtx_ = nullptr;
    bool inPerson_ = false;            // loading: inside an OC_PERSON block
    std::string personLines_;
};

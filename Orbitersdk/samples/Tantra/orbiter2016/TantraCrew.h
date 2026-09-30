// TantraCrew: the ship side of the crew for Orbiter 2016 (replaces UMmu). The roster lives in the
// ship's scenario; a member who goes out becomes an OrbiterCrew vessel (our own crew module) at the
// foot of the airlock lift, and comes back aboard when that vessel stands at the lift again.
// OrbiterCrew itself is not modified: the link is only its vessel class and the vessel name.
#pragma once
#define STRICT
#include "orbitersdk.h"

#include <string>
#include <vector>

class TantraCrew {
public:
    enum class Event { None, Returned };
    enum class EvaResult { Ok, NoOne, AirlockClosed, NotLanded, Failed };

    void Init(VESSEL* ship, const VECTOR3& liftFoot, int seats) { ship_ = ship; foot_ = liftFoot; seats_ = seats; }
    void SetLiftFoot(const VECTOR3& p) { foot_ = p; }  // follows the gear (ground height)
    void AddMember(const char* name, int age, int pulse, int weight, const char* role);

    void Save(FILEHANDLE scn) const;
    bool LoadLine(const char* line);   // true if the line was a crew line

    Event Process(double dt);          // picks up members standing at the lift
    EvaResult Eva(int slot);

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
    struct Member { std::string name, role; int age = 30, pulse = 65, weight = 70; bool aboard = true; std::string vessel; };
    const Member* Slot(int slot) const;
    static std::string VesselName(const std::string& name);

    VESSEL* ship_ = nullptr;
    VECTOR3 foot_ = {};
    int seats_ = 20;
    std::vector<Member> members_;
    bool loadedFromScenario_ = false;
    bool airlockOpen_ = false;
    double scanTimer_ = 0.0;
    std::string last_;
};

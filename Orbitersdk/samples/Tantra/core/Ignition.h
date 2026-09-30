// Tantra core: anamezon engine start-up sequence. No Orbiter dependencies.
//
// Efremov, braking scene: the handle is moved in steps -
//   1) green spirals in the boron-nitride cylinders: the chamber field,
//   2) the grey guide beam of K-particles,
//   3) the violet flash: anamezon starts to flow.
// Feed (thrust) is only possible once the field and the beam are up.
#pragma once

namespace tantra {

enum class IgnStage { Off = 0, Field = 1, Beam = 2, Feed = 3 };

class Ignition {
public:
    // Seconds each stage needs to come up.
    static constexpr double kFieldTime = 4.0;
    static constexpr double kBeamTime = 2.0;
    static constexpr double kFeedTime = 0.5;
    static constexpr double kDecayTime = 2.0;

    void StepUp();      // request next stage
    void SetTarget(IgnStage s) { target_ = s; }  // lever set straight to a position
    void Shutdown();    // request full stop
    void EmergencyCut();  // e.g. chamber power lost: instant stop

    // Advance the sequence. powerOk=false means the chamber field cannot be held.
    void Update(double dt, bool powerOk);

    IgnStage Stage() const { return stage_; }
    IgnStage Target() const { return target_; }
    bool Transitioning() const { return target_ != stage_; }
    bool FeedAvailable() const { return stage_ == IgnStage::Feed; }

    // 0..1 levels for visuals and sound.
    double FieldLevel() const { return field_; }
    double BeamLevel() const { return beam_; }
    double FeedLevel() const { return feed_; }

    // Persistence: stage only, levels are rebuilt from it.
    int SaveValue() const { return static_cast<int>(stage_); }
    void Load(int value);

private:
    IgnStage stage_ = IgnStage::Off;
    IgnStage target_ = IgnStage::Off;
    double field_ = 0.0, beam_ = 0.0, feed_ = 0.0;
};

const char* StageName(IgnStage s, bool russian);

}  // namespace tantra

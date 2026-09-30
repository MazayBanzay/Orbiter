// Tantra: anamezon exhaust visuals for Orbiter 2010 (D3D9Client).
// Maps core/ExhaustModel onto Orbiter primitives:
//   * beacons (camera-facing additive blobs) for the chamber mouths, the neck and
//     constrictions of the K-particle beam, and the internal-shock knots far aft;
//   * exhaust billboards with custom textures for the beam columns and the long glow;
//   * a point light that lights the stern, hull and ground.
#pragma once
#include "orbitersdk.h"

#include "../core/ExhaustModel.h"

class TantraExhaust {
public:
    static constexpr int kChambers = 4;
    static constexpr int kMaxKnots = 8;
    static constexpr int kMaxPackets = 20;
    static constexpr int kGlows = 5;      // long-glow billboard lengths
    static constexpr int kEmitters = 14;  // particle emitters along the axis, 25 m .. 205 km

    // mouths: chamber mouth positions (vessel frame); thrust along +z.
    TantraExhaust(VESSEL3* vessel, const VECTOR3 mouths[kChambers], const tantra::ExhaustSpec& spec);
    ~TantraExhaust();

    // fieldLevel/beamLevel from the ignition sequence, feed = pellet feed level (0 when
    // not flowing), airDensity around the ship [kg/m^3], jetPower = total jet power [W];
    // drag [N], airspeed and sound speed [m/s] drive the hull's own shock wake.
    void Update(double fieldLevel, double beamLevel, double feed, double airDensity, double jetPower, double drag,
                double airspeed, double soundSpeed);

    const tantra::ExhaustFrame& Frame() const { return frame_; }

private:
    // Integrates the air column along the beam from the stern (planet atmosphere model).
    tantra::AirPath TraceBeam() const;

    VESSEL3* v_;
    tantra::ExhaustModel model_;
    tantra::ExhaustFrame frame_;

    VECTOR3 mouth_[kChambers];
    VECTOR3 thrustDir_ = {0, 0, 1};

    // Per chamber: cup + neck + two constrictions.
    BEACONLIGHTSPEC bcn_[kChambers][4] = {};
    VECTOR3 bcnPos_[kChambers][4] = {};
    VECTOR3 bcnCol_[kChambers][4] = {};

    // Internal-shock knots on the ship axis.
    BEACONLIGHTSPEC knot_[kMaxKnots] = {};
    VECTOR3 knotPos_[kMaxKnots] = {};
    VECTOR3 knotCol_[kMaxKnots] = {};
    double sternZ_ = 0.0;

    // Pellet micro-explosions along the axis.
    BEACONLIGHTSPEC packet_[kMaxPackets] = {};
    VECTOR3 packetPos_[kMaxPackets] = {};
    VECTOR3 packetCol_[kMaxPackets] = {};

    // Billboards: grey guide beam, violet flow, long glow (per chamber).
    double lvGrey_[kChambers] = {}, lvViolet_[kChambers] = {}, lvLong_[kGlows] = {};
    VECTOR3 axisPos_ = {0, 0, 0};  // merged jet: on the ship axis at the stern
    double lvThread_[kChambers][kGlows] = {};  // guide beam thread in air
    SURFHANDLE texViolet_ = nullptr, texGrey_ = nullptr, texGlow_ = nullptr;
    SURFHANDLE texThread_ = nullptr;
    SURFHANDLE texColumn_ = nullptr, texPlasma_ = nullptr, texWake_ = nullptr, texDust_ = nullptr;  // particle textures

    // Atmosphere: plasma column from the stern, its light, and particle emitters
    // (hot plasma, shock wake, ejecta) at fixed distances along the axis.
    VECTOR3 fireLightPos_ = {0, 0, 0};
    LightEmitter* fireLight_ = nullptr;
    double lvFire_[kEmitters] = {}, lvSmoke_[kEmitters] = {}, lvDust_[kEmitters] = {};
    double lvHullHot_ = 0.0, lvHullTrail_ = 0.0;  // hull shock wake
    static constexpr int kHalos = 3;               // gamma halo size classes 2 / 8 / 32 km
    double lvHalo_[kHalos] = {};
    SURFHANDLE texHalo_ = nullptr;
    tantra::AirPath path_;
    double pathTime_ = -1e9;

    LightEmitter* light_ = nullptr;
    unsigned rng_ = 12345u;
};

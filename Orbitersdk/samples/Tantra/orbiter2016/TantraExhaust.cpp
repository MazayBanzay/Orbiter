// Tantra: anamezon exhaust visuals for Orbiter 2010 (D3D9Client).
#include "TantraExhaust.h"

#include <algorithm>
#include <cmath>

using namespace tantra;

namespace {
// Long-glow billboards come in fixed lengths (exhaust sizes cannot be changed after
// creation); the one that fits the beam's survival length in air is lit.
constexpr double kGlowLengths[TantraExhaust::kGlows] = {300.0, 1500.0, 6600.0, 30000.0, 150000.0};

// Emitter i sits at kEmit0 * 2^i behind the stern.
constexpr double kEmit0 = 25.0;
double EmitterDist(int i) { return kEmit0 * std::pow(2.0, i); }

// Particle sizes are fixed at creation, so each emitter gets the column radius typical
// for where it is used: a column of range ~ d lies in air of rho = column/d, i.e. at
// h = 8 km * ln(1.225/rho), where the hot-start interlock allows
// P = 1.2e17 W * 10^((h - 50 km)/10 km).
double TypicalRadius(const ExhaustModel& m, double d) {
    const double rho = m.Spec().absorptionColumn / d;
    const double h = (std::max)(0.0, 8000.0 * std::log(1.225 / rho));
    const double p = (std::min)(1.2e17, 1.2e17 * std::pow(10.0, (h - 50000.0) / 10000.0));
    return m.ColumnRadius(p);
}
constexpr double kGlowOffset = 80.0;  // starts past the second constriction

// D3D9Client blends beacons with alpha, not additively: a darkened colour paints a
// dark disc. Beacons therefore always get their full colour; brightness goes into size.
VECTOR3 Full(const double* rgb) { return _V(rgb[0], rgb[1], rgb[2]); }

// A beacon's sprite grows as size/distance: hide it when the camera is close enough
// for it to fill a large part of the screen.
bool TooClose(const VECTOR3& camL, const VECTOR3& pos, double size) {
    return length(camL - pos) < 40.0 * size;
}
}  // namespace

TantraExhaust::TantraExhaust(VESSEL3* vessel, const VECTOR3 mouths[kChambers], const ExhaustSpec& spec)
    : v_(vessel), model_(spec) {
    texViolet_ = oapiRegisterExhaustTexture(const_cast<char*>("Tantra_exh_violet"));
    texGrey_ = oapiRegisterExhaustTexture(const_cast<char*>("Tantra_exh_grey"));
    texGlow_ = oapiRegisterExhaustTexture(const_cast<char*>("Tantra_exh_glow"));
    texThread_ = oapiRegisterExhaustTexture(const_cast<char*>("Tantra_exh_thread"));
    texPlasma_ = oapiRegisterParticleTexture(const_cast<char*>("Tantra_plasma"));
    texWake_ = oapiRegisterParticleTexture(const_cast<char*>("Tantra_wake"));
    texDust_ = oapiRegisterParticleTexture(const_cast<char*>("Tantra_dust"));
    texHalo_ = oapiRegisterParticleTexture(const_cast<char*>("Tantra_halo"));

    sternZ_ = mouths[0].z;
    for (int i = 0; i < kChambers; ++i) {
        mouth_[i] = mouths[i];
        sternZ_ = (std::min)(sternZ_, mouths[i].z);

        for (int j = 0; j < 1; ++j) {  // cup only: constriction blobs read as artifacts
            BEACONLIGHTSPEC& b = bcn_[i][j];
            bcnPos_[i][j] = mouth_[i];
            b.shape = j == 0 ? BEACONSHAPE_COMPACT : BEACONSHAPE_DIFFUSE;
            b.pos = &bcnPos_[i][j];
            b.col = &bcnCol_[i][j];
            b.size = 2.0;
            b.falloff = 0.0;  // true size near the ship
            b.period = 0.0;
            b.active = false;
            v_->AddBeacon(&b);
        }

        EXHAUSTSPEC es = {};
        es.th = nullptr;
        es.lpos = &mouth_[i];
        es.ldir = &thrustDir_;
        es.flags = EXHAUST_CONSTANTPOS | EXHAUST_CONSTANTDIR;

        es.level = &lvGrey_[i];  // K-particle guide beam
        es.lsize = 90.0;
        es.wsize = 1.6;
        es.modulate = 0.05;
        es.tex = texGrey_;
        v_->AddExhaust(&es);

        es.level = &lvViolet_[i];  // synchrotron glow inside the magnetic nozzle
        es.lsize = spec.nozzleLength;
        es.wsize = 5.0;
        es.modulate = 0.2;
        es.tex = texViolet_;
        v_->AddExhaust(&es);

        for (int g = 0; g < kGlows; ++g) {  // long glow, several lengths
            es.level = &lvLong_[i][g];
            es.lsize = kGlowLengths[g];
            es.wsize = (std::max)(8.0, kGlowLengths[g] * 0.002);
            es.lofs = kGlowOffset;
            es.modulate = 0.1;
            es.tex = texGlow_;
            v_->AddExhaust(&es);
        }
        for (int g = 0; g < kGlows; ++g) {  // guide beam thread in air: thin, fading
            es.level = &lvThread_[i][g];
            es.lsize = kGlowLengths[g];
            es.wsize = (std::max)(1.0, kGlowLengths[g] * 0.0003);
            es.lofs = 0.0;
            es.modulate = 0.15;
            es.tex = texThread_;
            v_->AddExhaust(&es);
        }
    }

    for (int k = 0; k < kMaxKnots; ++k) {
        BEACONLIGHTSPEC& b = knot_[k];
        knotPos_[k] = _V(0, 0, sternZ_);
        b.shape = BEACONSHAPE_DIFFUSE;
        b.pos = &knotPos_[k];
        b.col = &knotCol_[k];
        b.size = 500.0;
        b.falloff = 0.85;  // visible from far away, as the design study says
        b.period = 0.0;
        b.active = false;
        v_->AddBeacon(&b);
    }

    for (int k = 0; k < kMaxPackets; ++k) {
        BEACONLIGHTSPEC& b = packet_[k];
        packetPos_[k] = _V(0, 0, sternZ_);
        b.shape = BEACONSHAPE_STAR;
        b.pos = &packetPos_[k];
        b.col = &packetCol_[k];
        b.size = 10.0;
        b.falloff = 0.6;
        b.period = 0.0;
        b.active = false;
        v_->AddBeacon(&b);
    }

    for (int e = 0; e < kEmitters; ++e) {
        const double d = EmitterDist(e), r = TypicalRadius(model_, d);
        const VECTOR3 pos = _V(0, 0, sternZ_ - d), back = _V(0, 0, -1);
        // Plasma: short-lived, fast-growing; the white core fades to a red rim.
        // Emitted aft at a speed that carries each particle to the next emitter (2d)
        // within its life, and kept moving with the ship: a continuous column in the
        // ship's frame instead of blobs at the emitter points.
        PARTICLESTREAMSPEC fire = {0, r * 0.8, 40.0, d / 1.5, 0.1, 1.5, r * 0.2, 0.0, PARTICLESTREAMSPEC::EMISSIVE,
                                   PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texPlasma_};
        v_->AddParticleStream(&fire, pos, back, &lvFire_[e]);
        // Shock wake: hot air and condensation, lingering and slowly spreading. Drawn
        // emissive: D3D9Client shades sunlit particles black on their far side, and this
        // air is lit by the column anyway.
        // (wake and ejecta brake against the air: they stay behind as a trail)
        PARTICLESTREAMSPEC wake = {0, r, 15.0, 0.0, 0.5, 30.0, r * 0.05, 3.0, PARTICLESTREAMSPEC::EMISSIVE,
                                   PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texWake_};
        v_->AddParticleStream(&wake, pos, back, &lvSmoke_[e]);
        // Ejecta where the beam reaches the ground: denser, thrown up, settling slower.
        PARTICLESTREAMSPEC dust = {0, r * 0.6, 5.0, r * 0.05, 0.8, 40.0, r * 0.04, 3.0, PARTICLESTREAMSPEC::EMISSIVE,
                                   PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texDust_};
        v_->AddParticleStream(&dust, pos, _V(0, 1, 0), &lvDust_[e]);
    }

    // Gamma halo around the chambers: moves with the ship (no air braking).
    for (int h = 0; h < kHalos; ++h) {
        const double size = 2000.0 * std::pow(4.0, h);
        PARTICLESTREAMSPEC halo = {0, size, 12.0, 0.0, 0.0, 0.5, 0.0, 0.0, PARTICLESTREAMSPEC::EMISSIVE,
                                   PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texHalo_};
        v_->AddParticleStream(&halo, _V(0, 0, sternZ_ + 20.0), _V(0, 0, -1), &lvHalo_[h]);
    }

    // Hull shock wake: shock-heated air from the nose back, then a lingering trail.
    {
        const VECTOR3 mid = _V(0, 0, 0), back = _V(0, 0, -1);
        PARTICLESTREAMSPEC hot = {0, 40.0, 40.0, 0.0, 0.2, 1.0, 120.0, 3.0, PARTICLESTREAMSPEC::EMISSIVE,
                                  PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texPlasma_};
        v_->AddParticleStream(&hot, mid, back, &lvHullHot_);
        PARTICLESTREAMSPEC trail = {0, 60.0, 15.0, 0.0, 0.3, 25.0, 12.0, 3.0, PARTICLESTREAMSPEC::EMISSIVE,
                                    PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, texWake_};
        v_->AddParticleStream(&trail, mid, back, &lvHullTrail_);
    }

    const COLOUR4 dif = {0.85f, 0.7f, 1.0f, 0.0f}, spec0 = {0.6f, 0.5f, 0.8f, 0.0f}, amb = {0.05f, 0.04f, 0.07f, 0.0f};
    light_ = v_->AddPointLight(_V(0, 0, sternZ_ - 15.0), 3000.0, 1.0, 5e-4, 1e-6, dif, spec0, amb);
    if (light_) light_->Activate(false);
    const COLOUR4 fdif = {1.0f, 0.8f, 0.6f, 0.0f}, fspec = {0.8f, 0.6f, 0.5f, 0.0f}, famb = {0.1f, 0.07f, 0.05f, 0.0f};
    fireLight_ = v_->AddPointLight(_V(0, 0, sternZ_ - 1000.0), 20000.0, 1.0, 0.0, 2e-8, fdif, fspec, famb);
    if (fireLight_) fireLight_->Activate(false);

    // Let the beacons (knots hundreds of km aft) stay visible far beyond the mesh.
    v_->SetVisibilityLimit(1e-6, 1e-3);
}

TantraExhaust::~TantraExhaust() {
    for (auto& row : bcn_) v_->DelBeacon(&row[0]);
    for (auto& b : knot_) v_->DelBeacon(&b);
    for (auto& b : packet_) v_->DelBeacon(&b);
    if (fireLight_) v_->DelLightEmitter(fireLight_);
    if (light_) v_->DelLightEmitter(light_);
}

tantra::AirPath TantraExhaust::TraceBeam() const {
    tantra::AirPath path;
    ATMPARAM here;
    OBJHANDLE planet = nullptr;
    oapiGetAtm(v_->GetHandle(), &here, &planet);
    if (!planet) return path;
    const ATMCONST* atm = oapiGetPlanetAtmConstants(planet);
    if (!atm) return path;

    VECTOR3 start, dir, pc;
    v_->Local2Global(_V(0, 0, sternZ_), start);
    v_->GlobalRot(_V(0, 0, -1), dir);
    oapiGetGlobalPos(planet, &pc);
    const VECTOR3 r0 = start - pc;
    const double surface = oapiGetSize(planet), column = model_.Spec().absorptionColumn;

    double s = 0.0, acc = 0.0;
    while (s < 3.0e6) {
        const VECTOR3 r = r0 + dir * s;
        const double rad = length(r);
        if (rad >= atm->radlimit && dotp(r, dir) > 0.0) break;  // left the atmosphere
        ATMPARAM p;
        oapiGetPlanetAtmParams(planet, rad, &p);
        if (rad <= surface) {
            path.absorbDist = s;
            path.depositDensity = p.rho;
            path.ground = true;
            return path;
        }
        const double ds = std::clamp(0.05 * column / (std::max)(p.rho, 1e-9), 5.0, 20000.0);
        acc += p.rho * ds;
        if (acc >= column) {
            path.absorbDist = s + ds * (1.0 - (acc - column) / (p.rho * ds));
            path.depositDensity = p.rho;
            return path;
        }
        s += ds;
    }
    return path;  // escapes: vacuum-like exhaust
}

void TantraExhaust::Update(double fieldLevel, double beamLevel, double feed, double airDensity, double jetPower,
                           double drag, double airspeed, double soundSpeed) {
    const HullWake hw = EvaluateHullWake(drag, airDensity, airspeed, soundSpeed);
    lvHullHot_ = hw.hot;
    lvHullTrail_ = hw.trail;

    rng_ = rng_ * 1664525u + 1013904223u;
    const double flicker = (rng_ >> 8) / double(1u << 24);
    const double t = oapiGetSimTime();
    if (feed <= 0.0 && beamLevel <= 0.05) {
        path_ = tantra::AirPath();
    } else if (std::abs(t - pathTime_) > 0.1) {
        path_ = TraceBeam();
        pathTime_ = t;
    }
    frame_ = model_.Evaluate(fieldLevel, beamLevel, feed, airDensity, flicker, path_, jetPower);
    const ExhaustFrame& f = frame_;
    VECTOR3 camG, camL;
    oapiCameraGlobalPos(&camG);
    v_->Global2Local(camG, camL);

    // Relativistic beaming: the jet (synchrotron light, packets, knots) is seen far
    // brighter and bluer from behind than from the side, and barely from the front.
    // Air glowing in the channel and the plasma ball are at rest: not beamed.
    const VECTOR3 toCam = camL - _V(0, 0, sternZ_);
    const double camDist = length(toCam);
    const double cosT = camDist > 0.0 ? -toCam.z / camDist : 1.0;  // the jet flows along -z
    const double delta = ExhaustModel::DopplerFactor(model_.Spec().exhaustBeta, cosT);
    const double beam = ExhaustModel::BeamingLevel(delta);
    const bool inAir = f.channel;  // the air channel is at rest: not beamed
    const double shift = std::clamp(std::log10(delta), -1.0, 1.0);  // blue behind, red aside
    const double blue[3] = {0.8, 0.85, 1.0}, red[3] = {1.0, 0.45, 0.35};
    auto Tint = [&](const double* rgb) {
        const double* to = shift > 0.0 ? blue : red;
        const double t = std::abs(shift) * 0.6;
        return _V(rgb[0] + (to[0] - rgb[0]) * t, rgb[1] + (to[1] - rgb[1]) * t, rgb[2] + (to[2] - rgb[2]) * t);
    };

    // Billboard lengths are fixed: take the one nearest (in log) to the wanted length.
    // The channel ends at the absorption point; the thread texture fades over 3 e-folds.
    auto Nearest = [](double want) {
        int best = 0;
        for (int g = 1; g < kGlows; ++g)
            if (std::abs(std::log(kGlowLengths[g] / want)) < std::abs(std::log(kGlowLengths[best] / want))) best = g;
        return best;
    };
    const int glow = Nearest((std::max)(f.longGlowLength, 1.0));
    const int thread = Nearest((std::max)(3.0 * f.threadLength, 1.0));

    for (int i = 0; i < kChambers; ++i) {
        BEACONLIGHTSPEC& cup = bcn_[i][0];
        bcnCol_[i][0] = Full(f.cup.rgb);
        cup.size = f.cup.size * f.cup.level;
        cup.active = f.cup.level > 0.01;
        lvGrey_[i] = f.columnGrey;
        lvViolet_[i] = f.columnViolet * beam;
        const double longLv = inAir ? f.longGlow : f.longGlow * beam;
        for (int g = 0; g < kGlows; ++g) {
            lvLong_[i][g] = g == glow ? longLv : 0.0;
            lvThread_[i][g] = g == thread ? f.thread : 0.0;
        }
    }

    for (int k = 0; k < kMaxKnots; ++k) {
        BEACONLIGHTSPEC& b = knot_[k];
        if (k < static_cast<int>(f.knots.size())) {
            const Glow& g = f.knots[k];
            knotPos_[k] = _V(0, 0, sternZ_ - g.dist);
            knotCol_[k] = Tint(g.rgb);
            b.size = g.size * g.level * beam;
            b.active = g.level > 0.05 && !TooClose(camL, knotPos_[k], b.size);
        } else {
            b.active = false;
        }
    }

    for (int k = 0; k < kMaxPackets; ++k) {
        BEACONLIGHTSPEC& b = packet_[k];
        if (k < static_cast<int>(f.packets.size())) {
            const Glow& g = f.packets[k];
            packetPos_[k] = _V(0, 0, sternZ_ - g.dist);
            packetCol_[k] = Tint(g.rgb);
            b.size = g.size * g.level * beam;
            b.active = g.level > 0.05 && !TooClose(camL, packetPos_[k], b.size);
        } else {
            b.active = false;
        }
    }

    // Atmosphere: plasma column from the stern (deposition ~ exp(-s/L)), its light,
    // wake along it, and ejecta where it meets the ground.
    const Glow& col = f.fireball;
    const bool column = col.level > 0.01 && col.size > 0.0;
    fireLightPos_ = _V(0, 0, sternZ_ - 0.5 * col.dist);
    if (fireLight_) {
        fireLight_->Activate(column);
        if (column) {
            fireLight_->SetPosition(fireLightPos_);
            fireLight_->SetIntensity(col.level);
        }
    }
    int ground = -1;
    if (column && f.dust > 0.0) {
        double best = 1e9;
        for (int e = 0; e < kEmitters; ++e) {
            const double gap = std::abs(std::log(EmitterDist(e) / (std::max)(f.columnEnd, 1.0)));
            if (gap < best) best = gap, ground = e;
        }
    }
    for (int e = 0; e < kEmitters; ++e) {
        const double d = EmitterDist(e);
        // Even deposition along the muon range, tapering over the last third.
        const double w = column ? std::clamp((f.columnEnd - d) / (0.3 * f.columnEnd), 0.0, 1.0) : 0.0;
        lvFire_[e] = col.level * w;
        lvSmoke_[e] = f.smoke * w;
        lvDust_[e] = e == ground ? f.dust : 0.0;
    }

    // Gamma halo: the size class nearest to the attenuation length.
    int halo = 0;
    for (int h = 1; h < kHalos; ++h)
        if (std::abs(std::log(2000.0 * std::pow(4.0, h) / (std::max)(f.haloRadius, 1.0))) <
            std::abs(std::log(2000.0 * std::pow(4.0, halo) / (std::max)(f.haloRadius, 1.0))))
            halo = h;
    for (int h = 0; h < kHalos; ++h) lvHalo_[h] = h == halo ? 0.8 * f.halo : 0.0;

    if (light_) {
        light_->Activate(f.light > 0.01);
        light_->SetIntensity(f.light);
    }
}

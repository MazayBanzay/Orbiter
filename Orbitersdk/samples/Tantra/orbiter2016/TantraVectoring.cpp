// Tantra - the pods' own thrust and УВТ (управление вектором тяги), the bridge's throttle quadrant (variant 7 of the bridge mockup,
// Tantra_Design/bridge_variants/v7.js; the user, 2026-10-04: «4 выдвижных блока с УВТ», the yoke mixes the pods; the bridge session, agreed
// with the fork). The ВЫДВ. БЛОКИ lever sets the pods' common thrust (podCmd_, locked at 0 until they are fully out); every cup follows
// it at 3 /s, in the hover (cups 45..135 deg, THGROUP_HOVER) and with the cups aft alike. Not while the pods help the carriage
// (PodAssistLevels sets every cup then). With УВТ on (the button on the yoke's right horn) the common thrust is held to 85 % (the
// reserve for the control) and the pilot's input adds the difference between the pods - the roll (the ailerons, Num 1 / 3): the
// left pods against the right ones; the pitch (the elevator, Num 2 / 8): the fore pair against the aft one; the yaw (the RCS yaw,
// Num 4 / 6): the left against the right - by thrust where it acts (the roll and the pitch in the hover, sin of the cups' angle;
// the yaw with the cups aft, cos of it). The mockup also leaned the cups differentially (±14 deg); the pods swivel together here
// (Tantra::UpdatePods), so that part is not carried over.
#include "Tantra.h"
#include "MeshLayout.h"

#include <algorithm>
#include <cmath>

namespace sp = tantra::spec;

void Tantra::PodVectoring(double simdt) {
    if (podOut_ < 1.0) podCmd_ = 0.0;                                    // the lever is locked while the pods are not fully out
    if (podAssist_) {                                                    // the carriage's assist drives the cups
        for (int k = 0; k < sp::kPodCups; ++k) podLv_[k] = GetThrusterLevel(pod_[k]);
        return;
    }
    const bool uvt = interior_.UvtOn() && podOut_ >= 1.0 && podAimed_;
    const double coll = uvt ? (std::min)(podCmd_, 0.85) : podCmd_;
    double R = 0.0, P = 0.0, Y = 0.0;
    if (uvt) {
        R = GetControlSurfaceLevel(AIRCTRL_AILERON);                     // the seat's keys set these (SeatKeys): + bank right
        P = GetControlSurfaceLevel(AIRCTRL_ELEVATOR);                    // + nose up
        Y = GetThrusterGroupLevel(THGROUP_ATT_YAWRIGHT) - GetThrusterGroupLevel(THGROUP_ATT_YAWLEFT);   // + nose right
    }
    const double a = podAngle_ * RAD, s = std::sin(a), c = std::cos(a);
    const double k = (std::min)(1.0, (std::max)(simdt, 0.0) * 3.0);
    // (2026-10-09) the hover's automatic balance: the Moon test rolled the hull 47 deg in 20 s and laid it on its side - the
    // pods thrust level with the CG (no restoring moment) and the RCS gives ~5e7 N m against ~1e9 needed. The local vertical
    // in the ship's axes holds the hull level: up to ±50 % of every cup, the left pods against the right (roll) and the fore
    // pair against the aft (pitch); the gains from the moment the pods have and the hull's inertia (a critically damped
    // loop at 0.6 rad/s in roll, 0.35 rad/s in pitch). The pilot's УВТ adds to it.
    double bR = 0.0, bP = 0.0;
    const bool hoverSet = podOut_ >= 1.0 && podAimed_ && podAngle_ >= 45.0 && podAngle_ <= 135.0;
    if (hoverSet && podCmd_ > 0.02) {
        VECTOR3 up;
        HorizonInvRot(_V(0, 1, 0), up);
        const double dt = (std::max)(simdt, 1e-4), f = (std::min)(1.0, dt * 8.0);
        if (hovOn_) {
            hovRate_[0] += ((up.x - hovUp_[0]) / dt - hovRate_[0]) * f;
            hovRate_[1] += ((up.z - hovUp_[1]) / dt - hovRate_[1]) * f;
        } else hovRate_[0] = hovRate_[1] = 0.0;
        hovUp_[0] = up.x; hovUp_[1] = up.z; hovOn_ = true;
        double F = 0.0;
        for (int i = 0; i < sp::kPodCups; ++i) F += GetThrusterMax0(pod_[i]);
        VECTOR3 pmi;
        GetPMI(pmi);
        const double m = GetMass(), xArm = std::fabs(tantra::mesh::kPods[0].pivot.x);
        const double zArm = 0.5 * (tantra::mesh::kPods[2].s - tantra::mesh::kPods[0].s);
        const double aR = 0.5 * F * s * xArm / (pmi.z * m), aP = 0.5 * F * s * zArm / (pmi.x * m);   // rad/s^2 at full swing
        if (aR > 1e-6 && aP > 1e-6) {
            const double wR = 0.6, wP = 0.35;
            bR = (std::max)(-1.0, (std::min)(1.0, (wR * wR * up.x + 2.0 * wR * hovRate_[0]) / aR));       // + roll right
            bP = (std::max)(-1.0, (std::min)(1.0, -(wP * wP * up.z + 2.0 * wP * hovRate_[1]) / aP));      // + nose up
        }
    } else hovOn_ = false;
    hovCmd_[0] = bR; hovCmd_[1] = bP;
    for (int p = 0; p < sp::kPodCount; ++p) {
        const double side = tantra::mesh::kPods[p].pivot.x < 0.0 ? -1.0 : 1.0;      // left -1, right +1
        const double fore = p >= 2 ? 1.0 : -1.0;                                    // kPods: the aft pair first, then the fore pair
        // the roll right: the left pods push more (in the hover they lift the left side); the pitch up: the fore pair; the yaw
        // right: the left pods push more forward (with the cups aft)
        const double dT = 0.15 * (-side * R * s + fore * P * s - side * Y * c) + 0.5 * (-side * bR + fore * bP);
        const double want = (std::max)(0.0, (std::min)(1.0, coll + dT));
        for (int cc = 0; cc < sp::kCupsPerPod; ++cc) {
            const int i = p * sp::kCupsPerPod + cc;
            podLv_[i] += (want - podLv_[i]) * k;
            SetThrusterLevel(pod_[i], podLv_[i]);
        }
    }
}

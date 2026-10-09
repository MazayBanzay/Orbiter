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
    for (int p = 0; p < sp::kPodCount; ++p) {
        const double side = tantra::mesh::kPods[p].pivot.x < 0.0 ? -1.0 : 1.0;      // left -1, right +1
        const double fore = p >= 2 ? 1.0 : -1.0;                                    // kPods: the aft pair first, then the fore pair
        // the roll right: the left pods push more (in the hover they lift the left side); the pitch up: the fore pair; the yaw
        // right: the left pods push more forward (with the cups aft)
        const double dT = 0.15 * (-side * R * s + fore * P * s - side * Y * c);
        const double want = (std::max)(0.0, (std::min)(1.0, coll + dT));
        for (int cc = 0; cc < sp::kCupsPerPod; ++cc) {
            const int i = p * sp::kCupsPerPod + cc;
            podLv_[i] += (want - podLv_[i]) * k;
            SetThrusterLevel(pod_[i], podLv_[i]);
        }
    }
}
